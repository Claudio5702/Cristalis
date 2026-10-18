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
#include "guest.h"
#include "xex_header.h"

extern "C"
{
#include "dl.h"
}

#include <windows.h>

#include <algorithm>
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
constexpr uint32_t kMaxScale = 2;
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

int g_scaleSetting = 0;        // 0: 1280 x 720, 1: 2560 x 1440
int g_languageSetting = 0;     // 0: Windows, then kGameLanguages
int g_vibration = 1, g_liveNotice = 0, g_storageChoice = 0;

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
    config.vibration = g_vibration != 0;
    config.liveWarning = g_liveNotice != 0;
    config.storageDialog = g_storageChoice != 0;
    config.volume = 100;             // DarkLauncher's Sound page sets the volume
    config.pauseInBackground = false; // and DarkLauncher pauses the game behind another window
}

// ---- Texts, in DarkLauncher's order: jp, fr, en, es, br, de, it ----

const DlText t_subtitle = { L"Windows 向け非公式ネイティブ移植版", L"portage natif non officiel pour Windows", L"unofficial native port for Windows",
    L"adaptación nativa no oficial para Windows", L"versão nativa não oficial para Windows", L"inoffizielle native Portierung für Windows", L"conversione nativa non ufficiale per Windows" };

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
const DlText t_automatic = { L"自動（Windows）", L"Automatique (Windows)", L"Automatic (Windows)", L"Automático (Windows)", L"Automático (Windows)",
    L"Automatisch (Windows)", L"Automatica (Windows)" };

const DlText t_resolution = { L"内部解像度", L"Résolution interne", L"Internal resolution", L"Resolución interna", L"Resolução interna", L"Interne Auflösung", L"Risoluzione interna" };
const DlText t_hResolution = { L"ゲームが画像を描く大きさです。大きいほど大画面でくっきりしますが、重くなります。",
    L"La taille à laquelle le jeu dessine ses images : plus grande, l'image est plus nette sur un grand écran, mais plus lourde.",
    L"The size the game draws its pictures at: larger is sharper on a big screen, but heavier.",
    L"El tamaño al que el juego dibuja sus imágenes: más grande es más nítido en una pantalla grande, pero más pesado.",
    L"O tamanho em que o jogo desenha as imagens: maior fica mais nítido numa tela grande, mas mais pesado.",
    L"Die Größe, in der das Spiel seine Bilder zeichnet: größer ist auf großen Bildschirmen schärfer, aber aufwendiger.", L"La dimensione a cui il gioco disegna le immagini: più grande è più nitida su un grande schermo, ma più pesante." };

const DlText t_game = { L"ゲーム", L"Jeu", L"Game", L"Juego", L"Jogo", L"Spiel", L"Opzioni" };
const DlText t_tabGame = { L"ゲーム", L"JEU", L"GAME", L"JUEGO", L"JOGO", L"SPIEL", L"OPZIONI" };
const DlText t_hGame = { L"振動と、オフラインの本体と同じ表示に関する設定です。",
    L"Vibrations, et ce que le jeu affiche comme sur une console hors ligne.",
    L"Vibration, and what the game shows as on an offline console.",
    L"Vibración y lo que el juego muestra como en una consola sin conexión.",
    L"Vibração e o que o jogo mostra como num console offline.",
    L"Vibration und was das Spiel wie auf einer Konsole ohne Verbindung zeigt.", L"Vibrazione e ciò che il gioco mostra come su una console non connessa." };
const DlText t_vibration = { L"振動", L"Vibrations", L"Vibration", L"Vibración", L"Vibração", L"Vibration", L"Vibrazione" };
const DlText t_hVibration = { L"ゲームの指示でコントローラーが振動します。", L"La manette vibre quand le jeu le demande.",
    L"The controller vibrates when the game asks for it.", L"El mando vibra cuando el juego lo pide.", L"O controle vibra quando o jogo pede.",
    L"Der Controller vibriert, wenn das Spiel es verlangt.", L"Il controller vibra quando il gioco lo richiede." };
