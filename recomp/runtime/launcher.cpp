// Start screen shown before the game, after the one of Kahna (Bahamut Lagoon
// Recomp): the project's name at the top, a bar of sections on the left, the
// page of the selected section beside it, a line of help underneath, and the
// language flags at the bottom right. Everything is drawn with GDI into one
// picture the size of the window; nothing is loaded from the game's files but
// the achievements' names.
//
// It works with the keyboard, an XInput controller and the mouse. Every change
// is written to crystal_defenders.ini at once, so the game starts with it.
//
// Test hook: CD_LAUNCHER_SHOT=<file.bmp> draws the screen once into that file
// (at CD_LAUNCHER_SIZE, 1280x800 by default) after the keys listed in
// CD_LAUNCHER_KEYS (up, down, left, right, enter, escape), then quits.
#include "launcher.h"

#ifndef _WIN32
bool LauncherRun(int, char*[])
{
    return true;
}
#else

#include "achievements.h"
#include "config.h"
#include "guest.h"
#include "xex_header.h"

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

// ---- Texts ----

enum Language { kFr, kEn, kDe, kEs, kIt, kJa, kLanguageCount };

enum Text
{
    T_PLAY, T_LANGUAGE, T_PICTURE, T_SOUND, T_OPTIONS, T_CONTROLS, T_ACHIEVEMENTS, T_SHOTS, T_FOLDER, T_QUIT,
    T_PREVIEW, T_SUBTITLE, T_AUTO_LANGUAGE, T_FULLSCREEN, T_WINDOW, T_AUTOMATIC, T_RENDER, T_SHARP, T_SMOOTH, T_VOLUME,
    T_VIBRATION, T_BACKGROUND_PAUSE, T_LIVE, T_STORAGE, T_UP, T_DOWN, T_LEFT, T_RIGHT, T_DEFAULTS, T_BACK, T_PRESS, T_NONE,
    T_NO_SHOTS, T_HINT_KEYS, T_HINT_PAD,
    // help, in the order of the sections
    H_PLAY, H_LANGUAGE, H_PICTURE, H_SOUND, H_OPTIONS, H_CONTROLS, H_ACHIEVEMENTS, H_SHOTS, H_FOLDER, H_QUIT,
    H_FULLSCREEN, H_WINDOW, H_RENDER, H_VOLUME, H_VIBRATION, H_BACKGROUND_PAUSE, H_LIVE, H_STORAGE, H_KEY, H_DEFAULTS, H_BACK,
    H_ACHIEVEMENT_TOTAL, // %d earned, %d in all, %d points earned, %d points in all
    H_SHOT, H_SHOT_DELETE, H_RESERVED,
    kTextCount
};

