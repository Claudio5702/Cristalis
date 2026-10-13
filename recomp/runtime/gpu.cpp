// Xenos GPU command processor. The game's statically linked Direct3D writes PM4
// packets into a ring buffer and kicks the write pointer register; this thread
// consumes them, keeps the register file, raises the graphics interrupts and
// turns draws into calls on the host renderer.
#include "gpu.h"
#include "renderer.h"
#include "xex_header.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

constexpr uint32_t kRegisterBase = 0x7FC80000;
constexpr uint32_t kRegisterCount = 0x5003;

// Register indices (word offsets from kRegisterBase).
constexpr uint32_t REG_CP_RB_RPTR = 0x01C4;
constexpr uint32_t REG_CP_RB_WPTR = 0x01C5;
constexpr uint32_t REG_SCRATCH_UMSK = 0x01DC;
constexpr uint32_t REG_SCRATCH_ADDR = 0x01DD;
constexpr uint32_t REG_SCRATCH_REG0 = 0x0578;
constexpr uint32_t REG_D1MODE_VBLANK_STATUS = 0x1951;
constexpr uint32_t REG_D1MODE_VIEWPORT_SIZE = 0x1961;

// PM4 type 3 opcodes.
enum : uint32_t
{
    PM4_REG_RMW = 0x21,
    PM4_DRAW_INDX = 0x22,
    PM4_IM_LOAD = 0x27,
    PM4_IM_LOAD_IMMEDIATE = 0x2B,
    PM4_SET_CONSTANT = 0x2D,
    PM4_LOAD_ALU_CONSTANT = 0x2F,
    PM4_DRAW_INDX_2 = 0x36,
    PM4_MEM_WRITE = 0x3D,
    PM4_INDIRECT_BUFFER = 0x3F,
    PM4_COND_WRITE = 0x45,
    PM4_INTERRUPT = 0x54,
    PM4_SET_CONSTANT2 = 0x55,
    PM4_SET_SHADER_CONSTANTS = 0x56,
    PM4_EVENT_WRITE_SHD = 0x58,
    PM4_EVENT_WRITE_EXT = 0x5A,
    PM4_SET_BIN_MASK_LO = 0x60,
    PM4_SET_BIN_MASK_HI = 0x61,
    PM4_SET_BIN_SELECT_LO = 0x62,
    PM4_SET_BIN_SELECT_HI = 0x63,
    PM4_XE_SWAP = 0x64,
};

static uint32_t g_registers[kRegisterCount];

static uint32_t g_ringBase;       // guest virtual address
static uint32_t g_ringWordCount;
static uint32_t g_readPointerWriteBack;
static uint32_t g_readIndex;

static uint32_t g_interruptCallback;
static uint32_t g_interruptContext;
static std::mutex g_interruptMutex;

static std::atomic<uint32_t> g_frameCount;
static std::atomic<uint32_t> g_vblankCount;
static uint32_t g_drawCount;
// CD_STATS accumulators, reset every report.
static std::atomic<uint32_t> g_statDraws;
static std::atomic<uint64_t> g_statDrawMicros;
static std::atomic<uint64_t> g_statPresentMicros;
static std::atomic<uint64_t> g_statIdleMicros;

static uint64_t Micros()
{
    static LARGE_INTEGER frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return uint64_t(now.QuadPart) * 1000000ull / uint64_t(frequency.QuadPart);
}

static uint64_t g_binMask = ~0ull;
static uint64_t g_binSelect = ~0ull;

// Shaders, identified by a hash of their microcode. Index 0 = vertex, 1 = pixel.
static uint64_t g_currentShader[2];
static std::set<uint64_t> g_knownShaders[2];
static uint32_t g_drawLogFrame; // CD_DRAWLOG=<frame>: describe every draw of that frame

static uint32_t PhysicalToVirtual(uint32_t address)
{
    return kPhysicalHeapBase | (address & 0x1FFFFFFF);
}

// The game programs some registers by writing the MMIO block directly rather
// than through the command stream; fall back to that copy when ours is unset.
static uint32_t ReadRegister(uint32_t index)
{
    if (index >= kRegisterCount)
        return 0;
    if (g_registers[index] != 0 || index * 4 >= 0x10000)
        return g_registers[index];
    return Load32(kRegisterBase + index * 4);
}

static void WriteRegister(uint32_t index, uint32_t value)
{
    // CD_AUDIT=1: the first writes to the display colour table (gamma ramp).
    if (index >= 0x191E && index <= 0x1922)
    {
        static const bool audit = getenv("CD_AUDIT") != nullptr;
        static uint32_t logged;
        if (audit && logged++ < 24)
            Log("GPU audit: gamma register %04X <- %08X", index, value);
    }
    if (index < kRegisterCount)
        g_registers[index] = value;

    // Scratch registers enabled in SCRATCH_UMSK are mirrored to memory at
    // SCRATCH_ADDR. Direct3D passes its interrupt callbacks to the CPU this way.
    if (index >= REG_SCRATCH_REG0 && index < REG_SCRATCH_REG0 + 8)
    {
        uint32_t scratch = index - REG_SCRATCH_REG0;
        if (ReadRegister(REG_SCRATCH_UMSK) & (1u << scratch))
            Store32(PhysicalToVirtual(ReadRegister(REG_SCRATCH_ADDR)) + scratch * 4, value);
    }
}

