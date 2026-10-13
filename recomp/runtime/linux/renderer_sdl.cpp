// Linux renderer: an SDL2 window and OpenGL 3.3 drawing what the GPU command
// processor decodes. The counterpart of renderer.cpp (Direct3D 11) on Windows.
// The settings menu follows the one in renderer.cpp line for line; only the
// drawing differs (overlay.cpp instead of GDI).
#include "renderer.h"
#include "config.h"
#include "guest.h"
#include "overlay.h"
#include "xex_header.h"

#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <windows.h>

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>

static SDL_Window* g_window;
static SDL_GLContext g_gl;
static GLuint g_programs[2]; // plain, sharp
static GLuint g_vertexArray;
static GLuint g_vertexBuffer;
static GLuint g_frameBuffer;
static GLuint g_frameTexture;
static GLuint g_whiteTexture;
static GLuint g_depthBuffer;
static uint32_t g_scale; // the frame is 1280x720 times this
static float g_clearColor[4] = { 0, 0, 0, 1 };
static uint32_t g_frameNumber = 1;
static bool g_fullscreen;

struct CachedTexture
{
    GLuint texture{};
    uint64_t contentHash{};
    uint32_t checkedFrame{};
};
static std::map<uint64_t, CachedTexture> g_textures;

// Input, gathered by the event loop on the render thread and read by the game.
static std::atomic<uint16_t> g_keyButtons;
static std::atomic<uint16_t> g_padButtons;
static std::atomic<int> g_padAxes[4];
static std::atomic<int> g_padTriggers[2];
static std::vector<SDL_GameController*> g_controllers;

// After a relaunch from the settings menu, this many frames are run as fast as
// possible without being shown or heard: the logos before the title screen.
static uint32_t g_fastStartFrames = getenv("CD_RESUME") != nullptr ? 530 : 0;
static std::atomic<bool> g_backgroundPause; // held because the window is not the active one
static std::atomic<bool> g_menuOpen;
static std::atomic<bool> g_menuRequest;
static std::atomic<bool> g_waitForRelease;  // after the menu: ignore buttons still held

bool RendererFastStart()
{
    return g_frameNumber < g_fastStartFrames;
}

bool RendererSilent()
{
    return RendererFastStart() || g_backgroundPause;
}

void RendererSetClearColor(float r, float g, float b, float a)
{
    g_clearColor[0] = r;
    g_clearColor[1] = g;
    g_clearColor[2] = b;
    g_clearColor[3] = a;
}

// ---- Set-up ----

static void UseProgram(bool sharp)
{
    static int current = -1;
    if (current != int(sharp))
    {
        current = int(sharp);
        glUseProgram(g_programs[current]);
    }
}

static GLuint Compile(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char text[1024]{};
        glGetShaderInfoLog(shader, sizeof(text), nullptr, text);
        Log("Renderer: shader compilation failed: %s", text);
    }
    return shader;
}

// The frame the game draws into, and its depth buffer, at a multiple of the
// console's 1280x720.
static bool CreateFrameTargets(uint32_t scale)
{
    g_scale = scale;
    glBindTexture(GL_TEXTURE_2D, g_frameTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kHostWidth * scale, kHostHeight * scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, kHostWidth * scale, kHostHeight * scale);
    glBindFramebuffer(GL_FRAMEBUFFER, g_frameBuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_frameTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depthBuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        Log("Renderer: the frame buffer is not usable");
        return false;
    }
    glViewport(0, 0, kHostWidth * scale, kHostHeight * scale);
    return true;
}

