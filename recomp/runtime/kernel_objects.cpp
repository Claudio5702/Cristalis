// Kernel objects: threads, events, semaphores, waits and spin locks.
// Guest handles and guest dispatcher-object pointers both resolve to a host HANDLE.
#include "kernel_objects.h"
#include "xex_header.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

constexpr uint32_t X_STATUS_TIMEOUT = 0x00000102;
constexpr uint32_t X_STATUS_INVALID_HANDLE = 0xC0000008;

static std::mutex g_objectMutex;
static std::unordered_map<uint32_t, KObject*> g_handles;
static std::unordered_map<uint32_t, KObject*> g_pointers;
static uint32_t g_nextHandle = 0xF8000004;
static thread_local KObject* t_currentThread;

uint32_t ObjectCreateHandle(KObject* object)
{
    std::lock_guard lock(g_objectMutex);
    uint32_t handle = g_nextHandle;
    g_nextHandle += 4;
    g_handles[handle] = object;
    return handle;
}

KObject* ObjectFromHandle(uint32_t handle)
{
    if (handle == 0xFFFFFFFE)
        return t_currentThread;
    std::lock_guard lock(g_objectMutex);
    auto it = g_handles.find(handle);
    return it != g_handles.end() ? it->second : nullptr;
}

void ObjectCloseHandle(uint32_t handle)
{
    std::lock_guard lock(g_objectMutex);
    g_handles.erase(handle);
}

// Dispatcher objects embedded in guest memory (KEVENT, KSEMAPHORE) get a host
// object the first time they are used.
static KObject* ObjectFromPointer(uint32_t pointer)
{
    std::lock_guard lock(g_objectMutex);
    auto it = g_pointers.find(pointer);
    if (it != g_pointers.end())
        return it->second;

    auto* object = new KObject();
    uint8_t type = g_base[pointer];
    int32_t signalState = int32_t(Load32(pointer + 4));
    if (type == 5)
    {
        object->type = KObjectType::Semaphore;
        int32_t limit = int32_t(Load32(pointer + 0x10));
        object->host = CreateSemaphoreW(nullptr, signalState, limit > 0 ? limit : 0x7FFFFFFF, nullptr);
    }
    else
    {
        object->type = KObjectType::Event;
        object->host = CreateEventW(nullptr, type != 1, signalState != 0, nullptr);
    }
    g_pointers[pointer] = object;
    return object;
}

static DWORD TimeoutToMilliseconds(uint32_t timeoutPointer)
{
    if (timeoutPointer == 0)
        return INFINITE;
    int64_t interval = int64_t(Load64(timeoutPointer));
    return interval < 0 ? DWORD(-interval / 10000) : 0;
}

static uint32_t Wait(HANDLE* handles, uint32_t count, bool waitAll, uint32_t timeoutPointer)
{
    DWORD result = WaitForMultipleObjects(count, handles, waitAll, TimeoutToMilliseconds(timeoutPointer));
    if (result < WAIT_OBJECT_0 + count)
        return waitAll ? STATUS_SUCCESS : result - WAIT_OBJECT_0;
    return result == WAIT_TIMEOUT ? X_STATUS_TIMEOUT : STATUS_UNSUCCESSFUL;
}

// ---- Threads ----

struct ThreadStart
{
    uint32_t xapiStartup;
    uint32_t startAddress;
    uint32_t startContext;
    uint32_t pcr;
    uint32_t stackTop;
    KObject* object;
};

// The guest reads a thread's fate straight from its KTHREAD: the dispatcher
// header's signal state, then the exit status.
static void MarkThreadExited(PPCContext& ctx, uint32_t exitCode)
{
    uint32_t thread = Load32(ctx.r13.u32 + 0x100);
    Store32(thread + 0x140, exitCode);
    Store32(thread + 4, 1);
}

// The KTHREAD stays, since the guest may still look at it, but the guest stack
// is no longer referenced once the thread's code has returned.
static void ReleaseThreadStack(PPCContext& ctx)
{
    GuestUnregisterContext();
    GuestFreeStack(Load32(ctx.r13.u32 + 0x74));
}

