// The game on DarkLauncher (kit\launcher, see dl.h): its DlGame, its pages and settings, and the
// bridge between DarkLauncher's thread and the game's.
//
// DarkLauncher asks for one picture at a time (frame). The game's own code runs in a thread of its
// own and draws with Direct3D 11 into a picture off screen; when it presents one, the renderer hands
// a copy here (DlBridgePresent) and the game waits until DarkLauncher asks for the next. So the game
// goes at DarkLauncher's pace, and stops while DarkLauncher's menu is open. The sound mixer fills a
// reserve that frame() empties at the rate it is played.
#include "dl_bridge.h"

#include "achievements.h"
#include "config.h"
#include "game_state.h"
#include "guest.h"
#include "xex_header.h"

extern "C"
{
#include "dl.h"
#include "project.h" // GAME_VERSION
}

#include <windows.h>

#include <algorithm>
#include <cstdlib> // std::_Exit
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t kWidth = 1280, kHeight = 720; // the console's picture
// The sizes the game can draw at, in halves of 1280 x 720 (see DlBridgeHalves).
constexpr uint32_t kSizeHalves[] = { 2, 3, 4, 6 };
constexpr int kSizeCount = int(sizeof(kSizeHalves) / sizeof(kSizeHalves[0]));
constexpr uint32_t kMaxHalves = 6;
constexpr int kRate = 48000;

// ---- Bridge ----

std::mutex g_mutex;
std::condition_variable g_signal;
std::vector<uint32_t> g_picture;   // the last picture the game presented, 0x00RRGGBB
uint32_t g_pictureWidth, g_pictureHeight;
uint64_t g_pictures;               // pictures presented so far
uint64_t g_asked;                  // pictures DarkLauncher asked for so far
std::atomic<uint16_t> g_buttons;

std::mutex g_soundMutex;
std::deque<int16_t> g_sound;       // interleaved stereo frames waiting to be played
constexpr size_t kSoundReserve = 3072; // frames (64 ms): the mixer waits beyond this

std::mutex g_unlockMutex;
std::vector<uint32_t> g_unlocks; // achievements to pass on to DarkLauncher

void (*g_runGuest)();

// ---- Settings ----

int g_scaleSetting = 0;        // an index in kSizeHalves: 1280 x 720, 1920 x 1080, 2560 x 1440, 3840 x 2160
int g_languageSetting = 0;     // 0: Windows, then kGameLanguages
int g_liveNotice = 0, g_storageChoice = 0;
std::atomic<uint32_t> g_motors; // the game's rumble, left << 16 | right, passed on in Frame

// The game's languages: Xbox language ids and their names, each written in itself.
const struct { uint32_t id; const wchar_t* name; } kGameLanguages[] =
{
    { 4, L"Français" }, { 1, L"English" }, { 3, L"Deutsch" }, { 5, L"Español" }, { 6, L"Italiano" }, { 2, L"日本語" },
};
constexpr int kGameLanguageCount = int(sizeof(kGameLanguages) / sizeof(kGameLanguages[0]));

void ApplySettings()
{
    Config& config = MutableConfig();
    config.language = g_languageSetting > 0 && g_languageSetting <= kGameLanguageCount ? kGameLanguages[g_languageSetting - 1].id : 0;
    config.vibration = true; // DarkLauncher's Vibration switch (Controls page) decides
    config.liveWarning = g_liveNotice != 0;
    config.storageDialog = g_storageChoice != 0;
    config.volume = 100;             // DarkLauncher's Sound page sets the volume
    config.pauseInBackground = false; // and DarkLauncher pauses the game behind another window
}

// ---- Texts, in DarkLauncher's order: jp, fr, en, es, br, de, it ----

#ifdef _WIN32
const DlText t_subtitle = { L"Windows 向け非公式ネイティブ移植版", L"portage natif non officiel pour Windows", L"unofficial native port for Windows",
    L"adaptación nativa no oficial para Windows", L"versão nativa não oficial para Windows", L"inoffizielle native Portierung für Windows", L"conversione nativa non ufficiale per Windows" };
