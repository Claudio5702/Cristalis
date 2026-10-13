// Linux renderer: an SDL2 window and OpenGL 3.3 drawing what the GPU command
// processor decodes. The counterpart of renderer.cpp (Direct3D 11) on Windows.
// Not carried over yet: the settings menu, the sharp high-resolution mode and
// the pause in background.
#include "renderer.h"
#include "config.h"
#include "guest.h"

#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <windows.h>

#include <atomic>
#include <cmath>
#include <map>
#include <string>
#include <vector>

static SDL_Window* g_window;
static SDL_GLContext g_gl;
static GLuint g_program;
static GLuint g_vertexArray;
static GLuint g_vertexBuffer;
static GLuint g_frameBuffer;
static GLuint g_frameTexture;
static GLuint g_whiteTexture;
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

bool RendererFastStart()
{
    return false;
}

bool RendererSilent()
{
    return false;
}

void RendererSetClearColor(float r, float g, float b, float a)
{
    g_clearColor[0] = r;
    g_clearColor[1] = g;
    g_clearColor[2] = b;
    g_clearColor[3] = a;
}

// ---- Set-up ----

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
    g_window = SDL_CreateWindow("Crystal Defenders", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
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
    g_program = glCreateProgram();
    glAttachShader(g_program, Compile(GL_VERTEX_SHADER,
        "#version 330 core\n"
        "layout(location = 0) in vec4 position; layout(location = 1) in vec2 uv; layout(location = 2) in vec4 color;\n"
        "out vec2 vUv; out vec4 vColor;\n"
        "void main() { gl_Position = vec4(position.xy, (position.z * 2.0 - position.w), position.w); vUv = uv; vColor = color; }\n"));
    glAttachShader(g_program, Compile(GL_FRAGMENT_SHADER,
        "#version 330 core\n"
        "uniform sampler2D tex; in vec2 vUv; in vec4 vColor; out vec4 outColor;\n"
        "void main() { outColor = texture(tex, vUv) * vColor; }\n"));
    glLinkProgram(g_program);
    glUseProgram(g_program);

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

    // The game's 1280x720 frame, with a depth buffer.
    glGenTextures(1, &g_frameTexture);
    glBindTexture(GL_TEXTURE_2D, g_frameTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kHostWidth, kHostHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLuint depth = 0;
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, kHostWidth, kHostHeight);
    glGenFramebuffers(1, &g_frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_frameBuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_frameTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        Log("Renderer: the frame buffer is not usable");
        return false;
    }

    const uint32_t white = 0xFFFFFFFF;
    glGenTextures(1, &g_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, g_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glViewport(0, 0, kHostWidth, kHostHeight);
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

    glScissor(draw.scissor[0], int(kHostHeight) - draw.scissor[3], draw.scissor[2] - draw.scissor[0], draw.scissor[3] - draw.scissor[1]);
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
    uint16_t automatic = 0;
    if (AutoPad(automatic))
    {
        pad.buttons = automatic;
        return pad;
    }
    pad.buttons = uint16_t(g_keyButtons | g_padButtons);
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
        case SDL_KEYDOWN:
            if (event.key.repeat == 0 && (event.key.keysym.scancode == SDL_SCANCODE_F11 ||
                (event.key.keysym.scancode == SDL_SCANCODE_RETURN && (event.key.keysym.mod & KMOD_ALT))))
            {
                g_fullscreen = !g_fullscreen;
                SDL_SetWindowFullscreen(g_window, g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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
}

// ---- Presenting ----

// Writes the game's frame as a 32-bit BMP (top-down, BGRA).
static void SaveFrame(const char* path)
{
    std::vector<uint32_t> pixels(size_t(kHostWidth) * kHostHeight);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_frameBuffer);
    glReadPixels(0, 0, kHostWidth, kHostHeight, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    if (FILE* file = fopen(path, "wb"))
    {
        const uint32_t dataSize = kHostWidth * kHostHeight * 4;
        uint8_t header[54]{};
        auto put = [&header](int offset, uint32_t value) { memcpy(header + offset, &value, 4); };
        header[0] = 'B';
        header[1] = 'M';
        put(2, 54 + dataSize);
        put(10, 54);
        put(14, 40);
        put(18, kHostWidth);
        put(22, kHostHeight); // bottom-up, as OpenGL returns the rows
        header[26] = 1;
        header[28] = 32;
        put(34, dataSize);
        fwrite(header, 1, sizeof(header), file);
        fwrite(pixels.data(), 4, pixels.size(), file);
        fclose(file);
        Log("Renderer: frame saved to %s", path);
    }
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

    PumpEvents();

    // Copy the frame to the window, as large as fits while keeping its 16:9
    // shape, with black bars for the rest.
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(g_window, &width, &height);
    if (width > 0 && height > 0)
    {
        float scale = std::min(float(width) / kHostWidth, float(height) / kHostHeight);
        int targetWidth = int(kHostWidth * scale), targetHeight = int(kHostHeight * scale);
        int left = (width - targetWidth) / 2, bottom = (height - targetHeight) / 2;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_frameBuffer);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBlitFramebuffer(0, 0, kHostWidth, kHostHeight, left, bottom, left + targetWidth, bottom + targetHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        SDL_GL_SwapWindow(g_window);
    }

    // Back to the game's frame, cleared as the console does after each one.
    glBindFramebuffer(GL_FRAMEBUFFER, g_frameBuffer);
    glViewport(0, 0, kHostWidth, kHostHeight);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(g_clearColor[0], g_clearColor[1], g_clearColor[2], g_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);

    // Pace to the console's 60 Hz. CD_NOLIMIT=1 disables the limiter.
    static const bool unlimited = getenv("CD_NOLIMIT") != nullptr;
    if (!unlimited)
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
