// The games made with DarkLauncher by the same author, offered on the "Other games" page of each of them
// (2.71; dl_menu.c reads it). A game leaves itself out, and the page and its entry in the bar only exist
// when another game is released. Change this list when a game comes out (released = 1), then give the
// new version to the projects: each one shows it from its next update.
//
// The format is the one of the list on line to come (read the same way): UTF-8 lines; "[id]" starts a
// game, id being its UNINSTALL_ID (project.h: how an installed copy is found in the Windows list of
// programs, and how a game knows itself); then "key = value":
//   name       as shown (a proper noun: the same in every language)
//   stem       its FILE_STEM: on Linux, an installed copy is <XDG_DATA_HOME>/<stem>/<stem>
//   url        its page, opened when the game is not installed ("": none)
//   released   1: offered; 0: not yet (only with --all-games, and in --ui-audit)
//   text.jp text.fr text.en text.es text.br text.de text.it text.zh text.ko   one or two sentences on the game
//   picture    (the list on line only) a JPG or PNG beside it, about 640 x 360, shown on the page
// "#" starts a comment line.
//
// The list on line (2.93): DL_GAMES_URL, the author's secret gist, its newest version
// (gist.github.com/Claudio5702/9c7231eefa311ed8c11600990a30a34d: change it there, with git for a
// picture), released games only, read once at start on a thread when the player lets the program look
// for new versions (the same leave), with its pictures; nothing kept on disk. Once read, it replaces this one, and the page
// and its entry appear then if they were not there. A project may give its own (project.h: the demo's
// is local, for the tests; "" none).
#ifndef DL_GAMES_H
#define DL_GAMES_H
#ifndef DL_GAMES_URL
#define DL_GAMES_URL "https://gist.githubusercontent.com/Claudio5702/9c7231eefa311ed8c11600990a30a34d/raw/games.txt" // the gist, its newest version
#endif

static const char dl_games_list[] =
    "[BahamutLagoonNatif]\n"
    "name = Kahna\n"
    "stem = bahamut\n"
    "url = https://github.com/Claudio5702/Kahna\n"
    "released = 0\n"
    "text.jp = スクウェアのスーパーファミコン用シミュレーションRPG『バハムート ラグーン』を PC 用にネイティブで再コンパイル。ワイド画面と、ファン翻訳による5つの言語で遊べます。\n"
    "text.fr = Bahamut Lagoon, le RPG tactique de Square sur Super Famicom, recompilé pour PC en version native, en écran large et en cinq langues (traductions des fans).\n"
    "text.en = Bahamut Lagoon, Square's tactical RPG for the Super Famicom, recompiled natively for PC, in widescreen and five languages (fan translations).\n"
    "text.es = Bahamut Lagoon, el RPG táctico de Square para Super Famicom, recompilado para PC en versión nativa, en pantalla ancha y en cinco idiomas (traducciones de fans).\n"
    "text.br = Bahamut Lagoon, o RPG tático da Square para Super Famicom, recompilado para PC em versão nativa, em tela larga e em cinco idiomas (traduções de fãs).\n"
    "text.de = Bahamut Lagoon, das Taktik-Rollenspiel von Square für das Super Famicom, nativ für den PC rekompiliert, im Breitbild und in fünf Sprachen (Fan-Übersetzungen).\n"
    "text.it = Bahamut Lagoon, il GdR tattico di Square per Super Famicom, ricompilato per PC in versione nativa, in schermo largo e in cinque lingue (traduzioni dei fan).\n"
    "text.zh = 史克威尔的超级任天堂战棋 RPG《巴哈姆特战记》，为 PC 原生重新编译，支持宽屏和五种语言（爱好者翻译）。\n"
    "text.ko = 스퀘어의 슈퍼 패미컴 전략 RPG 『바하무트 라군』을 PC용으로 네이티브 재컴파일했습니다. 와이드 화면과 다섯 가지 언어(팬 번역)를 지원합니다.\n"
    "\n"
    // (its widescreen is still to be confirmed by the user: check the texts before released = 1)
    "[NoFearDownhillRecompiled]\n"
    "name = No Fear Downhill\n"
    "stem = nofear\n"
    "url = https://github.com/Claudio5702/NoFearDownhill\n"
    "released = 0\n"
    "text.jp = プレイステーションのマウンテンバイク・ゲーム『No Fear Downhill Mountain Biking』を、高解像度とワイド画面で PC 用に再コンパイルしたものです。\n"
    "text.fr = No Fear Downhill Mountain Biking, la descente en VTT de la PlayStation, recompilée pour PC en haute résolution et en écran large.\n"
    "text.en = No Fear Downhill Mountain Biking, the PlayStation's downhill bike racer, recompiled for PC in high resolution and widescreen.\n"
    "text.es = No Fear Downhill Mountain Biking, el descenso en bicicleta de montaña de PlayStation, recompilado para PC en alta resolución y pantalla ancha.\n"
    "text.br = No Fear Downhill Mountain Biking, a descida de mountain bike do PlayStation, recompilada para PC em alta resolução e tela larga.\n"
    "text.de = No Fear Downhill Mountain Biking, das Downhill-Mountainbike-Rennen der PlayStation, für den PC rekompiliert, in hoher Auflösung und Breitbild.\n"
    "text.it = No Fear Downhill Mountain Biking, la discesa in mountain bike della PlayStation, ricompilata per PC in alta risoluzione e schermo largo.\n"
    "text.zh = PlayStation 的山地自行车速降游戏 No Fear Downhill Mountain Biking，为 PC 重新编译，支持高分辨率和宽屏。\n"
    "text.ko = PlayStation의 산악자전거 다운힐 게임 No Fear Downhill Mountain Biking을 고해상도와 와이드 화면으로 PC용으로 재컴파일했습니다.\n"
    "\n"
    "[CrystalDefendersRecomp]\n"
    "name = Cristalis\n"
    "stem = crystal_defenders\n"
    "url = https://github.com/Claudio5702/Cristalis\n"
    "released = 1\n" // public on GitHub since Cristalis 1.04; marked released by the user on 2026-10-26
    "text.jp = イヴァリースを舞台にしたスクウェア・エニックスのタワーディフェンス『クリスタルディフェンダーズ』（Xbox 360）を、PC 用にネイティブで動かすものです。\n"
    "text.fr = Crystal Defenders, le jeu de défense de tours de Square Enix situé à Ivalice (Xbox 360), en version native pour PC.\n"
    "text.en = Crystal Defenders, Square Enix's tower defence game set in Ivalice (Xbox 360), running natively on PC.\n"
    "text.es = Crystal Defenders, el juego de defensa de torres de Square Enix ambientado en Ivalice (Xbox 360), en versión nativa para PC.\n"
    "text.br = Crystal Defenders, o jogo de defesa de torres da Square Enix ambientado em Ivalice (Xbox 360), em versão nativa para PC.\n"
    "text.de = Crystal Defenders, das Tower-Defense-Spiel von Square Enix aus Ivalice (Xbox 360), nativ auf dem PC.\n"
    "text.it = Crystal Defenders, il tower defense di Square Enix ambientato a Ivalice (Xbox 360), in versione nativa per PC.\n"
    "text.zh = 以伊瓦利斯为舞台的史克威尔艾尼克斯塔防游戏《水晶守护者》（Xbox 360），在 PC 上原生运行。\n"
    "text.ko = 이발리스를 무대로 한 스퀘어 에닉스의 타워 디펜스 게임 『크리스탈 디펜더스』(Xbox 360)를 PC에서 네이티브로 실행합니다.\n";

#endif
