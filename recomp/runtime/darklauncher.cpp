// The game on DarkLauncher (kit\launcher, see dl.h): its DlGame, its pages and settings, and the
// bridge between DarkLauncher's thread and the game's.
//
// DarkLauncher asks for one picture at a time (frame). The game's own code runs in a thread of its
// own and draws with Direct3D 11 into a picture off screen; when it presents one, the renderer copies
// it into a texture shared with DarkLauncher (DlBridgePresentShared, dl_picture_shared), or hands the
// pixels here (DlBridgePresent) while DarkLauncher cannot show that, and the game waits until
// DarkLauncher asks for the next. So the game
// goes at DarkLauncher's pace, and stops while DarkLauncher's menu is open. The sound mixer fills a
// reserve that frame() empties at the rate it is played.
#include "dl_bridge.h"

#include "achievements.h"
#include "art.h"
#include "config.h"
#include "debug_script.h"
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
#include <map>
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
// Filling the screen (DlBridgeFill): at most half the picture's width again (2.67:1), a third of its height
// (4:3)
constexpr uint32_t kMaxExtraWidth = kWidth / 2, kMaxExtraHeight = kHeight / 3;
// the largest picture (DlGame.width and height): the largest size, with the most columns or lines added
constexpr uint32_t kMaxPictureWidth = (kWidth + kMaxExtraWidth) * kMaxHalves / 2;
constexpr uint32_t kMaxPictureHeight = (kHeight + kMaxExtraHeight) * kMaxHalves / 2;
constexpr int kRate = 48000;

// ---- Bridge ----

std::mutex g_mutex;
std::condition_variable g_signal;
std::vector<uint32_t> g_picture;   // the last picture the game presented, 0x00RRGGBB
uint32_t g_pictureWidth, g_pictureHeight;
uint64_t g_pictures;               // pictures presented so far
uint32_t g_pixelsWidth, g_pixelsHeight; // the size of g_picture
void* g_sharedHandle;              // the shared texture of the last picture, when it was shared
bool g_lastShared;                 // the last picture is in the shared texture, not in g_picture
std::atomic<void*> g_sharedGiven{ nullptr };
std::atomic<int> g_sharedPause{ 0 }; // pictures to give as pixels before sharing again
uint64_t g_asked;                  // pictures DarkLauncher asked for so far
std::atomic<uint16_t> g_buttons;

std::mutex g_soundMutex;
std::deque<int16_t> g_sound;       // interleaved stereo frames waiting to be played
std::condition_variable g_soundRoom; // DarkLauncher took some of it (the mixer may add more)
constexpr size_t kSoundReserve = 3072; // frames (64 ms): the mixer waits beyond this

std::mutex g_unlockMutex;
std::vector<uint32_t> g_unlocks; // achievements to pass on to DarkLauncher

void (*g_runGuest)();

// ---- Settings ----

int g_scaleSetting = 0;        // an index in kSizeHalves: 1280 x 720, 1920 x 1080, 2560 x 1440, 3840 x 2160
// The game's own fast forward is RB held on the map; this keeps it held for the player, from one press
// of its function (dl_function in Setup) or of the middle button to the next, or until the stage ends.
std::atomic<bool> g_fastForward;
int g_fillSetting = 1;       // the picture at the screen's proportions, the game's backgrounds stretched (DlBridgeFill)
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
// experimental: one real PC so far (Bazzite, 1.10), besides WSL
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

const DlText t_fill = { L"画面いっぱいに表示", L"Remplir l'écran", L"Fill the screen", L"Llenar la pantalla", L"Preencher a tela",
    L"Bildschirm füllen", L"Riempi lo schermo" };
const DlText t_hFill = { L"16:9 以外の画面（Steam Deck の 16:10 など）で、黒帯の代わりに背景を引き伸ばします。マップや文字はそのままの形です。",
    L"Sur un écran qui n'est pas en 16:9 (le 16:10 du Steam Deck…), les fonds du jeu s'étirent au lieu des bandes noires. La carte, les unités et les textes gardent leur forme.",
    L"On a screen that is not 16:9 (a Steam Deck's 16:10...), the game's backgrounds stretch instead of black bands. The map, the units and the texts keep their shape.",
    L"En una pantalla que no es 16:9 (el 16:10 de Steam Deck...), los fondos del juego se estiran en lugar de las franjas negras. El mapa, las unidades y los textos conservan su forma.",
    L"Numa tela que não é 16:9 (o 16:10 do Steam Deck...), os fundos do jogo se esticam em vez das faixas pretas. O mapa, as unidades e os textos mantêm a forma.",
    L"Auf einem Bildschirm, der nicht 16:9 ist (das 16:10 des Steam Deck ...), werden die Hintergründe des Spiels gestreckt statt schwarzer Balken. Karte, Einheiten und Texte behalten ihre Form.",
    L"Su uno schermo che non è 16:9 (il 16:10 dello Steam Deck...), gli sfondi del gioco si allungano al posto delle bande nere. La mappa, le unità e i testi mantengono la loro forma." };

