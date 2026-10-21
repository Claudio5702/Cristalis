# Cristalis

**Crystal Defenders Recomp — portage natif par recompilation statique**

*[English](README.md) · **Français** · [Deutsch](README.de.md) · [Español](README.es.md) · [Italiano](README.it.md) · [日本語](README.ja.md)*

![L'écran de réglages, DarkLauncher aux couleurs du jeu](docs/screenshots/launcher-en.png)

Ce projet fait tourner Crystal Defenders, le jeu Xbox Live Arcade, comme un programme Windows
natif : le code PowerPC du jeu est traduit à l'avance en C++ avec
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) et s'exécute sur un petit environnement
écrit pour ce seul jeu. Une version Linux est expérimentale.

**Aucune ressource du jeu n'est fournie**, ni dans ce dépôt ni dans les releases : les images, les
sons, la musique et les textes sont lus dans votre propre copie du jeu. Le dépôt ne contient pas non
plus de code du jeu : le code traduit est produit à partir d'un `default.xex` au moment de compiler le
programme, et les programmes de la [page des releases](https://github.com/Claudio5702/Cristalis/releases/latest) sont compilés ainsi. **Version 1.10,
une bêta** : jouée par peu de personnes jusqu'ici.

**Vous devez fournir votre propre copie de Crystal Defenders (Xbox 360).** Crystal Defenders
appartient à Square Enix ; ce projet n'est ni affilié à Square Enix ni approuvé par elle.

**Sommaire :** [Jouer](#jouer) · [Fonctions](#fonctions) · [Écran de réglages](#écran-de-réglages) ·
[Commandes](#commandes) · [Fichiers](#fichiers) · [Désinstaller](#désinstaller) · [Linux](#linux) ·
[Limites connues](#limites-connues) · [Pour les développeurs](#pour-les-développeurs) ·
[Questions fréquentes](#questions-fréquentes) · [Feuille de route](#feuille-de-route) ·
[Licence](#licence) · [Remerciements](#remerciements)

## Jouer

Téléchargez la dernière version sur la [page des releases](https://github.com/Claudio5702/Cristalis/releases/latest), et uniquement là :
`Cristalis-Setup.exe` pour Windows, `Cristalis-linux-x86_64.tar.gz` pour Linux.

1. Copiez le jeu depuis votre console : le dossier `CRYSTAL DEFENDERS` (ou le dossier qui porte
   l'identifiant du titre, `58410989`) tel qu'il se trouve sur le stockage de la console, par
   exemple sur une clé USB.
2. Avec l'installeur : placez ce dossier à côté de `Cristalis-Setup.exe` et lancez-le.
   Le jeu est installé à partir de ce dossier, avec des raccourcis et une entrée dans la liste des
   applications de Windows. Sans le dossier, la dernière fenêtre de l'installeur demande où il se
   trouve.
3. Compilé vous-même, sans l'installeur : lancez `crystal_defenders.exe`. Au premier démarrage, il demande ce dossier
   (ou un dossier qui contient déjà `default.xex` et `Media`), ou le prend tout de suite s'il se
   trouve à côté du programme.

Les données du jeu sont alors gardées dans un dossier `game` à côté du programme ; le dossier de la
console n'est plus nécessaire. Gardez ces fichiers pour vous.

Windows 10 ou 11, 64 bits, une carte graphique Direct3D 11.

### Le jeu à utiliser

| | |
| --- | --- |
| Identifiant du titre | `58410989` (Xbox Live Arcade) |
| Paquet de la console | `58410989\000D0000\18F4739D37853F8596CE621540680922AFE9F23A`, 58 126 336 octets, SHA-1 `118C64C901976CD8E2B570D77B0A5E01C15C7A46` |
| `default.xex` qu'il contient | 1 658 880 octets, SHA-1 `31A90704D4233F6E730714515CD5F20BF08D7F0D` |

Le programme vérifie que `default.xex` est bien celui dont son code a été traduit, et le dit si ce
n'est pas le cas.

## Fonctions

- **Le jeu dans [DarkLauncher](kit/launcher)**, le lanceur créé pour
  Kahna (Bahamut Lagoon Recomp) : un écran de réglages avant le jeu et un menu de pause en jeu, en
  sept langues, au clavier, à la manette ou à la souris. Son apparence suit la direction artistique
  du jeu : le bleu de ses fenêtres, l'or de ses cristaux, une nuit étoilée. DarkLauncher dessine
  tout lui-même ; rien n'est pris dans les fichiers du jeu.
- **Les six langues du jeu** (français, anglais, allemand, espagnol, italien, japonais), choisies
  dans les réglages ; le jeu redémarre dans la nouvelle langue.
- **Affichage par la carte graphique** : le jeu dessiné en 1280×720, 1920×1080, 2560×1440 ou
  3840×2160, puis net, lissé ou aux bords adoucis à toutes les tailles de fenêtre ; HDR (Windows).
- **Succès enregistrés sur l'ordinateur**, avec une notification par-dessus le jeu et une liste dans
  les réglages.
- **Le comportement hors ligne de la console, sur demande** : l'avis « Non connecté à Xbox LIVE »
  et le choix du périphérique de stockage sont désactivés par défaut et peuvent être activés.
- **Captures d'écran** de l'image telle qu'elle est affichée, filtres compris, et une galerie dans
  les réglages.
- **Manettes** : Xbox (XInput), ainsi que PlayStation 4 et 5 lues directement (USB ou Bluetooth),
  avec vibration sur les deux ; touches et boutons choisis sur la page Commandes. Une manette
  PlayStation 3 a besoin du pilote DsHidMini.
- **Un rapport de plantage** qui dit où en était la partie (« W1, Stage 2, Wave 5 »), écrit au démarrage suivant.
- **Un installeur et un désinstalleur**, en six langues.
- **Mises à jour** : à chaque lancement, le lanceur regarde si une nouvelle version existe et
  propose de l'installer ; rien n'est installé avant que vous choisissiez « Mettre à jour ». Cela
  se désactive dans Paramètres, Internet.

## Écran de réglages

Au démarrage, DarkLauncher affiche son écran de réglages, avec le jeu qui tourne dans l'aperçu.
**Jouer** le lance ; en jeu, **Échap** (ou le bouton View de la manette, maintenu) ouvre un menu de
pause par-dessus l'image figée : reprendre la partie, capture d'écran, réglages, quitter.

Une barre à gauche réunit Jouer, Captures, Jeu, Succès et Paramètres, avec Quitter tout en bas ; la
page de l'entrée sélectionnée s'affiche à côté. Entrée ou Droite y entre, Échap en revient.

- **Captures** : les captures d'écran prises, les plus récentes d'abord. Entrée en ouvre une,
  Suppr deux fois l'envoie à la Corbeille.
- **Jeu** : l'avis « Non connecté à Xbox LIVE » et le choix du périphérique de stockage (tous deux
  comme sur une console hors ligne ; à partir du démarrage suivant).
- **Succès** : les douze succès du jeu, avec la date de chacun et ce qu'il demande.
- **Paramètres** : une page qui mène au reste.
  - **Langues** : la langue du jeu (le jeu redémarre dans celle-ci) et celle du menu.
  - **Image** : la résolution interne (1280×720, 1920×1080, 2560×1440 ou 3840×2160), un préréglage,
    le plein écran, l'échelle entière, l'affichage par la carte graphique ou par le processeur, le
    filtre, le HDR, la synchro verticale, l'affichage des images par
    seconde, « Essayer en grand » et l'accessibilité (contraste élevé, filtre pour daltoniens,
    moins de mouvement, aide en grand).
  - **Son** : le volume et les sons du menu.
  - **Commandes** : les touches du clavier et les boutons de manette du jeu, la vibration et les
    raccourcis.
  - **Internet** : la recherche de nouvelles versions (activée au départ) et la mise à jour.
  - Le dossier du jeu, et les réglages par défaut.

![La page Image](docs/screenshots/picture-en.png)

## Commandes

Les touches suivent leur **position physique** (la même sur les claviers AZERTY et QWERTY) ; toutes
peuvent être changées sur la page Commandes.

| Jeu (Xbox 360) | Clavier (positions QWERTY) | Manette (PlayStation) |
| --- | --- | --- |
| Croix directionnelle | Flèches | Croix / stick gauche |
| A (valider) / B (retour) | X / Z | A / B (croix / rond) |
| X / Y | S / A | X / Y (carré / triangle) |
| LB / RB | Q / W | LB / RB (L1 / R1) |
| Start / Back | Entrée / Maj droite | Start / Back (Options / Share) ; maintenu, Back ouvre le menu de pause |

`Échap` : menu de pause — `F11` ou `Alt+Entrée` : plein écran — `F12` : capture d'écran dans
`captures\`.

Sur un clavier AZERTY, les positions QWERTY ci-dessus donnent : X / W pour A / B, S / Q pour X / Y
et A / Z pour LB / RB.

## Fichiers

Tout reste dans le dossier du programme :

| | |
| --- | --- |
| `game\` | les données du jeu, extraites de votre copie au premier démarrage |
| `saves\` | vos sauvegardes (`crystaldefendersW123\gamedata.bin`) et les succès obtenus, avec leurs dates |
| `captures\` | les captures d'écran |
| `crystal_defenders.ini` | les réglages |
| `crystal_defenders.log` | le journal de la dernière session (`.prev.log` : celle d'avant) |
| `crystal_defenders.ini.crash.txt` | le rapport de plantage, écrit au démarrage après un plantage |

## Désinstaller

Installé avec l'installeur : lancez `Uninstall.exe` dans le dossier du jeu, ou retirez « Cristalis (Crystal
Defenders) » de la liste des applications installées dans les paramètres de Windows. Par défaut, vos
sauvegardes, vos réglages, vos captures et les données du jeu restent dans le dossier, au cas où vous
réinstalleriez ; cochez la case pour les supprimer aussi, et le dossier part avec eux. Sans
l'installeur : supprimez le dossier.

## Linux

**Expérimental.** Une version native pour Linux x86-64 avec glibc 2.35 ou plus récente (Ubuntu
22.04, Debian 12, Fedora 36, SteamOS 3.5 et suivants), avec le même DarkLauncher que sous Windows :
son écran de réglages, son menu de pause, ses sept langues, les manettes (Xbox et PlayStation, avec
vibration), les succès et le thème. Le jeu dessine avec OpenGL 3.3. Il a besoin d'OpenGL, de
FreeType et de fontconfig sur le système ; SDL3 est fourni, dans `lib/`, et FFmpeg est intégré.

1. Décompressez la version Linux.
2. Copiez-y votre dossier `CRYSTAL DEFENDERS` (ou `58410989`).
3. Lancez `crystal_defenders.sh`. Le jeu est installé au premier lancement.

Une partie complète a été jouée sur un vrai PC sous Bazzite, un Linux proche de SteamOS, en mode Jeu de Steam
avec la nouvelle manette Steam Controller, sans problème constaté ;
elle n'a pas encore été essayée sur un Steam Deck. Elle reste marquée expérimentale : peu de machines l'ont fait tourner jusqu'ici. Sur un
Steam Deck, ajoutez `crystal_defenders.sh` à Steam comme jeu non-Steam, sans forcer d'outil de
compatibilité. La version Windows tourne aussi avec Proton.

## Limites connues

- Pas de classements en ligne : ils demandaient Xbox LIVE. Les meilleurs scores du jeu, gardés sur l'ordinateur, fonctionnent.
- Pas de sauvegardes d'état : le jeu est un programme Xbox 360 complet avec ses propres fils
  d'exécution ; il sauvegarde lui-même.
- Les boucles de musique sont gérées par l'environnement plutôt que par le mécanisme propre à la
  console.
- Testé par très peu de personnes jusqu'ici ; les manettes ont été moins essayées que le clavier,
  et les manettes PlayStation et la vibration pas encore sur du vrai matériel.

## Pour les développeurs

Le fonctionnement du projet, comment le compiler, traduire à nouveau le code du jeu, le vérifier et
faire l'installeur : [docs/DEVELOPERS.md](docs/DEVELOPERS.md) (en anglais).

Ce qui a changé à chaque version (actuellement 1.10) : [CHANGELOG.md](CHANGELOG.md) (en anglais).

## Questions fréquentes

**Est-ce un émulateur ?** Non. Le programme du jeu lui-même est traduit à l'avance en code natif
pour le PC (recompilation statique) ; ce que faisait le matériel de la console (graphismes, son
XMA, fonctions du système) est réécrit pour Windows.

**Pourquoi faut-il ma propre copie ?** Ni le dépôt ni les releases ne contiennent les images, les
sons, la musique ou les textes du jeu : le programme les lit dans votre copie, après avoir vérifié que
c'est la bonne.

**Puis-je utiliser mes sauvegardes de la console ?** Les sauvegardes sont dans `saves\`, au
format du jeu lui-même ; copier des sauvegardes entre la console et le PC n'a pas été essayé.

**Quelque chose est-il envoyé sur Internet ?** Seulement la recherche de nouvelles versions : à
chaque lancement, une demande à GitHub pour la dernière version de ce dépôt (GitHub voit votre
adresse IP, comme pour n'importe quelle page). Rien sur vous ni sur votre partie n'est envoyé.
Désactivez-la dans Paramètres, Internet, et le programme n'utilise plus du tout le réseau.

## Feuille de route

- Un essai sur un Steam Deck.
- Les sons du menu et davantage de la direction artistique du jeu dans le thème de DarkLauncher.
- Plus d'essais avec des manettes et sur d'autres ordinateurs.

## Licence

Le code source de ce projet est sous licence MIT (voir [LICENSE](LICENSE)). Cette licence ne couvre
ni le jeu, ni ses données, ni son code. DarkLauncher (`kit/launcher`) est lui aussi sous licence MIT, avec son propre titulaire ([kit/launcher/LICENSE](kit/launcher/LICENSE)).

## Remerciements

Ce projet a été réalisé à 100 % avec [Claude Code](https://claude.com/claude-code) (Claude Opus
5.5), l'assistant de programmation d'Anthropic : tout le code, les outils et la documentation ont
été écrits par lui, d'après les demandes et les essais de l'auteur du projet.

- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) de hedge-dev : le recompilateur qui
  traduit le code du jeu.
- [DarkLauncher](kit/launcher) : l'écran de réglages, le menu de
  pause et les filtres d'image, créés pour Kahna.
- [FFmpeg](https://ffmpeg.org) (LGPL) : le décodage XMA.
- [Xenia](https://xenia.jp) : sa documentation de la Xbox 360, utilisée comme référence.
- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) : le compilateur du programme et de
  l'installeur.

Et bien sûr Square Enix, qui a créé Crystal Defenders.
