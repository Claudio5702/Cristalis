# Builds the copy that can be given to someone else: the program only, with no
# game data and no save. On first launch it asks for the files of the player's
# own copy of the game. Also produces a .zip of it.
# Usage: share.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$build = Join-Path $root 'recomp\build'
$ffmpeg = Join-Path $root 'tools\ffmpeg\ffmpeg-n8.1-latest-win64-lgpl-shared-8.1'
$name = 'Crystal Defenders (PC) - a partager'
$destination = Join-Path $root $name
$zip = "$destination.zip"

if (Test-Path -LiteralPath $destination) { [IO.Directory]::Delete($destination, $true) }
New-Item -ItemType Directory -Force $destination | Out-Null
foreach ($file in 'crystal_defenders.exe', 'avcodec-62.dll', 'avutil-60.dll', 'swresample-6.dll') {
    Copy-Item -LiteralPath (Join-Path $build $file) -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $ffmpeg 'LICENSE.txt') -Destination (Join-Path $destination 'FFmpeg-LICENSE.txt') -Force

Set-Content -LiteralPath (Join-Path $destination 'LISEZMOI.txt') -Encoding utf8 -Value @(
    'Crystal Defenders - version native PC',
    '',
    'Ce dossier ne contient PAS le jeu : seulement le programme qui le fait',
    'tourner sur PC. Il faut votre propre copie de Crystal Defenders (Xbox 360,',
    'Xbox Live Arcade) : le dossier CRYSTAL DEFENDERS (ou 58410989) tel qu''il',
    'se trouve sur le stockage de la console, copie sur le PC.',
    '',
    'Installation :',
    '  1. Double-cliquer sur crystal_defenders.exe.',
    '  2. Au premier lancement, indiquer ce dossier CRYSTAL DEFENDERS.',
    '     Le jeu en est extrait dans le dossier game\ a cote du programme.',
    '  3. Le jeu demarre. Les fois suivantes, il se lance directement.',
    '',
    '(Un dossier contenant deja default.xex et Media est accepte aussi.)',
    '',
    'Plus simple : deposer le dossier CRYSTAL DEFENDERS dans ce dossier, a cote',
    'de crystal_defenders.exe, avant le premier lancement. Le jeu s''installe',
    'alors tout seul, sans question.',
    '',
    'Steam Deck (NON TESTE sur une vraie machine) : en mode Bureau, deposer le',
    'dossier CRYSTAL DEFENDERS ici ; dans Steam, "Ajouter un jeu non Steam" et',
    'choisir crystal_defenders.exe ; dans Proprietes > Compatibilite, forcer',
    'une version de Proton. Le jeu passe alors en plein ecran 1280x800.',
    '',
    'Windows 10 ou 11, 64 bits, carte graphique compatible Direct3D 11.',
    '',
    'En jeu :',
    '  F1 ou clic droit ...... menu des reglages (langue, resolution, volume...)',
    '  Alt+Entree ou F11 ..... plein ecran',
    '  F12 ................... capture d''ecran (dossier captures\)',
    '',
    'Manette : toute manette XInput (type Xbox) ; les deux sticks ouvrent le menu.',
    'Clavier (modifiable dans crystal_defenders.ini) :',
    '  Fleches ............ croix directionnelle',
    '  Espace ou X ........ A (confirmer)',
    '  Echap ou C ......... B (retour)',
    '  S / D .............. X / Y',
    '  Entree / Tab ....... Start / Back',
    '  Q / E .............. gachettes hautes',
    '',
    'Sauvegardes et succes : dossier userdata\ a cote du programme.',
    'En cas de probleme : crystal_defenders.log decrit la derniere session.',
    '',
    'Le decodage audio utilise FFmpeg (LGPL), voir FFmpeg-LICENSE.txt.'
)

if (Test-Path -LiteralPath $zip) { [IO.File]::Delete($zip) }
Compress-Archive -Path $destination -DestinationPath $zip
'{0} : {1:N1} Mo ; zip : {2:N1} Mo' -f $destination, ((Get-ChildItem -LiteralPath $destination -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), ((Get-Item -LiteralPath $zip).Length / 1MB)
