#include "renderer.h"
#include "config.h"
#ifdef CD_DARKLAUNCHER
#include "dl_bridge.h"
#endif
#include "translations.h"
#include "guest.h"
#include "xex_header.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cmath>
#include <map>
#include <string>
#include <thread>
#include <vector>

static std::atomic<HWND> g_window;
static ID3D11Device* g_device;
static ID3D11DeviceContext* g_context;
static IDXGISwapChain* g_swapChain;
static ID3D11Texture2D* g_frameTexture;
static ID3D11RenderTargetView* g_frameView;
static ID3D11DepthStencilView* g_depthView;
static ID3D11PixelShader* g_pixelShaders[2]; // plain, sharp
static ID3D11RasterizerState* g_rasterStates[8];
static uint32_t g_halves;                    // the frame is 1280x720 times this, counted in halves
                                             // (2: 1280x720, 3: 1920x1080, 4: 2560x1440, 6: 3840x2160)
static uint32_t FrameWidth() { return kHostWidth * g_halves / 2; }
static uint32_t FrameHeight() { return kHostHeight * g_halves / 2; }
static std::map<uint32_t, ID3D11DepthStencilState*> g_depthStates;
static ID3D11Buffer* g_vertexBuffer;
static ID3D11SamplerState* g_samplers[2]; // point, linear
static ID3D11ShaderResourceView* g_whiteTexture;
static std::map<uint64_t, ID3D11BlendState*> g_blendStates;
static float g_clearColor[4] = { 0, 0, 0, 1 };

constexpr uint32_t kMaxVertices = 65536;

struct CachedTexture
{
    ID3D11Texture2D* texture{};
    ID3D11ShaderResourceView* view{};
    uint64_t contentHash{};
    uint32_t checkedFrame{};
};
static std::map<uint64_t, CachedTexture> g_textures;
static uint32_t g_frameNumber = 1;
// After a relaunch from the settings menu, this many frames are run as fast as
// possible without being shown or heard: the logos before the title screen.
static uint32_t g_fastStartFrames = getenv("CD_RESUME") != nullptr ? 530 : 0;

bool RendererFastStart()
{
    return g_frameNumber < g_fastStartFrames;
}

static std::atomic<bool> g_backgroundPause; // held because the window is not the active one

bool RendererSilent()
{
    return RendererFastStart() || g_backgroundPause;
}

static const char kShaderSource[] = R"(
struct VSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };
VSOut vs(VSIn i) { VSOut o; o.pos = i.pos; o.uv = i.uv; o.color = i.color; return o; }
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float4 ps(VSOut i) : SV_Target { return tex.Sample(smp, i.uv) * i.color; }
// Enlarged texels keep hard edges, one screen pixel wide, instead of blurring.
float4 ps_sharp(VSOut i) : SV_Target
{
    float2 size;
    tex.GetDimensions(size.x, size.y);
    float2 texel = i.uv * size;
    float2 seam = floor(texel + 0.5);
    texel = seam + clamp((texel - seam) / fwidth(texel), -0.5, 0.5);
    return tex.Sample(smp, texel / size) * i.color;
}
)";

// ---- Window ----

constexpr DWORD kWindowedStyle = WS_OVERLAPPEDWINDOW;

// Borderless full screen on the monitor the window is on, and back.
static bool g_fullscreen;
static WINDOWPLACEMENT g_windowedPlacement{ sizeof(WINDOWPLACEMENT) }; // where the window goes back to

