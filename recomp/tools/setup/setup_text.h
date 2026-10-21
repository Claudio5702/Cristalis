// Texts of the installer and uninstaller. The language follows the Windows display language
// (/LANG=fr|en|de|es|it|ja forces one). %hs receives a folder path.
enum {
  T_TITLE_INSTALL, T_TITLE_UNINSTALL,
  // installer window
  T_INTRO, T_DIR_LABEL, T_BROWSE_BTN, T_BROWSE, T_CHK_DESKTOP, T_CHK_MENU,
  T_GAME_FOUND, // %hs: the folder of the game found next to the installer
  T_GAME_MISSING,
  T_BTN_INSTALL, T_BTN_CANCEL, T_BTN_CLOSE, T_BTN_PLAY, T_BTN_PICK,
  // result window
  T_DONE_TITLE,
  T_DONE_READY,  // %hs installation folder
  T_DONE_NOGAME, // %hs installation folder
  T_PICK_TITLE, T_GAME_FAIL, T_WORKING,
  // errors
  T_CANT_WRITE, T_COPY_ERR, T_BAD_SETUP,
  // uninstaller
  T_UN_NOTGAME, T_UN_RUNNING, T_UN_INTRO, T_UN_CHK_ALL, T_UN_NOTE, T_BTN_UNINSTALL, T_UN_DONE, T_UN_DONE_KEPT,
  T_COUNT
};

static int lang = 1;

