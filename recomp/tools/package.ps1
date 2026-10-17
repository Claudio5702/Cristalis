# Assembles a self-contained copy of the game that can be moved anywhere:
# the executable, the FFmpeg DLLs, the game data, a launcher and the current save.
# Usage: package.ps1 [-Destination <folder>]
param(
    [string]$Destination = (Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'Crystal Defenders (PC)')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$build = Join-Path $root 'build'
$ffmpeg = Join-Path $root 'tools\ffmpeg\ffmpeg-n8.1-latest-win64-lgpl-shared-8.1'

New-Item -ItemType Directory -Force $Destination | Out-Null

# Program
foreach ($file in 'crystal_defenders.exe', 'avcodec-62.dll', 'avutil-60.dll', 'swresample-6.dll') {
    Copy-Item -LiteralPath (Join-Path $build $file) -Destination $Destination -Force
}
Copy-Item -LiteralPath (Join-Path $ffmpeg 'LICENSE.txt') -Destination (Join-Path $Destination 'FFmpeg-LICENSE.txt') -Force

# Game data, as extracted from the user's own copy of the game
$game = Join-Path $Destination 'game'
New-Item -ItemType Directory -Force $game | Out-Null
Copy-Item -Path (Join-Path $root 'extracted\*') -Destination $game -Recurse -Force

# Current save, if the packaged copy has none yet
$save = Join-Path $root 'recomp\run\userdata'
if ((Test-Path $save) -and -not (Test-Path (Join-Path $Destination 'userdata'))) {
    Copy-Item -LiteralPath $save -Destination $Destination -Recurse
}

Set-Content -LiteralPath (Join-Path $Destination 'Jouer.bat') -Encoding ascii -Value @(
    '@echo off',
    'cd /d "%~dp0"',
    'start "" "crystal_defenders.exe"'
)

Set-Content -LiteralPath (Join-Path $Destination 'LISEZMOI.txt') -Encoding utf8 -Value @(
    'Crystal Defenders - version native PC',
    '',
    'Lancer : double-cliquer sur Jouer.bat.',
    '',
    'Menu des reglages (langue, resolution, rendu, plein ecran, volume,',
    'vibrations, pause en arriere-plan, quitter) - le jeu est en pause tant',
    'qu''il est ouvert :'
    'touche F1, clic droit dans la fenetre, ou appui sur les deux sticks de la',
    'manette. Il se manie a la manette : croix, A pour choisir, B pour fermer.',
    '',
    'Plein ecran : Alt+Entree ou F11. La fenetre se redimensionne librement.',
    'Capture d''ecran : F12, image enregistree dans le dossier captures\.',
    'Le pointeur de la souris disparait au bout de quelques secondes d''immobilite.',
    '',
    'Reglages : crystal_defenders.ini, cree au premier lancement. Il regle les',
    'touches du clavier, le volume, la langue, les vibrations et le demarrage',
    'en plein ecran. Il s''ouvre avec le Bloc-notes.',
    '',
    'Manette : toute manette XInput (type Xbox) est reconnue.',
    'Clavier (par defaut) :',
    '  Fleches ............ croix directionnelle',
    '  Espace ou X ........ A (confirmer)',
    '  Echap ou C ......... B (retour)',
    '  S / D .............. X / Y',
    '  Entree / Tab ....... Start / Back',
    '  Q / E .............. gachettes hautes',
    '',
    'Succes : enregistres dans userdata\achievements.txt, annonces par une petite',
    'fenetre en bas de l''ecran, et listes dans le menu Succes du jeu.',
    '',
    'Sauvegardes : dossier userdata\ a cote de ce fichier.',
    'Journal : crystal_defenders.log (la session precedente est gardee dans',
    'crystal_defenders.prev.log).',
    '',
    'Ce dossier peut etre deplace ou copie tel quel. Le decodage audio utilise',
    'FFmpeg (LGPL), voir FFmpeg-LICENSE.txt.'
)

$size = (Get-ChildItem -LiteralPath $Destination -Recurse -File | Measure-Object Length -Sum).Sum
'{0} : {1:N0} Mo' -f $Destination, ($size / 1MB)
