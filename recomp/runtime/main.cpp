// Host entry point: maps the guest address space, loads the XEX image, wires the
// recompiled function table and starts the game's entry point on a guest thread.
#include "gpu.h"
#include "guest.h"
#include "launcher.h"
#include "setup.h"
#ifdef CD_DARKLAUNCHER
#include "dl_bridge.h"
#endif
#include "xex_header.h"

#include <windows.h>
#ifdef _WIN32
#include <shellapi.h>
#endif

#include <filesystem>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <file.h>
#include <image.h>

uint8_t* g_base;

static FILE* g_logFile;
static std::mutex g_logMutex;

void Log(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::lock_guard lock(g_logMutex);
    fprintf(stderr, "%s\n", buffer);
    if (g_logFile != nullptr)
    {
        fprintf(g_logFile, "%s\n", buffer);
        fflush(g_logFile);
    }
}

// ---- Guest memory ----

static std::mutex g_memoryMutex;

constexpr uint32_t kPhysicalSize = 0x20000000;

bool GuestCommit(uint32_t address, uint32_t size)
{
    // The physical range is a mapped section and is always committed.
    if (size == 0 || address >= kPhysicalHeapBase)
        return true;
    return VirtualAlloc(g_base + address, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void GuestDecommit(uint32_t address, uint32_t size)
{
    if (size != 0 && address < kPhysicalHeapBase)
        VirtualFree(g_base + address, size, MEM_DECOMMIT);
}

// Reserves the 4 GB guest space. The console exposes its 512 MB of physical
// memory at 0xA0000000, 0xC0000000 and 0xE0000000 (one view per page size), and
// the game uses those addresses interchangeably, so all three are views of one
// section.
static bool ReserveGuestMemory()
{
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kPhysicalSize, nullptr);
    if (section == nullptr)
        return false;

    for (int attempt = 0; attempt < 16; attempt++)
    {
        // Find a free 4 GB hole, release it, then claim it piecewise. Another
        // allocation can race in between, hence the retries.
        auto* base = (uint8_t*)VirtualAlloc(nullptr, PPC_MEMORY_SIZE + 0x10000, MEM_RESERVE, PAGE_NOACCESS);
        if (base == nullptr)
            return false;
        VirtualFree(base, 0, MEM_RELEASE);

        if (VirtualAlloc(base, kPhysicalHeapBase, MEM_RESERVE, PAGE_NOACCESS) == nullptr)
            continue;

        bool mapped = true;
        for (uint32_t view = 0; view < 3; view++)
        {
            uint8_t* address = base + kPhysicalHeapBase + uint64_t(view) * kPhysicalSize;
            mapped = mapped && MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, kPhysicalSize, address) == address;
        }
        if (mapped)
        {
            g_base = base;
            return true;
        }

        for (uint32_t view = 0; view < 3; view++)
            UnmapViewOfFile(base + kPhysicalHeapBase + uint64_t(view) * kPhysicalSize);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return false;
}

// First-fit allocator over one guest address range, in 4 KB pages. Freed
// ranges are merged with their neighbours and handed out again.
class RangeAllocator
{
public:
    RangeAllocator(uint32_t base, uint32_t end)
    {
        free_[base] = end - base;
    }

    uint32_t Allocate(uint32_t size, uint32_t alignment)
    {
        size = (size + 0xFFF) & ~0xFFFu;
        if (size == 0)
            size = 0x1000;
        for (auto it = free_.begin(); it != free_.end(); ++it)
        {
            uint64_t start = it->first;
            uint64_t blockEnd = start + it->second;
            uint64_t aligned = (start + alignment - 1) & ~uint64_t(alignment - 1);
            if (aligned + size > blockEnd)
                continue;

            // Keep what is left before and after the allocation.
            free_.erase(it);
            if (aligned > start)
                free_[uint32_t(start)] = uint32_t(aligned - start);
            if (aligned + size < blockEnd)
                free_[uint32_t(aligned + size)] = uint32_t(blockEnd - (aligned + size));
            used_[uint32_t(aligned)] = size;
            return uint32_t(aligned);
        }
        return 0;
    }

    // Returns the size of the freed allocation, or 0 if the address is not the
    // start of one.
    uint32_t Free(uint32_t address)
    {
        auto it = used_.find(address);
        if (it == used_.end())
            return 0;
        uint32_t size = it->second;
        used_.erase(it);

        uint32_t start = address;
        uint32_t length = size;
        auto next = free_.lower_bound(address);
        if (next != free_.end() && next->first == start + length)
        {
            length += next->second;
            next = free_.erase(next);
        }
        if (next != free_.begin())
        {
            auto previous = std::prev(next);
            if (previous->first + previous->second == start)
            {
                start = previous->first;
                length += previous->second;
                free_.erase(previous);
            }
        }
        free_[start] = length;
        return size;
    }

private:
    std::map<uint32_t, uint32_t> free_; // start -> size
    std::map<uint32_t, uint32_t> used_; // start -> size
};

static RangeAllocator g_virtualRange(kVirtualHeapBase, kVirtualHeapEnd);
static RangeAllocator g_physicalRange(kPhysicalHeapBase, kPhysicalHeapEnd);

uint32_t GuestAllocVirtual(uint32_t size, uint32_t alignment)
{
    std::lock_guard lock(g_memoryMutex);
    uint32_t address = g_virtualRange.Allocate(size, alignment);
    if (address == 0)
        return 0;
    // Freshly committed pages are zero, as freeing decommits them.
    if (!GuestCommit(address, (size + 0xFFF) & ~0xFFFu))
    {
        g_virtualRange.Free(address);
        return 0;
    }
    return address;
}

bool GuestFreeVirtual(uint32_t address)
{
    std::lock_guard lock(g_memoryMutex);
    uint32_t size = g_virtualRange.Free(address);
    if (size == 0)
        return false;
    GuestDecommit(address, size);
    return true;
}

uint32_t GuestAllocPhysical(uint32_t size, uint32_t alignment)
{
    std::lock_guard lock(g_memoryMutex);
    uint32_t address = g_physicalRange.Allocate(size, alignment);
    // The physical range is a mapped section that cannot be decommitted, so a
    // reused block is cleared by hand.
    if (address != 0)
        memset(g_base + address, 0, (size + 0xFFF) & ~0xFFFu);
    return address;
}

bool GuestFreePhysical(uint32_t address)
{
    std::lock_guard lock(g_memoryMutex);
    return g_physicalRange.Free(kPhysicalHeapBase | (address & 0x1FFFFFFF)) != 0;
}

static RangeAllocator g_stackRange(kStackBase, kStackEnd);

uint32_t GuestAllocStack(uint32_t size)
{
    // Leave an uncommitted guard gap below each stack so overflows fault.
    std::lock_guard lock(g_memoryMutex);
    uint32_t address = g_stackRange.Allocate(size + 0x10000, 0x10000);
    if (address == 0)
        return 0;
    if (!GuestCommit(address + 0x10000, size))
    {
        g_stackRange.Free(address);
        return 0;
    }
    return address + 0x10000;
}

void GuestFreeStack(uint32_t stack)
{
    std::lock_guard lock(g_memoryMutex);
    uint32_t size = g_stackRange.Free(stack - 0x10000);
    if (size != 0)
        GuestDecommit(stack, size - 0x10000);
}

// ---- Imports ----

static std::atomic<bool> g_importWarned[512];

void RegisterImport(const char* name, ImportHandler handler)
{
    for (size_t i = 0; i < g_importCount; i++)
    {
        if (strcmp(g_importNames[i], name) == 0)
        {
            g_importHandlers[i] = handler;
            return;
        }
    }
    fprintf(stderr, "RegisterImport: %s is not imported by the game\n", name);
}

void ImportDispatch(size_t index, PPCContext& ctx, uint8_t* base)
{
    ImportHandler handler = g_importHandlers[index];
    if (handler != nullptr)
    {
        handler(ctx, base);
        return;
    }

    if (!g_importWarned[index].exchange(true))
    {
        Log("UNIMPLEMENTED import %s (r3=%08X r4=%08X r5=%08X r6=%08X lr=%08X)",
            g_importNames[index], ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, uint32_t(ctx.lr));
    }
    ctx.r3.u64 = 0;
}

PPCFunc* GuestResolveIndirect(uint8_t* base, uint32_t address)
{
    if (address >= PPC_CODE_BASE && address < PPC_CODE_BASE + PPC_CODE_SIZE)
    {
        PPCFunc* function = PPC_LOOKUP_FUNC(base, address);
        if (function != nullptr)
        {
            // CD_TRACE_CALLS=<from>-<to> (hex): log each distinct indirect call
            // made from that range of guest code, with its target.
            static const char* range = getenv("CD_TRACE_CALLS");
            if (range != nullptr && t_guestContext != nullptr)
            {
                static const uint32_t from = uint32_t(strtoul(range, nullptr, 16));
                static const uint32_t to = strchr(range, '-') ? uint32_t(strtoul(strchr(range, '-') + 1, nullptr, 16)) : 0;
                uint32_t caller = uint32_t(t_guestContext->lr);
                if (caller >= from && caller < to)
                {
                    static std::mutex mutex;
                    static std::vector<uint64_t> seen;
                    std::lock_guard lock(mutex);
                    uint64_t key = (uint64_t(caller) << 32) | address;
                    if (seen.size() < 64 && std::find(seen.begin(), seen.end(), key) == seen.end())
                    {
                        seen.push_back(key);
                        Log("Indirect call from %08X to %08X (r3=%08X r4=%08X r5=%08X)", caller - 4, address,
                            t_guestContext->r3.u32, t_guestContext->r4.u32, t_guestContext->r5.u32);
                    }
                }
            }
            return function;
        }
    }

    Log("FATAL: indirect call to %08X, which is not a recompiled function", address);
    ExitProcess(4);
}

// ---- XEX header ----

XexInfo g_xex;

static uint32_t FileU32(const std::vector<uint8_t>& file, size_t offset)
{
    uint32_t value;
    memcpy(&value, file.data() + offset, 4);
    return __builtin_bswap32(value);
}

uint32_t XexFindHeader(uint32_t key)
{
    uint32_t count = Load32(g_xex.headerAddress + 0x14);
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t entry = g_xex.headerAddress + 0x18 + i * 8;
        if (Load32(entry) != key)
            continue;

        switch (key & 0xFF)
        {
        case 0x00: return Load32(entry + 4);                    // value stored inline
        case 0x01: return entry + 4;                            // pointer to inline value
        default: return g_xex.headerAddress + Load32(entry + 4); // pointer to data in header
        }
    }
    return 0;
}

