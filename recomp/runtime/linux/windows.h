// Linux build: the small part of the Windows API the runtime uses, so that its
// files compile unchanged. Implemented in win32.cpp over POSIX and SDL2.
// Only what this program needs, with only the behaviour it relies on.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

using BOOL = int;
using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = unsigned;
using SHORT = int16_t;
using LONG = int32_t;
using ULONG = uint32_t;
using LONGLONG = int64_t;
using ULONGLONG = uint64_t;
using SIZE_T = size_t;
using INT_PTR = intptr_t;
using LONG_PTR = intptr_t;
using UINT_PTR = uintptr_t;
using ULONG_PTR = uintptr_t;
using DWORD_PTR = uintptr_t;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using HANDLE = void*;
using HWND = void*;
using HMODULE = void*;
using HINSTANCE = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPCWSTR = const wchar_t*;
using WCHAR = wchar_t;

#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define MAX_PATH 4096
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INFINITE 0xFFFFFFFFu
#define ERROR_SUCCESS 0u

// ---- Status codes the kernel imports return ----
#define STATUS_SUCCESS 0x00000000u
#define STATUS_TIMEOUT 0x00000102u
#define STATUS_PENDING 0x00000103u
#define STATUS_UNSUCCESSFUL 0xC0000001u
#define STATUS_NOT_IMPLEMENTED 0xC0000002u
#define STATUS_INVALID_HANDLE 0xC0000008u
#define STATUS_INVALID_PARAMETER 0xC000000Du
#define STATUS_NO_MEMORY 0xC0000017u
#define STATUS_ENTRYPOINT_NOT_FOUND 0xC0000139u
#define STATUS_NOT_FOUND 0xC0000225u

// ---- Time ----
struct FILETIME
{
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};
struct SYSTEMTIME
{
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};
union LARGE_INTEGER
{
    struct { DWORD LowPart; LONG HighPart; };
    LONGLONG QuadPart;
};
void Sleep(DWORD milliseconds);
BOOL SwitchToThread();
ULONGLONG GetTickCount64();
BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
void GetSystemTimeAsFileTime(FILETIME* time);
void GetLocalTime(SYSTEMTIME* time);
#define YieldProcessor() __builtin_ia32_pause()

// ---- Memory ----
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define MEM_FREE 0x10000u
#define PAGE_NOACCESS 0x01u
#define PAGE_READWRITE 0x04u
#define FILE_MAP_ALL_ACCESS 0xF001Fu
struct MEMORY_BASIC_INFORMATION
{
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
};
void* VirtualAlloc(void* address, SIZE_T size, DWORD type, DWORD protect);
BOOL VirtualFree(void* address, SIZE_T size, DWORD type);
SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);
HANDLE CreateFileMappingW(HANDLE file, void* security, DWORD protect, DWORD sizeHigh, DWORD sizeLow, const wchar_t* name);
void* MapViewOfFileEx(HANDLE mapping, DWORD access, DWORD offsetHigh, DWORD offsetLow, SIZE_T size, void* address);
BOOL UnmapViewOfFile(const void* address);

// ---- Handles, threads, events, semaphores ----
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define MAXIMUM_WAIT_OBJECTS 64
#define CREATE_SUSPENDED 0x4u
#define SYNCHRONIZE 0x00100000u
#define THREAD_SUSPEND_RESUME 0x2u
#define THREAD_GET_CONTEXT 0x8u
using LPTHREAD_START_ROUTINE = DWORD (*)(void*);
BOOL CloseHandle(HANDLE handle);
HANDLE CreateEventW(void* security, BOOL manualReset, BOOL initialState, const wchar_t* name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateSemaphoreW(void* security, LONG initialCount, LONG maximumCount, const wchar_t* name);
BOOL ReleaseSemaphore(HANDLE semaphore, LONG count, LONG* previous);
HANDLE CreateThread(void* security, SIZE_T stackSize, LPTHREAD_START_ROUTINE start, void* parameter, DWORD flags, DWORD* threadId);
DWORD ResumeThread(HANDLE thread);
[[noreturn]] void ExitThread(DWORD code);
DWORD GetCurrentThreadId();
DWORD GetCurrentProcessId();
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds);
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD processId);
[[noreturn]] void ExitProcess(UINT code);
DWORD GetLastError();