static DWORD WINAPI GuestThreadMain(void* parameter)
{
    std::unique_ptr<ThreadStart> start((ThreadStart*)parameter);
    t_currentThread = start->object;

    PPCContext ctx{};
    ctx.fpscr.loadFromHost();
    GuestRegisterContext(&ctx);
    ctx.r1.u64 = start->stackTop - 0x100;
    ctx.r13.u64 = start->pcr;
    if (start->xapiStartup != 0)
    {
        ctx.r3.u64 = start->startAddress;
        ctx.r4.u64 = start->startContext;
        (PPC_LOOKUP_FUNC(g_base, start->xapiStartup))(ctx, g_base);
    }
    else
    {
        ctx.r3.u64 = start->startContext;
        (PPC_LOOKUP_FUNC(g_base, start->startAddress))(ctx, g_base);
    }
    MarkThreadExited(ctx, ctx.r3.u32);
    ReleaseThreadStack(ctx);
    return ctx.r3.u32;
}

GUEST_IMPORT(ExCreateThread)
{
    uint32_t handleOut = ctx.r3.u32;
    uint32_t stackSize = ctx.r4.u32 ? ctx.r4.u32 : 0x10000;
    uint32_t threadIdOut = ctx.r5.u32;
    uint32_t creationFlags = ctx.r9.u32;

    auto* start = new ThreadStart();
    start->xapiStartup = ctx.r6.u32;
    start->startAddress = ctx.r7.u32;
    start->startContext = ctx.r8.u32;
    start->pcr = GuestCreateThreadBlock(stackSize, start->stackTop);
    if (start->pcr == 0)
    {
        delete start;
        ctx.r3.u64 = STATUS_NO_MEMORY;
        return;
    }

    auto* object = new KObject();
    object->type = KObjectType::Thread;
    start->object = object;
    {
        std::lock_guard lock(g_objectMutex);
        g_pointers[Load32(start->pcr + 0x100)] = object;
    }

    // The recompiled code keeps guest locals on the host stack as well.
    object->host = CreateThread(nullptr, 4 * 1024 * 1024, GuestThreadMain, start, CREATE_SUSPENDED, nullptr);
    uint32_t handle = ObjectCreateHandle(object);
    Log("ExCreateThread: start=%08X context=%08X stack=0x%X suspended=%u -> handle %08X, thread id %X",
        start->startAddress, start->startContext, stackSize, creationFlags & 1, handle,
        Load32(Load32(start->pcr + 0x100) + 0x14C));

    if (handleOut != 0)
        Store32(handleOut, handle);
    if (threadIdOut != 0)
        Store32(threadIdOut, Load32(Load32(start->pcr + 0x100) + 0x14C));
    if (!(creationFlags & 1))
        ResumeThread(object->host);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtResumeThread)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object == nullptr || object->type != KObjectType::Thread)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    DWORD previous = ResumeThread(object->host);
    if (ctx.r4.u32 != 0)
        Store32(ctx.r4.u32, previous);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(ExTerminateThread)
{
    MarkThreadExited(ctx, ctx.r3.u32);
    ReleaseThreadStack(ctx);
    ExitThread(ctx.r3.u32);
}

GUEST_IMPORT(KeSetAffinityThread)
{
    ctx.r3.u64 = 1;
}

GUEST_IMPORT(KeSetBasePriorityThread)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(KiApcNormalRoutineNop)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(KeEnterCriticalRegion) {}
GUEST_IMPORT(KeLeaveCriticalRegion) {}
GUEST_IMPORT(KeLockL2) {}
GUEST_IMPORT(KeUnlockL2) {}

GUEST_IMPORT(ExRegisterTitleTerminateNotification) {}

// ---- Handles ----

GUEST_IMPORT(NtClose)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object != nullptr && object->type == KObjectType::File)
    {
        CloseHandle(object->host);
        ObjectCloseHandle(ctx.r3.u32);
        delete object;
    }
    else if (object != nullptr)
    {
        // Other objects may still be referenced by pointer; keep them alive.
        ObjectCloseHandle(ctx.r3.u32);
    }
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtDuplicateObject)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    if (ctx.r4.u32 != 0)
        Store32(ctx.r4.u32, ObjectCreateHandle(object));
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(ObReferenceObjectByHandle)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }

    // Give the object a guest address so it can be waited on by pointer.
    std::lock_guard lock(g_objectMutex);
    if (object->guestAddress == 0)
    {
        for (auto& [pointer, candidate] : g_pointers)
        {
            if (candidate == object)
                object->guestAddress = pointer;
        }
        if (object->guestAddress == 0)
        {
            object->guestAddress = GuestAllocVirtual(0x1000, 0x1000);
            g_pointers[object->guestAddress] = object;
        }
    }
    if (ctx.r5.u32 != 0)
        Store32(ctx.r5.u32, object->guestAddress);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(ObDereferenceObject) {}