#else
// experimental: so far only run under WSL, Linux running inside Windows
const DlText t_subtitle = { L"Linux 向け非公式ネイティブ移植版（試験版）", L"portage natif non officiel pour Linux (expérimental)",
    L"unofficial native port for Linux (experimental)", L"adaptación nativa no oficial para Linux (experimental)",
    L"versão nativa não oficial para Linux (experimental)", L"inoffizielle native Portierung für Linux (experimentell)",
    L"conversione nativa non ufficiale per Linux (sperimentale)" };
#endif

const DlText t_gameLanguage = { L"ゲームの言語", L"Langue du jeu", L"Game language", L"Idioma del juego", L"Idioma do jogo", L"Spielsprache", L"Lingua del gioco" };
const DlText t_tabGameLanguage = { L"ゲームの言語", L"LANGUE DU JEU", L"GAME LANGUAGE", L"IDIOMA DEL JUEGO", L"IDIOMA DO JOGO", L"SPIELSPRACHE", L"LINGUA DEL GIOCO" };
const DlText t_hGameLanguage = { L"ゲームのテキストの言語です。変更するとゲームが再起動します。",
    L"La langue des textes du jeu. En changer relance le jeu.",
    L"The language of the game's texts. Changing it starts the game again.",
    L"El idioma de los textos del juego. Cambiarlo reinicia el juego.",
    L"O idioma dos textos do jogo. Mudá-lo reinicia o jogo.",
    L"Die Sprache der Spieltexte. Eine Änderung startet das Spiel neu.", L"La lingua dei testi del gioco. Cambiarla riavvia il gioco." };
const DlText t_hLanguageRow = { L"Enter：この言語でゲームを再起動します。セーブしていない進行は失われます。",
    L"Entrée : relance le jeu dans cette langue. Une partie non sauvegardée est perdue.",
    L"Enter: starts the game again in this language. Progress not saved is lost.",
    L"Intro: reinicia el juego en este idioma. Se pierde lo que no esté guardado.",
    L"Enter: reinicia o jogo neste idioma. O que não foi salvo é perdido.",
    L"Eingabe: startet das Spiel in dieser Sprache neu. Nicht Gespeichertes geht verloren.", L"Invio: riavvia il gioco in questa lingua. Ciò che non è salvato va perso." };
// the language the game takes from the system
#ifdef _WIN32
const DlText t_automatic = { L"自動（Windows）", L"Automatique (Windows)", L"Automatic (Windows)", L"Automático (Windows)", L"Automático (Windows)",
    L"Automatisch (Windows)", L"Automatica (Windows)" };
#else
const DlText t_automatic = { L"自動（システム）", L"Automatique (système)", L"Automatic (system)", L"Automático (sistema)", L"Automático (sistema)",
    L"Automatisch (System)", L"Automatica (sistema)" };
#endif

const DlText t_resolution = { L"内部解像度", L"Résolution interne", L"Internal resolution", L"Resolución interna", L"Resolução interna", L"Interne Auflösung", L"Risoluzione interna" };
const DlText t_hResolution = { L"ゲームが画像を描く大きさです。大きいほど大画面でくっきりしますが、重くなります。",
    L"La taille à laquelle le jeu dessine ses images : plus grande, l'image est plus nette sur un grand écran, mais plus lourde.",
    L"The size the game draws its pictures at: larger is sharper on a big screen, but heavier.",
    L"El tamaño al que el juego dibuja sus imágenes: más grande es más nítido en una pantalla grande, pero más pesado.",
    L"O tamanho em que o jogo desenha as imagens: maior fica mais nítido numa tela grande, mas mais pesado.",
    L"Die Größe, in der das Spiel seine Bilder zeichnet: größer ist auf großen Bildschirmen schärfer, aber aufwendiger.", L"La dimensione a cui il gioco disegna le immagini: più grande è più nitida su un grande schermo, ma più pesante." };

const DlText t_game = { L"ゲーム", L"Jeu", L"Game", L"Juego", L"Jogo", L"Spiel", L"Opzioni" };
const DlText t_tabGame = { L"ゲーム", L"JEU", L"GAME", L"JUEGO", L"JOGO", L"SPIEL", L"OPZIONI" };
const DlText t_hGame = { L"オフラインの本体と同じ表示に関する設定です。",
    L"Ce que le jeu affiche comme sur une console hors ligne.",
    L"What the game shows as on an offline console.",
    L"Lo que el juego muestra como en una consola sin conexión.",
    L"O que o jogo mostra como num console offline.",
    L"Was das Spiel wie auf einer Konsole ohne Verbindung zeigt.", L"Ciò che il gioco mostra come su una console non connessa." };
