# Builds build\crystal_defenders.exe, next to the FFmpeg DLLs it needs, from the translated code in
# recomp\ppc (made by recompile.ps1) and the runtime in recomp\runtime.
#
#   .\build.ps1          build what changed
#   .\build.ps1 -Clean   build everything again
#
# Needs clang, CMake and Ninja on the PATH. The game can then be started from build\: the first
# launch asks for your own copy of the game, or finds it in extracted\ (see the README).
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
if (-not (Test-Path -LiteralPath (Join-Path $root 'recomp\ppc\ppc_recomp_shared.h'))) {
    throw "recomp\ppc is empty: run .\recompile.ps1 first (it needs your own default.xex)"
}

if (-not (Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
    & cmake -S (Join-Path $root 'recomp') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
}
# -Clean: every object again (the settings, saves and screenshots of test runs in build\ stay)
if ($Clean) { & cmake --build $build --clean-first } else { & cmake --build $build }
if ($LASTEXITCODE -ne 0) { throw "build failed" }
"built $(Join-Path $build 'crystal_defenders.exe')"