// ---- Files. Paths come from std::filesystem::path::c_str(), so they are
// narrow here; backslashes and letter case are sorted out inside. ----
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define FILE_SHARE_WRITE 0x2u
#define FILE_SHARE_DELETE 0x4u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define MOVEFILE_REPLACE_EXISTING 0x1u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_HANDLE_EOF 38u
#define ERROR_ALREADY_EXISTS 183u
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard };
struct WIN32_FILE_ATTRIBUTE_DATA
{
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
};
struct BY_HANDLE_FILE_INFORMATION
{
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD dwVolumeSerialNumber, nFileSizeHigh, nFileSizeLow, nNumberOfLinks, nFileIndexHigh, nFileIndexLow;
};
HANDLE CreateFileW(const char* path, DWORD access, DWORD share, void* security, DWORD creation, DWORD flags, HANDLE templateFile);
BOOL ReadFile(HANDLE file, void* buffer, DWORD size, DWORD* read, void* overlapped);
BOOL WriteFile(HANDLE file, const void* buffer, DWORD size, DWORD* written, void* overlapped);
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* newPosition, DWORD method);
BOOL SetEndOfFile(HANDLE file);
BOOL FlushFileBuffers(HANDLE file);
BOOL GetFileAttributesExW(const char* path, GET_FILEEX_INFO_LEVELS level, void* data);
BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION* info);
BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags);
FILE* _wfopen(const char* path, const wchar_t* mode);
// The host file for a path built from a guest one: forward slashes, and the
// spelling found on disk when only the letter case differs.
std::string HostPath(const std::string& path);

// ---- Process and environment ----
DWORD GetModuleFileNameW(HMODULE module, wchar_t* path, DWORD size);
BOOL SetCurrentDirectoryW(const char* path);
BOOL SetEnvironmentVariableA(const char* name, const char* value);
const wchar_t* GetCommandLineW();
#define _wgetenv LinuxWideEnvironment
const wchar_t* LinuxWideEnvironment(const wchar_t* name);
#define _fseeki64 fseeko

// ---- Language ----
#define LANG_ENGLISH 0x09
#define LANG_FRENCH 0x0C
#define LANG_GERMAN 0x07
#define LANG_ITALIAN 0x10
#define LANG_JAPANESE 0x11
#define LANG_SPANISH 0x0A
#define PRIMARYLANGID(id) ((id) & 0x3FF)
WORD GetUserDefaultUILanguage();

// ---- Message boxes ----
#define MB_OK 0x0u
#define MB_OKCANCEL 0x1u
#define MB_YESNOCANCEL 0x3u
#define MB_YESNO 0x4u
#define MB_RETRYCANCEL 0x5u
#define MB_ICONERROR 0x10u
#define MB_ICONQUESTION 0x20u
#define MB_ICONWARNING 0x30u
#define MB_ICONINFORMATION 0x40u
#define MB_SETFOREGROUND 0x10000u
#define MB_TOPMOST 0x40000u
#define IDOK 1
#define IDCANCEL 2
#define IDRETRY 4
#define IDYES 6
#define IDNO 7
int MessageBoxW(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type);

// ---- Text ----
#define CP_UTF8 65001u
int MultiByteToWideChar(UINT codePage, DWORD flags, const char* text, int length, wchar_t* out, int capacity);
int WideCharToMultiByte(UINT codePage, DWORD flags, const wchar_t* text, int length, char* out, int capacity, const char*, BOOL*);

// ---- Virtual key codes (crystal_defenders.ini stores keys this way) ----
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_NUMPAD0 0x60
#define VK_F1 0x70
#define VK_F11 0x7A
#define VK_F12 0x7B

// ---- Timer resolution: Linux sleeps are already fine-grained ----
inline UINT timeBeginPeriod(UINT) { return 0; }
