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

El port implementa **carreras de 3 vueltas** en las 7 pistas: física, choques con muros y entre coches, turbo,
parrilla de salida y coches bien montados (sombra y dirección incluidas), contra hasta 7 rivales. Hay fin de carrera y
clasificación básicos; los rivales usan una **IA aproximada**. POS ya carga y anima los objetos de pista, con rotación
aproximada. El HUD de carrera ya usa el panel y fuentes LFT originales para tiempos, posición y velocidad; faltan la
reproducción exacta de sus estados, aguja/medidores dinámicos y avisos. El **frontend original está portado**: pantalla
de idioma, intro en vídeo CDP, menús en 6 idiomas con su motor de páginas, fondo vectorial animado (idéntico al píxel al
original), carrusel de coches 3D, récords, créditos y configuración en `IGN_DOS.BTZ`; al elegir pista y coche arranca la
carrera y al salir de ella se vuelve al menú. Faltan colisiones móviles completas, la IA fiel, el sombreado original,
los modos de campeonato/contrarreloj/persecución, la música y el multijugador.

| Métrica | Valor |
|---|---|
| Funciones candidatas detectadas | **1 112** (bytes de código cubiertos: 98,5 %; 488 clasificadas como runtime) |
| Instrucciones únicas desensambladas | **128 832** (20 323 de runtime/libc/DOS/4G) |
| Código del juego marcado como portado/verificado | **33 086 de 108 509 instrucciones (30,5 %)**; **120 de 624 funciones (19,2 %)** |
| Código del juego marcado como parcial | **11 785 instrucciones en 14 funciones**; excluido del numerador portado |
| Funciones de juego vinculadas explícitamente a una tarea del roadmap | **202 de 624 (32,4 %)**; runtime excluido y `unmapped` no significa fuera de alcance |
| Formatos de datos propios descifrados | 15 de 21 (TRI, POS y TAB solo en parte) |
| Código del port | ≈ 10 400 líneas de C (`src/`, sin contar las tablas generadas `menu_data.c`) |

Los totales anteriores se regeneran con `disasm.py coverage`; el detalle completo está en
[`docs/function-coverage.txt`](docs/function-coverage.txt) y el inventario conciliable por función en
[`docs/function-roadmap.tsv`](docs/function-roadmap.tsv). La cobertura cuenta instrucciones únicas de las funciones
marcadas `ported` o `verified`, excluye `partial` y las funciones clasificadas heurísticamente como `library`.
Mide cobertura de código original asociado a funciones, no equivalencia instrucción por instrucción. El **98,5 %**
es cobertura del desensamblado, no del port; la clasificación runtime también es heurística.

---

## Compilar y ejecutar

Requisitos en macOS: `brew install cmake sdl3`.

```sh
cmake -S . -B build
cmake --build build
./build/ignition          # frontend: idioma, intro, menús; la carrera sale de lo elegido
./build/ignition USA      # carrera directa, como antes
```

Sin argumentos (o con `--menu`) arranca el **frontend** del original. La configuración se lee y se guarda, como en el
DOS, en `IGN_DOS.BTZ` del directorio del juego; `IGNITION_SAVE=/ruta/archivo.btz` usa otro archivo. La opción
*RESET GHOST CAR* borra de verdad los `GHOSTS/*.GST`. Del menú pasan a la carrera la pista, el coche del jugador, el
número de corredores (6, o 1 en contrarreloj), la dificultad y la caja de cambios; Esc en carrera vuelve al menú.
En el menú: flechas, Enter, Esc/Retroceso; cualquier tecla salta la intro o los créditos.

Pistas: `USA`, `CANADA`, `AUSTRIA`, `BRAZIL`, `CARIB`, `ICELAND`, `JAPAN`. La ruta de datos se cambia con la variable
`IGNITION_DATA=/ruta/al/juego`.

### Controles

| Tecla | Acción |
|---|---|
| ↑ / W | acelerar |
| ↓ / S | frenar / marcha atrás |
| ← → / A D | girar |
| Espacio | turbo (la carga aparece en el título de la ventana) |
| M | alternar cambio manual/automático |
| Q / E | bajar / subir marcha en modo manual |
| F1 | cámara libre (W/A/S/D, Q/E, flechas, Shift = rápido) |
| R (mantener) | vista trasera |
| TAB | tras la meta: cámaras fijas 1–3 / seguir al coche en cabeza |
| Esc | salir |
| Teclear `BANARNE` | truco original: carrocerías ×3 de alto |

### Opciones de línea de comandos

| Opción | Uso |
|---|---|
| `--menu` | arranca el frontend (lo mismo que sin argumentos) |
| `--manual` | empieza con cambio manual (GEARBOX = MANUAL en el menú) |
| `--rivals N` | número de rivales (0–7; 3 por defecto en ventana) |
| `--car N` | modelo del jugador (0–10 de `CARS.PLC`; 0 por defecto) |
| `--shot out.ppm` | modo sin ventana: simula y guarda un fotograma (a la resolución de `--res`) |
| `--res WxH` | resolución interna: 640x480 por defecto (VESA del DOS), 320x200 (modo bajo) u 800x600 (`Ign_win.exe`); siempre a 4:3 |
| `--persp` | texturas con corrección de perspectiva (PERSP. POLY); por defecto afines como el original |
| `--sim SEG DIR` | simula `SEG` segundos; `DIR` = −1/0/1 (dirección fija), `auto` (IA) o `turbo` (recta con turbo) |
| `--sim-ticks N DIR` | con `--shot`: ejecuta exactamente `N` ticks de 1/72 s; mismas opciones de dirección que `--sim` |
| `--cam x y z yaw pitch` | cámara fija para `--shot` |
| `--countdown` | con `--shot`: incluye la cuenta atrás de 5,56 s (la ventana siempre la hace) |
| `--rear` | con `--shot`: fotograma con la vista trasera |
| `--probe PISTA x z alto` | consulta de suelo en un punto |
| `--model ID yaw giro balanceo cabeceo PISTA` | visor de coches (requiere `--shot` antes) |
| `--record-ghost F.GST` | con `--sim`: guarda la vuelta del jugador como fantasma del original (302 412 bytes) |
| `--ghost F.GST` | dibuja el fantasma (sin choques) en ventana y sin ventana; con `--sim` mide la desviación en x/z |

Variables de entorno de depuración: `IGN_TRACE=1` (traza por tick y estadísticas de tramos y puertas),
`IGN_TRACE=full` (estado inicial y cada tick, esquema v5 de 112 campos con controles y precisión de 17 cifras),
`IGN_TRACK_DUMP=archivo` (vuelca los tramos), `IGN_LIFT=u` (eleva el coche en el visor para ver la sombra) e
`IGN_BANARNE=1` (carrocería alta en el visor).

### Verificación diferencial de la física original

