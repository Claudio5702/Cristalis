// xam.xex imports: user/profile, input, notifications, content licensing, and the
// XAudio render driver registration. Also the two CRT formatting imports.
#include "achievements.h"
#include "config.h"
#include "kernel_objects.h"
#include "renderer.h"
#include "xex_header.h"

#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

constexpr uint32_t X_ERROR_SUCCESS = 0;
constexpr uint32_t X_ERROR_NO_MORE_FILES = 18;
constexpr uint32_t X_ERROR_DEVICE_NOT_CONNECTED = 0x48F;
constexpr uint32_t X_ERROR_NO_SUCH_USER = 0x525;

static void StoreFloat(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, 4);
    Store32(address, bits);
}

// ---- System ----

GUEST_IMPORT(XamGetSystemVersion)
{
    ctx.r3.u64 = 0x20446700; // 2.0.17511.0
}

GUEST_IMPORT(XamGetExecutionId)
{
    Store32(ctx.r3.u32, XexFindHeader(0x00040006));
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamLoaderTerminateTitle)
{
    Log("Guest requested exit (XamLoaderTerminateTitle).");
    ExitProcess(0);
}

GUEST_IMPORT(XamLoaderLaunchTitle)
{
    Log("Guest requested exit (XamLoaderLaunchTitle).");
    ExitProcess(0);
}

GUEST_IMPORT(XamAlloc)
{
    uint32_t address = GuestAllocVirtual(ctx.r4.u32, 0x1000);
    Store32(ctx.r5.u32, address);
    ctx.r3.u64 = address ? X_ERROR_SUCCESS : 8;
}