static void ToggleFullscreen(HWND window)
{
    bool& fullscreen = g_fullscreen;
    WINDOWPLACEMENT& windowed = g_windowedPlacement;

    if (!fullscreen)
    {
        MONITORINFO monitor{ sizeof(MONITORINFO) };
        if (!GetWindowPlacement(window, &windowed) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        SetWindowLongW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    else
    {
        SetWindowLongW(window, GWL_STYLE, kWindowedStyle | WS_VISIBLE);
        SetWindowPlacement(window, &windowed);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    fullscreen = !fullscreen;
}

// Gives the game's image the wanted size: a window of that size in the middle
// of its monitor, or full screen when the size is the monitor's own.
static void ResizeImage(HWND window, int width, int height)
{
    MONITORINFO monitor{ sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT& screen = monitor.rcMonitor;
    bool wholeScreen = width >= screen.right - screen.left || height >= screen.bottom - screen.top;
    if (wholeScreen != g_fullscreen)
        ToggleFullscreen(window);
    ConfigSave("affichage", "plein_ecran", g_fullscreen ? "1" : "0");
    if (wholeScreen)
        return;

    RECT rect{ 0, 0, width, height };
    AdjustWindowRect(&rect, kWindowedStyle, FALSE);
    int outerWidth = rect.right - rect.left;
    int outerHeight = rect.bottom - rect.top;
    const RECT& work = monitor.rcWork;
    int x = work.left + ((work.right - work.left) - outerWidth) / 2;
    int y = work.top + ((work.bottom - work.top) - outerHeight) / 2;
    ShowWindow(window, SW_RESTORE);
    SetWindowPos(window, nullptr, x, y < work.top ? work.top : y, outerWidth, outerHeight, SWP_NOZORDER | SWP_NOOWNERZORDER);
}

// Settings menu: F1, right click, or both sticks pressed on the pad. The menu
// itself is drawn over the game's image by the renderer (see "Settings menu").
constexpr UINT kMenuMessage = WM_APP + 1;       // open or close the menu
constexpr UINT kFullscreenMessage = WM_APP + 2; // toggle full screen, asked by the menu
constexpr UINT kMenuKeyMessage = WM_APP + 3;    // wParam: buttons to press in the menu (tests)
constexpr UINT kResizeMessage = WM_APP + 4;     // wParam, lParam: image width and height wanted
static std::atomic<bool> g_menuOpen;
static std::atomic<bool> g_menuRequest;
static std::atomic<bool> g_menuDirty;
static std::atomic<uint32_t> g_menuInjected;
static std::atomic<bool> g_waitForRelease; // after the menu: ignore buttons still held
static std::atomic<bool> g_screenshotRequest; // F12
static ULONGLONG g_lastMouseMove;             // the pointer hides over the game after a while
static LPARAM g_lastMousePosition = -1;
constexpr ULONGLONG kPointerHideDelay = 2500;

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_RBUTTONUP:
    case kMenuMessage:
        g_menuRequest = true;
        return 0;

    case kFullscreenMessage:
        ToggleFullscreen(window);
        ConfigSave("affichage", "plein_ecran", g_fullscreen ? "1" : "0");
        g_menuDirty = true;
        return 0;

    case kMenuKeyMessage:
        g_menuInjected |= uint32_t(wParam);
        return 0;

    case kResizeMessage:
        ResizeImage(window, int(wParam), int(lParam));
        g_menuDirty = true;
        return 0;

    case WM_CLOSE:
    case WM_DESTROY:
        Log("Window closed.");
        RendererRumble(0, 0);
        ExitProcess(0);

    case WM_SYSKEYDOWN:
        // Alt+Enter. Handled here so it does not reach the system menu.
        if (wParam == VK_RETURN && (lParam & (1 << 29)) && !(lParam & (1 << 30)))
        {
            ToggleFullscreen(window);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_F11 && !(lParam & (1 << 30)))
        {
            ToggleFullscreen(window);
            return 0;
        }
        if (wParam == VK_F1 && !(lParam & (1 << 30)))
        {
            g_menuRequest = true;
            return 0;
        }
        if (wParam == VK_F12 && !(lParam & (1 << 30)))
        {
            g_screenshotRequest = true;
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
        if (lParam != g_lastMousePosition)
        {
            g_lastMousePosition = lParam;
            g_lastMouseMove = GetTickCount64();
        }
        break;

    case WM_SETCURSOR:
        // No pointer over the game's image once the mouse has been still a while.
        if (LOWORD(lParam) == HTCLIENT && GetTickCount64() - g_lastMouseMove > kPointerHideDelay)
        {
            SetCursor(nullptr);
            return TRUE;
        }
        break;

    case WM_TIMER:
    {
        // WM_SETCURSOR only comes when the mouse moves: hide the pointer from here.
        POINT point;
        if (GetTickCount64() - g_lastMouseMove > kPointerHideDelay && GetCursorPos(&point) && WindowFromPoint(point) == window &&
            SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTCLIENT)
            SetCursor(nullptr);
        return 0;
    }

    case WM_SYSCHAR:
        if (wParam == VK_RETURN)
            return 0; // no beep for Alt+Enter
        break;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize = { 320, 200 };
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void WatchMenuButtons(HWND window);

static void WindowThread()
{
    // Work in real pixels on scaled displays; otherwise Windows stretches the
    // window's image and the full screen back buffer is not the panel's size.
    // Looked up at run time: the function needs Windows 10 1703.
    UINT dpi = 96;
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        using SetAwareness = BOOL(WINAPI*)(HANDLE);
        using GetSystemDpi = UINT(WINAPI*)();
        if (auto setAwareness = (SetAwareness)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
            setAwareness((HANDLE)(INT_PTR)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (auto getSystemDpi = (GetSystemDpi)(void*)GetProcAddress(user32, "GetDpiForSystem"))
            dpi = getSystemDpi();
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    windowClass.hIcon = LoadIconW(windowClass.hInstance, MAKEINTRESOURCEW(1)); // resources/crystal_defenders.rc
    windowClass.lpszClassName = L"CrystalDefendersRecomp";
    RegisterClassW(&windowClass);

    // Same apparent size at any display scale: 1280x720 at 100 %.
    DWORD style = kWindowedStyle;
    RECT rect{ 0, 0, LONG(kHostWidth * dpi / 96), LONG(kHostHeight * dpi / 96) };
    if (GetConfig().windowWidth != 0)
        rect = { 0, 0, LONG(GetConfig().windowWidth), LONG(GetConfig().windowHeight) };
    AdjustWindowRect(&rect, style, FALSE);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Crystal Defenders", style, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, windowClass.hInstance, nullptr);
    // CD_RESUME: relaunched from the settings menu. Take the place and state
    // the previous copy's window had.
    long left, top, right, bottom;
    int maximised, fullscreen;
    const char* resume = getenv("CD_RESUME");
    if (resume != nullptr && sscanf(resume, "%ld,%ld,%ld,%ld,%d,%d", &left, &top, &right, &bottom, &maximised, &fullscreen) == 6)
    {
        WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
        placement.showCmd = maximised ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        placement.rcNormalPosition = { left, top, right, bottom };
        SetWindowPlacement(window, &placement);
        if (fullscreen)
            ToggleFullscreen(window);
    }
    else
    {
        ShowWindow(window, SW_SHOW);
        if (GetConfig().fullscreen)
            ToggleFullscreen(window);
    }
    g_window = window;
    std::thread(WatchMenuButtons, window).detach();
    g_lastMouseMove = GetTickCount64();
    SetTimer(window, 1, 500, nullptr);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static uint16_t KeyboardButtons();

// XInput is loaded on demand so the game still runs without it.
static void* XInputFunction(const char* name)
{
    static const HMODULE module = []() -> HMODULE
    {
        for (const wchar_t* library : { L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll" })
        {
            if (HMODULE loaded = LoadLibraryW(library))
                return loaded;
        }
        return nullptr;
    }();
    return module ? (void*)GetProcAddress(module, name) : nullptr;
}

void RendererRumble(uint16_t left, uint16_t right)
{
    struct XInputVibration
    {
        WORD left, right;
    };
    using SetStateFunction = DWORD(WINAPI*)(DWORD, XInputVibration*);
    static const auto setState = (SetStateFunction)XInputFunction("XInputSetState");
    if (setState == nullptr || !GetConfig().vibration)
        return;

    // Only talk to the controllers when the motors change.
    static std::atomic<uint32_t> current;
    uint32_t wanted = (uint32_t(left) << 16) | right;
    if (current.exchange(wanted) == wanted)
        return;
    XInputVibration vibration{ left, right };
    for (DWORD slot = 0; slot < 4; slot++)
        setState(slot, &vibration);
}

// ---- Settings menu ----

struct XInputButtons
{
    DWORD packetNumber;
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT leftX, leftY, rightX, rightY;
};

// Buttons of every controller, with the left stick counted as the d-pad.
static uint16_t AllPadButtons()
{
    using GetStateFunction = DWORD(WINAPI*)(DWORD, XInputButtons*);
    static const auto getState = (GetStateFunction)XInputFunction("XInputGetState");
    uint16_t buttons = 0;
    for (DWORD slot = 0; getState != nullptr && slot < 4; slot++)
    {
        XInputButtons state{};
        if (getState(slot, &state) != ERROR_SUCCESS)
            continue;
        buttons |= state.buttons;
        buttons |= state.leftY > 16000 ? 0x0001 : state.leftY < -16000 ? 0x0002 : 0;
        buttons |= state.leftX < -16000 ? 0x0004 : state.leftX > 16000 ? 0x0008 : 0;
    }
    return buttons;
}

// Pressing both sticks opens the menu.
static void WatchMenuButtons(HWND window)
{
    bool held = false;
    for (;;)
    {
        Sleep(30);
        bool both = GetForegroundWindow() == window && (AllPadButtons() & 0x00C0) == 0x00C0;
        if (both && !held)
            PostMessageW(window, kMenuMessage, 0, 0);
        held = both;
    }
}

HostPad RendererPad()
{
    HostPad pad;
#ifdef CD_DARKLAUNCHER
    // DarkLauncher reads the keyboard and the controllers.
    pad.buttons = DlBridgeButtons();
    return pad;
#endif
    pad.buttons = KeyboardButtons();

    struct XInputState
    {
        DWORD packetNumber;
        WORD buttons;
        BYTE leftTrigger, rightTrigger;
        SHORT leftX, leftY, rightX, rightY;
    };
    using GetStateFunction = DWORD(WINAPI*)(DWORD, XInputState*);
    static const auto getState = (GetStateFunction)XInputFunction("XInputGetState");

    HWND window = g_window;
    if (g_menuOpen)
        return HostPad{};

    // A button that chose something in the menu must not reach the game too.
    auto released = [](const HostPad& state)
    {
        if (!g_waitForRelease)
            return state;
        if (state.buttons == 0)
            g_waitForRelease = false;
        return HostPad{};
    };
    if (getState == nullptr || window == nullptr || GetForegroundWindow() != window)
        return released(pad);

    // Every connected controller drives player one: buttons are combined, and
    // each axis takes whichever controller pushes it furthest.
    auto furthest = [](int current, int candidate) { return abs(candidate) > abs(current) ? candidate : current; };
    for (DWORD slot = 0; slot < 4; slot++)
    {
        XInputState state{};
        if (getState(slot, &state) != ERROR_SUCCESS)
            continue;
        pad.buttons |= state.buttons;
        pad.leftTrigger = state.leftTrigger > pad.leftTrigger ? state.leftTrigger : pad.leftTrigger;
        pad.rightTrigger = state.rightTrigger > pad.rightTrigger ? state.rightTrigger : pad.rightTrigger;
        pad.leftX = int16_t(furthest(pad.leftX, state.leftX));
        pad.leftY = int16_t(furthest(pad.leftY, state.leftY));
        pad.rightX = int16_t(furthest(pad.rightX, state.rightX));
        pad.rightY = int16_t(furthest(pad.rightY, state.rightY));
    }
    return released(pad);
}

static std::atomic<uint32_t> g_autoPadStep; // number of CD_AUTOPAD taps started so far

static uint16_t KeyboardButtons()
{
    // CD_AUTOPAD=<hex buttons>,<hex buttons>,...: every two seconds, tap the next
    // entry for 80 ms (the last one repeats), to walk through menus unattended
    // when testing. CD_AUTOPAD_START=<seconds> delays the first tap.
    // CD_AUTOPAD_SHOTS=1 saves the frame shown just before each tap as
    // frame_<tap number>.bmp.
    static const char* autoPad = getenv("CD_AUTOPAD");
    if (autoPad != nullptr)
    {
        static const ULONGLONG start = GetTickCount64() +
            (getenv("CD_AUTOPAD_START") ? strtoul(getenv("CD_AUTOPAD_START"), nullptr, 10) * 1000ull : 0);
        ULONGLONG now = GetTickCount64();
        if (now < start)
            return 0;
        ULONGLONG step = (now - start) / 2000;
        g_autoPadStep = uint32_t(step) + 1;
        if ((now - start) % 2000 >= 80)
            return 0;

        const char* entry = autoPad;
        for (ULONGLONG i = 0; i < step; i++)
        {
            const char* comma = strchr(entry, ',');
            if (comma == nullptr)
                break;
            entry = comma + 1;
        }
        return uint16_t(strtoul(entry, nullptr, 16));
    }

    HWND window = g_window;
    if (window == nullptr || GetForegroundWindow() != window || g_menuOpen)
        return 0;

    // Bindings come from crystal_defenders.ini. Enter does not count while Alt
    // is held: that is the full screen shortcut.
    bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
    uint16_t buttons = 0;
    for (auto& binding : GetConfig().keys)
    {
        if (alt && binding.key == VK_RETURN)
            continue;
        if (GetAsyncKeyState(binding.key) & 0x8000)
            buttons |= binding.button;
    }
    return buttons;
}

// ---- Device ----

static ID3D11ShaderResourceView* g_frameResource; // the game's frame, for scaling to the window
static ID3D11RenderTargetView* g_backBufferView;
static uint32_t g_backWidth;
static uint32_t g_backHeight;

static void CreateBackBufferView()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)))
        return;
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    g_backWidth = desc.Width;
    g_backHeight = desc.Height;
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_backBufferView);
    backBuffer->Release();
}

// The frame the game draws into, and its depth buffer, at a multiple of the
// console's 1280x720.
static void CreateFrameTargets(uint32_t halves)
{
    static ID3D11Texture2D* depthTexture;
    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    for (IUnknown* old : { (IUnknown*)g_frameResource, (IUnknown*)g_frameView, (IUnknown*)g_frameTexture, (IUnknown*)g_depthView,
             (IUnknown*)depthTexture })
    {
        if (old != nullptr)
            old->Release();
    }

    g_halves = halves;
    D3D11_TEXTURE2D_DESC frameDesc{};
    frameDesc.Width = FrameWidth();
    frameDesc.Height = FrameHeight();
    frameDesc.MipLevels = 1;
    frameDesc.ArraySize = 1;
    frameDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    frameDesc.SampleDesc.Count = 1;
    frameDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    g_device->CreateTexture2D(&frameDesc, nullptr, &g_frameTexture);
    g_device->CreateRenderTargetView(g_frameTexture, nullptr, &g_frameView);
    g_device->CreateShaderResourceView(g_frameTexture, nullptr, &g_frameResource);

    D3D11_TEXTURE2D_DESC depthDesc = frameDesc;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    g_device->CreateTexture2D(&depthDesc, nullptr, &depthTexture);
    g_device->CreateDepthStencilView(depthTexture, nullptr, &g_depthView);

    D3D11_VIEWPORT viewport{ 0, 0, float(frameDesc.Width), float(frameDesc.Height), 0, 1 };
    g_context->OMSetRenderTargets(1, &g_frameView, g_depthView);
    g_context->RSSetViewports(1, &viewport);
    g_context->ClearRenderTargetView(g_frameView, g_clearColor);
    g_context->ClearDepthStencilView(g_depthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

using D3DCompileFunction = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

bool RendererInit()
{
#ifdef CD_DARKLAUNCHER
    // DarkLauncher has the window: the game draws off screen, and each picture is read back for it.
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr,
        &g_context);
    if (FAILED(result))
    {
        Log("Renderer: D3D11CreateDevice failed (%08lX)", result);
        return false;
    }
    CreateFrameTargets(DlBridgeHalves());
    Log("Renderer: off screen for DarkLauncher, drawing at %ux%u", FrameWidth(), FrameHeight());
#else
    std::thread(WindowThread).detach();
    while (g_window.load() == nullptr)
        Sleep(1);

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferCount = 1;
    swapDesc.BufferDesc.Width = kHostWidth;
    swapDesc.BufferDesc.Height = kHostHeight;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = g_window;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &swapDesc, &g_swapChain, &g_device, nullptr, &g_context);
    if (FAILED(result))
    {
        Log("Renderer: D3D11CreateDeviceAndSwapChain failed (%08lX)", result);
        return false;
    }

    CreateFrameTargets(2);

    // The window handles Alt+Enter itself (borderless), not DXGI.
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_swapChain->GetParent(__uuidof(IDXGIFactory), (void**)&factory)))
    {
        factory->MakeWindowAssociation(g_window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        factory->Release();
    }
    CreateBackBufferView();
#endif

    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = compiler ? (D3DCompileFunction)(void*)GetProcAddress(compiler, "D3DCompile") : nullptr;
    if (compile == nullptr)
    {
        Log("Renderer: d3dcompiler_47.dll is not available");
        return false;
    }

    ID3DBlob* vertexCode = nullptr;
    ID3DBlob* pixelCode = nullptr;
    ID3DBlob* sharpCode = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(compile(kShaderSource, sizeof(kShaderSource) - 1, "host", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vertexCode, &errors)) ||
        FAILED(compile(kShaderSource, sizeof(kShaderSource) - 1, "host", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &pixelCode, &errors)) ||
        FAILED(compile(kShaderSource, sizeof(kShaderSource) - 1, "host", nullptr, nullptr, "ps_sharp", "ps_4_0", 0, 0, &sharpCode, &errors)))
    {
        Log("Renderer: shader compilation failed: %s", errors ? (const char*)errors->GetBufferPointer() : "?");
        return false;
    }

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11InputLayout* layout = nullptr;
    g_device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader);
    g_device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &g_pixelShaders[0]);
    g_device->CreatePixelShader(sharpCode->GetBufferPointer(), sharpCode->GetBufferSize(), nullptr, &g_pixelShaders[1]);
    const D3D11_INPUT_ELEMENT_DESC elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(elements, 3, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &layout);

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = kMaxVertices * sizeof(HostVertex);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&bufferDesc, nullptr, &g_vertexBuffer);

    for (int i = 0; i < 2; i++)
    {
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = i == 0 ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        g_device->CreateSamplerState(&samplerDesc, &g_samplers[i]);
    }

    // One rasteriser state per culling mode the game can ask for (see HostDraw).
    for (uint32_t cull = 0; cull < 8; cull++)
    {
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = (cull & 3) == 1 ? D3D11_CULL_FRONT : (cull & 3) == 2 ? D3D11_CULL_BACK : D3D11_CULL_NONE;
        rasterDesc.FrontCounterClockwise = !(cull & 4);
        rasterDesc.ScissorEnable = TRUE;
        rasterDesc.DepthClipEnable = FALSE;
        g_device->CreateRasterizerState(&rasterDesc, &g_rasterStates[cull]);
    }

    const uint32_t white = 0xFFFFFFFF;
    D3D11_TEXTURE2D_DESC whiteDesc{};
    whiteDesc.Width = whiteDesc.Height = 1;
    whiteDesc.MipLevels = whiteDesc.ArraySize = 1;
    whiteDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    whiteDesc.SampleDesc.Count = 1;
    whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA whiteData{ &white, 4, 4 };
    ID3D11Texture2D* whiteTexture = nullptr;
    g_device->CreateTexture2D(&whiteDesc, &whiteData, &whiteTexture);
    g_device->CreateShaderResourceView(whiteTexture, nullptr, &g_whiteTexture);

    // State that never changes.
    UINT stride = sizeof(HostVertex);
    UINT offset = 0;
    g_context->IASetInputLayout(layout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
    g_context->VSSetShader(vertexShader, nullptr, 0);
    g_context->RSSetState(g_rasterStates[0]);
    Log("Renderer: Direct3D 11 device and %ux%u window ready", kHostWidth, kHostHeight);
    return true;
}

static void FlushBatch();

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

static ID3D11ShaderResourceView* GetTexture(const HostTexture& info)
{
    constexpr uint32_t kFormat8888 = 6;
    if (!info.present)
        return g_whiteTexture;
    if (info.format != kFormat8888 || info.width == 0 || info.height == 0 || info.width > 8192 || info.height > 8192)
    {
        static std::atomic<uint32_t> warned;
        if (warned.fetch_add(1) < 8)
            Log("Renderer: unsupported texture format %u (%ux%u at %08X)", info.format, info.width, info.height, info.address);
        return g_whiteTexture;
    }

    // A texture is checked for changes at most once per frame: text draws one
    // quad per glyph from the same large atlas.
    uint64_t key = (uint64_t(info.address) << 32) | (info.width << 16) | info.height;
    CachedTexture& cached = g_textures[key];
    if (cached.view != nullptr && cached.checkedFrame == g_frameNumber)
        return cached.view;
    cached.checkedFrame = g_frameNumber;

    // Cheap change detection: hash a sparse sample of the guest texels.
    uint32_t storedHeight = info.tiled ? (info.height + 31) & ~31u : info.height;
    uint32_t byteSize = ((info.pitch + 31) & ~31u) * storedHeight * 4;
    uint64_t contentHash = 0xCBF29CE484222325ull;
    uint32_t step = byteSize / 4 > 4096 ? (byteSize / 4 / 4096) * 4 : 4;
    for (uint32_t i = 0; i < byteSize; i += step)
        contentHash = (contentHash ^ *(const uint32_t*)(g_base + info.address + i)) * 0x100000001B3ull;

    if (cached.view != nullptr && cached.contentHash == contentHash)
        return cached.view;

    static uint32_t uploads;
    if (++uploads % 500 == 0)
        Log("Renderer: %u texture uploads so far (latest %ux%u at %08X, %s)", uploads, info.width, info.height, info.address,
            cached.view != nullptr ? "content changed" : "new");

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

    // Pending draws may still sample the old contents of this texture.
    FlushBatch();

    if (cached.view == nullptr)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = info.width;
        desc.Height = info.height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        g_device->CreateTexture2D(&desc, nullptr, &cached.texture);
        g_device->CreateShaderResourceView(cached.texture, nullptr, &cached.view);
    }
    g_context->UpdateSubresource(cached.texture, 0, nullptr, pixels.data(), info.width * 4, 0);
    cached.contentHash = contentHash;
    return cached.view;
}