Prueba opcional, sin ventana ni DOSBox: ejecuta los bytes originales de `body_step` (`0x1e090`) en Unicorn x86-32
y compara cada tick con la biblioteca C del port. Requiere la versión de `MAINDOS.EXE` identificada por SHA-256;
una dependencia ausente, otro ejecutable, una ruta no admitida o una divergencia hacen fallar la prueba.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tests/requirements-original.txt
cmake -S . -B build -DIGN_VERIFY_ORIGINAL=ON -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build
ctest --test-dir build -R '^(physics_original|car_original|car_kernel_contract)$' --output-on-failure
```

El informe `build/physics-original-report.json` conserva hashes del original, del ejecutable de prueba, de las
fuentes y de las entradas, versiones y errores máximos. Hay **190 casos / 1 658 ticks**: tracción, marcha atrás,
saturación y sus límites, giro, fricción, derrape, pendientes y movimiento en el aire. Se comparan 33 campos
`double` y dos indicadores enteros. La tolerancia de las salidas es `2e-11 + 2e-12·|original|`; los indicadores
y los campos no modificados deben coincidir exactamente. No se ha encontrado una divergencia que justifique
cambiar `ign_body_step`.

Esto verifica **la rutina aislada, con x87 emulado y palabra de control `0x037f`**, no el juego completo,
`car_update`, colisiones, IA ni una carrera DOS. Véanse los límites y la
[revisión de los avisos de Ghidra](docs/formats.md#auditoría-de-la-decompilación-ghidra).
La regresión general preexistente sigue pasando solo **1/23** casos (divergencias de imágenes y de estado
de carrera). No se han cambiado los resultados esperados para convertir esas diferencias en un éxito.

### Kernels del coche verificados contra el original

El oracle `tests/car_original.py` pasa **42 530 casos / 302 572 pasos** contra los bytes LE:
dirección completa `0x500f0` (732 casos), bloque de agarre `0x3070a..0x30874` (672) y aritmética vertical
`0x2e234`, hasta la frontera de efectos de aterrizaje o retorno (252), más aceleración (6 829),
freno (72), aritmética de turbo (2 016), revoluciones (25 016) e integración acelerador→revoluciones (6 941).
Usa los kernels reales de `car.c`, sin reemplazar sonido/efectos por respuestas inventadas.
Las mayores diferencias numéricas son `7.11e-15`, `1.78e-15` y `1.43e-14`, respectivamente, dentro
de la tolerancia del oracle; los indicadores son exactos. Los informes conservan procedencia y cobertura.

Esta prueba confirmó y permitió corregir dos errores de agarre: si una rueda tiene un tipo fuera de 0..29,
se conserva la media anterior (inicial 0,4) y ambos agarres; los rivales usan perfil 11 y su propia tabla
trasera por modelo/dificultad, no la fórmula humana. Dirección, altura y `body_step` no necesitaron
cambiar sus fórmulas. Hay fixtures de límites numéricos, aleatorios y secuencias válido→inválido→válido.

Los tres bloques nuevos pasan **8 917 casos / 127 230 pasos**. Se corrigieron la truncación x87 del índice
de par, los límites originales de patinaje, el bloqueo por freno y el turbo agotado: limpia modo/frames
y no añade fuerza. Las diferencias máximas son `1.82e-12` (aceleración), cero (freno) y `9.32e-10` (turbo),
dentro de la misma tolerancia; los indicadores son exactos. Las curvas de par son entradas suministradas
a ambos kernels, no una verificación de su generación.

Revoluciones añade **31 957 casos / 166 942 pasos**, contando la integración (que repite deliberadamente
el acelerador). El tick reutiliza su índice final de par, incluido patinaje, en vez de recalcularlo.
Se corrigieron las reglas de IA: en el aire solo tiende al acelerador si entrada > 80; antes de salida
usa la relación de marcha y no reduce revoluciones por coche terminado. Humanos y rivales usan
sus respectivas tablas; el derrape bajo 60 km/h se aplica después y solo el humano terminado reduce
a cero. Errores máximos: `1.43e-14` aislado y `3.64e-12` integrado. Se prueban índices 0..199,
las tres marchas, umbrales, estados de salida/aire/derrape/fin, aleatorios y secuencias de transición.

El oracle de entradas `tests/input_original.py` añade **33 360 casos / 100 572 pasos**, sin tolerancia:
pedales dirigidos (180), arbitraje freno/marcha atrás (27 648), caja automática (5 526) y cambios manuales (6).
Ejecuta bytes LE sin parchear, incluido el freno y `__CHP` reales; evita audio mediante la rama original.
La fuerza se calcula con la marcha previa; la selección ocurre después. Manual permite 0–2 y el freno
no selecciona atrás. Automático respeta los umbrales estrictos, curvas compartidas del modelo 0,
comparación `int16` con signo y contador **> 30**, que se borra al coincidir el objetivo en la siguiente llamada.
Los fixtures incluyen vecinos de umbrales, pedales simultáneos, empates, extremos `int16` y transiciones.
`tests/gearbox.c` comprueba además el orden dentro del tick y la independencia de curvas locales/dificultad.
El informe `build/regression-clean/input-original-report.json` conserva hashes, cobertura y exclusiones.
Véase [el alcance por bloque](docs/formats.md#dispatcher-de-pedales-y-selección-de-marchas-verificación-ejecutable).

El arbitraje de solicitudes de turbo añade **10 728 casos / 82 440 pasos** exactos (modo J):
el informe de entradas suma **44 088 / 183 012**. Corrige el gate de salida para petición 1,
la exclusión de IA del aviso de negativa y el contador que solo avanza mientras se pide 1
(una negativa deja 29; soltar conserva la espera). Petición 2 fuerza modo 2 sin gates.
Se prueban vecinos de carga 20/100 y reloj cero, estados de fin/modo/espera y secuencias de 65 llamadas.
**Prueba composicional**, no dispatcher continuo: ejecuta decisiones hasta audio/efectos y compone
independientemente la escritura de espera y el contador originales, sin parchear ni sustituir callees.
No verifica efectos secundarios de esos callees; generación/latch se verifican en el bloque siguiente.
Véanse [fronteras y supuestos](docs/formats.md#solicitudes-de-turbo-verificación-por-fragmentos-originales).

Generación humana y latch añaden **5 184 casos / 31 104 pasos** exactos (modo P):
el informe de entradas suma **49 272 / 214 116**. Solo sondeo igual a 1 cuenta como pulsación;
cualquiera de los tres estados de wreck la bloquea. Tiempo suministrado **< 0,25**,
latch anterior 0 y modo previo 0 convierten petición 1 en 2, sin gate de salida, carga o fin.
Se corrigió el orden: la generación toma el modo **antes** de consumir turbo; el latch 0/1/2
se escribe **después** de despacho y selección de marcha. La IA conserva su petición directa.
Los fixtures cubren combinaciones de wreck, vecinos binary64 del umbral, tecla mantenida,
liberación y nueva pulsación. `tests/gearbox.c` prueba también agotamiento en el mismo tick.
La prueba compone generación y commit originales separados; no ejecuta los callees intermedios.
El port suministra ahora el reloj por coche descrito a continuación.
Véanse [contrato y límites](docs/formats.md#generación-humana-de-turbo-y-latch-verificación-por-fragmentos).

Reloj por coche y salida normal: **18 450 casos / 74 178 pasos** exactos en
`tests/race_time_original.py`. Se verifican conversión signed32 con wrap a segundos,
countdown, función completa de reset y actualización de un coche, compuestos con turbo/latch.
Se conserva el sample previo durante la transición y se congelan coches fuera/finalizados,
no wrecks. El reset conserva tiempo de vuelta y origen de vuelta.
Turbo humano, HUD y tiempo final del fantasma consumen esos relojes.
**Desviación**: milisegundos virtuales `floor(ticks*1000/72)` muestreados al final del tick;
no timer DOS, pausa ni salida forzada anticipada. Véase
[alcance y secuencia](docs/formats.md#reloj-por-coche-y-transición-normal-de-salida-verificación-ejecutable).

Inicialización de turbo y reset de controles al reaparecer: **7 422 casos / 31 344 pasos**
exactos en `tests/respawn_original.py`, comparando 21 campos con bytes LE sin parchear.
La colocación selecciona primera **y borra el contador de cambio**, conservando el retardo;
no borra modo/espera/latch/contador de efectos de turbo ni relojes. Los tres efectos de wreck
restauran carga 100 y modo 0 **en cada actualización**, no solo al comenzar.
Se prueban tecla mantenida/liberación/nueva pulsación y hasta 33 actualizaciones de caja.
`tests/gearbox.c` comprueba además colocación real sobre suelo sintético, fallo sin mutación
y preservación del resto del estado. La prueba original excluye geometría y FX completos:
véase [contrato del reset](docs/formats.md#inicialización-y-reaparición-reset-de-controles-verificado).

Progreso de vueltas y registro de tiempos: **3 024 casos / 4 112 pasos** exactos,
18 campos, en `tests/lap_original.py`. La vuelta exige fase 3, sonda actual cero,
sonda anterior en el último tramo y gate `+0x604 == 0`: no basta pisar el último tramo.
Se conserva el sentinel 10000 y se limita la tercera transición a `< 4*floor(count/4)`.
Se registran última vuelta y vueltas 1–3, origen desde el sample global y mejor vuelta
compartida con comparación estricta; `+0x3a0` guarda **posición**, no tiempo.
**Desviación**: mejor vuelta de sesión inicializada con `DBL_MAX`, sin récord guardado.
La geometría de las dos sondas, productor del gate, avisos y persistencia quedan fuera:
véase [contrato de vueltas](docs/formats.md#vueltas-y-registro-de-tiempos-verificación-por-fragmentos).

**No equivale a verificar todo `car_update` ni una carrera DOS**: faltan sondeo de controles, caché/latches,
frecuencia completa de callers e inicialización completa de controles, geometría,
producción original de km/h en binary64 (se suministra la misma velocidad binary32 a ambos kernels),
generación de solicitudes de IA/joystick, timer/pausa/salida forzada y efectos de turbo,
colisiones, daños y efectos de aterrizaje. La desviación de subidas con cambio de nivel permanece
explícita y fuera de la comparación original. Véanse [las fórmulas y límites](docs/formats.md#verificación-ejecutable-de-dirección-agarre-y-altura).
El replay entre dos builds Release independientes también pasa los 21 escenarios / 56 448 ticks
con 112 campos (v5, añade vueltas, tiempos registrados y mejor vuelta compartida), estado final y framebuffer idénticos;
demuestra reproducibilidad del port, no fidelidad DOS.
El bloque de vueltas mantiene ese resultado en `build/lap-crossbuild-replay.json`;
los doce objetivos no históricos de CTest pasan.
Tras las correcciones, la regresión histórica difiere en **10 estados y 22 imágenes**, sin fallos de ejecución.
`build/lap-regression-report.json` cambia estado e imagen de `CANADA 3 rivals 40s` respecto al bloque de reset;
los otros 22 escenarios conservan sus resultados. Los totales de divergencia histórica no cambian;
no se ha actualizado la referencia histórica.
También se ejecutaron 200 s con siete rivales en cada pista: los siete procesos terminaron correctamente
y sus resúmenes fueron finitos, pero persisten atascos de conducción; esto no demuestra carreras completas.

### Diagnóstico de regresiones y conducción reproducible

Se reconstruyó aisladamente la revisión `76bfca1`, que pasa **23/23** contra la instantánea histórica de
`tests/expected.json`. La revisión `3213fd5e` y su compilación limpia Release con AppleClang 21 dan
**1/23** con imágenes y **16/23** sin imágenes: **7 casos difieren en estado de carrera, 22 en imagen y
ninguno falla al ejecutar**. Por tanto, no era un problema de objetos de compilación antiguos.
Los casos con diferencias de estado son USA/CANADA/AUSTRIA con rivales y CARIB/JAPAN tanto solo como con rivales.

Las siete trazas de 40 s con piloto automático y sin rivales ya difieren en altura/suelo tras el primer tick
entre esas revisiones. Por ejemplo, BRAZIL pasa de `h=515,g=510` a `h=-505,g=-510`, e ICELAND de
`h=706.2,g=701.2` a `h=-699.8,g=-704.8` (formato antiguo redondeado). Entre ambas revisiones cambiaron el
signo Y de PLC, las consultas de suelo, el agarre, la IA y la presentación. El signo PLC tiene evidencia
estática documentada en [formatos](docs/formats.md); no se ha atribuido cada diferencia a una única causa
ni demostrado que la revisión histórica sea más fiel al original. **No se ha regenerado el baseline para
ocultar los fallos.**

El nuevo replay sin rivales pasa **21/21** escenarios: siete pistas con auto (2 880 ticks), recta
(576 ticks) y turbo (576 ticks), coche 9 y dificultad 0. Compara dos procesos nuevos sin tolerancia en los
112 campos muestreados (esquema v5, incluye relojes y registro de vueltas), estado final e imagen:
**56 448 ticks ejecutados**, más 42 estados iniciales.
La comparación contra una segunda compilación Release hecha desde cero también pasa los **21/21** escenarios.
El informe conserva argumentos, plataforma, hashes de ejecutables/fuentes/datos y capturas completas; rechaza
esquemas incompletos, tiempos desalineados, valores no finitos, fallos de proceso y cambios de datos durante la prueba.

```sh
python3 tests/regress.py --binary build/ignition --report build/regression-report.json
python3 tests/regress.py --binary build/ignition --no-images --report build/regression-state-report.json
python3 tests/solo_replay.py --binary build/ignition --report build/solo-replay-report.json
ctest --test-dir build -R '^(verification_tools|headless_trace_contract|solo_replay|gearbox|pos)$' --output-on-failure
IGN_TRACE=full ./build/ignition USA --sim-ticks 576 turbo --rivals 0 --car 9 --shot /tmp/x.ppm
```

`--reference-binary` permite comparar otra compilación del port que admita el mismo esquema.
`--sim` conserva su acumulación histórica de tiempo en `float`: por ejemplo, 8 s ejecutan 577 ticks.
El replay usa `--sim-ticks` para no confundir ese redondeo con una divergencia física.
Los tests sintéticos `verification_tools` comprueban también controles negativos del comparador y del informe.
`headless_trace_contract` verifica los conteos exactos, rechaza argumentos inválidos y comprueba que activar
las trazas no modifica el estado final ni la imagen. Con los oracles originales habilitados,
pasan los 10 objetivos no históricos; solo falla la regresión histórica.
Los informes y capturas se guardan en el directorio de compilación; no contienen una carrera DOS de referencia.

**Esto establece determinismo del port en esos escenarios, no equivalencia con el juego DOS ni ausencia de
atascos o errores de conducción.** La regresión histórica sigue fallando de forma visible.
Dirección, agarre y aritmética vertical ya tienen un límite verificable aislado. El siguiente paso es
ampliar a controles completos y colisiones, antes de comparar carreras reales en DOSBox-X.

Ejemplos:

```sh
./build/ignition JAPAN --rivals 7
./build/ignition BRAZIL --sim 200 auto --rivals 7 --shot /tmp/x.ppm
./build/ignition --shot coche.ppm --model 9 0.6 30 0.15 0 USA
python3 build/ppm2png.py coche.ppm && open coche.png
./build/ignition CANADA --sim 200 auto --record-ghost /tmp/MOOSEJAW.GST --shot /tmp/x.ppm
python3 tools/gst.py info /tmp/MOOSEJAW.GST --sections 175
python3 tools/gst.py plot /tmp/MOOSEJAW.GST -o /tmp/ghost.png
```

---

## Estructura del proyecto

```
CMakeLists.txt          objetivos: ignition_core (biblioteca), ignition (ejecutable), elf (volcado ELF)
src/game/assets.{c,h}   carga de COL, PIC, PLC, MSH, TEX, SRF, TRI; consulta de suelo; muros; mapeo motor↔PLC
src/game/physics.{c,h}  cuerpo rígido (modelo bicicleta), par motor, muros, choques entre coches, tablas por coche
src/game/car.{c,h}      coche: suelo bajo 4 ruedas, vertical, dirección, acelerador, turbo, pose visual, vueltas
src/game/track.{c,h}    clases PLC, orden de tramos y bifurcaciones, puertas por tramo (TRI)
src/game/ai.{c,h}       IA de rivales (port parcial de 0x1aea0)
src/game/ghost.{c,h}    fantasmas GST: leer, grabar a 72 Hz y guardar con el formato exacto del original
src/game/skid.{c,h}     efectos de derrape: marcas en la pista y polvo (`sub_0003731c`)
src/game/fx.{c,h}       huecos de efectos del original: sprites, piezas, salpicaduras, explosión, turbo, reaparición
src/render/raster.{c,h} rasterizador por software de 8 bits (320×200 a 800×600, 4:3), z-buffer, texturas afines
src/render/hud.{c,h}    superposición de carrera con H_PAN1.PIC y fuentes LFT originales (port parcial)
src/render/camera.{c,h} cámaras del original: persecución, trasera y fijas tras la meta (`sub_00047f94`, `0xc464c`)
src/platform/main.c     SDL3: ventana, entrada, bucle fijo de 72 Hz, cámaras, modos sin ventana y visor
docs/formats.md         referencia técnica detallada (formatos, fórmulas, direcciones)
tools/le2elf.py         LE (DOS/4GW) → ELF32 plano con reubicaciones aplicadas
tools/disasm.py         desensamblador recursivo (Capstone): símbolos, referencias, tablas de salto, barrido de huecos, clasificación de libc y exportación a Ghidra/IDA
tools/gap_seeds.txt     inicios de función hallados por `disasm.py sweep` (se cargan como semillas)
tools/structs.txt       estructuras verificadas (coche, cuerpo, tramo, estado de IA) para la exportación
tools/trace_compare.py  primera divergencia entre dos trazas por tick (port vs original en DOSBox-X)
tests/regress.py        regresión determinista sin ventana (23 casos; `ctest --test-dir build`)
tests/gearbox.c         prueba unitaria de cambios manuales y marcha atrás (`ctest --test-dir build -R gearbox`)
tests/body_step.c       ejecuta la física C para el verificador diferencial
tests/physics_original.py replay aislado del integrador LE contra el port, con Unicorn opcional
tests/car_original.py     replay LE de dirección/agarre/altura/aceleración/freno/turbo contra kernels C
tests/lap_original.py     cruce de vuelta, fases, tiempos y mejor vuelta contra fragmentos LE
tests/car_kernels.c       runner nativo de los seis kernels, sin duplicar sus fórmulas
tests/solo_replay.py    replay sin rivales, exacto por tick, con informe de procedencia (solo port)
tests/test_verification.py controles positivos y negativos de las herramientas de verificación
tools/ignassets.py      PIC/COL/TEX/DAT/nivel → PNG
tools/gst.py            fantasmas GST: cabecera, validación, tiempos por vuelta y trayectoria sobre la pista
tools/symbols.txt       símbolos identificados (funciones, globales, tablas)
.github/skills/ignition-re/SKILL.md   skill del proyecto para agentes de IA
```

---

## Herramientas de ingeniería inversa

```sh
python3 -m venv .venv && .venv/bin/pip install capstone
.venv/bin/python tools/disasm.py funcs            # lista de funciones
.venv/bin/python tools/disasm.py coverage         # estados por función (incl. `library`) y cobertura del código
.venv/bin/python tools/disasm.py sweep            # barrido de huecos de código → tools/gap_seeds.txt (≈ 2 min)
.venv/bin/python tools/disasm.py export ghidra -o build/ghidra_symbols.py   # o `ida`: nombres, comentarios y estructuras
python3 tests/regress.py                          # regresión determinista (`--update` regenera tests/expected.json)
ctest --test-dir build --output-on-failure        # regresiones de carrera + caja de cambios
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
| POS | animaciones de objetos (fotogramas pos+rot) | 🟡 parser, avance a 36 Hz y render aplicados; rotación aproximada y colisiones incompletas |
| TAB | tabla de sombreado `color×nivel` (64 niveles) | 🟡 significado conocido; no se usa |
| PAN / SHD | tablas 256×256 de 64 KB | � SHD fila 0x28 = sombra; PAN desconocida |
| CARS | 11 coches × (carrocería + 4 ruedas) | ✅ portado y montado como en `cars_init` |
| LFT / FNT | fuentes | ✅ LFT (`gfx2d.c`: alineación, proporcional, espaciado); FNT solo cabecera |
| CDP | vídeos de introducción | ✅ portado (`cdp.c`) |
| PSQ / PFM | animación vectorial del fondo del menú ("Script Player") | ✅ portado al píxel (`pfm.c`) |
| DAT (`MENUBKG.DAT`) | atlas de las cajas de resaltado del menú | ✅ portado (`gfx2d.c`) |
| GST | coches fantasma | 🟡 leer, escribir y reproducir (`ghost.c`, `tools/gst.py`); falta la contrarreloj |
| BTZ | configuración y récords | ✅ bloque de 0x597 bytes (`settings.c`) |
| INF | curvas del sonido del motor por coche | � 4 tablas de 200 B: tono y volumen de `00_A`/`01_A` por régimen (`sound.c`) |
| WAV / PAT / XI / OGG | sonido (PCM, Gravis, FastTracker, pistas de CD) | 🟢 WAV, PAT y XI (`audio.c`); falta OGG (música) |