const wchar_t* const kTexts[kLanguageCount][kTextCount] =
{
    { // Français
        L"Jouer", L"Langue", L"Image", L"Son", L"Options", L"Commandes", L"Succès", L"Captures", L"Dossier du jeu", L"Quitter",
        L"Aperçu", L"portage natif non officiel pour Windows", L"Automatique (langue de Windows)", L"Plein écran", L"Taille de la fenêtre",
        L"Automatique", L"Rendu", L"Net", L"Lisse", L"Volume", L"Vibrations", L"Pause en arrière-plan", L"Avertissement Xbox LIVE",
        L"Choix du stockage", L"Haut", L"Bas", L"Gauche", L"Droite", L"Touches d'origine", L"Retour", L"Appuyez sur une touche…", L"aucune",
        L"Aucune capture pour l'instant : en jeu, F12 enregistre l'image.",
        L"Flèches : déplacer     Entrée : choisir     Échap : retour",
        L"Croix : déplacer     A : choisir     B : retour     Start : jouer",
        L"Lance le jeu. En jeu, F1 ou les deux sticks enfoncés ouvrent le menu des réglages.",
        L"Langue des textes du jeu et de cet écran. Les drapeaux en bas à droite la changent aussi.",
        L"Plein écran, taille de la fenêtre et rendu de l'image.",
        L"Volume général. Le jeu a aussi ses propres réglages du son dans Aide et options.",
        L"Vibrations, pause, et ce que le jeu affiche comme sur une console hors ligne.",
        L"Touches du clavier. Toute manette XInput (type Xbox) fonctionne sans réglage.",
        L"Succès gagnés dans le jeu, enregistrés sur cet ordinateur.",
        L"Images enregistrées en jeu avec F12.",
        L"Ouvre le dossier du jeu : sauvegardes (userdata), captures et réglages.",
        L"Ferme le programme.",
        L"Démarrer en plein écran. En jeu, F11 ou Alt+Entrée passent de l'un à l'autre.",
        L"Taille de l'image en fenêtre. Automatique : 1280 × 720 à l'échelle d'affichage de Windows.",
        L"Net : image calculée à la résolution de l'écran. Lisse : image d'origine en 1280 × 720, agrandie.",
        L"Gauche et droite changent le volume.",
        L"La manette vibre quand le jeu le demande.",
        L"Le jeu s'arrête quand sa fenêtre n'est plus au premier plan.",
        L"Affiche au démarrage l'avertissement « Non connecté à Xbox LIVE », comme une console hors ligne.",
        L"Le jeu demande où enregistrer, comme sur la console. Sinon, il utilise directement le dossier userdata.",
        L"Entrée, puis la nouvelle touche. Échap annule.",
        L"Remet toutes les touches d'origine.",
        L"Revient à la barre de gauche.",
        L"%d succès sur %d     %d G sur %d",
        L"Entrée ouvre l'image, Suppr la met à la corbeille.",
        L"Appuyez encore sur Suppr pour mettre cette image à la corbeille.",
        L"Cette touche ne peut pas être choisie (F1, F11 et F12 sont réservées).",
    },
    { // English
        L"Play", L"Language", L"Picture", L"Sound", L"Options", L"Controls", L"Achievements", L"Screenshots", L"Game folder", L"Quit",
        L"Preview", L"unofficial native port for Windows", L"Automatic (Windows language)", L"Full screen", L"Window size",
        L"Automatic", L"Rendering", L"Sharp", L"Smooth", L"Volume", L"Vibration", L"Pause in background", L"Xbox LIVE notice",
        L"Storage choice", L"Up", L"Down", L"Left", L"Right", L"Default keys", L"Back", L"Press a key…", L"none",
        L"No screenshots yet: in the game, F12 saves the picture.",
        L"Arrows: move     Enter: select     Esc: back",
        L"D-pad: move     A: select     B: back     Start: play",
        L"Starts the game. While playing, F1 or both sticks pressed open the settings menu.",
        L"Language of the game's text and of this screen. The flags at the bottom right change it too.",
        L"Full screen, window size and rendering.",
        L"Overall volume. The game also has its own sound settings in Help & Options.",
        L"Vibration, pause, and what the game shows as on an offline console.",
        L"Keyboard keys. Any XInput (Xbox-type) controller works without setup.",
        L"Achievements earned in the game, stored on this computer.",
        L"Pictures saved in the game with F12.",
        L"Opens the game's folder: saves (userdata), screenshots and settings.",
        L"Closes the program.",
        L"Start in full screen. While playing, F11 or Alt+Enter switch.",
        L"Size of the picture in a window. Automatic: 1280 × 720 at the Windows display scale.",
        L"Sharp: drawn at the screen's resolution. Smooth: the original 1280 × 720 picture, enlarged.",
        L"Left and right change the volume.",
        L"The controller vibrates when the game asks for it.",
        L"The game stops while its window is not the active one.",
        L"Shows the “Not connected to Xbox LIVE” notice at start, as an offline console does.",
        L"The game asks where to save, as on the console. Otherwise it uses the userdata folder directly.",
        L"Enter, then the new key. Esc cancels.",
        L"Puts every key back to its default.",
        L"Goes back to the bar on the left.",
        L"%d of %d achievements     %d of %d G",
        L"Enter opens the picture, Delete sends it to the Recycle Bin.",
        L"Press Delete again to send this picture to the Recycle Bin.",
        L"This key cannot be chosen (F1, F11 and F12 are reserved).",
    },
    { // Deutsch
        L"Spielen", L"Sprache", L"Bild", L"Ton", L"Optionen", L"Steuerung", L"Erfolge", L"Screenshots", L"Spielordner", L"Beenden",
        L"Vorschau", L"inoffizielle native Portierung für Windows", L"Automatisch (Windows-Sprache)", L"Vollbild", L"Fenstergröße",
        L"Automatisch", L"Darstellung", L"Scharf", L"Weich", L"Lautstärke", L"Vibration", L"Pause im Hintergrund", L"Xbox-LIVE-Hinweis",
        L"Speicherauswahl", L"Oben", L"Unten", L"Links", L"Rechts", L"Standardtasten", L"Zurück", L"Taste drücken…", L"keine",
        L"Noch keine Screenshots: Im Spiel speichert F12 das Bild.",
        L"Pfeiltasten: bewegen     Eingabe: wählen     Esc: zurück",
        L"Steuerkreuz: bewegen     A: wählen     B: zurück     Start: spielen",
        L"Startet das Spiel. Im Spiel öffnen F1 oder beide gedrückten Sticks das Einstellungsmenü.",
        L"Sprache der Spieltexte und dieses Bildschirms. Auch die Flaggen unten rechts ändern sie.",
        L"Vollbild, Fenstergröße und Darstellung.",
        L"Gesamtlautstärke. Das Spiel hat unter Hilfe und Optionen auch eigene Toneinstellungen.",
        L"Vibration, Pause und was das Spiel wie auf einer Konsole ohne Verbindung zeigt.",
        L"Tastaturbelegung. Jeder XInput-Controller (Xbox-Art) funktioniert ohne Einrichtung.",
        L"Im Spiel errungene Erfolge, auf diesem Computer gespeichert.",
        L"Im Spiel mit F12 gespeicherte Bilder.",
        L"Öffnet den Spielordner: Spielstände (userdata), Screenshots und Einstellungen.",
        L"Schließt das Programm.",
        L"Im Vollbild starten. Im Spiel wechseln F11 oder Alt+Eingabe.",
        L"Bildgröße im Fenster. Automatisch: 1280 × 720 mit der Windows-Anzeigeskalierung.",
        L"Scharf: in der Bildschirmauflösung berechnet. Weich: das ursprüngliche 1280 × 720-Bild, vergrößert.",
        L"Links und rechts ändern die Lautstärke.",
        L"Der Controller vibriert, wenn das Spiel es verlangt.",
        L"Das Spiel hält an, solange sein Fenster nicht aktiv ist.",
        L"Zeigt beim Start den Hinweis „Nicht mit Xbox LIVE verbunden“, wie eine Konsole ohne Verbindung.",
        L"Das Spiel fragt wie auf der Konsole nach dem Speicherort. Sonst nutzt es direkt den Ordner userdata.",
        L"Eingabe, dann die neue Taste. Esc bricht ab.",
        L"Setzt alle Tasten auf die Standardbelegung zurück.",
        L"Zurück zur Leiste links.",
        L"%d von %d Erfolgen     %d von %d G",
        L"Eingabe öffnet das Bild, Entf legt es in den Papierkorb.",
        L"Noch einmal Entf drücken, um dieses Bild in den Papierkorb zu legen.",
        L"Diese Taste ist nicht wählbar (F1, F11 und F12 sind reserviert).",
    },
    { // Español
        L"Jugar", L"Idioma", L"Imagen", L"Sonido", L"Opciones", L"Controles", L"Logros", L"Capturas", L"Carpeta del juego", L"Salir",
        L"Vista previa", L"adaptación nativa no oficial para Windows", L"Automático (idioma de Windows)", L"Pantalla completa",
        L"Tamaño de ventana", L"Automático", L"Renderizado", L"Nítido", L"Suave", L"Volumen", L"Vibración", L"Pausa en segundo plano",
        L"Aviso de Xbox LIVE", L"Elegir almacenamiento", L"Arriba", L"Abajo", L"Izquierda", L"Derecha", L"Teclas por defecto", L"Volver",
        L"Pulsa una tecla…", L"ninguna",
        L"Aún no hay capturas: en el juego, F12 guarda la imagen.",
        L"Flechas: mover     Intro: elegir     Esc: volver",
        L"Cruceta: mover     A: elegir     B: volver     Start: jugar",
        L"Inicia el juego. Durante la partida, F1 o pulsar los dos sticks abren el menú de ajustes.",
        L"Idioma de los textos del juego y de esta pantalla. Las banderas de abajo a la derecha también lo cambian.",
        L"Pantalla completa, tamaño de ventana y renderizado.",
        L"Volumen general. El juego también tiene sus propios ajustes de sonido en Ayuda y opciones.",
        L"Vibración, pausa y lo que el juego muestra como en una consola sin conexión.",
        L"Teclas del teclado. Cualquier mando XInput (tipo Xbox) funciona sin configurarlo.",
        L"Logros obtenidos en el juego, guardados en este ordenador.",
        L"Imágenes guardadas en el juego con F12.",
        L"Abre la carpeta del juego: partidas (userdata), capturas y ajustes.",
        L"Cierra el programa.",
        L"Empezar en pantalla completa. En el juego, F11 o Alt+Intro cambian.",
        L"Tamaño de la imagen en ventana. Automático: 1280 × 720 según la escala de pantalla de Windows.",
        L"Nítido: calculada a la resolución de la pantalla. Suave: la imagen original de 1280 × 720, ampliada.",
        L"Izquierda y derecha cambian el volumen.",
        L"El mando vibra cuando el juego lo pide.",
        L"El juego se detiene mientras su ventana no está activa.",
        L"Muestra al inicio el aviso «No conectado a Xbox LIVE», como una consola sin conexión.",
        L"El juego pregunta dónde guardar, como en la consola. Si no, usa directamente la carpeta userdata.",
        L"Intro y luego la nueva tecla. Esc cancela.",
        L"Restablece todas las teclas.",
        L"Vuelve a la barra de la izquierda.",
        L"%d de %d logros     %d de %d G",
        L"Intro abre la imagen, Supr la envía a la papelera.",
        L"Pulsa Supr otra vez para enviar esta imagen a la papelera.",
        L"Esta tecla no se puede elegir (F1, F11 y F12 están reservadas).",
    },
    { // Italiano
        L"Gioca", L"Lingua", L"Immagine", L"Audio", L"Opzioni", L"Comandi", L"Obiettivi", L"Screenshot", L"Cartella del gioco", L"Esci",
        L"Anteprima", L"conversione nativa non ufficiale per Windows", L"Automatica (lingua di Windows)", L"Schermo intero",
        L"Dimensione finestra", L"Automatica", L"Resa", L"Nitida", L"Morbida", L"Volume", L"Vibrazione", L"Pausa in background",
        L"Avviso Xbox LIVE", L"Scelta archiviazione", L"Su", L"Giù", L"Sinistra", L"Destra", L"Tasti predefiniti", L"Indietro",
        L"Premi un tasto…", L"nessuno",
        L"Ancora nessuno screenshot: nel gioco, F12 salva l'immagine.",
        L"Frecce: muovi     Invio: scegli     Esc: indietro",
        L"Croce: muovi     A: scegli     B: indietro     Start: gioca",
        L"Avvia il gioco. Durante la partita, F1 o premere entrambi gli stick aprono il menu delle impostazioni.",
        L"Lingua dei testi del gioco e di questa schermata. Anche le bandiere in basso a destra la cambiano.",
        L"Schermo intero, dimensione della finestra e resa dell'immagine.",
        L"Volume generale. Il gioco ha anche le sue impostazioni audio in Aiuto e opzioni.",
        L"Vibrazione, pausa e ciò che il gioco mostra come su una console non connessa.",
        L"Tasti della tastiera. Qualsiasi controller XInput (tipo Xbox) funziona senza configurazione.",
        L"Obiettivi ottenuti nel gioco, salvati su questo computer.",
        L"Immagini salvate nel gioco con F12.",
        L"Apre la cartella del gioco: salvataggi (userdata), screenshot e impostazioni.",
        L"Chiude il programma.",
        L"Avvia a schermo intero. Nel gioco, F11 o Alt+Invio cambiano modalità.",
        L"Dimensione dell'immagine in finestra. Automatica: 1280 × 720 secondo la scala dello schermo di Windows.",
        L"Nitida: calcolata alla risoluzione dello schermo. Morbida: l'immagine originale 1280 × 720, ingrandita.",
        L"Sinistra e destra cambiano il volume.",
        L"Il controller vibra quando il gioco lo richiede.",
        L"Il gioco si ferma quando la sua finestra non è attiva.",
        L"Mostra all'avvio l'avviso «Non connesso a Xbox LIVE», come una console non connessa.",
        L"Il gioco chiede dove salvare, come sulla console. Altrimenti usa direttamente la cartella userdata.",
        L"Invio, poi il nuovo tasto. Esc annulla.",
        L"Ripristina tutti i tasti predefiniti.",
        L"Torna alla barra a sinistra.",
        L"%d obiettivi su %d     %d G su %d",
        L"Invio apre l'immagine, Canc la sposta nel cestino.",
        L"Premi di nuovo Canc per spostare questa immagine nel cestino.",
        L"Questo tasto non può essere scelto (F1, F11 e F12 sono riservati).",
    },
    { // 日本語
        L"プレイ", L"言語", L"画面", L"サウンド", L"オプション", L"操作", L"実績", L"スクリーンショット", L"ゲームフォルダー", L"終了",
        L"プレビュー", L"Windows 向け非公式ネイティブ移植版", L"自動（Windows の言語）", L"フルスクリーン", L"ウィンドウサイズ",
        L"自動", L"描画", L"くっきり", L"なめらか", L"音量", L"振動", L"バックグラウンドで一時停止", L"Xbox LIVE の通知",
        L"保存先の選択", L"上", L"下", L"左", L"右", L"初期設定のキー", L"戻る", L"キーを押してください…", L"なし",
        L"スクリーンショットはまだありません。ゲーム中に F12 で画面を保存できます。",
        L"矢印キー：移動　　Enter：決定　　Esc：戻る",
        L"十字キー：移動　　A：決定　　B：戻る　　START：プレイ",
        L"ゲームを開始します。プレイ中は F1 か両スティックの押し込みで設定メニューを開けます。",
        L"ゲームのテキストとこの画面の言語です。右下の国旗でも変更できます。",
        L"フルスクリーン、ウィンドウサイズ、描画方法。",
        L"全体の音量です。ゲーム内の「ヘルプ＆オプション」にも音の設定があります。",
        L"振動、一時停止、オフラインの本体と同じ表示に関する設定です。",
        L"キーボードのキー設定です。XInput（Xbox 型）コントローラーは設定なしで使えます。",
        L"ゲームで獲得した実績です（このパソコンに保存）。",
        L"ゲーム中に F12 で保存した画像です。",
        L"ゲームフォルダーを開きます：セーブデータ（userdata）、スクリーンショット、設定。",
        L"プログラムを終了します。",
        L"フルスクリーンで起動します。プレイ中は F11 か Alt+Enter で切り替えられます。",
        L"ウィンドウ表示時の画面サイズ。自動：Windows の表示スケールに合わせた 1280 × 720。",
        L"くっきり：画面の解像度で描画。なめらか：元の 1280 × 720 の画像を拡大。",
        L"左右で音量を変更します。",
        L"ゲームの指示でコントローラーが振動します。",
        L"ウィンドウがアクティブでない間、ゲームを止めます。",
        L"起動時に「Xbox LIVE に接続されていません」の通知を表示します（オフラインの本体と同じ）。",
        L"本体と同じように保存先を尋ねます。オフの場合は userdata フォルダーに直接保存します。",
        L"Enter を押してから新しいキーを押します。Esc で取り消し。",
        L"すべてのキーを初期設定に戻します。",
        L"左のバーに戻ります。",
        L"実績 %d / %d　　%d / %d G",
        L"Enter で画像を開き、Delete でごみ箱に移動します。",
        L"もう一度 Delete を押すと、この画像をごみ箱に移動します。",
        L"このキーは選べません（F1、F11、F12 は予約済み）。",
    },
};

// Xbox language ids, in the order of the flags (and of kTexts).
constexpr uint32_t kLanguageIds[kLanguageCount] = { 4, 1, 3, 5, 6, 2 };
constexpr const char* kLanguageCodes[kLanguageCount] = { "fr", "en", "de", "es", "it", "ja" };
constexpr const wchar_t* kLanguageNames[kLanguageCount] = { L"Français", L"English", L"Deutsch", L"Español", L"Italiano", L"日本語" };

int g_language = kEn;

int UiLanguage()
{
    uint32_t id = GuestLanguage();
    for (int i = 0; i < kLanguageCount; i++)
    {
        if (kLanguageIds[i] == id)
            return i;
    }
    return kEn;
}

const wchar_t* T(int text)
{
    return kTexts[g_language][text];
}

// ---- Sections and their rows ----

enum Section { kPlay, kLanguagePage, kPicture, kSound, kOptions, kControls, kAchievements, kShots, kFolder, kQuit, kSectionCount };
constexpr int kTopSections = 8; // the others sit at the bottom of the bar
constexpr wchar_t kIcons[kSectionCount] = { 0xE768, 0xE774, 0xE7F4, 0xE767, 0xE713, 0xE7FC, 0xE734, 0xE722, 0xE8B7, 0xE7E8 };

// The game's controller buttons, as named in the [clavier] part of the settings file.
struct ButtonKeys
{
    const char* name;
    uint16_t button;
    const wchar_t* label; // nullptr: the direction's text
    int defaults[2];
};
constexpr ButtonKeys kButtons[] =
{
    { "haut", 0x0001, nullptr, { VK_UP } }, { "bas", 0x0002, nullptr, { VK_DOWN } },
    { "gauche", 0x0004, nullptr, { VK_LEFT } }, { "droite", 0x0008, nullptr, { VK_RIGHT } },
    { "a", 0x1000, L"A", { VK_SPACE, 'X' } }, { "b", 0x2000, L"B", { VK_ESCAPE, 'C' } },
    { "x", 0x4000, L"X", { 'S' } }, { "y", 0x8000, L"Y", { 'D' } },
    { "start", 0x0010, L"Start", { VK_RETURN } }, { "back", 0x0020, L"Back", { VK_TAB } },
    { "lb", 0x0100, L"LB", { 'Q' } }, { "rb", 0x0200, L"RB", { 'E' } },
};
constexpr int kButtonCount = int(sizeof(kButtons) / sizeof(kButtons[0]));