// ---- Blending ----

static D3D11_BLEND TranslateBlend(uint32_t factor, bool alpha)
{
    switch (factor)
    {
    case 0: return D3D11_BLEND_ZERO;
    case 1: return D3D11_BLEND_ONE;
    case 4: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
    case 5: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
    case 6: return D3D11_BLEND_SRC_ALPHA;
    case 7: return D3D11_BLEND_INV_SRC_ALPHA;
    case 8: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
    case 9: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
    case 10: return D3D11_BLEND_DEST_ALPHA;
    case 11: return D3D11_BLEND_INV_DEST_ALPHA;
    case 16: return alpha ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA_SAT;
    default: return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND_OP TranslateBlendOp(uint32_t op)
{
    switch (op)
    {
    case 1: return D3D11_BLEND_OP_SUBTRACT;
    case 2: return D3D11_BLEND_OP_MIN;
    case 3: return D3D11_BLEND_OP_MAX;
    case 4: return D3D11_BLEND_OP_REV_SUBTRACT;
    default: return D3D11_BLEND_OP_ADD;
    }
}

static ID3D11BlendState* GetBlendState(const HostBlend& blend)
{
    uint64_t key = (uint64_t(blend.enabled) << 40) | (uint64_t(blend.writeMask & 0xF) << 32) | blend.control;
    ID3D11BlendState*& state = g_blendStates[key];
    if (state == nullptr)
    {
        D3D11_BLEND_DESC desc{};
        auto& target = desc.RenderTarget[0];
        target.BlendEnable = blend.enabled;
        target.SrcBlend = TranslateBlend(blend.control & 0x1F, false);
        target.BlendOp = TranslateBlendOp((blend.control >> 5) & 7);
        target.DestBlend = TranslateBlend((blend.control >> 8) & 0x1F, false);
        target.SrcBlendAlpha = TranslateBlend((blend.control >> 16) & 0x1F, true);
        target.BlendOpAlpha = TranslateBlendOp((blend.control >> 21) & 7);
        target.DestBlendAlpha = TranslateBlend((blend.control >> 24) & 0x1F, true);
        target.RenderTargetWriteMask = UINT8(blend.writeMask & 0xF);
        g_device->CreateBlendState(&desc, &state);
    }
    return state;
}

static ID3D11DepthStencilState* GetDepthState(const HostDepth& depth)
{
    uint32_t key = (uint32_t(depth.test) << 4) | (uint32_t(depth.write) << 3) | (depth.function & 7);
    ID3D11DepthStencilState*& state = g_depthStates[key];
    if (state == nullptr)
    {
        // Xenos and Direct3D 11 number the compare functions alike, one apart.
        D3D11_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = depth.test;
        desc.DepthWriteMask = depth.write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3D11_COMPARISON_FUNC((depth.function & 7) + 1);
        g_device->CreateDepthStencilState(&desc, &state);
    }
    return state;
}

// ---- Drawing ----

// Consecutive draws sharing the same state (a run of glyphs, a tile map) are
// accumulated and sent as one draw call.
struct Batch
{
    std::vector<HostVertex> vertices;
    ID3D11ShaderResourceView* texture{};
    ID3D11BlendState* blend{};
    ID3D11DepthStencilState* depth{};
    int sampler{};
    bool sharp{};
    uint32_t cull{};
    int scissor[4]{};
};
static Batch g_batch;
static uint32_t g_vertexCursor; // first free vertex in g_vertexBuffer

static void FlushBatch()
{
    uint32_t count = uint32_t(g_batch.vertices.size());
    if (count == 0)
        return;

    // Append behind earlier draws; start over only when the buffer is full.
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (g_vertexCursor + count > kMaxVertices)
    {
        mode = D3D11_MAP_WRITE_DISCARD;
        g_vertexCursor = 0;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_context->Map(g_vertexBuffer, 0, mode, 0, &mapped)))
    {
        memcpy((HostVertex*)mapped.pData + g_vertexCursor, g_batch.vertices.data(), count * sizeof(HostVertex));
        g_context->Unmap(g_vertexBuffer, 0);

        D3D11_RECT scissor{ g_batch.scissor[0], g_batch.scissor[1], g_batch.scissor[2], g_batch.scissor[3] };
        g_context->PSSetShaderResources(0, 1, &g_batch.texture);
        g_context->PSSetShader(g_pixelShaders[g_batch.sharp], nullptr, 0);
        g_context->RSSetState(g_rasterStates[g_batch.cull & 7]);
        g_context->PSSetSamplers(0, 1, &g_samplers[g_batch.sampler]);
        g_context->RSSetScissorRects(1, &scissor);
        g_context->OMSetBlendState(g_batch.blend, nullptr, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_batch.depth, 0);
        g_context->Draw(count, g_vertexCursor);
        g_vertexCursor += count;
    }
    g_batch.vertices.clear();
}

void RendererDraw(const HostDraw& draw)
{
    if (g_context == nullptr || draw.vertices.empty() || draw.vertices.size() > kMaxVertices)
        return;

    ID3D11ShaderResourceView* texture = GetTexture(draw.texture);
    ID3D11BlendState* blend = GetBlendState(draw.blend);
    ID3D11DepthStencilState* depth = GetDepthState(draw.depth);
    int sampler = draw.texture.linear ? 1 : 0;
    // Only textures the game wants smoothed need the sharp filter; the others
    // are already sampled to the nearest texel.
    bool sharp = g_halves > 2 && draw.texture.present && draw.texture.linear;
    int scissor[4];
    for (int i = 0; i < 4; i++)
        scissor[i] = (draw.scissor[i] * int(g_halves) + 1) / 2; // the far edges round up at 1.5 times

    bool sameState = texture == g_batch.texture && blend == g_batch.blend && depth == g_batch.depth && sampler == g_batch.sampler &&
        sharp == g_batch.sharp && draw.cull == g_batch.cull && memcmp(scissor, g_batch.scissor, sizeof(scissor)) == 0;
    if (!sameState || g_batch.vertices.size() + draw.vertices.size() > kMaxVertices)
    {
        FlushBatch();
        g_batch.texture = texture;
        g_batch.blend = blend;
        g_batch.depth = depth;
        g_batch.sampler = sampler;
        g_batch.sharp = sharp;
        g_batch.cull = draw.cull;
        memcpy(g_batch.scissor, scissor, sizeof(scissor));
    }
    g_batch.vertices.insert(g_batch.vertices.end(), draw.vertices.begin(), draw.vertices.end());
}

// Writes the current frame as a 32-bit BMP.
static void SaveFrame(const char* path)
{
    D3D11_TEXTURE2D_DESC desc{};
    g_frameTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging)))
        return;
    g_context->CopyResource(staging, g_frameTexture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        if (FILE* file = fopen(path, "wb"))
        {
            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};
            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
            fileHeader.bfSize = fileHeader.bfOffBits + desc.Width * desc.Height * 4;
            infoHeader.biSize = sizeof(infoHeader);
            infoHeader.biWidth = LONG(desc.Width);
            infoHeader.biHeight = -LONG(desc.Height); // top-down
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = 32;
            fwrite(&fileHeader, sizeof(fileHeader), 1, file);
            fwrite(&infoHeader, sizeof(infoHeader), 1, file);
            for (uint32_t y = 0; y < desc.Height; y++)
                fwrite((const uint8_t*)mapped.pData + size_t(y) * mapped.RowPitch, 4, desc.Width, file);
            fclose(file);
            Log("Renderer: frame saved to %s", path);
        }
        g_context->Unmap(staging, 0);
    }
    staging->Release();
}

