// xam.xex content imports: storage device selection and save game packages.
// A package is a host directory under saves\<file name>.
#include "config.h"
#include "content.h"
#include "kernel_objects.h"

#include <map>
#include <mutex>
#include <vector>

constexpr uint32_t X_ERROR_SUCCESS = 0;
constexpr uint32_t X_ERROR_FILE_NOT_FOUND = 2;
constexpr uint32_t X_ERROR_NO_MORE_FILES = 18;
constexpr uint32_t X_ERROR_ALREADY_EXISTS = 183;
constexpr uint32_t X_ERROR_IO_PENDING = 997;
constexpr uint32_t X_ERROR_DEVICE_NOT_CONNECTED = 0x48F;

constexpr uint32_t kHardDriveDeviceId = 1;
constexpr uint32_t kContentDataSize = 0x134; // XCONTENT_DATA

// Completes an XOVERLAPPED request, or returns the result directly without one.
static uint32_t Complete(uint32_t overlapped, uint32_t result, uint32_t length = 0)
{
    if (overlapped == 0)
        return result;

    Store32(overlapped + 0x00, result);
    Store32(overlapped + 0x04, length);
    Store32(overlapped + 0x18, result);
    if (KObject* event = ObjectFromHandle(Load32(overlapped + 0x0C)))
        SetEvent(event->host);
    return X_ERROR_IO_PENDING;
}

// XCONTENT_DATA: device id, content type, display name (128 UTF-16), file name (42 chars).
static std::string ContentFileName(uint32_t contentData)
{
    char name[43]{};
    memcpy(name, g_base + contentData + 0x108, 42);
    return name;
}

GUEST_IMPORT(XamShowDeviceSelectorUI)
{
    // No dialog: the only device is the "hard drive" backed by the saves folder.
    if (ctx.r7.u32 != 0)
        Store32(ctx.r7.u32, kHardDriveDeviceId);
    ctx.r3.u64 = Complete(ctx.r8.u32, X_ERROR_SUCCESS);
}

// The game keeps the chosen storage device in one variable, cleared by this
// function at start-up, and asks the player to pick a device while it is zero.
// Putting the only device there straight away spares the two dialogs.
PPC_FUNC_IMPL(__imp__sub_820EAF58);
PPC_FUNC(sub_820EAF58)
{
    constexpr uint32_t kSelectedDevice = 0x823D8280;
    __imp__sub_820EAF58(ctx, base);
    if (!GetConfig().storageDialog)
        Store32(kSelectedDevice, kHardDriveDeviceId);
}

GUEST_IMPORT(XamContentGetDeviceState)
{
    uint32_t result = ctx.r3.u32 == kHardDriveDeviceId ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
    ctx.r3.u64 = Complete(ctx.r4.u32, result);
}

GUEST_IMPORT(XamContentGetDeviceData)
{
    if (ctx.r3.u32 != kHardDriveDeviceId)
    {
        ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }

    uint32_t data = ctx.r4.u32;
    memset(g_base + data, 0, 0x50);
    Store32(data + 0x00, kHardDriveDeviceId);
    Store32(data + 0x04, 1); // hard drive
    Store64(data + 0x08, 20ull << 30);
    Store64(data + 0x10, 16ull << 30);
    const char name[] = "Hard Drive";
    for (size_t i = 0; i < sizeof(name); i++)
        Store16(data + 0x18 + uint32_t(i) * 2, uint16_t(name[i]));
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamContentCreateEx)
{
    std::string root = (const char*)g_base + ctx.r4.u32;
    std::string fileName = ContentFileName(ctx.r5.u32);
    uint32_t disposition = ctx.r6.u32 & 0xF;
    uint32_t overlapped = Load32(ctx.r1.u32 + 0x54); // ninth argument

    std::filesystem::path directory = ContentDirectory() / fileName;
    std::error_code error;
    bool exists = std::filesystem::is_directory(directory, error);

    // 1 create new, 2 create always, 3 open existing, 4 open always, 5 truncate existing.
    uint32_t result = X_ERROR_SUCCESS;
    bool created = false;
    switch (disposition)
    {
    case 1:
        if (exists)
            result = X_ERROR_ALREADY_EXISTS;
        else
            created = true;
        break;
    case 2:
        std::filesystem::remove_all(directory, error);
        created = true;
        break;
    case 3:
    case 5:
        if (!exists)
            result = X_ERROR_FILE_NOT_FOUND;
        break;
    default:
        created = !exists;
        break;
    }

    if (result == X_ERROR_SUCCESS)
    {
        std::filesystem::create_directories(directory, error);
        MountContentRoot(root, directory);
        if (ctx.r7.u32 != 0)
            Store32(ctx.r7.u32, created ? 1 : 2); // created / opened
        if (ctx.r8.u32 != 0)
            Store32(ctx.r8.u32, 0);               // no extra license bits
    }
    Log("Content: open '%s' as %s: (disposition %u) -> %u", fileName.c_str(), root.c_str(), disposition, result);
    ctx.r3.u64 = Complete(overlapped, result, created ? 1 : 2);
}