// ---- Guest threads ----

uint32_t GuestCreateThreadBlock(uint32_t stackSize, uint32_t& stackTop)
{
    stackSize = (stackSize + 0xFFFF) & ~0xFFFFu;
    uint32_t stack = GuestAllocStack(stackSize);
    uint32_t block = GuestAllocVirtual(0x2000);
    if (stack == 0 || block == 0)
        return 0;

    uint32_t pcr = block;
    uint32_t thread = block + 0x1000;
    uint32_t tls = 0;
    if (g_xex.tlsDataSize != 0)
    {
        tls = GuestAllocVirtual(g_xex.tlsDataSize);
        if (g_xex.tlsRawDataSize != 0)
            memcpy(g_base + tls, g_base + g_xex.tlsRawDataAddress, g_xex.tlsRawDataSize);
    }

    stackTop = stack + stackSize;
    Store32(pcr + 0x00, tls);
    Store32(pcr + 0x70, stackTop);
    Store32(pcr + 0x74, stack);
    Store32(pcr + 0x100, thread);

    // KTHREAD thread id; the game's locks compare owners by this value.
    static std::atomic<uint32_t> nextThreadId{ 0x100 };
    Store32(thread + 0x14C, nextThreadId.fetch_add(4));
    return pcr;
}

thread_local PPCContext* t_guestContext;

