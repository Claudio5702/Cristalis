// Linux implementation of the part of the Windows API declared in windows.h.
#include "windows.h"
#include "mmsystem.h"

#include <SDL2/SDL.h>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

static thread_local DWORD t_lastError;

DWORD GetLastError()
{
    return t_lastError;
}

// ---- Text ----

static std::string Narrow(const wchar_t* text)
{
    std::string out;
    for (; text != nullptr && *text != 0; text++)
    {
        uint32_t c = uint32_t(*text);
        if (c < 0x80)
            out += char(c);
        else if (c < 0x800)
        {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
        else
        {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

static std::wstring Widen(const char* text, size_t length)
{
    std::wstring out;
    for (size_t i = 0; i < length;)
    {
        uint8_t c = uint8_t(text[i]);
        uint32_t code = c;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra != 0)
            code = c & (0x3F >> extra);
        i++;
        for (int k = 0; k < extra && i < length; k++, i++)
            code = (code << 6) | (uint8_t(text[i]) & 0x3F);
        out += wchar_t(code);
    }
    return out;
}

int MultiByteToWideChar(UINT, DWORD, const char* text, int length, wchar_t* out, int capacity)
{
    std::wstring wide = Widen(text, length < 0 ? strlen(text) + 1 : size_t(length));
    if (out != nullptr && capacity > 0)
        memcpy(out, wide.data(), sizeof(wchar_t) * (wide.size() < size_t(capacity) ? wide.size() : size_t(capacity)));
    return int(wide.size());
}

int WideCharToMultiByte(UINT, DWORD, const wchar_t* text, int length, char* out, int capacity, const char*, BOOL*)
{
    std::wstring copy = length < 0 ? std::wstring(text) : std::wstring(text, size_t(length));
    std::string narrow = Narrow(copy.c_str());
    if (out != nullptr && capacity > 0)
        memcpy(out, narrow.data(), narrow.size() < size_t(capacity) ? narrow.size() : size_t(capacity));
    return int(narrow.size());
}

// ---- Time ----

void Sleep(DWORD milliseconds)
{
    if (milliseconds == 0)
        sched_yield();
    else
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

BOOL SwitchToThread()
{
    sched_yield();
    return TRUE;
}

static uint64_t MonotonicNanos()
{
    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
}

ULONGLONG GetTickCount64()
{
    return MonotonicNanos() / 1000000ull;
}

BOOL QueryPerformanceCounter(LARGE_INTEGER* count)
{
    count->QuadPart = LONGLONG(MonotonicNanos());
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
    frequency->QuadPart = 1000000000ll;
    return TRUE;
}

void GetSystemTimeAsFileTime(FILETIME* out)
{
    timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    // 100 ns units since 1601.
    uint64_t value = (uint64_t(time.tv_sec) + 11644473600ull) * 10000000ull + uint64_t(time.tv_nsec) / 100;
    out->dwLowDateTime = DWORD(value);
    out->dwHighDateTime = DWORD(value >> 32);
}

void GetLocalTime(SYSTEMTIME* out)
{
    timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    tm local{};
    localtime_r(&time.tv_sec, &local);
    *out = { WORD(local.tm_year + 1900), WORD(local.tm_mon + 1), WORD(local.tm_wday), WORD(local.tm_mday), WORD(local.tm_hour),
        WORD(local.tm_min), WORD(local.tm_sec), WORD(time.tv_nsec / 1000000) };
}

// ---- Memory ----
// Reservations are PROT_NONE mappings; committing a range makes it readable
// and writable. Which pages are committed is remembered for VirtualQuery.

struct Reservation
{
    size_t size;
    std::vector<bool> committed; // one per 4 KB page
};
static std::mutex g_memoryMutex;
static std::map<uintptr_t, Reservation> g_reservations;

static std::map<uintptr_t, Reservation>::iterator FindReservation(uintptr_t address)
{
    auto it = g_reservations.upper_bound(address);
    if (it == g_reservations.begin())
        return g_reservations.end();
    --it;
    return address < it->first + it->second.size ? it : g_reservations.end();
}

void* VirtualAlloc(void* address, SIZE_T size, DWORD type, DWORD)
{
    std::lock_guard lock(g_memoryMutex);
    size = (size + 0xFFF) & ~SIZE_T(0xFFF);
    if (type & MEM_RESERVE)
    {
        int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (address != nullptr ? MAP_FIXED_NOREPLACE : 0);
        void* base = mmap(address, size, PROT_NONE, flags, -1, 0);
        if (base == MAP_FAILED || (address != nullptr && base != address))
        {
            if (base != MAP_FAILED)
                munmap(base, size);
            return nullptr;
        }
        g_reservations[uintptr_t(base)] = { size, std::vector<bool>(size >> 12) };
        address = base;
    }
    if (type & MEM_COMMIT)
    {
        auto it = FindReservation(uintptr_t(address));
        if (it == g_reservations.end() || mprotect(address, size, PROT_READ | PROT_WRITE) != 0)
            return nullptr;
        size_t first = (uintptr_t(address) - it->first) >> 12;
        for (size_t page = first; page < first + (size >> 12) && page < it->second.committed.size(); page++)
            it->second.committed[page] = true;
    }
    return address;
}

BOOL VirtualFree(void* address, SIZE_T size, DWORD type)
{
    std::lock_guard lock(g_memoryMutex);
    auto it = FindReservation(uintptr_t(address));
    if (it == g_reservations.end())
        return FALSE;
    if (type & MEM_RELEASE)
    {
        munmap((void*)it->first, it->second.size);
        g_reservations.erase(it);
        return TRUE;
    }
    // Decommit: the pages go back to the system and read as zero if committed again.
    size = (size + 0xFFF) & ~SIZE_T(0xFFF);
    madvise(address, size, MADV_DONTNEED);
    mprotect(address, size, PROT_NONE);
    size_t first = (uintptr_t(address) - it->first) >> 12;
    for (size_t page = first; page < first + (size >> 12) && page < it->second.committed.size(); page++)
        it->second.committed[page] = false;
    return TRUE;
}

static std::map<uintptr_t, size_t> g_views; // mapped sections: always readable

SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T)
{
    std::lock_guard lock(g_memoryMutex);
    uintptr_t page = uintptr_t(address) & ~uintptr_t(0xFFF);
    *info = {};
    info->BaseAddress = (void*)page;
    info->AllocationProtect = PAGE_READWRITE;

    auto view = g_views.upper_bound(page);
    if (view != g_views.begin() && page < std::prev(view)->first + std::prev(view)->second)
    {
        --view;
        info->AllocationBase = (void*)view->first;
        info->RegionSize = view->first + view->second - page;
        info->State = MEM_COMMIT;
        info->Protect = PAGE_READWRITE;
        return sizeof(*info);
    }

    auto it = FindReservation(page);
    if (it == g_reservations.end())
    {
        info->RegionSize = 0x1000;
        info->State = MEM_FREE;
        return sizeof(*info);
    }
    size_t index = (page - it->first) >> 12;
    bool committed = it->second.committed[index];
    size_t end = index;
    while (end < it->second.committed.size() && it->second.committed[end] == committed)
        end++;
    info->AllocationBase = (void*)it->first;
    info->RegionSize = (end - index) << 12;
    info->State = committed ? MEM_COMMIT : MEM_RESERVE;
    info->Protect = committed ? PAGE_READWRITE : 0;
    return sizeof(*info);
}

// ---- Handles ----

struct Object
{
    enum Kind { Event, Semaphore, Thread, File, Mapping, Process } kind;
    // Events and threads
    bool signaled{};
    bool manualReset{};
    // Semaphores
    LONG count{}, maximum{};
    // Threads
    pthread_t thread{};
    LPTHREAD_START_ROUTINE start{};
    void* parameter{};
    bool resumed{};
    DWORD threadId{};
    // Files and mappings
    int fd{ -1 };
};

// One lock and one condition for every waitable object: there are few of them
// and waits on several objects at once then need no extra machinery.
static std::mutex g_objectMutex;
static std::condition_variable g_objectSignal;

static bool IsObject(HANDLE handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

BOOL CloseHandle(HANDLE handle)
{
    if (!IsObject(handle))
        return FALSE;
    auto* object = (Object*)handle;
    if (object->fd >= 0)
        close(object->fd);
    // Threads may still be running, and a waiter may still hold the pointer:
    // those small objects are left alone rather than freed under someone's feet.
    if (object->kind == Object::File || object->kind == Object::Mapping || object->kind == Object::Process)
        delete object;
    return TRUE;
}

HANDLE CreateEventW(void*, BOOL manualReset, BOOL initialState, const wchar_t*)
{
    auto* object = new Object{ Object::Event };
    object->manualReset = manualReset;
    object->signaled = initialState;
    return object;
}

BOOL SetEvent(HANDLE event)
{
    if (!IsObject(event))
        return FALSE;
    {
        std::lock_guard lock(g_objectMutex);
        ((Object*)event)->signaled = true;
    }
    g_objectSignal.notify_all();
    return TRUE;
}

BOOL ResetEvent(HANDLE event)
{
    if (!IsObject(event))
        return FALSE;
    std::lock_guard lock(g_objectMutex);
    ((Object*)event)->signaled = false;
    return TRUE;
}

HANDLE CreateSemaphoreW(void*, LONG initialCount, LONG maximumCount, const wchar_t*)
{
    auto* object = new Object{ Object::Semaphore };
    object->count = initialCount;
    object->maximum = maximumCount;
    return object;
}

BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG* previous)
{
    if (!IsObject(semaphore))
        return FALSE;
    {
        std::lock_guard lock(g_objectMutex);
        auto* object = (Object*)semaphore;
        if (previous != nullptr)
            *previous = object->count;
        if (object->count + count > object->maximum)
            return FALSE;
        object->count += count;
    }
    g_objectSignal.notify_all();
    return TRUE;
}

static bool Ready(const Object* object)
{
    return object->kind == Object::Semaphore ? object->count > 0 : object->kind == Object::Process ? true : object->signaled;
}

static void Consume(Object* object)
{
    if (object->kind == Object::Semaphore)
        object->count--;
    else if (object->kind == Object::Event && !object->manualReset)
        object->signaled = false;
}

DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds)
{
    for (DWORD i = 0; i < count; i++)
    {
        if (!IsObject(handles[i]))
            return WAIT_FAILED;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    std::unique_lock lock(g_objectMutex);
    for (;;)
    {
        DWORD ready = 0;
        DWORD first = count;
        for (DWORD i = 0; i < count; i++)
        {
            if (Ready((Object*)handles[i]))
            {
                ready++;
                if (first == count)
                    first = i;
            }
        }
        if (waitAll ? ready == count : ready != 0)
        {
            if (waitAll)
            {
                for (DWORD i = 0; i < count; i++)
                    Consume((Object*)handles[i]);
                return WAIT_OBJECT_0;
            }
            Consume((Object*)handles[first]);
            return WAIT_OBJECT_0 + first;
        }
        if (milliseconds == INFINITE)
            g_objectSignal.wait(lock);
        else if (g_objectSignal.wait_until(lock, deadline) == std::cv_status::timeout)
        {
            // One last look, then give up.
            bool any = false;
            for (DWORD i = 0; i < count && !waitAll; i++)
                any = any || Ready((Object*)handles[i]);
            if (!any)
                return WAIT_TIMEOUT;
        }
    }
}

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    return WaitForMultipleObjects(1, &handle, FALSE, milliseconds);
}

// ---- Threads ----

static thread_local Object* t_thread;

DWORD GetCurrentThreadId()
{
    return DWORD(syscall(SYS_gettid));
}

DWORD GetCurrentProcessId()
{
    return DWORD(getpid());
}

static void FinishThread()
{
    if (t_thread != nullptr)
    {
        {
            std::lock_guard lock(g_objectMutex);
            t_thread->signaled = true;
        }
        g_objectSignal.notify_all();
    }
}

static void* ThreadEntry(void* argument)
{
    auto* object = (Object*)argument;
    t_thread = object;
    {
        std::unique_lock lock(g_objectMutex);
        object->threadId = GetCurrentThreadId();
        g_objectSignal.wait(lock, [object] { return object->resumed; });
    }
    object->start(object->parameter);
    FinishThread();
    return nullptr;
}

HANDLE CreateThread(void*, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, void* parameter, DWORD flags, DWORD* threadId)
{
    auto* object = new Object{ Object::Thread };
    object->start = start;
    object->parameter = parameter;
    object->resumed = !(flags & CREATE_SUSPENDED);

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, stackSize < 0x400000 ? 0x400000 : stackSize);
    int result = pthread_create(&object->thread, &attributes, ThreadEntry, object);
    pthread_attr_destroy(&attributes);
    if (result != 0)
    {
        delete object;
        return nullptr;
    }
    pthread_detach(object->thread);
    if (threadId != nullptr)
        *threadId = 0;
    return object;
}

DWORD ResumeThread(HANDLE thread)
{
    if (!IsObject(thread))
        return DWORD(-1);
    auto* object = (Object*)thread;
    DWORD previous;
    {
        std::lock_guard lock(g_objectMutex);
        previous = object->resumed ? 0 : 1;
        object->resumed = true;
    }
    g_objectSignal.notify_all();
    return previous;
}

void ExitThread(DWORD)
{
    FinishThread();
    // Called from deep inside recompiled code. pthread_exit would unwind the
    // stack through it, which aborts; end just this thread instead. Its stack
    // stays allocated, which is a few megabytes for each of a handful of threads.
    syscall(SYS_exit, 0);
    __builtin_unreachable();
}

HANDLE OpenProcess(DWORD, BOOL, DWORD)
{
    return new Object{ Object::Process };
}

void ExitProcess(UINT code)
{
    // No static destructors: other threads are still running guest code.
    fflush(nullptr);
    _exit(int(code));
}

// ---- Files ----

// Guest paths use backslashes and any letter case; the files on disk have one
// spelling. Each missing component is looked up without regard to case.
std::string HostPath(const std::string& path)
{
    std::string normal = path;
    for (char& c : normal)
    {
        if (c == '\\')
            c = '/';
    }
    struct stat info;
    if (stat(normal.c_str(), &info) == 0)
        return normal;

    std::string resolved = normal.rfind('/', 0) == 0 ? "/" : "";
    size_t position = 0;
    while (position < normal.size())
    {
        size_t end = normal.find('/', position);
        if (end == std::string::npos)
            end = normal.size();
        std::string part = normal.substr(position, end - position);
        position = end + 1;
        if (part.empty())
            continue;

        std::string candidate = resolved + part;
        if (stat(candidate.c_str(), &info) != 0)
        {
            std::error_code error;
            for (auto& entry : std::filesystem::directory_iterator(resolved.empty() ? "." : resolved, error))
            {
                std::string name = entry.path().filename().string();
                if (strcasecmp(name.c_str(), part.c_str()) == 0)
                {
                    candidate = resolved + name;
                    break;
                }
            }
        }
        resolved = candidate + (position < normal.size() ? "/" : "");
    }
    return resolved;
}

HANDLE CreateFileW(const char* path, DWORD access, DWORD, void*, DWORD creation, DWORD, HANDLE)
{
    std::string host = HostPath(path);
    int flags = (access & GENERIC_WRITE) ? O_RDWR : O_RDONLY;
    switch (creation)
    {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }
    int fd = open(host.c_str(), flags | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        t_lastError = errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    auto* object = new Object{ Object::File };
    object->fd = fd;
    return object;
}

BOOL ReadFile(HANDLE file, void* buffer, DWORD size, DWORD* read, void*)
{
    ssize_t result = ::read(((Object*)file)->fd, buffer, size);
    if (read != nullptr)
        *read = result > 0 ? DWORD(result) : 0;
    return result >= 0;
}

BOOL WriteFile(HANDLE file, const void* buffer, DWORD size, DWORD* written, void*)
{
    ssize_t result = ::write(((Object*)file)->fd, buffer, size);
    if (written != nullptr)
        *written = result > 0 ? DWORD(result) : 0;
    return result >= 0;
}

BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* newPosition, DWORD method)
{
    off_t result = lseek(((Object*)file)->fd, off_t(distance.QuadPart), method == FILE_BEGIN ? SEEK_SET : method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    if (result < 0)
        return FALSE;
    if (newPosition != nullptr)
        newPosition->QuadPart = LONGLONG(result);
    return TRUE;
}

BOOL SetEndOfFile(HANDLE file)
{
    int fd = ((Object*)file)->fd;
    return ftruncate(fd, lseek(fd, 0, SEEK_CUR)) == 0;
}

BOOL FlushFileBuffers(HANDLE file)
{
    return fsync(((Object*)file)->fd) == 0;
}

static FILETIME ToFileTime(const timespec& time)
{
    uint64_t value = (uint64_t(time.tv_sec) + 11644473600ull) * 10000000ull + uint64_t(time.tv_nsec) / 100;
    return { DWORD(value), DWORD(value >> 32) };
}

BOOL GetFileAttributesExW(const char* path, GET_FILEEX_INFO_LEVELS, void* data)
{
    struct stat info;
    if (stat(HostPath(path).c_str(), &info) != 0)
    {
        t_lastError = ERROR_FILE_NOT_FOUND;
        return FALSE;
    }
    auto* out = (WIN32_FILE_ATTRIBUTE_DATA*)data;
    out->dwFileAttributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    out->ftCreationTime = out->ftLastWriteTime = ToFileTime(info.st_mtim);
    out->ftLastAccessTime = ToFileTime(info.st_atim);
    out->nFileSizeHigh = DWORD(uint64_t(info.st_size) >> 32);
    out->nFileSizeLow = DWORD(info.st_size);
    return TRUE;
}

BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION* out)
{
    struct stat info;
    if (fstat(((Object*)file)->fd, &info) != 0)
        return FALSE;
    *out = {};
    out->dwFileAttributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    out->ftCreationTime = out->ftLastWriteTime = ToFileTime(info.st_mtim);
    out->ftLastAccessTime = ToFileTime(info.st_atim);
    out->nFileSizeHigh = DWORD(uint64_t(info.st_size) >> 32);
    out->nFileSizeLow = DWORD(info.st_size);
    out->nNumberOfLinks = 1;
    return TRUE;
}

BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD)
{
    return rename(Narrow(from).c_str(), Narrow(to).c_str()) == 0;
}