static const wchar_t *const texts[6][T_COUNT] = {
    // ------------------------------------------------------------------ Français
    {
        [T_TITLE_INSTALL] = L"Installation de Cristalis (Crystal Defenders)", [T_TITLE_UNINSTALL] = L"Désinstallation de Cristalis (Crystal Defenders)",
        [T_INTRO] = L"Ce programme installe Cristalis (portage natif non officiel de Crystal Defenders pour Windows) sur cet ordinateur.",
        [T_DIR_LABEL] = L"Dossier d'installation :", [T_BROWSE_BTN] = L"Parcourir…",
        [T_BROWSE] = L"Dossier où installer Crystal Defenders (un sous-dossier « Cristalis » y sera créé)",
        [T_CHK_DESKTOP] = L"Créer un raccourci sur le Bureau", [T_CHK_MENU] = L"Créer un raccourci dans le menu Démarrer",
        [T_GAME_FOUND] = L"Votre copie du jeu a été trouvée à côté de l'installeur (%hs) : le jeu sera installé depuis celle-ci.",
        [T_GAME_MISSING] = L"Les données du jeu ne sont pas fournies. À la fin de l'installation, vous indiquerez votre copie : le dossier CRYSTAL DEFENDERS (ou 58410989) tel qu'il se trouve sur le stockage de la console.",
        [T_BTN_INSTALL] = L"Installer", [T_BTN_CANCEL] = L"Annuler", [T_BTN_CLOSE] = L"Fermer", [T_BTN_PLAY] = L"Lancer le jeu",
        [T_BTN_PICK] = L"Choisir le dossier du jeu",
        [T_DONE_TITLE] = L"Installation terminée",
        [T_DONE_READY] = L"Installé dans :\n%hs\n\nLe jeu est prêt.",
        [T_DONE_NOGAME] = L"Installé dans :\n%hs\n\nIl reste à indiquer votre copie du jeu : le dossier CRYSTAL DEFENDERS (ou 58410989) copié depuis la console (clé USB ou disque dur), ou un dossier contenant default.xex et Media.",
        [T_PICK_TITLE] = L"Dossier de votre copie de Crystal Defenders : CRYSTAL DEFENDERS ou 58410989",
        [T_GAME_FAIL] = L"Le jeu n'a pas été trouvé dans ce dossier, ou sa copie a échoué. Il faut le dossier CRYSTAL DEFENDERS ou 58410989 de la console, ou un dossier contenant default.xex et Media.",
        [T_WORKING] = L"Installation des données du jeu depuis votre copie…",
        [T_CANT_WRITE] = L"Impossible d'écrire :\n%hs\n\nFermez le jeu s'il est lancé, ou choisissez un dossier où l'écriture est permise.",
        [T_COPY_ERR] = L"Erreur pendant la copie des fichiers.", [T_BAD_SETUP] = L"Cet installeur est incomplet ou abîmé.",
        [T_UN_NOTGAME] = L"Ce dossier ne contient pas Crystal Defenders. Rien n'a été supprimé.",
        [T_UN_RUNNING] = L"Le jeu est lancé. Fermez-le, puis réessayez. (Rien n'a encore été supprimé.)",
        [T_UN_INTRO] = L"Crystal Defenders va être supprimé de ce dossier :\n%hs",
        [T_UN_CHK_ALL] = L"Supprimer aussi les sauvegardes, les succès, les réglages, les captures et les données du jeu",
        [T_UN_NOTE] = L"Case non cochée : ils restent dans le dossier (conseillé pour une réinstallation).",
        [T_BTN_UNINSTALL] = L"Désinstaller",
        [T_UN_DONE] = L"Crystal Defenders a été désinstallé.",
        [T_UN_DONE_KEPT] = L"Crystal Defenders a été désinstallé.\nLes sauvegardes, les succès, les réglages, les captures et les données du jeu sont restés dans le dossier.",
    },
    // ------------------------------------------------------------------ English
    {
        [T_TITLE_INSTALL] = L"Cristalis (Crystal Defenders) Setup", [T_TITLE_UNINSTALL] = L"Uninstall Cristalis (Crystal Defenders)",
        [T_INTRO] = L"This program installs Cristalis (an unofficial native port of Crystal Defenders for Windows) on this computer.",
        [T_DIR_LABEL] = L"Installation folder:", [T_BROWSE_BTN] = L"Browse…",
        [T_BROWSE] = L"Folder to install Crystal Defenders in (a \"Cristalis\" folder is created inside it)",
        [T_CHK_DESKTOP] = L"Create a shortcut on the desktop", [T_CHK_MENU] = L"Create a shortcut in the Start menu",
        [T_GAME_FOUND] = L"Your copy of the game was found next to the installer (%hs): the game will be installed from it.",
        [T_GAME_MISSING] = L"The game's data is not included. At the end of the installation you will point to your copy: the CRYSTAL DEFENDERS (or 58410989) folder as found on the console's storage.",
        [T_BTN_INSTALL] = L"Install", [T_BTN_CANCEL] = L"Cancel", [T_BTN_CLOSE] = L"Close", [T_BTN_PLAY] = L"Start the game",
        [T_BTN_PICK] = L"Choose the game's folder",
        [T_DONE_TITLE] = L"Installation complete",
        [T_DONE_READY] = L"Installed in:\n%hs\n\nThe game is ready.",
        [T_DONE_NOGAME] = L"Installed in:\n%hs\n\nOne step left: point to your copy of the game, the CRYSTAL DEFENDERS (or 58410989) folder copied from the console (USB drive or hard drive), or a folder holding default.xex and Media.",
        [T_PICK_TITLE] = L"Folder of your copy of Crystal Defenders: CRYSTAL DEFENDERS or 58410989",
        [T_GAME_FAIL] = L"The game was not found in this folder, or copying it failed. It takes the console's CRYSTAL DEFENDERS or 58410989 folder, or a folder holding default.xex and Media.",
        [T_WORKING] = L"Installing the game's data from your copy…",
        [T_CANT_WRITE] = L"Unable to write:\n%hs\n\nClose the game if it is running, or choose a folder that can be written to.",
        [T_COPY_ERR] = L"An error occurred while copying the files.", [T_BAD_SETUP] = L"This installer is incomplete or damaged.",
        [T_UN_NOTGAME] = L"This folder does not hold Crystal Defenders. Nothing was removed.",
        [T_UN_RUNNING] = L"The game is running. Close it, then try again. (Nothing has been removed yet.)",
        [T_UN_INTRO] = L"Crystal Defenders will be removed from this folder:\n%hs",
        [T_UN_CHK_ALL] = L"Also remove the saves, achievements, settings, screenshots and game data",
        [T_UN_NOTE] = L"Unchecked: they stay in the folder (recommended before reinstalling).",
        [T_BTN_UNINSTALL] = L"Uninstall",
        [T_UN_DONE] = L"Crystal Defenders has been uninstalled.",
        [T_UN_DONE_KEPT] = L"Crystal Defenders has been uninstalled.\nThe saves, achievements, settings, screenshots and game data were left in the folder.",
    },
    // ------------------------------------------------------------------ Deutsch
    {
        [T_TITLE_INSTALL] = L"Installation von Cristalis (Crystal Defenders)", [T_TITLE_UNINSTALL] = L"Deinstallation von Cristalis (Crystal Defenders)",
        [T_INTRO] = L"Dieses Programm installiert Cristalis (eine inoffizielle native Portierung von Crystal Defenders für Windows) auf diesem Computer.",
        [T_DIR_LABEL] = L"Installationsordner:", [T_BROWSE_BTN] = L"Durchsuchen…",
        [T_BROWSE] = L"Ordner für Crystal Defenders (darin wird ein Ordner „Cristalis“ angelegt)",
        [T_CHK_DESKTOP] = L"Verknüpfung auf dem Desktop erstellen", [T_CHK_MENU] = L"Verknüpfung im Startmenü erstellen",
        [T_GAME_FOUND] = L"Ihre Kopie des Spiels wurde neben dem Installationsprogramm gefunden (%hs): Das Spiel wird daraus installiert.",
        [T_GAME_MISSING] = L"Die Spieldaten sind nicht enthalten. Am Ende der Installation geben Sie Ihre Kopie an: den Ordner CRYSTAL DEFENDERS (oder 58410989), wie er auf dem Speicher der Konsole liegt.",
        [T_BTN_INSTALL] = L"Installieren", [T_BTN_CANCEL] = L"Abbrechen", [T_BTN_CLOSE] = L"Schließen", [T_BTN_PLAY] = L"Spiel starten",
        [T_BTN_PICK] = L"Spielordner wählen",
        [T_DONE_TITLE] = L"Installation abgeschlossen",
        [T_DONE_READY] = L"Installiert in:\n%hs\n\nDas Spiel ist bereit.",
        [T_DONE_NOGAME] = L"Installiert in:\n%hs\n\nEin Schritt fehlt noch: Geben Sie Ihre Kopie des Spiels an, den von der Konsole kopierten Ordner CRYSTAL DEFENDERS (oder 58410989) (USB-Stick oder Festplatte) oder einen Ordner mit default.xex und Media.",
        [T_PICK_TITLE] = L"Ordner Ihrer Kopie von Crystal Defenders: CRYSTAL DEFENDERS oder 58410989",
        [T_GAME_FAIL] = L"Das Spiel wurde in diesem Ordner nicht gefunden, oder das Kopieren ist fehlgeschlagen. Nötig ist der Ordner CRYSTAL DEFENDERS oder 58410989 der Konsole oder ein Ordner mit default.xex und Media.",
        [T_WORKING] = L"Die Spieldaten werden aus Ihrer Kopie installiert…",
        [T_CANT_WRITE] = L"Schreiben nicht möglich:\n%hs\n\nBeenden Sie das Spiel, falls es läuft, oder wählen Sie einen beschreibbaren Ordner.",
        [T_COPY_ERR] = L"Beim Kopieren der Dateien ist ein Fehler aufgetreten.", [T_BAD_SETUP] = L"Dieses Installationsprogramm ist unvollständig oder beschädigt.",
        [T_UN_NOTGAME] = L"Dieser Ordner enthält Crystal Defenders nicht. Es wurde nichts entfernt.",
        [T_UN_RUNNING] = L"Das Spiel läuft. Beenden Sie es und versuchen Sie es erneut. (Es wurde noch nichts entfernt.)",
        [T_UN_INTRO] = L"Crystal Defenders wird aus diesem Ordner entfernt:\n%hs",
        [T_UN_CHK_ALL] = L"Auch Spielstände, Erfolge, Einstellungen, Screenshots und Spieldaten entfernen",
        [T_UN_NOTE] = L"Nicht angehakt: Sie bleiben im Ordner (empfohlen vor einer Neuinstallation).",
        [T_BTN_UNINSTALL] = L"Deinstallieren",
        [T_UN_DONE] = L"Crystal Defenders wurde deinstalliert.",
        [T_UN_DONE_KEPT] = L"Crystal Defenders wurde deinstalliert.\nSpielstände, Erfolge, Einstellungen, Screenshots und Spieldaten sind im Ordner geblieben.",
    },
    // ------------------------------------------------------------------ Español
    {
        [T_TITLE_INSTALL] = L"Instalación de Cristalis (Crystal Defenders)", [T_TITLE_UNINSTALL] = L"Desinstalación de Cristalis (Crystal Defenders)",
        [T_INTRO] = L"Este programa instala Cristalis (una adaptación nativa no oficial de Crystal Defenders para Windows) en este ordenador.",
        [T_DIR_LABEL] = L"Carpeta de instalación:", [T_BROWSE_BTN] = L"Examinar…",
        [T_BROWSE] = L"Carpeta donde instalar Crystal Defenders (se creará dentro una carpeta «Cristalis»)",
        [T_CHK_DESKTOP] = L"Crear un acceso directo en el escritorio", [T_CHK_MENU] = L"Crear un acceso directo en el menú Inicio",
        [T_GAME_FOUND] = L"Se ha encontrado tu copia del juego junto al instalador (%hs): el juego se instalará desde ella.",
        [T_GAME_MISSING] = L"Los datos del juego no están incluidos. Al final de la instalación indicarás tu copia: la carpeta CRYSTAL DEFENDERS (o 58410989) tal como está en el almacenamiento de la consola.",
        [T_BTN_INSTALL] = L"Instalar", [T_BTN_CANCEL] = L"Cancelar", [T_BTN_CLOSE] = L"Cerrar", [T_BTN_PLAY] = L"Iniciar el juego",
        [T_BTN_PICK] = L"Elegir la carpeta del juego",
        [T_DONE_TITLE] = L"Instalación completada",
        [T_DONE_READY] = L"Instalado en:\n%hs\n\nEl juego está listo.",
        [T_DONE_NOGAME] = L"Instalado en:\n%hs\n\nFalta un paso: indicar tu copia del juego, la carpeta CRYSTAL DEFENDERS (o 58410989) copiada de la consola (memoria USB o disco duro), o una carpeta con default.xex y Media.",
        [T_PICK_TITLE] = L"Carpeta de tu copia de Crystal Defenders: CRYSTAL DEFENDERS o 58410989",
        [T_GAME_FAIL] = L"No se ha encontrado el juego en esta carpeta, o la copia ha fallado. Hace falta la carpeta CRYSTAL DEFENDERS o 58410989 de la consola, o una carpeta con default.xex y Media.",
        [T_WORKING] = L"Instalando los datos del juego desde tu copia…",
        [T_CANT_WRITE] = L"No se puede escribir:\n%hs\n\nCierra el juego si está abierto, o elige una carpeta en la que se pueda escribir.",
        [T_COPY_ERR] = L"Error al copiar los archivos.", [T_BAD_SETUP] = L"Este instalador está incompleto o dañado.",
        [T_UN_NOTGAME] = L"Esta carpeta no contiene Crystal Defenders. No se ha borrado nada.",
        [T_UN_RUNNING] = L"El juego está abierto. Ciérralo y vuelve a intentarlo. (Todavía no se ha borrado nada.)",
        [T_UN_INTRO] = L"Crystal Defenders se eliminará de esta carpeta:\n%hs",
        [T_UN_CHK_ALL] = L"Eliminar también las partidas, los logros, los ajustes, las capturas y los datos del juego",
        [T_UN_NOTE] = L"Sin marcar: se quedan en la carpeta (recomendado si vas a reinstalar).",
        [T_BTN_UNINSTALL] = L"Desinstalar",
        [T_UN_DONE] = L"Crystal Defenders se ha desinstalado.",
        [T_UN_DONE_KEPT] = L"Crystal Defenders se ha desinstalado.\nLas partidas, los logros, los ajustes, las capturas y los datos del juego se han quedado en la carpeta.",
    },
    // ------------------------------------------------------------------ Italiano
    {
        [T_TITLE_INSTALL] = L"Installazione di Cristalis (Crystal Defenders)", [T_TITLE_UNINSTALL] = L"Disinstallazione di Cristalis (Crystal Defenders)",
        [T_INTRO] = L"Questo programma installa Cristalis (una conversione nativa non ufficiale di Crystal Defenders per Windows) su questo computer.",
        [T_DIR_LABEL] = L"Cartella di installazione:", [T_BROWSE_BTN] = L"Sfoglia…",
        [T_BROWSE] = L"Cartella in cui installare Crystal Defenders (al suo interno verrà creata una cartella «Cristalis»)",
        [T_CHK_DESKTOP] = L"Crea un collegamento sul desktop", [T_CHK_MENU] = L"Crea un collegamento nel menu Start",
        [T_GAME_FOUND] = L"La tua copia del gioco è stata trovata accanto al programma di installazione (%hs): il gioco verrà installato da lì.",
        [T_GAME_MISSING] = L"I dati del gioco non sono inclusi. Alla fine dell'installazione indicherai la tua copia: la cartella CRYSTAL DEFENDERS (o 58410989) così come si trova nella memoria della console.",
        [T_BTN_INSTALL] = L"Installa", [T_BTN_CANCEL] = L"Annulla", [T_BTN_CLOSE] = L"Chiudi", [T_BTN_PLAY] = L"Avvia il gioco",
        [T_BTN_PICK] = L"Scegli la cartella del gioco",
        [T_DONE_TITLE] = L"Installazione completata",
        [T_DONE_READY] = L"Installato in:\n%hs\n\nIl gioco è pronto.",
        [T_DONE_NOGAME] = L"Installato in:\n%hs\n\nManca un passo: indicare la tua copia del gioco, la cartella CRYSTAL DEFENDERS (o 58410989) copiata dalla console (chiavetta USB o disco rigido), o una cartella con default.xex e Media.",
        [T_PICK_TITLE] = L"Cartella della tua copia di Crystal Defenders: CRYSTAL DEFENDERS o 58410989",
        [T_GAME_FAIL] = L"Il gioco non è stato trovato in questa cartella, oppure la copia non è riuscita. Serve la cartella CRYSTAL DEFENDERS o 58410989 della console, o una cartella con default.xex e Media.",
        [T_WORKING] = L"Installazione dei dati del gioco dalla tua copia…",
        [T_CANT_WRITE] = L"Impossibile scrivere:\n%hs\n\nChiudi il gioco se è aperto, o scegli una cartella in cui sia possibile scrivere.",
        [T_COPY_ERR] = L"Errore durante la copia dei file.", [T_BAD_SETUP] = L"Questo programma di installazione è incompleto o danneggiato.",
        [T_UN_NOTGAME] = L"Questa cartella non contiene Crystal Defenders. Non è stato eliminato nulla.",
        [T_UN_RUNNING] = L"Il gioco è aperto. Chiudilo e riprova. (Non è ancora stato eliminato nulla.)",
        [T_UN_INTRO] = L"Crystal Defenders verrà rimosso da questa cartella:\n%hs",
        [T_UN_CHK_ALL] = L"Elimina anche salvataggi, obiettivi, impostazioni, screenshot e dati del gioco",
        [T_UN_NOTE] = L"Non selezionato: restano nella cartella (consigliato se reinstalli).",
        [T_BTN_UNINSTALL] = L"Disinstalla",
        [T_UN_DONE] = L"Crystal Defenders è stato disinstallato.",
        [T_UN_DONE_KEPT] = L"Crystal Defenders è stato disinstallato.\nSalvataggi, obiettivi, impostazioni, screenshot e dati del gioco sono rimasti nella cartella.",
    },
    // ------------------------------------------------------------------ 日本語
    {
        [T_TITLE_INSTALL] = L"Cristalis（Crystal Defenders）のインストール", [T_TITLE_UNINSTALL] = L"Cristalis（Crystal Defenders）のアンインストール",
        [T_INTRO] = L"Cristalis（クリスタルディフェンダーズの Windows 向け非公式ネイティブ移植版）をこのパソコンにインストールします。",
        [T_DIR_LABEL] = L"インストール先のフォルダー：", [T_BROWSE_BTN] = L"参照…",
        [T_BROWSE] = L"Crystal Defenders のインストール先（中に「Cristalis」フォルダーが作られます）",
        [T_CHK_DESKTOP] = L"デスクトップにショートカットを作る", [T_CHK_MENU] = L"スタートメニューにショートカットを作る",
        [T_GAME_FOUND] = L"インストーラーの横にゲームのコピーが見つかりました（%hs）。ここからゲームをインストールします。",
        [T_GAME_MISSING] = L"ゲームのデータは含まれていません。インストールの最後に、お持ちのコピー（本体のストレージにある CRYSTAL DEFENDERS または 58410989 フォルダー）を指定してください。",
        [T_BTN_INSTALL] = L"インストール", [T_BTN_CANCEL] = L"キャンセル", [T_BTN_CLOSE] = L"閉じる", [T_BTN_PLAY] = L"ゲームを起動",
        [T_BTN_PICK] = L"ゲームのフォルダーを選ぶ",
        [T_DONE_TITLE] = L"インストールが完了しました",
        [T_DONE_READY] = L"インストール先：\n%hs\n\nすぐに遊べます。",
        [T_DONE_NOGAME] = L"インストール先：\n%hs\n\n最後に、お持ちのゲームのコピーを指定してください：本体からコピーした CRYSTAL DEFENDERS（または 58410989）フォルダー（USB メモリーやハードディスク）、または default.xex と Media を含むフォルダー。",
        [T_PICK_TITLE] = L"Crystal Defenders のコピーのフォルダー：CRYSTAL DEFENDERS または 58410989",
        [T_GAME_FAIL] = L"このフォルダーにゲームが見つからないか、コピーに失敗しました。本体の CRYSTAL DEFENDERS または 58410989 フォルダー、または default.xex と Media を含むフォルダーが必要です。",
        [T_WORKING] = L"お持ちのコピーからゲームのデータをインストールしています…",
        [T_CANT_WRITE] = L"書き込めませんでした：\n%hs\n\nゲームが起動中の場合は終了するか、書き込み可能なフォルダーを選んでください。",
        [T_COPY_ERR] = L"ファイルのコピー中にエラーが発生しました。", [T_BAD_SETUP] = L"このインストーラーは不完全か、壊れています。",
        [T_UN_NOTGAME] = L"このフォルダーには Crystal Defenders がありません。何も削除していません。",
        [T_UN_RUNNING] = L"ゲームが起動中です。ゲームを終了してから、もう一度お試しください。（まだ何も削除していません）",
        [T_UN_INTRO] = L"次のフォルダーから Crystal Defenders を削除します：\n%hs",
        [T_UN_CHK_ALL] = L"セーブデータ、実績、設定、スクリーンショット、ゲームのデータも削除する",
        [T_UN_NOTE] = L"チェックなし：これらはフォルダーに残ります（再インストールする場合におすすめ）。",
        [T_BTN_UNINSTALL] = L"アンインストール",
        [T_UN_DONE] = L"Crystal Defenders をアンインストールしました。",
        [T_UN_DONE_KEPT] = L"Crystal Defenders をアンインストールしました。\nセーブデータ、実績、設定、スクリーンショット、ゲームのデータはフォルダーに残っています。",
    },
};

#define T(id) (texts[lang][id])

static void pick_language(const char *forced) {
  static const char *const codes[6] = {"fr", "en", "de", "es", "it", "ja"};
  if (forced) {
    for (int k = 0; k < 6; k++) if (!_stricmp(forced, codes[k])) lang = k;
    return;
  }
  switch (GetUserDefaultUILanguage() & 0x3FF) {
    case 0x0C: lang = 0; break; // French
    case 0x07: lang = 2; break; // German
    case 0x0A: lang = 3; break; // Spanish
    case 0x10: lang = 4; break; // Italian
    case 0x11: lang = 5; break; // Japanese
    default: lang = 1; break;   // English
  }
}