struct RegisteredContext
{
    DWORD threadId;
    PPCContext* ctx;
};
static std::mutex g_contextMutex;
static std::vector<RegisteredContext> g_contexts;

void GuestRegisterContext(PPCContext* ctx)
{
    t_guestContext = ctx;
    std::lock_guard lock(g_contextMutex);
    g_contexts.push_back({ GetCurrentThreadId(), ctx });
}

void GuestUnregisterContext()
{
    t_guestContext = nullptr;
    std::lock_guard lock(g_contextMutex);
    DWORD threadId = GetCurrentThreadId();
    std::erase_if(g_contexts, [threadId](const RegisteredContext& entry) { return entry.threadId == threadId; });
}

static bool GuestReadable(uint32_t address)
{
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(g_base + address, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT;
}

// Registers may be slightly stale inside the faulting function, since the
// compiler keeps some of them in host registers between calls.
static void DumpGuestState(PPCContext* ctx = t_guestContext)
{
    if (ctx == nullptr)
        return;

    Log("  r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X",
        ctx->r3.u32, ctx->r4.u32, ctx->r5.u32, ctx->r6.u32, ctx->r7.u32, ctx->r8.u32, ctx->r9.u32, ctx->r10.u32);
    Log("  r11=%08X r28=%08X r29=%08X r30=%08X r31=%08X r1=%08X lr=%08X ctr=%08X",
        ctx->r11.u32, ctx->r28.u32, ctx->r29.u32, ctx->r30.u32, ctx->r31.u32, ctx->r1.u32, uint32_t(ctx->lr), ctx->ctr.u32);

    // Each frame's back chain points at the caller's frame; the callee saved
    // its return address 8 bytes below that.
    std::string trace;
    uint32_t frame = ctx->r1.u32;
    for (int depth = 0; depth < 24 && frame != 0 && GuestReadable(frame); depth++)
    {
        uint32_t caller = Load32(frame);
        if (caller <= frame || !GuestReadable(caller - 8))
            break;
        char entry[16];
        snprintf(entry, sizeof(entry), " %08X", Load32(caller - 8));
        trace += entry;
        frame = caller;
    }
    Log("  guest return addresses:%s", trace.c_str());
}

void LogGuestBacktrace(const char* label)
{
    PPCContext* ctx = t_guestContext;
    if (ctx == nullptr)
        return;

    char trace[24 * 9 + 1] = "";
    int count = 0;
    uint32_t frame = ctx->r1.u32;
    while (count < 24 && frame != 0 && GuestReadable(frame))
    {
        uint32_t caller = Load32(frame);
        if (caller <= frame || !GuestReadable(caller - 8))
            break;
        snprintf(trace + count * 9, 10, " %08X", Load32(caller - 8));
        count++;
        frame = caller;
    }
    Log("%s: lr %08X, returns:%s", label, uint32_t(ctx->lr), trace);
}

uint32_t GuestCall(uint32_t address, uint32_t r3, uint32_t r4, int processor)
{
    struct HostThreadGuest
    {
        PPCContext ctx{};
        uint32_t pcr{};
        uint32_t stackTop{};
    };
    static thread_local HostThreadGuest* state;
    if (state == nullptr)
    {
        state = new HostThreadGuest();
        state->pcr = GuestCreateThreadBlock(0x10000, state->stackTop);
        state->ctx.fpscr.loadFromHost();
        GuestRegisterContext(&state->ctx);
    }

    PPCContext& ctx = state->ctx;
    if (processor >= 0)
        g_base[state->pcr + 0x10C] = uint8_t(processor);
    ctx.r1.u64 = state->stackTop - 0x100;
    ctx.r13.u64 = state->pcr;
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    GuestResolveIndirect(g_base, address)(ctx, g_base);
    return ctx.r3.u32;
}

#ifdef _WIN32
// CD_SAMPLE=<seconds>: after that delay, log where every guest thread is.
static void SamplerThread(int seconds)
{
    Sleep(DWORD(seconds) * 1000);

    // CD_PEEK=<hex>,<hex>,...: guest words to print, following one pointer level.
    if (const char* peek = getenv("CD_PEEK"))
    {
        for (const char* p = peek; *p != 0;)
        {
            char* end;
            uint32_t address = uint32_t(strtoul(p, &end, 16));
            if (end == p)
                break;
            if (GuestReadable(address))
            {
                uint32_t value = Load32(address);
                if (GuestReadable(value) && value >= 0x10000)
                    Log("PEEK %08X = %08X -> %08X %08X %08X %08X", address, value,
                        Load32(value), Load32(value + 4), Load32(value + 8), Load32(value + 12));
                else
                    Log("PEEK %08X = %08X", address, value);
            }
            p = *end == ',' ? end + 1 : end;
        }
    }

    // A suspended thread may hold the heap or the log lock, so nothing is
    // allocated or logged until every thread has been resumed.
    struct Snapshot
    {
        DWORD threadId;
        uintptr_t rip;
        uint32_t r3, r4, r5, r1, r31, lr;
        uint32_t trace[24];
        int traceCount;
    };
    static Snapshot snapshots[48];
    int count = 0;

    {
        std::lock_guard lock(g_contextMutex);
        for (auto& entry : g_contexts)
        {
            if (count == 48)
                break;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.threadId);
            if (thread == nullptr)
                continue;

            Snapshot& snapshot = snapshots[count++];
            snapshot = {};
            snapshot.threadId = entry.threadId;

            SuspendThread(thread);
            CONTEXT host{};
            host.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(thread, &host))
                snapshot.rip = uintptr_t(host.Rip);
            snapshot.r3 = entry.ctx->r3.u32;
            snapshot.r4 = entry.ctx->r4.u32;
            snapshot.r5 = entry.ctx->r5.u32;
            snapshot.r1 = entry.ctx->r1.u32;
            snapshot.r31 = entry.ctx->r31.u32;
            snapshot.lr = uint32_t(entry.ctx->lr);

            uint32_t frame = snapshot.r1;
            while (snapshot.traceCount < 24 && frame != 0 && GuestReadable(frame))
            {
                uint32_t caller = Load32(frame);
                if (caller <= frame || !GuestReadable(caller - 8))
                    break;
                snapshot.trace[snapshot.traceCount++] = Load32(caller - 8);
                frame = caller;
            }
            ResumeThread(thread);
            CloseHandle(thread);
        }
    }

    uintptr_t imageBase = uintptr_t(GetModuleHandleW(nullptr));
    for (int i = 0; i < count; i++)
    {
        const Snapshot& snapshot = snapshots[i];
        char trace[24 * 9 + 1] = "";
        for (int k = 0; k < snapshot.traceCount; k++)
            snprintf(trace + k * 9, 10, " %08X", snapshot.trace[k]);
        Log("SAMPLE thread %lu: image offset 0x%zX r3=%08X r4=%08X r5=%08X r31=%08X r1=%08X lr=%08X; returns:%s",
            snapshot.threadId, size_t(snapshot.rip - imageBase), snapshot.r3, snapshot.r4, snapshot.r5, snapshot.r31,
            snapshot.r1, snapshot.lr, trace);
    }
}