const DlText t_fastForward = { L"早送りを続ける", L"Avance rapide continue", L"Fast forward on", L"Avance rápido fijo",
    L"Avanço rápido fixo", L"Dauerhafter Vorlauf", L"Avanti veloce fisso" };
const DlText t_hFastForward = {
    L"ゲームの早送り（RB を押し続ける）を、押さずに続けます。押すたびにオン・オフ（マウスの中ボタンでも）。ステージを出ると止まります。",
    L"Garde l'avance rapide du jeu (RB, à maintenir) sans tenir le bouton : un appui l'active, un autre la coupe (le clic du milieu aussi). Elle s'arrête en quittant l'étape.",
    L"Keeps the game's fast forward (RB, held) on without holding the button: one press turns it on, another off (the middle click too). It stops when the stage ends.",
    L"Mantiene el avance rápido del juego (RB, mantenido) sin sujetar el botón: una pulsación lo activa, otra lo quita (también el clic central). Se detiene al salir de la fase.",
    L"Mantém o avanço rápido do jogo (RB, segurado) sem segurar o botão: um toque liga, outro desliga (o clique do meio também). Para ao sair da fase.",
    L"Hält den Vorlauf des Spiels (RB, gehalten) an, ohne die Taste zu halten: ein Druck schaltet ihn ein, ein weiterer aus (auch die mittlere Maustaste). Er endet mit der Stage.",
    L"Mantiene l'avanti veloce del gioco (RB, tenuto premuto) senza tenere il pulsante: una pressione lo attiva, un'altra lo toglie (anche il clic centrale). Si ferma uscendo dalla fase." };

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
    // DarkLauncher's languages (jp fr en es br de it zh ko) as the game's Xbox language ids (Chinese and
    // Korean, not offered, in English)
    static const uint32_t ids[DL_LANGS] = { 2, 4, 1, 5, 1, 3, 6, 1, 1 };
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

// Each achievement's own picture, read from the player's copy of the game (64 x 64 pixel art), as the
// console showed it; nothing of it is in this project.
const uint32_t* AchievementPictureOf(const char* key, int* w, int* h)
{
    static std::map<std::string, std::pair<std::vector<uint32_t>, std::array<int, 2>>> pictures;
    auto found = pictures.find(key);
    if (found == pictures.end())
    {
        unsigned id = 0;
        int width = 0, height = 0;
        std::vector<uint32_t> pixels;
        if (sscanf(key, "ach%u", &id) == 1)
            pixels = AchievementPicture(id, width, height);
        found = pictures.emplace(key, std::make_pair(std::move(pixels), std::array<int, 2>{ width, height })).first;
    }
    if (found->second.first.empty())
        return nullptr;
    *w = found->second.second[0];
    *h = found->second.second[1];
    return found->second.first.data();
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
    { DL_SWITCH, t_fill, t_hFill, &g_fillSetting, 1 },
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
    // the game's fast forward kept on (F, a controller button chosen on the Controls page, the middle
    // click, the pause menu)
    dl_function(t_fastForward, t_hFastForward, "key_fast", 'F', "_fast", [] { g_fastForward = !g_fastForward; }, true);
    dl_setting("fill_screen", &g_fillSetting, 0, 1, false);
    dl_setting("game_language", &g_languageSetting, 0, kGameLanguageCount, true);
    dl_setting("live_notice", &g_liveNotice, 0, 1, false);
    dl_setting("storage_choice", &g_storageChoice, 0, 1, false);
}