// ---- Settings menu ----
// A panel in the style of the game's own dialogs, drawn over its image and
// worked like them: up and down to move, A to choose, B to go back. The game is
// told that system UI is open, as when the console's guide comes up, so it
// pauses meanwhile.

static const struct { uint32_t id; const char* code; const wchar_t* name; } kLanguages[] =
{
    { 0, "auto", nullptr }, { 4, "fr", L"Français" }, { 1, "en", L"English" }, { 3, "de", L"Deutsch" },
    { 5, "es", L"Español" }, { 6, "it", L"Italiano" }, { 2, "ja", L"日本語" },
};

enum MenuPage { kPageMain, kPageLanguage, kPageConfirmRestart, kPageConfirmQuit, kPageResolution };

// Sizes offered: the usual 16:9 ones and the Steam Deck's 1280x800 (where the
// 16:9 image gets thin black bars above and below), as far as they fit the
// monitor the window is on, then that monitor's own size, which means full
// screen.
static std::vector<SIZE> MenuResolutions()
{
    MONITORINFO monitor{ sizeof(MONITORINFO) };
    GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);
    LONG screenWidth = monitor.rcMonitor.right - monitor.rcMonitor.left;
    LONG screenHeight = monitor.rcMonitor.bottom - monitor.rcMonitor.top;

    std::vector<SIZE> sizes;
    static const SIZE usual[] =
    {
        { 1280, 720 }, { 1280, 800 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3200, 1800 }, { 3840, 2160 }, { 5120, 2880 },
    };
    for (const SIZE& size : usual)
    {
        if (size.cx < screenWidth && size.cy < screenHeight)
            sizes.push_back(size);
    }
    if (screenWidth > 0 && screenHeight > 0)
        sizes.push_back({ screenWidth, screenHeight });
    return sizes;
}

