// File I/O imports. Guest paths on game:\ and d:\ map to the directory holding
// default.xex; every other device maps under .\userdata\<device>\.
#include "kernel_objects.h"
#include "game_state.h"
#include "xex_header.h"

#include "content.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>

constexpr uint32_t STATUS_END_OF_FILE = 0xC0000011;
constexpr uint32_t X_STATUS_INVALID_HANDLE = 0xC0000008;
constexpr uint32_t STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034;
constexpr uint32_t STATUS_NO_MORE_FILES = 0x80000006;
constexpr uint32_t STATUS_INVALID_INFO_CLASS = 0xC0000003;

static std::filesystem::path g_gameRoot;

void SetGameRoot(const char* xexPath)
{
    g_gameRoot = std::filesystem::absolute(xexPath).parent_path();
}

static std::string ReadObjectName(uint32_t objectAttributes)
{
    uint32_t name = objectAttributes ? Load32(objectAttributes + 4) : 0;
    if (name == 0)
        return {};
    return std::string((const char*)g_base + Load32(name + 4), Load16(name));
}

// Content packages (saves) are mounted under a root name chosen by the game.
static std::mutex g_contentMutex;
static std::map<std::string, std::filesystem::path> g_contentRoots;

static std::string LowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
    return text;
}

std::filesystem::path GameRoot()
{
    return g_gameRoot;
}

std::filesystem::path ContentDirectory()
{
    return std::filesystem::current_path() / "userdata" / "content";
}

void MountContentRoot(const std::string& root, const std::filesystem::path& directory)
{
    std::lock_guard lock(g_contentMutex);
    g_contentRoots[LowerCase(root)] = directory;
}

void UnmountContentRoot(const std::string& root)
{
    std::lock_guard lock(g_contentMutex);
    g_contentRoots.erase(LowerCase(root));
}

static std::filesystem::path ResolvePath(const std::string& guestPath)
{
    size_t colon = guestPath.find(':');
    std::string device = colon != std::string::npos ? guestPath.substr(0, colon) : std::string();
    std::string rest = colon != std::string::npos ? guestPath.substr(colon + 1) : guestPath;
    rest.erase(0, rest.find_first_not_of("\\/"));
    std::replace(rest.begin(), rest.end(), '/', '\\');
    device = LowerCase(device);
    device.erase(0, device.find_first_not_of("\\/"));

    if (device.empty() || device == "game" || device == "d")
        return g_gameRoot / rest;
    {
        std::lock_guard lock(g_contentMutex);
        auto it = g_contentRoots.find(device);
        if (it != g_contentRoots.end())
            return it->second / rest;
    }
    return std::filesystem::current_path() / "userdata" / device / rest;
}

static void SetIoStatus(uint32_t ioStatus, uint32_t status, uint32_t information)
{
    if (ioStatus != 0)
    {
        Store32(ioStatus + 0, status);
        Store32(ioStatus + 4, information);
    }
}

static KObject* FileFromHandle(uint32_t handle)
{
    KObject* object = ObjectFromHandle(handle);
    return object != nullptr && object->type == KObjectType::File ? object : nullptr;
}