// The guest handler acknowledges per processor, reading its number from the PCR.
static void RaiseInterrupt(uint32_t source, uint32_t processor)
{
    if (g_interruptCallback == 0)
        return;
    std::lock_guard lock(g_interruptMutex);
    GuestCall(g_interruptCallback, source, g_interruptContext, int(processor));
}

static void LoadShader(uint32_t type, uint32_t microcode, uint32_t wordCount)
{
    if (type > 1)
        return;

    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < wordCount * 4; i++)
        hash = (hash ^ g_base[microcode + i]) * 0x100000001B3ull;
    g_currentShader[type] = hash;

    if (g_knownShaders[type].insert(hash).second)
    {
        // CD_DUMP_SHADERS=1: keep the microcode for offline analysis with
        // tools/xenos_disasm.py.
        static const bool dump = getenv("CD_DUMP_SHADERS") != nullptr;
        if (dump)
        {
            char name[64];
            snprintf(name, sizeof(name), "shaders\\%s_%016llX.bin", type == 0 ? "vs" : "ps", (unsigned long long)hash);
            std::error_code error;
            std::filesystem::create_directories("shaders", error);
            if (FILE* file = fopen(name, "wb"))
            {
                fwrite(g_base + microcode, 4, wordCount, file);
                fclose(file);
            }
        }
        Log("GPU: new %s shader %016llX, %u words (%zu vertex, %zu pixel so far)", type == 0 ? "vertex" : "pixel",
            (unsigned long long)hash, wordCount, g_knownShaders[0].size(), g_knownShaders[1].size());
    }
}

static void LogDraw(uint32_t initiator)
{
    if (g_drawLogFrame != 0 && g_frameCount + 1 == g_drawLogFrame)
    {
        // First vertex of each stream the known shaders read from.
        float first[2][3]{};
        const uint32_t slots[2] = { 0, 95 };
        for (int s = 0; s < 2; s++)
        {
            uint32_t word0 = ReadRegister(0x4800 + slots[s] * 2);
            for (int k = 0; k < 3 && (word0 & ~3u) != 0 && (word0 & 3) == 3; k++)
            {
                uint32_t bits = Load32(PhysicalToVirtual(word0 & ~3u) + k * 4);
                memcpy(&first[s][k], &bits, 4);
            }
        }
        Log("GPU: draw %2u: prim %u x%u vs %04X ps %04X mode %X vte %03X sumode %08X winoff %08X scissor %08X-%08X blend %08X cc %08X depth %08X | vf0 %.1f %.1f %.1f | vf95 %.1f %.1f %.1f",
            g_drawCount, initiator & 0x3F, initiator >> 16, unsigned(g_currentShader[0] >> 48), unsigned(g_currentShader[1] >> 48),
            ReadRegister(0x2208), ReadRegister(0x2206), ReadRegister(0x2205), ReadRegister(0x2080), ReadRegister(0x2081),
            ReadRegister(0x2082), ReadRegister(0x2201), ReadRegister(0x2202), ReadRegister(0x2200),
            first[0][0], first[0][1], first[0][2], first[1][0], first[1][1], first[1][2]);
    }
}