FILE* _wfopen(const char* path, const wchar_t* mode)
{
    return fopen(HostPath(path).c_str(), Narrow(mode).c_str());
}

// ---- Sections: one piece of memory visible at several addresses ----

HANDLE CreateFileMappingW(HANDLE, void*, DWORD, DWORD sizeHigh, DWORD sizeLow, const wchar_t*)
{
    int fd = memfd_create("guest-physical", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, off_t((uint64_t(sizeHigh) << 32) | sizeLow)) != 0)
        return nullptr;
    auto* object = new Object{ Object::Mapping };
    object->fd = fd;
    return object;
}

void* MapViewOfFileEx(HANDLE mapping, DWORD, DWORD, DWORD, SIZE_T size, void* address)
{
    void* view = mmap(address, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, ((Object*)mapping)->fd, 0);
    if (view == MAP_FAILED || view != address)
    {
        if (view != MAP_FAILED)
            munmap(view, size);
        return nullptr;
    }
    std::lock_guard lock(g_memoryMutex);
    g_views[uintptr_t(view)] = size;
    return view;
}

BOOL UnmapViewOfFile(const void* address)
{
    std::lock_guard lock(g_memoryMutex);
    auto it = g_views.find(uintptr_t(address));
    if (it == g_views.end())
        return FALSE;
    munmap((void*)address, it->second);
    g_views.erase(it);
    return TRUE;
}