const DlText t_live = { L"Xbox LIVE の通知", L"Avertissement Xbox LIVE", L"Xbox LIVE notice", L"Aviso de Xbox LIVE", L"Aviso da Xbox LIVE", L"Xbox-LIVE-Hinweis", L"Avviso Xbox LIVE" };
const DlText t_hLive = { L"起動時に「Xbox LIVE に接続されていません」を表示します（オフラインの本体と同じ）。次の起動から有効です。",
    L"Affiche au démarrage « Non connecté à Xbox LIVE », comme une console hors ligne. Au prochain lancement.",
    L"Shows “Not connected to Xbox LIVE” at start, as an offline console does. From the next start.",
    L"Muestra al inicio «No conectado a Xbox LIVE», como una consola sin conexión. Desde el próximo inicio.",
    L"Mostra no início “Não conectado à Xbox LIVE”, como um console offline. A partir do próximo início.",
    L"Zeigt beim Start „Nicht mit Xbox LIVE verbunden“, wie eine Konsole ohne Verbindung. Ab dem nächsten Start.", L"Mostra all'avvio «Non connesso a Xbox LIVE», come una console non connessa. Dal prossimo avvio." };
const DlText t_storage = { L"保存先の選択", L"Choix du stockage", L"Storage choice", L"Elegir almacenamiento", L"Escolha do armazenamento", L"Speicherauswahl", L"Scelta archiviazione" };
const DlText t_hStorage = { L"本体と同じように保存先を尋ねます。オフなら userdata に直接保存します。次の起動から有効です。",
    L"Le jeu demande où enregistrer, comme sur la console ; sinon il utilise le dossier userdata. Au prochain lancement.",
    L"The game asks where to save, as on the console; otherwise it uses the userdata folder. From the next start.",
    L"El juego pregunta dónde guardar, como en la consola; si no, usa la carpeta userdata. Desde el próximo inicio.",
    L"O jogo pergunta onde salvar, como no console; senão usa a pasta userdata. A partir do próximo início.",
    L"Das Spiel fragt wie auf der Konsole nach dem Speicherort, sonst nutzt es den Ordner userdata. Ab dem nächsten Start.", L"Il gioco chiede dove salvare, come sulla console; altrimenti usa la cartella userdata. Dal prossimo avvio." };


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
    static const wchar_t* const sizes[] = { L"1280 × 720", L"2560 × 1440" };
    return sizes[value < 0 ? 0 : value > 1 ? 1 : value];
}

const DlRow kPictureRows[] =
{
    { DL_CHOICE, t_resolution, t_hResolution, &g_scaleSetting, int(kMaxScale) - 1, nullptr, ScaleText },
};

// ---- The game's page ----

void SettingsChanged() { ApplySettings(); }

const DlRow kGameRows[] =
{
    { DL_SWITCH, t_vibration, t_hVibration, &g_vibration, 1, nullptr, nullptr, SettingsChanged },
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

    dl_setting("internal_scale", &g_scaleSetting, 0, int(kMaxScale) - 1, false);
    dl_setting("game_language", &g_languageSetting, 0, kGameLanguageCount, true);
    dl_setting("vibration", &g_vibration, 0, 1, false);
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
        g_signal.wait_for(lock, std::chrono::milliseconds(25), [&] { return g_pictures != seen; });
        if (!g_picture.empty())
        {
            memcpy(pixels, g_picture.data(), g_picture.size() * 4);
            dl_picture_size(int(g_pictureWidth), int(g_pictureHeight), 16.0 / 9.0);
        }
    }

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

uint32_t DlBridgeScale()
{
    return uint32_t(std::clamp(g_scaleSetting, 0, int(kMaxScale) - 1)) + 1;
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
    game.width = int(kWidth * kMaxScale);
    game.height = int(kHeight * kMaxScale);
    game.aspect = 16.0 / 9.0;
    game.fps = 60;
    game.audio_rate = kRate;
    game.subtitle = t_subtitle;
    game.langs = DL_LANGS_ALL; // every text above is in DarkLauncher's seven languages, Italian included
    game.start = Start;
    game.frame = Frame;
    game.setup = Setup;
    game.safe_mode = SafeMode;
    // An Xbox game: its buttons in the Xbox places at first, and View held (not pressed) opens the
    // pause menu, so a short press stays the game's Back.
    game.pad_layout = DL_PAD_XBOX;
    game.pad_menu = DL_MENU_HOLD;
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
    return dl_run(&game, argc, argv);
}
