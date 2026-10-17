// German, Spanish, Italian and Japanese for the runtime's own texts, keyed by
// their English version (see translations.h).
#include "translations.h"

#include "xex_header.h"

#include <cwchar>

namespace
{

struct Entry
{
    const wchar_t* english;
    const wchar_t* german;
    const wchar_t* spanish;
    const wchar_t* italian;
    const wchar_t* japanese;
};

const Entry kEntries[] =
{
    // ---- settings menu (F1, right click, both sticks)
    { L"On", L"Ein", L"Sí", L"Sì", L"オン" },
    { L"Off", L"Aus", L"No", L"No", L"オフ" },
    { L"Automatic", L"Automatisch", L"Automático", L"Automatica", L"自動" },
    { L"Pause", L"Pause", L"Pausa", L"Pausa", L"一時停止" },
    { L"The game will resume when its window is active again.", L"Das Spiel geht weiter, sobald sein Fenster wieder aktiv ist.",
        L"El juego continuará cuando su ventana vuelva a estar activa.", L"Il gioco riprenderà quando la sua finestra sarà di nuovo attiva.",
        L"ウィンドウが再びアクティブになると、ゲームが再開します。" },
    { L"Language", L"Sprache", L"Idioma", L"Lingua", L"言語" },
    { L"Resolution", L"Auflösung", L"Resolución", L"Risoluzione", L"解像度" },
    { L"  (full screen)", L"  (Vollbild)", L"  (pantalla completa)", L"  (schermo intero)", L"（フルスクリーン）" },
    { L"The game will go back to the title screen to change the language. A game in progress that is not saved will be lost.",
        L"Das Spiel kehrt zum Wechseln der Sprache zum Titelbildschirm zurück. Ein nicht gespeichertes laufendes Spiel geht verloren.",
        L"El juego volverá a la pantalla de título para cambiar el idioma. Se perderá la partida en curso que no esté guardada.",
        L"Il gioco tornerà alla schermata del titolo per cambiare lingua. Una partita in corso non salvata andrà persa.",
        L"言語を変更するため、タイトル画面に戻ります。セーブしていないプレイ中のデータは失われます。" },
    { L"Change the language", L"Sprache wechseln", L"Cambiar el idioma", L"Cambia lingua", L"言語を変更する" },
    { L"Cancel", L"Abbrechen", L"Cancelar", L"Annulla", L"キャンセル" },
    { L"Quit", L"Beenden", L"Salir", L"Esci", L"終了" },
    { L"Quit the game? A game in progress that is not saved will be lost.",
        L"Das Spiel beenden? Ein nicht gespeichertes laufendes Spiel geht verloren.",
        L"¿Salir del juego? Se perderá la partida en curso que no esté guardada.",
        L"Uscire dal gioco? Una partita in corso non salvata andrà persa.",
        L"ゲームを終了しますか？セーブしていないプレイ中のデータは失われます。" },
    { L"Quit the game", L"Spiel beenden", L"Salir del juego", L"Esci dal gioco", L"ゲームを終了" },
    { L"Settings", L"Einstellungen", L"Ajustes", L"Impostazioni", L"設定" },
    { L"Rendering", L"Darstellung", L"Renderizado", L"Resa", L"描画" },
    { L"Sharp", L"Scharf", L"Nítido", L"Nitida", L"くっきり" },
    { L"Smooth", L"Weich", L"Suave", L"Morbida", L"なめらか" },
    { L"Full screen", L"Vollbild", L"Pantalla completa", L"Schermo intero", L"フルスクリーン" },
    { L"Volume", L"Lautstärke", L"Volumen", L"Volume", L"音量" },
    { L"Vibration", L"Vibration", L"Vibración", L"Vibrazione", L"振動" },
    { L"Pause in background", L"Pause im Hintergrund", L"Pausa en segundo plano", L"Pausa in background", L"バックグラウンドで一時停止" },
    { L"Resume", L"Fortsetzen", L"Continuar", L"Riprendi", L"再開" },
    { L"Back", L"Zurück", L"Volver", L"Indietro", L"戻る" },
    { L"Confirm", L"Bestätigen", L"Confirmar", L"Conferma", L"決定" },

    // ---- achievements
    { L"Achievement unlocked", L"Erfolg freigeschaltet", L"Logro desbloqueado", L"Obiettivo sbloccato", L"実績のロックが解除されました" },
    { L" achievements unlocked", L" Erfolge freigeschaltet", L" logros desbloqueados", L" obiettivi sbloccati", L" 件の実績のロックが解除されました" },
    { L"Achievements: ", L"Erfolge: ", L"Logros: ", L"Obiettivi: ", L"実績：" },
    { L"No achievements defined.", L"Keine Erfolge festgelegt.", L"No hay logros definidos.", L"Nessun obiettivo definito.", L"実績はありません。" },

    // ---- first launch, without the game's data
    { L"Crystal Defenders - setup", L"Crystal Defenders – Installation", L"Crystal Defenders – instalación", L"Crystal Defenders – installazione",
        L"Crystal Defenders - インストール" },
    { L"Choose the folder of your copy of Crystal Defenders (Xbox 360): the \"CRYSTAL DEFENDERS\" or \"58410989\" folder copied from the console, or a folder holding default.xex.",
        L"Wählen Sie den Ordner Ihrer Kopie von Crystal Defenders (Xbox 360): den von der Konsole kopierten Ordner „CRYSTAL DEFENDERS“ oder „58410989“ oder einen Ordner mit default.xex.",
        L"Elige la carpeta de tu copia de Crystal Defenders (Xbox 360): la carpeta «CRYSTAL DEFENDERS» o «58410989» copiada de la consola, o una carpeta con default.xex.",
        L"Scegli la cartella della tua copia di Crystal Defenders (Xbox 360): la cartella «CRYSTAL DEFENDERS» o «58410989» copiata dalla console, o una cartella con default.xex.",
        L"お持ちの Crystal Defenders（Xbox 360）のフォルダーを選んでください：本体からコピーした「CRYSTAL DEFENDERS」または「58410989」フォルダー、または default.xex を含むフォルダー。" },
    { L"The game is not installed.\n\nCopy your console's CRYSTAL DEFENDERS (or 58410989) folder next to this program, then start it again.",
        L"Das Spiel ist nicht installiert.\n\nKopieren Sie den Ordner CRYSTAL DEFENDERS (oder 58410989) Ihrer Konsole neben dieses Programm und starten Sie es erneut.",
        L"El juego no está instalado.\n\nCopia la carpeta CRYSTAL DEFENDERS (o 58410989) de tu consola junto a este programa y vuelve a iniciarlo.",
        L"Il gioco non è installato.\n\nCopia la cartella CRYSTAL DEFENDERS (o 58410989) della tua console accanto a questo programma, poi avvialo di nuovo.",
        L"ゲームがインストールされていません。\n\n本体の CRYSTAL DEFENDERS（または 58410989）フォルダーをこのプログラムの横にコピーしてから、もう一度起動してください。" },
    { L"Welcome!\n\n"
      L"This program runs Crystal Defenders (Xbox 360) on PC, but it does not contain the game's data. "
      L"It needs your own copy of the game: the \"CRYSTAL DEFENDERS\" (or \"58410989\") folder as found on the console's "
      L"storage.\n\n"
      L"Click OK to choose that folder. The game will be installed next to the program, once.",
        L"Willkommen!\n\nDieses Programm führt Crystal Defenders (Xbox 360) auf dem PC aus, enthält aber nicht die Spieldaten. "
        L"Es braucht Ihre eigene Kopie des Spiels: den Ordner „CRYSTAL DEFENDERS“ (oder „58410989“), wie er auf dem Speicher der Konsole liegt.\n\n"
        L"Klicken Sie auf OK, um diesen Ordner zu wählen. Das Spiel wird einmalig neben dem Programm installiert.",
        L"¡Bienvenido!\n\nEste programa ejecuta Crystal Defenders (Xbox 360) en PC, pero no contiene los datos del juego. "
        L"Necesita tu propia copia del juego: la carpeta «CRYSTAL DEFENDERS» (o «58410989») tal como está en el almacenamiento de la consola.\n\n"
        L"Haz clic en Aceptar para elegir esa carpeta. El juego se instalará junto al programa, una sola vez.",
        L"Benvenuto!\n\nQuesto programma esegue Crystal Defenders (Xbox 360) su PC, ma non contiene i dati del gioco. "
        L"Serve la tua copia del gioco: la cartella «CRYSTAL DEFENDERS» (o «58410989») così come si trova nella memoria della console.\n\n"
        L"Fai clic su OK per scegliere quella cartella. Il gioco verrà installato accanto al programma, una volta sola.",
        L"ようこそ！\n\nこのプログラムは PC で Crystal Defenders（Xbox 360）を動かしますが、ゲームのデータは含まれていません。"
        L"お持ちのゲームのコピー（本体のストレージにある「CRYSTAL DEFENDERS」または「58410989」フォルダー）が必要です。\n\n"
        L"OK をクリックしてそのフォルダーを選んでください。ゲームはプログラムの横に一度だけインストールされます。" },
    { L"The game was not found in this folder. It takes the console's \"CRYSTAL DEFENDERS\" or \"58410989\" folder, or a folder holding default.xex and Media.\n\nChoose another folder?",
        L"Das Spiel wurde in diesem Ordner nicht gefunden. Nötig ist der Ordner „CRYSTAL DEFENDERS“ oder „58410989“ der Konsole oder ein Ordner mit default.xex und Media.\n\nEinen anderen Ordner wählen?",
        L"No se ha encontrado el juego en esta carpeta. Hace falta la carpeta «CRYSTAL DEFENDERS» o «58410989» de la consola, o una carpeta con default.xex y Media.\n\n¿Elegir otra carpeta?",
        L"Il gioco non è stato trovato in questa cartella. Serve la cartella «CRYSTAL DEFENDERS» o «58410989» della console, o una cartella con default.xex e Media.\n\nScegliere un'altra cartella?",
        L"このフォルダーにゲームが見つかりませんでした。本体の「CRYSTAL DEFENDERS」または「58410989」フォルダー、または default.xex と Media を含むフォルダーが必要です。\n\n別のフォルダーを選びますか？" },
    { L"Copying the files failed. Check there is free space and that the program's folder can be written to.\n\n",
        L"Das Kopieren der Dateien ist fehlgeschlagen. Prüfen Sie, ob genug Platz frei ist und der Programmordner beschreibbar ist.\n\n",
        L"No se han podido copiar los archivos. Comprueba que hay espacio libre y que se puede escribir en la carpeta del programa.\n\n",
        L"Copia dei file non riuscita. Verifica che ci sia spazio libero e che la cartella del programma sia scrivibile.\n\n",
        L"ファイルのコピーに失敗しました。空き容量があるか、プログラムのフォルダーに書き込めるかを確認してください。\n\n" },
};

} // namespace

const wchar_t* Translate(const wchar_t* french, const wchar_t* english)
{
    switch (GuestLanguage())
    {
    case 4: return french;
    case 3: case 5: case 6: case 2: break;
    default: return english;
    }
    for (const Entry& entry : kEntries)
    {
        if (wcscmp(entry.english, english) != 0)
            continue;
        switch (GuestLanguage())
        {
        case 3: return entry.german;
        case 5: return entry.spanish;
        case 6: return entry.italian;
        default: return entry.japanese;
        }
    }
    return english;
}
