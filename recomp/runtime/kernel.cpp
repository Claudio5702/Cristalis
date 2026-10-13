// Host implementations of xboxkrnl.exe imports: memory, synchronisation, TLS and
// the small informational calls the CRT startup needs.
#include "config.h"
#include "guest.h"
#include "xex_header.h"

#include <windows.h>

#include <atomic>
#include <mutex>

static uint32_t CurrentGuestThread(PPCContext& ctx)
{
    return Load32(ctx.r13.u32 + 0x100);
}

// ---- Process / image information ----

GUEST_IMPORT(KeGetCurrentProcessType)
{
    ctx.r3.u64 = 1; // title process
}

GUEST_IMPORT(RtlImageXexHeaderField)
{
    ctx.r3.u64 = XexFindHeader(ctx.r4.u32);
}

GUEST_IMPORT(XexCheckExecutablePrivilege)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(XexGetModuleHandle)
{
    // No other modules are loaded; the game falls back when the lookup fails.
    if (ctx.r4.u32 != 0)
        Store32(ctx.r4.u32, 0);
    ctx.r3.u64 = 0xC0000225; // STATUS_NOT_FOUND
}

GUEST_IMPORT(XexGetProcedureAddress)
{
    if (ctx.r5.u32 != 0)
        Store32(ctx.r5.u32, 0);
    ctx.r3.u64 = 0xC0000139; // STATUS_ENTRYPOINT_NOT_FOUND
}

GUEST_IMPORT(KeEnableFpuExceptions)
{
}

GUEST_IMPORT(KeQueryPerformanceFrequency)
{
    ctx.r3.u64 = 50000000; // Xbox 360 timebase frequency
}

GUEST_IMPORT(XGetAVPack)
{
    ctx.r3.u64 = 6; // HDMI
}

GUEST_IMPORT(XGetGameRegion)
{
    ctx.r3.u64 = 0xFFFF; // region free
}

uint32_t GuestLanguage()
{
    if (GetConfig().language != 0)
        return GetConfig().language;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_JAPANESE: return 2;
    case LANG_GERMAN: return 3;
    case LANG_FRENCH: return 4;
    case LANG_SPANISH: return 5;
    case LANG_ITALIAN: return 6;
    default: return 1; // English
    }
}

GUEST_IMPORT(XGetLanguage)
{
    ctx.r3.u64 = GuestLanguage();
}