// ---- Theme: the settings screen after the game's art direction, drawn by DarkLauncher ----
// Nothing is taken from the game's files. The game's menus are windows of a deep petrol blue with
// rounded corners and a bright rim, the choice lit by a bar; its backdrops are luminous veils over
// dark blue; its logo is tall upright serif capitals; the texts of its menus a narrow sans serif; its
// world is one of crystals. So: that blue, the cream gold of its crystals for the values, rounded
// frames, a bar for the selection, an upright title, a crystal chime; and since 1.11 the project's own
// painted pictures (art.cpp): a land of crystals in pixel art under DarkLauncher's moving clouds (soft
// ones: its clouds in pixels looked too coarse above it, even at their finest), the bar's icons in blue
// and gold.
const DlTheme kTheme = {
    0x32617F,                     // colour: the blue of the game's windows
    0xDFD296,                     // value: the cream gold of its crystals and gold windows
    L"Constantia",                // title_font: upright serif capitals, as the logo
    L"Bahnschrift SemiCondensed", // text_font: a narrow sans serif, as the texts of its menus
    ArtBackdrop, nullptr, ArtIcon, nullptr,
    DL_FRAME_ROUND, DL_CURSOR_BAR, DL_SKY_CLOUDS, DL_TITLE_FLAT, DL_BEEP_CRYSTAL,
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
    L"マウスで遊べるように|カーソルはポインターの下のマスへすぐに移動し、クリックでユニットを配置します。ユニットの列やメニューもマウスで選べます。タッチ画面でも遊べます。\n"
    L"早送りを続ける|ゲームの早送り（RB）を、F キーやマウスの中ボタンなどで押さずに続けられます。\n"
    L"もっと軽く|プロセッサの使用量が約 10 分の 1 になりました。ノート PC や Steam Deck で発熱が減り、電池が長持ちします。\n"
    L"画面いっぱいに|Steam Deck の 16:10 など、別の比率の画面では背景が広がり、黒い帯なしで画面を埋めます。設定の「画面」でオフにできます。\n"
    L"新しい画像|ランチャーの背景、起動中の画像、アイコンを新しく描きました。実績にはゲーム本来の画像が表示されます。",
    L"Jouable à la souris|Le curseur va tout de suite sur la case sous le pointeur, et un clic déploie une unité. La rangée d'unités et les menus se choisissent aussi à la souris. Les boutons affichés suivent les touches ou la manette utilisées, et l'écran tactile marche aussi.\n"
    L"Avance rapide continue|L'avance rapide du jeu (RB) reste active sans tenir le bouton : F, le clic du milieu ou un bouton choisi.\n"
    L"Bien plus léger|Le jeu utilise environ dix fois moins de processeur : moins de chaleur et plus d'autonomie sur un portable ou un Steam Deck.\n"
    L"Remplir l'écran|Sur un écran d'autres proportions, comme le 16:10 du Steam Deck, les fonds s'élargissent et l'image remplit l'écran sans bandes noires. Se désactive dans Paramètres, Image.\n"
    L"De nouvelles images|Un nouveau fond pour le lanceur, une image au démarrage et de nouvelles icônes. Les succès montrent leurs images d'origine.",
    L"Play with the mouse|The cursor goes straight to the square under the pointer, and a click deploys a unit. The row of units and the menus work with the mouse too. The buttons shown follow the keys or controller in use, and the touch screen works too.\n"
    L"Fast forward kept on|The game's fast forward (RB) stays on without holding the button: F, the middle click or a chosen button.\n"
    L"Much lighter|The game uses about ten times less processor time: less heat and a longer battery on a laptop or a Steam Deck.\n"
    L"Fill the screen|On a screen of other proportions, such as the Steam Deck's 16:10, the backgrounds widen and the picture fills the screen without black bands. It can be turned off in Settings, Picture.\n"
    L"New pictures|A new backdrop for the launcher, a picture while the game starts and new icons. The achievements show their own pictures.",
    L"Se juega con el ratón|El cursor va enseguida a la casilla bajo el puntero, y un clic despliega una unidad. La fila de unidades y los menús también se eligen con el ratón. Los botones mostrados siguen las teclas o el mando en uso, y la pantalla táctil también funciona.\n"
    L"Avance rápido fijo|El avance rápido del juego (RB) sigue activo sin sujetar el botón: F, el clic central o un botón elegido.\n"
    L"Mucho más ligero|El juego usa unas diez veces menos procesador: menos calor y más batería en un portátil o un Steam Deck.\n"
    L"Llenar la pantalla|En una pantalla de otras proporciones, como el 16:10 de Steam Deck, los fondos se ensanchan y la imagen llena la pantalla sin bandas negras. Se desactiva en Ajustes, Imagen.\n"
    L"Imágenes nuevas|Un fondo nuevo para el lanzador, una imagen al iniciar el juego e iconos nuevos. Los logros muestran sus propias imágenes.",
    L"Jogável com o mouse|O cursor vai na hora para a casa sob o ponteiro, e um clique posiciona uma unidade; os menus também. Os botões mostrados seguem as teclas ou o controle, e a tela de toque funciona.\n"
    L"Avanço rápido fixo|O avanço rápido do jogo (RB) fica ligado sem segurar o botão: F, o clique do meio ou um botão escolhido.\n"
    L"Muito mais leve|O jogo usa cerca de dez vezes menos processador: menos calor e mais bateria num notebook ou num Steam Deck.\n"
    L"Preencher a tela|Numa tela de outras proporções, como o 16:10 do Steam Deck, os fundos se alargam e a imagem preenche a tela sem faixas pretas. Pode ser desligado em Configurações, Imagem.\n"
    L"Imagens novas|Um novo fundo para o lançador, uma imagem ao iniciar o jogo e ícones novos. As conquistas mostram suas próprias imagens.",
    L"Mit der Maus spielbar|Der Cursor springt sofort auf das Feld unter dem Zeiger, ein Klick setzt eine Einheit ein; die Menüs auch. Die Tastenbilder folgen Tastatur oder Controller, und der Touchscreen geht.\n"
    L"Dauerhafter Vorlauf|Der Vorlauf des Spiels (RB) bleibt an, ohne die Taste zu halten: F, die mittlere Maustaste oder eine gewählte Taste.\n"
    L"Viel leichter|Das Spiel braucht etwa zehnmal weniger Prozessorzeit: weniger Wärme und mehr Akku auf einem Laptop oder Steam Deck.\n"
    L"Bildschirm füllen|Auf einem Bildschirm mit anderem Seitenverhältnis, etwa 16:10 beim Steam Deck, werden die Hintergründe breiter und das Bild füllt den Bildschirm ohne schwarze Balken. Abschaltbar unter Einstellungen, Bild.\n"
    L"Neue Bilder|Ein neuer Hintergrund für den Launcher, ein Bild beim Spielstart und neue Symbole. Die Erfolge zeigen ihre eigenen Bilder.",
    L"Si gioca con il mouse|Il cursore va subito sulla casella sotto il puntatore, e un clic schiera un'unità. La fila di unità e i menu si scelgono anche con il mouse. I pulsanti mostrati seguono i tasti o il controller in uso, e funziona anche lo schermo tattile.\n"
    L"Avanti veloce fisso|L'avanti veloce del gioco (RB) resta attivo senza tenere il pulsante: F, il clic centrale o un pulsante scelto.\n"
    L"Molto più leggero|Il gioco usa circa dieci volte meno processore: meno calore e più batteria su un portatile o uno Steam Deck.\n"
    L"Riempire lo schermo|Su uno schermo di altre proporzioni, come il 16:10 di Steam Deck, gli sfondi si allargano e l'immagine riempie lo schermo senza bande nere. Si disattiva in Impostazioni, Immagine.\n"
    L"Nuove immagini|Un nuovo sfondo per il launcher, un'immagine all'avvio del gioco e nuove icone. Gli obiettivi mostrano le loro immagini." };

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