static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* info)
{
    auto* record = info->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    // The recompiled function containing the faulting host instruction is the
    // one with the closest preceding host address.
    uint32_t guestFunction = 0;
    uintptr_t best = 0;
    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        uintptr_t host = uintptr_t(PPCFuncMappings[i].host);
        if (host <= uintptr_t(record->ExceptionAddress) && host > best)
        {
            best = host;
            guestFunction = uint32_t(PPCFuncMappings[i].guest);
        }
    }

    // Feed this to: llvm-symbolizer --obj=crystal_defenders.exe --relative-address <rva>
    Log("Crash at image offset 0x%zX", size_t(uintptr_t(record->ExceptionAddress) - uintptr_t(GetModuleHandleW(nullptr))));

    uint64_t address = record->ExceptionInformation[1];
    uint64_t guest = address - uint64_t(g_base);
    if (guest < PPC_MEMORY_SIZE)
        Log("FATAL: access violation %s guest address %08X in sub_%08X+0x%zX (host)",
            record->ExceptionInformation[0] ? "writing" : "reading", uint32_t(guest), guestFunction,
            size_t(uintptr_t(record->ExceptionAddress) - best));
    else
        Log("FATAL: access violation at host address %p in sub_%08X+0x%zX (host)", (void*)address, guestFunction,
            size_t(uintptr_t(record->ExceptionAddress) - best));
    DumpGuestState();
    ExitProcess(3);
}