### Sistemas del juego descifrados y portados

| Sistema | Original | Port | Fidelidad |
|---|---|---|---|
| Bucle fijo de física a 72 Hz | `frame_time` `0x2b734` | `main.c` | fiel |
| Cuerpo rígido (modelo bicicleta) | `body_step` `0x1e090` | `physics.c` | verificación diferencial de la rutina aislada; 190 casos |
| Curvas de par (Hermite) | `torque_spline` `0x53e70` | `physics.c` | fiel |
| Acelerador, freno y marcha atrás | `car_throttle` `0x4db90`, `car_brake` `0x4e1bc`, bloques de `car_input` `0x4d5f4` | `car.c` | aceleración/freno: 6 829/72 casos; acelerador→revoluciones: 6 941; pedales/arbitraje: 27 828 |
| Selección de marchas | `car_gearbox` `0x502c4`, cambios de `human_input` | `car.c` | caja automática sin audio: 5 526 casos / 72 738 pasos; manual: 6; orden del tick probado en el port, no caller completo original |
| Revoluciones | bloque de `car_throttle` `0x4dfbe..0x4e1a7` | `car.c` | verificado: 25 016 casos / 134 208 pasos; reutiliza índice final de par y distingue humano/IA |
| Coches de la IA: modelos, bono de par, perfil 11 | `sub_00022d08`, `car_steer`, `car_turbo`, `car_throttle` | `car.c`, `main.c` | fiel |
| Dirección | `car_steer` `0x500f0` | `car.c` | verificada: 732 casos / 5 856 pasos contra bytes LE |
| Agarre por superficie y coche | bloque `0x3070a`, tablas `0xc33f0`, `0xc4dc0`, `0xc5670` | `car.c` | verificado: 672 casos / 1 536 pasos; media persistente y reglas de rivales |
| Consulta de suelo | `ground_query` `0x20814` | `assets.c` | fiel (altura acotada a los vértices de la cara) |
| Suelo bajo 4 ruedas, ruedas anómalas | `car_update` `0x2eea4`–`0x30d98` | `car.c` | fiel: esquinas de la caja ×1,1 escaladas por `1 + km/h / 220`, cabeceo `asin` limitado a 0,1 rad/tick |
| Vertical: gravedad, saltos, aterrizaje | `car_vertical` `0x2e234` | `car.c` | aritmética verificada hasta efectos: 252 casos; desviación explícita de subidas |
| Muros (aristas no compartidas, activación por superficie, modo salto, caché de 150) | `collect_walls` `0x33ee8`, `car_update` `0x2f856`, `cars_update_all` `0x2d77a` | `assets.c`, `car.c` | fiel |
| Choque con muros | `box_vs_walls` `0x1f708`, `wall_impulse` `0x1f3fc` | `physics.c` | fiel |
| Choque entre coches | pase en `0x2d2f8`, `0x1ebb4`, `0x1e638` | `physics.c`, `car.c` | fiel (falta el caso de solape) |
| Tramos, bifurcaciones y saltos | `split_plc_types` `0x247b0`, `build_sections` `0x28f08` | `track.c` | búsqueda por origen PLC, contador de bifurcación, ruta B paralela y merge portados; prueba estructural sobre assets de las 7 pistas (`track_real_assets`), sin cotejo runtime completo con el original |
| Vueltas, fases y tiempos | `car_update` `0x2f2be`, `0x2fc1c` | `race.c`, `car.c` | 3 024 casos / 4 112 pasos exactos por fragmentos; geometría, récord guardado y avisos excluidos |
| Parrilla de salida | `cars_init` `0x26458` | `main.c` | fiel |
| Turbo | `car_turbo` `0x4e2d8`, `car_input` `0x4d8eb`, `human_input` `0x2c99e` | `car.c` | aritmética: 2 016 casos / 96 768 pasos; arbitraje por fragmentos: 10 728 / 82 440 exactos; generación humana/latch: 5 184 / 31 104 exactos; sondeo, timer DOS, IA/joystick, sonido/efectos y coherencia del rumbo del caller no verificados |
| Relojes por coche/salida normal | `frame_time` `0x2b734`, `race_clock` `0x2b850`, reset `0x2a410`, update `0x4c738` | `race.c`, `car.c` | 18 450 casos / 74 178 pasos exactos, composicionales; timer virtual, pausa/salida forzada y fase completa de callers excluidos |
| Destrucción y reaparición | `sub_0003a788`, `sub_00041430` | `race.c`, `car.c` | reset de controles/turbo: 7 422 casos / 31 344 pasos exactos; geometría/FX completos sin prueba, desviación en huecos de suelo |
| Humo de la pista y polvo al aterrizar | `sub_0003636c`, `sub_00036ce0`, `sub_0003f37c` | `fx.c` | fiel (opciones de detalle activadas) |
| Montaje del coche | `cars_init` `0x265f6` | `car.c` | fiel |
| Pose: dirección visual, inclinación | `0x48a14`, `0x40ec8`, `0x2e6f8` | `car.c`, `raster.c` | fiel |
| Sombra del coche | `0x48a14` | `raster.c` | geometría fiel; sombreado aproximado |
| Trucos (BANARNE, SURMULE, SLASKTRATT…) | `menu_page_key` `0x160c4` | `frontend.c`, `main.c` | fiel: se teclean en el menú; BANARNE también en carrera |
| IA de rivales | `ai_drive` `0x1aea0` | `ai.c` | ⚠️ **port parcial**: tabla `t_ai_lanes`/RNG, registros `load_tri` con mapeo engine→PLC, selector `ai_find_section` y reglas observadas de topología/ramas implementados; la estructura de rutas y registros TRI pasa `track_real_assets` en las 7 pistas, pero falta una comparación independiente con la tabla runtime original y la asignación exacta de registros divididos a carriles; pendiente (Y), giro por solape y frenada de curva (heurística) |
| Render 3D | motor Lisa 2 | `raster.c` | propio: z-buffer, sin sombreado TAB |
| Frontend: carga, bucle por fotograma, cierre | `menu_load_gfx` `0x11504`, `menu_frame` `0x12bb8`, `menu_shutdown` `0x13a5c` | `frontend/frontend.c` | literal: intro, fundidos, créditos, iris, volúmenes; sin CD ni joystick |
| Motor de menús "Baltazar" | `0x51b20`, `0x51c94`, `0x527c8`, `0x534cc`, `0x5383c` | `frontend/gui.c` | literal; tablas extraídas del EXE (`tools/gen_menu_data.py`) |
| Callbacks de página y acciones | `0x160c4`, `0x170ac`, `0x155d0`, `0x16f18`–`0x17054` | `frontend/frontend.c` | literal; prueba `menu_flow` |
| Fondo animado PSQ/PFM | `0x549b0`, `0x54df8`, `0x60e3d` (tipos 7, 11, 15, 17, 18, 19) | `frontend/pfm.c` | **al píxel**: 209 fotogramas iguales al original emulado; prueba `menu_background` |
| Fuentes, sprites, cajas, CDP | `0x61959`, `0x55968`, `0x541d4`–`0x54864`, `0x55384` | `frontend/gfx2d.c`, `cdp.c` | literal; rotozoom aproximado |
| Coches 3D del menú | `0x19508`, `0x1aaf8` | `frontend/frontend.c` | modelos y giro fieles; proyección con el rasterizador del port (cámara aproximada) |
| Configuración y récords | `0x1a864`, `0x198bc`, `IGN_DOS.BTZ` | `frontend/settings.c` | literal |