enum PictureRow { kRowFullscreen, kRowWindow, kRowRender, kPictureRows };
enum OptionRow { kRowVibration, kRowBackgroundPause, kRowLive, kRowStorage, kOptionRows };

struct State
{
    int section = kPlay;
    bool inPage = false; // the selection is in the page rather than in the bar
    int row = 0;
    int top = 0;         // first row shown, on pages that scroll
    int waitKey = -1;    // controls row waiting for its new key
    int note = -1;       // help shown instead of the usual one until the selection moves
    int deleteArmed = -1; // screenshot waiting for the second press of Delete
    int result = 0;      // 1: play, 2: quit
} g;

std::vector<AchievementEntry> g_achievements;
bool g_achievementsRead;

struct Shot
{
    std::filesystem::path path;
    std::wstring label;
};
std::vector<Shot> g_shots;

void ReadShots()
{
    g_shots.clear();
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    for (auto& entry : std::filesystem::directory_iterator("captures", error))
    {
        std::wstring extension = entry.path().extension().wstring();
        if (_wcsicmp(extension.c_str(), L".bmp") == 0)
            found.push_back({ entry.last_write_time(error), entry.path() });
    }
    // newest first; the names hold the time they were taken
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second > b.second; });
    for (auto& [time, path] : found)
    {
        // crystal_defenders_YYYYMMDD_HHMMSS.bmp, as the game names them
        std::wstring name = path.stem().wstring();
        std::wstring label = name;
        unsigned y, mo, d, h, mi, s;
        size_t at = name.rfind(L'_');
        if (at != std::wstring::npos && at >= 9 &&
            swscanf(name.c_str() + at - 8, L"%4u%2u%2u_%2u%2u%2u", &y, &mo, &d, &h, &mi, &s) == 6)
        {
            wchar_t text[64];
            swprintf(text, 64, L"%04u-%02u-%02u   %02u:%02u:%02u", y, mo, d, h, mi, s);
            label = text;
        }
        g_shots.push_back({ path, label });
    }
}

const std::vector<AchievementEntry>& Achievements()
{
    if (!g_achievementsRead)
    {
        g_achievements = AchievementsList();
        g_achievementsRead = true;
    }
    return g_achievements;
}

// Sizes offered for the window: the usual 16:9 ones and the Steam Deck's
// 1280x800, as far as they fit the screen; "automatic" first.
std::vector<SIZE> WindowSizes(HMONITOR monitor)
{
    MONITORINFO info{ sizeof(MONITORINFO) };
    GetMonitorInfoW(monitor, &info);
    LONG width = info.rcMonitor.right - info.rcMonitor.left, height = info.rcMonitor.bottom - info.rcMonitor.top;
    std::vector<SIZE> sizes{ { 0, 0 } };
    static const SIZE usual[] = { { 1280, 720 }, { 1280, 800 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3200, 1800 }, { 3840, 2160 } };
    for (const SIZE& size : usual)
    {
        if (size.cx < width && size.cy < height)
            sizes.push_back(size);
    }
    const Config& config = GetConfig();
    if (config.windowWidth != 0 && std::none_of(sizes.begin(), sizes.end(), [&](const SIZE& s) { return UINT(s.cx) == config.windowWidth && UINT(s.cy) == config.windowHeight; }))
        sizes.push_back({ LONG(config.windowWidth), LONG(config.windowHeight) });
    return sizes;
}

HWND g_window;

int RowCount(int section)
{
    switch (section)
    {
    case kLanguagePage: return 1 + kLanguageCount + 1;
    case kPicture: return kPictureRows + 1;
    case kSound: return 2;
    case kOptions: return kOptionRows + 1;
    case kControls: return kButtonCount + 2;
    case kAchievements: return int(Achievements().size());
    case kShots: return int(g_shots.size());
    default: return 0;
    }
}

bool HasPage(int section)
{
    return section != kPlay && section != kFolder && section != kQuit;
}

// ---- Settings ----

void SaveFlag(const char* section, const char* key, bool value)
{
    ConfigSave(section, key, value ? "1" : "0");
}

void SetLanguage(uint32_t id)
{
    MutableConfig().language = id;
    const char* code = "auto";
    for (int i = 0; i < kLanguageCount; i++)
    {
        if (kLanguageIds[i] == id)
            code = kLanguageCodes[i];
    }
    ConfigSave("jeu", "langue", code);
    g_language = UiLanguage();
    g_achievementsRead = false;
}

// Name of a key in the settings file (see ParseKey in config.cpp); nullptr
// when it has none there.
std::string KeyFileName(int key)
{
    static const struct { int key; const char* name; } names[] =
    {
        { VK_UP, "Haut" }, { VK_DOWN, "Bas" }, { VK_LEFT, "Gauche" }, { VK_RIGHT, "Droite" }, { VK_RETURN, "Entree" },
        { VK_SPACE, "Espace" }, { VK_ESCAPE, "Echap" }, { VK_TAB, "Tab" }, { VK_BACK, "Retour" }, { VK_SHIFT, "Maj" },
        { VK_CONTROL, "Ctrl" },
    };
    for (auto& entry : names)
    {
        if (entry.key == key)
            return entry.name;
    }
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
        return std::string(1, char(key));
    if (key >= VK_F2 && key <= VK_F10)
        return "F" + std::to_string(key - VK_F1 + 1);
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        return "Num" + std::to_string(key - VK_NUMPAD0);
    return {};
}

// Name of a key as Windows writes it in the user's keyboard layout.
std::wstring KeyLabel(int key)
{
    switch (key)
    {
    case VK_UP: return L"↑";
    case VK_DOWN: return L"↓";
    case VK_LEFT: return L"←";
    case VK_RIGHT: return L"→";
    }
    wchar_t name[64]{};
    LONG scan = LONG(MapVirtualKeyW(UINT(key), MAPVK_VK_TO_VSC)) << 16;
    if (int length = GetKeyNameTextW(scan, name, 64); length > 0)
    {
        // "ESPACE" -> "Espace"
        CharLowerBuffW(name + 1, DWORD(length - 1));
        return name;
    }
    std::string file = KeyFileName(key);
    return std::wstring(file.begin(), file.end());
}

void SaveButton(uint16_t button)
{
    for (auto& entry : kButtons)
    {
        if (entry.button != button)
            continue;
        std::string value;
        for (auto& binding : GetConfig().keys)
        {
            if (binding.button == button)
                value += (value.empty() ? "" : " ") + KeyFileName(binding.key);
        }
        ConfigSave("clavier", entry.name, value.c_str());
    }
}

// The key now works this button only, in place of the button's other keys.
bool BindKey(int row, int key)
{
    if (KeyFileName(key).empty())
        return false;
    auto& keys = MutableConfig().keys;
    std::vector<uint16_t> touched{ kButtons[row].button };
    for (auto& binding : keys)
    {
        if (binding.key == key && binding.button != kButtons[row].button)
            touched.push_back(binding.button);
    }
    std::erase_if(keys, [&](const KeyBinding& binding) { return binding.key == key || binding.button == kButtons[row].button; });
    keys.push_back({ key, kButtons[row].button });
    for (uint16_t button : touched)
        SaveButton(button);
    return true;
}

void DefaultKeys()
{
    auto& keys = MutableConfig().keys;
    keys.clear();
    for (auto& entry : kButtons)
    {
        for (int key : entry.defaults)
        {
            if (key != 0)
                keys.push_back({ key, entry.button });
        }
    }
    for (auto& entry : kButtons)
        SaveButton(entry.button);
}

std::wstring ButtonKeysText(uint16_t button)
{
    std::wstring text;
    for (auto& binding : GetConfig().keys)
    {
        if (binding.button == button)
            text += (text.empty() ? L"" : L"  ") + KeyLabel(binding.key);
    }
    return text.empty() ? T(T_NONE) : text;
}

std::wstring SizeText(SIZE size)
{
    if (size.cx == 0)
        return T(T_AUTOMATIC);
    return std::to_wstring(size.cx) + L" × " + std::to_wstring(size.cy);
}

void ChangeWindowSize(int step)
{
    Config& config = MutableConfig();
    std::vector<SIZE> sizes = WindowSizes(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST));
    int current = 0;
    for (int i = 0; i < int(sizes.size()); i++)
    {
        if (UINT(sizes[i].cx) == config.windowWidth && UINT(sizes[i].cy) == config.windowHeight)
            current = i;
    }
    current = (current + step + int(sizes.size())) % int(sizes.size());
    config.windowWidth = UINT(sizes[current].cx);
    config.windowHeight = UINT(sizes[current].cy);
    char text[32] = "auto";
    if (config.windowWidth != 0)
        snprintf(text, sizeof(text), "%ux%u", config.windowWidth, config.windowHeight);
    ConfigSave("affichage", "fenetre", text);
}

void ChangeVolume(int step)
{
    Config& config = MutableConfig();
    config.volume = std::clamp((config.volume + 5) / 10 * 10 + step * 10, 0, 100);
    ConfigSave("son", "volume", std::to_string(config.volume).c_str());
}

// Left / right (step -1 / +1) or Enter (step 0) on a row with a value.
bool ChangeValue(int step)
{
    Config& config = MutableConfig();
    switch (g.section)
    {
    case kPicture:
        if (g.row == kRowFullscreen)
            SaveFlag("affichage", "plein_ecran", config.fullscreen = !config.fullscreen);
        else if (g.row == kRowWindow)
            ChangeWindowSize(step < 0 ? -1 : 1);
        else if (g.row == kRowRender)
            ConfigSave("affichage", "rendu", (config.sharp = !config.sharp) ? "net" : "lisse");
        else
            return false;
        return true;
    case kSound:
        if (g.row != 0)
            return false;
        ChangeVolume(step < 0 ? -1 : step > 0 ? 1 : (config.volume >= 100 ? -10 : 1));
        return true;
    case kOptions:
        if (g.row == kRowVibration)
            SaveFlag("jeu", "vibration", config.vibration = !config.vibration);
        else if (g.row == kRowBackgroundPause)
            SaveFlag("jeu", "pause_arriere_plan", config.pauseInBackground = !config.pauseInBackground);
        else if (g.row == kRowLive)
            SaveFlag("jeu", "avertissement_live", config.liveWarning = !config.liveWarning);
        else if (g.row == kRowStorage)
            SaveFlag("jeu", "choix_stockage", config.storageDialog = !config.storageDialog);
        else
            return false;
        return true;
    }
    return false;
}

// ---- Drawing ----

// The screen is laid out on a 640 x 400 grid, scaled to fit the window and
// centred in it; the backdrop fills the whole window.
double g_scale = 1;
int g_originX, g_originY;
HDC g_dc;

int PX(double value) { return int(std::floor(value * g_scale + 0.5)); }
int SX(double x) { return g_originX + PX(x); }
int SY(double y) { return g_originY + PX(y); }

