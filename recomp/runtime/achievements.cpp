// Achievements, kept locally. The game reports every achievement whose
// condition is met (XUserWriteAchievements); the console would store them in the
// profile and show a notification. Here they go to userdata\achievements.txt,
// with a small pop-up over the game window for each new one. Names and
// descriptions come from the title's own resource data inside the executable.
#include "achievements.h"
#include "content.h"
#include "guest.h"
#include "xex_header.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

struct Achievement
{
    uint32_t id{};
    uint32_t score{};
    std::wstring name;
    std::wstring description; // once unlocked
    std::wstring howTo;       // while locked
};

static std::mutex g_mutex;
static std::set<uint32_t> g_unlocked;
static bool g_loaded;

static bool French()
{
    return GuestLanguage() == 4;
}

static std::wstring Wide(const std::string& utf8)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring wide(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), length);
    return wide;
}

std::string Utf8(const std::wstring& wide)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

// ---- Title resource data (XDBF) ----

// The XEX resource table lists the block named after the title id.
static uint32_t FindTitleData()
{
    uint32_t resources = XexFindHeader(0x000002FF);
    if (resources == 0)
        return 0;
    uint32_t size = Load32(resources);
    for (uint32_t entry = resources + 4; entry + 16 <= resources + size; entry += 16)
    {
        uint32_t address = Load32(entry + 8);
        if (address != 0 && memcmp(g_base + address, "XDBF", 4) == 0)
            return address;
    }
    return 0;
}

// Returns the address and length of one XDBF entry, or 0.
static uint32_t FindEntry(uint32_t xdbf, uint16_t space, uint64_t id, uint32_t& length)
{
    uint32_t tableLength = Load32(xdbf + 8);
    uint32_t entryCount = Load32(xdbf + 12);
    uint32_t freeLength = Load32(xdbf + 16);
    uint32_t data = xdbf + 24 + tableLength * 18 + freeLength * 8;
    for (uint32_t i = 0; i < entryCount; i++)
    {
        uint32_t entry = xdbf + 24 + i * 18;
        if (Load16(entry) == space && Load64(entry + 2) == id)
        {
            length = Load32(entry + 14);
            return data + Load32(entry + 10);
        }
    }
    return 0;
}

static const std::vector<Achievement>& Achievements()
{
    static const std::vector<Achievement> list = []
    {
        std::vector<Achievement> result;
        uint32_t xdbf = FindTitleData();
        if (xdbf == 0)
            return result;

        // String table of the game's language, English if it has none.
        std::map<uint16_t, std::wstring> strings;
        uint32_t length = 0;
        uint32_t table = FindEntry(xdbf, 3, GuestLanguage(), length);
        if (table == 0)
            table = FindEntry(xdbf, 3, 1, length);
        if (table != 0 && memcmp(g_base + table, "XSTR", 4) == 0)
        {
            uint32_t count = Load16(table + 12);
            uint32_t cursor = table + 14;
            for (uint32_t i = 0; i < count && cursor + 4 <= table + length; i++)
            {
                uint16_t id = Load16(cursor);
                uint16_t size = Load16(cursor + 2);
                strings[id] = Wide(std::string((const char*)g_base + cursor + 4, size));
                cursor += 4 + size;
            }
        }

        uint32_t list = FindEntry(xdbf, 1, 0x58414348, length); // 'XACH'
        if (list != 0 && memcmp(g_base + list, "XACH", 4) == 0)
        {
            uint32_t count = Load16(list + 12);
            for (uint32_t i = 0; i < count && list + 14 + (i + 1) * 0x24 <= list + length; i++)
            {
                uint32_t entry = list + 14 + i * 0x24;
                Achievement achievement;
                achievement.id = Load16(entry);
                achievement.name = strings[Load16(entry + 2)];
                achievement.description = strings[Load16(entry + 4)];
                achievement.howTo = strings[Load16(entry + 6)];
                achievement.score = Load16(entry + 12);
                result.push_back(achievement);
            }
        }
        Log("Achievements: %zu defined by the title", result.size());
        return result;
    }();
    return list;
}

// ---- Storage ----

static std::filesystem::path StoragePath()
{
    return ContentDirectory().parent_path() / "achievements.txt";
}

static void LoadUnlocked()
{
    if (g_loaded)
        return;
    g_loaded = true;
    std::ifstream file(StoragePath());
    for (uint32_t id; file >> id;)
        g_unlocked.insert(id);
}

static void SaveUnlocked()
{
    std::error_code error;
    std::filesystem::create_directories(StoragePath().parent_path(), error);
    std::ofstream file(StoragePath(), std::ios::trunc);
    for (uint32_t id : g_unlocked)
        file << id << "\n";
}

#ifdef _WIN32
// ---- Pop-up ----

struct Toast
{
    std::wstring title;
    std::wstring body;
    HFONT titleFont;
    HFONT bodyFont;
};