static float RegisterFloat(uint32_t index)
{
    uint32_t bits = ReadRegister(index);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

static float GuestFloat(uint32_t address)
{
    uint32_t bits = Load32(address);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

// The game only uses a handful of tiny shaders, so instead of translating Xenos
// microcode each one is recognised by hash and evaluated here: vertices on the
// CPU, pixels as "texture * colour" on the host.
struct VertexShaderLayout
{
    uint64_t hash;
    uint32_t fetchSlot;      // vertex fetch constant index
    uint32_t stride;         // in 32-bit words
    uint32_t positionSize;   // float components read for the position
    int uvOffset;            // word offset, -1 if absent
    int colorOffset;         // word offset, -1 if absent
    bool transform;          // multiply by the matrix in c0..c3
};

static const VertexShaderLayout kVertexShaders[] =
{
    { 0x3EA824CF4FAA004Aull, 95, 5, 3, 3, -1, true },   // sprite: position, uv
    { 0x5BDC8A4DFF835989ull, 95, 5, 3, -1, -1, true },  // flat: position
    { 0x539EA9E08AA83F0Cull, 0, 7, 3, -1, 3, false },   // screen space position, colour
    { 0xA4AE7C2B7C1818C4ull, 0, 2, 2, -1, -1, false },  // screen space rectangle
};

enum class PixelShaderKind { Texture, Constant, VertexColor };

static bool FindPixelShader(uint64_t hash, PixelShaderKind& kind)
{
    switch (hash)
    {
    case 0xAFDFF469057FBBF6ull: kind = PixelShaderKind::Texture; return true;      // texture * c0
    case 0x03119CFD11BEB173ull: kind = PixelShaderKind::Constant; return true;     // c0
    case 0x438C2AF84C78A133ull: kind = PixelShaderKind::VertexColor; return true;  // interpolated colour
    default: return false;
    }
}

static void WarnUnknownShader(const char* stage, uint64_t hash)
{
    static std::set<uint64_t> warned;
    if (warned.insert(hash).second)
        Log("GPU: draws with %s shader %016llX are skipped (not implemented)", stage, (unsigned long long)hash);
}

// CD_AUDIT=1: log each distinct value of the render state the game asks for,
// the first time it is seen, to find what the host renderer does not honour.
static void AuditValue(const char* what, uint64_t value)
{
    static std::set<std::pair<std::string, uint64_t>> seen;
    if (seen.insert({ what, value }).second)
        Log("GPU audit: %s = %llX (frame %u)", what, (unsigned long long)value, g_frameCount.load());
}

static void AuditDraw(uint32_t initiator)
{
    uint32_t mode = ReadRegister(0x2208) & 7;
    AuditValue("mode", mode);
    if (mode != 4)
        return;
    AuditValue("vertex shader", g_currentShader[0]);
    AuditValue("pixel shader", g_currentShader[1]);
    AuditValue("shader pair", (g_currentShader[0] >> 48 << 16) | (g_currentShader[1] >> 48));
    AuditValue("primitive", initiator & 0x3F);
    // How each shader pair is used: xxxx yyyy = vertex and pixel shader, then the value.
    uint64_t pair = ((g_currentShader[0] >> 48 << 16) | (g_currentShader[1] >> 48)) << 32;
    AuditValue("pair + primitive", pair | (initiator & 0x3F));
    AuditValue("pair + cull/face mode", pair | (ReadRegister(0x2205) & 0x7));
    AuditValue("pair + colour write mask", pair | (ReadRegister(0x2104) & 0xF));
    AuditValue("pair + depth control", pair | ReadRegister(0x2200));
    if (g_currentShader[0] == 0x8BB7E189D92E3DEFull)
        AuditValue("skipped shader: vertex count", initiator >> 16);
    AuditValue("index source", (initiator >> 6) & 3);
    uint32_t depth = ReadRegister(0x2200);
    AuditValue("depth test/write/function", depth & 0x76);
    AuditValue("stencil enable", depth & 1);
    if (depth & 1)
        AuditValue("stencil state", depth);
    uint32_t color = ReadRegister(0x2202);
    AuditValue("alpha test enable", (color >> 3) & 1);
    if (color & 8)
        AuditValue("alpha test function/reference", (uint64_t(color & 7) << 32) | ReadRegister(0x2104 + 0x1E0));
    AuditValue("blend disable", (color >> 5) & 1);
    AuditValue("color control (other bits)", color & ~0x2Fu);
    AuditValue("blend control", ReadRegister(0x2201));
    AuditValue("color write mask", ReadRegister(0x2104) & 0xF);
    AuditValue("cull/face mode", ReadRegister(0x2205) & 0x7);
    AuditValue("polygon mode bits", ReadRegister(0x2205) & ~0x7u);
    AuditValue("viewport transform flags", ReadRegister(0x2206));
    AuditValue("pixel centre", ReadRegister(0x2302) & 1);
    // Display controller colour look-up table (the gamma ramp): mode, index, data.
    AuditValue("gamma table registers touched", (ReadRegister(0x191E) | ReadRegister(0x191F) | ReadRegister(0x1920) |
        ReadRegister(0x1921) | ReadRegister(0x1922)) != 0);

    // Texture 0, when the pixel shader samples one.
    if (g_currentShader[1] == 0xAFDFF469057FBBF6ull)
    {
        uint32_t word0 = ReadRegister(0x4800);
        uint32_t word1 = ReadRegister(0x4801);
        uint32_t word3 = ReadRegister(0x4803);
        uint32_t word4 = ReadRegister(0x4804);
        uint32_t word5 = ReadRegister(0x4805);
        AuditValue("texture clamp x/y/z", (word0 >> 10) & 0x1FF);
        AuditValue("texture format/endian", word1 & 0xFF);
        AuditValue("texture mag/min/mip filter, aniso", (word3 >> 19) & 0x1FF);
        AuditValue("texture swizzle/exp", word3 & 0x7FFFF);
        AuditValue("texture mip range/lod bias", word4);
        AuditValue("texture dimension/border", word5 & 0xFFF);
        AuditValue("texture sign/type bits", word0 & 0x3FF);
    }
}

static void Draw(uint32_t initiator, uint32_t indexBase)
{
    g_drawCount++;
    LogDraw(initiator);
    static const bool audit = getenv("CD_AUDIT") != nullptr;
    if (audit)
        AuditDraw(initiator);

    // Only colour passes are visible; depth-only passes are skipped. A resolve
    // copies the tile to the front buffer, which the host does not need, but it
    // can also clear the render target for whatever is drawn next: that clear
    // is where the frame's background colour comes from on all but the first tile.
    uint32_t mode = ReadRegister(0x2208) & 7;
    if (mode == 6 && (ReadRegister(0x2318) & 0x100))
    {
        uint32_t color = ReadRegister(0x2326); // RB_COLOR_CLEAR, A8R8G8B8
        RendererSetClearColor(((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f,
            (color >> 24) / 255.0f);
    }
    if (mode != 4)
        return;

    const VertexShaderLayout* layout = nullptr;
    for (auto& candidate : kVertexShaders)
    {
        if (candidate.hash == g_currentShader[0])
            layout = &candidate;
    }
    if (layout == nullptr)
    {
        WarnUnknownShader("vertex", g_currentShader[0]);
        return;
    }
    PixelShaderKind pixelKind;
    if (!FindPixelShader(g_currentShader[1], pixelKind))
    {
        WarnUnknownShader("pixel", g_currentShader[1]);
        return;
    }

    uint32_t primitive = initiator & 0x3F;
    uint32_t count = initiator >> 16;
    uint32_t source = (initiator >> 6) & 3;
    bool wideIndices = (initiator >> 11) & 1;
    uint32_t fetchWord = ReadRegister(0x4800 + layout->fetchSlot * 2);
    if ((fetchWord & ~3u) == 0 || count == 0 || count > 0x10000 || source == 1)
        return;
    uint32_t vertexBase = PhysicalToVirtual(fetchWord & ~3u);
    uint32_t indexData = PhysicalToVirtual(indexBase);

    // Viewport transform (PA_CL_VTE_CNTL) and pixel centre convention.
    uint32_t vte = ReadRegister(0x2206);
    float scaleX = vte & 1 ? RegisterFloat(0x210F) : 1.0f;
    float offsetX = vte & 2 ? RegisterFloat(0x2110) : 0.0f;
    float scaleY = vte & 4 ? RegisterFloat(0x2111) : 1.0f;
    float offsetY = vte & 8 ? RegisterFloat(0x2112) : 0.0f;
    float scaleZ = vte & 0x10 ? RegisterFloat(0x2113) : 1.0f;
    float offsetZ = vte & 0x20 ? RegisterFloat(0x2114) : 0.0f;
    bool preDivided = (vte >> 8) & 1;
    float halfPixel = (ReadRegister(0x2302) & 1) ? 0.0f : 0.5f;

    float constantColor[4];
    for (int i = 0; i < 4; i++)
        constantColor[i] = RegisterFloat(0x4400 + i);

    std::vector<HostVertex> vertices(count);
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t index = i;
        if (source == 0)
            index = wideIndices ? Load32(indexData + i * 4) : Load16(indexData + i * 2);
        uint32_t address = vertexBase + index * layout->stride * 4;

        float position[4] = { 0, 0, 0, 1 };
        for (uint32_t k = 0; k < layout->positionSize; k++)
            position[k] = GuestFloat(address + k * 4);
        if (layout->positionSize == 2)
            position[2] = 1.0f;

        float clip[4] = { position[0], position[1], position[2], position[3] };
        if (layout->transform)
        {
            for (int k = 0; k < 4; k++)
            {
                clip[k] = position[0] * RegisterFloat(0x4000 + k) + position[1] * RegisterFloat(0x4004 + k) +
                    position[2] * RegisterFloat(0x4008 + k) + position[3] * RegisterFloat(0x400C + k);
            }
        }

        float w = preDivided || clip[3] == 0.0f ? 1.0f : clip[3];
        float screenX = clip[0] / w * scaleX + offsetX + halfPixel;
        float screenY = clip[1] / w * scaleY + offsetY + halfPixel;

        HostVertex& out = vertices[i];
        out.x = screenX / (kHostWidth * 0.5f) - 1.0f;
        out.y = 1.0f - screenY / (kHostHeight * 0.5f);
        out.z = clip[2] / w * scaleZ + offsetZ;
        out.w = 1.0f;
        out.u = layout->uvOffset >= 0 ? GuestFloat(address + layout->uvOffset * 4) : 0.0f;
        out.v = layout->uvOffset >= 0 ? GuestFloat(address + layout->uvOffset * 4 + 4) : 0.0f;

        const float white[4] = { 1, 1, 1, 1 };
        float vertexColor[4] = { 1, 1, 1, 1 };
        if (layout->colorOffset >= 0)
        {
            for (int k = 0; k < 4; k++)
                vertexColor[k] = GuestFloat(address + (layout->colorOffset + k) * 4);
        }
        const float* color = pixelKind == PixelShaderKind::VertexColor ? vertexColor : constantColor;
        if (pixelKind == PixelShaderKind::VertexColor && layout->colorOffset < 0)
            color = white;
        out.r = color[0];
        out.g = color[1];
        out.b = color[2];
        out.a = color[3];
    }

    // Expand every primitive type to a triangle list.
    HostDraw draw;
    auto triangle = [&](const HostVertex& a, const HostVertex& b, const HostVertex& c)
    {
        draw.vertices.push_back(a);
        draw.vertices.push_back(b);
        draw.vertices.push_back(c);
    };
    // Lines become one pixel wide quads, as the host only draws triangle lists.
    auto line = [&](const HostVertex& a, const HostVertex& b)
    {
        float dx = (b.x - a.x) * (kHostWidth * 0.5f);
        float dy = (b.y - a.y) * (kHostHeight * 0.5f);
        float length = sqrtf(dx * dx + dy * dy);
        if (length < 1e-6f)
            return;
        float offsetX = -dy / length * 0.5f / (kHostWidth * 0.5f);
        float offsetY = dx / length * 0.5f / (kHostHeight * 0.5f);
        HostVertex corners[4] = { a, a, b, b };
        corners[0].x += offsetX; corners[0].y += offsetY;
        corners[1].x -= offsetX; corners[1].y -= offsetY;
        corners[2].x += offsetX; corners[2].y += offsetY;
        corners[3].x -= offsetX; corners[3].y -= offsetY;
        triangle(corners[0], corners[1], corners[2]);
        triangle(corners[1], corners[3], corners[2]);
    };
    switch (primitive)
    {
    case 2: // line list
        for (uint32_t i = 0; i + 1 < count; i += 2)
            line(vertices[i], vertices[i + 1]);
        break;
    case 3: // line strip
        for (uint32_t i = 0; i + 1 < count; i++)
            line(vertices[i], vertices[i + 1]);
        break;
    case 4: // triangle list
        for (uint32_t i = 0; i + 2 < count; i += 3)
            triangle(vertices[i], vertices[i + 1], vertices[i + 2]);
        break;
    case 5: // triangle fan
        for (uint32_t i = 1; i + 1 < count; i++)
            triangle(vertices[0], vertices[i], vertices[i + 1]);
        break;
    case 6: // triangle strip: every other triangle is wound the other way round
        for (uint32_t i = 0; i + 2 < count; i++)
        {
            if (i & 1)
                triangle(vertices[i + 1], vertices[i], vertices[i + 2]);
            else
                triangle(vertices[i], vertices[i + 1], vertices[i + 2]);
        }
        break;
    case 8: // rectangle list: the fourth corner is implied
        for (uint32_t i = 0; i + 2 < count; i += 3)
        {
            const HostVertex& a = vertices[i];
            const HostVertex& b = vertices[i + 1];
            const HostVertex& c = vertices[i + 2];
            HostVertex d = c;
            d.x = a.x + c.x - b.x;
            d.y = a.y + c.y - b.y;
            d.u = a.u + c.u - b.u;
            d.v = a.v + c.v - b.v;
            triangle(a, b, c);
            triangle(a, c, d);
        }
        break;
    case 13: // quad list
        for (uint32_t i = 0; i + 3 < count; i += 4)
        {
            triangle(vertices[i], vertices[i + 1], vertices[i + 2]);
            triangle(vertices[i], vertices[i + 2], vertices[i + 3]);
        }
        break;
    default:
    {
        static std::set<uint32_t> warned;
        if (warned.insert(primitive).second)
            Log("GPU: primitive type %u is not implemented", primitive);
        return;
    }
    }

    if (pixelKind == PixelShaderKind::Texture)
    {
        // Texture fetch constant 0.
        uint32_t word0 = ReadRegister(0x4800);
        uint32_t word1 = ReadRegister(0x4801);
        uint32_t word2 = ReadRegister(0x4802);
        uint32_t word3 = ReadRegister(0x4803);
        draw.texture.present = true;
        draw.texture.tiled = word0 >> 31;
        draw.texture.pitch = ((word0 >> 22) & 0x1FF) * 32;
        draw.texture.format = word1 & 0x3F;
        draw.texture.address = PhysicalToVirtual((word1 >> 12) << 12);
        draw.texture.width = (word2 & 0x1FFF) + 1;
        draw.texture.height = ((word2 >> 13) & 0x1FFF) + 1;
        draw.texture.linear = ((word3 >> 19) & 3) == 1;
    }

    draw.blend.enabled = !(ReadRegister(0x2202) & 0x20);
    draw.blend.control = ReadRegister(0x2201);
    draw.blend.writeMask = ReadRegister(0x2104) & 0xF;

    // PA_SU_SC_MODE_CNTL: which faces are hidden. Lines have no faces.
    // CD_NOCULL=1 shows every face, to compare.
    static const bool noCull = getenv("CD_NOCULL") != nullptr;
    draw.cull = primitive == 2 || primitive == 3 || noCull ? 0 : ReadRegister(0x2205) & 7;

    // CD_AUDIT=1: which way round each shader pair's triangles are wound (in
    // clip space, y up), to see whether culling ever hides any.
    if (audit && draw.cull != 0)
    {
        uint64_t pair = ((g_currentShader[0] >> 48 << 16) | (g_currentShader[1] >> 48)) << 32;
        for (size_t i = 0; i + 2 < draw.vertices.size(); i += 3)
        {
            const HostVertex& a = draw.vertices[i];
            const HostVertex& b = draw.vertices[i + 1];
            const HostVertex& c = draw.vertices[i + 2];
            float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            AuditValue(area > 0 ? "pair + cull mode, counter-clockwise triangles" : area < 0 ? "pair + cull mode, clockwise triangles" :
                "pair + cull mode, empty triangles", pair | draw.cull);
            // Describe some of the hidden ones: where on screen, and from which texture.
            static uint32_t described;
            static std::set<uint64_t> texturesSeen;
            if (area > 0 && described < 60 && texturesSeen.insert((uint64_t(ReadRegister(0x4801)) << 32) | uint32_t(a.u * 4096) ).second)
            {
                described++;
                auto px = [](float v, float size) { return int((v * 0.5f + 0.5f) * size); };
                Log("GPU audit: hidden face, frame %u draw %u: screen (%d,%d) (%d,%d) (%d,%d), uv (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f), texture %08X %ux%u, colour %.2f %.2f %.2f %.2f",
                    g_frameCount.load(), g_drawCount, px(a.x, 1280), px(-a.y, 720), px(b.x, 1280), px(-b.y, 720), px(c.x, 1280), px(-c.y, 720),
                    a.u, a.v, b.u, b.v, c.u, c.v, ReadRegister(0x4801) & ~0xFFFu, (ReadRegister(0x4802) & 0x1FFF) + 1,
                    ((ReadRegister(0x4802) >> 13) & 0x1FFF) + 1, a.r, a.g, a.b, a.a);
            }
        }
    }

    // RB_DEPTHCONTROL: test enable, write enable, compare function.
    uint32_t depthControl = ReadRegister(0x2200);
    draw.depth.test = (depthControl >> 1) & 1;
    draw.depth.write = draw.depth.test && ((depthControl >> 2) & 1);
    draw.depth.function = (depthControl >> 4) & 7;

    // Window scissor, in screen pixels. Tiled rendering replays the scene once
    // per tile with a different scissor, so honouring it keeps blending correct.
    uint32_t topLeft = ReadRegister(0x2081);
    uint32_t bottomRight = ReadRegister(0x2082);
    auto clamp = [](uint32_t value, uint32_t limit) { return int(value > limit ? limit : value); };
    draw.scissor[0] = clamp(topLeft & 0x7FFF, kHostWidth);
    draw.scissor[1] = clamp((topLeft >> 16) & 0x7FFF, kHostHeight);
    draw.scissor[2] = clamp(bottomRight & 0x7FFF, kHostWidth);
    draw.scissor[3] = clamp((bottomRight >> 16) & 0x7FFF, kHostHeight);

    // CD_DRAWLOG=<frame>: where each draw of that frame lands on screen.
    if (g_drawLogFrame != 0 && g_frameCount + 1 == g_drawLogFrame && !draw.vertices.empty())
    {
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        for (auto& vertex : draw.vertices)
        {
            float x = (vertex.x * 0.5f + 0.5f) * kHostWidth, y = (0.5f - vertex.y * 0.5f) * kHostHeight;
            left = x < left ? x : left; right = x > right ? x : right;
            top = y < top ? y : top; bottom = y > bottom ? y : bottom;
        }
        const HostVertex& first = draw.vertices[0];
        Log("GPU: box %2u: (%.1f,%.1f)-(%.1f,%.1f) %.1fx%.1f colour %.2f %.2f %.2f %.2f tex %08X %ux%u uv %.3f,%.3f", g_drawCount, left, top,
            right, bottom, right - left, bottom - top, first.r, first.g, first.b, first.a,
            draw.texture.present ? draw.texture.address : 0, draw.texture.width, draw.texture.height, first.u, first.v);
    }

    uint64_t before = Micros();
    RendererDraw(draw);
    g_statDrawMicros += Micros() - before;
    g_statDraws++;
}

static void ExecuteRange(uint32_t address, uint32_t wordCount);

static void ExecuteType3(uint32_t opcode, uint32_t address, uint32_t count)
{
    // Constant classes map onto fixed register blocks.
    static const uint32_t constantBases[] = { 0x4000, 0x4800, 0x4900, 0x4908, 0x2000 };

    switch (opcode)
    {
    case PM4_INDIRECT_BUFFER:
        ExecuteRange(PhysicalToVirtual(Load32(address)), Load32(address + 4) & 0xFFFFF);
        break;

    case PM4_REG_RMW:
    {
        uint32_t info = Load32(address);
        uint32_t index = info & 0x1FFF;
        uint32_t value = ReadRegister(index);
        uint32_t andMask = Load32(address + 4);
        uint32_t orMask = Load32(address + 8);
        value &= (info >> 31) & 1 ? ReadRegister(andMask & 0x1FFF) : andMask;
        value |= (info >> 30) & 1 ? ReadRegister(orMask & 0x1FFF) : orMask;
        WriteRegister(index, value);
        break;
    }

    case PM4_MEM_WRITE:
    {
        uint32_t destination = PhysicalToVirtual(Load32(address) & ~3u);
        for (uint32_t i = 1; i < count; i++)
            Store32(destination + (i - 1) * 4, Load32(address + i * 4));
        break;
    }

    case PM4_COND_WRITE:
    {
        // Compare a register or memory word against a reference, then write.
        uint32_t info = Load32(address);
        uint32_t pollAddress = Load32(address + 4);
        uint32_t reference = Load32(address + 8);
        uint32_t mask = Load32(address + 12);
        uint32_t writeAddress = Load32(address + 16);
        uint32_t writeData = Load32(address + 20);
        uint32_t value = (info & 0x10) ? Load32(PhysicalToVirtual(pollAddress & ~3u)) : ReadRegister(pollAddress & 0x7FFF);
        value &= mask;

        bool matched = false;
        switch (info & 7)
        {
        case 1: matched = value < reference; break;
        case 2: matched = value <= reference; break;
        case 3: matched = value == reference; break;
        case 4: matched = value != reference; break;
        case 5: matched = value >= reference; break;
        case 6: matched = value > reference; break;
        case 7: matched = true; break;
        }
        if (matched)
        {
            if (info & 0x100)
                Store32(PhysicalToVirtual(writeAddress & ~3u), writeData);
            else
                WriteRegister(writeAddress & 0x7FFF, writeData);
        }
        break;
    }

    case PM4_EVENT_WRITE_SHD:
    {
        // Fence: the game polls this memory word to know the GPU got this far.
        // With the top initiator bit set, the vertical blank counter is written
        // instead, which Direct3D uses to pace presentation.
        uint32_t initiator = Load32(address);
        uint32_t destination = PhysicalToVirtual(Load32(address + 4) & ~3u);
        Store32(destination, (initiator & 0x80000000) ? g_vblankCount.load() : Load32(address + 8));
        break;
    }

    case PM4_EVENT_WRITE_EXT:
    {
        // Screen extents of the preceding draws, from which the command stream
        // itself works out which tiles each draw touches. Reporting the whole
        // surface makes every draw run in every tile; the scissor does the rest.
        uint32_t destination = PhysicalToVirtual(Load32(address + 4) & ~3u);
        const uint16_t extents[6] = { 0, 2560 >> 3, 0, 2560 >> 3, 0, 1 }; // min/max x, y (in 8 pixel units), z
        for (int i = 0; i < 6; i++)
            Store16(destination + i * 2, extents[i]);
        break;
    }

    case PM4_INTERRUPT:
    {
        uint32_t processorMask = Load32(address);
        for (uint32_t processor = 0; processor < 6; processor++)
        {
            if (processorMask & (1u << processor))
                RaiseInterrupt(1, processor);
        }
        break;
    }

    case PM4_SET_CONSTANT:
    {
        uint32_t info = Load32(address);
        uint32_t index = info & 0x7FF;
        uint32_t type = (info >> 16) & 0xFF;
        if (type < 5)
        {
            for (uint32_t i = 1; i < count; i++)
                WriteRegister(constantBases[type] + index + i - 1, Load32(address + i * 4));
        }
        break;
    }

    case PM4_SET_CONSTANT2:
    case PM4_SET_SHADER_CONSTANTS:
    {
        uint32_t index = Load32(address) & 0xFFFF;
        for (uint32_t i = 1; i < count; i++)
            WriteRegister(index + i - 1, Load32(address + i * 4));
        break;
    }

    case PM4_LOAD_ALU_CONSTANT:
    {
        // Constants come from memory instead of the packet.
        uint32_t source = PhysicalToVirtual(Load32(address) & 0x3FFFFFFF);
        uint32_t info = Load32(address + 4);
        uint32_t size = Load32(address + 8) & 0xFFF;
        uint32_t index = info & 0x7FF;
        uint32_t type = (info >> 16) & 0xFF;
        if (type < 5)
        {
            for (uint32_t i = 0; i < size; i++)
                WriteRegister(constantBases[type] + index + i, Load32(source + i * 4));
        }
        break;
    }

    case PM4_IM_LOAD:
    {
        uint32_t typeAndAddress = Load32(address);
        LoadShader(typeAndAddress & 3, PhysicalToVirtual(typeAndAddress & ~3u), Load32(address + 4) & 0xFFFF);
        break;
    }

    case PM4_IM_LOAD_IMMEDIATE:
        LoadShader(Load32(address) & 3, address + 8, Load32(address + 4) & 0xFFFF);
        break;

    case PM4_DRAW_INDX:
        Draw(Load32(address + 4), count > 2 ? Load32(address + 8) : 0);
        break;

    case PM4_DRAW_INDX_2:
        Draw(Load32(address), 0);
        break;

    case PM4_XE_SWAP:
    {
        uint64_t before = Micros();
        RendererPresent();
        g_statPresentMicros += Micros() - before;
        ++g_frameCount;
        g_drawCount = 0;
        break;
    }

    default:
        break;
    }
}

static void ExecuteRange(uint32_t address, uint32_t wordCount)
{
    uint32_t end = address + wordCount * 4;
    while (address < end)
    {
        uint32_t packet = Load32(address);
        address += 4;

        switch (packet >> 30)
        {
        case 0:
        {
            uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
            uint32_t index = packet & 0x7FFF;
            bool single = (packet >> 15) & 1;
            for (uint32_t i = 0; i < count; i++)
                WriteRegister(single ? index : index + i, Load32(address + i * 4));
            address += count * 4;
            break;
        }
        case 1:
            WriteRegister(packet & 0x7FF, Load32(address));
            WriteRegister((packet >> 11) & 0x7FF, Load32(address + 4));
            address += 8;
            break;
        case 2:
            break;
        case 3:
        {
            uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
            uint32_t opcode = (packet >> 8) & 0x7F;

            // Tiled rendering replays the scene once per bin. Packets carrying
            // the predicate bit only run for the bins currently selected, which
            // is how each tile gets its own window offset and scissor.
            bool predicated = packet & 1;
            switch (opcode)
            {
            case PM4_SET_BIN_MASK_LO: g_binMask = (g_binMask & 0xFFFFFFFF00000000ull) | Load32(address); break;
            case PM4_SET_BIN_MASK_HI: g_binMask = (g_binMask & 0xFFFFFFFFull) | (uint64_t(Load32(address)) << 32); break;
            case PM4_SET_BIN_SELECT_LO: g_binSelect = (g_binSelect & 0xFFFFFFFF00000000ull) | Load32(address); break;
            case PM4_SET_BIN_SELECT_HI: g_binSelect = (g_binSelect & 0xFFFFFFFFull) | (uint64_t(Load32(address)) << 32); break;
            default:
            {
                bool run = !predicated || (g_binMask & g_binSelect) != 0;
                if (g_drawLogFrame != 0 && g_frameCount + 1 == g_drawLogFrame &&
                    (opcode == PM4_DRAW_INDX || opcode == PM4_DRAW_INDX_2 || opcode == PM4_INDIRECT_BUFFER))
                {
                    // Summarise runs of identical lines.
                    static uint64_t lastKey;
                    static uint32_t repeats;
                    uint64_t key = (uint64_t(opcode) << 56) ^ (uint64_t(run) << 55) ^ (uint64_t(predicated) << 54) ^
                        (g_binMask << 20) ^ g_binSelect;
                    if (key != lastKey)
                    {
                        if (repeats > 0)
                            Log("GPU:    ... repeated %u more times", repeats);
                        Log("GPU: bin: %s%s %s mask %08X select %08X", opcode == PM4_INDIRECT_BUFFER ? "buffer" : "draw",
                            predicated ? " (predicated)" : "", run ? "run" : "SKIP", uint32_t(g_binMask), uint32_t(g_binSelect));
                        lastKey = key;
                        repeats = 0;
                    }
                    else
                    {
                        repeats++;
                    }
                }
                if (run)
                    ExecuteType3(opcode, address, count);
                break;
            }
            }
            address += count * 4;
            break;
        }
        }
    }
}

static void CommandThread()
{
    // The device belongs to this thread: every draw and present happens here.
    if (!RendererInit())
        Log("GPU: no host renderer, running without display");

    for (;;)
    {
        uint32_t writeIndex = Load32(kRegisterBase + REG_CP_RB_WPTR * 4);
        if (g_ringBase == 0 || writeIndex == g_readIndex || writeIndex >= g_ringWordCount)
        {
            uint64_t before = Micros();
            Sleep(1);
            g_statIdleMicros += Micros() - before;
            continue;
        }

        if (writeIndex < g_readIndex)
        {
            // The pending range wraps, and a packet may straddle the end of the
            // ring. Copy both halves into a linear scratch buffer first.
            static uint32_t scratch;
            if (scratch == 0)
                scratch = GuestAllocPhysical(g_ringWordCount * 4);
            uint32_t tailWords = g_ringWordCount - g_readIndex;
            memcpy(g_base + scratch, g_base + g_ringBase + g_readIndex * 4, tailWords * 4);
            memcpy(g_base + scratch + tailWords * 4, g_base + g_ringBase, writeIndex * 4);
            ExecuteRange(scratch, tailWords + writeIndex);
        }
        else
        {
            ExecuteRange(g_ringBase + g_readIndex * 4, writeIndex - g_readIndex);
        }
        g_readIndex = writeIndex;

        Store32(kRegisterBase + REG_CP_RB_RPTR * 4, g_readIndex);
        if (g_readPointerWriteBack != 0)
            Store32(g_readPointerWriteBack, g_readIndex);
    }
}

static void VBlankThread()
{
    for (;;)
    {
        Sleep(16);
        uint32_t vblank = ++g_vblankCount;
        RaiseInterrupt(0, 2);

        // CD_STATS=1: progress line every two seconds.
        static const bool stats = getenv("CD_STATS") != nullptr;
        if (stats && vblank % 120 == 0)
        {
            static uint32_t lastFrames;
            uint32_t frames = g_frameCount.load();
            uint32_t delta = frames - lastFrames ? frames - lastFrames : 1;
            lastFrames = frames;
            Log("GPU: stats: vblank %u, frames %u (+%u), per frame: %u draws, %.1f ms drawing, %.1f ms presenting, %.1f ms idle",
                vblank, frames, delta, g_statDraws.exchange(0) / delta, g_statDrawMicros.exchange(0) / 1000.0 / delta,
                g_statPresentMicros.exchange(0) / 1000.0 / delta, g_statIdleMicros.exchange(0) / 1000.0 / delta);
        }
    }
}

bool GpuInit()
{
    if (!GuestCommit(kRegisterBase, 0x10000))
        return false;
    Store32(kRegisterBase + REG_D1MODE_VBLANK_STATUS * 4, 1);
    Store32(kRegisterBase + REG_D1MODE_VIEWPORT_SIZE * 4, 0x050002D0);
    if (const char* frame = getenv("CD_DRAWLOG"))
        g_drawLogFrame = uint32_t(atoi(frame));
    return true;
}

void GpuInitRingBuffer(uint32_t physicalAddress, uint32_t sizeLog2)
{
    g_ringBase = PhysicalToVirtual(physicalAddress);
    g_ringWordCount = 1u << (sizeLog2 + 1);
    g_readIndex = 0;
    Log("GPU: ring buffer at %08X, %u words", g_ringBase, g_ringWordCount);

    static bool started = false;
    if (!started)
    {
        started = true;
        timeBeginPeriod(1);
        std::thread(CommandThread).detach();
        std::thread(VBlankThread).detach();
    }
}

void GpuEnableReadPointerWriteBack(uint32_t physicalAddress)
{
    g_readPointerWriteBack = PhysicalToVirtual(physicalAddress);
}

void GpuSetInterruptCallback(uint32_t callback, uint32_t context)
{
    g_interruptCallback = callback;
    g_interruptContext = context;
}

void GpuWriteSwapPacket(uint32_t buffer, uint32_t frontBuffer, uint32_t width, uint32_t height)
{
    // The game reserved 64 words in its command stream for the kernel to fill.
    for (uint32_t i = 0; i < 64; i++)
        Store32(buffer + i * 4, 0x80000000);
    Store32(buffer + 0, 0xC0000000 | (3u << 16) | (PM4_XE_SWAP << 8));
    Store32(buffer + 4, 0x53574150); // 'SWAP'
    Store32(buffer + 8, frontBuffer);
    Store32(buffer + 12, width);
    Store32(buffer + 16, height);
}

uint32_t GpuFrameCount()
{
    return g_frameCount;
}