constexpr double kPanelX0 = 164, kPanelX1 = 630, kPanelY0 = 60, kPanelY1 = 312;
constexpr double kHelpY0 = 320, kHelpY1 = 372;
constexpr double kSideX0 = 10, kSideX1 = 156;
constexpr double kRowX0 = 174, kRowX1 = 620, kRowY0 = 72, kRowH = 20;
constexpr double kValueW = 176;
constexpr int kVisibleRows = 11;
constexpr double kEntryH = 28; // an achievement: its name, then a line of text
constexpr int kVisibleEntries = 8;
constexpr double kFlagW = 22, kFlagH = 14, kFlagY = 379;
double FlagX(int i) { return 630 - (kLanguageCount - i) * 26.0 + 4; }

const COLORREF kText = RGB(232, 240, 246);
const COLORREF kTextDim = RGB(170, 196, 212);
const COLORREF kValue = RGB(255, 226, 150);  // as the values of the game's own menus
const COLORREF kFocus = RGB(46, 128, 168);
const COLORREF kHere = RGB(26, 78, 106);
const COLORREF kEdge = RGB(110, 180, 210);

struct Fonts
{
    double scale = 0;
    int language = -1;
    HFONT item{}, small{}, help{}, titleBig{}, titleSmall{}, name{}, subtitle{}, icons{};
    bool haveIcons = false;
} g_fonts;

HFONT MakeFont(double size, int weight, bool italic, const wchar_t* face)
{
    return CreateFontW(-PX(size), 0, 0, 0, weight, italic, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
}

void PrepareFonts()
{
    if (g_fonts.scale == g_scale && g_fonts.language == g_language)
        return;
    for (HFONT font : { g_fonts.item, g_fonts.small, g_fonts.help, g_fonts.titleBig, g_fonts.titleSmall, g_fonts.name, g_fonts.subtitle, g_fonts.icons })
    {
        if (font != nullptr)
            DeleteObject(font);
    }
    bool japanese = g_language == kJa;
    const wchar_t* ui = japanese ? L"Yu Gothic UI" : L"Segoe UI";
    g_fonts.item = MakeFont(15, FW_SEMIBOLD, false, ui);
    g_fonts.small = MakeFont(11.5, FW_NORMAL, false, ui);
    g_fonts.help = MakeFont(13, FW_NORMAL, !japanese, ui);
    g_fonts.titleBig = MakeFont(37, FW_BOLD, false, L"Palatino Linotype");
    g_fonts.titleSmall = MakeFont(28, FW_BOLD, false, L"Palatino Linotype");
    g_fonts.name = MakeFont(15, FW_BOLD, false, L"Palatino Linotype");
    g_fonts.subtitle = MakeFont(12.5, FW_NORMAL, !japanese, japanese ? L"Yu Mincho" : L"Palatino Linotype");
    // Icons come from the symbol font of Windows 10 and 11; without it the entries have none.
    g_fonts.icons = MakeFont(13, FW_NORMAL, false, L"Segoe MDL2 Assets");
    wchar_t face[64]{};
    HGDIOBJ old = SelectObject(g_dc, g_fonts.icons);
    GetTextFaceW(g_dc, 64, face);
    SelectObject(g_dc, old);
    g_fonts.haveIcons = _wcsicmp(face, L"Segoe MDL2 Assets") == 0;
    g_fonts.scale = g_scale;
    g_fonts.language = g_language;
}

void Rect(double x0, double y0, double x1, double y1, COLORREF color)
{
    RECT rect{ SX(x0), SY(y0), SX(x1), SY(y1) };
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(g_dc, &rect, brush);
    DeleteObject(brush);
}

void Round(double x0, double y0, double x1, double y1, COLORREF color, double radius)
{
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(g_dc, brush), oldPen = SelectObject(g_dc, GetStockObject(NULL_PEN));
    int d = PX(radius * 2);
    RoundRect(g_dc, SX(x0), SY(y0), SX(x1) + 1, SY(y1) + 1, d, d);
    SelectObject(g_dc, oldBrush);
    SelectObject(g_dc, oldPen);
    DeleteObject(brush);
}

// A colour laid over what is already drawn, alpha out of 255.
void Veil(int x0, int y0, int x1, int y1, COLORREF color, int alpha)
{
    static HDC source;
    static uint32_t* pixel;
    if (source == nullptr)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 1;
        info.bmiHeader.biHeight = 1;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        source = CreateCompatibleDC(nullptr);
        SelectObject(source, CreateDIBSection(source, &info, DIB_RGB_COLORS, (void**)&pixel, nullptr, 0));
    }
    *pixel = (uint32_t(GetRValue(color)) << 16) | (uint32_t(GetGValue(color)) << 8) | GetBValue(color);
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, BYTE(alpha), 0 };
    AlphaBlend(g_dc, x0, y0, x1 - x0, y1 - y0, source, 0, 0, 1, 1, blend);
}

// A panel: rounded, letting the backdrop show through a little, with an
// optional title on its top edge.
void Panel(double x0, double y0, double x1, double y1, COLORREF fill, const wchar_t* tab)
{
    int X0 = SX(x0), Y0 = SY(y0), X1 = SX(x1), Y1 = SY(y1), d = PX(12);
    HRGN region = CreateRoundRectRgn(X0, Y0, X1 + 1, Y1 + 1, d, d);
    SelectClipRgn(g_dc, region);
    Veil(X0, Y0, X1 + 1, Y1 + 1, fill, 212);
    SelectClipRgn(g_dc, nullptr);
    DeleteObject(region);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, PX(1)), kEdge);
    HGDIOBJ oldPen = SelectObject(g_dc, pen), oldBrush = SelectObject(g_dc, GetStockObject(NULL_BRUSH));
    RoundRect(g_dc, X0, Y0, X1 + 1, Y1 + 1, d, d);
    SelectObject(g_dc, oldPen);
    SelectObject(g_dc, oldBrush);
    DeleteObject(pen);
    if (tab != nullptr)
    {
        std::wstring upper = tab;
        CharUpperBuffW(upper.data(), DWORD(upper.size()));
        SelectObject(g_dc, g_fonts.small);
        SIZE size{};
        GetTextExtentPoint32W(g_dc, upper.c_str(), int(upper.size()), &size);
        double w = size.cx / g_scale + 14;
        Round(x0 + 12, y0 - 8, x0 + 12 + w, y0 + 7, RGB(172, 222, 240), 4);
        SetBkMode(g_dc, TRANSPARENT);
        SetTextAlign(g_dc, TA_LEFT | TA_TOP);
        SetTextColor(g_dc, RGB(10, 46, 70));
        TextOutW(g_dc, SX(x0 + 19), SY(y0 - 7), upper.c_str(), int(upper.size()));
    }
}

void Write(HFONT font, double x, double y, const std::wstring& text, COLORREF color, UINT align)
{
    SelectObject(g_dc, font);
    SetBkMode(g_dc, TRANSPARENT);
    SetTextAlign(g_dc, align | TA_TOP);
    SetTextColor(g_dc, color);
    TextOutW(g_dc, SX(x), SY(y), text.c_str(), int(text.size()));
}

// Like Write, but the text must fit in a width: it is drawn narrower when too
// wide, and whatever still does not fit is cut off.
void WriteFit(HFONT font, double x, double y, const std::wstring& text, COLORREF color, UINT align, double width)
{
    SelectObject(g_dc, font);
    SIZE extent{};
    GetTextExtentPoint32W(g_dc, text.c_str(), int(text.size()), &extent);
    HFONT narrow = nullptr;
    if (extent.cx > PX(width) && extent.cx > 0)
    {
        TEXTMETRICW metrics{};
        GetTextMetricsW(g_dc, &metrics);
        LOGFONTW description{};
        GetObjectW(font, sizeof(description), &description);
        double ratio = std::max(0.72, double(PX(width)) / extent.cx * 0.98);
        description.lfWidth = std::max(1, int(metrics.tmAveCharWidth * ratio));
        narrow = CreateFontIndirectW(&description);
        font = narrow;
    }
    double left = align == TA_CENTER ? x - width / 2 : align == TA_RIGHT ? x - width : x;
    HRGN clip = CreateRectRgn(SX(left), SY(y - 3), SX(left + width), SY(y + 24));
    SelectClipRgn(g_dc, clip);
    Write(font, x, y, text, color, align);
    SelectClipRgn(g_dc, nullptr);
    DeleteObject(clip);
    if (narrow != nullptr)
        DeleteObject(narrow);
}

void WriteBlock(HFONT font, double x0, double y0, double x1, double y1, const std::wstring& text, COLORREF color)
{
    SelectObject(g_dc, font);
    SetBkMode(g_dc, TRANSPARENT);
    SetTextAlign(g_dc, TA_LEFT | TA_TOP);
    SetTextColor(g_dc, color);
    RECT rect{ SX(x0), SY(y0), SX(x1), SY(y1) };
    DrawTextW(g_dc, text.c_str(), -1, &rect, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
}

// ---- Backdrop: night sky over a sea of mist, with crystals standing in it;
// computed, in the teal of the game's dialogs. ----

double Hash(int x, int y)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0;
}