---

## Cobertura del desensamblado

Estado de las funciones del juego en el último informe generado (el detalle por dirección, nombre, estado y número de
instrucciones está en `docs/function-coverage.txt`):

| Estado | Funciones | Interpretación |
|---|---:|---|
| `unknown` | 412 | Sin identificación fiable; requiere análisis |
| `identified` | 71 | Nombre o propósito provisional conocido; falta establecer su comportamiento completo |
| `understood` | 0 | Comportamiento entendido, todavía no implementado |
| `port-pending` | 2 | Pendiente explícita y priorizada: `car_ground_contact`, `collisions_pass` |
| `partial` | 14 | Implementación incompleta o solo se verificó un fragmento; no cuenta como portada |
| `ported` | 115 | Implementada en el port, sin afirmar verificación completa |
| `verified` | 5 | Verificada para el alcance anotado, no implica equivalencia de toda la carrera |
| `native` | 5 | Sustitución nativa explícita: `main`, `main_step` y el temporizador PIT/BIOS (`0x10010`–`0x1034c`) → SDL |
| `library` | 488 | Runtime/libc/DOS/4G detectados heurísticamente, fuera de la conciliación del código de juego |

El inventario de 1 112 filas se puede regenerar con
`.venv/bin/python tools/disasm.py roadmap -o docs/function-roadmap.tsv`; la cobertura con
`.venv/bin/python tools/disasm.py coverage > docs/function-coverage.txt`. En el TSV, `unmapped` significa que aún no
se ha asociado la función a una tarea concreta, no que esté fuera de alcance. La conciliación cubre **202 de 624
funciones de juego (32,4 %)**; el informe conserva también el recuento bruto de 202/1 112 para que no se confunda con
una proporción del código de juego. Las tareas `roadmap=` usadas son identificadores estables de subsistema; las
sustituciones de la biblioteca estándar, DOS/4G y el motor gráfico se deben decidir por separado, no contabilizar
automáticamente como porte literal.