bool RendererInit()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
    {
        Log("Renderer: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const Config& config = GetConfig();
    int width = config.windowWidth ? int(config.windowWidth) : int(kHostWidth);
    int height = config.windowHeight ? int(config.windowHeight) : int(kHostHeight);
    g_fullscreen = config.fullscreen;
    // CD_RESUME: relaunched from the settings menu; take the previous window's place.
    int resumeX = SDL_WINDOWPOS_CENTERED, resumeY = SDL_WINDOWPOS_CENTERED, resumeFullscreen = 0;
    if (const char* resume = getenv("CD_RESUME"))
    {
        int x, y, w, h;
        if (sscanf(resume, "%d,%d,%d,%d,%d", &x, &y, &w, &h, &resumeFullscreen) == 5 && w > 0 && h > 0)
        {
            resumeX = x;
            resumeY = y;
            width = w;
            height = h;
            g_fullscreen = resumeFullscreen != 0;
        }
    }
    g_window = SDL_CreateWindow("Crystal Defenders", resumeX, resumeY, width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    g_gl = g_window ? SDL_GL_CreateContext(g_window) : nullptr;
    if (g_gl == nullptr)
    {
        Log("Renderer: no OpenGL 3.3 window: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(0); // paced by the limiter in RendererPresent
    SDL_ShowCursor(SDL_DISABLE);

    // Depth arrives in Direct3D's 0..1 range.
    // Two programs: the plain one, and the "sharp" one where enlarged texels keep
    // hard edges, one screen pixel wide, instead of blurring. Kept apart so the
    // plain one costs nothing extra.
    const char* vertexSource =
        "#version 330 core\n"
        "layout(location = 0) in vec4 position; layout(location = 1) in vec2 uv; layout(location = 2) in vec4 color;\n"
        "out vec2 vUv; out vec4 vColor;\n"
        "void main() { gl_Position = vec4(position.xy, (position.z * 2.0 - position.w), position.w); vUv = uv; vColor = color; }\n";
    const char* fragmentSources[2] =
    {
        "#version 330 core\n"
        "uniform sampler2D tex; in vec2 vUv; in vec4 vColor; out vec4 outColor;\n"
        "void main() { outColor = texture(tex, vUv) * vColor; }\n",
        "#version 330 core\n"
        "uniform sampler2D tex; in vec2 vUv; in vec4 vColor; out vec4 outColor;\n"
        "void main() {\n"
        "  vec2 size = vec2(textureSize(tex, 0));\n"
        "  vec2 texel = vUv * size;\n"
        "  vec2 seam = floor(texel + 0.5);\n"
        "  texel = seam + clamp((texel - seam) / fwidth(texel), -0.5, 0.5);\n"
        "  outColor = texture(tex, texel / size) * vColor;\n"
        "}\n",
    };
    for (int i = 0; i < 2; i++)
    {
        g_programs[i] = glCreateProgram();
        glAttachShader(g_programs[i], Compile(GL_VERTEX_SHADER, vertexSource));
        glAttachShader(g_programs[i], Compile(GL_FRAGMENT_SHADER, fragmentSources[i]));
        glLinkProgram(g_programs[i]);
    }
    UseProgram(false);

    glGenVertexArrays(1, &g_vertexArray);
    glBindVertexArray(g_vertexArray);
    glGenBuffers(1, &g_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, g_vertexBuffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(HostVertex), (void*)offsetof(HostVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HostVertex), (void*)offsetof(HostVertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(HostVertex), (void*)offsetof(HostVertex, r));

    glGenTextures(1, &g_frameTexture);
    glGenRenderbuffers(1, &g_depthBuffer);
    glGenFramebuffers(1, &g_frameBuffer);
    if (!CreateFrameTargets(1))
        return false;

    const uint32_t white = 0xFFFFFFFF;
    glGenTextures(1, &g_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, g_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, g_frameBuffer);
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    Log("Renderer: OpenGL %s on %s, %dx%d window ready", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
        width, height);
    return true;
}

// ---- Textures ----

// Xenos tiled 2D texture addressing: byte offset of texel (x, y).
static uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2Bpp)
{
    uint32_t alignedPitch = (pitch + 31) & ~31u;
    uint32_t rowMacro = ((y / 32) * (alignedPitch / 32)) << (log2Bpp + 7);
    uint32_t rowMicro = ((y & 6) << 2) << log2Bpp;
    uint32_t row = rowMacro + ((rowMicro & ~0xFu) << 1) + (rowMicro & 0xF) + ((y & 8) << (3 + log2Bpp)) + ((y & 1) << 4);

    uint32_t columnMacro = (x / 32) << (log2Bpp + 7);
    uint32_t columnMicro = (x & 7) << log2Bpp;
    uint32_t offset = row + columnMacro + ((columnMicro & ~0xFu) << 1) + (columnMicro & 0xF);
    return ((offset & ~0x1FFu) << 3) + ((offset & 0x1C0) << 2) + (offset & 0x3F) + ((y & 16) << 7) +
        (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

static GLuint GetTexture(const HostTexture& info)
{
    constexpr uint32_t kFormat8888 = 6;
    if (!info.present)
        return g_whiteTexture;
    if (info.format != kFormat8888 || info.width == 0 || info.height == 0 || info.width > 8192 || info.height > 8192)
    {
        static uint32_t warned;
        if (warned++ < 8)
            Log("Renderer: unsupported texture format %u (%ux%u at %08X)", info.format, info.width, info.height, info.address);
        return g_whiteTexture;
    }

    // A texture is checked for changes at most once per frame.
    uint64_t key = (uint64_t(info.address) << 32) | (info.width << 16) | info.height;
    CachedTexture& cached = g_textures[key];
    if (cached.texture != 0 && cached.checkedFrame == g_frameNumber)
        return cached.texture;
    cached.checkedFrame = g_frameNumber;

    // Cheap change detection: hash a sparse sample of the guest texels.
    uint32_t storedHeight = info.tiled ? (info.height + 31) & ~31u : info.height;
    uint32_t byteSize = ((info.pitch + 31) & ~31u) * storedHeight * 4;
    uint64_t contentHash = 0xCBF29CE484222325ull;
    uint32_t step = byteSize / 4 > 4096 ? (byteSize / 4 / 4096) * 4 : 4;
    for (uint32_t i = 0; i < byteSize; i += step)
        contentHash = (contentHash ^ *(const uint32_t*)(g_base + info.address + i)) * 0x100000001B3ull;
    if (cached.texture != 0 && cached.contentHash == contentHash)
        return cached.texture;

    // Texels are A8R8G8B8 in big-endian words, which read as host BGRA.
    std::vector<uint32_t> pixels(size_t(info.width) * info.height);
    for (uint32_t y = 0; y < info.height; y++)
    {
        for (uint32_t x = 0; x < info.width; x++)
        {
            uint32_t offset = info.tiled ? TiledOffset(x, y, info.pitch, 2) : (y * info.pitch + x) * 4;
            pixels[size_t(y) * info.width + x] = Load32(info.address + offset);
        }
    }
    if (cached.texture == 0)
    {
        glGenTextures(1, &cached.texture);
        glBindTexture(GL_TEXTURE_2D, cached.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, cached.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, info.width, info.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    cached.contentHash = contentHash;
    return cached.texture;
}

// ---- Drawing ----

static GLenum BlendFactor(uint32_t factor, bool alpha)
{
    switch (factor)
    {
    case 0: return GL_ZERO;
    case 1: return GL_ONE;
    case 4: return alpha ? GL_SRC_ALPHA : GL_SRC_COLOR;
    case 5: return alpha ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE_MINUS_SRC_COLOR;
    case 6: return GL_SRC_ALPHA;
    case 7: return GL_ONE_MINUS_SRC_ALPHA;
    case 8: return alpha ? GL_DST_ALPHA : GL_DST_COLOR;
    case 9: return alpha ? GL_ONE_MINUS_DST_ALPHA : GL_ONE_MINUS_DST_COLOR;
    case 10: return GL_DST_ALPHA;
    case 11: return GL_ONE_MINUS_DST_ALPHA;
    case 16: return alpha ? GL_ONE : GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

static GLenum BlendOperation(uint32_t operation)
{
    switch (operation)
    {
    case 1: return GL_FUNC_SUBTRACT;
    case 2: return GL_MIN;
    case 3: return GL_MAX;
    case 4: return GL_FUNC_REVERSE_SUBTRACT;
    default: return GL_FUNC_ADD;
    }
}

void RendererDraw(const HostDraw& draw)
{
    if (g_gl == nullptr || draw.vertices.empty())
        return;

    GLuint texture = GetTexture(draw.texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint filter = draw.texture.present && draw.texture.linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    if (draw.blend.enabled)
    {
        uint32_t control = draw.blend.control;
        glEnable(GL_BLEND);
        glBlendFuncSeparate(BlendFactor(control & 0x1F, false), BlendFactor((control >> 8) & 0x1F, false),
            BlendFactor((control >> 16) & 0x1F, true), BlendFactor((control >> 24) & 0x1F, true));
        glBlendEquationSeparate(BlendOperation((control >> 5) & 7), BlendOperation((control >> 21) & 7));
    }
    else
    {
        glDisable(GL_BLEND);
    }
    uint32_t mask = draw.blend.writeMask;
    glColorMask(mask & 1, (mask >> 1) & 1, (mask >> 2) & 1, (mask >> 3) & 1);

    if (draw.depth.test)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_NEVER + (draw.depth.function & 7));
        glDepthMask(draw.depth.write);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }

    // Winding is judged as the triangle appears, the same way round as Direct3D.
    if ((draw.cull & 3) == 1 || (draw.cull & 3) == 2)
    {
        glEnable(GL_CULL_FACE);
        glFrontFace((draw.cull & 4) ? GL_CW : GL_CCW);
        glCullFace((draw.cull & 3) == 1 ? GL_FRONT : GL_BACK);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    // Only textures the game wants smoothed need the sharp filter.
    UseProgram(g_scale > 1 && draw.texture.present && draw.texture.linear);
    int s = int(g_scale);
    glScissor(draw.scissor[0] * s, (int(kHostHeight) - draw.scissor[3]) * s, (draw.scissor[2] - draw.scissor[0]) * s, (draw.scissor[3] - draw.scissor[1]) * s);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(draw.vertices.size() * sizeof(HostVertex)), draw.vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(draw.vertices.size()));
}

// ---- Input ----

static SDL_Scancode ScancodeFor(int key)
{
    if (key >= 'A' && key <= 'Z')
        return SDL_Scancode(SDL_SCANCODE_A + (key - 'A'));
    if (key >= '1' && key <= '9')
        return SDL_Scancode(SDL_SCANCODE_1 + (key - '1'));
    if (key >= VK_F1 && key < VK_F1 + 12)
        return SDL_Scancode(SDL_SCANCODE_F1 + (key - VK_F1));
    if (key > VK_NUMPAD0 && key <= VK_NUMPAD0 + 9)
        return SDL_Scancode(SDL_SCANCODE_KP_1 + (key - VK_NUMPAD0 - 1));
    switch (key)
    {
    case '0': return SDL_SCANCODE_0;
    case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_RETURN: return SDL_SCANCODE_RETURN;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_SHIFT: return SDL_SCANCODE_LSHIFT;
    case VK_CONTROL: return SDL_SCANCODE_LCTRL;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

// CD_AUTOPAD=<hex buttons>,...: every two seconds, tap the next entry for 80 ms
// (the last one repeats), to walk through menus unattended when testing.
// CD_AUTOPAD_START=<seconds> delays the first tap; CD_AUTOPAD_SHOTS=1 saves the
// frame shown just before each tap as frame_<tap number>.bmp.
static std::atomic<uint32_t> g_autoPadStep;

static bool AutoPad(uint16_t& buttons)
{
    static const char* autoPad = getenv("CD_AUTOPAD");
    if (autoPad == nullptr)
        return false;
    static const ULONGLONG start = GetTickCount64() +
        (getenv("CD_AUTOPAD_START") ? strtoul(getenv("CD_AUTOPAD_START"), nullptr, 10) * 1000ull : 0);
    ULONGLONG now = GetTickCount64();
    buttons = 0;
    if (now < start)
        return true;
    ULONGLONG step = (now - start) / 2000;
    g_autoPadStep = uint32_t(step) + 1;
    if ((now - start) % 2000 >= 80)
        return true;
    const char* entry = autoPad;
    for (ULONGLONG i = 0; i < step; i++)
    {
        const char* comma = strchr(entry, ',');
        if (comma == nullptr)
            break;
        entry = comma + 1;
    }
    buttons = uint16_t(strtoul(entry, nullptr, 16));
    return true;
}

HostPad RendererPad()
{
    HostPad pad;
    if (g_menuOpen)
        return pad;
    uint16_t automatic = 0;
    if (AutoPad(automatic))
    {
        pad.buttons = automatic;
        return pad;
    }
    pad.buttons = uint16_t(g_keyButtons | g_padButtons);
    // A button that chose something in the menu must not reach the game too.
    if (g_waitForRelease)
    {
        if (pad.buttons == 0)
            g_waitForRelease = false;
        return HostPad{};
    }
    pad.leftX = int16_t(g_padAxes[0]);
    pad.leftY = int16_t(g_padAxes[1]);
    pad.rightX = int16_t(g_padAxes[2]);
    pad.rightY = int16_t(g_padAxes[3]);
    pad.leftTrigger = uint8_t(g_padTriggers[0]);
    pad.rightTrigger = uint8_t(g_padTriggers[1]);
    return pad;
}

void RendererRumble(uint16_t left, uint16_t right)
{
    if (!GetConfig().vibration)
        return;
    static std::atomic<uint32_t> current;
    uint32_t wanted = (uint32_t(left) << 16) | right;
    if (current.exchange(wanted) == wanted)
        return;
    for (SDL_GameController* controller : g_controllers)
        SDL_GameControllerRumble(controller, left, right, 5000);
}

// Every connected controller drives player one.
static void ReadControllers()
{
    static const struct { SDL_GameControllerButton button; uint16_t bit; } buttons[] =
    {
        { SDL_CONTROLLER_BUTTON_DPAD_UP, 0x0001 }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0x0002 },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0x0004 }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0x0008 },
        { SDL_CONTROLLER_BUTTON_START, 0x0010 }, { SDL_CONTROLLER_BUTTON_BACK, 0x0020 },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, 0x0040 }, { SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0x0080 },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0x0100 }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x0200 },
        { SDL_CONTROLLER_BUTTON_A, 0x1000 }, { SDL_CONTROLLER_BUTTON_B, 0x2000 },
        { SDL_CONTROLLER_BUTTON_X, 0x4000 }, { SDL_CONTROLLER_BUTTON_Y, 0x8000 },
    };
    uint16_t bits = 0;
    int axes[4]{};
    int triggers[2]{};
    auto furthest = [](int current, int candidate) { return abs(candidate) > abs(current) ? candidate : current; };
    for (SDL_GameController* controller : g_controllers)
    {
        for (auto& button : buttons)
        {
            if (SDL_GameControllerGetButton(controller, button.button))
                bits |= button.bit;
        }
        // SDL's vertical axes point down; the pad's point up.
        axes[0] = furthest(axes[0], SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
        axes[1] = furthest(axes[1], -1 - SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
        axes[2] = furthest(axes[2], SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
        axes[3] = furthest(axes[3], -1 - SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
        triggers[0] = std::max(triggers[0], SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
        triggers[1] = std::max(triggers[1], SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
    }
    g_padButtons = bits;
    for (int i = 0; i < 4; i++)
        g_padAxes[i] = axes[i];
    g_padTriggers[0] = triggers[0];
    g_padTriggers[1] = triggers[1];
}

static std::atomic<bool> g_screenshotRequest; // F12

static void ToggleFullscreen()
{
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(g_window, g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void PumpEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            Log("Window closed.");
            ExitProcess(0);
        case SDL_CONTROLLERDEVICEADDED:
            if (SDL_GameController* controller = SDL_GameControllerOpen(event.cdevice.which))
            {
                g_controllers.push_back(controller);
                Log("Renderer: controller connected: %s", SDL_GameControllerName(controller));
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (auto it = g_controllers.begin(); it != g_controllers.end(); ++it)
            {
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it)) == event.cdevice.which)
                {
                    SDL_GameControllerClose(*it);
                    g_controllers.erase(it);
                    break;
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_RIGHT)
                g_menuRequest = true;
            break;
        case SDL_KEYDOWN:
            if (event.key.repeat == 0 && event.key.keysym.scancode == SDL_SCANCODE_F1)
                g_menuRequest = true;
            if (event.key.repeat == 0 && event.key.keysym.scancode == SDL_SCANCODE_F12)
                g_screenshotRequest = true;
            if (event.key.repeat == 0 && (event.key.keysym.scancode == SDL_SCANCODE_F11 ||
                (event.key.keysym.scancode == SDL_SCANCODE_RETURN && (event.key.keysym.mod & KMOD_ALT))))
            {
                ToggleFullscreen();
            }
            break;
        default:
            break;
        }
    }

    // Keyboard bindings come from crystal_defenders.ini.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    uint16_t bits = 0;
    for (auto& binding : GetConfig().keys)
    {
        SDL_Scancode code = ScancodeFor(binding.key);
        if (code != SDL_SCANCODE_UNKNOWN && keys[code] && !(alt && code == SDL_SCANCODE_RETURN))
            bits |= binding.button;
    }
    g_keyButtons = bits;
    ReadControllers();

    // Pressing both sticks opens the menu.
    static bool sticksHeld;
    bool sticks = (g_padButtons & 0x00C0) == 0x00C0;
    if (sticks && !sticksHeld)
        g_menuRequest = true;
    sticksHeld = sticks;

    // CD_MENU_AT=<tap number>: open the menu at that CD_AUTOPAD tap (tests).
    // CD_SHOT_AT=<tap number>: as if F12 were pressed at that tap (tests).
    static const char* shotAt = getenv("CD_SHOT_AT");
    static bool shotAtDone;
    if (shotAt != nullptr && !shotAtDone && g_autoPadStep == uint32_t(atoi(shotAt)) + 1)
    {
        shotAtDone = true;
        g_screenshotRequest = true;
    }
    static const char* menuAt = getenv("CD_MENU_AT");
    static bool menuAtDone;
    if (menuAt != nullptr && !menuAtDone && g_autoPadStep == uint32_t(atoi(menuAt)) + 1)
    {
        menuAtDone = true;
        g_menuRequest = true;
    }
}

// ---- Presenting ----

// Writes the game's frame as a 32-bit BMP (top-down, BGRA).
static void SaveFrame(const char* path)
{
    const uint32_t frameWidth = kHostWidth * g_scale, frameHeight = kHostHeight * g_scale;
    std::vector<uint32_t> pixels(size_t(frameWidth) * frameHeight);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_frameBuffer);
    glReadPixels(0, 0, frameWidth, frameHeight, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    if (FILE* file = fopen(path, "wb"))
    {
        const uint32_t dataSize = frameWidth * frameHeight * 4;
        uint8_t header[54]{};
        auto put = [&header](int offset, uint32_t value) { memcpy(header + offset, &value, 4); };
        header[0] = 'B';
        header[1] = 'M';
        put(2, 54 + dataSize);
        put(10, 54);
        put(14, 40);
        put(18, frameWidth);
        put(22, frameHeight); // bottom-up, as OpenGL returns the rows
        header[26] = 1;
        header[28] = 32;
        put(34, dataSize);
        fwrite(header, 1, sizeof(header), file);
        fwrite(pixels.data(), 4, pixels.size(), file);
        fclose(file);
        Log("Renderer: frame saved to %s", path);
    }
}

// ---- Overlays ----

// Draws a picture or a flat colour over the window, in window pixels.
static void DrawOverlayQuad(GLuint texture, float left, float top, float right, float bottom, float r, float g, float b, float a,
    int windowWidth, int windowHeight)
{
    float x0 = left / windowWidth * 2 - 1, x1 = right / windowWidth * 2 - 1;
    float y0 = 1 - top / windowHeight * 2, y1 = 1 - bottom / windowHeight * 2;
    const HostVertex vertices[6] =
    {
        { x0, y0, 0.5f, 1, 0, 0, r, g, b, a }, { x1, y0, 0.5f, 1, 1, 0, r, g, b, a }, { x0, y1, 0.5f, 1, 0, 1, r, g, b, a },
        { x1, y0, 0.5f, 1, 1, 0, r, g, b, a }, { x1, y1, 0.5f, 1, 1, 1, r, g, b, a }, { x0, y1, 0.5f, 1, 0, 1, r, g, b, a },
    };
    glBindTexture(GL_TEXTURE_2D, texture);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

struct OverlayTexture
{
    GLuint texture{};
    uint32_t width{}, height{};

    void Set(const OverlayImage& image)
    {
        width = image.width;
        height = image.height;
        if (image.pixels.empty())
            return;
        if (texture == 0)
            glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, image.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
};

// ---- Achievement notice ----

static std::mutex g_toastMutex;
static std::wstring g_toastTitle, g_toastBody;
static ULONGLONG g_toastUntil;
static bool g_toastDirty;

// Called by achievements.cpp from the game's threads.
void LinuxToast(const std::wstring& title, const std::wstring& body)
{
    std::lock_guard lock(g_toastMutex);
    g_toastTitle = title;
    g_toastBody = body;
    g_toastUntil = GetTickCount64() + 6000;
    g_toastDirty = true;
}

// ---- Settings menu ----
// A panel in the style of the game's own dialogs, drawn over its image and
// worked like them: up and down to move, A to choose, B to go back.

static const struct { uint32_t id; const char* code; const wchar_t* name; } kLanguages[] =
{
    { 0, "auto", nullptr }, { 4, "fr", L"Français" }, { 1, "en", L"English" }, { 3, "de", L"Deutsch" },
    { 5, "es", L"Español" }, { 6, "it", L"Italiano" }, { 2, "ja", L"日本語" },
};

enum MenuPage { kPageMain, kPageLanguage, kPageConfirmRestart, kPageConfirmQuit, kPageResolution };

static struct
{
    int page{};
    int cursor{};
    int language{};      // index in kLanguages waiting for confirmation
    uint16_t previous{}; // buttons held on the previous frame
    bool dirty{};
    float drawnScale{};
    OverlayTexture picture;
} g_menu;

static std::wstring MenuText(const wchar_t* french, const wchar_t* english)
{
    return GuestLanguage() == 4 ? french : english;
}

static void WindowSize(int& width, int& height)
{
    SDL_GL_GetDrawableSize(g_window, &width, &height);
}

// Sizes offered: the usual 16:9 ones and the Steam Deck's 1280x800, as far as
// they fit the screen the window is on, then that screen's own size, which
// means full screen.
static std::vector<SDL_Point> MenuResolutions()
{
    SDL_DisplayMode mode{};
    SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(g_window), &mode);
    std::vector<SDL_Point> sizes;
    static const SDL_Point usual[] =
    {
        { 1280, 720 }, { 1280, 800 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3200, 1800 }, { 3840, 2160 }, { 5120, 2880 },
    };
    for (const SDL_Point& size : usual)
    {
        if (size.x < mode.w && size.y < mode.h)
            sizes.push_back(size);
    }
    if (mode.w > 0 && mode.h > 0)
        sizes.push_back({ mode.w, mode.h });
    return sizes;
}

static std::wstring MenuSizeText(int width, int height)
{
    return std::to_wstring(width) + L" × " + std::to_wstring(height);
}

static std::vector<OverlayItem> MenuItems(std::wstring& title, std::wstring& message)
{
    const Config& config = GetConfig();
    auto onOff = [](bool on) { return on ? MenuText(L"Oui", L"On") : MenuText(L"Non", L"Off"); };
    const std::wstring automatic = MenuText(L"Automatique", L"Automatic");
    int width = 0, height = 0;
    WindowSize(width, height);

    std::vector<OverlayItem> items;
    if (!g_menuOpen)
    {
        // Not the menu: the notice shown while the game waits in the background.
        title = L"Pause";
        message = MenuText(L"Le jeu reprendra quand sa fenêtre sera de nouveau active.",
            L"The game will resume when its window is active again.");
        return items;
    }
    switch (g_menu.page)
    {
    case kPageLanguage:
        title = MenuText(L"Langue", L"Language");
        for (auto& language : kLanguages)
            items.push_back({ language.name ? language.name : automatic, config.language == language.id ? L"✓" : L"" });
        break;

    case kPageResolution:
    {
        title = MenuText(L"Résolution", L"Resolution");
        const std::vector<SDL_Point> sizes = MenuResolutions();
        for (size_t i = 0; i < sizes.size(); i++)
        {
            bool last = i + 1 == sizes.size();
            items.push_back({ MenuSizeText(sizes[i].x, sizes[i].y) + (last ? MenuText(L"  (plein écran)", L"  (full screen)") : L""),
                sizes[i].x == width && sizes[i].y == height ? L"✓" : L"" });
        }
        break;
    }

    case kPageConfirmRestart:
        title = MenuText(L"Langue", L"Language");
        message = MenuText(L"Le jeu va revenir à l'écran titre pour changer de langue. Une partie en cours qui n'est pas sauvegardée sera perdue.",
            L"The game will go back to the title screen to change the language. A game in progress that is not saved will be lost.");
        items.push_back({ MenuText(L"Changer de langue", L"Change the language"), L"" });
        items.push_back({ MenuText(L"Annuler", L"Cancel"), L"" });
        break;

    case kPageConfirmQuit:
        title = MenuText(L"Quitter", L"Quit");
        message = MenuText(L"Quitter le jeu ? Une partie en cours qui n'est pas sauvegardée sera perdue.",
            L"Quit the game? A game in progress that is not saved will be lost.");
        items.push_back({ MenuText(L"Quitter le jeu", L"Quit the game"), L"" });
        items.push_back({ MenuText(L"Annuler", L"Cancel"), L"" });
        break;

    default:
    {
        std::wstring language = automatic;
        for (auto& candidate : kLanguages)
        {
            if (candidate.id == config.language && candidate.name != nullptr)
                language = candidate.name;
        }
        title = MenuText(L"Réglages", L"Settings");
        items.push_back({ MenuText(L"Langue", L"Language"), language });
        items.push_back({ MenuText(L"Résolution", L"Resolution"), MenuSizeText(width, height) });
        items.push_back({ MenuText(L"Rendu", L"Rendering"), config.sharp ? MenuText(L"Net", L"Sharp") : MenuText(L"Lisse", L"Smooth") });
        items.push_back({ MenuText(L"Plein écran", L"Full screen"), onOff(g_fullscreen) });
        items.push_back({ L"Volume", std::to_wstring(config.volume) + L" %" });
        items.push_back({ MenuText(L"Vibrations", L"Vibration"), onOff(config.vibration) });
        items.push_back({ MenuText(L"Pause en arrière-plan", L"Pause in background"), onOff(config.pauseInBackground) });
        items.push_back({ MenuText(L"Quitter le jeu", L"Quit the game"), L"" });
        items.push_back({ MenuText(L"Reprendre", L"Resume"), L"" });
        break;
    }
    }
    return items;
}

// The language is read once by the game when it starts, so changing it means
// starting again: this program replaces itself with a fresh copy, which takes
// the window's place and runs through the opening logos unseen.
static void RestartGame()
{
    int x = 0, y = 0, width = 0, height = 0;
    SDL_GetWindowPosition(g_window, &x, &y);
    SDL_GetWindowSize(g_window, &width, &height);
    char resume[96];
    snprintf(resume, sizeof(resume), "%d,%d,%d,%d,%d", x, y, width, height, int(g_fullscreen));
    setenv("CD_RESUME", resume, 1);

    // The arguments this copy was started with.
    static std::vector<std::string> arguments;
    static std::vector<char*> pointers;
    if (FILE* file = fopen("/proc/self/cmdline", "rb"))
    {
        std::string all;
        char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), file)) != 0)
            all.append(buffer, read);
        fclose(file);
        for (size_t start = 0; start < all.size(); start += strlen(all.c_str() + start) + 1)
            arguments.emplace_back(all.c_str() + start);
    }
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    pointers.push_back(nullptr);

    Log("Menu: relaunching to apply the new language.");
    RendererRumble(0, 0);
    fflush(nullptr);
    SDL_Quit();
    if (!arguments.empty())
        execv("/proc/self/exe", pointers.data());
    _exit(0);
}

static void MenuClose()
{
    g_menuOpen = false;
    g_waitForRelease = true;
    XamSystemUiChanged(false);
}

static void MenuChoose()
{
    Config& config = MutableConfig();
    switch (g_menu.page)
    {
    case kPageMain:
        switch (g_menu.cursor)
        {
        case 0:
            g_menu.page = kPageLanguage;
            g_menu.cursor = 0;
            for (int i = 0; i < int(std::size(kLanguages)); i++)
            {
                if (kLanguages[i].id == config.language)
                    g_menu.cursor = i;
            }
            break;
        case 1:
        {
            int width = 0, height = 0;
            WindowSize(width, height);
            g_menu.page = kPageResolution;
            g_menu.cursor = 0;
            const std::vector<SDL_Point> sizes = MenuResolutions();
            for (int i = 0; i < int(sizes.size()); i++)
            {
                if (sizes[size_t(i)].x == width && sizes[size_t(i)].y == height)
                    g_menu.cursor = i;
            }
            break;
        }
        case 2:
            config.sharp = !config.sharp;
            ConfigSave("affichage", "rendu", config.sharp ? "net" : "lisse");
            break;
        case 3:
            ToggleFullscreen();
            ConfigSave("affichage", "plein_ecran", g_fullscreen ? "1" : "0");
            break;
        case 4:
            // Each press lowers the volume by a fifth, then back to full.
            config.volume = config.volume >= 20 ? (config.volume - 1) / 20 * 20 : 100;
            ConfigSave("son", "volume", std::to_string(config.volume).c_str());
            break;
        case 5:
            RendererRumble(0, 0);
            config.vibration = !config.vibration;
            ConfigSave("jeu", "vibration", config.vibration ? "1" : "0");
            break;
        case 6:
            config.pauseInBackground = !config.pauseInBackground;
            ConfigSave("jeu", "pause_arriere_plan", config.pauseInBackground ? "1" : "0");
            break;
        case 7:
            g_menu.page = kPageConfirmQuit;
            g_menu.cursor = 1;
            break;
        default:
            MenuClose();
            break;
        }
        break;

    case kPageLanguage:
        if (kLanguages[g_menu.cursor].id == config.language)
        {
            g_menu.page = kPageMain;
            g_menu.cursor = 0;
        }
        else
        {
            g_menu.language = g_menu.cursor;
            g_menu.page = kPageConfirmRestart;
            g_menu.cursor = 0;
        }
        break;

    case kPageConfirmRestart:
        if (g_menu.cursor == 0)
        {
            ConfigSave("jeu", "langue", kLanguages[g_menu.language].code);
            RestartGame();
        }
        g_menu.page = kPageLanguage;
        g_menu.cursor = g_menu.language;
        break;

    case kPageConfirmQuit:
        if (g_menu.cursor == 0)
        {
            Log("Window closed.");
            RendererRumble(0, 0);
            ExitProcess(0);
        }
        g_menu.page = kPageMain;
        g_menu.cursor = 7;
        break;

    case kPageResolution:
    {
        const std::vector<SDL_Point> sizes = MenuResolutions();
        if (g_menu.cursor < int(sizes.size()))
        {
            const SDL_Point& size = sizes[size_t(g_menu.cursor)];
            bool wholeScreen = g_menu.cursor + 1 == int(sizes.size());
            if (wholeScreen != g_fullscreen)
                ToggleFullscreen();
            ConfigSave("affichage", "plein_ecran", g_fullscreen ? "1" : "0");
            if (!wholeScreen)
            {
                config.windowWidth = uint32_t(size.x);
                config.windowHeight = uint32_t(size.y);
                ConfigSave("affichage", "fenetre", (std::to_string(size.x) + "x" + std::to_string(size.y)).c_str());
                SDL_SetWindowSize(g_window, size.x, size.y);
                SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
        }
        break;
    }
    }
}

// Opens or closes the menu when asked, and applies this frame's button presses.
static void MenuUpdate()
{
    if (g_menuRequest.exchange(false))
    {
        if (g_menuOpen)
        {
            MenuClose();
            return;
        }
        g_menu.page = kPageMain;
        g_menu.cursor = 0;
        g_menu.previous = 0xFFFF; // whatever is held when the menu opens does not count
        g_menu.dirty = true;
        g_menuOpen = true;
        XamSystemUiChanged(true);
    }
    if (!g_menuOpen)
        return;

    // The pad, with the left stick counted as the d-pad, and a few keys.
    uint16_t buttons = g_padButtons;
    buttons |= g_padAxes[1] > 16000 ? 0x0001 : g_padAxes[1] < -16000 ? 0x0002 : 0;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    buttons |= keys[SDL_SCANCODE_UP] ? 0x0001 : 0;
    buttons |= keys[SDL_SCANCODE_DOWN] ? 0x0002 : 0;
    buttons |= keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] ? 0x1000 : 0;
    buttons |= keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE] ? 0x2000 : 0;
    uint16_t automatic = 0;
    if (AutoPad(automatic))
        buttons |= automatic;

    uint16_t pressed = uint16_t(buttons & ~g_menu.previous);
    g_menu.previous = buttons;
    if (pressed == 0)
        return;

    std::wstring title, message;
    int count = int(MenuItems(title, message).size());
    if (count == 0)
        return;
    if (pressed & 0x0001)
        g_menu.cursor = (g_menu.cursor + count - 1) % count;
    if (pressed & 0x0002)
        g_menu.cursor = (g_menu.cursor + 1) % count;
    if (pressed & 0x1000)
        MenuChoose();
    else if (pressed & 0x2000)
    {
        if (g_menu.page == kPageMain)
            MenuClose();
        else
        {
            g_menu.cursor = g_menu.page == kPageConfirmRestart ? g_menu.language : g_menu.page == kPageConfirmQuit ? 7 :
                g_menu.page == kPageResolution ? 1 : 0;
            g_menu.page = g_menu.page == kPageConfirmRestart ? kPageLanguage : kPageMain;
        }
    }
    g_menu.dirty = true;
}

// Writes what is about to be shown in the window as a BMP (tests).
static void SaveWindow(const char* path, int width, int height)
{
    std::vector<uint32_t> pixels(size_t(width) * size_t(height));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    if (FILE* file = fopen(path, "wb"))
    {
        const uint32_t dataSize = uint32_t(width * height * 4);
        uint8_t header[54]{};
        auto put = [&header](int offset, uint32_t value) { memcpy(header + offset, &value, 4); };
        header[0] = 'B';
        header[1] = 'M';
        put(2, 54 + dataSize);
        put(10, 54);
        put(14, 40);
        put(18, uint32_t(width));
        put(22, uint32_t(height));
        header[26] = 1;
        header[28] = 32;
        put(34, dataSize);
        fwrite(header, 1, sizeof(header), file);
        fwrite(pixels.data(), 4, pixels.size(), file);
        fclose(file);
    }
}

// Puts the game's current frame in the window, as large as fits while keeping
// its 16:9 shape, with the settings menu, the pause notice or an achievement
// notice over it when one is up.
static void ShowFrame()
{
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(g_window, &width, &height);
    if (width <= 0 || height <= 0)
        return;

    float scale = std::min(float(width) / kHostWidth, float(height) / kHostHeight);
    int targetWidth = int(kHostWidth * scale), targetHeight = int(kHostHeight * scale);
    int left = (width - targetWidth) / 2, bottom = (height - targetHeight) / 2;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_frameBuffer);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlitFramebuffer(0, 0, kHostWidth * g_scale, kHostHeight * g_scale, left, bottom, left + targetWidth, bottom + targetHeight,
        GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Overlays are drawn straight into the window.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    UseProgram(false);

    if (g_menuOpen || g_backgroundPause)
    {
        if (g_menu.dirty || g_menu.drawnScale != scale)
        {
            std::wstring title, message;
            std::vector<OverlayItem> items = MenuItems(title, message);
            g_menu.picture.Set(OverlayPanel(title, message, items, g_menuOpen ? g_menu.cursor : -1,
                MenuText(L"Confirmer", L"Confirm"), MenuText(L"Retour", L"Back"), scale));
            g_menu.dirty = false;
            g_menu.drawnScale = scale;
        }
        // Darken the game's image, as its own dialogs do, and put the panel in the middle.
        DrawOverlayQuad(g_whiteTexture, float(left), float(height - bottom - targetHeight), float(left + targetWidth), float(height - bottom),
            0, 0, 0, 0.5f, width, height);
        if (g_menu.picture.texture != 0)
        {
            float x = std::floor((width - float(g_menu.picture.width)) * 0.5f), y = std::floor((height - float(g_menu.picture.height)) * 0.5f);
            DrawOverlayQuad(g_menu.picture.texture, x, y, x + g_menu.picture.width, y + g_menu.picture.height, 1, 1, 1, 1, width, height);
        }
    }

    // An achievement notice near the bottom, for a few seconds.
    {
        static OverlayTexture picture;
        static float drawnScale;
        std::lock_guard lock(g_toastMutex);
        if (GetTickCount64() < g_toastUntil)
        {
            if (g_toastDirty || drawnScale != scale)
            {
                picture.Set(OverlayPanel(g_toastTitle, g_toastBody, {}, -1, L"", L"", scale));
                g_toastDirty = false;
                drawnScale = scale;
            }
            if (picture.texture != 0)
            {
                float x = std::floor((width - float(picture.width)) * 0.5f);
                float y = std::floor(height - bottom - targetHeight * 0.08f - float(picture.height));
                DrawOverlayQuad(picture.texture, x, y, x + picture.width, y + picture.height, 1, 1, 1, 1, width, height);
            }
        }
    }

    // CD_AUTOPAD_SHOTS=1: with the menu open, also keep what the window shows.
    // CD_WINDOW_SHOTS=1: do so at every tap, and every second while paused in the background.
    static const bool shots = getenv("CD_AUTOPAD_SHOTS") != nullptr;
    static const bool always = getenv("CD_WINDOW_SHOTS") != nullptr;
    static uint32_t shotStep;
    static ULONGLONG pauseShot;
    if (always && g_backgroundPause && GetTickCount64() - pauseShot > 1000)
    {
        pauseShot = GetTickCount64();
        SaveWindow("pause.bmp", width, height);
    }
    if (shots && (g_menuOpen || always) && g_autoPadStep != shotStep)
    {
        shotStep = g_autoPadStep;
        char path[64];
        snprintf(path, sizeof(path), "menu_%u.bmp", shotStep);
        SaveWindow(path, width, height);
    }
    SDL_GL_SwapWindow(g_window);
}

void RendererPresent()
{
    if (g_gl == nullptr)
        return;
    uint32_t frameNumber = g_frameNumber++;

    // CD_SCREENSHOT=<frame>[,<frame>...]: save those frames as frame_<n>.bmp.
    if (const char* list = getenv("CD_SCREENSHOT"))
    {
        for (const char* p = list; *p != 0;)
        {
            char* end;
            unsigned long wanted = strtoul(p, &end, 10);
            if (end == p)
                break;
            if (wanted == frameNumber)
            {
                char path[64];
                snprintf(path, sizeof(path), "frame_%lu.bmp", wanted);
                SaveFrame(path);
            }
            p = *end == ',' ? end + 1 : end;
        }
    }
    static const bool autoPadShots = getenv("CD_AUTOPAD_SHOTS") != nullptr;
    static uint32_t shotStep;
    if (autoPadShots && g_autoPadStep != shotStep)
    {
        shotStep = g_autoPadStep;
        char path[64];
        snprintf(path, sizeof(path), "frame_%u.bmp", shotStep - 1);
        SaveFrame(path);
    }

    // F12: keep this frame as a picture.
    if (g_screenshotRequest.exchange(false))
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        char path[96];
        mkdir("captures", 0755);
        snprintf(path, sizeof(path), "captures/crystal_defenders_%04u%02u%02u_%02u%02u%02u.bmp", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond);
        SaveFrame(path);
    }

    PumpEvents();
    MenuUpdate();
    if (!RendererFastStart())
        ShowFrame();

    // The pause: while the settings menu is open, or the window is in the
    // background if asked, no new frame is taken from the game, which leaves
    // it waiting where it is. The same frame stays on screen under the panel.
    static const bool unattended = getenv("CD_HEADLESS") != nullptr || getenv("CD_AUTOPAD") != nullptr;
    static bool everFocused;
    for (;;)
    {
        // Only trust "not focused" from a system that has reported focus before.
        bool focused = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        everFocused = everFocused || focused;
        bool background = GetConfig().pauseInBackground && !unattended && !RendererFastStart() && everFocused && !focused;

        // CD_TEST_BACKGROUND=<from>-<to>: behave as if the window were in the
        // background between those seconds after start (tests).
        static const char* testBackground = getenv("CD_TEST_BACKGROUND");
        if (testBackground != nullptr)
        {
            static const ULONGLONG start = GetTickCount64();
            unsigned from = 0, to = 0;
            sscanf(testBackground, "%u-%u", &from, &to);
            ULONGLONG seconds = (GetTickCount64() - start) / 1000;
            background = GetConfig().pauseInBackground && seconds >= from && seconds < to;
        }
        if (background != g_backgroundPause)
        {
            g_backgroundPause = background;
            g_menu.dirty = true;
            if (background)
                RendererRumble(0, 0);
        }
        if (!g_menuOpen && !background)
            break;
        Sleep(16);
        PumpEvents();
        MenuUpdate();
        ShowFrame();
    }

    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(g_window, &width, &height);

    // Draw the next frame at the smallest multiple of 1280x720 that covers the
    // window, so nothing is enlarged afterwards. CD_SCALE=<n> forces it.
    uint32_t wanted = 1;
    if (GetConfig().sharp)
    {
        while (wanted < 4 && int(kHostWidth * wanted) < width && int(kHostHeight * wanted) < height)
            wanted++;
    }
    static const char* forced = getenv("CD_SCALE");
    if (forced != nullptr && atoi(forced) >= 1 && atoi(forced) <= 4)
        wanted = uint32_t(atoi(forced));
    if (wanted != g_scale && CreateFrameTargets(wanted))
        Log("Renderer: drawing at %ux%u", kHostWidth * wanted, kHostHeight * wanted);

    glBindFramebuffer(GL_FRAMEBUFFER, g_frameBuffer);
    glViewport(0, 0, kHostWidth * g_scale, kHostHeight * g_scale);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(g_clearColor[0], g_clearColor[1], g_clearColor[2], g_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);

    // Pace to the console's 60 Hz. CD_NOLIMIT=1 disables the limiter.
    static const bool unlimited = getenv("CD_NOLIMIT") != nullptr;
    if (!unlimited && !RendererFastStart())
    {
        static LONGLONG next;
        const LONGLONG period = 1000000000ll / LONGLONG(GetConfig().framesPerSecond);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (next == 0 || now.QuadPart > next + period * 4)
            next = now.QuadPart; // fell behind: do not try to catch up
        next += period;
        for (;;)
        {
            QueryPerformanceCounter(&now);
            LONGLONG remaining = next - now.QuadPart;
            if (remaining <= 0)
                break;
            if (remaining > 2000000)
                Sleep(DWORD(remaining / 1000000 - 1));
            else
                SwitchToThread();
        }
    }
}
