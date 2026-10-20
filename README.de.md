# Cristalis

**Crystal Defenders Recomp — native Portierung durch statische Rekompilierung**

*[English](README.md) · [Français](README.fr.md) · **Deutsch** · [Español](README.es.md) · [Italiano](README.it.md) · [日本語](README.ja.md)*

![Der Einstellungsbildschirm, DarkLauncher in den Farben des Spiels](docs/screenshots/launcher-en.png)

Dieses Projekt lässt Crystal Defenders, das Xbox-Live-Arcade-Spiel, als natives Windows-Programm
laufen: Der PowerPC-Code des Spiels wird im Voraus mit
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) nach C++ übersetzt und läuft auf einer
kleinen Laufzeitumgebung, die nur für dieses Spiel geschrieben wurde. Eine Linux-Version ist
experimentell.

**Es sind keine Ressourcen des Spiels enthalten**, weder in diesem Repository noch in den Releases:
Bilder, Töne, Musik und Texte werden aus Ihrer eigenen Kopie des Spiels gelesen. Das Repository
enthält auch keinen Code des Spiels: Der übersetzte Code wird beim Bauen des Programms aus einer
`default.xex` erzeugt, und die Programme auf der [Release-Seite](https://github.com/Claudio5702/Cristalis/releases/latest) werden so gebaut.
**Version 1.09, eine Beta**: bisher von wenigen gespielt.

**Sie brauchen Ihre eigene Kopie von Crystal Defenders (Xbox 360).** Crystal Defenders gehört Square
Enix; dieses Projekt steht in keiner Verbindung zu Square Enix und wird von Square Enix nicht
unterstützt.

**Inhalt:** [Spielen](#spielen) · [Funktionen](#funktionen) ·
[Einstellungsbildschirm](#einstellungsbildschirm) · [Steuerung](#steuerung) · [Dateien](#dateien) ·
[Deinstallieren](#deinstallieren) · [Linux](#linux) · [Bekannte Grenzen](#bekannte-grenzen) ·
[Für Entwickler](#für-entwickler) · [Häufige Fragen](#häufige-fragen) · [Pläne](#pläne) ·
[Lizenz](#lizenz) · [Danksagungen](#danksagungen)

## Spielen

Laden Sie die neueste Version von der [Release-Seite](https://github.com/Claudio5702/Cristalis/releases/latest) herunter, und nur von dort:
`Cristalis-Setup.exe` für Windows, `Cristalis-linux-x86_64.tar.gz` für Linux.

1. Kopieren Sie das Spiel von Ihrer Konsole: den Ordner `CRYSTAL DEFENDERS` (oder den Ordner mit der
   Titel-ID, `58410989`), so wie er auf dem Speicher der Konsole liegt, zum Beispiel auf einem
   USB-Stick.
2. Mit dem Installationsprogramm: Legen Sie diesen Ordner neben `Cristalis-Setup.exe` und
   starten Sie es. Das Spiel wird aus diesem Ordner installiert, mit Verknüpfungen und einem Eintrag
   in der Windows-Liste der Apps. Ohne den Ordner fragt das letzte Fenster des Installationsprogramms,
   wo er liegt.
3. Selbst gebaut, ohne das Installationsprogramm: Starten Sie `crystal_defenders.exe`. Beim ersten Start fragt es
   nach diesem Ordner (oder einem Ordner, der schon `default.xex` und `Media` enthält), oder nimmt
   ihn sofort, wenn er neben dem Programm liegt.

Die Daten des Spiels werden dann in einem Ordner `game` neben dem Programm aufbewahrt; der Ordner der
Konsole wird nicht mehr gebraucht. Behalten Sie diese Dateien für sich.

Windows 10 oder 11, 64 Bit, eine Grafikkarte mit Direct3D 11.

### Das zu verwendende Spiel

| | |
| --- | --- |
| Titel-ID | `58410989` (Xbox Live Arcade) |
| Paket der Konsole | `58410989\000D0000\18F4739D37853F8596CE621540680922AFE9F23A`, 58.126.336 Bytes, SHA-1 `118C64C901976CD8E2B570D77B0A5E01C15C7A46` |
| `default.xex` darin | 1.658.880 Bytes, SHA-1 `31A90704D4233F6E730714515CD5F20BF08D7F0D` |

Das Programm prüft, ob `default.xex` diejenige ist, aus der sein Code übersetzt wurde, und sagt es,
wenn nicht.

## Funktionen

- **Das Spiel in [DarkLauncher](kit/launcher)**, dem für Kahna
  (Bahamut Lagoon Recomp) entwickelten Launcher: ein Einstellungsbildschirm vor dem Spiel und ein
  Pausenmenü im Spiel, in sieben Sprachen, mit Tastatur, Controller oder Maus. Sein Aussehen folgt
  der künstlerischen Gestaltung des Spiels: das Blau seiner Fenster, das Gold seiner Kristalle, ein
  Sternenhimmel. DarkLauncher zeichnet alles selbst; nichts stammt aus den Dateien des Spiels.
- **Die sechs Sprachen des Spiels** (Französisch, Englisch, Deutsch, Spanisch, Italienisch,
  Japanisch), in den Einstellungen gewählt; das Spiel startet in der neuen Sprache neu.
- **Anzeige über die Grafikkarte**: das Spiel in 1280×720, 1920×1080, 2560×1440 oder 3840×2160
  gezeichnet, dann scharf, geglättet oder mit weichen Kanten in jeder Fenstergröße; HDR (Windows).
- **Erfolge auf dem Computer gespeichert**, mit einer Meldung über dem Spiel und einer Liste in den
  Einstellungen.
- **Das Offline-Verhalten der Konsole auf Wunsch**: der Hinweis „Nicht mit Xbox LIVE verbunden" und
  die Wahl des Speichergeräts sind standardmäßig aus und lassen sich einschalten.
- **Screenshots** des Bildes, wie es angezeigt wird, mit Filtern, und eine Galerie in den
  Einstellungen.
- **Controller**: Xbox (XInput) sowie PlayStation 4 und 5 direkt gelesen (USB oder Bluetooth), mit
  Vibration bei beiden; Tasten und Knöpfe werden auf der Seite Steuerung gewählt. Ein PlayStation-3-
  Controller braucht den Treiber DsHidMini.
- **Ein Absturzbericht**, der sagt, wo das Spiel war („W1, Stage 2, Wave 5"), beim nächsten Start geschrieben.
- **Ein Installations- und ein Deinstallationsprogramm**, in sechs Sprachen.
- **Updates**: Bei jedem Start schaut der Launcher, ob es eine neue Version gibt, und bietet an,
  sie zu installieren; nichts wird installiert, bevor Sie „Aktualisieren" wählen. Abschalten
  lässt es sich unter Einstellungen, Internet.

## Einstellungsbildschirm

Beim Start zeigt DarkLauncher seinen Einstellungsbildschirm, mit dem laufenden Spiel in der Vorschau.
**Spielen** startet es; im Spiel öffnet **Esc** (oder die View-Taste des Controllers, gehalten) ein
Pausenmenü über dem angehaltenen Bild: zurück zum Spiel, Screenshot, Einstellungen, beenden.

Eine Leiste links enthält Spielen, Screenshots, Spiel, Erfolge und Einstellungen, ganz unten Beenden;
die Seite des gewählten Eintrags erscheint daneben. Enter oder Rechts öffnet sie, Esc kehrt zurück.

- **Screenshots**: die aufgenommenen Screenshots, die neuesten zuerst. Enter öffnet einen, zweimal
  Entf verschiebt ihn in den Papierkorb.
- **Spiel**: der Hinweis „Nicht mit Xbox LIVE verbunden" und die Wahl des Speichergeräts (beides wie
  auf einer Konsole ohne Netz; ab dem nächsten Start).
- **Erfolge**: die zwölf Erfolge des Spiels, mit dem Datum jedes Erfolgs und was er verlangt.
- **Einstellungen**: eine Seite, die zum Rest führt.
  - **Sprachen**: die Sprache des Spiels (das Spiel startet in ihr neu) und die des Menüs.
  - **Bild**: die interne Auflösung (1280×720, 1920×1080, 2560×1440 oder 3840×2160), eine Voreinstellung,
    Vollbild, Ganzzahl-Skalierung, Anzeige über die Grafikkarte oder in Software, Filter, HDR, vertikale Synchronisation, die Anzeige der Bildrate, „Groß ausprobieren" und
    Barrierefreiheit (hoher Kontrast, Farbenblind-Filter, weniger Bewegung, große Hilfetexte).
  - **Ton**: die Lautstärke und die Töne des Menüs.
  - **Steuerung**: die Tastatur-Tasten und Controller-Knöpfe des Spiels, die Vibration und die
    Tastenkürzel.
  - **Internet**: die Suche nach neuen Versionen (anfangs eingeschaltet) und das Update selbst.
  - Der Spielordner und die Standardeinstellungen.

![Die Seite Bild](docs/screenshots/picture-en.png)

## Steuerung

Die Tasten folgen ihrer **physischen Position** (dieselbe auf QWERTZ-, AZERTY- und
QWERTY-Tastaturen); alle lassen sich auf der Seite Steuerung ändern.

| Spiel (Xbox 360) | Tastatur (QWERTY-Positionen) | Controller (PlayStation) |
| --- | --- | --- |
| Steuerkreuz | Pfeiltasten | Steuerkreuz / linker Stick |
| A (bestätigen) / B (zurück) | X / Z | A / B (Kreuz / Kreis) |
| X / Y | S / A | X / Y (Quadrat / Dreieck) |
| LB / RB | Q / W | LB / RB (L1 / R1) |
| Start / Back | Enter / rechte Umschalttaste | Start / Back (Options / Share); gehalten öffnet Back das Pausenmenü |

`Esc`: Pausenmenü — `F11` oder `Alt+Enter`: Vollbild — `F12`: Screenshot in `captures\`.

Auf einer deutschen QWERTZ-Tastatur ergeben die QWERTY-Positionen oben: X / Y für A / B, S / A für
X / Y und Q / W für LB / RB.

## Dateien

Alles bleibt im Ordner des Programms:

| | |
| --- | --- |
| `game\` | die Daten des Spiels, beim ersten Start aus Ihrer Kopie entnommen |
| `saves\` | Ihre Spielstände (`crystaldefendersW123\gamedata.bin`) und die erreichten Erfolge, mit ihren Daten |
| `captures\` | Screenshots |
| `crystal_defenders.ini` | Einstellungen |
| `crystal_defenders.log` | Protokoll der letzten Sitzung (`.prev.log`: der davor) |
| `crystal_defenders.ini.crash.txt` | Absturzbericht, beim Start nach einem Absturz geschrieben |

## Deinstallieren

Mit dem Installationsprogramm installiert: Starten Sie `Uninstall.exe` im Spielordner oder entfernen
Sie „Cristalis (Crystal Defenders)" aus der Liste der installierten Apps in den Windows-Einstellungen.
Standardmäßig bleiben Ihre Spielstände, Einstellungen, Screenshots und die Daten des Spiels im
Ordner, falls Sie neu installieren; setzen Sie das Häkchen, um sie ebenfalls zu entfernen, dann
verschwindet auch der Ordner. Ohne das Installationsprogramm: Löschen Sie den Ordner.

## Linux

**Experimentell.** Eine native Version für x86-64-Linux mit glibc 2.35 oder neuer (Ubuntu 22.04,
Debian 12, Fedora 36, SteamOS 3.5 und neuer), mit demselben DarkLauncher wie unter Windows: sein
Einstellungsbildschirm, Pausenmenü, sieben Sprachen, Controller (Xbox und PlayStation, mit
Vibration), Erfolge und Design. Das Spiel zeichnet mit OpenGL 3.3. Es braucht OpenGL, FreeType und
fontconfig vom System; SDL3 liegt bei, in `lib/`, und FFmpeg ist eingebaut.

1. Entpacken Sie die Linux-Version.
2. Kopieren Sie Ihren Ordner `CRYSTAL DEFENDERS` (oder `58410989`) hinein.
3. Starten Sie `crystal_defenders.sh`. Das Spiel wird beim ersten Start installiert.

Sie lief bisher nur unter WSL, einem Linux innerhalb von Windows (darüber auf einer echten Grafikkarte, aber ohne Controller); auf einem
echten Linux-Desktop oder einem Steam Deck hat sie noch niemand ausprobiert. Der Linux-Teil von DarkLauncher (Fenster, Menüs, Ton, Controller) ist genauso neu und nur auf dieselbe Weise ausprobiert. Auf einem Steam Deck
fügen Sie `crystal_defenders.sh` als Nicht-Steam-Spiel zu Steam hinzu, ohne ein Kompatibilitätstool
zu erzwingen. Die Windows-Version läuft auch über Proton.

## Bekannte Grenzen

- Keine Online-Bestenlisten: Sie brauchten Xbox LIVE. Die eigenen Bestenlisten des Spiels auf dem Computer funktionieren.
- Keine Speicherstände des Zustands: Das Spiel ist ein vollständiges Xbox-360-Programm mit eigenen
  Threads; es speichert selbst.
- Musikschleifen werden von der Laufzeitumgebung behandelt statt vom eigenen Mechanismus der
  Konsole.
- Bisher von sehr wenigen Personen getestet; Controller wurden weniger ausprobiert als die Tastatur,
  PlayStation-Controller und Vibration noch nicht auf echter Hardware.

## Für Entwickler

Wie das Projekt funktioniert, wie man es baut, den Code des Spiels neu übersetzt, prüft und das
Installationsprogramm erstellt: [docs/DEVELOPERS.md](docs/DEVELOPERS.md) (auf Englisch).

Was sich in jeder Version geändert hat (jetzt 1.09): [CHANGELOG.md](CHANGELOG.md) (auf Englisch).

## Häufige Fragen

**Ist das ein Emulator?** Nein. Das Programm des Spiels selbst wird im Voraus in nativen Code für den
PC übersetzt (statische Rekompilierung); was die Hardware der Konsole tat (Grafik, XMA-Ton,
Systemfunktionen), ist für Windows neu geschrieben.

**Warum brauche ich meine eigene Kopie?** Weder das Repository noch die Releases enthalten Bilder,
Töne, Musik oder Texte des Spiels: Das Programm liest sie aus Ihrer Kopie, nachdem es geprüft hat,
dass sie die erwartete ist.

**Kann ich meine Spielstände von der Konsole verwenden?** Die Spielstände liegen in `saves\`, im
eigenen Format des Spiels; Spielstände zwischen Konsole und PC zu kopieren wurde nicht ausprobiert.

**Wird etwas ins Internet gesendet?** Nur die Suche nach neuen Versionen: bei jedem Start eine
Anfrage an GitHub nach der neuesten Version dieses Repositorys (GitHub sieht Ihre IP-Adresse, wie
bei jeder Seite). Nichts über Sie oder Ihr Spiel wird gesendet. Schalten Sie sie unter
Einstellungen, Internet ab, dann benutzt das Programm das Netzwerk überhaupt nicht.

## Pläne

- Ein echter Test auf einem Linux-PC und einem Steam Deck.
- Die Töne des Menüs und mehr von der künstlerischen Gestaltung des Spiels im Design von
  DarkLauncher.
- Mehr Tests mit Controllern und auf anderen Computern.

## Lizenz

Der Quellcode dieses Projekts steht unter der MIT-Lizenz (siehe [LICENSE](LICENSE)). Diese Lizenz
gilt nicht für das Spiel, seine Daten oder seinen Code. DarkLauncher (`kit/launcher`) steht ebenfalls unter der MIT-Lizenz, mit eigenem Rechteinhaber ([kit/launcher/LICENSE](kit/launcher/LICENSE)).

## Danksagungen

Dieses Projekt wurde zu 100 % mit [Claude Code](https://claude.com/claude-code) (Claude Opus 5.5)
erstellt, dem Programmierassistenten von Anthropic: Der gesamte Code, die Werkzeuge und die
Dokumentation wurden von ihm geschrieben, nach den Wünschen und Tests des Autors des Projekts.

- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) von hedge-dev: der Rekompilierer, der den
  Code des Spiels übersetzt.
- [DarkLauncher](kit/launcher): der Einstellungsbildschirm, das
  Pausenmenü und die Bildfilter, entwickelt für Kahna.
- [FFmpeg](https://ffmpeg.org) (LGPL): XMA-Dekodierung.
- [Xenia](https://xenia.jp): seine Dokumentation der Xbox 360, als Referenz genutzt.
- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): der Compiler des Programms und des
  Installationsprogramms.

Und natürlich Square Enix, die Crystal Defenders geschaffen haben.