// ---- Process and environment ----

DWORD GetModuleFileNameW(HMODULE, wchar_t* path, DWORD size)
{
    char buffer[4096]{};
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    std::wstring wide = Widen(buffer, length > 0 ? size_t(length) : 0);
    if (wide.size() + 1 > size)
        return 0;
    memcpy(path, wide.c_str(), sizeof(wchar_t) * (wide.size() + 1));
    return DWORD(wide.size());
}

BOOL SetCurrentDirectoryW(const char* path)
{
    return chdir(path) == 0;
}

BOOL SetEnvironmentVariableA(const char* name, const char* value)
{
    return (value != nullptr ? setenv(name, value, 1) : unsetenv(name)) == 0;
}

// The program's own path and arguments, for relaunching itself.
const wchar_t* GetCommandLineW()
{
    static const std::wstring line = []
    {
        std::string text;
        if (FILE* file = fopen("/proc/self/cmdline", "rb"))
        {
            char buffer[4096];
            size_t read = fread(buffer, 1, sizeof(buffer), file);
            fclose(file);
            for (size_t i = 0; i < read; i++)
                text += buffer[i] != 0 ? buffer[i] : ' ';
        }
        return Widen(text.data(), text.size());
    }();
    return line.c_str();
}

const wchar_t* LinuxWideEnvironment(const wchar_t* name)
{
    static std::map<std::wstring, std::wstring> values;
    const char* value = getenv(Narrow(name).c_str());
    if (value == nullptr)
        return nullptr;
    return (values[name] = Widen(value, strlen(value))).c_str();
}