double Noise(double x, double y)
{
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    double fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    double a = Hash(ix, iy), b = Hash(ix + 1, iy), c = Hash(ix, iy + 1), d = Hash(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

double Clouds(double x, double y)
{
    double sum = 0, amplitude = 0.5;
    for (int i = 0; i < 4; i++)
    {
        sum += Noise(x, y) * amplitude;
        x = x * 2.03 + 17.1;
        y = y * 2.03 + 5.3;
        amplitude *= 0.5;
    }
    return sum / 0.9375;
}

double Step(double low, double high, double value)
{
    double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// A six-sided crystal standing upright, seen from the side: its width at a
// height, and the brightness of the face a point is on (0 when outside).
struct Crystal { double x, top, bottom, halfWidth, tip, alpha; };
constexpr Crystal kCrystals[] =
{
    { -0.98, 0.30, 0.98, 0.075, 0.13, 0.55 }, { -0.82, 0.52, 1.05, 0.05, 0.09, 0.40 },
    { 0.93, 0.22, 1.02, 0.09, 0.15, 0.55 }, { 1.10, 0.48, 1.05, 0.055, 0.10, 0.40 }, { 0.76, 0.62, 1.05, 0.04, 0.07, 0.30 },
};

double CrystalFace(const Crystal& crystal, double x, double y, double& edge)
{
    if (y < crystal.top || y > crystal.bottom)
        return 0;
    double width = crystal.halfWidth;
    if (y < crystal.top + crystal.tip)
        width *= (y - crystal.top) / crystal.tip;
    else if (y > crystal.bottom - crystal.tip)
        width *= (crystal.bottom - y) / crystal.tip;
    double dx = x - crystal.x;
    if (width <= 0 || std::fabs(dx) > width)
        return 0;
    double side = dx / width; // -1 .. 1
    edge = std::max(0.0, 1 - (1 - std::fabs(side)) * 18);
    bool cap = y < crystal.top + crystal.tip;
    if (side < -0.45)
        return cap ? 0.95 : 0.75;
    if (side < 0.05)
        return cap ? 1.0 : 0.92;
    if (side < 0.5)
        return cap ? 0.7 : 0.55;
    return cap ? 0.5 : 0.35;
}

void BackdropPixel(double x, double y, double* rgb)
{
    // x: from the middle, in screen heights; y: 0 at the top, 1 at the bottom
    static const double stops[4][4] = { { 0.00, 4, 10, 30 }, { 0.42, 10, 40, 74 }, { 0.70, 22, 90, 120 }, { 0.80, 120, 196, 214 } };
    const double horizon = 0.80;
    if (y < horizon)
    {
        int k = 0;
        while (k < 2 && y > stops[k + 1][0])
            k++;
        double f = (y - stops[k][0]) / (stops[k + 1][0] - stops[k][0]);
        for (int c = 0; c < 3; c++)
            rgb[c] = stops[k][c + 1] + (stops[k + 1][c + 1] - stops[k][c + 1]) * f;
        // thin veils of light, and a few stars high up
        double veil = Step(0.55, 0.85, Clouds(x * 2.2 + 3, y * 9)) * (0.10 + 0.30 * y / horizon);
        static const double veilColor[3] = { 150, 220, 236 };
        for (int c = 0; c < 3; c++)
            rgb[c] += (veilColor[c] - rgb[c]) * veil;
        int cellX = int(std::floor((x + 4) * 160)), cellY = int(std::floor(y * 160));
        double star = Hash(cellX, cellY);
        if (star > 0.9965 && y < 0.6)
        {
            double light = (star - 0.9965) / 0.0035 * (1 - y / 0.6);
            for (int c = 0; c < 3; c++)
                rgb[c] += (255 - rgb[c]) * light;
        }
    }
    else
    {
        // the mist sea, seen in perspective and fading into the haze at the horizon
        double p = (y - horizon) / (1 - horizon), z = 1 / (p + 0.06);
        double n = Clouds(x * z * 0.6 + 40, z * 0.9);
        static const double deep[3] = { 8, 40, 62 }, top[3] = { 180, 226, 236 };
        double crest = Step(0.42, 0.75, n);
        for (int c = 0; c < 3; c++)
            rgb[c] = deep[c] + (top[c] - deep[c]) * crest * 0.75;
        double fog = std::exp(-p * 6.5);
        static const double haze[3] = { 120, 196, 214 };
        for (int c = 0; c < 3; c++)
            rgb[c] = (rgb[c] + (haze[c] - rgb[c]) * fog) * (1 - 0.35 * p * p);
    }
    // a soft glow behind the title
    double glow = std::exp(-((x + 0.35) * (x + 0.35) * 3 + (y - 0.05) * (y - 0.05) * 18)) * 0.35;
    static const double glowColor[3] = { 90, 180, 220 };
    for (int c = 0; c < 3; c++)
        rgb[c] += (glowColor[c] - rgb[c]) * glow;
    for (auto& crystal : kCrystals)
    {
        double edge = 0;
        double face = CrystalFace(crystal, x, y, edge);
        if (face <= 0)
            continue;
        static const double ice[3] = { 110, 210, 250 };
        double light[3];
        for (int c = 0; c < 3; c++)
            light[c] = ice[c] * face + (255 - ice[c] * face) * edge * 0.8;
        double a = crystal.alpha * (0.55 + 0.45 * face);
        for (int c = 0; c < 3; c++)
            rgb[c] += (light[c] - rgb[c]) * a;
    }
}

void DrawBackdrop(int width, int height)
{
    static HDC cache;
    static HBITMAP cacheBitmap;
    static int cacheWidth, cacheHeight;
    if (cache == nullptr)
        cache = CreateCompatibleDC(g_dc);
    if (width != cacheWidth || height != cacheHeight)
    {
        // computed at half the size, then enlarged smoothly
        int w = std::max(320, width / 2), h = std::max(200, height / 2);
        std::vector<uint32_t> pixels(size_t(w) * size_t(h));
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                double rgb[3];
                BackdropPixel((x + 0.5 - w / 2.0) / h, (y + 0.5) / h, rgb);
                uint32_t color = 0;
                for (double channel : rgb)
                    color = (color << 8) | uint32_t(std::clamp(int(channel + 0.5), 0, 255));
                pixels[size_t(y) * size_t(w) + size_t(x)] = color;
            }
        }
        HBITMAP bitmap = CreateCompatibleBitmap(g_dc, width, height);
        SelectObject(cache, bitmap);
        if (cacheBitmap != nullptr)
            DeleteObject(cacheBitmap);
        cacheBitmap = bitmap;
        cacheWidth = width;
        cacheHeight = height;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        SetStretchBltMode(cache, HALFTONE);
        SetBrushOrgEx(cache, 0, 0, nullptr);
        StretchDIBits(cache, 0, 0, width, height, 0, 0, w, h, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    }
    BitBlt(g_dc, 0, 0, width, height, cache, 0, 0, SRCCOPY);
}

// ---- Title ----

// "Crystal Defenders" lettered like the game's own captions: serif capitals
// with larger initials, spaced out. Returns where it ends.
double TitlePath(double x, double baseline)
{
    static const struct { const wchar_t* text; bool big; } parts[] = { { L"C", true }, { L"RYSTAL", false }, { L"D", true }, { L"EFENDERS", false } };
    SetBkMode(g_dc, TRANSPARENT);
    SetTextAlign(g_dc, TA_LEFT | TA_BASELINE);
    SetTextCharacterExtra(g_dc, PX(1.6));
    int at = SX(x);
    BeginPath(g_dc);
    for (int i = 0; i < 4; i++)
    {
        SelectObject(g_dc, parts[i].big ? g_fonts.titleBig : g_fonts.titleSmall);
        if (i == 2)
            at += PX(12); // between the two words
        int length = int(wcslen(parts[i].text));
        TextOutW(g_dc, at, SY(baseline), parts[i].text, length);
        SIZE size{};
        GetTextExtentPoint32W(g_dc, parts[i].text, length, &size);
        at += size.cx;
    }
    EndPath(g_dc);
    SetTextCharacterExtra(g_dc, 0);
    return (at - g_originX) / g_scale;
}

void DrawTitle(double x, double baseline)
{
    // soft shadow, then a dark edge, then the lettering, white turning to ice
    HBRUSH shade = CreateSolidBrush(RGB(2, 14, 26)), edgeFill = CreateSolidBrush(RGB(8, 40, 60));
    HPEN edge = CreatePen(PS_SOLID, std::max(2, PX(3)), RGB(6, 30, 48));
    HGDIOBJ oldBrush = SelectObject(g_dc, shade), oldPen = SelectObject(g_dc, GetStockObject(NULL_PEN));
    TitlePath(x + 2.2, baseline + 2.6);
    FillPath(g_dc);
    SelectObject(g_dc, edge);
    SelectObject(g_dc, edgeFill);
    TitlePath(x, baseline);
    StrokeAndFillPath(g_dc);
    double end = TitlePath(x, baseline);
    SelectClipPath(g_dc, RGN_COPY);
    int x0 = SX(x - 4), x1 = SX(end + 4), top = SY(baseline - 36), middle = SY(baseline - 14), bottom = SY(baseline + 4);
    TRIVERTEX vertices[4] =
    {
        { x0, top, 0xFF00, 0xFF00, 0xFF00, 0 }, { x1, middle, 0xFF00, 0xFF00, 0xFF00, 0 },
        { x0, middle, 0xFF00, 0xFF00, 0xFF00, 0 }, { x1, bottom, 0x9C00, 0xE000, 0xF800, 0 },
    };
    GRADIENT_RECT rects[2] = { { 0, 1 }, { 2, 3 } };
    GradientFill(g_dc, vertices, 4, rects, 2, GRADIENT_FILL_RECT_V);
    SelectClipRgn(g_dc, nullptr);
    SelectObject(g_dc, oldBrush);
    SelectObject(g_dc, oldPen);
    DeleteObject(shade);
    DeleteObject(edgeFill);
    DeleteObject(edge);
    // thin rule under the name, fading out to the right
    TRIVERTEX rule[2] = { { SX(x), SY(baseline + 6.5), 0xFF00, 0xFF00, 0xFF00, 0 },
        { SX(end + 40), SY(baseline + 6.5) + std::max(1, PX(1.2)), 0x0800, 0x3000, 0x4800, 0 } };
    GRADIENT_RECT ruleRect{ 0, 1 };
    GradientFill(g_dc, rule, 2, &ruleRect, 1, GRADIENT_FILL_RECT_H);

    // what the project is, beside it
    double sub = end + 30;
    SetTextCharacterExtra(g_dc, PX(2.4));
    Write(g_fonts.name, sub + 1, baseline - 30, L"RECOMP", RGB(2, 14, 26), TA_LEFT);
    Write(g_fonts.name, sub, baseline - 31, L"RECOMP", RGB(255, 255, 255), TA_LEFT);
    SetTextCharacterExtra(g_dc, 0);
    WriteFit(g_fonts.subtitle, sub + 1, baseline - 9, T(T_SUBTITLE), RGB(2, 14, 26), TA_LEFT, 630 - sub);
    WriteFit(g_fonts.subtitle, sub, baseline - 10, T(T_SUBTITLE), RGB(200, 236, 246), TA_LEFT, 630 - sub);
}

// ---- Flags ----

void FlagLine(double x0, double y0, double x1, double y1, double width, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, std::max(1, PX(width)), color);
    HGDIOBJ old = SelectObject(g_dc, pen);
    MoveToEx(g_dc, SX(x0), SY(y0), nullptr);
    LineTo(g_dc, SX(x1), SY(y1));
    SelectObject(g_dc, old);
    DeleteObject(pen);
}

void FlagDisc(double x0, double y0, double x1, double y1, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(g_dc, brush), oldPen = SelectObject(g_dc, GetStockObject(NULL_PEN));
    Ellipse(g_dc, SX(x0), SY(y0), SX(x1) + 1, SY(y1) + 1);
    SelectObject(g_dc, oldBrush);
    SelectObject(g_dc, oldPen);
    DeleteObject(brush);
}

void DrawFlag(int language, double x, double y, bool selected)
{
    double w = kFlagW, h = kFlagH;
    if (selected)
        Rect(x - 2.5, y - 2.5, x + w + 2.5, y + h + 2.5, RGB(255, 255, 255));
    else
        Rect(x - 1, y - 1, x + w + 1, y + h + 1, RGB(6, 24, 36));
    HRGN clip = CreateRectRgn(SX(x), SY(y), SX(x + w), SY(y + h));
    SelectClipRgn(g_dc, clip);
    switch (language)
    {
    case kFr:
        Rect(x, y, x + w / 3, y + h, RGB(0, 38, 153));
        Rect(x + w / 3, y, x + 2 * w / 3, y + h, RGB(255, 255, 255));
        Rect(x + 2 * w / 3, y, x + w, y + h, RGB(230, 40, 50));
        break;
    case kEn:
        Rect(x, y, x + w, y + h, RGB(1, 33, 105));
        FlagLine(x, y, x + w, y + h, 3.0, RGB(255, 255, 255));
        FlagLine(x, y + h, x + w, y, 3.0, RGB(255, 255, 255));
        FlagLine(x, y, x + w, y + h, 1.0, RGB(200, 16, 46));
        FlagLine(x, y + h, x + w, y, 1.0, RGB(200, 16, 46));
        Rect(x + w / 2 - 2.5, y, x + w / 2 + 2.5, y + h, RGB(255, 255, 255));
        Rect(x, y + h / 2 - 2.5, x + w, y + h / 2 + 2.5, RGB(255, 255, 255));
        Rect(x + w / 2 - 1.4, y, x + w / 2 + 1.4, y + h, RGB(200, 16, 46));
        Rect(x, y + h / 2 - 1.4, x + w, y + h / 2 + 1.4, RGB(200, 16, 46));
        break;
    case kDe:
        Rect(x, y, x + w, y + h / 3, RGB(0, 0, 0));
        Rect(x, y + h / 3, x + w, y + 2 * h / 3, RGB(221, 0, 0));
        Rect(x, y + 2 * h / 3, x + w, y + h, RGB(255, 206, 0));
        break;
    case kEs:
        Rect(x, y, x + w, y + h, RGB(198, 11, 30));
        Rect(x, y + h / 4, x + w, y + 3 * h / 4, RGB(255, 196, 0));
        break;
    case kIt:
        Rect(x, y, x + w / 3, y + h, RGB(0, 146, 70));
        Rect(x + w / 3, y, x + 2 * w / 3, y + h, RGB(255, 255, 255));
        Rect(x + 2 * w / 3, y, x + w, y + h, RGB(206, 43, 55));
        break;
    default: // Japan
        Rect(x, y, x + w, y + h, RGB(255, 255, 255));
        FlagDisc(x + w / 2 - h * 0.3, y + h * 0.2, x + w / 2 + h * 0.3, y + h * 0.8, RGB(188, 0, 45));
        break;
    }
    SelectClipRgn(g_dc, nullptr);
    DeleteObject(clip);
}

// ---- Rows ----

double RowY(int row)
{
    return kRowY0 + (row - g.top) * kRowH;
}

void Selection(double x0, double y, double x1, bool focus)
{
    Round(x0, y - 1, x1, y + kRowH - 2, focus ? kFocus : kHere, 4);
}

// A row of a page; value is drawn in a box at the right (switch: -1 none, 0 off, 1 on).
void Row(int row, const std::wstring& label, const std::wstring& value, int switchState = -1, int bar = -1, bool arrows = false)
{
    if (row < g.top || row >= g.top + kVisibleRows)
        return;
    double y = RowY(row);
    bool selected = g.inPage && g.row == row;
    if (selected)
        Selection(kRowX0, y, kRowX1, true);
    bool hasValue = !value.empty() || switchState >= 0 || bar >= 0;
    WriteFit(g_fonts.item, kRowX0 + 10, y, label, selected ? RGB(255, 255, 255) : kText, TA_LEFT,
        hasValue ? kRowX1 - kValueW - kRowX0 - 18 : kRowX1 - kRowX0 - 18);
    if (!hasValue)
        return;
    double x1 = kRowX1 - 6, x0 = x1 - kValueW;
    if (switchState >= 0)
    {
        double right = x1 - 2, left = right - 30, cy = y + 8.5;
        Round(left, cy - 7, right, cy + 7, switchState ? RGB(250, 226, 150) : RGB(30, 70, 96), 7);
        double knob = switchState ? right - 7 : left + 7;
        Round(knob - 5, cy - 5, knob + 5, cy + 5, switchState ? RGB(70, 50, 10) : RGB(200, 226, 236), 5);
        return;
    }
    Round(x0, y, x1, y + kRowH - 4, RGB(6, 26, 40), 4);
    if (bar >= 0)
    {
        double step = (kValueW - 8) / 10;
        for (int i = 0; i < 10; i++)
            Rect(x0 + 4 + i * step, y + 5, x0 + 4 + (i + 1) * step - 1.5, y + 13, i < bar ? kValue : RGB(30, 66, 90));
        return;
    }
    WriteFit(g_fonts.item, (x0 + x1) / 2, y, value, kValue, TA_CENTER, selected && arrows ? kValueW - 30 : kValueW - 6);
    if (selected && arrows)
    {
        Write(g_fonts.item, x0 + 6, y, L"<", RGB(255, 255, 255), TA_LEFT);
        Write(g_fonts.item, x1 - 6, y, L">", RGB(255, 255, 255), TA_RIGHT);
    }
}

// Small marks at the right edge when a page has rows above or below those shown.
void ScrollMarks(int count, int visible)
{
    if (g.top > 0)
        Write(g_fonts.small, kRowX1 - 2, kRowY0 - 13, L"▲", kTextDim, TA_RIGHT);
    if (g.top + visible < count)
        Write(g_fonts.small, kRowX1 - 2, kPanelY1 - 15, L"▼", kTextDim, TA_RIGHT);
}

// ---- Pictures (screenshots) ----

struct Picture
{
    std::filesystem::path path;
    HBITMAP bitmap{};
    int width{}, height{};
} g_picture;

bool LoadPicture(const std::filesystem::path& path)
{
    if (g_picture.bitmap != nullptr && g_picture.path == path)
        return true;
    if (g_picture.bitmap != nullptr)
        DeleteObject(g_picture.bitmap);
    g_picture = {};
    g_picture.path = path;
    g_picture.bitmap = (HBITMAP)LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    BITMAP info{};
    if (g_picture.bitmap == nullptr || !GetObjectW(g_picture.bitmap, sizeof(info), &info))
        return false;
    g_picture.width = info.bmWidth;
    g_picture.height = std::abs(info.bmHeight);
    return true;
}

// The picture inside a box, keeping its proportions, with a thin frame.
void DrawPicture(double x0, double y0, double x1, double y1, bool middle)
{
    double boxW = x1 - x0, boxH = y1 - y0;
    double w = boxW, h = boxW * g_picture.height / std::max(1, g_picture.width);
    if (h > boxH)
    {
        h = boxH;
        w = boxH * g_picture.width / std::max(1, g_picture.height);
    }
    double left = x0 + (boxW - w) / 2, top = middle ? y0 + (boxH - h) / 2 : y0;
    Rect(left - 1, top - 1, left + w + 1, top + h + 1, RGB(150, 210, 230));
    HDC source = CreateCompatibleDC(g_dc);
    HGDIOBJ old = SelectObject(source, g_picture.bitmap);
    SetStretchBltMode(g_dc, HALFTONE);
    SetBrushOrgEx(g_dc, 0, 0, nullptr);
    StretchBlt(g_dc, SX(left), SY(top), SX(left + w) - SX(left), SY(top + h) - SY(top), source, 0, 0, g_picture.width, g_picture.height, SRCCOPY);
    SelectObject(source, old);
    DeleteDC(source);
}

// Without any screenshot yet: one large crystal, as on the program's icon.
void DrawEmblem(double cx, double cy, double size)
{
    auto point = [&](double x, double y) { return POINT{ SX(cx + x * size), SY(cy + y * size) }; };
    struct Face { POINT p[4]; int n; COLORREF color; };
    const Face faces[] =
    {
        { { point(0, -1), point(-0.42, -0.45), point(0, -0.3) }, 3, RGB(200, 244, 255) },
        { { point(0, -1), point(0, -0.3), point(0.42, -0.45) }, 3, RGB(120, 200, 236) },
        { { point(-0.42, -0.45), point(0, -0.3), point(0, 1), point(-0.42, 0.45) }, 4, RGB(110, 196, 236) },
        { { point(0, -0.3), point(0.42, -0.45), point(0.42, 0.45), point(0, 1) }, 4, RGB(40, 120, 176) },
    };
    HPEN pen = CreatePen(PS_SOLID, std::max(1, PX(1.2)), RGB(230, 250, 255));
    HGDIOBJ oldPen = SelectObject(g_dc, pen);
    for (auto& face : faces)
    {
        HBRUSH brush = CreateSolidBrush(face.color);
        HGDIOBJ oldBrush = SelectObject(g_dc, brush);
        Polygon(g_dc, face.p, face.n);
        SelectObject(g_dc, oldBrush);
        DeleteObject(brush);
    }
    SelectObject(g_dc, oldPen);
    DeleteObject(pen);
}

// ---- Pages ----

void DrawPage(int section)
{
    const Config& config = GetConfig();
    switch (section)
    {
    case kLanguagePage:
    {
        Row(0, T(T_AUTO_LANGUAGE), config.language == 0 ? L"✓" : L"");
        for (int i = 0; i < kLanguageCount; i++)
            Row(1 + i, kLanguageNames[i], config.language == kLanguageIds[i] ? L"✓" : L"");
        Row(1 + kLanguageCount, T(T_BACK), L"");
        break;
    }
    case kPicture:
    {
        SIZE size{ LONG(config.windowWidth), LONG(config.windowHeight) };
        Row(kRowFullscreen, T(T_FULLSCREEN), L"", config.fullscreen);
        Row(kRowWindow, T(T_WINDOW), SizeText(size), -1, -1, true);
        Row(kRowRender, T(T_RENDER), T(config.sharp ? T_SHARP : T_SMOOTH), -1, -1, true);
        Row(kPictureRows, T(T_BACK), L"");
        break;
    }
    case kSound:
        Row(0, T(T_VOLUME), L"", -1, (config.volume + 5) / 10);
        Row(1, T(T_BACK), L"");
        break;
    case kOptions:
        Row(kRowVibration, T(T_VIBRATION), L"", config.vibration);
        Row(kRowBackgroundPause, T(T_BACKGROUND_PAUSE), L"", config.pauseInBackground);
        Row(kRowLive, T(T_LIVE), L"", config.liveWarning);
        Row(kRowStorage, T(T_STORAGE), L"", config.storageDialog);
        Row(kOptionRows, T(T_BACK), L"");
        break;
    case kControls:
        for (int i = 0; i < kButtonCount; i++)
        {
            std::wstring label = kButtons[i].label ? kButtons[i].label : T(T_UP + i);
            Row(i, label, g.waitKey == i ? T(T_PRESS) : ButtonKeysText(kButtons[i].button));
        }
        Row(kButtonCount, T(T_DEFAULTS), L"");
        Row(kButtonCount + 1, T(T_BACK), L"");
        ScrollMarks(kButtonCount + 2, kVisibleRows);
        break;
    case kAchievements:
    {
        const auto& list = Achievements();
        for (int i = g.top; i < int(list.size()) && i < g.top + kVisibleEntries; i++)
        {
            double y = kRowY0 + (i - g.top) * kEntryH;
            if (g.inPage && g.row == i)
                Round(kRowX0, y - 2, kRowX1, y + kEntryH - 3, kFocus, 4);
            const auto& entry = list[size_t(i)];
            // a lit or dim gem in front of the name
            double gx = kRowX0 + 12, gy = y + 6.5;
            POINT gem[4] = { { SX(gx), SY(gy - 6) }, { SX(gx + 5), SY(gy) }, { SX(gx), SY(gy + 6) }, { SX(gx - 5), SY(gy) } };
            HBRUSH brush = CreateSolidBrush(entry.unlocked ? RGB(120, 220, 250) : RGB(40, 72, 92));
            HGDIOBJ oldBrush = SelectObject(g_dc, brush), oldPen = SelectObject(g_dc, GetStockObject(NULL_PEN));
            Polygon(g_dc, gem, 4);
            SelectObject(g_dc, oldBrush);
            SelectObject(g_dc, oldPen);
            DeleteObject(brush);
            COLORREF color = entry.unlocked ? RGB(255, 255, 255) : kTextDim;
            WriteFit(g_fonts.item, kRowX0 + 24, y - 2.5, entry.name, color, TA_LEFT, kRowX1 - kRowX0 - 80);
            Write(g_fonts.item, kRowX1 - 8, y - 2.5, std::to_wstring(entry.score) + L" G", entry.unlocked ? kValue : kTextDim, TA_RIGHT);
            WriteFit(g_fonts.small, kRowX0 + 24, y + 13.5, entry.text, entry.unlocked ? kText : RGB(130, 160, 176), TA_LEFT, kRowX1 - kRowX0 - 32);
        }
        if (g.top > 0)
            Write(g_fonts.small, kRowX1 - 2, kRowY0 - 13, L"▲", kTextDim, TA_RIGHT);
        if (g.top + kVisibleEntries < int(list.size()))
            Write(g_fonts.small, kRowX1 - 2, kPanelY1 - 15, L"▼", kTextDim, TA_RIGHT);
        break;
    }
    case kShots:
    {
        if (g_shots.empty())
        {
            WriteBlock(g_fonts.item, kRowX0 + 10, kRowY0 + 4, kRowX1 - 10, kPanelY1 - 10, T(T_NO_SHOTS), kText);
            break;
        }
        const double listX1 = 352;
        for (int i = g.top; i < int(g_shots.size()) && i < g.top + kVisibleRows; i++)
        {
            double y = RowY(i);
            if (g.inPage && g.row == i)
                Selection(kRowX0, y, listX1, true);
            WriteFit(g_fonts.item, kRowX0 + 10, y, g_shots[size_t(i)].label, g.inPage && g.row == i ? RGB(255, 255, 255) : kText, TA_LEFT, listX1 - kRowX0 - 16);
        }
        if (g.top > 0)
            Write(g_fonts.small, listX1 - 2, kRowY0 - 13, L"▲", kTextDim, TA_RIGHT);
        if (g.top + kVisibleRows < int(g_shots.size()))
            Write(g_fonts.small, listX1 - 2, kPanelY1 - 15, L"▼", kTextDim, TA_RIGHT);
        int shown = g.inPage ? g.row : 0;
        if (LoadPicture(g_shots[size_t(shown)].path))
            DrawPicture(362, kRowY0 + 1, kRowX1, kPanelY1 - 12, false);
        break;
    }
    default: // the preview: the latest screenshot, or the emblem
        if (!g_shots.empty() && LoadPicture(g_shots[0].path))
            DrawPicture(kRowX0 + 8, kRowY0 + 2, kRowX1 - 8, kPanelY1 - 12, true);
        else
            DrawEmblem((kPanelX0 + kPanelX1) / 2, (kPanelY0 + kPanelY1) / 2 + 4, 100);
        break;
    }
}

std::wstring HelpText()
{
    if (g.note >= 0)
        return T(g.note);
    if (!g.inPage || g.section == kAchievements)
    {
        std::wstring text = T(H_PLAY + g.section);
        if (g.section == kAchievements)
        {
            int earned = 0, points = 0, total = 0;
            for (auto& entry : Achievements())
            {
                earned += entry.unlocked;
                points += entry.unlocked ? int(entry.score) : 0;
                total += int(entry.score);
            }
            wchar_t line[160];
            swprintf(line, 160, T(H_ACHIEVEMENT_TOTAL), earned, int(Achievements().size()), points, total);
            text += L"\n";
            text += line;
        }
        return text;
    }
    if (g.row == RowCount(g.section) - 1 && g.section != kShots)
        return T(H_BACK);
    switch (g.section)
    {
    case kLanguagePage: return T(H_LANGUAGE);
    case kPicture: return T(g.row == kRowFullscreen ? H_FULLSCREEN : g.row == kRowWindow ? H_WINDOW : H_RENDER);
    case kSound: return T(H_VOLUME);
    case kOptions: return T(H_VIBRATION + g.row);
    case kControls: return T(g.row == kButtonCount ? H_DEFAULTS : H_KEY);
    case kShots: return T(g.deleteArmed == g.row ? H_SHOT_DELETE : H_SHOT);
    }
    return {};
}

bool g_padSeen; // the hints at the bottom show the controller's buttons

void Paint(HDC dc, int width, int height)
{
    g_dc = dc;
    g_scale = std::min(width / 640.0, height / 400.0);
    g_originX = int((width - 640 * g_scale) / 2);
    g_originY = int((height - 400 * g_scale) / 2);
    PrepareFonts();
    DrawBackdrop(width, height);
    DrawTitle(16, 42);

    // the bar
    Panel(kSideX0, kPanelY0, kSideX1, kHelpY1, RGB(4, 22, 34), nullptr);
    for (int i = 0; i < kSectionCount; i++)
    {
        double y = i < kTopSections ? kRowY0 + i * kRowH : kHelpY1 - 6 - (kSectionCount - i) * kRowH;
        bool focus = !g.inPage && g.section == i, here = g.inPage && g.section == i;
        if (focus || here)
            Selection(kSideX0 + 4, y, kSideX1 - 4, focus);
        COLORREF color = focus || here ? RGB(255, 255, 255) : RGB(190, 214, 226);
        double labelX = kSideX0 + 12;
        if (g_fonts.haveIcons)
        {
            Write(g_fonts.icons, kSideX0 + 12, y + 2.5, std::wstring(1, kIcons[i]), color, TA_LEFT);
            labelX = kSideX0 + 32;
        }
        WriteFit(g_fonts.item, labelX, y, T(T_PLAY + i), color, TA_LEFT, kSideX1 - labelX - 6);
    }

    // the page of the selected section, shown as soon as it is selected in the bar
    bool page = HasPage(g.section);
    Panel(kPanelX0, kPanelY0, kPanelX1, kPanelY1, RGB(10, 44, 64), page ? T(T_PLAY + g.section) : T(T_PREVIEW));
    DrawPage(page ? g.section : kPlay);

    Panel(kPanelX0, kHelpY0, kPanelX1, kHelpY1, RGB(10, 44, 64), nullptr);
    WriteBlock(g_fonts.help, kPanelX0 + 12, kHelpY0 + 7, kPanelX1 - 12, kHelpY1 - 4, HelpText(), kText);

    Write(g_fonts.small, 18, 380, T(g_padSeen ? T_HINT_PAD : T_HINT_KEYS), RGB(2, 14, 26), TA_LEFT);
    Write(g_fonts.small, 17, 379, T(g_padSeen ? T_HINT_PAD : T_HINT_KEYS), RGB(236, 246, 250), TA_LEFT);
    for (int i = 0; i < kLanguageCount; i++)
        DrawFlag(i, FlagX(i), kFlagY, g_language == i);
}

// ---- Input ----

void KeepRowShown()
{
    int visible = g.section == kAchievements ? kVisibleEntries : kVisibleRows;
    if (g.row < g.top)
        g.top = g.row;
    if (g.row >= g.top + visible)
        g.top = g.row - visible + 1;
}

void SelectSection(int section)
{
    g.section = section;
    g.inPage = false;
    g.row = 0;
    g.top = 0;
}

void EnterPage()
{
    if (!HasPage(g.section) || RowCount(g.section) == 0)
        return;
    g.inPage = true;
    g.row = 0;
    g.top = 0;
}

void OpenFolder()
{
    wchar_t folder[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, folder);
    ShellExecuteW(g_window, L"open", folder, nullptr, nullptr, SW_SHOWNORMAL);
}

void DeleteShot()
{
    if (g.deleteArmed != g.row)
    {
        g.deleteArmed = g.row;
        return;
    }
    std::wstring path = std::filesystem::absolute(g_shots[size_t(g.row)].path).wstring();
    path.push_back(L'\0'); // the list of files ends with an empty name
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = g_window;
    operation.wFunc = FO_DELETE;
    operation.pFrom = path.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (g_picture.bitmap != nullptr)
    {
        DeleteObject(g_picture.bitmap);
        g_picture = {};
    }
    SHFileOperationW(&operation);
    g.deleteArmed = -1;
    ReadShots();
    if (g_shots.empty())
        SelectSection(kShots);
    else
        g.row = std::min(g.row, int(g_shots.size()) - 1);
    KeepRowShown();
}

void Activate()
{
    if (!g.inPage)
    {
        if (g.section == kPlay)
            g.result = 1;
        else if (g.section == kQuit)
            g.result = 2;
        else if (g.section == kFolder)
            OpenFolder();
        else
            EnterPage();
        return;
    }
    if (g.row == RowCount(g.section) - 1 && g.section != kShots && g.section != kAchievements)
    {
        g.inPage = false;
        return;
    }
    switch (g.section)
    {
    case kLanguagePage:
        SetLanguage(g.row == 0 ? 0 : kLanguageIds[g.row - 1]);
        break;
    case kControls:
        if (g.row < kButtonCount)
            g.waitKey = g.row;
        else
            DefaultKeys();
        break;
    case kShots:
        ShellExecuteW(g_window, L"open", g_shots[size_t(g.row)].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default:
        ChangeValue(0);
        break;
    }
}

void Move(int step)
{
    if (!g.inPage)
    {
        g.section = (g.section + step + kSectionCount) % kSectionCount;
        return;
    }
    int count = RowCount(g.section);
    if (count == 0)
        return;
    g.row = (g.row + step + count) % count;
    KeepRowShown();
}

void Key(int key)
{
    if (g.waitKey >= 0)
    {
        if (key == VK_F1 || key == VK_F11 || key == VK_F12 || (key != VK_ESCAPE && !BindKey(g.waitKey, key)))
            g.note = H_RESERVED;
        g.waitKey = -1;
        return;
    }
    int armed = g.deleteArmed;
    g.deleteArmed = -1;
    g.note = -1;
    switch (key)
    {
    case VK_UP: Move(-1); break;
    case VK_DOWN: Move(1); break;
    case VK_LEFT:
        if (g.inPage && !ChangeValue(-1))
            g.inPage = false;
        break;
    case VK_RIGHT:
        if (!g.inPage)
            EnterPage();
        else
            ChangeValue(1);
        break;
    case VK_RETURN:
    case VK_SPACE:
        Activate();
        break;
    case VK_ESCAPE:
    case VK_BACK:
        if (g.inPage)
            g.inPage = false;
        else
            g.section = kQuit;
        break;
    case VK_DELETE:
        if (g.inPage && g.section == kShots)
        {
            g.deleteArmed = armed;
            DeleteShot();
        }
        break;
    }
}

// The bar entry or the page row under the mouse; false when there is none.
bool HitTest(int mouseX, int mouseY, int& section, int& row, int& flag)
{
    double x = (mouseX - g_originX) / g_scale, y = (mouseY - g_originY) / g_scale;
    section = row = flag = -1;
    for (int i = 0; i < kLanguageCount; i++)
    {
        if (x >= FlagX(i) - 2 && x <= FlagX(i) + kFlagW + 2 && y >= kFlagY - 2 && y <= kFlagY + kFlagH + 2)
            flag = i;
    }
    if (flag >= 0)
        return true;
    if (x >= kSideX0 && x <= kSideX1)
    {
        for (int i = 0; i < kSectionCount; i++)
        {
            double top = i < kTopSections ? kRowY0 + i * kRowH : kHelpY1 - 6 - (kSectionCount - i) * kRowH;
            if (y >= top - 1 && y < top + kRowH - 1)
                section = i;
        }
        return section >= 0;
    }
    if (!HasPage(g.section) || x < kRowX0 || x > kRowX1 || y < kRowY0 || y > kPanelY1 - 6)
        return false;
    if (g.section == kShots && x > 352)
        return false;
    double height = g.section == kAchievements ? kEntryH : kRowH;
    int index = g.top + int((y - kRowY0) / height);
    if (index >= RowCount(g.section))
        return false;
    row = index;
    return true;
}

void Mouse(int x, int y, bool click)
{
    int section, row, flag;
    if (!HitTest(x, y, section, row, flag))
        return;
    if (flag >= 0)
    {
        if (click)
            SetLanguage(kLanguageIds[flag]);
        return;
    }
    if (section >= 0)
    {
        if (section != g.section || g.inPage)
            SelectSection(section);
        if (click)
            Activate();
        return;
    }
    g.inPage = true;
    if (g.row != row)
        g.deleteArmed = -1;
    g.row = row;
    if (click && g.waitKey < 0)
        Activate();
}

// ---- Controller ----

struct PadState
{
    uint32_t packet;
    uint16_t buttons;
    uint8_t leftTrigger, rightTrigger;
    int16_t thumbLX, thumbLY, thumbRX, thumbRY;
};
using GetStateFunction = DWORD(WINAPI*)(DWORD, PadState*);

GetStateFunction PadFunction()
{
    static GetStateFunction function = []() -> GetStateFunction
    {
        for (const wchar_t* library : { L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll" })
        {
            if (HMODULE module = LoadLibraryW(library))
                return (GetStateFunction)(void*)GetProcAddress(module, "XInputGetState");
        }
        return nullptr;
    }();
    return function;
}

// Turns the controllers' buttons into the keys above, repeating a held direction.
bool PollPad()
{
    GetStateFunction getState = PadFunction();
    if (getState == nullptr)
        return false;
    uint16_t buttons = 0;
    for (DWORD i = 0; i < 4; i++)
    {
        PadState state{};
        if (getState(i, &state) != ERROR_SUCCESS)
            continue;
        buttons |= state.buttons;
        if (state.thumbLY > 16000) buttons |= 0x0001;
        if (state.thumbLY < -16000) buttons |= 0x0002;
        if (state.thumbLX < -16000) buttons |= 0x0004;
        if (state.thumbLX > 16000) buttons |= 0x0008;
    }
    static uint16_t previous = 0xFFFF; // buttons held when the screen opens are ignored
    static ULONGLONG repeatAt;
    if (previous == 0xFFFF)
    {
        previous = buttons;
        return false;
    }
    uint16_t pressed = buttons & ~previous;
    previous = buttons;
    ULONGLONG now = GetTickCount64();
    static const struct { uint16_t button; int key; } map[] =
    {
        { 0x0001, VK_UP }, { 0x0002, VK_DOWN }, { 0x0004, VK_LEFT }, { 0x0008, VK_RIGHT }, { 0x1000, VK_RETURN }, { 0x2000, VK_ESCAPE },
    };
    bool acted = false;
    if (pressed != 0)
    {
        g_padSeen = true;
        repeatAt = now + 400;
        if (pressed & 0x0010) // Start
        {
            g.result = 1;
            return true;
        }
    }
    else if ((buttons & 0x000F) != 0 && now >= repeatAt)
    {
        pressed = buttons & 0x000F;
        repeatAt = now + 90;
    }
    for (auto& entry : map)
    {
        if (pressed & entry.button)
        {
            if (g.waitKey >= 0) // a key is awaited: the controller cancels
                g.waitKey = -1;
            else
                Key(entry.key);
            acted = true;
        }
    }
    return acted;
}

// ---- Window ----

bool g_dirty = true;
bool g_fullscreen;
WINDOWPLACEMENT g_placement{ sizeof(WINDOWPLACEMENT) };

void ToggleFullscreen()
{
    DWORD style = DWORD(GetWindowLongW(g_window, GWL_STYLE));
    if (!g_fullscreen)
    {
        MONITORINFO monitor{ sizeof(MONITORINFO) };
        GetWindowPlacement(g_window, &g_placement);
        GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongW(g_window, GWL_STYLE, LONG(style & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(g_window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top, monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    else
    {
        SetWindowLongW(g_window, GWL_STYLE, LONG(style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(g_window, &g_placement);
        SetWindowPos(g_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_fullscreen = !g_fullscreen;
}

void PaintWindow(HDC target)
{
    RECT client{};
    GetClientRect(g_window, &client);
    if (client.right <= 0 || client.bottom <= 0)
        return;
    static HDC memory;
    static HBITMAP bitmap;
    static int width, height;
    if (memory == nullptr)
        memory = CreateCompatibleDC(target);
    if (client.right != width || client.bottom != height)
    {
        HBITMAP fresh = CreateCompatibleBitmap(target, client.right, client.bottom);
        SelectObject(memory, fresh);
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        bitmap = fresh;
        width = client.right;
        height = client.bottom;
    }
    Paint(memory, width, height);
    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam == VK_F11 || (wParam == VK_RETURN && (lParam & (1 << 29)) != 0))
        {
            if ((lParam & (1 << 30)) == 0)
                ToggleFullscreen();
        }
        else if (message == WM_KEYDOWN || g.waitKey >= 0)
        {
            Key(int(wParam));
        }
        else
        {
            break;
        }
        g_dirty = true;
        return 0;
    case WM_SYSCHAR:
        return 0; // no beep for Alt+Enter
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    {
        static LPARAM last = -1;
        if (message == WM_MOUSEMOVE && lParam == last)
            return 0;
        last = lParam;
        g_padSeen = false;
        if (g.waitKey < 0)
        {
            Mouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), message == WM_LBUTTONDOWN);
            g_dirty = true;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (g.inPage)
            Move(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
        g_dirty = true;
        return 0;
    case WM_SIZE:
        g_dirty = true;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        PaintWindow(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ACTIVATE:
        g_dirty = true;
        break;
    case WM_CLOSE:
        g.result = 2;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Draws the screen once into a BMP file, after a list of keys (test hook).
bool SaveShot(const char* path)
{
    int width = 1280, height = 800;
    if (const char* size = getenv("CD_LAUNCHER_SIZE"))
        sscanf(size, "%dx%d", &width, &height);
    if (const char* keys = getenv("CD_LAUNCHER_KEYS"))
    {
        static const struct { const char* name; int key; } names[] =
        {
            { "up", VK_UP }, { "down", VK_DOWN }, { "left", VK_LEFT }, { "right", VK_RIGHT }, { "enter", VK_RETURN },
            { "escape", VK_ESCAPE }, { "delete", VK_DELETE },
        };
        std::string list = keys;
        size_t position = 0;
        while (position <= list.size())
        {
            size_t end = list.find(',', position);
            if (end == std::string::npos)
                end = list.size();
            std::string name = list.substr(position, end - position);
            for (auto& entry : names)
            {
                if (name == entry.name)
                    Key(entry.key);
            }
            position = end + 1;
        }
    }
    if (getenv("CD_LAUNCHER_PAD") != nullptr)
        g_padSeen = true;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bitmap);
    Paint(dc, width, height);
    GdiFlush();
    bool saved = false;
    if (FILE* file = fopen(path, "wb"))
    {
        BITMAPFILEHEADER header{};
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + DWORD(width * height * 4);
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&info.bmiHeader, sizeof(BITMAPINFOHEADER), 1, file);
        fwrite(bits, size_t(width) * size_t(height) * 4, 1, file);
        saved = fclose(file) == 0;
    }
    DeleteObject(bitmap);
    DeleteDC(dc);
    Log("Launcher: test picture %s %s", path, saved ? "written" : "NOT written");
    return false;
}

} // namespace

bool LauncherRun(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--no-launcher") == 0)
            return true;
    }
    // Not for a relaunch from the in-game menu, nor for the automatic tests.
    for (const char* variable : { "CD_RESUME", "CD_AUTOPAD", "CD_HEADLESS", "CD_SCREENSHOT", "CD_AUDIT", "CD_TEST_PAD" })
    {
        if (getenv(variable) != nullptr)
            return true;
    }
    g_language = UiLanguage();
    ReadShots();
    if (const char* shot = getenv("CD_LAUNCHER_SHOT"))
        return SaveShot(shot);
    if (!GetConfig().launcher)
        return true;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetAwareness = BOOL(WINAPI*)(HANDLE);
    if (auto setAwareness = (SetAwareness)(void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
        setAwareness((HANDLE)(INT_PTR)-4); // per monitor, as the game's window

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); // resources/crystal_defenders.rc
    windowClass.lpszClassName = L"CrystalDefendersLauncher";
    RegisterClassW(&windowClass);

    // 16:10, as large as 1280 x 800 at the screen's scale allows on its work area
    POINT origin{ 0, 0 };
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{ sizeof(MONITORINFO) };
    GetMonitorInfoW(monitor, &info);
    int areaW = info.rcWork.right - info.rcWork.left, areaH = info.rcWork.bottom - info.rcWork.top;
    HDC screen = GetDC(nullptr);
    int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    int width = std::min(MulDiv(1120, dpi, 96), areaW * 85 / 100);
    int height = width * 10 / 16;
    if (height > areaH * 85 / 100)
    {
        height = areaH * 85 / 100;
        width = height * 16 / 10;
    }
    RECT rect{ 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    int outerW = rect.right - rect.left, outerH = rect.bottom - rect.top;
    g_window = CreateWindowExW(0, windowClass.lpszClassName, L"Crystal Defenders", WS_OVERLAPPEDWINDOW,
        info.rcWork.left + (areaW - outerW) / 2, info.rcWork.top + (areaH - outerH) / 2, outerW, outerH, nullptr, nullptr, instance, nullptr);
    if (g_window == nullptr)
        return true;
    if (GetConfig().fullscreen || OnSteamDeck())
        ToggleFullscreen();
    ShowWindow(g_window, SW_SHOW);
    SetForegroundWindow(g_window);

    while (g.result == 0)
    {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (PollPad())
            g_dirty = true;
        if (g_dirty && g.result == 0)
        {
            g_dirty = false;
            HDC dc = GetDC(g_window);
            PaintWindow(dc);
            ReleaseDC(g_window, dc);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
    DestroyWindow(g_window);
    g_window = nullptr;
    if (g_picture.bitmap != nullptr)
        DeleteObject(g_picture.bitmap);
    Log("Launcher: %s", g.result == 1 ? "play" : "quit");
    return g.result == 1;
}

#endif