La libc y DOS/4G se agrupan en `library` mediante la clasificación heurística del desensamblador. El motor Lisa 2
no se mezcla con esa categoría: el port usa SDL3 y un rasterizador nativo propio, así que no necesita replicar
literalmente cada rutina interna del motor original. `native` se reserva para sustituciones función-a-función
explícitas (hoy solo el bucle principal y el temporizador), para no confundir una sustitución de subsistema con equivalencia de cada rutina.
Las tareas de render sí siguen registradas por separado cuando afectan al comportamiento visible del juego.

Las etiquetas de roadmap se agrupan así: `phase1.*` carrera, controles, física y rutas; `phase2.*` mundo, efectos y
colisiones móviles; `phase3.*` actualización del coche y render; `phase4.*` HUD, bucle, audio y frontend.

No se publica aquí un desglose regional heredado: sus cifras no se regeneran desde el informe actual y podrían inducir
a error. El total de instrucciones y cobertura de bytes sí se toma directamente de `disasm.py coverage`.

Quedan 7 402 bytes (1,5 %) de código sin cubrir: tablas, relleno y datos intercalados. `tools/gap_seeds.txt` guarda
303 inicios añadidos por el barrido; se aceptaron 277 candidatos directos a partir de huecos y 26 funciones llamadas.
Cada candidato debe decodificar limpiamente, terminar en `ret`/`jmp`, no solaparse con instrucciones aceptadas, pasar
las comprobaciones de instrucciones plausibles y ubicar toda reubicación LE dentro de un operando. La clasificación
`library` no usa firmas FLIRT: parte de funciones conocidas de libc (`sprintf`, `atan2`, `memset`, `open`…) y añade
sus llamadas y funciones que solo tocan datos de runtime; es heurística, no una identificación exhaustiva.

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
`car_ground_contact` `0x350f0` (colisión con objetos) · derrape (marcas y polvo) `0x3731c` · explosión y reaparición `0x3a788` · sprites translúcidos `0x39d1c`,
`0x39fe8`, `0x3e6ec` · *plots* `0x3a2b4`, `0x3a498` · piezas volando `0x3b744` · relámpagos `0x3bdac` · salpicaduras
`0x3d808` · clima `0x403c8` · animación POS `0x412e8` · crear efecto *¿?* `0x3fee8` · tipos de objeto `0x360f0`,
`0x3636c`, `0x36ce0`.

**Render y presentación**: `render_scene` `0x56410` · `race_setup_video` `0x492fc` · `camera_angles` `0x5891c` ·
`render_main` `0x43fe0` (resultados y puntuaciones) · `car_place_objects` `0x48a14` · HUD `0x4a320` · tiempos
`0x4bdf4` · dígitos `0x4be7c` · depuración XYZ *¿?* `0x49fdc` · pausa `0x4395c` · salir o reiniciar `0x43e48` ·
captura TGA `0x515f8` · fondo del menú `0x541d4`.

**Frontend** (portado en `src/frontend/`): `main` `0x10010` · `main_step` `0x10060` · `menu_load_gfx` `0x11504`
(con `menu_init` `0x14000` en línea) · `menu_frame` `0x12bb8` · `menu_shutdown` `0x13a5c` · iris `0x15344` · capa de
texto `0x155d0` · callback de teclas de página y trucos `0x160c4` · acciones SALIR/CARRERA/VÍDEO/RESTAURAR/BORRAR
FANTASMAS `0x16f18`–`0x17054` · callback de dibujo de página `0x170ac` · récords `0x1911c` · coche 3D `0x19508` ·
aplicar menús a la carrera `0x198bc` · valores por defecto `0x1a864` · `menu_load_car` `0x1aaf8` · tabla del iris
`0x2ac28`. Motor de menús "Baltazar" `0x51b20`–`0x539f8`; cajas MenuBkg `0x541d4`–`0x54864`; *Script Player* PSQ/PFM
`0x549b0`–`0x54df8`; CDP `0x552fc`/`0x55384`. Animación de texturas de objetos PLC `0xfa` `0x10390`–`0x10cf8`
(carrera, sin portar) y utilidades de paleta `0x10d34`–`0x114f4` (casi todas muertas).

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
- `BALTAZAR/DATA`: ya usados por el frontend salvo `DEMOCAR.PIC`, `DEMOTRK.PIC`, `MARK.PIC` y `CONSPR.PIC` (no los
  carga `MAINDOS.EXE`).
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
| `+0x364`, `+0x370`, `+0x374`, `+0x378` | tramo de navegación, última sonda de vuelta no sentinel, vuelta y fase |
| `+0x534/+0x53c/+0x548` | carga, modo y espera del turbo |
| `+0x52c/+0x530` | petición de turbo actual y latch anterior (0/1/2), generación humana verificada con tiempo suministrado `+0x3a4` |
| `+0x3a4/+0x3ac/+0x3d4` | tiempo total, tiempo de vuelta y origen de vuelta (segundos); conversión/update/reset normal verificados, timer DOS excluido |
| `+0x3a0/+0x3b4/+0x3bc/+0x3c4/+0x3cc` | posición al terminar, última vuelta y tiempos de vueltas 1–3; commit verificado por fragmentos |
| `+0x544` | canal de sonido |
| `+0x554` | contador de vuelco *¿?* |
| `+0x558/+0x55c/+0x560` | solape con otro coche |
| `+0x57c` | muros activos este tick (ruedas sobre 0x50, 0x5a, 0x5b, 0x51/0x5c/0x5d desde abajo, 0x6e–0x8d sin salto) |
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

1. **IA**: port parcial de `0x1aea0` (`ai.c`): puertas reconstruidas al vuelo (con mapeo engine→PLC; sin asumir índices idénticos), sin tablas de carril `0xbf9a0`, acelerador (`0x1b9a2`) aproximado y frenada por curvatura propia.
2. **Render**: z-buffer propio en lugar del orden del pintor del motor Lisa 2 (el original no usa profundidad por
   píxel). Texturas afines como el original (`--persp` activa la corrección de perspectiva, la opción PERSP. POLY).
   Sin TAB: el motor la pasa al rasterizador (`[0xc484c]`) pero las caras texturizadas se dibujan sin sombrear (en
   `Ign_win.exe` la primitiva 0x11 no tiene campo de luz); solo la usan primitivas planas y de Gouraud que el port aún
   no dibuja. El modo de cara decide la transparencia: 17 y 21 opacos, el resto con el índice 0 transparente. La sombra
   usa `SHD[0x28*256 + destino]`, como la primitiva 0x13 de `Ign_win.exe`.