// The mouse (DlGame.mouse_in_game), added to the controller's buttons: left click is A, right click B.
// When the pointer moves (the controller and the keyboard work as before in between): in a stage, the map's
// cursor goes at once to the point under it (GameStateRequestCursor: the game's own checks redone); in the
// row of units to deploy, in a deployed unit's menu and before a stage in a menu's list, the choice goes
// to the unit or the row under it, by the controller's arrows held until it is there; a click on a row of
// the title's turning menu turns it there (one tap a row) and chooses it. The wheel taps the arrows of the
// menus.
uint16_t MouseButtons(uint16_t buttons)
{
    constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kArrows = 0x000F, kA = 0x1000, kB = 0x2000;
    constexpr int kTapHold = 3, kTapLength = 6; // a wheel's tap: three pictures held, three released
    static int target = -1;            // what is being reached: 1 a unit of the row, 2 a row of a menu (a
                                       // unit's, or before a stage); -1 none
    static int targetX, targetY;       // that unit or row (targetX)
    static int tapPhase;               // pictures into the current tap: arrows held, then released
    static int clickFrames;            // A to press once there (a click while on the way)
    static int stillFrames;            // pictures since the choice last moved
    static int lastX = -1, lastY = -1; // where they were then
    static int wheelTaps;              // notches of the wheel still to tap (up: positive)
    static int sideTaps;               // taps of left or right still to make (right: positive)
    static int rowTaps;                // taps of up or down still to make (up: positive)

    double mouseX = 0, mouseY = 0;
    unsigned mouse = 0;
    int wheel = 0;
    bool overPicture = dl_mouse(&mouseX, &mouseY, &mouse, &wheel);
    // CD_MOUSE_AT=<seconds>,<x>,<y>[,<buttons>][;<seconds>,...]: the pointer there, on the 1280 x 720
    // picture, from that many seconds of play on, until the next (tests: DarkLauncher sees only a real
    // pointer)
    static const char* mouseAt = getenv("CD_MOUSE_AT");
    static ULONGLONG playStart = GetTickCount64();
    double atX = 0, atY = 0;
    for (const char* at = mouseAt; at != nullptr; at = strchr(at, ';') ? strchr(at, ';') + 1 : nullptr)
    {
        double seconds, x, y;
        unsigned buttons = 0;
        if (sscanf(at, "%lf,%lf,%lf,%u", &seconds, &x, &y, &buttons) >= 3 && GetTickCount64() - playStart >= ULONGLONG(seconds * 1000))
        {
            overPicture = true;
            mouseX = -1, mouseY = -1; // (set below, once the picture's place is known)
            atX = x, atY = y;
            mouse = buttons;
        }
    }
    // the debug mode (CD_DEBUG, debug_script.h): its buttons, and its pointer
    static const bool debug = DebugScriptActive();
    if (debug)
    {
        DebugInput input = DebugScriptStep();
        buttons |= input.buttons;
        wheel += input.wheel;
        if (input.pointer)
        {
            overPicture = true;
            mouseX = -1, mouseY = -1;
            atX = input.x, atY = input.y;
            mouse = input.mouse;
        }
    }

    // The console's 1280 x 720 within the picture: drawn at a number of halves of it, with the columns
    // or lines added on both sides to fill the screen (DlBridgeFill)
    double scale = DlBridgeHalves() / 2.0;
    double sideX = (g_pictureWidth / scale - kWidth) / 2, sideY = (g_pictureHeight / scale - kHeight) / 2;
    if ((mouseAt != nullptr || debug) && overPicture && mouseX < 0)
    {
        static bool wiggle; // a hand's slight move each picture, as a real pointer gives (CD_MOUSE_AT)
        wiggle = !wiggle && !debug;
        mouseX = (atX + sideX + (wiggle ? 0.01 : 0)) * scale;
        mouseY = (atY + sideY) * scale;
    }

    int stageMode = GameStateNow().pack != 0 ? GameStateMode() : -1; // -1: not in a stage
    // the row of units to deploy: the stage's mode 1 on its screen 11 (W2's summons, screen 17, are mode 1 too)
    bool unitRow = stageMode == 1 && GameStateStageScreen() == 11;
    // the game's fast forward kept on: RB held on the map (CD_TURBO=<seconds>: on from then, tests)
    static const char* turboTest = getenv("CD_TURBO");
    static bool turboTested;
    if (turboTest != nullptr && !turboTested && stageMode >= 0 && GetTickCount64() - playStart >= strtoul(turboTest, nullptr, 10) * 1000ull)
        g_fastForward = turboTested = true;
    if (stageMode < 0)
        g_fastForward = false;
    else if (g_fastForward && GameStateStageScreen() == 9)
        buttons |= 0x0200;
    if (overPicture && g_pictureWidth != 0 && g_pictureHeight != 0)
    {
        double x = mouseX / scale - sideX, y = mouseY / scale - sideY;
        static double movedX = -1, movedY = -1;
        bool moved = x != movedX || y != movedY;
        movedX = x;
        movedY = y;
        int gridX, gridY, unit, row;
        GameMenu menu = stageMode != 0 && stageMode != 1 ? GameStateMenu() : GameMenu{}; // (before a stage, or a unit's menu)
        if (moved && stageMode == 0 && GameStateGridPoint(x, y, gridX, gridY))
            GameStateRequestCursor(gridX, gridY); // at once, the game's checks redone (see game_state.cpp)
        else if (moved && unitRow && (unit = GameStateUnitAt(x, y)) >= 0)
            target = 1, targetX = unit, targetY = 0;
        else if (moved && menu.kind == GameMenu::kList && (row = GameStateMenuRow(menu, x, y)) >= 0)
            target = 2, targetX = row, targetY = 0;
        // the title's turning menu: a click on another row turns it there (one tap a row), then chooses
        static unsigned held;
        static bool turning; // the click that turns: its A is not the game's
        if ((mouse & 1) && !(held & 1) && menu.kind == GameMenu::kCarousel && (row = GameStateMenuRow(menu, x, y)) >= 0 && row != 2)
        {
            wheelTaps = 2 - row; // up: positive
            clickFrames = 4;
            turning = true;
        }
        if (!(mouse & 1))
            turning = false;
        if ((mouse & 4) && !(held & 4)) // the middle click: fast forward, on and off
            g_fastForward = !g_fastForward;
        // the best scores: a click on an arrow turns the page (the wheel too), on the first row plays its
        // replay; elsewhere a click does nothing
        int scores = GameStateScoresAt(x, y);
        if (scores >= 0 && scores <= 1 && (mouse & 1) && !(held & 1))
            sideTaps += scores == 1 ? 1 : -1;
        if (scores != -2)
        {
            sideTaps -= wheel;
            wheel = 0;
        }
        if (GameStateHelpSettings()) // the wheel changes the value: up, more
        {
            sideTaps += wheel;
            wheel = 0;
        }
        // W2's summons and the rows of units above them: a click on an arrow goes a row up or down, on the
        // summon chooses it; elsewhere on the summons a click does nothing
        int summon = stageMode >= 0 ? GameStateSummonAt(x, y) : -2;
        if (summon >= 0 && summon <= 1 && (mouse & 1) && !(held & 1))
            rowTaps += summon == 0 ? 1 : -1;
        if ((mouseAt != nullptr || debug) && (mouse & 1) && !(held & 1))
            Log("Mouse: click at %.0f,%.0f, stage screen %d, summon %d, scores %d", x, y, GameStateStageScreen(), summon, scores);
        held = mouse;
        bool clickIsA = (scores == -2 || scores == 2) && (summon == -2 || summon == 2);
        buttons |= (mouse & 1 && !turning && clickIsA ? kA : 0) | (mouse & 2 ? kB : 0);
    }
    if (stageMode != 0) // (on the map the arrows move the cursor)
        wheelTaps += wheel;

    // where the choice is now
    int nowX = -1, nowY = 0;
    if (target == 1 && unitRow)
    {
        UnitGrid grid = GameStateUnitGrid();
        nowX = GameStateUnitChoice();
        if (nowX < 0 || nowX >= grid.columns * grid.rows)
            nowX = -1;
    }
    else if (target == 2 && stageMode != 0 && stageMode != 1)
    {
        GameMenu menu = GameStateMenu();
        nowX = GameStateMenuChoice(menu);
        if (menu.kind != GameMenu::kList || nowX >= menu.rows)
            nowX = -1;
    }
    static int travel; // pictures on the way (tests)
    if (target >= 0)
    {
        travel++;
        stillFrames = nowX == lastX && nowY == lastY ? stillFrames + 1 : 0;
        lastX = nowX, lastY = nowY;
        // there, gone (another screen), the controller or the keyboard used, held by the game, or never
        // reached (two seconds): stop
        if (nowX < 0 || (nowX == targetX && nowY == targetY) || (buttons & kArrows) != 0 || stillFrames > 30 || travel > 120)
        {
            if (mouseAt != nullptr && travel > 1)
                Log("Mouse: %s after %d pictures", nowX == targetX && nowY == targetY ? "there" : "stopped", travel);
            travel = 0;
            target = -1;
            stillFrames = 0;
            lastX = lastY = -1;
        }
    }

    if (target >= 0)
    {
        if (buttons & kA)
            clickFrames = 4;
        buttons &= ~kA; // a click waits until the choice is there
        if (target == 1) // the other row first (up or down), then the shorter way round the row
        {
            UnitGrid grid = GameStateUnitGrid();
            int columns = grid.columns, column = targetX % columns, now = nowX % columns;
            if (targetX / columns != nowX / columns) // (below the last row: the summons, W2 and W3)
                buttons |= targetX / columns < nowX / columns ? kUp : kDown;
            else
                buttons |= (column - now + columns) % columns <= columns / 2 ? kRight : kLeft;
        }
        else // a menu's rows, from the top
            buttons |= targetX < nowX ? kUp : kDown;
    }
    else if ((wheelTaps != 0 || sideTaps != 0 || rowTaps != 0) && (buttons & kArrows) == 0)
    {
        // one tap of the arrows per notch: up and down, or left and right in the row of units; then the
        // taps of left and right asked for (the best scores' pages), then those of up and down (the arrows
        // of W2's rows of units and summons)
        if (tapPhase < kTapHold && wheelTaps != 0)
            buttons |= unitRow ? (wheelTaps > 0 ? kLeft : kRight) : (wheelTaps > 0 ? kUp : kDown);
        else if (tapPhase < kTapHold && sideTaps != 0)
            buttons |= sideTaps > 0 ? kRight : kLeft;
        else if (tapPhase < kTapHold)
            buttons |= rowTaps > 0 ? kUp : kDown;
        tapPhase = (tapPhase + 1) % kTapLength;
        if (tapPhase == 0 && wheelTaps != 0)
            wheelTaps -= wheelTaps > 0 ? 1 : -1;
        else if (tapPhase == 0 && sideTaps != 0)
            sideTaps -= sideTaps > 0 ? 1 : -1;
        else if (tapPhase == 0)
            rowTaps -= rowTaps > 0 ? 1 : -1;
    }
    else
    {
        tapPhase = 0;
        wheelTaps = 0;
        sideTaps = 0;
        rowTaps = 0;
        if (clickFrames > 0)
        {
            clickFrames--;
            buttons |= kA;
        }
    }
    return buttons;
}