GUEST_IMPORT(ExGetXConfigSetting)
{
    constexpr uint32_t XCONFIG_USER_CATEGORY = 3;
    constexpr uint32_t XCONFIG_USER_LANGUAGE = 9;

    uint32_t buffer = ctx.r5.u32;
    uint32_t size = ctx.r6.u32;
    if (buffer != 0)
        memset(g_base + buffer, 0, size);
    if (ctx.r3.u32 == XCONFIG_USER_CATEGORY && ctx.r4.u32 == XCONFIG_USER_LANGUAGE && buffer != 0 && size >= 4)
        Store32(buffer, GuestLanguage());
    if (ctx.r7.u32 != 0)
        Store16(ctx.r7.u32, uint16_t(size));
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(DbgPrint)
{
    Log("[guest] %s", (const char*)GuestToHost(ctx.r3.u32));
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(KeBugCheck)
{
    Log("FATAL: guest KeBugCheck(%08X) lr=%08X", ctx.r3.u32, uint32_t(ctx.lr));
    ExitProcess(2);
}

GUEST_IMPORT(KeBugCheckEx)
{
    Log("FATAL: guest KeBugCheckEx(%08X, %08X, %08X, %08X, %08X) lr=%08X",
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, uint32_t(ctx.lr));
    ExitProcess(2);
}

GUEST_IMPORT(HalReturnToFirmware)
{
    Log("Guest requested exit (HalReturnToFirmware %u).", ctx.r3.u32);
    ExitProcess(0);
}

GUEST_IMPORT(RtlNtStatusToDosError)
{
    switch (ctx.r3.u32)
    {
    case 0x00000000: ctx.r3.u64 = 0; break;    // ERROR_SUCCESS
    case 0x00000103: ctx.r3.u64 = 997; break;  // ERROR_IO_PENDING
    case 0xC0000008: ctx.r3.u64 = 6; break;    // ERROR_INVALID_HANDLE
    case 0xC000000D: ctx.r3.u64 = 87; break;   // ERROR_INVALID_PARAMETER
    case 0xC000000F:
    case 0xC0000034: ctx.r3.u64 = 2; break;    // ERROR_FILE_NOT_FOUND
    case 0xC000003A: ctx.r3.u64 = 3; break;    // ERROR_PATH_NOT_FOUND
    case 0xC0000011: ctx.r3.u64 = 38; break;   // ERROR_HANDLE_EOF
    case 0xC0000017: ctx.r3.u64 = 8; break;    // ERROR_NOT_ENOUGH_MEMORY
    case 0xC0000022: ctx.r3.u64 = 5; break;    // ERROR_ACCESS_DENIED
    case 0xC0000035: ctx.r3.u64 = 183; break;  // ERROR_ALREADY_EXISTS
    case 0x80000006: ctx.r3.u64 = 18; break;   // ERROR_NO_MORE_FILES
    default: ctx.r3.u64 = 317; break;          // ERROR_MR_MID_NOT_FOUND
    }
}

// ---- Memory ----

GUEST_IMPORT(NtAllocateVirtualMemory)
{
    constexpr uint32_t MEM_RESERVE_ = 0x2000;

    uint32_t address = Load32(ctx.r3.u32);
    uint32_t size = Load32(ctx.r4.u32);
    uint32_t type = ctx.r5.u32;

    if (address == 0)
    {
        // Reservations are committed up front, in full.
        size = (size + 0xFFFF) & ~0xFFFFu;
        address = GuestAllocVirtual(size);
        if (address == 0)
        {
            ctx.r3.u64 = STATUS_NO_MEMORY;
            return;
        }
    }
    else
    {
        // With large pages the granularity is 64 KB, and the guest heap relies
        // on the rounded size written back below.
        uint32_t pageMask = (type & 0x20000000) ? 0xFFFFu : 0xFFFu;
        uint32_t end = (address + size + pageMask) & ~pageMask;
        address &= ~pageMask;
        size = end - address;
        if (!(type & MEM_RESERVE_) && !GuestCommit(address, size))
        {
            ctx.r3.u64 = STATUS_NO_MEMORY;
            return;
        }
    }

    Store32(ctx.r3.u32, address);
    Store32(ctx.r4.u32, size);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtFreeVirtualMemory)
{
    constexpr uint32_t MEM_DECOMMIT_ = 0x4000;
    constexpr uint32_t MEM_RELEASE_ = 0x8000;

    uint32_t address = Load32(ctx.r3.u32);
    uint32_t size = Load32(ctx.r4.u32);
    uint32_t type = ctx.r5.u32;

    // Releasing a whole reservation returns its address space for reuse.
    if (type & MEM_RELEASE_)
    {
        GuestFreeVirtual(address);
        ctx.r3.u64 = STATUS_SUCCESS;
        return;
    }

    // Decommitted pages must read back as zero when the guest commits them
    // again, as its heap relies on that.
    if ((type & MEM_DECOMMIT_) && size != 0)
    {
        uint32_t begin = (address + 0xFFF) & ~0xFFFu;
        uint32_t end = (address + size) & ~0xFFFu;
        if (end > begin)
            GuestDecommit(begin, end - begin);
    }
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(MmAllocatePhysicalMemoryEx)
{
    constexpr uint32_t X_MEM_LARGE_PAGES = 0x20000000;

    uint32_t size = ctx.r4.u32;
    uint32_t protect = ctx.r5.u32;
    uint32_t alignment = ctx.r8.u32;
    if (alignment < 0x1000)
        alignment = 0x1000;
    if ((protect & X_MEM_LARGE_PAGES) && alignment < 0x10000)
        alignment = 0x10000;
    alignment = 1u << (31 - __builtin_clz(alignment));

    ctx.r3.u64 = GuestAllocPhysical(size, alignment);
}

GUEST_IMPORT(MmFreePhysicalMemory)
{
    GuestFreePhysical(ctx.r4.u32);
}

GUEST_IMPORT(MmMapIoSpace)
{
    // Maps a physical range; every physical page is already visible at 0xA0000000.
    ctx.r3.u64 = kPhysicalHeapBase | (ctx.r4.u32 & 0x1FFFFFFF);
}

GUEST_IMPORT(MmQueryStatistics)
{
    // X_MM_QUERY_STATISTICS_RESULT, in 4 KB pages. The caller sets the size field.
    uint32_t stats = ctx.r3.u32;
    if (Load32(stats) != 0x68)
    {
        ctx.r3.u64 = 0xC0000004; // STATUS_INFO_LENGTH_MISMATCH
        return;
    }
    memset(g_base + stats + 4, 0, 0x64);
    Store32(stats + 0x04, 0x20000);     // total physical pages (512 MB)
    Store32(stats + 0x08, 0x300);       // kernel pages
    Store32(stats + 0x0C, 0x13300);     // title available pages
    Store32(stats + 0x10, 0x7FCF0000);  // title total virtual bytes
    Store32(stats + 0x14, 0x160000);    // title reserved virtual bytes
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(MmGetPhysicalAddress)
{
    ctx.r3.u64 = ctx.r3.u32 & 0x1FFFFFFF;
}

GUEST_IMPORT(RtlFillMemoryUlong)
{
    uint32_t pattern = __builtin_bswap32(ctx.r5.u32);
    auto* destination = (uint32_t*)GuestToHost(ctx.r3.u32);
    for (uint32_t i = 0; i < ctx.r4.u32 / 4; i++)
        memcpy(destination + i, &pattern, 4);
}

GUEST_IMPORT(RtlCompareMemoryUlong)
{
    uint32_t pattern = __builtin_bswap32(ctx.r5.u32);
    auto* source = (const uint8_t*)GuestToHost(ctx.r3.u32);
    uint32_t matched = 0;
    while (matched + 4 <= ctx.r4.u32 && memcmp(source + matched, &pattern, 4) == 0)
        matched += 4;
    ctx.r3.u64 = matched;
}

GUEST_IMPORT(RtlInitAnsiString)
{
    uint32_t string = ctx.r4.u32;
    uint16_t length = string ? uint16_t(strlen((const char*)GuestToHost(string))) : 0;
    Store16(ctx.r3.u32 + 0, length);
    Store16(ctx.r3.u32 + 2, string ? length + 1 : 0);
    Store32(ctx.r3.u32 + 4, string);
}

// ---- Critical sections ----
// X_RTL_CRITICAL_SECTION: dispatch header (0x10), lock count, recursion count,
// owning thread. The owner field doubles as the lock word.

static std::atomic<uint32_t>& CriticalSectionOwner(uint32_t cs)
{
    return *(std::atomic<uint32_t>*)(g_base + cs + 0x18);
}

GUEST_IMPORT(RtlInitializeCriticalSection)
{
    uint32_t cs = ctx.r3.u32;
    memset(g_base + cs, 0, 0x1C);
    Store32(cs + 0x10, uint32_t(-1));
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(RtlEnterCriticalSection)
{
    uint32_t cs = ctx.r3.u32;
    uint32_t self = CurrentGuestThread(ctx);
    auto& owner = CriticalSectionOwner(cs);

    if (owner.load(std::memory_order_relaxed) == self)
    {
        Store32(cs + 0x14, Load32(cs + 0x14) + 1);
        ctx.r3.u64 = STATUS_SUCCESS;
        return;
    }

    uint32_t spins = 0;
    uint32_t expected = 0;
    while (!owner.compare_exchange_weak(expected, self, std::memory_order_acquire))
    {
        expected = 0;
        if (++spins < 64)
            YieldProcessor();
        else
            Sleep(0);
    }
    Store32(cs + 0x14, 1);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(RtlTryEnterCriticalSection)
{
    uint32_t cs = ctx.r3.u32;
    uint32_t self = CurrentGuestThread(ctx);
    auto& owner = CriticalSectionOwner(cs);

    if (owner.load(std::memory_order_relaxed) == self)
    {
        Store32(cs + 0x14, Load32(cs + 0x14) + 1);
        ctx.r3.u64 = 1;
        return;
    }

    uint32_t expected = 0;
    if (owner.compare_exchange_strong(expected, self, std::memory_order_acquire))
    {
        Store32(cs + 0x14, 1);
        ctx.r3.u64 = 1;
        return;
    }
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(RtlLeaveCriticalSection)
{
    uint32_t cs = ctx.r3.u32;
    uint32_t recursion = Load32(cs + 0x14) - 1;
    Store32(cs + 0x14, recursion);
    if (recursion == 0)
        CriticalSectionOwner(cs).store(0, std::memory_order_release);
    ctx.r3.u64 = STATUS_SUCCESS;
}

// ---- Thread local storage ----

static std::mutex g_tlsMutex;
static bool g_tlsUsed[64];
static thread_local uint32_t t_tlsValues[64];

GUEST_IMPORT(KeTlsAlloc)
{
    std::lock_guard lock(g_tlsMutex);
    for (uint32_t i = 0; i < 64; i++)
    {
        if (!g_tlsUsed[i])
        {
            g_tlsUsed[i] = true;
            ctx.r3.u64 = i;
            return;
        }
    }
    ctx.r3.u64 = 0xFFFFFFFF;
}

GUEST_IMPORT(KeTlsFree)
{
    std::lock_guard lock(g_tlsMutex);
    if (ctx.r3.u32 < 64)
        g_tlsUsed[ctx.r3.u32] = false;
    ctx.r3.u64 = 1;
}

GUEST_IMPORT(KeTlsGetValue)
{
    ctx.r3.u64 = ctx.r3.u32 < 64 ? t_tlsValues[ctx.r3.u32] : 0;
}

GUEST_IMPORT(KeTlsSetValue)
{
    if (ctx.r3.u32 < 64)
        t_tlsValues[ctx.r3.u32] = ctx.r4.u32;
    ctx.r3.u64 = 1;
}

// ---- Scheduling ----

GUEST_IMPORT(NtYieldExecution)
{
    SwitchToThread();
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(KeDelayExecutionThread)
{
    // Interval is a pointer to a 64-bit count of 100 ns units, negative = relative.
    int64_t interval = ctx.r5.u32 ? int64_t(Load64(ctx.r5.u32)) : 0;
    if (interval < 0)
        Sleep(DWORD(-interval / 10000));
    else
        SwitchToThread();
    ctx.r3.u64 = STATUS_SUCCESS;
}

// ---- String conversion (the code page is treated as Latin-1) ----

GUEST_IMPORT(RtlMultiByteToUnicodeN)
{
    uint32_t count = ctx.r7.u32 < ctx.r4.u32 / 2 ? ctx.r7.u32 : ctx.r4.u32 / 2;
    for (uint32_t i = 0; i < count; i++)
        Store16(ctx.r3.u32 + i * 2, g_base[ctx.r6.u32 + i]);
    if (ctx.r5.u32 != 0)
        Store32(ctx.r5.u32, count * 2);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(RtlUnicodeToMultiByteN)
{
    uint32_t count = ctx.r7.u32 / 2 < ctx.r4.u32 ? ctx.r7.u32 / 2 : ctx.r4.u32;
    for (uint32_t i = 0; i < count; i++)
    {
        uint16_t character = Load16(ctx.r6.u32 + i * 2);
        g_base[ctx.r3.u32 + i] = character < 0x100 ? uint8_t(character) : '?';
    }
    if (ctx.r5.u32 != 0)
        Store32(ctx.r5.u32, count);
    ctx.r3.u64 = STATUS_SUCCESS;
}

// ---- Memory queries ----

// Describes the region holding an address, as VirtualQuery does: base, allocation
// base, allocation protection, size, state, protection, type.
GUEST_IMPORT(NtQueryVirtualMemory)
{
    MEMORY_BASIC_INFORMATION host{};
    if (ctx.r4.u32 == 0 || VirtualQuery(g_base + ctx.r3.u32, &host, sizeof(host)) == 0)
    {
        ctx.r3.u64 = 0xC000000D; // STATUS_INVALID_PARAMETER
        return;
    }

    // Keep everything inside the 4 GB the guest can see.
    uint64_t regionBase = uint64_t((uint8_t*)host.BaseAddress - g_base);
    uint64_t size = host.RegionSize;
    if (regionBase + size > 0x100000000ull)
        size = 0x100000000ull - regionBase;
    uint64_t allocation = host.AllocationBase != nullptr && (uint8_t*)host.AllocationBase >= g_base
        ? uint64_t((uint8_t*)host.AllocationBase - g_base) : regionBase;

    uint32_t info = ctx.r4.u32;
    Store32(info + 0x00, uint32_t(regionBase));
    Store32(info + 0x04, uint32_t(allocation));
    Store32(info + 0x08, host.AllocationProtect);
    Store32(info + 0x0C, uint32_t(size));
    Store32(info + 0x10, host.State);
    Store32(info + 0x14, host.State == MEM_COMMIT ? host.Protect : 0);
    Store32(info + 0x18, host.State == MEM_FREE ? 0 : 0x20000); // MEM_PRIVATE
    ctx.r3.u64 = STATUS_SUCCESS;
}

// ---- Debugging and exceptions ----

GUEST_IMPORT(DbgBreakPoint)
{
    Log("Guest hit a debug breakpoint (lr=%08X), ignored.", uint32_t(ctx.lr));
}

GUEST_IMPORT(RtlCaptureContext)
{
    // Only read by the exception path below, which never resumes the guest.
    if (ctx.r3.u32 != 0)
        memset(g_base + ctx.r3.u32, 0, 0x2C0);
}

// The game's C++ exceptions (out of memory, bad index) cannot be unwound through
// recompiled code. On the console they would end the game too, as nothing in it
// catches them usefully: stop here, saying why, rather than carry on past a
// throw with the program in an undefined state.
[[noreturn]] static void FatalGuestException(const char* what, PPCContext& ctx)
{
    uint32_t record = ctx.r3.u32;
    uint32_t code = record ? Load32(record) : 0;
    uint32_t address = record ? Load32(record + 12) : 0;
    Log("FATAL: guest exception through %s: code %08X at %08X (lr=%08X)", what, code, address, uint32_t(ctx.lr));
    LogGuestBacktrace("exception");
    MessageBoxW(nullptr, L"Le jeu a rencontré une erreur interne et doit se fermer.\n"
        L"Les détails sont dans crystal_defenders.log.\n\n"
        L"The game hit an internal error and has to close. Details are in crystal_defenders.log.",
        L"Crystal Defenders", MB_OK | MB_ICONERROR | MB_TOPMOST);
    ExitProcess(3);
}

GUEST_IMPORT(RtlRaiseException)
{
    FatalGuestException("RtlRaiseException", ctx);
}

GUEST_IMPORT(RtlUnwind)
{
    FatalGuestException("RtlUnwind", ctx);
}

GUEST_IMPORT(__C_specific_handler)
{
    FatalGuestException("__C_specific_handler", ctx);
}