WORD GetUserDefaultUILanguage()
{
    static const struct { const char* code; WORD id; } languages[] =
    {
        { "fr", LANG_FRENCH }, { "de", LANG_GERMAN }, { "es", LANG_SPANISH }, { "it", LANG_ITALIAN }, { "ja", LANG_JAPANESE },
    };
    for (const char* variable : { "LC_ALL", "LC_MESSAGES", "LANG" })
    {
        const char* value = getenv(variable);
        if (value == nullptr || value[0] == 0)
            continue;
        for (auto& language : languages)
        {
            if (strncmp(value, language.code, 2) == 0)
                return language.id;
        }
        break;
    }
    return LANG_ENGLISH;
}

// ---- Message boxes ----

int MessageBoxW(HWND, const wchar_t* text, const wchar_t* caption, UINT type)
{
    std::string message = Narrow(text);
    std::string title = Narrow(caption);
    fprintf(stderr, "[%s] %s\n", title.c_str(), message.c_str());

    static const struct { UINT type; int count; SDL_MessageBoxButtonData buttons[3]; } layouts[] =
    {
        { MB_OK, 1, { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDOK, "OK" } } },
        { MB_OKCANCEL, 2, { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDOK, "OK" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDCANCEL, "Cancel" } } },
        { MB_YESNO, 2, { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDYES, "Yes" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDNO, "No" } } },
        { MB_YESNOCANCEL, 3, { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDYES, "Yes" }, { 0, IDNO, "No" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDCANCEL, "Cancel" } } },
        { MB_RETRYCANCEL, 2, { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDRETRY, "Retry" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDCANCEL, "Cancel" } } },
    };
    // CD_HEADLESS=1 (unattended tests): never wait for an answer.
    if (getenv("CD_HEADLESS") != nullptr)
        return (type & 0xF) == MB_YESNO || (type & 0xF) == MB_YESNOCANCEL ? IDYES : IDOK;

    for (auto& layout : layouts)
    {
        if (layout.type != (type & 0xF))
            continue;
        SDL_MessageBoxData data{};
        data.flags = (type & 0xF0) == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR : (type & 0xF0) == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
        data.title = title.c_str();
        data.message = message.c_str();
        data.numbuttons = layout.count;
        data.buttons = layout.buttons;
        int pressed = IDOK;
        if (SDL_ShowMessageBox(&data, &pressed) == 0)
            return pressed;
        break;
    }
    return IDOK;
}

