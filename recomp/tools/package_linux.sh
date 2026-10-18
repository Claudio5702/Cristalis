#!/bin/sh
# Assembles the Linux release: the program only, with no game data. Run on Linux after building with
# DarkLauncher (CD_DARKLAUNCHER, the default) and -DCD_FFMPEG_ROOT=<static FFmpeg>, so that the
# program needs nothing from the system but the C library, OpenGL, FreeType and fontconfig: SDL3,
# which few systems have yet, goes into lib/, where the program looks first.
# Usage: package_linux.sh <build directory> <output directory>
set -e
build=${1:?build directory}
out=${2:?output directory}
here=$(cd "$(dirname "$0")" && pwd)
name=CrystalDefendersRecomp-linux-x86_64
dest="$out/$name"

rm -rf "$dest"
mkdir -p "$dest/lib"
cp "$build/crystal_defenders" "$dest/"
strip "$dest/crystal_defenders"
sdl=$(ldd "$build/crystal_defenders" | awk '/libSDL3/ {print $3}')
[ -n "$sdl" ] && cp -L "$sdl" "$dest/lib/"

# The window's icon: the 256 x 256 picture of the program's .ico (kept there as PNG), beside the
# program, where DarkLauncher looks for it.
ico="$here/../resources/crystal_defenders.ico"
n=$(od -An -t u2 -j 4 -N 2 "$ico" | tr -d ' ')
k=0
while [ "$k" -lt "$n" ]; do
  e=$((6 + 16 * k))
  width=$(od -An -t u1 -j "$e" -N 1 "$ico" | tr -d ' ')
  if [ "$width" = 0 ]; then
    size=$(od -An -t u4 -j $((e + 8)) -N 4 "$ico" | tr -d ' ')
    at=$(od -An -t u4 -j $((e + 12)) -N 4 "$ico" | tr -d ' ')
    dd if="$ico" of="$dest/crystal_defenders.png" bs=1 skip="$at" count="$size" 2>/dev/null
  fi
  k=$((k + 1))
done

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
     first launch, then the settings screen (DarkLauncher) opens: Play starts it.

Steam Deck (NOT TESTED on a real Deck): in Desktop Mode do step 1, then in
Steam choose "Add a Non-Steam Game" and pick crystal_defenders.sh. Do not force
a compatibility tool: this is a Linux program.

Needs: x86-64 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36,
SteamOS 3.5 and later), OpenGL 3.3, FreeType and fontconfig (standard on desktop
systems). SDL3 comes with it, in lib/.

The settings screen: Play, Screenshots, Game, Achievements, Languages (the
game's and the menu's), Picture (internal resolution up to 3840 x 2160, filters,
full screen), Sound, Controls (keys, controller buttons, vibration).

In the game:
  Escape, or the controller's Back/View held ... pause menu
  F11 or Alt+Enter ............................. full screen
  F12 .......................................... screenshot (captures folder)

Keys (QWERTY positions; all can be changed on the Controls page):
  Arrows ............. d-pad          X / Z .............. A (confirm) / B (back)
  S / A .............. X / Y          Q / W .............. LB / RB
  Enter / Right Shift  Start / Back
Xbox controllers, and PlayStation 4 and 5 ones, work as they are.

Saves: the userdata folder; achievements: userdata and saves. Settings:
crystal_defenders.ini. Log: crystal_defenders.log.

Audio decoding uses FFmpeg (https://ffmpeg.org), LGPL 2.1 or later, built in
with only the XMA decoders. SDL3 (https://libsdl.org) is under the zlib license.
The program's source code, which allows rebuilding it against another FFmpeg,
is at https://github.com/Claudio5702/CrystalDefendersRecomp
EOF

tar -C "$out" -czf "$out/$name.tar.gz" "$name"
ls -la "$dest" "$dest/lib" "$out/$name.tar.gz"