3. **Suelo**: se ignoran las caras degeneradas, y la altura de una cara se acota a la de sus vértices (los bordes de los
   *spans* del SRF caen a veces fuera de las caras casi verticales).
4. **Vertical**: cuando las cuatro esquinas cambian de nivel a la vez (un talud de tipo ≥ 0x50 o un hueco de la
   consulta de suelo del port, CARIB tramos 1–4), una subida mayor que `|km/h · 0,05| + 1` no se sigue con su
   velocidad: el original lanzaría el coche cientos de unidades hacia arriba.
5. **Cámara**: la de `sub_00047f94` se actualiza a 36 Hz para todos los coches. Mientras sigue al coche, la vista se
   recalcula con la posición actual del coche (el port dibuja más deprisa que las actualizaciones de la cámara). La
   media de ángulos de tramo con `car+0x36c == 3` no está portada. La cuenta atrás solo sale en el título de la
   ventana (faltan los sprites y sonidos), y sin ventana empieza la carrera directamente salvo con `--countdown`.
6. **Posiciones**: la clasificación (`ign_race_position`) es propia; falta la rutina original del HUD `0x4a320`.
7. **Dificultad**: `[0x1fedd8]` se fija con `--difficulty` (agarre trasero, índice de velocidad, modelos y bono de par
   de la IA).
8. **Efectos**: los sprites se ordenan de lejos a cerca como en el original, pero además se prueban contra el z-buffer
   del port (con la profundidad `z·2,8` que usa el original para ordenarlos frente a los triángulos). Si el punto de
   reaparición no tiene suelo en la consulta del port, se prueban los tramos siguientes (CARIB, tramos 1–4). Los
   emisores de humo se portan con las dos opciones de detalle (`[0x1fed70]`, `[0x1fed64]`) activadas.
   Las líneas de lluvia (cara `0x0d`) se dibujan como degradado de las filas SHD `0x77`→`0x7b`: no está claro si el
   original interpola el color. La destrucción por objeto `0x130` (alud de AUSTRIA, `0x3db4c`) está portada; las rocas
   avanzan con POS, pero su rotación usa un orden de Euler aproximado.
9. **POS**: el avance se ejecuta a 36 Hz para todas las entradas animadas (sin equivalente portado del flag activo del
   motor original). La rotación `Z(Y(X(v)))` y la geometría transformada son aproximaciones; la rejilla SRF conserva
   sus spans estáticos, por lo que la consulta de suelo y las colisiones de objetos trasladados no son completas.
10. **Sonido**: mezclador propio en SDL3 (mono, 44,1 kHz) en lugar del driver GUS/Sound Blaster; volumen 0..`0x10000`
   y frecuencia en Hz como en `0x625f4`. Sin panorama estéreo, sin música y sin los sonidos de objetos móviles.
11. **Frontend**: el menú corre a 320×200 como el DOS. No hay CD (el original tocaba pistas de audio con MSCDEX; la
   versión GOG trae `MUSIC/Track02–08.ogg`, que el port aún no decodifica), ni joystick/volante (sus páginas de
   calibración no terminan solas), ni la espera "NO SOUNDCARD FOUND". Los coches 3D del carrusel usan el rasterizador
   del port con una cámara que reproduce posición y objetivo pero no la focal exacta del motor Lisa 2. El escalado de
   sprites (rótulo del coche, miniatura de pista) usa muestreo propio. El reloj del menú es `SDL_GetTicks` (el original
   iba un 1,15 % rápido). La carrera aún no usa el modo de juego, los nombres, las teclas configuradas ni las opciones
   gráficas del menú.

---

## Roadmap

Prioridad: **P1** = necesario para un juego completo de un jugador · **P2** = fidelidad y contenido · **P3** = extras.
Cada tarea indica las direcciones de partida.

### Siguientes portes recomendados

Orden sugerido según impacto, tamaño y lo que ya está identificado. Los recuentos son instrucciones desensambladas
del original, no líneas de C; `identified` significa que falta portar, y no implica que el comportamiento ya esté
implementado.

1. **Contacto y colisiones con objetos móviles (P1, impacto alto):** `car_ground_contact` (`0x350f0`, 841) y
   `collisions_pass` (`0x3fd04`, 136). Conectar el contacto del coche con el pase de colisiones y los tipos de objeto;
   las animaciones POS ya están conectadas, pero la geometría móvil aún no se refleja en toda la consulta de suelo.
   Véase Fase 2.
2. **Entrada y controles fieles (P1, alcance medio):** completar `human_input` (`0x2c5b8`, 424),
   `car_input` (`0x4d5f4`, 335), `car_steer` (`0x500f0`, 121), `car_throttle` (`0x4db90`, 389) y
   `car_turbo` (`0x4e2d8`, 215). Esto permite reemplazar los controles aproximados, conservar las reglas originales
   de salida y añadir el joystick; véase Fase 1.
3. **Terminar la IA original (P1, grande):** completar `ai_drive` (`0x1aea0`, 1 869) y `ai_find_section`
   (`0x1decc`, 94): selección de carril y puertas reales, velocidad según curvatura/pendiente, adelantamientos y
   frenada. Ya hay un núcleo portado; véase Fase 1.
4. **Fidelidad del paso de coche (P1, muy grande):** portar por partes `car_update` (`0x2ed00`, 2 335) y
   `cars_update_all` (`0x2d008`, 846). El planificador rotatorio de IA ya está conectado; siguen pendientes la
   actualización completa de superficies y el resto del dispatcher. Requiere
   mantener las regresiones de las siete pistas; no conviene abordarlo como un único porte monolítico. Véase Fase 3.

### Fase 0 — Infraestructura de ingeniería inversa

- [x] **P1** Inicializar git y subir solo código, documentación y herramientas (los datos ya están excluidos).
- [~] **P1** Separar la libc de Watcom y DOS/4G del código del juego: no hay base de firmas FLIRT para Watcom 10.x, así
      que `disasm.py` la clasifica heurísticamente por grafo de llamadas y datos de runtime (estado `library`,
      488 funciones, 20 323 instrucciones). Quedan sin clasificar ≈ 25 000 instrucciones de `0x60000–0x86000`;
      la separación completa requiere mejores firmas o verificación manual.
- [x] **P1** Cubrir el 17 % de bytes restante: `disasm.py sweep` (barrido de huecos con prólogos Watcom, decodificación
      limpia, validación de relocaciones y rechazo de solapes) subió la cobertura del 83,0 % al **98,5 %**
      (277 candidatos de huecos aceptados; 303 inicios validados persistidos, incluidos los llamados).
- [x] **P2** Exportar `symbols.txt` como script de Ghidra o IDA (nombres, comentarios y tipos de estructura):
      `disasm.py export ghidra|ida -o FICHERO` (estructuras en `tools/structs.txt`). No probado en Ghidra/IDA reales.
- [x] **P2** Generar estados por función en `symbols.txt`: `unknown`, `identified`, `understood`, `port-pending`,
      `partial`, `ported`, `verified`, `native`; `library` se clasifica automáticamente y se mantiene aparte.
      `disasm.py coverage` excluye parciales del numerador y `disasm.py roadmap` exporta las 1 112 funciones con
      estado y tarea asociada. Estados detallados y definiciones en "Cobertura del desensamblado".
- [x] **P2** Pruebas de regresión deterministas: `tests/regress.py` (23 casos: 7 pistas solo, con 3 rivales y con turbo,
      visor y cámara de parrilla; sección, fase, vuelta, NaN y hash del framebuffer; `ctest --test-dir build`).
      Los resultados esperados son una instantánea histórica, reproducida por `76bfca1`, no del estado actual
      (incluye el atasco de CANADA en la sección 22). `3213fd5e` pasa 1/23 con imagen y 16/23 sin ella;
      el informe separa ejecución, estado e imagen y no oculta esos fallos.
- [x] **P2** Conducción sin rivales reproducible: `tests/solo_replay.py`, 21 escenarios / 56 448 ticks
      entre dos ejecuciones, 112 campos por fila y controles negativos. Prueba del port, no fidelidad DOS.
- [x] **P2** Verificación de dirección, agarre y aritmética vertical contra bytes LE: `tests/car_original.py`,
      1 656 casos / 8 400 pasos. Corregidos agarre persistente y tablas de rivales; no verifica todo `car_update`.