static uint64_t FileTimeToU64(const FILETIME& time)
{
    return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

static void OpenFile(PPCContext& ctx, uint32_t disposition)
{
    uint32_t handleOut = ctx.r3.u32;
    uint32_t access = ctx.r4.u32;
    uint32_t ioStatus = ctx.r6.u32;
    std::string guestPath = ReadObjectName(ctx.r5.u32);
    std::filesystem::path path = ResolvePath(guestPath);

    bool write = (access & (0x40000000 | 0x10000000 | 0x2 | 0x4)) != 0;
    DWORD creation = OPEN_EXISTING;
    switch (disposition)
    {
    case 0: case 5: creation = CREATE_ALWAYS; break;
    case 2: creation = CREATE_NEW; break;
    case 3: creation = OPEN_ALWAYS; break;
    case 4: creation = TRUNCATE_EXISTING; break;
    }
    if (creation != OPEN_EXISTING)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    HANDLE host = CreateFileW(path.c_str(), GENERIC_READ | (write ? GENERIC_WRITE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (host == INVALID_HANDLE_VALUE)
    {
        Log("File open FAILED: '%s' -> %ls", guestPath.c_str(), path.c_str());
        SetIoStatus(ioStatus, STATUS_OBJECT_NAME_NOT_FOUND, 0);
        ctx.r3.u64 = STATUS_OBJECT_NAME_NOT_FOUND;
        return;
    }

    auto* object = new KObject();
    object->type = KObjectType::File;
    object->host = host;
    Store32(handleOut, ObjectCreateHandle(object));
    Log("File open: '%s'", guestPath.c_str());
    GameStateFileOpened(guestPath.c_str());
    SetIoStatus(ioStatus, STATUS_SUCCESS, 1);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtCreateFile)
{
    OpenFile(ctx, ctx.r10.u32);
}

GUEST_IMPORT(NtOpenFile)
{
    OpenFile(ctx, 1);
}

static void Transfer(PPCContext& ctx, bool write)
{
    KObject* file = FileFromHandle(ctx.r3.u32);
    uint32_t event = ctx.r4.u32;
    uint32_t ioStatus = ctx.r7.u32;
    if (file == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }

    if (ctx.r10.u32 != 0)
    {
        LARGE_INTEGER offset;
        offset.QuadPart = int64_t(Load64(ctx.r10.u32));
        SetFilePointerEx(file->host, offset, nullptr, FILE_BEGIN);
    }

    DWORD transferred = 0;
    BOOL ok = write
        ? WriteFile(file->host, g_base + ctx.r8.u32, ctx.r9.u32, &transferred, nullptr)
        : ReadFile(file->host, g_base + ctx.r8.u32, ctx.r9.u32, &transferred, nullptr);

    uint32_t status = STATUS_SUCCESS;
    if (!ok)
        status = STATUS_UNSUCCESSFUL;
    else if (!write && transferred == 0 && ctx.r9.u32 != 0)
        status = STATUS_END_OF_FILE;

    SetIoStatus(ioStatus, status, transferred);
    if (KObject* eventObject = event ? ObjectFromHandle(event) : nullptr)
        SetEvent(eventObject->host);
    ctx.r3.u64 = status;
}

GUEST_IMPORT(NtReadFile)
{
    Transfer(ctx, false);
}

GUEST_IMPORT(NtWriteFile)
{
    Transfer(ctx, true);
}

GUEST_IMPORT(NtFlushBuffersFile)
{
    if (KObject* file = FileFromHandle(ctx.r3.u32))
        FlushFileBuffers(file->host);
    ctx.r3.u64 = STATUS_SUCCESS;
}

static void StoreNetworkOpenInformation(uint32_t buffer, const BY_HANDLE_FILE_INFORMATION& info)
{
    uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    Store64(buffer + 0x00, FileTimeToU64(info.ftCreationTime));
    Store64(buffer + 0x08, FileTimeToU64(info.ftLastAccessTime));
    Store64(buffer + 0x10, FileTimeToU64(info.ftLastWriteTime));
    Store64(buffer + 0x18, FileTimeToU64(info.ftLastWriteTime));
    Store64(buffer + 0x20, (size + 0xFFF) & ~uint64_t(0xFFF));
    Store64(buffer + 0x28, size);
    Store32(buffer + 0x30, info.dwFileAttributes);
}

GUEST_IMPORT(NtQueryInformationFile)
{
    KObject* file = FileFromHandle(ctx.r3.u32);
    uint32_t ioStatus = ctx.r4.u32;
    uint32_t buffer = ctx.r5.u32;
    if (file == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    GetFileInformationByHandle(file->host, &info);
    uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;

    uint32_t written = 0;
    switch (ctx.r7.u32)
    {
    case 5: // FileStandardInformation
        Store64(buffer + 0x00, (size + 0xFFF) & ~uint64_t(0xFFF));
        Store64(buffer + 0x08, size);
        Store32(buffer + 0x10, 1);
        g_base[buffer + 0x14] = 0;
        g_base[buffer + 0x15] = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        written = 0x18;
        break;
    case 14: // FilePositionInformation
    {
        LARGE_INTEGER zero{}, position{};
        SetFilePointerEx(file->host, zero, &position, FILE_CURRENT);
        Store64(buffer, uint64_t(position.QuadPart));
        written = 8;
        break;
    }
    case 34: // FileNetworkOpenInformation
        StoreNetworkOpenInformation(buffer, info);
        written = 0x38;
        break;
    default:
        Log("NtQueryInformationFile: unsupported class %u", ctx.r7.u32);
        SetIoStatus(ioStatus, STATUS_INVALID_INFO_CLASS, 0);
        ctx.r3.u64 = STATUS_INVALID_INFO_CLASS;
        return;
    }

    SetIoStatus(ioStatus, STATUS_SUCCESS, written);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtSetInformationFile)
{
    KObject* file = FileFromHandle(ctx.r3.u32);
    if (file == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }

    LARGE_INTEGER value;
    value.QuadPart = int64_t(Load64(ctx.r5.u32));
    switch (ctx.r7.u32)
    {
    case 14: // FilePositionInformation
        SetFilePointerEx(file->host, value, nullptr, FILE_BEGIN);
        break;
    case 20: // FileEndOfFileInformation
        SetFilePointerEx(file->host, value, nullptr, FILE_BEGIN);
        SetEndOfFile(file->host);
        break;
    }
    SetIoStatus(ctx.r4.u32, STATUS_SUCCESS, 0);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtQueryFullAttributesFile)
{
    std::string guestPath = ReadObjectName(ctx.r3.u32);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(ResolvePath(guestPath).c_str(), GetFileExInfoStandard, &data))
    {
        ctx.r3.u64 = STATUS_OBJECT_NAME_NOT_FOUND;
        return;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    info.dwFileAttributes = data.dwFileAttributes;
    info.ftCreationTime = data.ftCreationTime;
    info.ftLastAccessTime = data.ftLastAccessTime;
    info.ftLastWriteTime = data.ftLastWriteTime;
    info.nFileSizeHigh = data.nFileSizeHigh;
    info.nFileSizeLow = data.nFileSizeLow;
    StoreNetworkOpenInformation(ctx.r4.u32, info);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtQueryVolumeInformationFile)
{
    uint32_t buffer = ctx.r5.u32;
    memset(g_base + buffer, 0, ctx.r6.u32);
    if (ctx.r7.u32 == 3 && ctx.r6.u32 >= 0x18) // FileFsSizeInformation
    {
        Store64(buffer + 0x00, 0x1000000);
        Store64(buffer + 0x08, 0x800000);
        Store32(buffer + 0x10, 8);
        Store32(buffer + 0x14, 512);
    }
    SetIoStatus(ctx.r4.u32, STATUS_SUCCESS, ctx.r6.u32);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtQueryDirectoryFile)
{
    Log("NtQueryDirectoryFile: directory enumeration is not implemented");
    SetIoStatus(ctx.r7.u32, STATUS_NO_MORE_FILES, 0);
    ctx.r3.u64 = STATUS_NO_MORE_FILES;
}

GUEST_IMPORT(ObCreateSymbolicLink)
{
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(ObDeleteSymbolicLink)
{
    ctx.r3.u64 = STATUS_SUCCESS;
}

// Reads into a list of page-sized buffers. The game imports it but has never
// been seen to call it; refuse cleanly rather than pretend data was read.
GUEST_IMPORT(NtReadFileScatter)
{
    Log("NtReadFileScatter is not supported (lr=%08X)", uint32_t(ctx.lr));
    ctx.r3.u64 = 0xC0000002; // STATUS_NOT_IMPLEMENTED
}