// ---- Sound output (the waveOut calls of audio.cpp, over SDL) ----
// The mixer paces itself on buffers being reported done, so each written
// buffer is marked done once the device has played past its end.

static SDL_AudioDeviceID g_audioDevice;
static std::mutex g_audioMutex;
static std::vector<std::pair<WAVEHDR*, uint64_t>> g_audioPending; // header, queue position of its end
static uint64_t g_audioQueued;

static void AudioWatcher()
{
    for (;;)
    {
        Sleep(1);
        std::lock_guard lock(g_audioMutex);
        uint64_t played = g_audioQueued - SDL_GetQueuedAudioSize(g_audioDevice);
        while (!g_audioPending.empty() && g_audioPending.front().second <= played)
        {
            g_audioPending.front().first->dwFlags |= WHDR_DONE;
            g_audioPending.erase(g_audioPending.begin());
        }
    }
}

UINT waveOutOpen(HWAVEOUT* device, UINT, const WAVEFORMATEX* format, DWORD_PTR, DWORD_PTR, DWORD)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return 1;
    SDL_AudioSpec wanted{};
    wanted.freq = int(format->nSamplesPerSec);
    wanted.format = AUDIO_S16SYS;
    wanted.channels = Uint8(format->nChannels);
    wanted.samples = 512;
    g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (g_audioDevice == 0)
        return 1;
    SDL_PauseAudioDevice(g_audioDevice, 0);
    std::thread(AudioWatcher).detach();
    *device = (HWAVEOUT)(uintptr_t)g_audioDevice;
    return MMSYSERR_NOERROR;
}