// ---- The game's button prompts, as the player's device shows them ----

// The game's five button pictures (32 x 32, in its guide at the bottom left and its "A Confirm, B Back"),
// known by the hash of their texels as the renderer reads them, and the DarkLauncher button each shows.
struct Prompt
{
    uint64_t hash;
    unsigned button;
    std::vector<uint32_t> picture; // 32 x 32, 0xAARRGGBB with straight alpha; empty: the game's own
};
std::mutex g_promptMutex;
Prompt g_prompts[] =
{
    { 0xB64DCEADF5F4D340ull, DL_A }, { 0xF04897C7581DA771ull, DL_B }, { 0xA4D50B842EAA7173ull, DL_X },
    { 0x0A3149442B806B77ull, DL_Y }, { 0xE805B2D795D30779ull, DL_R },
};
std::atomic<uint32_t> g_promptGeneration{ 1 };

// DarkLauncher's picture of a button (premultiplied, any width) fitted into 32 x 32: averaged down from twice
// the size, centred, the colours divided by their opacity again.
std::vector<uint32_t> PromptPicture(unsigned button)
{
    constexpr int kSide = 32, kAsk = 64;
    int w = 0, h = 0;
    const uint32_t* source = dl_button_picture(button, kAsk, &w, &h);
    if (source == nullptr || w <= 0 || h <= 0)
        return {};
    double scale = std::min(double(kSide) / w, double(kSide) / h);
    int outW = std::max(1, int(w * scale + 0.5)), outH = std::max(1, int(h * scale + 0.5));
    int left = (kSide - outW) / 2, top = (kSide - outH) / 2;
    std::vector<uint32_t> picture(kSide * kSide, 0);
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++)
        {
            int x0 = int(x / scale), x1 = std::max(x0 + 1, std::min(w, int((x + 1) / scale)));
            int y0 = int(y / scale), y1 = std::max(y0 + 1, std::min(h, int((y + 1) / scale)));
            double sum[4]{};
            int n = 0;
            for (int sy = y0; sy < y1 && sy < h; sy++)
                for (int sx = x0; sx < x1 && sx < w; sx++, n++)
                    for (int c = 0; c < 4; c++)
                        sum[c] += (source[sy * w + sx] >> (c * 8)) & 255;
            if (n == 0)
                continue;
            double a = sum[3] / n;
            uint32_t pixel = uint32_t(a + 0.5) << 24;
            for (int c = 0; c < 3; c++)
                pixel |= uint32_t(a > 0 ? std::min(255.0, sum[c] / n * 255 / a + 0.5) : 0) << (c * 8);
            picture[(top + y) * kSide + left + x] = pixel;
        }
    return picture;
}