- [x] **P2** Aislar aceleración, freno y turbo contra bytes LE: 8 917 casos / 127 230 pasos adicionales;
      total 10 573 / 135 630. Corregidos índice x87, patinaje, bloqueo del freno y agotamiento del turbo.
      Pendiente dispatcher completo; solicitudes de turbo se verifican en los bloques siguientes.
- [x] **P2** Revoluciones contra bytes LE, aisladas y con acelerador: 31 957 casos / 166 942 pasos adicionales;
      total 42 530 / 302 572. Reutilizado índice final de par; corregidas reglas de aire/salida/fin de la IA.
      Velocidad binary32 suministrada, no verificación de su producción original binary64.
- [x] **P2** Pedales/arbitraje y selección de marchas contra bytes LE: `tests/input_original.py`,
      33 360 casos / 100 572 pasos exactos. Marcha previa para la fuerza, manual 0–2, umbrales estrictos,
      curvas compartidas y retardo original. Alcance D/A/K/H; el arbitraje de turbo se verifica en el bloque siguiente.
- [x] **P2** Arbitraje de solicitudes de turbo por fragmentos LE: modo J, 10 728 casos / 82 440 pasos exactos;
      total entradas 44 088 / 183 012. Corregidos gates, negativa humana y contador solo en petición 1.
      Audio/efectos excluidos; composición condicionada a sus efectos secundarios, no dispatcher continuo.
- [x] **P2** Generación humana de solicitudes, pulsación inicial y latch por fragmentos LE: modo P,
      5 184 casos / 31 104 pasos exactos; total entradas 49 272 / 214 116.
      Snapshot antes de consumir carga y commit después de marchas; sin gate de salida inventado.
      Pendientes sondeo, timer DOS/pausa/salida forzada, IA/joystick y callees intermedios.
- [~] **P3** Trazas de ejecución del original en DOSBox-X: hecho `tools/trace_compare.py`; DOSBox-X 2026.10.01 está
      instalado; se confirmó la entrada MZ de `MAINDOS.EXE` y que DOS lo mantiene cargado, pero tras reanudar solo se
      observó el bucle de espera de BIOS. Aún falta llegar a carrera, validar el mapeo LE, el muestreo por tick y la
      comparación ([`docs/dosbox-trace.md`](docs/dosbox-trace.md)).

### Fase 1 — Carrera completa de un jugador (jugabilidad)

- [~] **P1** Portar 1:1 la **IA original** `0x1aea0` (hecho: núcleo de dirección/atasco/turbo/límites y álgebra exacta, elección de carril, registros `load_tri`, selector `0x1decc` y reglas observadas de `build_sections`; estructura validada sobre assets de las 7 pistas, aún sin comparación runtime independiente con el original; pendiente asignación exacta de registros divididos, pendiente (Y) y giro por solape) (1 869 instrucciones): elección de carril (`+0x64/+0x66`,
      byte `+0x6d` de TRI), consulta de puertas `0x1decc`, velocidad objetivo por curvatura y pendiente
      (`curveinflu`, `slopeinflu`), frenada (`carebed`), adelantamientos (`careother`, `aggr`), turbo (`0x1ca15`).
      Descifrar los `double`s de los bloques TRI y el estado de 0x7c B.
- [x] **P1** Arreglar el atasco en la bifurcación de CANADA (tramo −34): la IA usa las puertas de la rama B (`gate_b`) en tramos negativos.
- [x] **P1** Orden de carrera, posiciones, fin de carrera y número de vueltas por modo (`race.c`; 3 vueltas, modo 3 sin límite; clasificación propia: ver desviaciones).
      Cruce/fases/registro de tiempos verificados por fragmentos: 3 024 casos / 4 112 pasos.
      Pendientes geometría de sondas, productor del gate, récords guardados y avisos.
- [x] **P1** Evento de eliminación `0x2de30` (modo 3): el último coche de cada vuelta queda eliminado. Portada la lógica
      (`--elimination`); faltan explosión (`0x3fee8`), sonido y mensajes "%s IS OUT" / "WARNING, YOU'RE LAST!" (`0x4a320`).
- [ ] **P1** (decodificado: `0x31858` es solo cambio de malla + sonido y `0x2ea44` un efecto; falta portarlos con los efectos de Fase 2) Solape entre coches `0x31858` (|dx|,|dz| < 25: cambio de malla y reaparición) y efectos de golpe
      `0x2ea44` (impacto > 15).
- [x] **P1** Destrucción y reaparición (`car_vertical` `0x2e397`, `car_update` `0x2fd63`, `sub_0003a788`): al aterrizar
      en superficies prohibidas, sin suelo bajo las 4 ruedas o 6 ticks con las 4 sobre tipos > 0x28, el coche explota
      (o se hunde) y 80 fotogramas después reaparece dos tramos más adelante, con explosión, piezas y humo
      (`0x3a788`), salpicadura (`0x3c208`) y destellos al volver (`0x3e6ec`). La colocación de `0x41430` deja el
      coche quieto, en el aire 250 unidades sobre la media de las caras de carretera, en primera, y le asigna el tramo
      del punto de reaparición (si vuelve a explotar, reaparece dos tramos más allá). Falta el disparador por objetos
      `0x12c`.
- [~] **P1** Entrada original: teclado `0x2c5b8` decodificado (dirección digital, sin rampas; ver `docs/formats.md`);
      pedales/arbitraje y selección de marchas verificados, manual 0–2 (M, Q/E); faltan sondeo/flancos/caché,
      joystick `0x4c9b4` y `car_input` completo
      (`0x4f874`, `0x50464`, `0x50824` sin identificar; freno `0x4e1bc` identificado y verificado para coche no terminado).
- [x] **P2** Caja automática `0x502c4` sin audio verificada (curvas compartidas, ángulo, empate y contador/retardo);
      cambios manuales 0–2 y reset al reaparecer verificados. Inicialización completa y secuencia de callers aún fuera de prueba.
- [x] **P2** Dificultad `0xc5470` (agarre trasero ×1/1,15/1,3/1,3, `--difficulty N`) y tipo de control
      `0x1fefd0[coche]+4` = 1 (IA: perfil 11, bono de par, modelos de `0xc5520`). Pendiente: tipos 2 y 7.
- [ ] **P2** Superficies especiales `0x12c–0x131` (meta, rampas…) y `0x6e–0x83`.
- [ ] **P2** Trucos restantes: identificar el efecto de SURMULE (`0x1fb2f4`), SLASKTRATT (`0x1fb2f8`),
      FILMJOLK (`0x1fb2fc`, `0x420cc`), SKUNK (`0x1fb300`, muy usado en efectos y `0x48a14`), SVINPOLE (`0x1fb304`) y
      STRINGS (`0x1fb308`).

### Fase 2 — Mundo vivo: objetos, colisiones y efectos

- [~] **P1** Animaciones **POS** (`pos_make_relative` `0x41244`, `pos_apply_frame` `0x412e8`): parser, deltas cíclicos,
      transformaciones y avance a 36 Hz integrados. Pendiente verificar regresiones, el orden exacto de rotación y la
      cobertura de suelo/colisiones para objetos trasladados.
- [ ] **P1** Colisiones con objetos móviles `car_ground_contact` `0x350f0` y los tipos `0x360f0`, `0x3636c` y
      `0x36ce0` (pase `collisions_pass` `0x3fd04`).
- [x] **P1** Derrape `0x3731c`: marcas (anillo de 700, fila SHD por superficie `0xc34e0`) y polvo (efecto 1,
      `sub_00039d1c`, sprite de `SMOKE.TEX` mezclado con la SHD) y sonido de `GENERAL/SOUND/SKID`.
- [ ] **P2** Objeto especial PLC tipo `0xfa` (`0x2abdc`).
- [x] **P2** Efectos (`fx.c`, 200 huecos de `0x1d5f10`, `sub_0003fee8`): sprites `0x39d1c`/`0x39fe8`, piezas
      `0x3b744`, salpicadura `0x3d808`, destellos de reaparición `0x3e6ec`; explosión y hundimiento (`0x3a788`,
      `0x3c208`) y turbo (`0x4e97c`), con las texturas `GENERAL/*.TEX`.
- [x] **P2** Efectos de chispas (*plots* `0x3a2b4`/`0x3a498`, tipos 2 y 8: roce con muro `0x3ea00` y ruedas sobre
      superficies 6/0x10/0x1a `0x3ee70`), golpes entre coches `0x2ea44`, relámpagos `0x3bdac` (malla `0xbfe90` y
      destello de paleta) y destrucción por alud `0x3db4c` (tipo `0xa`).
- [x] **P2** Clima (`weather.c`): creación `0x26154`, zonas `0x400f4` y actualización `0x403c8` (lluvia de CANADA,
      nieve de AUSTRIA, viento de USA; mosaico de 3×3 alrededor del coche y oscurecimiento de paleta).