UINT waveOutPrepareHeader(HWAVEOUT, WAVEHDR*, UINT)
{
    return MMSYSERR_NOERROR;
}

UINT waveOutWrite(HWAVEOUT, WAVEHDR* header, UINT)
{
    std::lock_guard lock(g_audioMutex);
    if (SDL_QueueAudio(g_audioDevice, header->lpData, header->dwBufferLength) != 0)
    {
        header->dwFlags |= WHDR_DONE;
        return 1;
    }
    g_audioQueued += header->dwBufferLength;

    g_audioPending.push_back({ header, g_audioQueued });
    return MMSYSERR_NOERROR;
}

// ---- Older C libraries ----
// A recent compiler calls the C23 variants of these functions, which only exist
// from glibc 2.38. Supplying them here, on top of the classic ones, lets the
// program start on older systems. The only difference is the "0b" prefix,
// which nothing here parses.
extern "C"
{
long ClassicStrtol(const char*, char**, int) __asm__("strtol");
unsigned long ClassicStrtoul(const char*, char**, int) __asm__("strtoul");
long long ClassicStrtoll(const char*, char**, int) __asm__("strtoll");
unsigned long long ClassicStrtoull(const char*, char**, int) __asm__("strtoull");
int ClassicVsscanf(const char*, const char*, va_list) __asm__("vsscanf");

long __isoc23_strtol(const char* text, char** end, int base) { return ClassicStrtol(text, end, base); }
unsigned long __isoc23_strtoul(const char* text, char** end, int base) { return ClassicStrtoul(text, end, base); }
long long __isoc23_strtoll(const char* text, char** end, int base) { return ClassicStrtoll(text, end, base); }
unsigned long long __isoc23_strtoull(const char* text, char** end, int base) { return ClassicStrtoull(text, end, base); }
int __isoc23_sscanf(const char* text, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = ClassicVsscanf(text, format, arguments);
    va_end(arguments);
    return result;
}
}

// arc4random only exists from glibc 2.36, and the C++ library asks for it.
// Supplying it keeps the program startable on glibc 2.35 (Ubuntu 22.04).
#include <sys/random.h>
extern "C" uint32_t arc4random()
{
    uint32_t value = 0;
    if (getrandom(&value, sizeof(value), 0) != sizeof(value))
        value = uint32_t(MonotonicNanos());
    return value;
}