#else
// Linux: no sampler; a bad access is reported with the guest state and ends the game.
#include <csignal>

static void SamplerThread(int)
{
}

static void CrashSignal(int, siginfo_t* info, void*)
{
    uintptr_t address = uintptr_t(info->si_addr);
    if (address >= uintptr_t(g_base) && address < uintptr_t(g_base) + 0x100000000ull)
        Log("FATAL: access violation on guest address %08X", uint32_t(address - uintptr_t(g_base)));
    else
        Log("FATAL: access violation at host address %p", (void*)address);
    DumpGuestState();
    _exit(3);
}
#endif

// The game's main thread: its thread block and the top of its stack, made by main().
static uint32_t g_mainPcr;
static uint32_t g_mainStackTop;

// Runs the game's own code from its entry point, on the calling thread.
static void RunGuest()
{
    PPCContext ctx{};
    // Start from the host MXCSR so floating point exceptions stay masked.
    ctx.fpscr.loadFromHost();
    GuestRegisterContext(&ctx);
    if (const char* sample = getenv("CD_SAMPLE"))
        std::thread(SamplerThread, atoi(sample)).detach();
    ctx.r1.u64 = g_mainStackTop - 0x100;
    ctx.r13.u64 = g_mainPcr;
    (PPC_LOOKUP_FUNC(g_base, g_xex.entryPoint))(ctx, g_base);

    Log("Guest entry point returned (r3=%08X).", ctx.r3.u32);
}