const DlText t_live = { L"Xbox LIVE の通知", L"Avertissement Xbox LIVE", L"Xbox LIVE notice", L"Aviso de Xbox LIVE", L"Aviso da Xbox LIVE", L"Xbox-LIVE-Hinweis", L"Avviso Xbox LIVE" };
const DlText t_hLive = { L"起動時に「Xbox LIVE に接続されていません」を表示します（オフラインの本体と同じ）。次の起動から有効です。",
    L"Affiche au démarrage « Non connecté à Xbox LIVE », comme une console hors ligne. Au prochain lancement.",
    L"Shows “Not connected to Xbox LIVE” at start, as an offline console does. From the next start.",
    L"Muestra al inicio «No conectado a Xbox LIVE», como una consola sin conexión. Desde el próximo inicio.",
    L"Mostra no início “Não conectado à Xbox LIVE”, como um console offline. A partir do próximo início.",
    L"Zeigt beim Start „Nicht mit Xbox LIVE verbunden“, wie eine Konsole ohne Verbindung. Ab dem nächsten Start.", L"Mostra all'avvio «Non connesso a Xbox LIVE», come una console non connessa. Dal prossimo avvio." };
const DlText t_storage = { L"保存先の選択", L"Choix du stockage", L"Storage choice", L"Elegir almacenamiento", L"Escolha do armazenamento", L"Speicherauswahl", L"Scelta archiviazione" };
const DlText t_hStorage = { L"本体と同じように保存先を尋ねます。オフなら saves フォルダーに直接保存します。次の起動から有効です。",
    L"Le jeu demande où enregistrer, comme sur la console ; sinon il utilise le dossier saves. Au prochain lancement.",
    L"The game asks where to save, as on the console; otherwise it uses the saves folder. From the next start.",
    L"El juego pregunta dónde guardar, como en la consola; si no, usa la carpeta saves. Desde el próximo inicio.",
    L"O jogo pergunta onde salvar, como no console; senão usa a pasta saves. A partir do próximo início.",
    L"Das Spiel fragt wie auf der Konsole nach dem Speicherort, sonst nutzt es den Ordner saves. Ab dem nächsten Start.", L"Il gioco chiede dove salvare, come sulla console; altrimenti usa la cartella saves. Dal prossimo avvio." };


// ---- The game's language: a page of actions, one per language ----

const wchar_t* LanguageMark(int value)
{
    return value == g_languageSetting ? L"✓" : L"";
}

void ChooseLanguage(int value)
{
    if (value == g_languageSetting)
        return;
    g_languageSetting = value;
    // The game reads its language once, at its start: the program starts again, with its settings
    // kept. The new copy waits for this one to end (CD_WAIT_PID), as it still holds the log.
    SetEnvironmentVariableA("CD_WAIT_PID", std::to_string(GetCurrentProcessId()).c_str());
    dl_relaunch();
}

template <int N> void ChooseLanguageN() { ChooseLanguage(N); }
template <int N> const wchar_t* MarkN(int) { return LanguageMark(N); }