// From Frame: the pictures again whenever the device or the keys assigned change (the game's own on an
// Xbox controller, which they are).
void UpdatePrompts()
{
    static unsigned serial = ~0u;
    unsigned now = dl_input_serial();
    if (now == serial)
        return;
    serial = now;
    bool own = dl_input_device() == DL_DEVICE_XBOX;
    std::lock_guard lock(g_promptMutex);
    for (Prompt& prompt : g_prompts)
        prompt.picture = own ? std::vector<uint32_t>() : PromptPicture(prompt.button);
    g_promptGeneration++;
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
    g_buttons = MouseButtons(buttons);
    UpdatePrompts();

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
        if (g_sharedPause > 0)
            g_sharedPause--;
        // the shared texture, for the same picture again too; DarkLauncher cannot show it (no graphics card
        // path yet, or display without one): the pixels for two seconds, and the last ones until then
        bool shown = false;
        if (g_lastShared && g_sharedHandle != nullptr)
        {
            shown = dl_picture_shared(g_sharedHandle, 0); // square pixels: 16:9, or the screen's when filled
            if (shown)
                g_sharedGiven = g_sharedHandle;
            else
                g_sharedPause = 120;
        }
        static int told = -1; // the way the picture goes, logged when it changes
        if (int(shown) != told && (shown || !g_picture.empty()))
        {
            told = int(shown);
            Log("DarkLauncher: picture given %s", shown ? "in a shared texture" : "as pixels");
        }
        // (never more than the buffer DarkLauncher made for the largest picture)
        if (!shown && !g_picture.empty() && g_pixelsWidth <= kMaxPictureWidth && g_pixelsHeight <= kMaxPictureHeight)
        {
            memcpy(pixels, g_picture.data(), g_picture.size() * 4);
            dl_picture_size(int(g_pixelsWidth), int(g_pixelsHeight), 0);
        }
        uint32_t height = shown ? g_pictureHeight : g_pixelsHeight;
        // the picture is drawn larger than the console's 720 lines: the scanlines and the CRT filter
        // follow the console's lines, not the picture's (1920 x 1080, 1.5 times, cannot be said)
        if (shown || !g_picture.empty())
            dl_picture_scale(height % kHeight == 0 ? int(height / kHeight) : 1);
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
    if (count > 0)
        g_soundRoom.notify_one(); // room for the mixer's next sound (DlBridgeAudio)
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
    g_pictureWidth = g_pixelsWidth = width;
    g_pictureHeight = g_pixelsHeight = height;
    g_lastShared = false;
    g_pictures++;
    uint64_t asked = g_asked;
    g_signal.notify_all();
    // Held until DarkLauncher asks for another picture: its menu pauses the game here.
    g_signal.wait(lock, [&] { return g_asked != asked; });
}

