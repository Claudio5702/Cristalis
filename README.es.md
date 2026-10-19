# Cristalis

**Crystal Defenders Recomp — adaptación nativa por recompilación estática**

*[English](README.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · **Español** · [Italiano](README.it.md) · [日本語](README.ja.md)*

![La pantalla de ajustes, DarkLauncher con los colores del juego](docs/screenshots/launcher-en.png)

Este proyecto ejecuta Crystal Defenders, el juego de Xbox Live Arcade, como un programa nativo de
Windows: el código PowerPC del juego se traduce de antemano a C++ con
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) y se ejecuta sobre un pequeño entorno
escrito solo para este juego. Hay una versión experimental para Linux.

**No se incluye ningún recurso del juego**, ni en este repositorio ni en las releases: las imágenes,
los sonidos, la música y los textos se leen de tu propia copia del juego. El repositorio tampoco
contiene código del juego: el código traducido se genera a partir de un `default.xex` al compilar el
programa, y los programas de la [página de releases](https://github.com/Claudio5702/Cristalis/releases/latest) se compilan así. **Versión 1.02, una
beta**: jugada por pocas personas hasta ahora.

**Necesitas tu propia copia de Crystal Defenders (Xbox 360).** Crystal Defenders pertenece a Square
Enix; este proyecto no está afiliado a Square Enix ni cuenta con su aprobación.

**Contenido:** [Jugar](#jugar) · [Funciones](#funciones) · [Pantalla de ajustes](#pantalla-de-ajustes) ·
[Controles](#controles) · [Archivos](#archivos) · [Desinstalar](#desinstalar) · [Linux](#linux) ·
[Límites conocidos](#límites-conocidos) · [Para desarrolladores](#para-desarrolladores) ·
[Preguntas frecuentes](#preguntas-frecuentes) · [Planes](#planes) · [Licencia](#licencia) ·
[Agradecimientos](#agradecimientos)

## Jugar

Descarga la última versión de la [página de releases](https://github.com/Claudio5702/Cristalis/releases/latest), y solo de ahí:
`Cristalis-Setup.exe` para Windows, `Cristalis-linux-x86_64.tar.gz` para Linux.

1. Copia el juego desde tu consola: la carpeta `CRYSTAL DEFENDERS` (o la carpeta con el
   identificador del título, `58410989`) tal como está en el almacenamiento de la consola, por
   ejemplo en una memoria USB.
2. Con el instalador: pon esa carpeta junto a `Cristalis-Setup.exe` y ejecútalo. El juego
   se instala desde ella, con accesos directos y una entrada en la lista de aplicaciones de Windows.
   Sin la carpeta, la última ventana del instalador pregunta dónde está.
3. Sin el instalador: ejecuta `crystal_defenders.exe`. En el primer arranque pide esa carpeta (o una
   carpeta que ya contenga `default.xex` y `Media`), o la toma enseguida si está junto al programa.

Los datos del juego se guardan entonces en una carpeta `game` junto al programa; la carpeta de la
consola ya no hace falta. Guarda estos archivos para ti.

Windows 10 u 11, 64 bits, una tarjeta gráfica con Direct3D 11.

### El juego que hay que usar

| | |
| --- | --- |
| Identificador del título | `58410989` (Xbox Live Arcade) |
| Paquete de la consola | `58410989\000D0000\18F4739D37853F8596CE621540680922AFE9F23A`, 58.126.336 bytes, SHA-1 `118C64C901976CD8E2B570D77B0A5E01C15C7A46` |
| `default.xex` dentro de él | 1.658.880 bytes, SHA-1 `31A90704D4233F6E730714515CD5F20BF08D7F0D` |

El programa comprueba que `default.xex` es aquel del que se tradujo su código, y lo indica si no lo
es.

## Funciones

- **El juego en [DarkLauncher](https://github.com/Claudio5702/DarkLauncher)**, el lanzador creado
  para Kahna (Bahamut Lagoon Recomp): una pantalla de ajustes antes del juego y un menú de pausa
  durante la partida, en siete idiomas, con teclado, mando o ratón. Su aspecto sigue la dirección
  artística del juego: el azul de sus ventanas, el oro de sus cristales, una noche estrellada.
  DarkLauncher lo dibuja todo; nada se toma de los archivos del juego.
- **Los seis idiomas del juego** (francés, inglés, alemán, español, italiano, japonés), elegidos en
  los ajustes; el juego se reinicia en el nuevo idioma.
- **Imagen a través de la tarjeta gráfica**: el juego dibujado a 1280×720, 1920×1080, 2560×1440 o
  3840×2160, y luego nítido, suavizado o con bordes suavizados en cualquier tamaño de ventana;
  scanlines, un filtro CRT plano o curvo, HDR.
- **Logros guardados en el ordenador**, con un aviso sobre el juego y una lista en los ajustes.
- **El comportamiento sin conexión de la consola, si se desea**: el aviso «No conectado a Xbox LIVE»
  y la elección del dispositivo de almacenamiento están desactivados por defecto y se pueden activar.
- **Capturas de pantalla** de la imagen tal como se muestra, filtros incluidos, y una galería en los
  ajustes.
- **Mandos**: Xbox (XInput), y PlayStation 4 y 5 leídos directamente (USB o Bluetooth), con
  vibración en ambos; teclas y botones elegidos en la página Controles. Un mando de PlayStation 3
  necesita el controlador DsHidMini.
- **Un instalador y un desinstalador**, en seis idiomas.

## Pantalla de ajustes

Al arrancar, DarkLauncher muestra su pantalla de ajustes, con el juego en marcha en la vista previa.
**Jugar** lo inicia; durante la partida, **Esc** (o el botón View del mando, mantenido) abre un menú
de pausa sobre la imagen detenida: volver a la partida, captura, ajustes, salir.

Una barra a la izquierda reúne Jugar, Capturas, Juego, Logros y Ajustes, con Salir abajo del todo; la
página de la entrada seleccionada se muestra a su lado. Intro o Derecha entra en ella, Esc vuelve.

- **Capturas**: las capturas de pantalla tomadas, las más recientes primero. Intro abre una, Supr
  dos veces la envía a la Papelera de reciclaje.
- **Juego**: el aviso «No conectado a Xbox LIVE» y la elección del dispositivo de almacenamiento
  (ambos como en una consola sin conexión; a partir del siguiente arranque).
- **Logros**: los doce logros del juego, con la fecha de cada uno y lo que pide.
- **Ajustes**: una página que lleva al resto.
  - **Idiomas**: el idioma del juego (el juego se reinicia en él) y el del menú.
  - **Imagen**: la resolución interna (1280×720, 1920×1080, 2560×1440 o 3840×2160), un preajuste,
    pantalla completa, escala entera, imagen por la tarjeta gráfica o por software, filtro,
    scanlines, filtro CRT, HDR, sincronización vertical, mostrar los fotogramas por segundo,
    «Probar en grande» y accesibilidad (alto contraste, filtro para daltónicos, menos movimiento,
    ayuda en grande).
  - **Sonido**: el volumen y los sonidos del menú.
  - **Controles**: las teclas del teclado y los botones del mando del juego, la vibración y los
    atajos.
  - La carpeta del juego y los ajustes por defecto.

![La página Imagen](docs/screenshots/picture-en.png)

## Controles

Las teclas siguen su **posición física** (la misma en teclados AZERTY y QWERTY); todas se pueden
cambiar en la página Controles.

| Juego (Xbox 360) | Teclado (posiciones QWERTY) | Mando (PlayStation) |
| --- | --- | --- |
| Cruceta | Flechas | Cruceta / stick izquierdo |
| A (aceptar) / B (atrás) | X / Z | A / B (equis / círculo) |
| X / Y | S / A | X / Y (cuadrado / triángulo) |
| LB / RB | Q / W | LB / RB (L1 / R1) |
| Start / Back | Intro / Mayús derecha | Start / Back (Options / Share); mantenido, Back abre el menú de pausa |

`Esc`: menú de pausa — `F11` o `Alt+Intro`: pantalla completa — `F12`: captura en `captures\` —
`Tab` (mantenido): avance rápido.

## Archivos

Todo se queda en la carpeta del programa:

| | |
| --- | --- |
| `game\` | los datos del juego, sacados de tu copia en el primer arranque |
| `userdata\` | las partidas guardadas del juego y su registro de logros obtenidos |
| `saves\` | la lista de logros de DarkLauncher, con sus fechas |
| `captures\` | capturas de pantalla |
| `crystal_defenders.ini` | ajustes |
| `crystal_defenders.log` | registro de la última sesión (`.prev.log`: la anterior) |

## Desinstalar

Instalado con el instalador: ejecuta `Uninstall.exe` en la carpeta del juego, o quita «Crystal
Defenders» de la lista de aplicaciones instaladas en la configuración de Windows. Por defecto, tus
partidas, ajustes, capturas y los datos del juego se quedan en la carpeta, por si reinstalas; marca
la casilla para borrarlos también, y la carpeta se va con ellos. Sin el instalador: borra la
carpeta.

## Linux

**Experimental.** Una versión nativa para Linux x86-64 con glibc 2.35 o posterior (Ubuntu 22.04,
Debian 12, Fedora 36, SteamOS 3.5 y posteriores), con el mismo DarkLauncher que en Windows: su
pantalla de ajustes, menú de pausa, siete idiomas, mandos (Xbox y PlayStation, con vibración),
logros y tema. El juego dibuja con OpenGL 3.3. Necesita OpenGL, FreeType y fontconfig del sistema;
SDL3 viene incluido, en `lib/`, y FFmpeg está integrado.

1. Descomprime la versión para Linux.
2. Copia dentro tu carpeta `CRYSTAL DEFENDERS` (o `58410989`).
3. Ejecuta `crystal_defenders.sh`. El juego se instala en el primer arranque.

Hasta ahora solo se ha ejecutado en WSL (sobre una tarjeta gráfica real a través de él, pero sin
mando); nadie la ha probado aún en un escritorio Linux real ni en una Steam Deck. En una Steam Deck,
añade `crystal_defenders.sh` a Steam como juego ajeno a Steam, sin forzar una herramienta de
compatibilidad. La versión de Windows también funciona con Proton.

## Límites conocidos

- Sin clasificaciones en línea: necesitaban Xbox LIVE.
- Sin guardado de estado: el juego es un programa completo de Xbox 360 con sus propios hilos; guarda
  por sí mismo.
- Los bucles de música los gestiona el entorno en lugar del mecanismo propio de la consola.
- Probado por muy pocas personas hasta ahora; los mandos se han probado menos que el teclado, y los
  mandos de PlayStation y la vibración aún no en hardware real.

## Para desarrolladores

Cómo funciona el proyecto, cómo compilarlo, volver a traducir el código del juego, comprobarlo y
crear el instalador: [docs/DEVELOPERS.md](docs/DEVELOPERS.md) (en inglés).

Lo que cambió en cada versión (ahora 1.02): [CHANGELOG.md](CHANGELOG.md) (en inglés).

## Preguntas frecuentes

**¿Es un emulador?** No. El propio programa del juego se traduce de antemano a código nativo para PC
(recompilación estática); lo que hacía el hardware de la consola (gráficos, sonido XMA, funciones
del sistema) está escrito de nuevo para Windows.

**¿Por qué necesito mi propia copia?** Ni el repositorio ni las releases contienen las imágenes, los
sonidos, la música ni los textos del juego: el programa los lee de tu copia, tras comprobar que es la
esperada.

**¿Puedo usar mis partidas de la consola?** Las partidas están en `userdata\`, en el formato propio
del juego; copiar partidas entre la consola y el PC no se ha probado.

**¿Se envía algo por Internet?** No. El programa no usa la red en absoluto.

## Planes

- Una prueba real en un PC con Linux y en una Steam Deck.
- Los sonidos del menú y más de la dirección artística del juego en el tema de DarkLauncher.
- Más pruebas con mandos y en otros ordenadores.

## Licencia

El código fuente de este proyecto está bajo la licencia MIT (ver [LICENSE](LICENSE)). Esa licencia
no cubre el juego, sus datos ni su código, ni DarkLauncher (`kit/launcher`), que se incluye compilado
bajo su propia licencia ([kit/launcher/LICENSE.txt](kit/launcher/LICENSE.txt)): libre de usar y de
redistribuir sin cambios con el juego, pero no de código abierto.

## Agradecimientos

Este proyecto se hizo al 100 % con [Claude Code](https://claude.com/claude-code) (Claude Opus 5.5),
el asistente de programación de Anthropic: todo el código, las herramientas y la documentación los
escribió él, siguiendo las peticiones y las pruebas del autor del proyecto.

- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) de hedge-dev: el recompilador que traduce
  el código del juego.
- [DarkLauncher](https://github.com/Claudio5702/DarkLauncher): la pantalla de ajustes, el menú de
  pausa y los filtros de imagen, creados para Kahna.
- [FFmpeg](https://ffmpeg.org) (LGPL): la decodificación XMA.
- [Xenia](https://xenia.jp): su documentación de la Xbox 360, usada como referencia.
- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): el compilador del programa y del instalador.

Y, por supuesto, a Square Enix, que creó Crystal Defenders.