// Labels of the language rows: each language in itself, the same in every language of the menu.
const DlText t_lang[] =
{
    { L"Français", L"Français", L"Français", L"Français", L"Français", L"Français", L"Français" },
    { L"English", L"English", L"English", L"English", L"English", L"English", L"English" },
    { L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch" },
    { L"Español", L"Español", L"Español", L"Español", L"Español", L"Español", L"Español" },
    { L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano" },
    { L"日本語", L"日本語", L"日本語", L"日本語", L"日本語", L"日本語", L"日本語" },
};

const DlRow kLanguageRows[] =
{
    { DL_ACTION, t_automatic, t_hLanguageRow, nullptr, 0, nullptr, MarkN<0>, ChooseLanguageN<0> },
    { DL_ACTION, t_lang[0], t_hLanguageRow, nullptr, 0, nullptr, MarkN<1>, ChooseLanguageN<1> },
    { DL_ACTION, t_lang[1], t_hLanguageRow, nullptr, 0, nullptr, MarkN<2>, ChooseLanguageN<2> },
    { DL_ACTION, t_lang[2], t_hLanguageRow, nullptr, 0, nullptr, MarkN<3>, ChooseLanguageN<3> },
    { DL_ACTION, t_lang[3], t_hLanguageRow, nullptr, 0, nullptr, MarkN<4>, ChooseLanguageN<4> },
    { DL_ACTION, t_lang[4], t_hLanguageRow, nullptr, 0, nullptr, MarkN<5>, ChooseLanguageN<5> },
    { DL_ACTION, t_lang[5], t_hLanguageRow, nullptr, 0, nullptr, MarkN<6>, ChooseLanguageN<6> },
};

// ---- Achievements: DarkLauncher's page, with the game's own names ----

// "ach<id>": an achievement's name in DarkLauncher's file of them.
std::string AchievementKey(uint32_t id)
{
    return "ach" + std::to_string(id);
}

// The game's twelve achievements, their names and how to earn them read from the player's copy of the
// game in each of its languages (Portuguese, which it does not have, in English), the points after it.
void DeclareAchievements()
{
    // DarkLauncher's languages (jp fr en es br de it) as the game's Xbox language ids
    static const uint32_t ids[DL_LANGS] = { 2, 4, 1, 5, 1, 3, 6 };
    static std::deque<std::wstring> texts; // they stay where they are while the program runs
    static std::vector<std::array<const wchar_t*, DL_LANGS>> names, descriptions;
    static std::vector<std::string> keys;
    static std::vector<DlAchievement> list;
    std::vector<AchievementEntry> byLanguage[DL_LANGS];
    for (int l = 0; l < DL_LANGS; l++)
        byLanguage[l] = AchievementsIn(ids[l]);
    size_t count = byLanguage[0].size();
    names.resize(count);
    descriptions.resize(count);
    keys.resize(count);
    list.resize(count);
    for (size_t k = 0; k < count; k++)
    {
        for (int l = 0; l < DL_LANGS; l++)
        {
            const AchievementEntry& entry = k < byLanguage[l].size() ? byLanguage[l][k] : byLanguage[0][k];
            names[k][size_t(l)] = texts.emplace_back(entry.name).c_str();
            descriptions[k][size_t(l)] = texts.emplace_back(entry.howTo + L" (" + std::to_wstring(entry.score) + L" G)").c_str();
        }
        keys[k] = AchievementKey(byLanguage[0][k].id);
        list[k] = { keys[k].c_str(), names[k].data(), descriptions[k].data() };
    }
    if (count != 0)
        dl_achievements(list.data(), int(count));
}

// ---- Picture page: the internal resolution ----

const wchar_t* ScaleText(int value)
{
    static const wchar_t* const sizes[kSizeCount] = { L"1280 \u00D7 720", L"1920 \u00D7 1080", L"2560 \u00D7 1440", L"3840 \u00D7 2160" };
    return sizes[std::clamp(value, 0, kSizeCount - 1)];
}

const DlRow kPictureRows[] =
{
    { DL_CHOICE, t_resolution, t_hResolution, &g_scaleSetting, kSizeCount - 1, nullptr, ScaleText },
};

// ---- The game's page ----

void SettingsChanged() { ApplySettings(); }

const DlRow kGameRows[] =
{
    { DL_SWITCH, t_live, t_hLive, &g_liveNotice, 1, nullptr, nullptr, SettingsChanged },
    { DL_SWITCH, t_storage, t_hStorage, &g_storageChoice, 1, nullptr, nullptr, SettingsChanged },
};

int g_languagePage;

const DlRow kLanguageLink[] =
{
    { DL_PAGE, t_gameLanguage, t_hGameLanguage, nullptr, 0, nullptr, nullptr, nullptr, 0 /* set in Setup */ },
};

void Setup()
{
    g_languagePage = dl_page(t_tabGameLanguage, kLanguageRows, int(sizeof(kLanguageRows) / sizeof(kLanguageRows[0])));
    static DlRow link[1];
    link[0] = kLanguageLink[0];
    link[0].page = g_languagePage;
    dl_add_rows(DL_PAGE_LANGS, link, 1);
    dl_add_rows(DL_PAGE_IMAGE, kPictureRows, int(sizeof(kPictureRows) / sizeof(kPictureRows[0])));

    int game = dl_page(t_tabGame, kGameRows, int(sizeof(kGameRows) / sizeof(kGameRows[0])));
    dl_section(game, 0xE7FC, t_game, t_hGame);
    DeclareAchievements();

    dl_setting("internal_size", &g_scaleSetting, 0, kSizeCount - 1, false);
    dl_setting("game_language", &g_languageSetting, 0, kGameLanguageCount, true);
    dl_setting("live_notice", &g_liveNotice, 0, 1, false);
    dl_setting("storage_choice", &g_storageChoice, 0, 1, false);
}

// ---- Theme: the settings screen after the game's art direction, drawn by DarkLauncher ----
// Nothing is taken from the game's files. The game's menus are windows of a deep petrol blue with
// rounded corners and a bright rim, the choice lit by a bar; its backdrops are luminous veils over
// dark blue; its logo is tall upright serif capitals; the texts of its menus a narrow sans serif; its
// world is one of crystals. So: that blue, the cream gold of its crystals for the values, rounded
// frames, a bar for the selection, a starry night behind (the sparkle of its crystals), an upright title,
// a crystal chime.
const DlTheme kTheme = {
    0x32617F,                     // colour: the blue of the game's windows
    0xDFD296,                     // value: the cream gold of its crystals and gold windows
    L"Constantia",                // title_font: upright serif capitals, as the logo
    L"Bahnschrift SemiCondensed", // text_font: a narrow sans serif, as the texts of its menus
    nullptr, nullptr, nullptr, nullptr,
    DL_FRAME_ROUND, DL_CURSOR_BAR, DL_SKY_STARS, DL_TITLE_FLAT, DL_BEEP_CRYSTAL,
};

// ---- Safe mode ----

// The run after a crash: the game draws at the console's own 1280 x 720, the lightest for the graphics
// card (DarkLauncher turns off its own risky options; the player's setting comes back unless changed).
void SafeMode()
{
    g_scaleSetting = 0;
}

// ---- Where the player is ----

// "W1 · Stage 2 · Wave 5", in the game's own words (its screens say Stage and Wave in every language);
// empty outside a stage. For the crash report and the status Discord shows.
static void WhereLine(char* out, int n, const char* dot)
{
    GameState state = GameStateNow();
    if (state.pack == 0)
        out[0] = 0;
    else if (state.wave == 0)
        snprintf(out, size_t(n), "W%d %s Stage %d", state.pack, dot, state.stage);
    else
        snprintf(out, size_t(n), "W%d %s Stage %d %s Wave %d", state.pack, dot, state.stage, dot, state.wave);
}

void Report(char* out, int n)
{
    WhereLine(out, n, ",");
    if (out[0] == 0)
        snprintf(out, size_t(n), "menus");
}

void Status(char* out, int n)
{
    WhereLine(out, n, "\xC2\xB7"); // empty: DarkLauncher's own "Playing" or "In the menus"
}

// ---- What is new ----

// What is new in this version (GAME_VERSION, project.h), one line per news: its title, then | and what
// it is. Shown once, the first time the version starts.
const DlText t_news = {
    L"メニューの音楽を修正|メニューを動かすたびに音楽が途切れることはなくなりました。プレビューも滑らかになりました。",
    L"Musique des menus corrigée|La musique ne se coupe plus à chaque déplacement dans les menus, et l'aperçu reste fluide.",
    L"Menu music fixed|The music no longer cuts out at every move in the menus, and the preview stays smooth.",
    L"Música de los menús corregida|La música ya no se corta con cada movimiento en los menús, y la vista previa sigue fluida.",
    L"Música dos menus corrigida|A música não corta mais a cada movimento nos menus, e a prévia continua fluida.",
    L"Menümusik korrigiert|Die Musik bricht nicht mehr bei jeder Bewegung in den Menüs ab, und die Vorschau bleibt flüssig.",
    L"Musica dei menu corretta|La musica non si interrompe più a ogni spostamento nei menu, e l'anteprima resta fluida." };

// ---- The game ----

DWORD WINAPI GuestThread(void*)
{
    g_runGuest();
    return 0;
}

bool Start()
{
    ApplySettings();
    // Achievements earned before, as the runtime records them: DarkLauncher learns them (it says
    // nothing for those it already knows).
    for (const AchievementEntry& entry : AchievementsList())
    {
        if (entry.unlocked)
            DlBridgeUnlock(entry.id);
    }
    // The same stack as the game's other threads (see ExCreateThread).
    HANDLE thread = CreateThread(nullptr, 4 * 1024 * 1024, GuestThread, nullptr, 0, nullptr);
    if (thread == nullptr)
        return false;
    CloseHandle(thread);
    return true;
}

int Frame(const DlInput* in, uint32_t* pixels, int16_t* audio, int audioMax)
{
    // DarkLauncher's buttons as the game's controller (XINPUT_GAMEPAD bits); with DL_PAD_XBOX they are
    // in the Xbox places, so its A is the controller's A.
    static const struct { uint32_t from; uint16_t to; } map[] =
    {
        { DL_UP, 0x0001 }, { DL_DOWN, 0x0002 }, { DL_LEFT, 0x0004 }, { DL_RIGHT, 0x0008 }, { DL_START, 0x0010 }, { DL_SELECT, 0x0020 },
        { DL_L, 0x0100 }, { DL_R, 0x0200 }, { DL_A, 0x1000 }, { DL_B, 0x2000 }, { DL_X, 0x4000 }, { DL_Y, 0x8000 },
    };
    uint16_t buttons = 0;
    for (auto& entry : map)
        buttons |= (in->buttons & entry.from) ? entry.to : 0;
    g_buttons = buttons;

    // Let the game make its next picture, and wait for it a little (the game is still starting, or
    // slower than the screen: the last picture is shown again).
    {
        std::unique_lock lock(g_mutex);
        uint64_t seen = g_pictures;
        g_asked++;
        g_signal.notify_all();
        bool fresh = g_signal.wait_for(lock, std::chrono::milliseconds(25), [&] { return g_pictures != seen; });
        if (!fresh)
            dl_picture_same(); // the last picture again: --bench counts the new ones apart
        if (!g_picture.empty())
        {
            memcpy(pixels, g_picture.data(), g_picture.size() * 4);
            dl_picture_size(int(g_pictureWidth), int(g_pictureHeight), 16.0 / 9.0);
            // the picture is drawn larger than the console's 720 lines: the scanlines and the CRT filter
            // follow the console's lines, not the picture's (1920 x 1080, 1.5 times, cannot be said)
            dl_picture_scale(g_pictureHeight % kHeight == 0 ? int(g_pictureHeight / kHeight) : 1);
        }
    }

    // the game's rumble, to Xbox and PlayStation controllers alike (DarkLauncher sends only changes, and
    // nothing outside the game)
    uint32_t motors = g_motors;
    dl_rumble(int(motors >> 16), int(motors & 0xFFFF));

    {
        std::lock_guard lock(g_unlockMutex);
        for (uint32_t id : g_unlocks)
            dl_unlock(AchievementKey(id).c_str()); // nothing for one already unlocked
        g_unlocks.clear();
    }

    // The sound of one picture (800 frames at 60 per second); a little more when too much waits.
    std::lock_guard lock(g_soundMutex);
    size_t waiting = g_sound.size() / 2;
    size_t count = std::min<size_t>(waiting, waiting > 2 * kSoundReserve / 3 ? 816 : 800);
    count = std::min<size_t>(count, size_t(audioMax));
    for (size_t i = 0; i < count * 2; i++)
    {
        audio[i] = g_sound.front();
        g_sound.pop_front();
    }
    return int(count);
}

} // namespace

void DlBridgePresent(const void* pixels, uint32_t width, uint32_t height, uint32_t pitch)
{
    std::unique_lock lock(g_mutex);
    g_picture.resize(size_t(width) * height);
    for (uint32_t y = 0; y < height; y++)
    {
        const uint32_t* from = (const uint32_t*)((const uint8_t*)pixels + size_t(y) * pitch);
        uint32_t* to = g_picture.data() + size_t(y) * width;
        for (uint32_t x = 0; x < width; x++)
            to[x] = from[x] & 0x00FFFFFF;
    }
    g_pictureWidth = width;
    g_pictureHeight = height;
    g_pictures++;
    uint64_t asked = g_asked;
    g_signal.notify_all();
    // Held until DarkLauncher asks for another picture: its menu pauses the game here.
    g_signal.wait(lock, [&] { return g_asked != asked; });
}

uint32_t DlBridgeHalves()
{
    return kSizeHalves[std::clamp(g_scaleSetting, 0, kSizeCount - 1)];
}

void DlBridgeRumble(uint16_t left, uint16_t right)
{
    g_motors = uint32_t(left) << 16 | right;
}

uint16_t DlBridgeButtons()
{
    return g_buttons;
}

void DlBridgeAudio(const int16_t* samples, uint32_t frames)
{
    for (;;)
    {
        {
            std::lock_guard lock(g_soundMutex);
            if (g_sound.size() / 2 < kSoundReserve)
            {
                g_sound.insert(g_sound.end(), samples, samples + frames * 2);
                return;
            }
        }
        Sleep(2);
    }
}

void DlBridgeUnlock(uint32_t id)
{
    std::lock_guard lock(g_unlockMutex);
    g_unlocks.push_back(id);
}

int DarkLauncherMain(int argc, char* argv[], void (*runGuest)())
{
    g_runGuest = runGuest;
    static DlGame game{};
    game.width = int(kWidth * kMaxHalves / 2);
    game.height = int(kHeight * kMaxHalves / 2);
    game.aspect = 16.0 / 9.0;
    game.fps = 60;
    game.audio_rate = kRate;
    game.subtitle = t_subtitle;
    game.langs = DL_LANGS_ALL; // every text above is in DarkLauncher's seven languages, Italian included
    game.start = Start;
    game.frame = Frame;
    game.setup = Setup;
    game.safe_mode = SafeMode;
    game.report = Report;
    game.status = Status;
    game.version = GAME_VERSION;
    game.news = t_news;
    // An Xbox game: its buttons in the Xbox places at first, and View held (not pressed) opens the
    // pause menu, so a short press stays the game's Back.
    game.pad_layout = DL_PAD_XBOX;
    game.pad_menu = DL_MENU_HOLD;
    game.rumble = true; // DarkLauncher's Vibration switch, and dl_rumble to every kind of controller
    // a 2009 HD game made for flat screens: the tube's look is not offered; nor DarkLauncher's fast
    // forward, which cannot speed up a game that makes each picture when asked (it has its own)
    game.hide = DL_HIDE_CRT | DL_HIDE_SCANLINES | DL_HIDE_TURBO;
    // --no-theme: DarkLauncher's own look (to compare)
    bool themed = true;
    for (int i = 1; i < argc; i++)
        themed = themed && strcmp(argv[i], "--no-theme") != 0;
    game.theme = themed ? &kTheme : nullptr;
    // CD_THEME=<frame>,<cursor>,<sky>,<title>,<beep>[,<colour hex>]: other styles for a moment, to
    // compare them (tests); the numbers are DarkLauncher's DL_FRAME_*, DL_CURSOR_*... in order.
    static DlTheme tried = kTheme;
    if (const char* styles = getenv("CD_THEME"); themed && styles != nullptr)
    {
        unsigned colour = tried.colour;
        if (sscanf(styles, "%d,%d,%d,%d,%d,%x", &tried.frame, &tried.cursor, &tried.sky, &tried.title, &tried.beep, &colour) >= 5)
        {
            tried.colour = colour;
            game.theme = &tried;
        }
    }
    int code = dl_run(&game, argc, argv);
    // The game's threads are still waiting (for a picture asked for, for sound to be played): the
    // program ends here rather than unwinding them. On Linux an ordinary exit could wait for them; on
    // Windows it freed what they still use and crashed on the way out.
    fflush(nullptr);
    std::_Exit(code);
}
