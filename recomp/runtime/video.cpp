// Video imports. The game links Direct3D statically and drives the Xenos GPU through
// its register block and a command ring buffer. This file answers the display
// queries and hands the ring buffer setup to the command processor in gpu.cpp.
#include "gpu.h"
#include "xex_header.h"

bool VideoInit()
{
    return GpuInit();
}

static void StoreFloat(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, 4);
    Store32(address, bits);
}

static void FillVideoMode(uint32_t mode)
{
    memset(g_base + mode, 0, 0x30);
    Store32(mode + 0x00, 1280);
    Store32(mode + 0x04, 720);
    Store32(mode + 0x08, 0);     // interlaced
    Store32(mode + 0x0C, 1);     // widescreen
    Store32(mode + 0x10, 1);     // high definition
    StoreFloat(mode + 0x14, 60.0f);
    Store32(mode + 0x18, 1);     // NTSC-M
    Store32(mode + 0x1C, 0x4A);
    Store32(mode + 0x20, 1);
}

GUEST_IMPORT(XGetVideoMode)
{
    FillVideoMode(ctx.r3.u32);
}

GUEST_IMPORT(VdQueryVideoMode)
{
    FillVideoMode(ctx.r3.u32);
}

GUEST_IMPORT(VdQueryVideoFlags)
{
    ctx.r3.u64 = 3; // widescreen | high definition
}

GUEST_IMPORT(VdGetCurrentDisplayGamma)
{
    Store32(ctx.r3.u32, 2); // sRGB
    StoreFloat(ctx.r4.u32, 2.22222233f);
}

GUEST_IMPORT(VdGetCurrentDisplayInformation)
{
    memset(g_base + ctx.r3.u32, 0, 0x58);
    Store16(ctx.r3.u32 + 0, 1280);
    Store16(ctx.r3.u32 + 2, 720);
}

GUEST_IMPORT(VdInitializeEngines)
{
    ctx.r3.u64 = 1;
}

GUEST_IMPORT(VdShutdownEngines) {}

GUEST_IMPORT(VdSetGraphicsInterruptCallback)
{
    GpuSetInterruptCallback(ctx.r3.u32, ctx.r4.u32);
}

GUEST_IMPORT(VdInitializeRingBuffer)
{
    GpuInitRingBuffer(ctx.r3.u32, ctx.r4.u32);
}

GUEST_IMPORT(VdEnableRingBufferRPtrWriteBack)
{
    GpuEnableReadPointerWriteBack(ctx.r3.u32);
}

GUEST_IMPORT(VdSetSystemCommandBufferGpuIdentifierAddress) {}

GUEST_IMPORT(VdGetSystemCommandBuffer)
{
    Store32(ctx.r3.u32, 0);
    Store32(ctx.r4.u32, 0);
}

GUEST_IMPORT(VdIsHSIOTrainingSucceeded)
{
    ctx.r3.u64 = 1;
}

GUEST_IMPORT(VdRetrainEDRAM)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(VdRetrainEDRAMWorker)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(VdEnableDisableClockGating)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(VdSetDisplayMode)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(VdCallGraphicsNotificationRoutines)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(VdPersistDisplay)
{
    if (ctx.r4.u32 != 0)
        Store32(ctx.r4.u32, GuestAllocPhysical(0x1000));
    ctx.r3.u64 = 1;
}

GUEST_IMPORT(VdInitializeScalerCommandBuffer)
{
    // Ninth and tenth arguments live in the caller's parameter save area.
    uint32_t destination = Load32(ctx.r1.u32 + 0x54);
    uint32_t count = Load32(ctx.r1.u32 + 0x5C);

    // In this SDK version the tenth argument is not a word count (a pointer
    // shows up there), so anything implausible is left untouched.
    if (count > 0x1000)
    {
        ctx.r3.u64 = 0;
        return;
    }
    for (uint32_t i = 0; i < count; i++)
        Store32(destination + i * 4, 0x80000000); // GPU no-op packet
    ctx.r3.u64 = count;
}

GUEST_IMPORT(VdSwap)
{
    // r3: 64-word slot in the command stream, r8: pointer to the front buffer
    // address. Width and height are the ninth and tenth arguments.
    uint32_t frontBuffer = ctx.r8.u32 ? Load32(ctx.r8.u32) : 0;
    uint32_t width = Load32(ctx.r1.u32 + 0x54);
    uint32_t height = Load32(ctx.r1.u32 + 0x5C);
    if (width == 0 || width > 4096 || height == 0 || height > 4096)
    {
        width = 1280;
        height = 720;
    }
    GpuWriteSwapPacket(ctx.r3.u32, frontBuffer, width, height);
}
