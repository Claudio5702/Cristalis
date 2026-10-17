# Translates the game's PowerPC code to C++ again (recomp\ppc) with XenonRecomp, regenerates the
# import table, then rebuilds build\crystal_defenders.exe with build.ps1.
#
#   .\recompile.ps1                     uses extracted\default.xex, or build\game\default.xex
#   .\recompile.ps1 -Xex <default.xex>  your own copy of the game's executable
#   .\recompile.ps1 -XenonRecomp <XenonRecomp.exe>
#
# XenonRecomp needs the changes listed in recomp\tools\README.md. The translated code is a copy of
# the game's own program: it stays on your computer and is never put in the repository.
param([string]$Xex, [string]$XenonRecomp)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $Xex) {
    $Xex = @('extracted\default.xex', 'build\game\default.xex') | ForEach-Object { Join-Path $root $_ } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $Xex) { throw "default.xex not found: pass -Xex <path to your own default.xex>" }
}
if (-not (Test-Path -LiteralPath $Xex)) { throw "not found: $Xex" }
if (-not $XenonRecomp) { $XenonRecomp = Join-Path $root 'tools\build-xenonrecomp\XenonRecomp\XenonRecomp.exe' }
if (-not (Test-Path -LiteralPath $XenonRecomp)) { throw "XenonRecomp not found: pass -XenonRecomp <path to XenonRecomp.exe>" }
$context = Join-Path $root 'tools\XenonRecomp-main\XenonUtils\ppc_context.h'

# The configuration names the executable and the output folder relative to itself; a copy beside it
# points them at the chosen executable and at recomp\ppc.
$config = Join-Path $root 'recomp\config'
$ppc = Join-Path $root 'recomp\ppc'
$toml = Join-Path $config 'recompile.tmp.toml'
New-Item -ItemType Directory -Force $ppc | Out-Null
$text = Get-Content -LiteralPath (Join-Path $config 'crystal_defenders.toml') -Raw
$text = $text -replace '(?m)^file_path = .*$', ('file_path = "' + ((Resolve-Path -LiteralPath $Xex).Path -replace '\\', '/') + '"')
$text = $text -replace '(?m)^out_directory_path = .*$', ('out_directory_path = "' + ($ppc -replace '\\', '/') + '"')
[IO.File]::WriteAllText($toml, $text)
# XenonRecomp is given the configuration by its name, from its own folder: it crashes when given
# a full path to it (and finds the jump table file named inside it from the current folder).
Push-Location -LiteralPath $config
try {
    & $XenonRecomp (Split-Path -Leaf $toml) $context
    if ($LASTEXITCODE -ne 0) { throw "XenonRecomp failed (exit code $LASTEXITCODE)" }
} finally {
    Pop-Location
    Remove-Item -LiteralPath $toml -ErrorAction SilentlyContinue
}

& py -3 -I (Join-Path $root 'recomp\tools\gen_imports.py') (Join-Path $ppc 'ppc_recomp_shared.h') (Join-Path $root 'recomp\runtime\imports.gen.cpp')
if ($LASTEXITCODE -ne 0) { throw "gen_imports.py failed" }
& (Join-Path $root 'build.ps1')