static std::wstring MenuSizeText(LONG width, LONG height)
{
    return std::to_wstring(width) + L" \u00D7 " + std::to_wstring(height);
}

struct MenuItem
{
    std::wstring label;
    std::wstring value;
};

static struct
{
    int page{};
    int cursor{};
    int language{};       // index in kLanguages waiting for confirmation
    uint16_t previous{};  // buttons held on the previous frame
    ID3D11ShaderResourceView* view{};
    uint32_t width{};     // of the drawn panel, in window pixels
    uint32_t height{};
    float drawnScale{};
} g_menu;

static std::wstring MenuText(const wchar_t* french, const wchar_t* english)
{
    return Translate(french, english);
}

static std::vector<MenuItem> MenuItems(std::wstring& title, std::wstring& message)
{
    const Config& config = GetConfig();
    auto onOff = [](bool on) { return on ? MenuText(L"Oui", L"On") : MenuText(L"Non", L"Off"); };
    const std::wstring automatic = MenuText(L"Automatique", L"Automatic");

    std::vector<MenuItem> items;
    if (!g_menuOpen)
    {
        // Not the menu: the notice shown while the game waits in the background.
        title = MenuText(L"Pause", L"Pause");
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
        const std::vector<SIZE> sizes = MenuResolutions();
        for (size_t i = 0; i < sizes.size(); i++)
        {
            bool current = uint32_t(sizes[i].cx) == g_backWidth && uint32_t(sizes[i].cy) == g_backHeight;
            bool last = i + 1 == sizes.size();
            items.push_back({ MenuSizeText(sizes[i].cx, sizes[i].cy) + (last ? MenuText(L"  (plein écran)", L"  (full screen)") : L""),
                current ? L"\u2713" : L"" });
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
        items.push_back({ MenuText(L"Résolution", L"Resolution"), MenuSizeText(LONG(g_backWidth), LONG(g_backHeight)) });
        items.push_back({ MenuText(L"Rendu", L"Rendering"), config.sharp ? MenuText(L"Net", L"Sharp") : MenuText(L"Lisse", L"Smooth") });
        items.push_back({ MenuText(L"Plein écran", L"Full screen"), onOff(g_fullscreen) });
        items.push_back({ MenuText(L"Volume", L"Volume"), std::to_wstring(config.volume) + L" %" });
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
// starting again. The new copy takes over this window's place and state and
// runs through the opening logos unseen (see g_fastStartFrames), so the change
// looks like a short pause rather than the game closing and opening.
static void RestartGame()
{
    HWND window = g_window;
    WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
    if (g_fullscreen)
        placement = g_windowedPlacement;
    else
        GetWindowPlacement(window, &placement);

    char resume[128];
    snprintf(resume, sizeof(resume), "%ld,%ld,%ld,%ld,%d,%d", placement.rcNormalPosition.left, placement.rcNormalPosition.top,
        placement.rcNormalPosition.right, placement.rcNormalPosition.bottom, int(placement.showCmd == SW_SHOWMAXIMIZED), int(g_fullscreen));
    SetEnvironmentVariableA("CD_RESUME", resume);
    SetEnvironmentVariableA("CD_WAIT_PID", std::to_string(GetCurrentProcessId()).c_str());

    std::wstring commandLine = GetCommandLineW();
    STARTUPINFOW startup{ sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        Log("Menu: could not relaunch the game (error %lu)", GetLastError());
        return;
    }
    Log("Menu: relaunching to apply the new language.");
    RendererRumble(0, 0);
    ExitProcess(0);
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
    HWND window = g_window;
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
            g_menu.page = kPageResolution;
            g_menu.cursor = 0;
            const std::vector<SIZE> sizes = MenuResolutions();
            for (int i = 0; i < int(sizes.size()); i++)
            {
                if (uint32_t(sizes[i].cx) == g_backWidth && uint32_t(sizes[i].cy) == g_backHeight)
                    g_menu.cursor = i;
            }
            break;
        }
        case 2:
            config.sharp = !config.sharp;
            ConfigSave("affichage", "rendu", config.sharp ? "net" : "lisse");
            break;
        case 3:
            PostMessageW(window, kFullscreenMessage, 0, 0); // windows belong to their own thread
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
            PostMessageW(window, WM_CLOSE, 0, 0);
        g_menu.page = kPageMain;
        g_menu.cursor = 7;
        break;

    case kPageResolution:
    {
        const std::vector<SIZE> sizes = MenuResolutions();
        if (g_menu.cursor < int(sizes.size()))
        {
            const SIZE& size = sizes[size_t(g_menu.cursor)];
            bool wholeScreen = g_menu.cursor + 1 == int(sizes.size());
            if (!wholeScreen)
            {
                config.windowWidth = uint32_t(size.cx);
                config.windowHeight = uint32_t(size.cy);
                ConfigSave("affichage", "fenetre", (std::to_string(size.cx) + "x" + std::to_string(size.cy)).c_str());
            }
            PostMessageW(window, kResizeMessage, WPARAM(size.cx), LPARAM(size.cy));
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
        g_menuDirty = true;
        g_menuOpen = true;
        XamSystemUiChanged(true);
    }
    if (!g_menuOpen)
        return;

    uint16_t buttons = 0;
    if (GetForegroundWindow() == g_window.load())
    {
        static const struct { int key; uint16_t button; } keys[] =
        {
            { VK_UP, 0x0001 }, { VK_DOWN, 0x0002 }, { VK_RETURN, 0x1000 }, { VK_SPACE, 0x1000 },
            { VK_ESCAPE, 0x2000 }, { VK_BACK, 0x2000 },
        };
        buttons = AllPadButtons();
        for (auto& key : keys)
        {
            if (GetAsyncKeyState(key.key) & 0x8000)
                buttons |= key.button;
        }
    }
    uint16_t pressed = uint16_t((buttons & ~g_menu.previous) | g_menuInjected.exchange(0));
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
    g_menuDirty = true;
}

// Draws the current page with GDI into a texture, at the size it will have on
// screen so the text stays sharp at any window size.
static void MenuRender(float scale)
{
    std::wstring title, message;
    const std::vector<MenuItem> items = MenuItems(title, message);
    auto px = [scale](float value) { return int(value * scale + 0.5f); };

    const int width = px(540);
    const int margin = px(30);
    const int row = px(40);
    const int titleHeight = px(60);
    const int hintHeight = px(46);

    HDC dc = CreateCompatibleDC(nullptr);
    // Japanese gets a face made for it rather than Segoe UI's fallback.
    const wchar_t* face = GuestLanguage() == 2 ? L"Yu Gothic UI" : L"Segoe UI";
    HFONT titleFont = CreateFontW(-px(27), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
    HFONT itemFont = CreateFontW(-px(22), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
    HFONT hintFont = CreateFontW(-px(17), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);

    int messageHeight = 0;
    if (!message.empty())
    {
        RECT measure{ 0, 0, width - margin * 2, 0 };
        SelectObject(dc, itemFont);
        messageHeight = DrawTextW(dc, message.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) + px(16);
    }
    const int height = titleHeight + messageHeight + row * int(items.size()) + px(8) + hintHeight;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr)
    {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    SetBkMode(dc, TRANSPARENT);

    auto fill = [dc](int left, int top, int right, int bottom, COLORREF color)
    {
        RECT rect{ left, top, right, bottom };
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    };
    auto shape = [dc](bool round, int left, int top, int right, int bottom, int corner, COLORREF inside, COLORREF edge, int edgeWidth)
    {
        HPEN pen = CreatePen(PS_INSIDEFRAME, edgeWidth, edge);
        HBRUSH brush = CreateSolidBrush(inside);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        if (round)
            RoundRect(dc, left, top, right, bottom, corner, corner);
        else
            Ellipse(dc, left, top, right, bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    };
    // White text with the dark drop shadow the game gives its own.
    auto write = [&](HFONT font, const std::wstring& text, RECT rect, UINT format, COLORREF color)
    {
        SelectObject(dc, font);
        RECT shadow{ rect.left + px(2), rect.top + px(2), rect.right + px(2), rect.bottom + px(2) };
        SetTextColor(dc, RGB(14, 40, 58));
        DrawTextW(dc, text.c_str(), -1, &shadow, format | DT_NOPREFIX);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
    };

    // Outside the rounded panel stays this colour, turned transparent below.
    const COLORREF transparent = RGB(255, 0, 255);
    fill(0, 0, width, height, transparent);
    shape(true, 0, 0, width, height, px(26), RGB(40, 98, 134), RGB(12, 34, 52), px(4));
    shape(true, px(5), px(5), width - px(5), height - px(5), px(20), RGB(44, 106, 144), RGB(96, 168, 200), px(1));

    write(titleFont, title, { 0, px(12), width, titleHeight }, DT_CENTER | DT_SINGLELINE, RGB(140, 236, 248));
    fill(margin, titleHeight - px(6), width - margin, titleHeight - px(4), RGB(104, 176, 206));

    int y = titleHeight + px(4);
    if (!message.empty())
    {
        write(itemFont, message, { margin, y, width - margin, y + messageHeight }, DT_WORDBREAK, RGB(240, 244, 248));
        y += messageHeight;
    }
    for (int i = 0; i < int(items.size()); i++, y += row)
    {
        bool selected = i == g_menu.cursor;
        if (selected)
        {
            shape(true, margin / 2, y + px(2), width - margin / 2, y + row - px(2), px(12), RGB(60, 134, 176), RGB(130, 200, 228), px(1));
            shape(false, margin, y + row / 2 - px(9), margin + px(18), y + row / 2 + px(9), 0, RGB(255, 150, 40), RGB(255, 228, 150), px(2));
        }
        else
        {
            fill(margin + px(5), y + row / 2 - px(4), margin + px(13), y + row / 2 + px(4), RGB(22, 56, 80));
        }
        RECT line{ margin + px(32), y, width - margin, y + row };
        write(itemFont, items[i].label, line, DT_LEFT | DT_SINGLELINE | DT_VCENTER, selected ? RGB(255, 255, 255) : RGB(214, 230, 240));
        if (!items[i].value.empty())
            write(itemFont, items[i].value, line, DT_RIGHT | DT_SINGLELINE | DT_VCENTER, RGB(255, 226, 150));
    }

    // Button hints, as at the bottom of the game's screens.
    const struct { const wchar_t* letter; std::wstring text; COLORREF color; } hints[] =
    {
        { L"B", MenuText(L"Retour", L"Back"), RGB(214, 60, 52) },
        { L"A", MenuText(L"Confirmer", L"Confirm"), RGB(92, 190, 60) },
    };
    int x = width - margin;
    const int hintTop = height - hintHeight;
    for (auto& hint : hints)
    {
        if (items.empty())
            break;
        SelectObject(dc, hintFont);
        SIZE size{};
        GetTextExtentPoint32W(dc, hint.text.c_str(), int(hint.text.size()), &size);
        write(hintFont, hint.text, { x - size.cx, hintTop, x, height - px(10) }, DT_LEFT | DT_SINGLELINE | DT_VCENTER, RGB(236, 242, 246));
        x -= size.cx + px(6) + px(24);
        int centre = hintTop + (hintHeight - px(10)) / 2;
        shape(false, x, centre - px(12), x + px(24), centre + px(12), 0, hint.color, RGB(240, 246, 240), px(2));
        SelectObject(dc, hintFont);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT letter{ x, centre - px(12), x + px(24), centre + px(12) };
        DrawTextW(dc, hint.letter, -1, &letter, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        x -= px(18);
    }

    // GDI leaves the alpha channel empty: opaque everywhere but the corners.
    GdiFlush();
    uint32_t* pixels = (uint32_t*)bits;
    for (int i = 0; i < width * height; i++)
        pixels[i] = (pixels[i] & 0x00FFFFFF) == 0x00FF00FF ? 0 : (pixels[i] | 0xFF000000);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = UINT(width);
    desc.Height = UINT(height);
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{ bits, UINT(width * 4), 0 };
    ID3D11Texture2D* texture = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture)))
    {
        if (g_menu.view != nullptr)
            g_menu.view->Release();
        g_menu.view = nullptr;
        g_device->CreateShaderResourceView(texture, nullptr, &g_menu.view);
        texture->Release();
        g_menu.width = desc.Width;
        g_menu.height = desc.Height;
        g_menu.drawnScale = scale;
    }

    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteObject(titleFont);
    DeleteObject(itemFont);
    DeleteObject(hintFont);
    DeleteDC(dc);
}

// Called while the window's back buffer is the render target: darkens the
// game's image, as its own dialogs do, and puts the panel in the middle.
static void MenuDraw(const D3D11_VIEWPORT& image)
{
    float scale = image.Height / float(kHostHeight);
    if (g_menuDirty.exchange(false) || g_menu.view == nullptr || g_menu.drawnScale != scale)
        MenuRender(scale);
    if (g_menu.view == nullptr)
        return;

    D3D11_VIEWPORT whole{ 0, 0, float(g_backWidth), float(g_backHeight), 0, 1 };
    g_context->RSSetViewports(1, &whole);

    HostBlend blend;
    blend.enabled = true;
    blend.control = 0x00010706; // source alpha, one minus source alpha
    blend.writeMask = 0xF;
    auto quad = [&](float left, float top, float right, float bottom, float r, float g, float b, float a, ID3D11ShaderResourceView* texture)
    {
        float x0 = left / g_backWidth * 2 - 1, x1 = right / g_backWidth * 2 - 1;
        float y0 = 1 - top / g_backHeight * 2, y1 = 1 - bottom / g_backHeight * 2;
        const HostVertex vertices[6] =
        {
            { x0, y0, 0.5f, 1, 0, 0, r, g, b, a }, { x1, y0, 0.5f, 1, 1, 0, r, g, b, a }, { x0, y1, 0.5f, 1, 0, 1, r, g, b, a },
            { x1, y0, 0.5f, 1, 1, 0, r, g, b, a }, { x1, y1, 0.5f, 1, 1, 1, r, g, b, a }, { x0, y1, 0.5f, 1, 0, 1, r, g, b, a },
        };
        g_batch.vertices.assign(vertices, vertices + 6);
        g_batch.texture = texture;
        g_batch.blend = GetBlendState(blend);
        g_batch.depth = GetDepthState({});
        g_batch.sampler = 0;
        g_batch.sharp = false;
        g_batch.cull = 0;
        g_batch.scissor[0] = 0;
        g_batch.scissor[1] = 0;
        g_batch.scissor[2] = int(g_backWidth);
        g_batch.scissor[3] = int(g_backHeight);
        FlushBatch();
    };

    quad(image.TopLeftX, image.TopLeftY, image.TopLeftX + image.Width, image.TopLeftY + image.Height, 0, 0, 0, 0.5f, g_whiteTexture);
    float left = floorf(image.TopLeftX + (image.Width - g_menu.width) * 0.5f);
    float top = floorf(image.TopLeftY + (image.Height - g_menu.height) * 0.5f);
    quad(left, top, left + g_menu.width, top + g_menu.height, 1, 1, 1, 1, g_menu.view);
}

// Puts the game's current frame in the window, with the settings menu or the
// pause notice over it when one is up.
static void ShowFrame()
{
    // Follow the window: resized, maximised or full screen.
    RECT client{};
    GetClientRect(g_window, &client);
    uint32_t width = uint32_t(client.right - client.left);
    uint32_t height = uint32_t(client.bottom - client.top);
    if (width != 0 && height != 0 && (width != g_backWidth || height != g_backHeight))
    {
        g_context->OMSetRenderTargets(0, nullptr, nullptr);
        if (g_backBufferView != nullptr)
        {
            g_backBufferView->Release();
            g_backBufferView = nullptr;
        }
        g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackBufferView();
        Log("Renderer: output resized to %ux%u", g_backWidth, g_backHeight);
    }

    // Draw the 1280x720 frame into the window, as large as fits while keeping
    // its 16:9 shape, with black bars for the rest.
    const bool fastStart = RendererFastStart();
    if (fastStart && g_backBufferView != nullptr)
    {
        // Relaunched: the opening logos go by unseen, behind a black window.
        const float black[4] = { 0, 0, 0, 1 };
        g_context->ClearRenderTargetView(g_backBufferView, black);
    }
    else if (g_backBufferView != nullptr && width != 0 && height != 0)
    {
        float scale = float(g_backWidth) / kHostWidth < float(g_backHeight) / kHostHeight
            ? float(g_backWidth) / kHostWidth : float(g_backHeight) / kHostHeight;
        D3D11_VIEWPORT target{};
        target.Width = kHostWidth * scale;
        target.Height = kHostHeight * scale;
        target.TopLeftX = (g_backWidth - target.Width) * 0.5f;
        target.TopLeftY = (g_backHeight - target.Height) * 0.5f;
        target.MaxDepth = 1;

        const float black[4] = { 0, 0, 0, 1 };
        g_context->OMSetRenderTargets(1, &g_backBufferView, nullptr);
        g_context->ClearRenderTargetView(g_backBufferView, black);
        g_context->RSSetViewports(1, &target);

        static const HostVertex quad[6] =
        {
            { -1, 1, 0.5f, 1, 0, 0, 1, 1, 1, 1 }, { 1, 1, 0.5f, 1, 1, 0, 1, 1, 1, 1 }, { -1, -1, 0.5f, 1, 0, 1, 1, 1, 1, 1 },
            { 1, 1, 0.5f, 1, 1, 0, 1, 1, 1, 1 }, { 1, -1, 0.5f, 1, 1, 1, 1, 1, 1, 1 }, { -1, -1, 0.5f, 1, 0, 1, 1, 1, 1, 1 },
        };
        HostBlend opaque;
        opaque.control = 0x00010001;
        opaque.writeMask = 0xF;
        g_batch.vertices.assign(quad, quad + 6);
        g_batch.texture = g_frameResource;
        g_batch.blend = GetBlendState(opaque);
        g_batch.depth = GetDepthState({});
        g_batch.sampler = 1;
        g_batch.sharp = false;
        g_batch.cull = 0;
        g_batch.scissor[0] = 0;
        g_batch.scissor[1] = 0;
        g_batch.scissor[2] = int(g_backWidth);
        g_batch.scissor[3] = int(g_backHeight);
        FlushBatch();
        if (g_menuOpen || g_backgroundPause)
            MenuDraw(target);

        // Back to drawing the game's frame. It cannot stay bound as a texture
        // while it is the render target.
        ID3D11ShaderResourceView* none = nullptr;
        D3D11_VIEWPORT frame{ 0, 0, float(FrameWidth()), float(FrameHeight()), 0, 1 };
        g_context->PSSetShaderResources(0, 1, &none);
        g_context->OMSetRenderTargets(1, &g_frameView, g_depthView);
        g_context->RSSetViewports(1, &frame);
        g_batch.texture = nullptr;
        g_batch.blend = nullptr;
        g_batch.depth = nullptr;
    }
    g_swapChain->Present(0, 0);
}

#ifdef CD_DARKLAUNCHER
// The picture just drawn goes to DarkLauncher (which waits for it); the game is held there until
// DarkLauncher asks for the next one.
static void DarkLauncherPresent()
{
    FlushBatch();
    static ID3D11Texture2D* staging;
    static uint32_t stagingHalves;
    if (staging == nullptr || stagingHalves != g_halves)
    {
        if (staging != nullptr)
            staging->Release();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = FrameWidth();
        desc.Height = FrameHeight();
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging = nullptr;
        g_device->CreateTexture2D(&desc, nullptr, &staging);
        stagingHalves = g_halves;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (staging != nullptr)
    {
        g_context->CopyResource(staging, g_frameTexture);
        if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            DlBridgePresent(mapped.pData, FrameWidth(), FrameHeight(), mapped.RowPitch);
            g_context->Unmap(staging, 0);
        }
    }

    g_context->ClearDepthStencilView(g_depthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
    uint32_t wanted = DlBridgeHalves();
    if (wanted != g_halves)
    {
        CreateFrameTargets(wanted);
        Log("Renderer: drawing at %ux%u", FrameWidth(), FrameHeight());
    }
    g_context->ClearRenderTargetView(g_frameView, g_clearColor);
}
#endif

void RendererPresent()
{
#ifdef CD_DARKLAUNCHER
    if (g_device != nullptr)
        DarkLauncherPresent();
    return;
#endif
    if (g_swapChain == nullptr)
        return;
    FlushBatch();
    MenuUpdate();

    // CD_SCREENSHOT=<frame>[,<frame>...]: save those frames as frame_<n>.bmp.
    uint32_t frameNumber = g_frameNumber++;
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
        CreateDirectoryA("captures", nullptr);
        snprintf(path, sizeof(path), "captures\\crystal_defenders_%04u%02u%02u_%02u%02u%02u.bmp", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond);
        SaveFrame(path);
    }

    ShowFrame();

    // The game does not stop for system UI, so the pause is made here: while
    // the settings menu is open, or the window is in the background if asked,
    // no new frame is taken from the game, which leaves it waiting where it is.
    // The same frame stays on screen under the menu or the pause notice.
    static const bool unattended = getenv("CD_HEADLESS") != nullptr || getenv("CD_AUTOPAD") != nullptr;
    for (;;)
    {
        bool background = GetConfig().pauseInBackground && !unattended && !RendererFastStart() &&
            GetForegroundWindow() != g_window.load();
        if (background != g_backgroundPause)
        {
            g_backgroundPause = background;
            g_menuDirty = true;
            if (background)
                RendererRumble(0, 0);
        }
        if (!g_menuOpen && !background)
            break;
        Sleep(16);
        MenuUpdate();
        ShowFrame();
    }

    // The game clears depth one screen tile at a time; the host draws the frame
    // in one go, so clear it all here.
    g_context->ClearDepthStencilView(g_depthView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Draw the next frame at the smallest multiple of 1280x720 that covers the
    // window, so nothing is enlarged afterwards (4K full screen: three times).
    uint32_t wanted = 1;
    if (GetConfig().sharp)
    {
        while (wanted < 4 && (kHostWidth * wanted < g_backWidth && kHostHeight * wanted < g_backHeight))
            wanted++;
    }
    // CD_SCALE=<n> forces the multiple, to test without a large window.
    static const char* forced = getenv("CD_SCALE");
    if (forced != nullptr && atoi(forced) >= 1 && atoi(forced) <= 4)
        wanted = uint32_t(atoi(forced));
    if (wanted * 2 != g_halves)
    {
        CreateFrameTargets(wanted * 2);
        Log("Renderer: drawing at %ux%u", FrameWidth(), FrameHeight());
    }

    // Pace to the console's 60 Hz ourselves: the driver's vertical sync does not
    // hold the window back reliably. CD_NOLIMIT=1 disables the limiter.
    static const bool unlimited = getenv("CD_NOLIMIT") != nullptr;
    if (!unlimited && !RendererFastStart())
    {
        static LARGE_INTEGER frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        static LONGLONG next;
        // CD_TURBO_AFTER=<seconds>: from then on run four times faster, to reach
        // the end of a long replay quickly when testing.
        static const char* turbo = getenv("CD_TURBO_AFTER");
        static const ULONGLONG turboStart = GetTickCount64() + (turbo ? strtoul(turbo, nullptr, 10) * 1000ull : 0);
        const LONGLONG rate = LONGLONG(GetConfig().framesPerSecond) * (turbo != nullptr && GetTickCount64() > turboStart ? 4 : 1);
        const LONGLONG period = frequency.QuadPart / rate;
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
            LONGLONG millis = remaining * 1000 / frequency.QuadPart;
            if (millis > 2)
                Sleep(DWORD(millis - 1));
            else
                SwitchToThread();
        }
    }

    // The guest clears its render target after each resolve; mirror that here.
    g_context->ClearRenderTargetView(g_frameView, g_clearColor);
}

void RendererSetClearColor(float r, float g, float b, float a)
{
    g_clearColor[0] = r;
    g_clearColor[1] = g;
    g_clearColor[2] = b;
    g_clearColor[3] = a;
}
