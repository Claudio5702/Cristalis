#!/bin/sh
# Assembles the Linux release: the program only, with no game data. Run on Linux
# after building with -DCD_FFMPEG_ROOT=<static FFmpeg>, so that the program needs
# nothing from the system but the C library, SDL2 and OpenGL.
# Usage: package_linux.sh <build directory> <output directory>
set -e
build=${1:?build directory}
out=${2:?output directory}
name=CrystalDefendersRecomp-linux-x86_64
dest="$out/$name"

rm -rf "$dest"
mkdir -p "$dest"
cp "$build/crystal_defenders" "$dest/"
strip "$dest/crystal_defenders"

cat > "$dest/crystal_defenders.sh" <<'EOF'
#!/bin/sh
# Starts the game from its own folder (saves and settings are kept next to it).
cd "$(dirname "$(readlink -f "$0")")" && exec ./crystal_defenders "$@"
EOF
chmod +x "$dest/crystal_defenders.sh" "$dest/crystal_defenders"

cat > "$dest/README.txt" <<'EOF'
CrystalDefendersRecomp - unofficial native Linux build (experimental)

This folder does NOT contain the game, only the program that runs it. You need
your own copy of Crystal Defenders (Xbox 360, Xbox Live Arcade): the
"CRYSTAL DEFENDERS" (or "58410989") folder as found on the console's storage.

Install:
  1. Copy your CRYSTAL DEFENDERS folder into this folder, next to
     crystal_defenders.
  2. Run crystal_defenders.sh. The game is installed from that folder on the
     first launch, then starts.

Steam Deck (NOT TESTED on a real Deck): in Desktop Mode do step 1, then in
Steam choose "Add a Non-Steam Game" and pick crystal_defenders.sh. Do not force
a compatibility tool: this is a Linux program.

Needs: x86-64 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36, SteamOS 3.5 and later), SDL2 and
OpenGL 3.3.

Keys (change them in crystal_defenders.ini, created on first launch):
  Arrows ............. d-pad          Space or X ......... A (confirm)
  Escape or C ........ B (back)       S / D .............. X / Y
  Enter / Tab ........ Start / Back   Q / E .............. shoulder buttons
  F11 or Alt+Enter ... full screen
Any controller SDL2 recognises works.

Not in this build yet: the settings menu and the
achievement pop-up. Language, volume, keys and rendering are set in
crystal_defenders.ini
(section [jeu], langue = fr / en / de / es / it / ja).

Saves and achievements: the userdata folder. Log: crystal_defenders.log.

Audio decoding uses FFmpeg (https://ffmpeg.org), LGPL 2.1 or later, built in
with only the XMA decoders. The program's source code, which allows rebuilding
it against another FFmpeg, is at
https://github.com/Claudio5702/CrystalDefendersRecomp
EOF

tar -C "$out" -czf "$out/$name.tar.gz" "$name"
ls -la "$dest" "$out/$name.tar.gz"