int main(int argc, char* argv[])
{
    // Started without a path (double click on the program): work from the
    // program's own folder and look for the game data next to it.
    // (Arguments starting with "--" are options, such as --no-launcher.)
    std::string xexPath;
    bool installOnly = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--install-from") == 0)
        {
            installOnly = true;
            i++;
        }
        else if (strcmp(argv[i], "--ini") == 0 || strcmp(argv[i], "--ui-audit") == 0 || strcmp(argv[i], "--gpu-check") == 0 ||
            strcmp(argv[i], "--bench") == 0)
        {
            i++; // DarkLauncher's options, with their value
        }
        else if (strncmp(argv[i], "--", 2) != 0 && xexPath.empty())
        {
            xexPath = argv[i];
        }
    }
#ifdef _WIN32
    // --install-from <folder>: installs the game's data from the player's copy
    // next to the program, then ends (used by the installer). The folder is
    // read from the wide command line, as it may hold any character.
    if (installOnly)
    {
        int count = 0;
        wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 1; wide != nullptr && i + 1 < count; i++)
        {
            if (wcscmp(wide[i], L"--install-from") == 0)
                _wputenv_s(L"CD_SETUP_SOURCE", wide[i + 1]);
        }
        LocalFree(wide);
        wchar_t program[MAX_PATH]{};
        GetModuleFileNameW(nullptr, program, MAX_PATH);
        std::filesystem::path folder = std::filesystem::path(program).parent_path();
        SetCurrentDirectoryW(folder.c_str());
        return RunFirstTimeSetup(folder) ? 0 : 2;
    }
#else
    (void)installOnly;