GUEST_IMPORT(XamFree)
{
    GuestFreeVirtual(ctx.r4.u32);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// Completes an XOVERLAPPED request, or returns the result directly without one.
static uint32_t Complete(uint32_t overlapped, uint32_t result)
{
    if (overlapped == 0)
        return result;

    Store32(overlapped + 0x00, result);
    Store32(overlapped + 0x04, 0);
    Store32(overlapped + 0x18, result);
    if (KObject* event = ObjectFromHandle(Load32(overlapped + 0x0C)))
        SetEvent(event->host);
    return 997; // ERROR_IO_PENDING
}

// Each distinct service message is logged once, to know what the game asked for.
static void LogMessageOnce(const char* call, uint32_t message)
{
    static std::mutex mutex;
    static std::set<uint32_t> seen;
    std::lock_guard lock(mutex);
    if (seen.insert(message).second)
        Log("%s: message %08X", call, message);
}

GUEST_IMPORT(XMsgStartIORequest)
{
    // Xbox LIVE service requests (user context, presence, achievements, scores).
    // Nothing is online: report success so the game carries on.
    LogMessageOnce("XMsgStartIORequest", ctx.r4.u32);

    // CD_TEST_ACHIEVEMENTS=<id>,<id>: pretend the game reported these, once,
    // to see the pop-up without having to earn them.
    static const char* test = getenv("CD_TEST_ACHIEVEMENTS");
    if (test != nullptr)
    {
        std::vector<uint32_t> ids;
        for (const char* p = test; *p != 0; p += strcspn(p, ","), p += *p == ',')
            ids.push_back(uint32_t(atoi(p)));
        test = nullptr;
        AchievementsUnlock(ids);
    }

    // XUserWriteAchievements: a count and a list of (user, achievement id).
    constexpr uint32_t kWriteAchievements = 0x000B0008;
    if (ctx.r4.u32 == kWriteAchievements && ctx.r6.u32 != 0 && ctx.r7.u32 >= 8)
    {
        uint32_t count = Load32(ctx.r6.u32);
        uint32_t list = Load32(ctx.r6.u32 + 4);
        std::vector<uint32_t> ids;
        for (uint32_t i = 0; i < count && i < 64 && list != 0; i++)
            ids.push_back(Load32(list + i * 8 + 4));
        AchievementsUnlock(ids);
    }
    ctx.r3.u64 = Complete(ctx.r5.u32, X_ERROR_SUCCESS);
}

GUEST_IMPORT(XMsgInProcessCall)
{
    LogMessageOnce("XMsgInProcessCall", ctx.r4.u32);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XMsgCancelIORequest)
{
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// Sessions only exist to report scores online; a handle and an object the game
// passes back to service requests are all it needs.
GUEST_IMPORT(XamSessionCreateHandle)
{
    Store32(ctx.r3.u32, 0x5E550001);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamSessionRefObjByHandle)
{
    static const uint32_t object = GuestAllocVirtual(0x1000, 0x1000);
    Store32(ctx.r4.u32, object);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(NetDll_WSAStartup)
{
    if (ctx.r5.u32 != 0)
        memset(g_base + ctx.r5.u32, 0, 0x190);
    ctx.r3.u64 = 0;
}

// Quality-of-service probes towards Xbox LIVE servers: there is no network.
GUEST_IMPORT(NetDll_XNetQosServiceLookup)
{
    ctx.r3.u64 = 10050; // WSAENETDOWN
}

GUEST_IMPORT(NetDll_XNetQosRelease)
{
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(NetDll_WSACleanup)
{
    ctx.r3.u64 = 0;
}

// ---- Notifications ----

// System notifications are how a title learns that a profile is signed in or
// that system UI opened and closed. One queue serves every listener.
constexpr uint32_t XN_SYS_UI = 0x00000009;
constexpr uint32_t XN_SYS_SIGNINCHANGED = 0x0000000A;

struct Notification
{
    uint32_t id;
    uint32_t param;
};
static std::mutex g_notifyMutex;
static std::deque<Notification> g_notifications;
static HANDLE g_notifyEvent;
static bool g_startupNotified;

static void PostNotification(uint32_t id, uint32_t param)
{
    std::lock_guard lock(g_notifyMutex);
    g_notifications.push_back({ id, param });
    if (g_notifyEvent != nullptr)
        SetEvent(g_notifyEvent);
}

void XamSystemUiChanged(bool open)
{
    PostNotification(XN_SYS_UI, open ? 1 : 0);
}

GUEST_IMPORT(XamNotifyCreateListener)
{
    auto* object = new KObject();
    object->type = KObjectType::Event;
    object->host = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    {
        std::lock_guard lock(g_notifyMutex);
        g_notifyEvent = object->host;
    }

    // What the console sends every title at startup: UI closed, user 0 signed in.
    if (!g_startupNotified)
    {
        g_startupNotified = true;
        PostNotification(XN_SYS_UI, 0);
        PostNotification(XN_SYS_SIGNINCHANGED, 1);
    }
    ctx.r3.u64 = ObjectCreateHandle(object);
}

GUEST_IMPORT(XNotifyGetNext)
{
    uint32_t wanted = ctx.r4.u32;
    std::lock_guard lock(g_notifyMutex);
    for (auto it = g_notifications.begin(); it != g_notifications.end(); ++it)
    {
        if (wanted != 0 && it->id != wanted)
            continue;
        if (ctx.r5.u32 != 0)
            Store32(ctx.r5.u32, it->id);
        if (ctx.r6.u32 != 0)
            Store32(ctx.r6.u32, it->param);
        g_notifications.erase(it);
        if (g_notifications.empty() && g_notifyEvent != nullptr)
            ResetEvent(g_notifyEvent);
        ctx.r3.u64 = 1;
        return;
    }
    ctx.r3.u64 = 0;
}

GUEST_IMPORT(XamShowSigninUI)
{
    // There is no sign-in dialog: user 0 is always signed in. Report the UI as
    // opened and closed straight away, with the sign-in state unchanged.
    PostNotification(XN_SYS_UI, 1);
    PostNotification(XN_SYS_UI, 0);
    PostNotification(XN_SYS_SIGNINCHANGED, 1);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// System screens that have no equivalent here (achievements list, gamer card,
// marketplace): report them as opened and closed straight away.
static void ShowNothing(PPCContext& ctx, const char* what)
{
    Log("System UI requested: %s (not available, closed immediately)", what);
    PostNotification(XN_SYS_UI, 1);
    PostNotification(XN_SYS_UI, 0);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// The achievements screen of the console, as a plain Windows message box.
GUEST_IMPORT(XamShowAchievementsUI)
{
    PostNotification(XN_SYS_UI, 1);
    std::thread([]
    {
        std::wstring title;
        std::wstring text = AchievementsSummary(title);
        // CD_HEADLESS=1 (unattended tests): log the text instead of showing it.
        if (getenv("CD_HEADLESS") != nullptr)
            Log("Achievements UI: %s\n%s", Utf8(title).c_str(), Utf8(text).c_str());
        else
            MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
        PostNotification(XN_SYS_UI, 0);
    }).detach();
    ctx.r3.u64 = X_ERROR_SUCCESS;
}
GUEST_IMPORT(XamShowGamerCardUIForXUID) { ShowNothing(ctx, "gamer card"); }
GUEST_IMPORT(XamShowMarketplaceUI) { ShowNothing(ctx, "marketplace"); }

// System message box, shown as a Windows one. The game supplies up to three
// button labels; Windows cannot rename its buttons, so the labels are listed in
// the text next to the button that stands for each.
static std::wstring GuestWide(uint32_t address)
{
    std::wstring text;
    for (; address != 0 && Load16(address) != 0; address += 2)
        text += wchar_t(Load16(address));
    return text;
}

static uint32_t ShowMessageBox(uint32_t title, uint32_t text, uint32_t buttonCount, uint32_t buttons,
    uint32_t resultPointer, uint32_t overlapped)
{
    std::wstring caption = GuestWide(title);
    std::wstring message = GuestWide(text);
    std::vector<std::wstring> labels;
    for (uint32_t i = 0; i < buttonCount && i < 3 && buttons != 0; i++)
        labels.push_back(GuestWide(Load32(buttons + i * 4)));
    Log("System message box: \"%s\" / \"%s\" (%zu button(s))", Utf8(caption).c_str(), Utf8(message).c_str(), labels.size());

    auto show = [=]
    {
        static const wchar_t* const hostButtons[] = { L"Oui / Yes", L"Non / No", L"Annuler / Cancel" };
        std::wstring body = message;
        if (labels.size() >= 2)
        {
            body += L"\n";
            for (size_t i = 0; i < labels.size(); i++)
                body += std::wstring(L"\n") + hostButtons[i] + L" = " + labels[i];
        }
        UINT type = labels.size() >= 3 ? MB_YESNOCANCEL : labels.size() == 2 ? MB_YESNO : MB_OK;
        int choice = MessageBoxW(nullptr, body.c_str(), caption.empty() ? L"Crystal Defenders" : caption.c_str(),
            type | MB_TOPMOST | MB_SETFOREGROUND);
        if (resultPointer != 0)
            Store32(resultPointer, choice == IDNO ? 1 : choice == IDCANCEL ? 2 : 0);
    };

    PostNotification(XN_SYS_UI, 1);
    if (overlapped == 0)
    {
        show();
        PostNotification(XN_SYS_UI, 0);
        return X_ERROR_SUCCESS;
    }
    std::thread([=]
    {
        show();
        Complete(overlapped, X_ERROR_SUCCESS);
        PostNotification(XN_SYS_UI, 0);
    }).detach();
    return 997; // ERROR_IO_PENDING
}

// Arguments: user, title, text, button count, buttons, focused button, flags,
// result, overlapped. The extended form has one more argument before the result.
GUEST_IMPORT(XamShowMessageBoxUI)
{
    ctx.r3.u64 = ShowMessageBox(ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r10.u32, Load32(ctx.r1.u32 + 0x54));
}

GUEST_IMPORT(XamShowMessageBoxUIEx)
{
    ctx.r3.u64 = ShowMessageBox(ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, Load32(ctx.r1.u32 + 0x54),
        Load32(ctx.r1.u32 + 0x5C));
}

// ---- Users ----

GUEST_IMPORT(XamUserGetSigninState)
{
    // 1 = signed in locally, 2 = signed in to Xbox LIVE. Claiming LIVE spares the
    // "not connected" warning at every start; the service requests that follow
    // are answered as if they had succeeded, and the leaderboards come back empty.
    ctx.r3.u64 = ctx.r3.u32 == 0 ? (GetConfig().liveWarning ? 1 : 2) : 0;
}

GUEST_IMPORT(XamUserGetXUID)
{
    if (ctx.r3.u32 != 0)
    {
        ctx.r3.u64 = X_ERROR_NO_SUCH_USER;
        return;
    }
    Store64(ctx.r5.u32, GetConfig().liveWarning ? 0xE000000000000001ull : 0x0009000000000001ull); // offline / online XUID
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamUserGetName)
{
    if (ctx.r3.u32 != 0)
    {
        ctx.r3.u64 = X_ERROR_NO_SUCH_USER;
        return;
    }
    static const char name[] = "Player";
    uint32_t size = ctx.r5.u32 < sizeof(name) ? ctx.r5.u32 : uint32_t(sizeof(name));
    if (size != 0)
    {
        memcpy(g_base + ctx.r4.u32, name, size);
        g_base[ctx.r4.u32 + size - 1] = 0;
    }
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// ---- Content ----

GUEST_IMPORT(XamContentGetLicenseMask)
{
    Store32(ctx.r3.u32, 0xFFFFFFFF); // full version unlocked
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// ---- Input (no host gamepad wired yet: pad 0 present, nothing pressed) ----

GUEST_IMPORT(XamInputGetCapabilities)
{
    if (ctx.r3.u32 != 0)
    {
        ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    uint32_t caps = ctx.r5.u32;
    memset(g_base + caps, 0, 0x14);
    g_base[caps + 0] = 1;          // XINPUT_DEVTYPE_GAMEPAD
    g_base[caps + 1] = 1;          // XINPUT_DEVSUBTYPE_GAMEPAD
    Store16(caps + 4, 0xFFFF);     // buttons
    g_base[caps + 6] = 0xFF;
    g_base[caps + 7] = 0xFF;
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamInputGetState)
{
    if (ctx.r3.u32 != 0)
    {
        ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    if (ctx.r5.u32 != 0)
    {
        // X_INPUT_STATE: packet number, then buttons, triggers and sticks.
        static HostPad previous;
        static uint32_t packetNumber;
        HostPad pad = RendererPad();
        if (memcmp(&pad, &previous, sizeof(pad)) != 0)
        {
            previous = pad;
            packetNumber++;
        }
        uint32_t state = ctx.r5.u32;
        Store32(state + 0, packetNumber);
        Store16(state + 4, pad.buttons);
        g_base[state + 6] = pad.leftTrigger;
        g_base[state + 7] = pad.rightTrigger;
        Store16(state + 8, uint16_t(pad.leftX));
        Store16(state + 10, uint16_t(pad.leftY));
        Store16(state + 12, uint16_t(pad.rightX));
        Store16(state + 14, uint16_t(pad.rightY));
    }
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XamInputSetState)
{
    if (ctx.r3.u32 != 0)
    {
        ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    // X_INPUT_VIBRATION: left then right motor speed.
    if (ctx.r5.u32 != 0)
        RendererRumble(Load16(ctx.r5.u32), Load16(ctx.r5.u32 + 2));
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// ---- XAudio queries (the render driver and XMA decoder live in audio.cpp) ----

GUEST_IMPORT(XAudioGetSpeakerConfig)
{
    Store32(ctx.r3.u32, 0x00010001);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

GUEST_IMPORT(XAudioGetVoiceCategoryVolume)
{
    StoreFloat(ctx.r4.u32, 1.0f);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// ---- CRT formatting ----
// Variadic arguments are 8-byte slots; integers sit in the low 32 bits.

struct GuestArgs
{
    PPCContext* ctx{};     // register arguments first when set
    uint32_t registerIndex{};
    uint32_t stack{};      // then guest memory slots

    uint64_t Next()
    {
        if (ctx != nullptr && registerIndex < 8)
        {
            const PPCRegister* registers[] = { &ctx->r3, &ctx->r4, &ctx->r5, &ctx->r6, &ctx->r7, &ctx->r8, &ctx->r9, &ctx->r10 };
            return registers[registerIndex++]->u64;
        }
        uint64_t value = Load64(stack);
        stack += 8;
        return value;
    }
};

static std::string FormatGuest(const char* format, GuestArgs& args)
{
    std::string out;
    char buffer[512];
    for (const char* p = format; *p != 0; p++)
    {
        if (*p != '%')
        {
            out += *p;
            continue;
        }
        if (p[1] == '%')
        {
            out += '%';
            p++;
            continue;
        }

        std::string spec = "%";
        p++;
        while (*p != 0 && strchr("-+ #0", *p) != nullptr)
            spec += *p++;
        while (*p == '*' || (*p >= '0' && *p <= '9') || *p == '.')
        {
            if (*p == '*')
                spec += std::to_string(int32_t(args.Next()));
            else
                spec += *p;
            p++;
        }

        bool wide64 = false;
        bool wideChar = false;
        if (p[0] == 'I' && p[1] == '6' && p[2] == '4')
        {
            wide64 = true;
            p += 3;
        }
        while (*p == 'l' || *p == 'h' || *p == 'w')
        {
            if (p[0] == 'l' && p[1] == 'l')
            {
                wide64 = true;
                p++;
            }
            else if (*p == 'l' || *p == 'w')
            {
                wideChar = true;
            }
            p++;
        }
        if (*p == 0)
            break;

        switch (*p)
        {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c':
        {
            uint64_t value = args.Next();
            spec += "ll";
            spec += *p == 'c' ? 'c' : *p;
            if (*p == 'c')
            {
                spec.erase(spec.size() - 3, 2);
                snprintf(buffer, sizeof(buffer), spec.c_str(), int(value));
            }
            else if (wide64)
                snprintf(buffer, sizeof(buffer), spec.c_str(), (long long)value);
            else if (*p == 'd' || *p == 'i')
                snprintf(buffer, sizeof(buffer), spec.c_str(), (long long)int32_t(value));
            else
                snprintf(buffer, sizeof(buffer), spec.c_str(), (unsigned long long)uint32_t(value));
            out += buffer;
            break;
        }
        case 'p':
            snprintf(buffer, sizeof(buffer), "%08X", uint32_t(args.Next()));
            out += buffer;
            break;
        case 'e': case 'E': case 'f': case 'g': case 'G':
        {
            uint64_t bits = args.Next();
            double value;
            memcpy(&value, &bits, 8);
            spec += *p;
            snprintf(buffer, sizeof(buffer), spec.c_str(), value);
            out += buffer;
            break;
        }
        case 's': case 'S':
        {
            uint32_t address = uint32_t(args.Next());
            std::string text;
            if (address == 0)
                text = "(null)";
            else if (wideChar || *p == 'S')
            {
                for (uint32_t a = address; Load16(a) != 0; a += 2)
                    text += char(Load16(a) < 0x80 ? Load16(a) : '?');
            }
            else
                text = (const char*)g_base + address;
            spec += 's';
            snprintf(buffer, sizeof(buffer), spec.c_str(), text.c_str());
            out += strlen(buffer) + 1 < sizeof(buffer) ? std::string(buffer) : text;
            break;
        }
        default:
            out += '%';
            out += *p;
            break;
        }
    }
    return out;
}

GUEST_IMPORT(sprintf)
{
    GuestArgs args;
    args.ctx = &ctx;
    args.registerIndex = 2;
    args.stack = ctx.r1.u32 + 0x50;
    std::string text = FormatGuest((const char*)g_base + ctx.r4.u32, args);
    memcpy(g_base + ctx.r3.u32, text.c_str(), text.size() + 1);
    ctx.r3.u64 = uint32_t(text.size());
}

GUEST_IMPORT(_vsnprintf)
{
    GuestArgs args;
    args.stack = ctx.r6.u32;
    std::string text = FormatGuest((const char*)g_base + ctx.r5.u32, args);
    uint32_t capacity = ctx.r4.u32;
    if (text.size() < capacity)
    {
        memcpy(g_base + ctx.r3.u32, text.c_str(), text.size() + 1);
        ctx.r3.u64 = uint32_t(text.size());
    }
    else
    {
        memcpy(g_base + ctx.r3.u32, text.c_str(), capacity);
        ctx.r3.s64 = -1;
    }
}