void DlBridgePresentShared(void* handle, uint32_t width, uint32_t height)
{
    std::unique_lock lock(g_mutex);
    g_sharedHandle = handle;
    g_lastShared = true;
    g_pictureWidth = width;
    g_pictureHeight = height;
    g_pictures++;
    uint64_t asked = g_asked;
    g_signal.notify_all();
    g_signal.wait(lock, [&] { return g_asked != asked; });
}

bool DlBridgeSharedWanted()
{
    return g_sharedPause == 0;
}

void* DlBridgeSharedGiven()
{
    return g_sharedGiven;
}

uint32_t DlBridgeHalves()
{
    return kSizeHalves[std::clamp(g_scaleSetting, 0, kSizeCount - 1)];
}

void DlBridgeFill(uint32_t& extraWidth, uint32_t& extraHeight)
{
    extraWidth = extraHeight = 0;
    // the proportions of where the picture is shown (CD_ASPECT=<width / height>: another, for tests)
    static const double testAspect = getenv("CD_ASPECT") ? atof(getenv("CD_ASPECT")) : 0;
    double aspect = testAspect > 0 ? testAspect : dl_view_aspect();
    if (g_fillSetting == 0 || aspect <= 0)
        return;
    // narrower (16:10, 4:3): lines above and below; wider (21:9): columns on both sides; even, and at most
    // kMaxExtraWidth or kMaxExtraHeight
    if (aspect < 16.0 / 9.0 - 0.01)
        extraHeight = std::min(uint32_t(kWidth / aspect - kHeight + 1) & ~1u, kMaxExtraHeight);
    else if (aspect > 16.0 / 9.0 + 0.01)
        extraWidth = std::min(uint32_t(kHeight * aspect - kWidth + 1) & ~1u, kMaxExtraWidth);
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
    // waits until DarkLauncher has taken some (Frame says so), not looking every 2 ms (500 times a second)
    static const bool poll = getenv("CD_SPIN") != nullptr; // as before: a look every 2 ms (tests)
    std::unique_lock lock(g_soundMutex);
    while (g_sound.size() / 2 >= kSoundReserve)
    {
        if (!poll)
            g_soundRoom.wait_for(lock, std::chrono::milliseconds(20));
        else
        {
            lock.unlock();
            Sleep(2);
            lock.lock();
        }
    }
    g_sound.insert(g_sound.end(), samples, samples + frames * 2);
}