#endif
    if (xexPath.empty())
    {
        xexPath = "default.xex";
        wchar_t program[MAX_PATH]{};
        GetModuleFileNameW(nullptr, program, MAX_PATH);
        std::filesystem::path folder = std::filesystem::path(program).parent_path();
        SetCurrentDirectoryW(folder.c_str());
        bool found = false;
        for (const char* candidate : { "game/default.xex", "default.xex", "../extracted/default.xex", "../../extracted/default.xex" })
        {
            std::error_code error;
            if (std::filesystem::exists(folder / candidate, error))
            {
                xexPath = candidate;
                found = true;
                break;
            }
        }
        // No game data yet: ask for the player's own copy of the game.
        if (!found)
        {
            if (!RunFirstTimeSetup(folder))
                return 0;
            xexPath = "game/default.xex";
        }
    }
    // Relaunched from the settings menu: let the old copy finish first, as it
    // still holds the log file and the audio device.
    if (const char* parent = getenv("CD_WAIT_PID"))
    {
        if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, strtoul(parent, nullptr, 10)))
        {
            WaitForSingleObject(process, 5000);
            CloseHandle(process);
        }
        SetEnvironmentVariableA("CD_WAIT_PID", nullptr);
    }

    // Keep the previous session's log: a crash is usually followed by a relaunch.
    MoveFileExW(L"crystal_defenders.log", L"crystal_defenders.prev.log", MOVEFILE_REPLACE_EXISTING);
    g_logFile = fopen("crystal_defenders.log", "w");

    const auto file = LoadFile(xexPath);
    if (file.size() < 0x18 || memcmp(file.data(), "XEX2", 4) != 0)
    {
        Log("Unable to read XEX file '%s'. Pass the path to your own default.xex.", xexPath.c_str());
        MessageBoxW(nullptr, L"Les données du jeu sont introuvables.\n\nLe fichier default.xex doit se trouver dans le dossier \"game\" "
            L"à côté de ce programme.\n\nGame data not found: default.xex is expected in the \"game\" folder next to this program.",
            L"Crystal Defenders", MB_OK | MB_ICONERROR);
        return 1;
    }

    Image image = Image::ParseImage(file.data(), file.size());
    if (image.data == nullptr || image.base != PPC_IMAGE_BASE || image.size != PPC_IMAGE_SIZE)
    {
        Log("'%s' is not the executable this build was recompiled from.", xexPath.c_str());
        MessageBoxW(nullptr, L"Ce default.xex n'est pas celui de la version du jeu attendue (Crystal Defenders, Xbox 360).\n\n"
            L"This default.xex is not from the expected version of the game (Crystal Defenders, Xbox 360).",
            L"Crystal Defenders", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!ReserveGuestMemory())
    {
        Log("Unable to reserve the 4 GB guest address space (error %lu).", GetLastError());
        return 1;
    }
#ifdef _WIN32
    AddVectoredExceptionHandler(1, CrashHandler);
#else
    struct sigaction action{};
    action.sa_sigaction = CrashSignal;
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
#endif

    // Image, followed by the host function pointer table PPC_LOOKUP_FUNC indexes.
    const uint32_t tableAddress = uint32_t(PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
    const uint32_t tableSize = uint32_t(PPC_CODE_SIZE * 2);
    if (!GuestCommit(uint32_t(PPC_IMAGE_BASE), uint32_t(PPC_IMAGE_SIZE)) || !GuestCommit(tableAddress, tableSize))
    {
        Log("Unable to commit guest image memory.");
        return 1;
    }
    memcpy(g_base + PPC_IMAGE_BASE, image.data.get(), image.size);

    size_t functionCount = 0;
    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        if (PPCFuncMappings[i].host != nullptr)
        {
            PPC_LOOKUP_FUNC(g_base, PPCFuncMappings[i].guest) = PPCFuncMappings[i].host;
            functionCount++;
        }
    }

    // Keep a copy of the XEX header in guest memory for RtlImageXexHeaderField.
    const uint32_t headerSize = FileU32(file, 0x08);
    g_xex.headerAddress = GuestAllocVirtual(headerSize);
    memcpy(g_base + g_xex.headerAddress, file.data(), headerSize);
    g_xex.entryPoint = uint32_t(image.entry_point);
    g_xex.stackSize = XexFindHeader(0x00020200);
    if (g_xex.stackSize == 0)
        g_xex.stackSize = 0x40000;
    if (uint32_t tlsInfo = XexFindHeader(0x00020104))
    {
        g_xex.tlsSlotCount = Load32(tlsInfo + 0x0);
        g_xex.tlsRawDataAddress = Load32(tlsInfo + 0x4);
        g_xex.tlsDataSize = Load32(tlsInfo + 0x8);
        g_xex.tlsRawDataSize = Load32(tlsInfo + 0xC);
    }

    ResolveImports();
    SetGameRoot(xexPath.c_str());
    // The start screen comes once the game's files are known (it lists the
    // achievements they define) and before the game's window opens.
    if (!LauncherRun(argc, argv))
        return 0;
    if (!VideoInit())
    {
        Log("Unable to map the GPU register block.");
        return 1;
    }

    if (!AudioInit())
    {
        Log("Unable to set up the audio decoder.");
        return 1;
    }

    size_t implemented = 0;
    for (size_t i = 0; i < g_importCount; i++)
        implemented += g_importHandlers[i] != nullptr;

    Log("Loaded %s: %zu functions, entry point %08X, %zu/%zu imports implemented",
        xexPath.c_str(), functionCount, g_xex.entryPoint, implemented, g_importCount);

    g_mainPcr = GuestCreateThreadBlock(g_xex.stackSize, g_mainStackTop);
    if (g_mainPcr == 0)
    {
        Log("Unable to allocate the main guest thread.");
        return 1;
    }

#ifdef CD_DARKLAUNCHER
    // DarkLauncher keeps this thread for its window; the game runs in its own.
    return DarkLauncherMain(argc, argv, RunGuest);
#else
    RunGuest();
    return 0;
#endif
}
