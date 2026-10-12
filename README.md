# Ignition — port nativo por ingeniería inversa

Reconstrucción en **C11 + SDL3** de *Ignition* (Unique Development Studios, 1997) a partir del ejecutable DOS
original (`MAINDOS.EXE`, DOS/4GW, Watcom C). El objetivo es un juego nativo para **macOS**, y después **Windows** e
**iOS**, fiel al original. Cada sistema se porta tras desensamblarlo y documentarlo.

> **Datos del juego**: este repositorio no contiene ni debe contener archivos del juego (son propiedad de sus autores).
> Hace falta una copia original (p. ej. GOG), por defecto en `/Users/Shared/Epic Games/Ignition/ignition`.
> `.gitignore` excluye ejecutables, imágenes y volcados derivados.

---

## Índice

1. [Estado en una línea](#estado-en-una-línea)
2. [Compilar y ejecutar](#compilar-y-ejecutar)
3. [Estructura del proyecto](#estructura-del-proyecto)
4. [Herramientas de ingeniería inversa](#herramientas-de-ingeniería-inversa)
5. [Lo que ya está hecho](#lo-que-ya-está-hecho)
6. [Cobertura del desensamblado](#cobertura-del-desensamblado)
7. [Mapa del binario](#mapa-del-binario)
8. [Estructuras de datos conocidas](#estructuras-de-datos-conocidas)
9. [Desviaciones conocidas respecto al original](#desviaciones-conocidas-respecto-al-original)
10. [Roadmap](#roadmap)
11. [Metodología](#metodología)

---

## Estado en una línea

Se pueden **recorrer vueltas completas** en las 7 pistas: física, choques con muros y entre coches, turbo, vueltas,
parrilla de salida y coches bien montados (sombra y dirección incluidas), contra hasta 7 rivales. Los rivales usan una
**IA aproximada**. Todavía no hay fin de carrera ni clasificación, y faltan el sombreado original, los objetos
animados, los efectos, el HUD, los menús, el sonido, los fantasmas y el multijugador.

| Métrica | Valor |
|---|---|
| Funciones candidatas detectadas | **809** (bytes de código cubiertos: 83 %) |
| Instrucciones únicas desensambladas | **107 474** |
| Funciones con nombre en `tools/symbols.txt` | 65 (≈ 25 800 instrucciones, el 24 %) |
| Código propio del juego (sin libc/DOS4G/drivers, < `0x60000`) | ≈ 74 700 instrucciones, ≈ 34 % identificado |
| Formatos de datos propios descifrados | 10 de 21 (TRI, POS y TAB solo en parte) |
| Código del port | ≈ 2 300 líneas de C (`src/`) |

---

## Compilar y ejecutar

Requisitos en macOS: `brew install cmake sdl3`.

```sh
cmake -S . -B build
cmake --build build
./build/ignition USA
```

Pistas: `USA`, `CANADA`, `AUSTRIA`, `BRAZIL`, `CARIB`, `ICELAND`, `JAPAN`. La ruta de datos se cambia con la variable
`IGNITION_DATA=/ruta/al/juego`.

### Controles

| Tecla | Acción |
|---|---|
| ↑ / W | acelerar |
| ↓ / S | frenar / marcha atrás |
| ← → / A D | girar |
| Espacio | turbo (la carga aparece en el título de la ventana) |
| F1 | cámara libre (W/A/S/D, Q/E, flechas, Shift = rápido) |
| Esc | salir |
| Teclear `BANARNE` | truco original: carrocerías ×3 de alto |

### Opciones de línea de comandos

| Opción | Uso |
|---|---|
| `--rivals N` | número de rivales (0–7; 3 por defecto en ventana) |
| `--shot out.ppm` | modo sin ventana: simula y guarda un fotograma 320×200 |
| `--sim SEG DIR` | simula `SEG` segundos; `DIR` = −1/0/1 (dirección fija), `auto` (IA) o `turbo` (recta con turbo) |
| `--cam x y z yaw pitch` | cámara fija para `--shot` |
| `--probe PISTA x z alto` | consulta de suelo en un punto |
| `--model ID yaw giro balanceo cabeceo PISTA` | visor de coches (requiere `--shot` antes) |

Variables de entorno de depuración: `IGN_TRACE=1` (traza por tick y estadísticas de tramos y puertas),
`IGN_TRACK_DUMP=archivo` (vuelca los tramos), `IGN_LIFT=u` (eleva el coche en el visor para ver la sombra) e
`IGN_BANARNE=1` (carrocería alta en el visor).

Ejemplos:

```sh
./build/ignition JAPAN --rivals 7
./build/ignition BRAZIL --sim 200 auto --rivals 7 --shot /tmp/x.ppm
./build/ignition --shot coche.ppm --model 9 0.6 30 0.15 0 USA
python3 build/ppm2png.py coche.ppm && open coche.png
```

---

## Estructura del proyecto

```
CMakeLists.txt          objetivos: ignition_core (biblioteca), ignition (ejecutable), elf (volcado ELF)
src/game/assets.{c,h}   carga de COL, PIC, PLC, MSH, TEX, SRF, TRI; consulta de suelo; muros; mapeo motor↔PLC
src/game/physics.{c,h}  cuerpo rígido (modelo bicicleta), par motor, muros, choques entre coches, tablas por coche
src/game/car.{c,h}      coche: suelo bajo 4 ruedas, vertical, dirección, acelerador, turbo, pose visual, vueltas
src/game/track.{c,h}    clases PLC, orden de tramos y bifurcaciones, puertas por tramo (TRI)
src/game/ai.{c,h}       IA de rivales (aproximación)
src/render/raster.{c,h} rasterizador por software de 8 bits 320×200, z-buffer, texturas con perspectiva
src/platform/main.c     SDL3: ventana, entrada, bucle fijo de 72 Hz, cámaras, modos sin ventana y visor
docs/formats.md         referencia técnica detallada (formatos, fórmulas, direcciones)
tools/le2elf.py         LE (DOS/4GW) → ELF32 plano con reubicaciones aplicadas
tools/disasm.py         desensamblador recursivo (Capstone) con símbolos, referencias y tablas de salto
tools/ignassets.py      PIC/COL/TEX/DAT/nivel → PNG
tools/symbols.txt       símbolos identificados (funciones, globales, tablas)
.github/skills/ignition-re/SKILL.md   skill del proyecto para agentes de IA
```

---

## Herramientas de ingeniería inversa

```sh
python3 -m venv .venv && .venv/bin/pip install capstone
.venv/bin/python tools/disasm.py funcs            # lista de funciones
.venv/bin/python tools/disasm.py coverage         # estados por función y cobertura del código
.venv/bin/python tools/disasm.py dis 1aea0        # una función
.venv/bin/python tools/disasm.py all > build/all.asm   # todo, para buscar con grep
.venv/bin/python tools/disasm.py xref "LAP"       # funciones que usan cadenas con "LAP"
.venv/bin/python tools/disasm.py callers 4d5f4    # quién llama a car_input
.venv/bin/python tools/disasm.py refs 1fefe0      # quién usa g_cars
.venv/bin/python tools/disasm.py mem a9e98 -n 4   # leer doubles (-f i/I/h/p/s: enteros, punteros, cadenas)
.venv/bin/python tools/ignassets.py pic ARCHIVO.PIC -o build/ref
cmake --build build --target elf                  # build/maindos.elf para Ghidra/IDA/Hopper
```

- El analizador descubre funciones desde la entrada, desde los punteros a código guardados en datos (reubicaciones
  del LE), desde los inmediatos `push`/`mov` que apuntan a código y desde las tablas de salto de los `switch` de Watcom.
- Convención de llamada de Watcom: argumentos en `eax, edx, ebx, ecx` y el resto en la pila (`cdecl` para varargs).
- `sub_00055f9c` (`__CHP`, truncado de la FPU) aparece antes de cada `fistp`: no es lógica del juego.
- Skill de proyecto para agentes (GitHub Copilot CLI, VS Code): [`.github/skills/ignition-re`](.github/skills/ignition-re/SKILL.md)
  resume el flujo de trabajo, las herramientas, las convenciones y la verificación. En Copilot CLI, `/ignition-re`.

---

## Lo que ya está hecho

### Binario y cadena de herramientas

- [x] Cargador LE propio: 3 objetos, 41 233 reubicaciones de 32 bits y 71 relativas, entrada `0x55fbc`.
- [x] Conversión a ELF para usar desensambladores externos (Hopper/REA no funcionaba con el LE).
- [x] Desensamblador recursivo con símbolos, referencias cruzadas, cadenas, punteros y tablas de salto.
- [x] Identificación del motor interno de UDS: **"Lisa 2 Development System"** (`mem.c`, `geputget.c`, API `LI_*`).

### Formatos de datos

| Formato | Contenido | Estado |
|---|---|---|
| COL | paleta 256×RGB | ✅ portado |
| PIC | imagen con paleta | ✅ portado (fondo de carga) |
| TEX / DAT | texturas 256×N de 8 bits | ✅ portado |
| PLC | colocación de objetos (pistas y coches) | ✅ portado |
| MSH | mallas: vértices + caras de 44 B (UV 8.8, página, tipo de superficie) | ✅ portado |
| SRF | rejilla de consulta de suelo (celdas, listas A/B, spans trapezoidales) | ✅ portado |
| TRI | bordes izquierdo/derecho por tramo (puertas de la IA) | 🟡 bordes portados; `double`s sin descifrar |
| POS | animaciones de objetos (fotogramas pos+rot) | 🟡 descifrado y verificado byte a byte; no se aplica |
| TAB | tabla de sombreado `color×nivel` (64 niveles) | 🟡 significado conocido; no se usa |
| PAN / SHD | tablas 256×256 de 64 KB | 🔴 función exacta desconocida |
| CARS | 11 coches × (carrocería + 4 ruedas) | ✅ portado y montado como en `cars_init` |
| LFT / FNT | fuentes | 🔴 solo cabeceras |
| CDP | vídeos de introducción | 🔴 solo cabecera |
| PSQ / PFM | coche 3D del menú | 🔴 |
| GST | coches fantasma | 🔴 |
| BTZ | configuración y récords | 🔴 |
| INF | curvas del sonido del motor por coche | 🔴 |
| WAV / PAT / XI / OGG | sonido (PCM, Gravis, FastTracker, pistas de CD) | 🔴 formatos estándar; falta el motor de sonido |

### Sistemas del juego descifrados y portados

| Sistema | Original | Port | Fidelidad |
|---|---|---|---|
| Bucle fijo de física a 72 Hz | `frame_time` `0x2b734` | `main.c` | fiel |
| Cuerpo rígido (modelo bicicleta) | `body_step` `0x1e090` | `physics.c` | fiel |
| Curvas de par (Hermite) | `torque_spline` `0x53e70` | `physics.c` | fiel |
| Acelerador, freno y marcha atrás | `car_throttle` `0x4db90`, `car_gearbox` `0x502c4` | `car.c` | acelerador fiel; caja de cambios simplificada |
| Dirección | `car_steer` `0x500f0` | `car.c` | fiel |
| Agarre por superficie y coche | tablas `0xc33f0`, `0xc4dc0` | `car.c` | fiel (dificultad `0xc5470` = 1) |
| Consulta de suelo | `ground_query` `0x20814` | `assets.c` | fiel + desviación (caras > 80°) |
| Suelo bajo 4 ruedas, ruedas anómalas | `car_update` `0x303xx` | `car.c` | fiel |
| Vertical: gravedad, saltos, aterrizaje | `car_vertical` `0x2e234` | `car.c` | fiel + desviación (límite hacia arriba) |
| Muros (aristas no compartidas) | `collect_walls` `0x33ee8` | `assets.c` | fiel |
| Choque con muros | `box_vs_walls` `0x1f708`, `wall_impulse` `0x1f3fc` | `physics.c` | fiel |
| Choque entre coches | pase en `0x2d2f8`, `0x1ebb4`, `0x1e638` | `physics.c`, `car.c` | fiel (falta el caso de solape) |
| Tramos, bifurcaciones y saltos | `split_plc_types` `0x247b0`, `build_sections` `0x28f08` | `track.c` | fiel |
| Vueltas y fases | `car_update` | `car.c` | fiel |
| Parrilla de salida | `cars_init` `0x26458` | `main.c` | fiel |
| Turbo | `car_turbo` `0x4e2d8`, `car_input` `0x4d8eb` | `car.c` | fiel (sin sonido) |
| Montaje del coche | `cars_init` `0x265f6` | `car.c` | fiel |
| Pose: dirección visual, inclinación | `0x48a14`, `0x40ec8`, `0x2e6f8` | `car.c`, `raster.c` | fiel |
| Sombra del coche | `0x48a14` | `raster.c` | geometría fiel; sombreado aproximado |
| Truco BANARNE | `cheat_check` `0x160c4` | `main.c` | fiel |
| IA de rivales | `ai_drive` `0x1aea0` | `ai.c` | ⚠️ **aproximación**: usa sus datos (puertas TRI, parámetros), pero no su ley de control |
| Render 3D | motor Lisa 2 | `raster.c` | propio: z-buffer, sin sombreado TAB |

---

## Cobertura del desensamblado

Instrucciones únicas por zona del segmento de código. "Identificado" cuenta las instrucciones de funciones con nombre en
`symbols.txt`, estén portadas o solo identificadas.

| Zona | Contenido | Funciones | Instrucciones | Identificado |
|---|---|---:|---:|---:|
| `0x10000–0x1a000` | frontend: menús, idioma, opciones, fantasmas, coche del menú | 30 | 7 048 | 31 % |
| `0x1a000–0x20000` | IA, cuerpo físico, choques entre coches | 14 | 5 701 | **93 %** |
| `0x20000–0x2b000` | arranque, carga, creación del mundo, coches, sonido de coche | 47 | 10 893 | 51 % |
| `0x2b000–0x34000` | bucle de carrera, actualización de coches, eventos | 20 | 7 460 | 57 % |
| `0x34000–0x43000` | objetos móviles, colisiones con objetos, efectos, clima | 33 | 14 689 | **11 %** |
| `0x43000–0x4c000` | resultados, pausa, HUD, colocación de objetos | 15 | 8 572 | 51 % |
| `0x4c000–0x54000` | controles, entrada, turbo, fantasmas, utilidades | 40 | 7 474 | 18 % |
| `0x54000–0x60000` | motor Lisa 2: escena, rasterizador, sprites, fuentes | 109 | 12 884 | **6 %** |
| `0x60000–0x86000` | libc de Watcom, DOS/4G, drivers de sonido | 501 | 32 753 | 2 % |
| **Total** | | **809** | **107 474** | **24 %** |

Aún queda un 17 % de bytes de código sin cubrir: datos intercalados, relleno y funciones a las que solo se llega por
caminos que el analizador todavía no sigue.

---

## Mapa del binario

Funciones identificadas por subsistema (ver `tools/symbols.txt` y `docs/formats.md`). Las marcadas con *¿?* son
hipótesis basadas en cadenas o en el uso.

**Arranque y carga**: `_cstart_` `0x55fbc` · `game_init` `0x215d0` · `load_system_gfx` `0x21860` · `load_level`
`0x224d0` · `level_prepare` `0x2236c` · `load_plc/msh/tex` `0x230f4/0x2322c/0x23368` · `load_tri` `0x1ce30` ·
`load_srf_grid` `0x1fee0` · `load_fonts` `0x240f4` · `load_panel_gfx` `0x243e0` · crear mundo *¿?* `0x2489c` ·
crear clima `0x26154` · pantalla de carga `0x21d68` · nombres de pista `0x22f38` · pedir CD `0x219d4`.

**Carrera**: bucle principal *¿?* `0x20d60` · `cars_update_all` `0x2d008` · `car_update` `0x2ed00` · `car_input`
`0x4d5f4` · `human_input` `0x2c5b8` (teclado) · joystick *¿?* `0x4c9b4` · `ai_drive` `0x1aea0` · eliminación
("%s IS OUT") *¿?* `0x2de30` · cambio de malla al reaparecer `0x31858` · efectos de golpe *¿?* `0x2ea44`.

**Física**: `body_step` `0x1e090` · `body_corners` `0x1fdb4` · `box_vs_walls` `0x1f708` · `wall_impulse` `0x1f3fc` ·
`body_vs_body` `0x1ebb4` · `body_pair_impulse` `0x1e638` · `car_vertical` `0x2e234` · `ground_query` `0x20814` ·
`collect_walls` `0x33ee8` · `car_turbo` `0x4e2d8` · `car_throttle` `0x4db90` · `car_steer` `0x500f0` · `car_gearbox`
`0x502c4` · `torque_spline` `0x53e70`.

**Objetos y efectos** (cadenas `FEL VID LI_MOVEOBJECT …`, "error en LI_MOVEOBJECT"): `collisions_pass` `0x3fd04` ·
`car_ground_contact` `0x350f0` (colisión con objetos) · objetos deslizantes `0x3731c` · sprites translúcidos `0x39d1c`,
`0x39fe8`, `0x3e6ec` · *plots* `0x3a2b4`, `0x3a498` · piezas volando `0x3b744` · relámpagos `0x3bdac` · salpicaduras
`0x3d808` · clima `0x403c8` · animación POS `0x412e8` · crear efecto *¿?* `0x3fee8` · tipos de objeto `0x360f0`,
`0x3636c`, `0x36ce0`.

**Render y presentación**: `render_scene` `0x56410` · `race_setup_video` `0x492fc` · `camera_angles` `0x5891c` ·
`render_main` `0x43fe0` (resultados y puntuaciones) · `car_place_objects` `0x48a14` · HUD `0x4a320` · tiempos
`0x4bdf4` · dígitos `0x4be7c` · depuración XYZ *¿?* `0x49fdc` · pausa `0x4395c` · salir o reiniciar `0x43e48` ·
captura TGA `0x515f8` · fondo del menú `0x541d4`.

**Frontend**: `menu_load_gfx` `0x11504` · opciones y BTZ *¿?* `0x12bb8` · idioma y menú principal *¿?* `0x170ac` ·
lista de fantasmas `0x17054` · `menu_load_car` `0x1aaf8` · trucos `0x160c4`.

**Fantasmas y persistencia**: cargar o crear fantasma `0x2adf8` · guardar fantasma `0x2ccc8` · mover fantasma
`0x50e94` · guardar configuración `0x2b430`, `0x2cf60` · apagado `0x2b6b4`.

**Sonido**: sonidos de coche (ROLL, SKID…) `0x2a4ec` · cargar muestras `.WAV/.PAT/.RW8` `0x63310` · reproducir sonido
*¿?* `0x49a8c` · canal *¿?* `0x626fc` · detección de Sound Blaster (`BLASTER`) `0x7a85c` · *Script Player* `0x549b0`.

**Motor Lisa 2 y runtime**: API de objetos `LI_MOVEOBJECT` `0x56d24`, `LI_PLACEOBJECT` `0x56c74`, cambiar malla
`0x56e9c` · `engine_alloc_objects` `0x569ec` · memoria `mem.c` (`pool_alloc` `0x64e84`, `0x64db8`, `0x650f8`, `0x651c8`)
· sprites `geputget.c` `0x771e0` · fuentes `0x61220` · aleatorio ("FLUM SLUMP", *slump* = azar) `0x51714` · tecla
pulsada `0x55c28` · palabra tecleada `0x55cd0` · `atan2` `0x647ef` · `pow` `0x64c2a` · `fmod` `0x65782` · `rand`
`0x64774` · `memset` `0x65367` · `sprintf` `0x62775` · E/S `0x653d3…0x6577d`.

### Datos del juego que aún no se han tocado

- `GENERAL/*.TEX`: texturas de efectos (BOOM, DARKSMOK, DIVSPR, EXSMOKE, LIGHT, SMOKE).
- `GENERAL/SOUND/{BOOST,COLL,DIV,ROLL,SKID}`, `CARS/<coche>/SOUND/{00_A.WAV,01_A.WAV,ENGINE.INF}` y
  `LEVELS/<pista>/SOUND/*.PAT|WAV`: ambiente por pista.
- `MUSIC/Track02–08.ogg`: música (pistas de CD).
- `FONTS/*.LFT`, `IGNITION.FNT`; `*_PANGFX.PIC`, `H_PAN*.PIC`, `*_SIGNS.PIC`, `POKAL.PIC`: HUD, señales y trofeo.
- `BALTAZAR/DATA`: menú (MENU.COL/TAB, MENUCAR.*, BILAR/CAR_SEL/DEMOCAR/DEMOTRK/FLAGGOR/IGN_LOGO/MARK/CONSPR/TRK_SPR.PIC,
  `*.LFT`, `IGN*.CDP`, `DEFAULT2.PSQ`, `TEST2.PFM`).
- `LEVELS/<pista>/<pista>.{PAN,SHD,TAB,POS}`.
- Nombres internos de las pistas: ICELAND = CAPETHOR, CANADA = MOOSEJAW (Moosejaw Falls), USA = GOLDRUSH,
  CARIB = SNAKEISL, AUSTRIA = YODELPEA, JAPAN = TOKYOBUL, BRAZIL = LOSTRUIN.

---

## Estructuras de datos conocidas

**Coche** (`g_cars` `0x1fefe0`, `0x484c` bytes por coche; solo los campos verificados):

| Offset | Campo |
|---|---|
| `+0x00/+0x08/+0x10` | posición en el mundo x, y (altura) y z |
| `+0x18/+0x28`, `+0x30/+0x40` | desplazamiento del último tick y del anterior |
| `+0x20` | velocidad vertical |
| `+0xb0` | dirección visual (grados) |
| `+0xb8/+0xc0`, `+0xc8/+0xd0`, `+0xd8/+0xe0` | inclinación dinámica, amortiguación 0,97 y límites 35°/20° |
| `+0xf8` | rumbo |
| `+0x108/+0x110` | balanceo y cabeceo del suelo |
| `+0x120/+0x128` | suelo actual y anterior |
| `+0x148` | agarre medio |
| `+0x270` | en el aire |
| `+0x2fc..0x314` / `+0x31c..0x334` | x / z de las 4 ruedas (sombra y suelo) |
| `+0x344` | entrada de dirección ±100 |
| `+0x364`, `+0x370`, `+0x374`, `+0x378` | tramo (IA), tramo bajo el coche, vuelta y fase |
| `+0x534/+0x53c/+0x548` | carga, modo y espera del turbo |
| `+0x544` | canal de sonido |
| `+0x554` | contador de vuelco *¿?* |
| `+0x558/+0x55c/+0x560` | solape con otro coche |
| `+0x57c` | rueda sobre borde (0x50/0x51) |
| `+0x584/+0x58c` | inclinación de la carrocería (décimas de grado) |

**Cuerpo físico** (`g_bodies` `0x1fefe8`, `0x1d0` B): masa `+0x00`, inercia `+0x08`, restitución `+0x10`, ejes
`+0x18/+0x20`, rumbo `+0x28`, guiñada `+0x30`, posición `+0x68/+0x70`, velocidad `+0x78/+0x80`, velocidad
longitudinal/lateral `+0x98/+0xa0`, dirección `+0xac`, cabeceo/balanceo `+0xb4/+0xbc`, agarre `+0xc4/+0xcc`, fuerza
motriz `+0xd8`, límites `+0xe0/+0xe8`, caja `+0xf0/+0x110`, radio `+0x130`, esquinas `+0x138`, impacto `+0x1b8`.

**Otros**: tramo (`g_sections` `0x1feff8`, 24 B: objeto, ángulo `double`, …) · objeto del motor (42 B, `[0x24c058]`)
· estado de IA (`[0xeb7c0]`, `0x7c` B por coche) · parámetros de IA (`0xbfb30`, 9 `double` por coche) · trucos
(`0x1fb2f0[7]`) · estado del rasterizador (`0xc4840`).

---

## Desviaciones conocidas respecto al original

1. **IA**: ley de control propia (`ai.c`), no la de `0x1aea0`. 
2. **Render**: z-buffer propio en lugar del rasterizador del motor Lisa 2; sin sombreado TAB/SHD/PAN; la sombra se
   oscurece con una tabla de paleta al 50 %.
3. **Suelo**: se ignoran las caras de más de ~80° y las degeneradas.
4. **Vertical**: el límite de seguimiento del suelo también se aplica hacia arriba (evita saltos de capa).
5. **Cámara**: cámara de persecución propia (falta la original de `camera_angles`).
6. **Dificultad**: factor de agarre trasero fijo a 1,0 (`0xc5470`).

---

## Roadmap

Prioridad: **P1** = necesario para un juego completo de un jugador · **P2** = fidelidad y contenido · **P3** = extras.
Cada tarea indica las direcciones de partida.

### Fase 0 — Infraestructura de ingeniería inversa

- [x] **P1** Inicializar git y subir solo código, documentación y herramientas (los datos ya están excluidos).
- [ ] **P1** Separar la libc de Watcom y DOS/4G del código del juego con firmas (estilo FLIRT) generadas a partir de
      Watcom C 10.x/11, para descartar unas 33 000 instrucciones de `0x60000–0x86000` sin revisarlas.
- [ ] **P1** Cubrir el 17 % de bytes restante: barrido lineal de huecos con heurísticas de prólogo Watcom
      (`push ebx; push ecx…; sub esp`), validando cada candidata con las reubicaciones.
- [ ] **P2** Exportar `symbols.txt` como script de Ghidra o IDA (nombres, comentarios y tipos de estructura) para
      decompilar las funciones grandes (`0x43fe0`, `0x2ed00`, `0x3731c`, `0x1aea0`).
- [x] **P2** Generar estados por función en `symbols.txt` (`unknown` / `identified` / `ported` / `verified`) y un
      informe automático de cobertura (`disasm.py coverage`).
- [ ] **P2** Pruebas de regresión deterministas: carreras sin ventana por pista con resultados esperados (tramos,
      vueltas, sin NaN) e imágenes de referencia del visor.
- [ ] **P3** Trazas de ejecución del original en DOSBox-X (depurador o registro de memoria) para comparar valores
      tick a tick con el port: posición, velocidad y tramo.

### Fase 1 — Carrera completa de un jugador (jugabilidad)

- [ ] **P1** Portar 1:1 la **IA original** `0x1aea0` (1 869 instrucciones): elección de carril (`+0x64/+0x66`,
      byte `+0x6d` de TRI), consulta de puertas `0x1decc`, velocidad objetivo por curvatura y pendiente
      (`curveinflu`, `slopeinflu`), frenada (`carebed`), adelantamientos (`careother`, `aggr`), turbo (`0x1ca15`).
      Descifrar los `double`s de los bloques TRI y el estado de 0x7c B.
- [x] **P1** Arreglar el atasco en la bifurcación de CANADA (tramo −34): la IA usa las puertas de la rama B (`gate_b`) en tramos negativos.
- [ ] **P1** Orden de carrera, posiciones, fin de carrera y número de vueltas por modo.
- [ ] **P1** Evento de eliminación `0x2de30` ("BIL %d RACEEND %d BLOW", "%s IS OUT", "WARNING, YOU'RE LAST!"):
      ¿se elimina al último de cada vuelta? Portar la lógica y la explosión.
- [ ] **P1** Solape entre coches `0x31858` (|dx|,|dz| < 25: cambio de malla y reaparición) y efectos de golpe
      `0x2ea44` (impacto > 15).
- [ ] **P1** Reaparición o recolocación tras caer o volcar (`car+0x554`, contador de vuelco).
- [ ] **P1** Entrada original: teclado `0x2c5b8` (rampas de dirección), joystick `0x4c9b4`, `car_input` completo
      (`0x4f874`, `0x50464`, `0x50824`, `0x4e1bc` sin identificar).
- [ ] **P2** Caja de cambios completa `0x502c4` (selección de marcha por régimen).
- [ ] **P2** Dificultad `0xc5470` y tipos de control `0x1fefd0[coche]+4` (1, 2, 7).
- [ ] **P2** Superficies especiales `0x12c–0x131` (meta, rampas…) y `0x6e–0x83`.
- [ ] **P2** Trucos restantes: identificar el efecto de SURMULE (`0x1fb2f4`), SLASKTRATT (`0x1fb2f8`),
      FILMJOLK (`0x1fb2fc`, `0x420cc`), SKUNK (`0x1fb300`, muy usado en efectos y `0x48a14`), SVINPOLE (`0x1fb304`) y
      STRINGS (`0x1fb308`).

### Fase 2 — Mundo vivo: objetos, colisiones y efectos

- [ ] **P1** Aplicar las animaciones **POS** (`pos_apply_frame` `0x412e8`) a los objetos de la pista.
- [ ] **P1** Colisiones con objetos móviles `car_ground_contact` `0x350f0` y los tipos `0x360f0`, `0x3636c` y
      `0x36ce0` (pase `collisions_pass` `0x3fd04`).
- [ ] **P1** Objetos deslizantes `0x3731c` (2 264 instrucciones; puertas, barreras…).
- [ ] **P2** Objeto especial PLC tipo `0xfa` (`0x2abdc`).
- [ ] **P2** Efectos: piezas volando `0x3b744`, humo y explosiones (texturas `GENERAL/*.TEX`), salpicaduras `0x3d808`,
      relámpagos `0x3bdac`, *plots* `0x3a2b4`/`0x3a498`, sprites translúcidos `0x39d1c`/`0x39fe8`/`0x3e6ec`, crear
      efecto `0x3fee8`.
- [ ] **P2** Clima *¿?*: creación `0x26154` y actualización `0x403c8`.
- [ ] **P2** Mapeo exacto entre los objetos del motor y la PLC (hoy se infiere al 88–98 %): portar la creación del
      mundo `0x2489c` ("Error while creating the world") para obtener el orden real.

### Fase 3 — Render fiel

- [ ] **P1** Rasterizador del motor Lisa 2 (`0x54000–0x60000`, ~12 900 instrucciones): orden de dibujo, recorte,
      relleno y z (o algoritmo del pintor).
- [ ] **P1** Sombreado **TAB**: cómo se elige el nivel por cara (bits bajos de `flags`: `0x11/0x13/0x15`; byte alto
      ≥ 64 = sin sombra) y luz por distancia o normal.
- [ ] **P1** Función de **SHD** y **PAN** (tablas de 64 KB indexadas con `(a<<8)|b`: ¿transparencia, mezcla, niebla?).
- [ ] **P1** Cámara original `camera_angles` `0x5891c` (décimas de grado) y sus modos de vista.
- [ ] **P2** `render_scene` `0x56410`: capas según los flags de configuración `[0x24c05c]+0x38..0x54`.
- [ ] **P2** Resoluciones 320×200 / 640×480 / 800×600 y viewports por jugador (`race_setup_video` `0x492fc`).
- [ ] **P2** Sprites (`geputget.c`, `0x771e0`) y señales (`*_SIGNS.PIC`).
- [ ] **P3** Escalado moderno (pantalla completa, relación de aspecto, filtrado opcional).

### Fase 4 — HUD, menús y flujo de juego

- [ ] **P1** Fuentes **LFT/FNT** (`load_fonts` `0x240f4`, `0x61220`).
- [ ] **P1** HUD en carrera `0x4a320`: tiempos, vuelta, posición, velocímetro, turbo, avisos multilingües; gráficos
      `*_PANGFX.PIC`, `H_PAN*.PIC`.
- [ ] **P1** Pantallas de resultados y puntuación (`render_main` `0x43fe0`: TRACK RESULTS, TRACK SCORE, TOTAL SCORE)
      y trofeo `POKAL.PIC`.
- [ ] **P1** Pausa, continuar, salir y reiniciar (`0x4395c`, `0x43e48`).
- [ ] **P1** Menús: `menu_load_gfx` `0x11504`, menú principal e idioma `0x170ac`, opciones `0x12bb8`, selección de
      coche (`BILAR.PIC`, `CAR_SEL.PIC`, coche 3D del menú `0x1aaf8` con **PSQ/PFM**), selección de pista
      (`DEMOTRK.PIC`, `TRK_SPR.PIC`), fondo `MENUBKG.DAT` (`0x541d4`).
- [ ] **P2** Modos de juego (campeonato, contrarreloj, práctica…), desbloqueos y flujo entre pistas.
- [ ] **P2** Pantallas de carga (`0x21d68`) e idiomas (inglés, alemán, francés, italiano, español y sueco).
- [ ] **P2** Configuración y récords en **BTZ** (`0x2b430`, `0x2cf60`, `IGN_DOS.BTZ`).
- [ ] **P3** Vídeos **CDP** (`IGN1.CDP`, `IGN2.CDP`, `IGN3_*.CDP`) y *Script Player* `0x549b0`.
- [ ] **P3** Capturas TGA `0x515f8` y superposición de depuración `0x49fdc`.

### Fase 5 — Sonido y música

- [ ] **P1** Sustituir el driver (Sound Blaster/GUS, `0x7a85c` y `0x60000+`) por SDL3 audio con un mezclador propio.
- [ ] **P1** Cargador de muestras `0x63310` (`.WAV`, `.PAT` de Gravis y `.RW8`) y formato **XI**.
- [ ] **P1** Motor de cada coche: **ENGINE.INF** (curvas por régimen) + `00_A.WAV`/`01_A.WAV` con tono según las
      revoluciones.
- [ ] **P1** Efectos: BOOST, COLL, DIV, ROLL, SKID (`0x2a4ec`); reproducción posicional `0x49a8c` y canales `0x626fc`.
- [ ] **P2** Ambiente por pista `LEVELS/<pista>/SOUND` (PAT/WAV) y su activación por zona.
- [ ] **P2** Música `MUSIC/Track02–08.ogg` (antes pistas de CD): asignación por pista y menú.

### Fase 6 — Fantasmas y persistencia

- [ ] **P2** Formato **GST**: cargar o crear (`0x2adf8`), guardar (`0x2ccc8`) y reproducir (`0x50e94`); compatibilidad
      con los fantasmas del original (`GHOSTS/*.GST`).
- [ ] **P2** Récords de vuelta ("LAP RECORD") y su persistencia.

### Fase 7 — Multijugador

- [ ] **P2** Pantalla partida para 2 jugadores (viewports en `0x492fc`; `0x26154` avisa "does not support more than 2
      ca…").
- [ ] **P3** Red: localizar el código de red del original (IPX/módem/serie) o diseñar uno nuevo con UDP; nombres sobre
      los coches (tipo de control 2, `0x48a14`).

### Fase 8 — Plataformas y distribución

- [ ] **P1** Bundle `.app` de macOS con selector de la carpeta de datos (sin rutas fijas).
- [ ] **P2** Windows: CMake + SDL3 (MSVC/MinGW), CI con GitHub Actions para macOS y Windows.
- [ ] **P2** iOS: SDL3 en iOS, controles táctiles e importación de los datos por el usuario (no se pueden incluir).
- [ ] **P3** Mandos (SDL gamepad), remapeo de teclas y guardado en una carpeta de usuario.

### Fase 9 — Cierre del desensamblado

- [ ] **P2** Nombrar todas las funciones del juego (`< 0x60000`, ~300) y documentar sus estructuras.
- [ ] **P2** Clasificar el motor Lisa 2 (`0x54000–0x60000`, 109 funciones): API `LI_*`, memoria, sprites, fuentes,
      escena. Basta con equivalencias, no hace falta portarlo línea a línea.
- [ ] **P3** Verificar con firmas que `0x60000–0x86000` es solo libc, DOS/4G y drivers, y documentar las excepciones.

---

## Metodología

1. **Localizar** la función por cadenas (`xref`), globales (`refs`), constantes `double` o llamadas (`callers`).
2. **Leer** el ensamblado (`dis`), resolver constantes y tablas con `Image.find`, y anotar el resultado en
   `tools/symbols.txt` y `docs/formats.md`.
3. **Portar** a C manteniendo nombres y comentarios con la dirección original (p. ej. `/* sub_0004e2d8 */`).
4. **Verificar** sin ventana: simulaciones `--sim`, trazas `IGN_TRACE`, imágenes `--shot` y el visor `--model`, en
   las 7 pistas y sin avisos de compilación.
5. **Marcar las desviaciones** explícitamente en el código y en este README.

Convenciones del port: Y hacia abajo; 1 m = 21,76 unidades del mundo; rumbo 0 = +z; ángulo físico
`= π/2 − rumbo`; las coordenadas SRF y TRI son las del mundo + 25 600.