void DlBridgeUnlock(uint32_t id)
{
    std::lock_guard lock(g_unlockMutex);
    g_unlocks.push_back(id);
}

uint32_t DlBridgePromptGeneration()
{
    return g_promptGeneration;
}

bool DlBridgePrompt(uint32_t* pixels, uint32_t width, uint32_t height)
{
    if (width != 32 || height != 32)
        return false;
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < width * height; i++)
        hash = (hash ^ pixels[i]) * 0x100000001B3ull;
    std::lock_guard lock(g_promptMutex);
    for (const Prompt& prompt : g_prompts)
        if (prompt.hash == hash && !prompt.picture.empty())
        {
            memcpy(pixels, prompt.picture.data(), width * height * 4);
            return true;
        }
    return false;
}

int DarkLauncherMain(int argc, char* argv[], void (*runGuest)())
{
    g_runGuest = runGuest;
    static DlGame game{};
    game.width = int(kMaxPictureWidth);
    game.height = int(kMaxPictureHeight);
    game.aspect = 16.0 / 9.0;
    game.fps = 60;
    game.audio_rate = kRate;
    game.subtitle = t_subtitle;
    // every text above is in seven of DarkLauncher's languages, Italian included (not yet in Chinese
    // nor Korean, which DL_LANGS_ALL counts since DarkLauncher 2.90)
    game.langs = (1u << DL_LANG_JP) | (1u << DL_LANG_FR) | (1u << DL_LANG_EN) | (1u << DL_LANG_ES) | (1u << DL_LANG_BR) |
                 (1u << DL_LANG_DE) | (1u << DL_LANG_IT);
    game.look = DL_LOOK_PIXELS; // 2D pixel art: every row of the Picture page (xBR included)
    game.start = Start;
    game.frame = Frame;
    game.setup = Setup;
    game.safe_mode = SafeMode;
    game.report = Report;
    game.status = Status;
    game.version = GAME_VERSION;
    game.news = t_news;
    game.achievement_picture = AchievementPictureOf;
    game.starting_picture = ArtStarting;
    // An Xbox game: its buttons in the Xbox places at first, and View held (not pressed) opens the
    // pause menu, so a short press stays the game's Back.
    game.pad_layout = DL_PAD_XBOX;
    game.pad_menu = DL_MENU_HOLD;
    game.mouse_in_game = true; // the stage's cursor follows the mouse (see Frame)
    game.rumble = true; // DarkLauncher's Vibration switch, and dl_rumble to every kind of controller
    // a 2009 HD game made for flat screens: the tube's look is not offered; nor DarkLauncher's fast
    // forward, which cannot speed up a game that makes each picture when asked (it has its own)
    // (the game has its own fast forward, RB held: see g_fastForward)
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