static LRESULT CALLBACK ToastProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        auto* toast = (Toast*)GetWindowLongPtrW(window, GWLP_USERDATA);
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(RGB(22, 30, 46));
        HBRUSH border = CreateSolidBrush(RGB(120, 190, 90));
        FillRect(dc, &client, background);
        FrameRect(dc, &client, border);
        DeleteObject(background);
        DeleteObject(border);

        int margin = (client.bottom - client.top) / 6 < 12 ? 12 : 16;
        RECT text{ client.left + margin, client.top + margin / 2, client.right - margin, client.bottom };
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, toast->titleFont);
        SetTextColor(dc, RGB(150, 220, 110));
        text.top += DrawTextW(dc, toast->title.c_str(), -1, &text, DT_LEFT | DT_NOPREFIX) + margin / 4;
        SelectObject(dc, toast->bodyFont);
        SetTextColor(dc, RGB(240, 240, 240));
        DrawTextW(dc, toast->body.c_str(), -1, &text, DT_LEFT | DT_NOPREFIX);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_TIMER:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Shown over the bottom of the game window for a few seconds, without taking
// the keyboard focus away from the game.
static void ShowToast(std::wstring title, std::wstring body)
{
    std::thread([title = std::move(title), body = std::move(body)]
    {
        static std::mutex oneAtATime;
        std::lock_guard lock(oneAtATime);

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = ToastProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"CrystalDefendersToast";
        RegisterClassW(&windowClass); // fails harmlessly from the second time on

        HDC screen = GetDC(nullptr);
        int dpi = GetDeviceCaps(screen, LOGPIXELSY);
        Toast toast{ title, body };
        toast.titleFont = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        toast.bodyFont = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        RECT titleRect{ 0, 0, 2000, 0 };
        RECT bodyRect{ 0, 0, 2000, 0 };
        SelectObject(screen, toast.titleFont);
        DrawTextW(screen, title.c_str(), -1, &titleRect, DT_CALCRECT | DT_NOPREFIX);
        SelectObject(screen, toast.bodyFont);
        DrawTextW(screen, body.c_str(), -1, &bodyRect, DT_CALCRECT | DT_NOPREFIX);
        ReleaseDC(nullptr, screen);

        int width = (titleRect.right > bodyRect.right ? titleRect.right : bodyRect.right) + 40;
        int height = titleRect.bottom + bodyRect.bottom + 28;

        RECT area{};
        HWND game = FindWindowW(L"CrystalDefendersRecomp", nullptr);
        if (game == nullptr || IsIconic(game) || !GetWindowRect(game, &area))
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        int x = (area.left + area.right - width) / 2;
        int y = area.bottom - height - (area.bottom - area.top) / 12;

        HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED, windowClass.lpszClassName,
            L"", WS_POPUP, x, y, width, height, nullptr, nullptr, windowClass.hInstance, nullptr);
        if (window != nullptr)
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&toast);
            SetLayeredWindowAttributes(window, 0, 238, LWA_ALPHA);
            ShowWindow(window, SW_SHOWNOACTIVATE);
            SetTimer(window, 1, 6000, nullptr);
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
                DispatchMessageW(&message);
        }
        DeleteObject(toast.titleFont);
        DeleteObject(toast.bodyFont);
    }).detach();
}

#else
// Linux: no pop-up window yet; the unlock is recorded and logged.
static void ShowToast(std::wstring, std::wstring)
{
}
#endif

// ---- Entry points ----

void AchievementsUnlock(const std::vector<uint32_t>& ids)
{
    std::vector<uint32_t> fresh;
    {
        std::lock_guard lock(g_mutex);
        LoadUnlocked();
        for (uint32_t id : ids)
        {
            if (g_unlocked.insert(id).second)
                fresh.push_back(id);
        }
        if (fresh.empty())
            return;
        SaveUnlocked();
    }

    std::wstring body;
    for (uint32_t id : fresh)
    {
        std::wstring line = L"#" + std::to_wstring(id);
        for (auto& achievement : Achievements())
        {
            if (achievement.id == id)
                line = std::to_wstring(achievement.score) + L" G  -  " + achievement.name;
        }
        Log("Achievements: unlocked %u (%s)", id, Utf8(line).c_str());
        body += (body.empty() ? L"" : L"\n") + line;
    }
    std::wstring title = fresh.size() == 1 ? (French() ? L"Succès déverrouillé" : L"Achievement unlocked")
        : std::to_wstring(fresh.size()) + (French() ? L" succès déverrouillés" : L" achievements unlocked");
    ShowToast(title, body);
}

std::wstring AchievementsSummary(std::wstring& title)
{
    std::set<uint32_t> unlocked;
    {
        std::lock_guard lock(g_mutex);
        LoadUnlocked();
        unlocked = g_unlocked;
    }

    uint32_t earned = 0;
    uint32_t total = 0;
    uint32_t count = 0;
    std::wstring text;
    for (auto& achievement : Achievements())
    {
        bool done = unlocked.count(achievement.id) != 0;
        total += achievement.score;
        earned += done ? achievement.score : 0;
        count += done;
        text += std::wstring(done ? L"☑  " : L"☐  ") + achievement.name + L"  (" + std::to_wstring(achievement.score) + L" G)\n        " +
            (done ? achievement.description : achievement.howTo) + L"\n\n";
    }
    title = (French() ? L"Succès : " : L"Achievements: ") + std::to_wstring(count) + L" / " + std::to_wstring(Achievements().size()) +
        L"   -   " + std::to_wstring(earned) + L" / " + std::to_wstring(total) + L" G";
    if (text.empty())
        text = French() ? L"Aucun succès défini." : L"No achievements defined.";
    return text;
}
