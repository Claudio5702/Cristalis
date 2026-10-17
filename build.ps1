# Builds build\crystal_defenders.exe, next to the FFmpeg DLLs it needs, from the translated code in
# recomp\ppc (made by recompile.ps1) and the runtime in recomp\runtime.
#
#   .\build.ps1                build what changed
#   .\build.ps1 -Clean         build everything again
#   .\build.ps1 -DarkLauncher  the game inside DarkLauncher (kit\launcher), in build-dl\
#
# Needs clang, CMake and Ninja on the PATH. The game can then be started from build\: the first
# launch asks for your own copy of the game, or finds it in extracted\ (see the README).
param([switch]$Clean, [switch]$DarkLauncher)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root $(if ($DarkLauncher) { 'build-dl' } else { 'build' })
$option = $(if ($DarkLauncher) { 'ON' } else { 'OFF' })
if (-not (Test-Path -LiteralPath (Join-Path $root 'recomp\ppc\ppc_recomp_shared.h'))) {
    throw "recomp\ppc is empty: run .\recompile.ps1 first (it needs your own default.xex)"
}
# never while that copy of the game runs: its program could not be replaced
$exe = Join-Path $build 'crystal_defenders.exe'
if (Get-Process crystal_defenders -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
    throw "$exe is running: close the game first"
}

if (-not (Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
    & cmake -S (Join-Path $root 'recomp') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCD_DARKLAUNCHER=$option"
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
}
# -Clean: every object again (the settings, saves and screenshots of test runs in the folder stay)
if ($Clean) { & cmake --build $build --clean-first } else { & cmake --build $build }
if ($LASTEXITCODE -ne 0) { throw "build failed" }
"built $exe"