GUEST_IMPORT(XamContentClose)
{
    UnmountContentRoot((const char*)g_base + ctx.r3.u32);
    ctx.r3.u64 = Complete(ctx.r4.u32, X_ERROR_SUCCESS);
}

GUEST_IMPORT(XamContentSetThumbnail)
{
    ctx.r3.u64 = Complete(ctx.r7.u32, X_ERROR_SUCCESS);
}

// ---- Enumeration of existing packages ----

struct ContentEnumerator
{
    std::vector<std::string> names; // packages not returned yet
    uint32_t itemsPerCall{};
    uint32_t resultSize{};          // leaderboard enumerators: size of the result buffer
};
static std::mutex g_enumeratorMutex;
static std::map<uint32_t, ContentEnumerator> g_enumerators; // by handle

GUEST_IMPORT(XamContentCreateEnumerator)
{
    std::vector<std::string> names;
    std::error_code error;
    for (auto& entry : std::filesystem::directory_iterator(ContentDirectory(), error))
    {
        // DarkLauncher keeps its copies of written-over files in saves\backup.
        if (entry.is_directory() && entry.path().filename() != "backup")
            names.push_back(entry.path().filename().string());
    }

    auto* object = new KObject();
    object->type = KObjectType::Event;
    object->host = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    uint32_t handle = ObjectCreateHandle(object);
    size_t found = names.size();
    {
        std::lock_guard lock(g_enumeratorMutex);
        g_enumerators[handle] = { std::move(names), ctx.r7.u32 ? ctx.r7.u32 : 1 };
    }
    Log("Content: %zu saved package(s) found", found);
    if (ctx.r8.u32 != 0)
        Store32(ctx.r8.u32, kContentDataSize * (ctx.r7.u32 ? ctx.r7.u32 : 1));
    if (ctx.r9.u32 != 0)
        Store32(ctx.r9.u32, handle);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// Leaderboards are an Xbox LIVE service: the enumerator exists but holds no rows.
// Arguments: title, kind (by rank or around a player), pivot, rows, view count,
// views, then where to return the buffer size and the handle.
GUEST_IMPORT(XamUserCreateStatsEnumerator)
{
    uint32_t rows = ctx.r6.u32 ? ctx.r6.u32 : 1;
    uint32_t views = ctx.r7.u32 ? ctx.r7.u32 : 1;
    uint32_t size = 0x100 + views * (0x20 + rows * 0x680);

    auto* object = new KObject();
    object->type = KObjectType::Event;
    object->host = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    uint32_t handle = ObjectCreateHandle(object);
    {
        std::lock_guard lock(g_enumeratorMutex);
        g_enumerators[handle] = { {}, 1, size };
    }
    Log("Leaderboard: enumerator for %u row(s) of %u view(s), empty (no online service)", rows, views);
    if (ctx.r9.u32 != 0)
        Store32(ctx.r9.u32, size);
    if (ctx.r10.u32 != 0)
        Store32(ctx.r10.u32, handle);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamEnumerate)
{
    uint32_t buffer = ctx.r5.u32;
    uint32_t itemsOut = ctx.r7.u32;
    uint32_t overlapped = ctx.r8.u32;

    uint32_t count = 0;
    {
        std::lock_guard lock(g_enumeratorMutex);
        auto it = g_enumerators.find(ctx.r3.u32);

        // An empty leaderboard: a result header announcing no view.
        if (it != g_enumerators.end() && it->second.resultSize != 0 && buffer != 0)
            memset(g_base + buffer, 0, ctx.r6.u32 < it->second.resultSize ? ctx.r6.u32 : it->second.resultSize);

        // The item count fixed when the enumerator was created decides how much
        // is written, not the buffer length passed here: the game passes 4 (the
        // size of a pointer), which the console accepts.
        while (it != g_enumerators.end() && !it->second.names.empty() && count < it->second.itemsPerCall)
        {
            const std::string name = it->second.names.back();
            it->second.names.pop_back();

            uint32_t item = buffer + count * kContentDataSize;
            memset(g_base + item, 0, kContentDataSize);
            Store32(item + 0x00, kHardDriveDeviceId);
            Store32(item + 0x04, 1); // saved game
            for (size_t i = 0; i < name.size() && i < 127; i++)
                Store16(item + 0x08 + uint32_t(i) * 2, uint16_t(uint8_t(name[i])));
            memcpy(g_base + item + 0x108, name.c_str(), name.size() < 42 ? name.size() : 42);
            count++;
        }
    }

    uint32_t result = count != 0 ? X_ERROR_SUCCESS : X_ERROR_NO_MORE_FILES;
    if (itemsOut != 0)
        Store32(itemsOut, count);
    ctx.r3.u64 = Complete(overlapped, result, count);
}