// ---- Events and semaphores ----

GUEST_IMPORT(NtCreateEvent)
{
    auto* object = new KObject();
    object->type = KObjectType::Event;
    object->host = CreateEventW(nullptr, ctx.r5.u32 == 0, ctx.r6.u32 != 0, nullptr);
    Store32(ctx.r3.u32, ObjectCreateHandle(object));
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtSetEvent)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    SetEvent(object->host);
    if (ctx.r4.u32 != 0)
        Store32(ctx.r4.u32, 0);
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtClearEvent)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object != nullptr)
        ResetEvent(object->host);
    ctx.r3.u64 = object ? STATUS_SUCCESS : X_STATUS_INVALID_HANDLE;
}

GUEST_IMPORT(KeSetEvent)
{
    SetEvent(ObjectFromPointer(ctx.r3.u32)->host);
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(KeResetEvent)
{
    ResetEvent(ObjectFromPointer(ctx.r3.u32)->host);
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(NtCreateSemaphore)
{
    auto* object = new KObject();
    object->type = KObjectType::Semaphore;
    object->host = CreateSemaphoreW(nullptr, LONG(ctx.r5.u32), LONG(ctx.r6.u32), nullptr);
    Store32(ctx.r3.u32, ObjectCreateHandle(object));
    ctx.r3.u64 = STATUS_SUCCESS;
}

GUEST_IMPORT(NtReleaseSemaphore)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    LONG previous = 0;
    ReleaseSemaphore(object->host, LONG(ctx.r4.u32), &previous);
    if (ctx.r5.u32 != 0)
        Store32(ctx.r5.u32, uint32_t(previous));
    ctx.r3.u64 = STATUS_SUCCESS;
}

// ---- Waits ----

GUEST_IMPORT(NtWaitForSingleObjectEx)
{
    KObject* object = ObjectFromHandle(ctx.r3.u32);
    if (object == nullptr)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    ctx.r3.u64 = Wait(&object->host, 1, false, ctx.r6.u32);
}

GUEST_IMPORT(KeWaitForSingleObject)
{
    KObject* object = ObjectFromPointer(ctx.r3.u32);
    ctx.r3.u64 = Wait(&object->host, 1, false, ctx.r7.u32);
}

GUEST_IMPORT(NtWaitForMultipleObjectsEx)
{
    uint32_t count = ctx.r3.u32;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
    {
        ctx.r3.u64 = STATUS_UNSUCCESSFUL;
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        KObject* object = ObjectFromHandle(Load32(ctx.r4.u32 + i * 4));
        if (object == nullptr)
        {
            ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
            return;
        }
        handles[i] = object->host;
    }
    // WaitType: 0 = all, 1 = any.
    ctx.r3.u64 = Wait(handles, count, ctx.r5.u32 == 0, ctx.r8.u32);
}

GUEST_IMPORT(KeWaitForMultipleObjects)
{
    uint32_t count = ctx.r3.u32;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
    {
        ctx.r3.u64 = STATUS_UNSUCCESSFUL;
        return;
    }
    for (uint32_t i = 0; i < count; i++)
        handles[i] = ObjectFromPointer(Load32(ctx.r4.u32 + i * 4))->host;
    ctx.r3.u64 = Wait(handles, count, ctx.r5.u32 == 0, ctx.r9.u32);
}

// ---- Spin locks ----
// The lock word lives in guest memory; any non-zero value means held.

static void SpinAcquire(uint32_t lock)
{
    auto& word = *(std::atomic<uint32_t>*)(g_base + lock);
    uint32_t expected = 0;
    while (!word.compare_exchange_weak(expected, 1, std::memory_order_acquire))
    {
        expected = 0;
        YieldProcessor();
    }
}

static void SpinRelease(uint32_t lock)
{
    ((std::atomic<uint32_t>*)(g_base + lock))->store(0, std::memory_order_release);
}

GUEST_IMPORT(KfAcquireSpinLock)
{
    SpinAcquire(ctx.r3.u32);
    ctx.r3.u64 = 0; // previous IRQL
}

GUEST_IMPORT(KfReleaseSpinLock)
{
    SpinRelease(ctx.r3.u32);
}

GUEST_IMPORT(KeAcquireSpinLockAtRaisedIrql)
{
    SpinAcquire(ctx.r3.u32);
}

GUEST_IMPORT(KeReleaseSpinLockFromRaisedIrql)
{
    SpinRelease(ctx.r3.u32);
}