- [x] **P2** Emisores de objetos especiales `0x36ce0` (humo `0x12e` de USA, géiseres `0x131` de ICELAND) y `0x3636c`
      (chimeneas y vapor `0x15e`/`0x15f`, `0x161`, `0x164`, `0x130`, niebla `0x166`, columnas `0x167`), y polvo al
      aterrizar `0x3f37c`.
- [ ] **P2** Mapeo exacto entre los objetos del motor y la PLC (hoy se infiere al 88–98 %): portar la creación del
      mundo `0x2489c` ("Error while creating the world") para obtener el orden real.

### Fase 3 — Render fiel

- [ ] **P1** Rasterizador del motor Lisa 2 (`0x54000–0x60000`, ~12 900 instrucciones): orden de dibujo, recorte,
      relleno y z (o algoritmo del pintor).
- [ ] **P1** Sombreado **TAB**: cómo se elige el nivel por cara (bits bajos de `flags`: `0x11/0x13/0x15`; byte alto
      ≥ 64 = sin sombra) y luz por distancia o normal.
- [ ] **P1** Función de **PAN** (tabla de 64 KB `PAN[a*256+b]`; filas 16–159 a cero, así que no es la mezcla de las
      caras 18/19/22). **SHD**: fila 0x28 = sombra de coche (hecho); resto de filas por confirmar en el DOS.
- [ ] **P1** USA tramos 48–50: la IA no salta el barranco a ~70 km/h, cae al fondo y no hay reaparición
      (`0x00435910` en `Ign_win.exe`), así que se queda atascada.
- [ ] **P1** IA contra los muros: ahora los bordes contienen a los coches en las 7 pistas, y la IA (que apunta al punto
      medio de puertas a +1…+8 tramos) corta las curvas contra el borde interior a toda velocidad y se queda empujando
      (ICELAND tramo 17 a 157 km/h, tramo ~65, JAPAN, CARIB). Falta la línea de carrera y los registros por tramo
      originales de `load_tri` ya se cargan parcialmente, pero falta reproducir toda su geometría de conducción.
      El contador de atasco y las ventanas de marcha atrás ya siguen el original; no garantizan desatascarse.
- [x] **P1** Planificador rotatorio de IA de `cars_update_all`: fase de controles a 36 Hz, pero decisión nueva
      por coche a **36/N Hz**, con N coches totales, incluidos humanos/inactivos. Resto de turnos: replay de
      pedales, dirección y solicitud de turbo; caja automática antes de controles, física a 72 Hz.
      Contador de atasco incrementado por N, sin espera artificial. Verificación contra fragmentos LE:
      **17 640 casos sin diferencias** y **32 comprobaciones de caché**. Cadencia en el ejecutable con N=1..8,
      replay independiente 21/21 y conducción humana recta/turbo exacta frente al binario previo (14/14).
      Smoke de 200 s con siete rivales en siete pistas, repetido con estados/trazas/imágenes idénticos y sin
      valores no finitos. No verifica la IA completa ni el dispatcher: `car+0x350` sigue sin modelar y la
      geometría del error de rumbo aún es aproximada. La referencia histórica no se regenera.
      Comparación histórica actual: 1/23, con 22 diferencias de estado y 22 de imagen, sin fallos de ejecución;
      ya existían divergencias previas y este cambio altera deliberadamente la conducción de IA.
- [x] **P1** Cámara de persecución original (`sub_00047f94`, `sub_000420cc`, focales de `race_setup_video`).
- [x] **P2** Cámara trasera (`car+0x5a4`, campos `+0x90/+0xa0/+0x70`) y cámaras fijas tras la meta (`0xc464c`).
- [x] **P1** Cuenta atrás y reloj de salida (`sub_0002b850`) con sus pitidos; faltan los sprites 3-2-1-GO.
- [x] **P1** Conversión del reloj por coche y transición normal verificadas contra instrucciones originales;
      18 450 casos / 74 178 pasos, HUD/turbo/fantasma conectados; timer DOS y pausa pendientes.
- [ ] **P2** `render_scene` `0x56410`: capas según los flags de configuración `[0x24c05c]+0x38..0x54`.
- [x] **P2** Resoluciones 320×200 / 640×480 / 800×600 (`--res`), mostradas a 4:3.
- [ ] **P2** Viewports por jugador (`race_setup_video` `0x492fc`).
- [ ] **P2** Sprites (`geputget.c`, `0x771e0`) y señales (`*_SIGNS.PIC`).
- [ ] **P3** Escalado moderno (pantalla completa, relación de aspecto, filtrado opcional).

### Fase 4 — HUD, menús y flujo de juego

- [ ] **P1** Fuentes **LFT/FNT** (`load_fonts` `0x240f4`, `0x61220`): LFT ya se lee para los campos del HUD básico;
      faltan FNT y el resto de las fuentes.
- [ ] **P1** Completar el HUD en carrera `0x4a320`: ya dibuja partes de `H_PAN1.PIC` y los campos de tiempo, vuelta,
      posición y velocidad. Pendiente: reconstruir coordenadas/estados exactos, medidores dinámicos, avisos
      multilingües y los demás paneles `H_PAN*.PIC`, `*_PANGFX.PIC`, `*_SIGNS.PIC`.
- [ ] **P1** Pantallas de resultados y puntuación (`render_main` `0x43fe0`: TRACK RESULTS, TRACK SCORE, TOTAL SCORE)
      y trofeo `POKAL.PIC`.
- [ ] **P1** Pausa, continuar, salir y reiniciar (`0x4395c`, `0x43e48`).
- [x] **P1** Menús: `menu_load_gfx` `0x11504`, `menu_frame` `0x12bb8`, callbacks `0x160c4`/`0x170ac`, motor de menús
      `0x51b20`–`0x539f8` con las 25 páginas × 6 idiomas, intro CDP, fondo PSQ/PFM al píxel, carrusel 3D `0x19508`,
      récords, créditos, cajas `MENUBKG.DAT` y `IGN_DOS.BTZ`. Pruebas `menu_flow` y `menu_background`.
- [ ] **P1** Llevar a la carrera el resto de la configuración del menú: modos (`[0xe9c04]`), nombres, teclas de
      `IGN_DOS.BTZ`, opciones gráficas, fantasma y obstáculos.
- [ ] **P2** Modos de juego (campeonato, contrarreloj, práctica…), desbloqueos y flujo entre pistas.
- [ ] **P2** Pantallas de carga (`0x21d68`) e idiomas (inglés, alemán, francés, italiano, español y sueco).
- [ ] **P2** Récords al terminar la carrera en **BTZ** (`0x2b430`, `0x2cf60`); el formato y la carga/guardado del menú
      ya están portados (`settings.c`).
- [x] **P3** Vídeos **CDP** (`IGN1.CDP`, `IGN2.CDP`, `IGN3_*.CDP`) y *Script Player* `0x549b0`.
- [ ] **P3** Capturas TGA `0x515f8` y superposición de depuración `0x49fdc`.

### Fase 5 — Sonido y música

- [x] **P1** Sustituir el driver (Sound Blaster/GUS, `0x7a85c` y `0x60000+`) por SDL3 audio con un mezclador propio.
- [x] **P1** Cargador de muestras `0x63310` (`.WAV`, `.PAT` de Gravis) y formato **XI**; bancos por prefijo `NN_`.
- [x] **P1** Motor de cada coche (`0x50824`): **ENGINE.INF** + `00_A.WAV`/`01_A.WAV` con tono según las revoluciones
      (`car+0x288`, `car_throttle` `0x4e000`).
- [x] **P1** Efectos: BOOST, COLL, DIV, ROLL, SKID (`0x2a4ec`); reproducción posicional `0x49a8c` y canales `0x626fc`.
- [x] **P2** Ambiente por pista `LEVELS/<pista>/SOUND`: emisores PLC `0x190+n` (`0x2489c`, `0x360f0`), lluvia,
      viento y truenos.
- [ ] **P2** Música `MUSIC/Track02–08.ogg` (antes pistas de CD): asignación por pista y menú.

### Fase 6 — Fantasmas y persistencia

- [x] **P2** Formato **GST**: lectura (`0x2adf8`), grabación por tick (`0x51204`) y guardado con relleno (`0x2ccc8`).
- [x] **P2** Reproducir el fantasma (`--ghost`): una muestra por tick, sin choques, con el modelo de la cabecera.
- [ ] **P2** Modo contrarreloj (`[0x1feefc] == 2`): grabar al terminar si se bate `[0x1fee04]` y cargar al empezar.
- [ ] **P1** Validar física e IA contra un fantasma real: grabar una contrarreloj en DOSBox y usar `--ghost`.
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
