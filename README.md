# Ignition — port nativo por ingeniería inversa

Reconstrucción en **C11 + SDL3** de *Ignition* (Unique Development Studios, 1997) a partir del ejecutable DOS
original (`MAINDOS.EXE`, DOS/4GW, Watcom C). El objetivo es un juego nativo para **macOS**, y después **Windows** e
**iOS**, fiel al original. Cada sistema se porta tras desensamblarlo y documentarlo.

> **Datos del juego**: este repositorio no contiene ni debe contener archivos del juego (son propiedad de sus autores).
> Hace falta una copia original (p. ej. GOG), por defecto en `/Users/Shared/Epic Games/Ignition/ignition`.
> `.gitignore` excluye ejecutables, imágenes y volcados derivados.
> Como las demás tablas portadas (p. ej. los récords de fábrica de `save.c`), las estructuras y textos del menú de
> `MAINDOS.EXE` están en C (`src/frontend/menu_data.c`, regenerable con `tools/gen_menu_data.py`): así se compila sin
> Python ni capstone. Imágenes, sonidos, vídeos y fuentes se leen siempre de la copia del juego.

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
clasificación básicos; los rivales usan el **controlador de IA original portado**, contrastado contra sus bytes LE.
POS ya carga y anima los objetos de pista, con rotación
aproximada; los objetos móviles cuentan en las colisiones como en el motor original: conos y barriles que salen
volando, peligros móviles que destruyen el coche, géiseres, coches aplastados y animaciones de textura. El HUD de carrera
(`0x4a320`) está portado en sus tres resoluciones con las coordenadas, estados, aguja, turbo, señales, semáforo
3-2-1-GO, lugar al terminar y avisos multilingües originales; los rasterizadores 2D que usa (triángulo plano y sprites
con matriz) son idénticos píxel a píxel a los bytes originales. El **frontend original está portado**: pantalla de
idioma, intro en vídeo CDP, menús en 6 idiomas con su motor de páginas, fondo vectorial animado (idéntico al píxel al
original), carrusel de coches 3D, récords, créditos y configuración en `IGN_DOS.BTZ`; al elegir pista y coche arranca
la carrera y al salir de ella se vuelve al menú. Hay una implementación nativa integrada de pausa, resultados,
puntuación y avance del campeonato; falta validar ese ciclo de extremo a extremo. Siguen pendientes la validación
integral de una carrera DOS, el sombreado original y el multijugador.

| Métrica | Valor |
|---|---|
| Funciones candidatas detectadas | **1 112** (bytes de código cubiertos: 98,5 %; 446 clasificadas como runtime) |
| Instrucciones únicas desensambladas | **128 832** (19 033 de runtime/libc/DOS/4G) |
| Código del juego marcado como portado/verificado | **50 134 de 109 799 instrucciones (45,7 %)**; **212 de 666 funciones (31,8 %)** |
| Código del juego marcado como parcial | **14 089 instrucciones en 29 funciones**; excluido del numerador portado |
| Funciones de juego vinculadas explícitamente a una tarea del roadmap | **666 de 666 (100,0 %)**; 343 documentadas, 293 inferidas y 30 por resolver; no es cobertura de portado |
| Formatos de datos propios descifrados | 15 de 21 (TRI, POS y TAB solo en parte) |
| Código del port | ≈ 13 900 líneas de C (`src/`, sin contar las tablas generadas `menu_data.c`) |

Los totales anteriores se derivan del catálogo único
[`docs/functions.json`](docs/functions.json), que contiene estados, tareas, notas, implementación, pruebas,
referencias y revisiones históricas por dirección. La cobertura cuenta instrucciones únicas de las funciones
marcadas `ported` o `verified`, excluye `partial` y las funciones clasificadas heurísticamente como `library`.
Mide cobertura de código original asociado a funciones, no equivalencia instrucción por instrucción. El **98,5 %**
es cobertura del desensamblado, no del port; la clasificación runtime también es heurística.

### Dashboard del port

La sección **Conducción y carrera** incluye ahora helpers de signo/normalización, apoyo y avance
antiguos, centro de bordes de carretera y correcciones de balanceo y grabación del fantasma,
con **1 014 casos contra instrucciones LE originales**.
Los helpers sin llamador DOS no se insertan artificialmente en la física activa. El balanceo del
modelo 7, varios wrappers de entrada/efectos y la equivalencia de una carrera completa siguen
parciales; el catálogo no los presenta como terminados.
Detalle: [kernels auxiliares](docs/formats.md#conduccion-y-carrera-kernels-auxiliares).

Abrir [`docs/port-dashboard.html`](docs/port-dashboard.html) en el navegador: funciona sin servidor ni dependencias
externas. Muestra cobertura de instrucciones únicas, estados por función, sistemas, roadmap y un inventario con
búsqueda y filtros. Las fichas desplegables muestran evidencia individual, certeza de la clasificación,
notas, implementación y pruebas documentadas. Es una instantánea del catálogo, no un porcentaje de fidelidad
o de juego terminado.
Para regenerarla después de actualizar el catálogo único `docs/functions.json`:

```sh
python3 tools/port_dashboard.py
```

El generador valida el catálogo y rechaza una cobertura desactualizada; no necesita el ejecutable ni los datos
del juego. El HTML es una vista generada, nunca otro inventario editable.

---

## Compilar y ejecutar

Requisitos en macOS: `brew install cmake sdl3`.

```sh
cmake -S . -B build
cmake --build build
./build/ignition          # frontend: idioma, intro, menús; la carrera sale de lo elegido
./build/ignition USA      # carrera directa, como antes
```

Sin argumentos (o con `--menu`) arranca el **frontend** del original. Su configuración es la misma `IGN_DOS.BTZ` que
usa la carrera, en la carpeta de usuario (`SDL_GetPrefPath("UDS", "Ignition")` o `IGN_SAVE_DIR`); el de la carpeta del
juego solo se lee si aún no hay uno propio, y nunca se escribe en ella. Del menú pasan a la carrera la pista, el coche
del jugador, el número de corredores (6, o 1 en contrarreloj), la dificultad, la caja de cambios, el modo (contrarreloj
con fantasma, persecución = eliminación) y la música (volumen y pista); Esc en carrera vuelve al menú.
En el menú: flechas, Enter, Esc/Retroceso; cualquier tecla salta la intro o los créditos.

Pistas: `USA`, `CANADA`, `AUSTRIA`, `BRAZIL`, `CARIB`, `ICELAND`, `JAPAN`. La ruta de datos se cambia con la variable
`IGNITION_DATA=/ruta/al/juego`.

### Controles

| Tecla | Acción |
|---|---|
| ↑ / W | acelerar |
| ↓ / S | frenar / marcha atrás |
| ← → / A D | girar |
| Espacio | turbo (barra y piloto del HUD; la carga también aparece en el título de la ventana) |
| M | alternar cambio manual/automático |
| Q / E | bajar / subir marcha en modo manual |
| F1 | cámara libre (W/A/S/D, Q/E, flechas, Shift = rápido) con la superposición de depuración de `0x49fdc` |
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
| `--time-trial` | contrarreloj (`[0x1feefc] == 2`, sin rivales): carga `GHOSTS/<pista>.GST` de la carpeta de usuario y lo graba si se bate |

Variables de entorno de depuración: `IGN_TRACE=1` (traza por tick y estadísticas de tramos y puertas),
`IGN_TRACE=full` (estado inicial y cada tick, esquema v5 de 112 campos con controles y precisión de 17 cifras),
`IGN_SAVE_DIR=carpeta/` (carpeta de récords, `IGN_DOS.BTZ` y fantasmas en lugar de `SDL_GetPrefPath`; sin ventana
activa la persistencia),
`IGN_TRACK_DUMP=archivo` (vuelca los tramos), `IGN_LIFT=u` (eleva el coche en el visor para ver la sombra),
`IGN_BANARNE=1` (carrocería alta en el visor), `IGN_LANG=en|de|it|es|sv|fr` (idioma de los avisos del HUD; por defecto
el de `IGN_DOS.BTZ`) e `IGN_KEYS="ms:Tecla,ms:Tecla,…"` (pulsaciones guionizadas con nombres de tecla de SDL, o `quit`,
cada una `ms` después de la anterior). Con esta última se comprobó el ciclo completo en ventana: menú → carrera → Esc
→ menú (idioma confirmado solo, iris) → salida, por ejemplo
`IGN_SAVE_DIR=/tmp/s/ IGN_KEYS="1500:Return,1500:Space,2500:Space,1500:Return,1000:Return,1000:Return,8000:Escape,5000:quit" ./build/ignition`.

### Biblioteca C y DOS/4G

El port usa servicios C11/libm en lugar de reconstruir el runtime Watcom. El catálogo documenta
**38 correspondencias** de esta categoría: 8 funciones verificadas y 30 sustituciones
nativas de memoria, archivos, errores, cadenas y matemáticas. No se declara completada la categoría
de 306 entradas: quedan 268 sin resolver.
El dashboard muestra las sustituciones nativas separadas de las funciones portadas/verificadas,
también en las barras de subsistemas. Su estado no añade instrucciones al numerador portado.

El generador compatible se centraliza en `src/platform/runtime.c`; ya no depende de `rand()`/`RAND_MAX`
del host. Se preserva la secuencia de efectos/IA y se corrigen los sorteos de HUD, cámara, menú y parrilla.
`runtime_native` prueba los contratos C11; `runtime_original` (con `IGN_VERIFY_ORIGINAL=ON`) compara
**45 725 casos** contra bytes LE sin parchear: azar, cadenas, rellenos y conversiones de enteros exactos;
matemáticas con tolerancia explícita. El segundo lote añade **cuatro implementaciones nuevas**:
dos rellenos que conservan patrones/alineación/cursores originales y dos conversiones int32/uint32 en bases
2..36, integradas en limpieza de fotogramas y posiciones del HUD. Añade además una equivalencia nativa
verificada de `memset`; no cuenta esa sustitución existente como una quinta implementación nueva.
Véanse las [correspondencias individuales, evidencia y límites](docs/formats.md#biblioteca-c-y-dos4g-equivalentes-nativos).

```sh
ctest --test-dir build -R '^runtime_(native|original)$' --output-on-failure
```

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
`double` y dos indicadores enteros. La tolerancia de las salidas es `2e-14 + 1e-14·|original|`; los indicadores
y los campos no modificados deben coincidir exactamente. No se ha encontrado una divergencia que justifique
cambiar `ign_body_step`.

Esto verifica **la rutina aislada, con x87 emulado y la palabra de control del juego `0x127f`**, no el juego
completo, `car_update`, colisiones, IA ni una carrera DOS. Véanse los límites, la
[palabra de control x87](docs/formats.md#palabra-de-control-x87) y la
[revisión de los avisos de Ghidra](docs/formats.md#auditoría-de-la-decompilación-ghidra).
Unicorn calcula `FSIN`/`FCOS`/`FPTAN`/`FPATAN` en binary64 del host, así que `body_step` no puede ser exacto bit a bit.
La regresión general preexistente pasa **0/23** casos (antes 1/23: la imagen de `USA viewer car 9` cambia al
desactivar la contracción FMA). No se han cambiado los resultados esperados para convertir esas diferencias en un éxito.

### Kernels del coche verificados contra el original

El oracle `tests/car_original.py` pasa **42 530 casos / 302 572 pasos** contra los bytes LE:
dirección completa `0x500f0` (732 casos), bloque de agarre `0x3070a..0x30874` (672) y aritmética vertical
`0x2e234`, hasta la frontera de efectos de aterrizaje o retorno (252), más aceleración (6 829),
freno (72), aritmética de turbo (2 016), revoluciones (25 016) e integración acelerador→revoluciones (6 941).
Usa los kernels reales de `car.c`, sin reemplazar sonido/efectos por respuestas inventadas.
Con la palabra de control del juego (`0x127f`) y el port compilado sin contracción FMA, todos los kernels
coinciden **bit a bit** (tolerancia cero). Los informes conservan procedencia y cobertura.

Esta prueba confirmó y permitió corregir dos errores de agarre: si una rueda tiene un tipo fuera de 0..29,
se conserva la media anterior (inicial 0,4) y ambos agarres; los rivales usan perfil 11 y su propia tabla
trasera por modelo/dificultad, no la fórmula humana. Dirección, altura y `body_step` no necesitaron
cambiar sus fórmulas. Hay fixtures de límites numéricos, aleatorios y secuencias válido→inválido→válido.

Los tres bloques nuevos pasan **8 917 casos / 127 230 pasos**. Se corrigieron la truncación x87 del índice
de par, los límites originales de patinaje, el bloqueo por freno y el turbo agotado: limpia modo/frames
y no añade fuerza. Las salidas coinciden bit a bit; un control negativo con `0x037f` (precisión de 64 bits)
debe cambiar índices de par en el barrido `torque-index`. Las curvas de par son entradas suministradas
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
**Desviación**: sin carpeta de usuario (`--shot` sin `--time-trial` ni `IGN_SAVE_DIR`) la mejor vuelta parte de
`DBL_MAX`; con ella, del récord guardado de la pista, como `0x2a442` (véase `save.c`).
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

### Contacto y objetos móviles verificados contra el original

`tests/contact_original.py` ejecuta en Unicorn los bytes LE del contacto coche↔objeto y de las consultas sobre la
lista de objetos del motor, y los compara con `contact_test --runner`:

```sh
ctest --test-dir build -R '^contact' --output-on-failure
```

Son **11 303 casos**, todos dentro de tolerancia y **11 262 exactos** (1e−12):

- cruce de polígonos `0x349e8` (1 503), impulso `0x346cc` (1 500, 1 459 exactos) y reflexión `0x35f38` (1 500);
- fragmentos de vuelo `0x356b8` y rebote `0x35967` de `car_ground_contact` (1 500 cada uno);
- objeto especial bajo el coche `0x3383c` (1 500) y consulta de suelo sobre la lista del motor `0x32dc8` (1 500), ambas
  sobre listas sintéticas;
- lista del motor `0x58c1c` (300 rejillas sintéticas con cadenas y capas) y paso de animación de texturas `0x10a00` (500).

Los 41 impulsos no exactos difieren en 1 ulp float: `jB0 = −1,25·j0` (masas 10 y 2) cae en el punto medio de dos
floats y el x87 desempata con su redondeo de 80 bits, que el port no tiene en arm64. El oracle encontró y corrigió dos
errores del port (par de cruces más alejado y `r·k` en float32). `contact_real_assets` comprueba en las 7 pistas el
número de objetos de colisión, que todos aterrizan y reposan sobre el suelo, que la consulta encuentra cada objeto
especial antes y después de 200 fotogramas POS, que las animaciones de textura vuelven al estado inicial tras un ciclo y
que ningún objeto móvil tiene spans SRF. No verifica la composición completa de `car_update`/`collisions_pass` ni los
efectos de sonido y sprites (portados sin oracle). Véase
[`docs/formats.md`](docs/formats.md#objetos-de-colisión-contacto-objetos-especiales-y-collisions_pass).

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
IGN_SAVE_DIR=/tmp/ign/ ./build/ignition USA --time-trial --sim 150 auto --shot /tmp/x.ppm
python3 tools/btz.py info /tmp/ign/IGN_DOS.BTZ
```

---

## Estructura del proyecto

```
CMakeLists.txt          objetivos: ignition_core (biblioteca), ignition (ejecutable), elf (volcado ELF)
src/game/assets.{c,h}   carga de COL, PIC, PLC, MSH, TEX, SRF, TRI; consultas de suelo (SRF y lista del motor); rejilla
                        de objetos del motor; muros; mapeo motor↔PLC
src/game/physics.{c,h}  cuerpo rígido (modelo bicicleta), par motor, muros, choques entre coches, tablas por coche
src/game/car.{c,h}      coche: suelo bajo 4 ruedas, vertical, dirección, acelerador, turbo, pose visual, vueltas,
                        contacto con objetos, aplastamiento, reaparición
src/game/track.{c,h}    clases PLC, orden de tramos y bifurcaciones, puertas por tramo (TRI)
src/game/ai.{c,h}       IA de rivales (controlador original 0x1aea0)
src/game/ghost.{c,h}    fantasmas GST: leer, grabar a 72 Hz y guardar con el formato exacto del original; reglas de contrarreloj
src/game/save.{c,h}     IGN_DOS.BTZ: imagen por defecto, récords, campeonato y persistencia en la carpeta de usuario
src/game/skid.{c,h}     efectos de derrape: marcas en la pista y polvo (`sub_0003731c`)
src/game/fx.{c,h}       huecos de efectos del original: sprites, piezas, salpicaduras, explosión, turbo, reaparición
src/game/pos.{c,h}      animaciones POS, objetos de colisión, objetos especiales, animaciones de textura, mensajes
                        del HUD y humo de eliminación (`collisions_pass`)
src/render/raster.{c,h} rasterizador por software de 8 bits (320×200 a 800×600, 4:3), z-buffer, texturas afines
src/render/hud.{c,h}    HUD de carrera (`0x4a320`): paneles, medidores, señales, semáforo, lugar, mensajes, ventanas
src/render/font.{c,h}   fuentes LFT (ranuras de `0x24c214`) y FNT (`0x53d60`)
src/render/sprite.{c,h} sprites de geputget con tabla PAN, triángulo plano `0x729f3` y sprites con matriz `0x82950`
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
tests/save_btz.c        BTZ de ida y vuelta byte a byte, récords, campeonato y reglas del fantasma
tests/time_trial.py     contrarreloj sin ventana: grabar, recargar y sustituir el fantasma solo si se bate
tests/btz_defaults_original.py imagen BTZ por defecto contra `0x1a864` + `0xbf628` del original (opcional, Unicorn)
tests/body_step.c       ejecuta la física C para el verificador diferencial
tests/physics_original.py replay aislado del integrador LE contra el port, con Unicorn opcional
tests/car_original.py     replay LE de dirección/agarre/altura/aceleración/freno/turbo contra kernels C
tests/lap_original.py     cruce de vuelta, fases, tiempos y mejor vuelta contra fragmentos LE
tests/car_kernels.c       runner nativo de los seis kernels, sin duplicar sus fórmulas
tests/contact.c           contacto y objetos móviles: pruebas unitarias y runner del oracle
tests/contact_original.py replay LE de contacto, vuelo/rebote, consultas sobre la lista del motor y texturas
tests/contact_real_assets.c objetos de colisión, especiales, POS y texturas en las 7 pistas
tests/solo_replay.py    replay sin rivales, exacto por tick, con informe de procedencia (solo port)
tests/test_verification.py controles positivos y negativos de las herramientas de verificación
tests/hud_assets.c      parsers LFT/FNT, sprites, formato de tiempo y fotogramas del HUD sin ventana (`GAME_DIR [PPM_DIR]`)
tests/hud_original.py   triángulo `0x729f3` y sprites con matriz `0x82950` contra bytes LE (Unicorn, `IGN_VERIFY_ORIGINAL`)
tools/ignassets.py      PIC/COL/TEX/DAT/nivel → PNG
tools/gst.py            fantasmas GST: cabecera, validación, tiempos por vuelta y trayectoria sobre la pista
tools/btz.py            IGN_DOS.BTZ: campos, participantes, récords por pista y comprobación de ida y vuelta
docs/functions.json    catálogo único: funciones, datos/etiquetas, estados, notas, implementación, pruebas y cobertura
tools/function_catalog.py validación, consultas y sincronización del resumen del README (sin datos del juego)
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
| TRI | bordes izquierdo/derecho por tramo (puertas de la IA) | ✅ cargador, slots y geometría derivados portados; quedan bytes del archivo no usados por el cargador |
| POS | animaciones de objetos (fotogramas pos+rot) | 🟡 parser, avance a 36 Hz, render y colisiones (solo traslación, como el motor); orden de rotación del render aproximado |
| TAB | tabla de sombreado `color×nivel` (64 niveles) | 🟡 significado conocido; no se usa |
| PAN / SHD | tablas 256×256 de 64 KB | 🟡 SHD fila 0x28 = sombra; PAN = tabla de mezcla de los sprites y primitivas del HUD (portada) |
| CARS | 11 coches × (carrocería + 4 ruedas) | ✅ portado y montado como en `cars_init` |
| LFT / FNT | fuentes | ✅ portado (`font.c`): ranuras de `0x61399`, ancho/dibujo alineado de `0x616db`/`0x61959`, FNT de `0x53d60`; las usan el HUD y el menú |
| CDP | vídeos de introducción | ✅ portado (`cdp.c`) |
| PSQ / PFM | animación vectorial del fondo del menú ("Script Player") | ✅ portado al píxel (`pfm.c`) |
| DAT (`MENUBKG.DAT`) | atlas de las cajas de resaltado del menú | ✅ portado (`gfx2d.c`) |
| GST | coches fantasma | ✅ leer, escribir, reproducir y contrarreloj (`ghost.c`, `save.c`, `tools/gst.py`) |
| BTZ | configuración y récords | ✅ todos los bytes del fichero distribuido mapeados; ida y vuelta exacta (`save.c`, `tools/btz.py`); el HUD usa su idioma y récords de carrera y el menú lo edita (`settings.c`) |
| INF | curvas del sonido del motor por coche | � 4 tablas de 200 B: tono y volumen de `00_A`/`01_A` por régimen (`sound.c`) |
| WAV / PAT / XI / OGG | sonido (PCM, Gravis, FastTracker, pistas de CD) | 🟢 WAV, PAT y XI (`audio.c`); OGG con stb_vorbis (`music.c`) |

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
| Dirección | `human_input` `0x2c5b8..0x2c90c`, `car_steer` `0x500f0` | `car.c` | teclado: 24 casos exactos (izquierda −100, derecha +100, prioridad derecha, inversión al aplastarse); integrador: 732 casos / 5 856 pasos contra bytes LE |
| Agarre por superficie y coche | bloque `0x3070a`, tablas `0xc33f0`, `0xc4dc0`, `0xc5670` | `car.c` | verificado: 672 casos / 1 536 pasos; media persistente y reglas de rivales |
| Consulta de suelo | `ground_query` `0x20814` | `assets.c` | fiel: puntos sin rotación, coste entero (altura acotada a los vértices de la cara) |
| Rejilla y lista de objetos del motor, consulta sobre ella | `0x569ec`, `LI_PLACEOBJECT`/`LI_MOVEOBJECT`, `0x58c1c`, `0x32dc8` | `assets.c` | verificadas: 300 listas y 1 500 consultas exactas; usadas por la reaparición y el relámpago |
| Objetos de colisión (conos, barriles, balizas) | `0x24f22`, `car_ground_contact` `0x350f0`, `0x349e8`, `0x346cc`, `0x35f38` | `pos.c`, `car.c` | núcleos y fragmentos verificados: 7 503 casos |
| Objetos especiales 0x12c–0x131 | `car_update` `0x2ef9d`/`0x30dbb`, `0x3383c` | `pos.c`, `car.c` | consulta verificada (1 500); peligros móviles 0x12c, alud 0x130, vuelta de campana 0x12d/géiser, empujón 0x12e, aplastamiento 0x12f |
| Aplastamiento | `0x31858`, `cars_update_all` `0x2d3c2`, `car_update` `0x31316` | `car.c`, `pos.c`, `ai.c` | fiel salvo render (solo altura); inversión de dirección IA con ventana/dificultad original |
| Animaciones de textura | `0x2abdc`, `0x10508`, `0x10a00` | `pos.c` | paso verificado (500); creación portada |
| Resto de `collisions_pass` | `0x3fd04`: `0x4c858`, `0x4fcf0` | `pos.c`, `main.c` | cola de mensajes (sin dibujo) y humo de eliminación portados |
| Suelo bajo 4 ruedas, ruedas anómalas | `car_update` `0x2eea4`–`0x30d98` | `car.c` | fiel: esquinas de la caja ×1,1 escaladas por `1 + km/h / 220`, cabeceo `asin` limitado a 0,1 rad/tick |
| Vertical: gravedad, saltos, aterrizaje | `car_vertical` `0x2e234` | `car.c` | aritmética verificada hasta efectos: 252 casos; desviación explícita de subidas |
| Muros (aristas no compartidas, activación por superficie, modo salto, caché de 150) | `collect_walls` `0x33ee8`, `car_update` `0x2f856`, `cars_update_all` `0x2d77a` | `assets.c`, `car.c` | fiel |
| Choque con muros | `box_vs_walls` `0x1f708`, `wall_impulse` `0x1f3fc` | `physics.c` | fiel |
| Choque entre coches | pase en `0x2d2f8`, `0x1ebb4`, `0x1e638`; esquinas `0x1fdb4` | `physics.c`, `car.c` | kernels completos: 1 277 casos contra bytes originales; un contacto indefinido excluido; el pase completo del dispatcher no queda verificado |
| Tramos, bifurcaciones y saltos | `split_plc_types` `0x247b0`, `build_sections` `0x28f08` | `track.c` | topología, mapas y ángulos de 1 365 secciones comparados con ejecución original en las 7 pistas (`track_original`) |
| Vueltas, fases y tiempos | `car_update` `0x2f2be`, `0x2fc1c` | `race.c`, `car.c` | 3 024 casos / 4 112 pasos exactos por fragmentos; geometría, récord guardado y avisos excluidos |
| Parrilla de salida | `cars_init` `0x26458` | `main.c` | fiel |
| Turbo | `car_turbo` `0x4e2d8`, `car_input` `0x4d8eb`, `human_input` `0x2c99e` | `car.c` | aritmética: 2 016 casos / 96 768 pasos; arbitraje por fragmentos: 10 728 / 82 440 exactos; generación humana/latch: 5 184 / 31 104 exactos; sondeo, timer DOS, IA/joystick, sonido/efectos y coherencia del rumbo del caller no verificados |
| Relojes por coche/salida normal | `frame_time` `0x2b734`, `race_clock` `0x2b850`, reset `0x2a410`, update `0x4c738` | `race.c`, `car.c` | 18 450 casos / 74 178 pasos exactos, composicionales; timer virtual, pausa/salida forzada y fase completa de callers excluidos |
| Destrucción y reaparición | `sub_0003a788`, `sub_00041430` | `race.c`, `car.c` | reset de controles/turbo: 7 422 casos / 31 344 pasos exactos; suelo con `0x32dc8` sobre la lista del motor; desviación en huecos de suelo |
| Humo de la pista y polvo al aterrizar | `sub_0003636c`, `sub_00036ce0`, `sub_0003f37c` | `fx.c` | fiel (opciones de detalle activadas) |
| Montaje del coche | `cars_init` `0x265f6` | `car.c` | fiel |
| Pose: dirección visual, inclinación | `0x48a14`, `0x40ec8`, `0x2e6f8` | `car.c`, `raster.c` | fiel |
| Sombra del coche | `0x48a14` | `raster.c` | geometría fiel; sombreado aproximado |
| Trucos (BANARNE, SURMULE, SLASKTRATT…) | `menu_page_key` `0x160c4` | `frontend.c`, `main.c` | fiel: se teclean en el menú; BANARNE también en carrera |
| BTZ, récords, contrarreloj y campeonato | `0x2293c`, `0x2adf8`, `0x2ccc8`, `0x2cf60`, `render_main` `0x45938`, menú `0x1a864` | `save.c`, `ghost.c` | ida y vuelta byte a byte del BTZ real; imagen por defecto idéntica al original (Unicorn); reglas por pruebas unitarias; sin clasificación por puntos |
| IA de rivales | `ai_drive` `0x1aea0`, `load_tri` `0x1ce30`, `ai_find_section` `0x1decc` | `ai.c`, `track.c` | portados: 4 323 casos de controlador definido, 1 635 registros y 5 516 consultas comparados con el original; incluye pendiente, giro por aplastamiento, ganancia de modelos 5/8, esquiva, carriles y turbo; desviaciones explícitas ante lecturas indefinidas del original |
| Render 3D | motor Lisa 2 | `raster.c` | propio: z-buffer, sin sombreado TAB |
| HUD de carrera y mensajes | `race_hud` `0x4a320`, `0x4be7c`, `0x4c7c0`/`0x4c858`/`0x4c89c`, `0x4a150`, `0x28000` | `hud.c` | fiel: tablas de recortes, colocación y texto de las tres resoluciones, ventanas de uno y dos jugadores; contadores a 36 Hz |
| Fuentes LFT/FNT | `0x61220`, `0x61399`, `0x616db`, `0x61959`, `0x53d60` | `font.c` | fiel; pruebas del parser |
| Sprites, triángulo plano y sprites con matriz | `0x771e0`, `0x791c9`, `0x729f3`, `0x82950`/`0x78304`/`0x78ac0` | `sprite.c` | triángulo: 6 009 casos y sprites con matriz: 624 fotogramas idénticos a los bytes LE (`tests/hud_original.py`) |
| Frontend: carga, bucle por fotograma, cierre | `menu_load_gfx` `0x11504`, `menu_frame` `0x12bb8`, `menu_shutdown` `0x13a5c` | `frontend/frontend.c` | literal: intro, fundidos, créditos, iris, volúmenes; sin CD ni joystick |
| Motor de menús "Baltazar" | `0x51b20`, `0x51c94`, `0x527c8`, `0x534cc`, `0x5383c` | `frontend/gui.c` | literal; tablas extraídas del EXE (`tools/gen_menu_data.py`) |
| Callbacks de página y acciones | `0x160c4`, `0x170ac`, `0x155d0`, `0x16f18`–`0x17054` | `frontend/frontend.c` | literal; prueba `menu_flow` |
| Fondo animado PSQ/PFM | `0x549b0`, `0x54df8`, `0x60e3d` (tipos 7, 11, 15, 17, 18, 19) | `frontend/pfm.c` | **al píxel**: 209 fotogramas iguales al original emulado; prueba `menu_background` |
| Sprites con hot spot, cajas, CDP | `0x55968`, `0x541d4`–`0x54864`, `0x55384` | `frontend/gfx2d.c` (sobre `sprite.c`/`font.c`), `cdp.c` | literal |
| Coches 3D del menú | `0x19508`, `0x1aaf8` | `frontend/frontend.c` | modelos y giro con el motor Lisa 2, dibujados directamente sobre el fondo del menú (cámara aproximada) |
| Configuración y récords | `0x1a864`, `0x198bc`, `IGN_DOS.BTZ` | `frontend/settings.c` | literal |

Geometría IA: `tests/ai_geometry_original.py` compara ambos fragmentos LE con `atan2`/`__CHP`
reales, para 902 fixtures sintéticos y 1 565 registros de las siete pistas; compara bits float32
y coordenadas enteras. Ahora se ejecuta con el control word del runtime **`0x127f`** (53 bits), como
los demás oracles: con el antiguo `0x037F` (64 bits) el oracle pasaba, pero con `0x127f` el residuo
extendido del port fallaba en 297 comparaciones de interiores (20 registros reales) y en un límite de
la mezcla de rumbo. Interiores y mezcla usan ya aritmética binary64 directa; con `0x127f` hay 0 fallos.

Recorte del objetivo: `tests/ai_target_original.py` ejecuta sin parchear `0x1b4a2→0x1b823` de `ai_drive`
(cambio de rumbo inicial, recorte por interiores, byte TRI de agarre y suma de curvatura) con `atan2` real
y CW `0x127f`: **7 590 casos** (2 130 sintéticos y 5 460 de registros slot 0 de las siete pistas) sin
fallos; enteros, centros, signo y doubles comparados bit a bit. Cubre 224 de 228 instrucciones del
fragmento; mutaciones del port (bucle interior, signo, orden de producto, constante π, centro) se detectan.
Ese oracle por fragmento no valida la selección de registros/carril ni el controlador completo. Los consumidores posteriores
(divisor `3·índice+6` del acelerador, condición `índice>2` del ajuste interior, `look_off[índice]` del turbo,
`a8 = 1+(agarre+0.8)·curva`) se contrastan ahora además en `ai_drive_original`; la lectura inicial
corrigió además el divisor invertido del ajuste interior con signo +1 (`0x1c57a..0x1c5a4`).

Referencia histórica del bloque de recorte del objetivo: controles humanos (`--sim 60` con dirección 0, 1 y turbo):
**21/21** trazas `IGN_TRACE` de 60 s e imágenes idénticas al build
anterior al bloque, en las siete pistas. El smoke repetido de 200 s con siete rivales es determinista en las
**7/7** pistas y sin NaN. Frente al build anterior (casi todos los rivales en la vuelta 0), en USA, BRAZIL,
CARIB, ICELAND y JAPAN la mayoría pasa a las vueltas 1–3, pero CANADA (secciones −158/−159) y AUSTRIA
(196–201, todos en la vuelta 0) siguen atascados y quedan rivales a <10 km/h: no demuestra carreras
completas ni equivalencia integral. Se conserva la referencia histórica sin
regenerar: **0/23** (22 diferencias de estado, 23 de imagen, ninguna de ejecución).
Véanse [alcance geométrico y límites](docs/formats.md#geometría-derivada-de-load_tri-y-mezcla-de-rumbo-verificación-por-fragmentos).

Controlador y trazado completos: `track_original` ejecuta las entradas originales y compara **2 182 objetos,
55 735 vértices, 1 365 secciones, 1 635 registros y 5 516 consultas** en siete pistas. El recentrado se aplica
antes de construir grid/POS, conservando vértices mundiales y usando los nuevos orígenes en paredes/culling.
`ai_drive_original` compara **4 323 casos definidos** (1 617 sintéticos y 2 706 reales), con controles y
estado entero exactos; los doubles usan tolerancias `2e-13`. Otros 212 candidatos leen heap indefinido
en el original (24 reales): se enumeran, se coteja su estado de ruta/RNG definido y se exige salida nativa finita.
No se simulan los efectos de `car_input` en ese oracle ni una carrera DOS completa.
El smoke actual de 200 s con ocho coches, repetido dos veces, es determinista y finito en las siete pistas.
Vueltas del jugador automático tras corregir el rumbo: USA/CANADA/CARIB/ICELAND 3,
AUSTRIA/BRAZIL/JAPAN 2. No es una garantía de terminar todas las carreras en ese tiempo.
**Diagnóstico integrado anterior a la corrección:** 49 225 decisiones reales de siete carreras de 200 s;
48 580 casos definidos coinciden con el controlador DOS y 645 lecturas indefinidas se excluyen explícitamente.
Se confirmó una frontera sin portar en `cars_update_all`: el original llama a `angle_normalize` (`0x51938`)
para copiar `body+0x28` a `car+0xf8`, pero la IA nativa recibía directamente el rumbo acumulado del cuerpo.
Aplicar sólo esa copia DOS a los estados capturados cambiaba **11 984 controles** y 220 actualizaciones del atasco.
**Corrección aplicada:** `IgnCar.drive_heading` conserva la copia normalizada separada de `body.heading`;
se sincroniza al inicializar/cambiar pose, tras física y después de colisiones, antes de los controles.
El helper y su fragmento llamador pasan **291 casos bit a bit**, incluidos límites y cero con signo.
Las nuevas carreras registran **49 444 decisiones: 48 945 comparaciones definidas sin discrepancias**
y 499 lecturas indefinidas DOS excluidas. No hay discrepancias de copia ni entradas IA fuera de rango.
Las permanencias de ≥5 s en una sección pasan de **24 a 0**; CARIB pasa de vuelta 0 a 3 y CANADA de 1 a 3.
En 14 pruebas humanas recta/turbo sin rivales se conservan estados e imágenes del binario anterior.
`car_update` y el resto de `cars_update_all` siguen parciales; esto no verifica una carrera DOS integral.
Véase [diagnóstico y reproducción](docs/formats.md#diagnóstico-integrado-de-atascos-2026-10-16).
Los informes se generan con:

```sh
ctest --test-dir build -R '^(track_original|ai_drive_original)$' --output-on-failure
```

Con `IGN_VERIFY_ORIGINAL=ON`, CTest genera primero las tablas originales como fixture compartida.
Detalle y desviaciones: [controlador completo](docs/formats.md#controlador-completo-y-tablas-originales).

---

## Cobertura del desensamblado

Estado de las funciones del juego en el catálogo único [`docs/functions.json`](docs/functions.json).
Las tablas de este README y el dashboard son vistas derivadas; se edita una sola ficha por dirección en el JSON.

| Estado | Funciones | Interpretación |
|---|---:|---|
| `unknown` | 333 | Sin identificación fiable; requiere análisis |
| `identified` | 44 | Nombre o propósito provisional conocido; falta establecer su comportamiento completo |
| `understood` | 2 | Comportamiento entendido, todavía no implementado |
| `port-pending` | 0 | Pendiente explícita y priorizada (ninguna: `car_ground_contact` y `collisions_pass` ya están portadas) |
| `partial` | 29 | Implementación incompleta o solo se verificó un fragmento; no cuenta como portada |
| `ported` | 150 | Implementada en el port, sin afirmar verificación completa |
| `verified` | 62 | Verificada para el alcance anotado, no implica equivalencia de toda la carrera |
| `native` | 46 | Bucle/temporizador (`0x10010`–`0x1034c`) → SDL; busy, play y stop de CD → reproducción OGG |
| `library` | 446 | Runtime/libc/DOS/4G detectados heurísticamente, fuera de la conciliación del código de juego |

### Catálogo único de funciones

`docs/functions.json` sustituye los antiguos `function-roadmap.tsv`, `function-coverage.txt` y `symbols.txt`.
Contiene las **1 112 funciones y 62 símbolos de datos/etiquetas** sin perder ninguna de las 252 anotaciones originales.
Cada ficha tiene dirección, nombre/alias, tipo, notas, implementación, pruebas y referencias; las funciones añaden
estado, instrucciones y tarea `roadmap`. Los enlaces vacíos significan «no documentado», no «sin implementación».
La versión 3 añade el plan de ejecución: `milestones` (hitos H0–H6 con entradas y aplazamientos), `milestone` y
`treatment` por subsistema, el campo opcional `plan` por función (excepción revisada con su justificación) y el
grafo medido por `disasm.py catalog --refresh`: `calls`, `pointer_refs`, `indirect_root` y la entrada
`executable.entry`. `catalog` sin `--refresh` falla si el grafo ya no coincide con el ejecutable, y `check` falla si
el plan tiene dependencias invertidas sin resolver o aplazamientos sin uso (véase [Funciones por hito](#funciones-por-hito)).
`phase3.car-update.*` pasó a `phase1.car-update.*`, porque es simulación y no render.
La versión 2 del catálogo asigna **las 1 112 funciones**, incluidas las 482 con estado heurístico `library`,
a **162 subsistemas específicos**. Se revisaron individualmente nombres/notas, llamadas/llamadores directos,
referencias a datos/cadenas y, en los casos seleccionados, instrucciones del ejecutable.
La clasificación no modifica nombres, estados, recuentos, métricas únicas ni anotaciones `review`.
Las fórmulas y explicaciones extensas siguen en `docs/formats.md`, enlazadas desde las fichas cuando están documentadas;
`tools/structs.txt` conserva las definiciones de estructuras, no otro inventario de funciones.

La migración conserva todos los estados publicados. El campo `review` registra las discrepancias históricas,
sin convertir sus valores antiguos en un segundo estado operativo.
También conserva las dos notas diferentes de `frame_time`, que necesitan conciliación.
Ya no queda ninguna función con `roadmap=unmapped`. El campo `classification.previous_roadmap` conserva la
asignación anterior, incluido `unmapped`; no es una segunda asignación vigente.
`subsystems` define los títulos y alcances de las categorías, y cada función tiene una ficha `classification`:

- `documented`: área respaldada por el nombre, las notas o la tarea previamente documentados.
- `inferred`: propuesta de área basada en evidencia estática; no afirma entender todo el comportamiento.
- `unresolved`: área o cola de revisión provisional; **no** se cuenta como identificación semántica resuelta.
- `evidence`: referencias individuales a símbolos, llamadas, llamadores, datos, cadenas o inspección estática.

**100 % asignado no significa 100 % identificado ni portado.** El dashboard publica los tres grados de certeza
y permite filtrarlos. Las funciones sin conexiones semánticas suficientes siguen señaladas como `unresolved`,
incluso cuando tienen una categoría provisional; por ejemplo, `0x541c0` tiene una decodificación sospechosa y
se asigna a revisión del límite de función. Las entradas internas de kernels de scanline compartidos se conservan
como candidatas, no se eliminan ni se contabilizan de nuevo como instrucciones únicas.

El estado `library` y el área `runtime.*` son ejes distintos: hay helpers gráficos/de audio clasificados
heurísticamente como `library`, y entradas del runtime que siguen en el denominador histórico del juego.
Esta revisión no cambia esa heurística ni infla la cobertura del port; las sustituciones de biblioteca,
DOS/4G y motor gráfico requieren reconciliación individual antes de cambiar el alcance medido.

Consultar el catálogo no requiere el juego ni Capstone:

```sh
python3 tools/function_catalog.py check
python3 tools/function_catalog.py plan H2
python3 tools/function_catalog.py list --status identified
python3 tools/function_catalog.py list --status partial --roadmap phase1
python3 tools/function_catalog.py list --roadmap phase1.ai-route
python3 tools/function_catalog.py list --confidence unresolved
python3 tools/function_catalog.py show 1ebb4
```

Después de modificar estados o recuentos, recalcular la cobertura **única** con el ejecutable original y actualizar
las vistas. No sumar los recuentos de instrucciones de las fichas: hay funciones que se solapan.

```sh
.venv/bin/python tools/disasm.py catalog --refresh
python3 tools/function_catalog.py sync-readme
python3 tools/port_dashboard.py
```

`catalog --refresh` conserva nombres, estados, tareas y notas; actualiza únicamente medidas y la huella de cobertura.
`disasm.py catalog` comprueba esas medidas contra el ejecutable sin escribir nada. Si cambia el conjunto de funciones,
se exige revisión explícita, no se borran ni añaden fichas silenciosamente. `coverage`, `roadmap` y `export` siguen
disponibles como salidas derivadas bajo demanda, no como archivos maestros adicionales.

La libc y DOS/4G se agrupan en `library` mediante la clasificación heurística del desensamblador; esa clasificación
también alcanza algunos helpers del motor gráfico y de audio. El port usa SDL3 y un rasterizador nativo propio,
pero esto no convierte automáticamente todas las rutinas internas del motor original en funciones portadas.
`native` se reserva para sustituciones función-a-función
explícitas (bucle principal, temporizador y tres operaciones de reproducción musical), para no confundir una
sustitución de subsistema con equivalencia de cada rutina. La equivalencia de CD cubre las pistas completas usadas
por el juego, no paquetes MSCDEX, TOC, cambios de disco ni rangos MSF arbitrarios.
Las tareas de render sí siguen registradas por separado cuando afectan al comportamiento visible del juego.

**Portado de las 84 identificadas: 28/84 cerradas; quedan 56.**

**Primer bloque (13):**
Siete helpers de animación de texturas, dos recorridos SRF y la conversión relativa POS pasan a
`ported`/`verified`; busy, play y stop de CD pasan a `native`. Además, el constructor de animaciones
`0x10508`, que ya figuraba como portado, queda verificado sin contarlo de nuevo entre las 13.
`texanim_original` contrasta 218 escenas (incluidas las siete pistas), descriptores y caras completos;
`surface_spans_original`, 1 816 listas / 7 492 spans y salidas ordenadas;
`pos_original`, 169 buffers / 673 312 bytes. El cálculo SRF reproduce IMUL de 32 bits antes de SAR 16,
no el antiguo producto de 64 bits. `texanim_oom` cubre los 12 fallos de asignación posibles, sin fugas.
El teardown nativo corrige deliberadamente las fugas originales del contenedor de listas y de animaciones sin
fotogramas; limpia su estado y es idempotente, por lo que sus dos entradas quedan `ported`, no `verified`.
`music_native` usa el decodificador real con locks/dispositivo inyectados: prueba pistas 2–8, fin, repetición,
stop y errores; no valida hardware de audio. Al silenciar, la música sigue avanzando hasta EOF, como cuando suena.
Las siete carreras de 14 400 ticks con ocho coches son finitas, deterministas y conservan estados e imágenes
del binario previo. Pasan 44 pruebas; `time_trial` mantiene dos fallos también reproducidos en un build limpio de
HEAD (BTZ sin nuevo récord y slots de eliminación), sin modificar expectativas ni baseline.

**Segundo bloque (3):** `body_corners` `0x1fdb4`, `body_pair_impulse` `0x1e638` y
`body_vs_body` `0x1ebb4` quedan `verified` para la geometría definida.
`body_pair_original` ejecuta las tres funciones completas y sus callees matemáticos sin interceptarlos:
248 casos de esquinas, 418 impulsos y 611 parejas de cajas; un contacto de vértice usa índices no inicializados
en DOS y se excluye explícitamente. Compara retornos, cuerpos y esquinas; verifica pila, CW `0x127f` y bytes
no modificables. Tolerancias absoluta `2e-14` / relativa `1e-14`; error máximo observado `7.11e-15` absoluto.
Se corrigen la limpieza del contacto, el retorno de contacto sin impulso, la evaluación de intersecciones,
el promedio y el orden de los productos de rotación/impulso; no se sustituyen por identidades trigonométricas.
El test nativo comprueba también la propagación de contactos de impacto cero por `ign_cars_collide`.
Build sin warnings y nueve pruebas relacionadas superadas. Las siete carreras de 14 400 ticks y ocho coches
son finitas y deterministas; conservan las vueltas del jugador, pero cambian trayectorias, imágenes y algunas
vueltas de rivales frente al binario anterior. No se modifica el baseline ni se afirma equivalencia de una carrera DOS.

**Bloque de render (12):** once kernels de tablas de color `0x10d34`, `0x10e04`–`0x11494`
y `camera_angles` `0x5891c` pasan a `verified`, con alcance y pruebas en el catálogo.
Las tablas se contrastan con 2 096 casos y seis salidas completas de 65 536 entradas; la cámara,
con 1 550 casos exactos ejecutando también sus callees originales. El motor usa el constructor común
de transparencia y admite la capa look-at opcional, sin cambiar las cámaras de carrera existentes.
Desviación de seguridad: la búsqueda rápida rechaza con error la lista sin terminador que produce DOS
cuando sus 256 colores tienen brillo >160, en vez de leer fuera de la tabla.
Los otros constructores quedan disponibles como kernels; no se regeneran ni sustituyen los TAB/SHD/PAN
del juego. El contexto de búsqueda nativo es del llamador, no un conjunto de globales DOS.
**La cohorte anterior de render y actualización queda en 24/46 funciones cerradas; faltan 22.**
No es el denominador del nuevo desglose exhaustivo por subsistemas. `render_scene` pasa a `partial`,
no a completado: faltan la conservación de buffers al omitir etapas y el calentamiento de mips que consume `rand`.
También siguen pendientes los monolitos de actualización de coches, el fotograma de carrera y los viewports.
Las siete carreras de 14 400 ticks y ocho coches conservan estados, logs e imágenes del binario anterior.

Las etiquetas de roadmap se desglosan en tareas de al menos tres componentes. Por ejemplo, el antiguo
`phase1.ai-route` pasa a `controller`, `tri-load`, `section-lookup`, `topology`, `opponents` y `progress`.
Hay tareas separadas para acelerador, freno, turbo, marchas, teclado/joystick; construcción/avance de texturas;
consultas y generación SRF; visibilidad/proyección del motor; caras afines/perspectiva/mipmaps; sprites, fuentes,
menús, resultados, audio, MSCDEX, persistencia y servicios del runtime.

Agrupación: `phase0.*` infraestructura/servicios; `phase1.*` carrera/controles/física/IA; `phase2.*`
mundo/superficies/efectos; `phase3.*` simulación/render; `phase4.*` frontend/HUD/flujo; `phase5.*` audio/CD;
`phase6.*` fantasmas/persistencia; `phase9.*` revisión de candidatos; `runtime.*` soporte C/DOS4G.
Las barras de subsistema del dashboard usan **funciones**, conservan las asignaciones provisionales en el
denominador y permiten incluir/excluir `library`. No suman instrucciones solapadas para simular cobertura única.

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

Funciones identificadas por subsistema (ver `docs/functions.json` y `docs/formats.md`). Las marcadas con *¿?* son
hipótesis basadas en cadenas o en el uso.

**Arranque y carga**: `_cstart_` `0x55fbc` · `game_init` `0x215d0` · `load_system_gfx` `0x21860` · `load_level`
`0x224d0` · `level_prepare` `0x2236c` · `load_plc/msh/tex` `0x230f4/0x2322c/0x23368` · `load_tri` `0x1ce30` ·
`load_srf_grid` `0x1fee0` · `load_fonts` `0x240f4` · `load_panel_gfx` `0x243e0` · crear mundo *¿?* `0x2489c` ·
crear clima `0x26154` · pantalla de carga `0x21d68` · nombres de pista `0x22f38` · pedir CD `0x219d4`.

**Carrera**: bucle principal *¿?* `0x20d60` · `cars_update_all` `0x2d008` · `car_update` `0x2ed00` · `car_input`
`0x4d5f4` · `human_input` `0x2c5b8` (teclado) · joystick *¿?* `0x4c9b4` · `ai_drive` `0x1aea0` · eliminación
("%s IS OUT") *¿?* `0x2de30` · aplastar coche `0x31858` · efectos de golpe `0x2ea44` · objetos especiales bajo el coche
`0x3383c` · consulta de suelo sobre la lista del motor `0x32dc8`.

**Física**: `body_step` `0x1e090` · `body_corners` `0x1fdb4` · `box_vs_walls` `0x1f708` · `wall_impulse` `0x1f3fc` ·
`body_vs_body` `0x1ebb4` · `body_pair_impulse` `0x1e638` · `car_vertical` `0x2e234` · `ground_query` `0x20814` ·
`collect_walls` `0x33ee8` · `car_turbo` `0x4e2d8` · `car_throttle` `0x4db90` · `car_steer` `0x500f0` · `car_gearbox`
`0x502c4` · `torque_spline` `0x53e70`.

**Objetos y efectos** (cadenas `FEL VID LI_MOVEOBJECT …`, "error en LI_MOVEOBJECT"): `collisions_pass` `0x3fd04` ·
`car_ground_contact` `0x350f0` (objetos de colisión) · contacto `0x349e8`/`0x346cc`/`0x35f38` · animaciones de textura
`0x2abdc`/`0x10508`/`0x10a00` · mensajes del HUD `0x4c7c0`/`0x4c858`/`0x4c89c` · humo de eliminación `0x4fcf0` · derrape (marcas y polvo) `0x3731c` · explosión y reaparición `0x3a788` · sprites translúcidos `0x39d1c`,
`0x39fe8`, `0x3e6ec` · *plots* `0x3a2b4`, `0x3a498` · piezas volando `0x3b744` · relámpagos `0x3bdac` · salpicaduras
`0x3d808` · clima `0x403c8` · animación POS `0x412e8` · crear efecto *¿?* `0x3fee8` · tipos de objeto `0x360f0`,
`0x3636c`, `0x36ce0`.

**Render y presentación**: `render_scene` `0x56410` · `race_setup_video` `0x492fc` · `camera_angles` `0x5891c` ·
`render_main` `0x43fe0` (resultados y puntuaciones) · `car_place_objects` `0x48a14` · HUD `0x4a320` · tiempos
`0x4bdf4` · esfera, aguja y velocidad `0x4be7c` · mensajes `0x4c7c0`/`0x4c89c` · depuración de la cámara `0x49fdc` · pausa `0x4395c` · salir o reiniciar `0x43e48` ·
captura TGA `0x515f8` · fondo del menú `0x541d4`.

**Frontend** (portado en `src/frontend/`): `main` `0x10010` · `main_step` `0x10060` · `menu_load_gfx` `0x11504`
(con `menu_init` `0x14000` en línea) · `menu_frame` `0x12bb8` · `menu_shutdown` `0x13a5c` · iris `0x15344` · capa de
texto `0x155d0` · callback de teclas de página y trucos `0x160c4` · acciones SALIR/CARRERA/VÍDEO/RESTAURAR/BORRAR
FANTASMAS `0x16f18`–`0x17054` · callback de dibujo de página `0x170ac` · pantalla de récords `0x1911c` · coche 3D `0x19508` ·
aplicar menús a la carrera `0x198bc` · BTZ por defecto `0x1a864` · `menu_load_car` `0x1aaf8` · tabla del iris
`0x2ac28`. Motor de menús "Baltazar" `0x51b20`–`0x539f8`; cajas MenuBkg `0x541d4`–`0x54864`; *Script Player* PSQ/PFM
`0x549b0`–`0x54df8`; CDP `0x552fc`/`0x55384`. Animación de texturas de objetos PLC `0xfa` `0x10390`–`0x10cf8`
(carrera; estado detallado en el catálogo). Los once kernels de color `0x10d34`, `0x10e04`–`0x11494`
están portados y verificados; las envolturas de vídeo adyacentes siguen pendientes.

**Fantasmas y persistencia**: leer el BTZ en carrera `0x2293c` · cargar fantasma `0x2adf8` · fin de carrera
(fantasma y campeonato) `0x2ccc8` · mover fantasma `0x50e94` · guardar BTZ `0x2cf60`, `0x2b430` · apagado `0x2b6b4` ·
récords de fábrica `0xbf628`.

**Sonido**: sonidos de coche (ROLL, SKID…) `0x2a4ec` · cargar muestras `.WAV/.PAT/.RW8` `0x63310` · reproducir sonido
*¿?* `0x49a8c` · canal *¿?* `0x626fc` · detección de Sound Blaster (`BLASTER`) `0x7a85c` · *Script Player* `0x549b0` ·
CD audio (MSCDEX): tocar pista `0x64241`, volumen `0x63f3e`, ocupado `0x63cf3`, TOC `0x640b3`.

**Motor Lisa 2 y runtime**: API de objetos `LI_MOVEOBJECT` `0x56d24`, `LI_PLACEOBJECT` `0x56c74`, cambiar malla
`0x56e9c` · `engine_alloc_objects` `0x569ec` · lista de objetos de una vista `0x58c1c` · memoria `mem.c` (`pool_alloc` `0x64e84`, `0x64db8`, `0x650f8`, `0x651c8`)
· sprites `geputget.c` `0x771e0` · fuentes `0x61220` · aleatorio ("FLUM SLUMP", *slump* = azar) `0x51714` · tecla
pulsada `0x55c28` · palabra tecleada `0x55cd0` · `atan2` `0x647ef` · `pow` `0x64c2a` · `fmod` `0x65782` · `rand`
`0x64774` · `memset` `0x65367` · `sprintf` `0x62775` · E/S `0x653d3…0x6577d`.

### Datos del juego que aún no se han tocado

- `GENERAL/*.TEX`: texturas de efectos (BOOM, DARKSMOK, DIVSPR, EXSMOKE, LIGHT, SMOKE).
- `GENERAL/SOUND/{BOOST,COLL,DIV,ROLL,SKID}`, `CARS/<coche>/SOUND/{00_A.WAV,01_A.WAV,ENGINE.INF}` y
  `LEVELS/<pista>/SOUND/*.PAT|WAV`: ambiente por pista.
- `MUSIC/Track02–08.ogg`: música (pistas de CD).
- `POKAL.PIC`: trofeo de los resultados (las fuentes y paneles del HUD ya se usan).
- `BALTAZAR/DATA`: ya usados por el frontend salvo `DEMOCAR.PIC`, `DEMOTRK.PIC`, `MARK.PIC` y `CONSPR.PIC` (no los
  carga `MAINDOS.EXE`).
- `LEVELS/<pista>/<pista>.{SHD,TAB}` en parte (PAN ya mezcla el HUD y POS anima los objetos).
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
| `+0xf8` | rumbo normalizado `[0, 2π)`, copia separada del rumbo acumulado del cuerpo |
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
· estado de IA (`[0xeb7c0]`, `0x7c` B por coche) · parámetros fuente IA (`0xbfb28`, 9 `double` por coche) · trucos
(`0x1fb2f0[7]`) · estado del rasterizador (`0xc4840`).

---

## Desviaciones conocidas respecto al original

- **Runtime nativo**: simulación/efectos y HUD conservan semillas privadas; menú, cámara y parrilla
  comparten una semilla compatible. El original usa una sola para todos; no se afirma igualdad del
  orden global de sorteos. C11/libm conserva sus errores, tamaños de tipos y semántica de texto,
  no los de DOS/Watcom. `fmod` conserva el signo del dividendo incluso en cero (cuatro diferencias
  de signo frente a `FPREM` emulado en los fixtures). No se reproducen handlers FPU/errno de Watcom.
  Las conversiones enteras rechazan bases fuera de 2..36 con NULL/EINVAL sin tocar el destino;
  no se reproducen división por cero, bucles ni lecturas fuera de la tabla de dígitos.
  Los rellenos preservan bytes y cursor devuelto, pero omiten lecturas especulativas y ABI de registros.

1. **IA**: controlador, cargador TRI, selector y topología portados y contrastados con bytes LE.
   El original puede consultar altura/extremos fuera de placeholders TRI de un byte: el port conserva
   la puerta seleccionada o busca una pendiente válida y emite diagnóstico una vez por pista.
   Un registro actual ausente/inválido se rechaza con diagnóstico en vez de leer heap indefinido.
   Los informes enumeran esas exclusiones; no se afirma igualdad de una carrera DOS completa.
   Un byte de agarre fuera de tabla distinto de −21/79/90/97 lee 0 en lugar de memoria del original.
   Las puertas antiguas
   `ign_track_gates` aún hacen una segunda conversión engine→PLC incorrecta para secciones; se conservan como
   deuda separada. En el bucle actual sólo se usan para diagnóstico, no para conducción/progreso/reaparición.
   La copia normalizada `body+0x28 → car+0xf8` de `cars_update_all` ya está integrada y verificada;
   el resto del dispatcher continúa parcial: véase el diagnóstico integrado.
2. **Render**: z-buffer propio en lugar del orden del pintor del motor Lisa 2 (el original no usa profundidad por
   píxel). Texturas afines como el original (`--persp` activa la corrección de perspectiva, la opción PERSP. POLY).
   Sin TAB: el motor la pasa al rasterizador (`[0xc484c]`) pero las caras texturizadas se dibujan sin sombrear (en
   `Ign_win.exe` la primitiva 0x11 no tiene campo de luz); solo la usan primitivas planas y de Gouraud que el port aún
   no dibuja. El modo de cara decide la transparencia: 17 y 21 opacos, el resto con el índice 0 transparente. La sombra
   usa `SHD[0x28*256 + destino]`, como la primitiva 0x13 de `Ign_win.exe`.
3. **Suelo**: la altura de una cara se acota a la de sus vértices (los bordes de los *spans* del SRF caen a veces fuera
   de las caras casi verticales) y una cara elegida degenerada no da suelo.
4. **Vertical**: cuando las cuatro esquinas cambian de nivel a la vez (un talud de tipo ≥ 0x50 o un hueco de la
   consulta de suelo del port, CARIB tramos 1–4), una subida mayor que `|km/h · 0,05| + 1` no se sigue con su
   velocidad: el original lanzaría el coche cientos de unidades hacia arriba.
5. **Cámara**: la de `sub_00047f94` se actualiza a 36 Hz para todos los coches. Mientras sigue al coche, la vista se
   recalcula con la posición actual del coche (el port dibuja más deprisa que las actualizaciones de la cámara). La
   media de ángulos de tramo con `car+0x36c == 3` no está portada. Sin ventana la carrera empieza directamente salvo
   con `--countdown` (entonces se dibuja el semáforo del HUD).
6. **Posiciones**: la clasificación (`ign_race_position`) es propia; el HUD (`0x4a320`, portado) solo la muestra.
7. **Dificultad**: `[0x1fedd8]` se fija con `--difficulty` (agarre trasero, índice de velocidad, modelos y bono de par
   de la IA).
8. **Efectos**: los sprites se ordenan de lejos a cerca como en el original, pero además se prueban contra el z-buffer
   del port (con la profundidad `z·2,8` que usa el original para ordenarlos frente a los triángulos). Si el punto de
   reaparición no tiene suelo en la consulta del port, se prueban los tramos siguientes (CARIB, tramos 1–4). Los
   emisores de humo se portan con las dos opciones de detalle (`[0x1fed70]`, `[0x1fed64]`) activadas.
   Las líneas de lluvia (cara `0x0d`) se dibujan como degradado de las filas SHD `0x77`→`0x7b`: no está claro si el
   original interpola el color. La destrucción por objeto `0x130` (alud de AUSTRIA, `0x3db4c`) está portada; las rocas
   avanzan con POS (para colisiones solo cuenta la traslación, como en el motor).
9. **POS**: el avance se ejecuta a 36 Hz para todas las entradas animadas (todas se crean con las opciones de detalle
   activadas, así que todas tienen el flag de `LI_PLACEOBJECT`). La rotación `Z(Y(X(v)))` del render es aproximada; las
   consultas de suelo y colisión usan, como el motor, solo la traslación.
10. **Sonido**: mezclador propio en SDL3 (mono, 44,1 kHz) en lugar del driver GUS/Sound Blaster; volumen 0..`0x10000`
   y frecuencia en Hz como en `0x625f4`. Sin panorama estéreo y sin los sonidos de objetos móviles.
   La música usa OGG nativo; no emula el transporte MSCDEX. La liberación de animaciones de textura corrige fugas
   del original y revierte todas las asignaciones si falta memoria.
11. **Objetos móviles y especiales**: los objetos especiales (`0x3383c`, relámpago `0x32dc8`) se evalúan justo antes del
   tick del coche y no dentro de `car_update`; el aplastamiento solo reduce la altura en el render (falta x·1,2 / z·1,6
   en `raster.c`); la inversión temporal de dirección de la IA sí está portada. Los mensajes del HUD se encolan y envejecen pero no se
   dibujan (`hud.c`), usan "PLAYER/CPU n" en vez de los nombres y no hay "TRACK RECORD"; en `0x32dc8` más de 8 caras se
   descartan (el original desborda) y un objeto especial devuelve sus puntos con la posición del objeto; en
   `0x346cc` 1 ulp float en empates de `jB0` (ver la verificación).
   En `0x1ebb4`, si todos los cruces coinciden, DOS usa índices del par más alejado sin inicializar; el port
   conserva de forma segura el primer punto (comportamiento nativo previo), fuera de la equivalencia verificada.
12. **Persistencia**: `IGN_DOS.BTZ` y `GHOSTS/` en `SDL_GetPrefPath("UDS", "Ignition")` (o `IGN_SAVE_DIR`), nunca en la
   carpeta del juego, que solo se lee. Jugador = participante 0, récords
   guardados ya al terminar el jugador, nombres de 3 letras, sin récords de coches eliminados ni vueltas vacías, y
   con lógica nativa de puntos y desbloqueos del campeonato integrada, pendiente de validación del ciclo completo.
   "LAP RECORD"/"TRACK RECORD" salen por la salida
   estándar y también como mensajes del HUD. Véase [BTZ](docs/formats.md#configuración-y-récords-ign_dosbtz-portado-en-savec-toolsbtzpy).
13. **HUD**: `main.c` usa una sola ventana (el HUD admite las dos del original); los contadores por fotograma avanzan
   una vez por cada 1/36 s del reloj de carrera; el primer fotograma pone aguja y palanca en su objetivo; el temblor
   usa un generador propio; los mensajes se detectan comparando el estado entre fotogramas y los pilotos se nombran por
   el código de su modelo; el frontend usa las tablas originales de modo/vista para los paneles y el CLI headless
   conserva la selección por ancho; cada sprite escalado ocupa su propia
   página de textura; no se emiten el sonido del lugar al terminar (`0x4bc54`) ni el bucle de «fuegos» sin dibujo de
   `0x4bca8`.
14. **Frontend**: el menú corre a 320×200 como el DOS y comparte la carpeta de usuario de la persistencia: lee su
   `IGN_DOS.BTZ` (o, si no hay, el de la carpeta del juego) y escribe solo allí; *RESET GHOST CAR* borra los fantasmas de
   esa carpeta. El CD del menú es la música OGG (`MUSIC/Track02–08.ogg`, un disco de 8 pistas con la 1 de datos).
   Hay un backend SDL de gamepad/joystick y calibración de ejes; falta validar el recorrido completo de las páginas
   de calibración con hardware real. No se reproduce la espera "NO SOUNDCARD FOUND". Los coches 3D del
   carrusel usan el rasterizador del port con una cámara que reproduce posición y objetivo pero no la focal exacta del
   motor Lisa 2. El reloj del menú es `SDL_GetTicks` (el original iba un 1,15 % rápido). La carrera lee las teclas,
   el tipo de control y la aceleración automática del menú; el avance entre pistas del campeonato está conectado.
   Faltan la validación integral de esos recorridos y la integración completa de nombres y opciones gráficas.
   Los resultados y diálogos usan composición nativa, no las tiras 320×480 ni todos los sprites/animaciones DOS;
   la carga ya compone las cajas, esquinas, fuentes y dos franjas originales a 320×200 y escala el resultado
   completo a la ventana; presenta hitos discretos 0/33/66/100. Los participantes séptimo/octavo reciben cero puntos.
   Los avisos de desconexión tienen API y pruebas, pero no transporte de red.
   Alcance por función y evidencia: [Frontend, HUD y flujo](docs/formats.md#frontend-hud-y-flujo-lote-phase4-2026-10-16).

---

## Roadmap

Prioridad: **P1** = necesario para un juego completo de un jugador · **P2** = fidelidad y contenido · **P3** = extras.
Cada tarea indica las direcciones de partida. Las **fases 0–9 son áreas de trabajo**, no una secuencia obligatoria.
El orden de ejecución lo fijan los hitos siguientes y sus dependencias; la prioridad no sustituye a esas dependencias.

### Hitos de ejecución y criterios de cierre

Objetivo: cerrar primero una carrera fiable y el ciclo completo de un jugador en macOS; después completar la
fidelidad visual y ampliar plataformas y modos. No se prioriza por número de funciones o instrucciones portadas.

| Hito | Dependencias | Trabajo y criterio de cierre |
|---|---|---|
| **H0 — Estado y referencias reproducibles (P1)** | Ninguna | Reconciliar código, catálogo y tareas de las fases, conservando el alcance de cada prueba. Ejecutar y registrar las pruebas relevantes con revisión, datos, binario y argumentos identificados. Separar fallos de ejecución, diferencias históricas y regresiones nuevas; no sobrescribir la referencia histórica para hacerla pasar. |
| **H1 — Referencia integrada DOS (P1)** | H0 | Conseguir primero un tramo corto reproducible, no una carrera entera: confirmar ejecución en carrera, mapeo LE, coche, estado inicial, entradas y frontera/cadencia de tick. Conservar captura y conversión, comparar campos comunes y obtener un informe que localice la primera divergencia. Véase [captura DOS](docs/dosbox-trace.md). |
| **H2 — Cadena de simulación fiel (P1)** | H0; H1 para acreditar equivalencia integrada | Completar por bloques controles → dispatcher → física → consultas de pista. Mantener oráculos de fragmentos y replay; para cada escenario integrado, declarar campos y tolerancias antes de comparar, sin divergencias inexplicadas dentro del alcance acordado. Las desviaciones deliberadas se documentan y quedan fuera de la afirmación de equivalencia. |
| **H3 — Carreras completas en siete pistas (P1)** | H2; incorporar creación/mapeo de objetos de Fase 2 si bloquea consultas de pista | Ejecutar escenarios deterministas de tres vueltas en las siete pistas, primero sin rivales y después con siete. Registrar llegada o eliminación de cada participante dentro de un límite de ticks fijado por escenario, sin NaN/Inf ni atascos sin diagnosticar. Cubrir bifurcaciones, barranco de USA, muros, peligros y destrucción/reaparición con casos específicos. Un smoke de 200 s con avance de vueltas no cierra este hito. |
| **H4 — Ciclo completo de un jugador (P1)** | H3 para cierre; pruebas de UI/configuración pueden avanzar tras H0 | Validar menú → carrera → pausa/continuar/reinicio/salida → resultados → siguiente pista → fin del campeonato → menú. Cubrir puntos, empates, reintentos, desbloqueos, récords y fantasma; comprobar persistencia tras relanzar usando una carpeta de usuario aislada. Verificar las opciones del menú realmente aplicadas; completar las restantes o declarar su limitación. |
| **H5 — Fidelidad visual (P2)** | H3 para comparación de escenas estables; creación y transformaciones de objetos antes de comparar escenas que las usan | Completar correspondencia de objetos y rotaciones POS; después orden de dibujo, recorte y primitivas Lisa 2. Resolver TAB/PAN/SHD en sus consumidores, reutilizando los kernels ya verificados. Comparar escenas con cámara, tick, resolución y configuración fijos; igualdad de píxel donde se afirme equivalencia exacta y desviaciones explícitas en el resto. |
| **H6 — Entrega y ampliaciones (P1/P2/P3)** | H4 para entrega funcional macOS; H5 para afirmar fidelidad visual; dependencias propias para cada ampliación | Bundle macOS con selección de datos, probado fuera de la ruta de desarrollo y sin modificar la copia del juego. Windows requiere build y pruebas en CI; pantalla partida requiere dos entradas/cámaras/viewports; iOS requiere importación de datos y controles táctiles; red requiere decidir protocolo y sincronización. No son un único bloqueo secuencial. |

### Funciones por hito

Cada hito tiene en `docs/functions.json` sus puntos de entrada, los subsistemas que le pertenecen y los aplazamientos
justificados. `tools/function_catalog.py` asigna cada función al **primer hito que la necesita** siguiendo el grafo
de llamadas medido en el ejecutable, y la ordena **de las hojas a las llamadoras**, en orden de dependencias. Cada
función tiene un **tratamiento**:
`port` (porte fiel), `equivalent` (mismo resultado observable sin porte línea a línea; motor Lisa 2, rasterizador
y cargadores de formatos), `native` (sustituida por SDL3/libc: DOS/4G, VESA, drivers de audio, MSCDEX) o
`not-needed`. Una función está hecha cuando su estado publicado satisface su tratamiento; el plan nunca promueve
estados. Tabla generada por `python3 tools/function_catalog.py sync-readme`:

<!-- milestones:begin -->
| Hito | Requeridas (primera vez) | Hechas | Pendientes: port / equiv. / nativo | Instr. pendientes (suma por función) | Siguientes en orden de dependencias |
|---|---|---|---|---|---|
| **H0** Estado y referencias reproducibles | 0 (0) | 0 | 0: 0 / 0 / 0 | 0 | — |
| **H1** Referencia integrada DOS | 0 (0) | 0 | 0: 0 / 0 / 0 | 0 | — |
| **H2** Cadena de simulación fiel | 90 (90) | 56 | 34: 14 / 5 / 15 | 8 175 | `sub_00055f9c`, `car_throttle`, `car_turbo` |
| **H3** Carreras completas en siete pistas | 104 (56) | 37 | 19: 11 / 7 / 1 | 3 482 | `load_msh`, `load_level`, `sub_00064db8` |
| **H4** Ciclo completo de un jugador | 346 (205) | 98 | 107: 49 / 0 / 58 | 12 436 | `gfx_create_sprite`, `sub_00060dbc`, `sub_000558a0` |
| **H5** Fidelidad visual | 259 (162) | 13 | 149: 6 / 141 / 2 | 151 473 | `alloc_ea3c4`, `sub_000825b8`, `sub_000739d3` |
| **H6** Entrega y ampliaciones | 8 (1) | 0 | 1: 1 / 0 / 0 | 86 | `sub_000515f8` |
<!-- milestones:end -->

```sh
python3 tools/function_catalog.py plan H2        # pendientes de H2, de las hojas a las llamadoras
python3 tools/function_catalog.py plan H2 --all  # incluye las ya satisfechas
```

H0 y H1 son trabajo de verificación, no de porte. Hallazgos de la revisión manual, registrados como `plan` o
aplazamiento con su justificación en el catálogo:

- **La clasificación es parte de la simulación:** `0x4c2d8` escribe `car+0x39c` (posición), que leen `car_throttle`
  y `car_update`. Pertenece a H2; el port usa hoy una clasificación propia (desviación 6).
- **`rand` es compartido:** IA, efectos de turbo, `0x4f874` en `car_input`, derrapes, clima, HUD y el calentamiento
  de mipmaps de `render_scene` (`0x58864`) consumen la misma secuencia. Comparar la IA de H2 contra DOS exige
  reproducir o registrar el estado de `rand` por tick, aunque esos consumidores pertenezcan a otros hitos.
- **`LI_MOVEOBJECT` de objetos de colisión** (`0x56d24` desde `car_ground_contact`) reenlaza la rejilla que recorren
  las consultas de suelo: entra en H2 como `equivalent`. La creación del mundo y el pool de objetos del motor
  (`0x564d8`, `0x569ec`, `0x56b28`, `0x56c74`, `0x56e9c`, `0x56f8c`) entran en H3 por el mismo motivo.
- Sonido, HUD, vídeo, fantasma y carga de recursos se aplazan en H2/H3 porque no escriben estado de carrera ni
  consumen `rand`; los supuestos sin confirmar (objetos gráficos del coche en `0x32dc8`) quedan marcados con `¿?`.
- **285 funciones no tienen ninguna referencia estática** en `MAINDOS.EXE`: ni llamadas directas o relativas
  (incluidos los bytes sin decodificar), ni operandos, ni fixups desde datos o tablas. Se tratan como `not-needed`;
  entre ellas hay funciones ya portadas, como `menu_init`, `menu_setup` y los kernels de tablas de color
  `0x10d34`–`0x11410`. El candidato `0x541c0` no es una función, sino datos que usa `menubox_init`.
- Las demás funciones vivas sin hito son infraestructura (runtime, drivers DOS, VESA, audio y emulador x87),
  cubierta por la sustitución nativa de sus llamadoras.

El grafo no resuelve llamadas por registro: una función alcanzable solo por esa vía figuraría sin hito. Las tablas
de punteros sí se tratan como raíces (`indirect_root`), de forma conservadora.

**Siguiente objetivo operativo:** cerrar H0 y obtener la captura corta de H1; usar su primera divergencia para elegir
el siguiente bloque de H2, no empezar un porte monolítico del rasterizador ni de `car_update`.
Si la captura DOS queda bloqueada, registrar el intento y el obstáculo en `docs/dosbox-trace.md` y continuar con
oráculos LE y pruebas de integración del port. Eso permite avanzar H2, pero no cerrar su equivalencia integrada.

En H2, los puntos de entrada son `human_input` (`0x2c5b8`), `car_input` (`0x4d5f4`),
`cars_update_all` (`0x2d008`) y `car_update` (`0x2ed00`). Dirección, acelerador, freno, marchas y turbo ya tienen
fragmentos verificados: se debe completar su composición y sus productores de estado, no reimplementarlos desde cero.
La IA y su trazado también tienen oráculos propios; una divergencia integrada no se atribuye al controlador sin
localizar antes el primer estado o llamada que difiere.

**Estado de las tareas:** `[ ]` pendiente, `[~]` parcial o implementación integrada con cierre pendiente,
`[x]` alcance descrito completado. Una marca no acredita fidelidad de todo el sistema. Los estados por función siguen
siendo los de `docs/functions.json`; esta revisión del plan no los promueve. La revisión documental de 2026-10-16
confirma conexiones estáticas de código, no una nueva ejecución de carreras ni una nueva validación DOS.

**Validación transversal:** en cada cambio, ejecutar las pruebas focalizadas, conservar los oráculos existentes y
escalar a las siete pistas cuando afecte a la simulación. El replay mide determinismo del port; los fragmentos LE
miden su alcance aislado; las capturas DOS miden integración. No son pruebas intercambiables.

### Fase 0 — Infraestructura de ingeniería inversa

- [x] **P1** Inicializar git y subir solo código, documentación y herramientas (los datos ya están excluidos).
- [~] **P1** Separar la libc de Watcom y DOS/4G del código del juego: no hay base de firmas FLIRT para Watcom 10.x, así
      que `disasm.py` la clasifica heurísticamente por grafo de llamadas y datos de runtime (estado `library`;
      recuentos actuales en "Estado en una línea"). La separación completa de
      `0x60000–0x86000` requiere mejores firmas o verificación manual; no bloquea el ciclo de un jugador.
- [x] **P1** Cubrir el 17 % de bytes restante: `disasm.py sweep` (barrido de huecos con prólogos Watcom, decodificación
      limpia, validación de relocaciones y rechazo de solapes) subió la cobertura del 83,0 % al **98,5 %**
      (277 candidatos de huecos aceptados; 303 inicios validados persistidos, incluidos los llamados).
- [x] **P2** Exportar el catálogo único como script de Ghidra o IDA (nombres, comentarios y tipos de estructura):
      `disasm.py export ghidra|ida -o FICHERO` (estructuras en `tools/structs.txt`). No probado en Ghidra/IDA reales.
- [x] **P2** Centralizar estados por función en `docs/functions.json`: `unknown`, `identified`, `understood`, `port-pending`,
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
- [~] **P1** Referencia integrada H1 mediante trazas del original en DOSBox-X: hecho `tools/trace_compare.py`; DOSBox-X 2026.10.01 está
      instalado; se confirmó la entrada MZ de `MAINDOS.EXE` y que DOS lo mantiene cargado, pero tras reanudar solo se
      observó el bucle de espera de BIOS. Aún falta llegar a carrera, validar el mapeo LE, el muestreo por tick y la
      comparación ([`docs/dosbox-trace.md`](docs/dosbox-trace.md)).

### Fase 1 — Carrera completa de un jugador (jugabilidad)

- [x] **P1** Portar **IA y trazado originales**: `ai_drive` `0x1aea0`, `load_tri` `0x1ce30`,
      `ai_find_section` `0x1decc` y `build_sections` `0x28f08`. Carriles/RNG, pendiente Y,
      aplastamiento, ganancia de dirección de los modelos 5/8, esquiva, frenada, turbo, ramas y geometría integrados.
      `track_original`: 1 365 secciones, 1 565 registros normales + 70 placeholders y 5 516 consultas.
      `ai_drive_original`: 4 323 casos definidos de controles/estado contra bytes LE, CW `0x127f`.
      Las lecturas indefinidas del original se enumeran y tienen sustitución nativa explícita;
      no acredita equivalencia de una carrera completa.
- [x] **P1** Portar `angle_normalize` (`0x51938`) y completar su integración en `cars_update_all`
      (`0x2d9c7..0x2d9e8`): copiar el rumbo normalizado a un estado separado del cuerpo antes de los controles.
      `drive_heading` preserva el cuerpo acumulado; 291 casos bit exactos del helper/fragmento original.
      Diagnóstico repetido: 48 945 decisiones definidas sin discrepancias y permanencias de ≥5 s de 24 a 0.
      No acredita todos los productores del dispatcher ni equivalencia de una carrera DOS completa.
- [x] **P1** Arreglar el atasco en la bifurcación de CANADA (tramo −34): la IA usa registros de la rama B en tramos negativos.
- [~] **P1** Completar por bloques `car_update` (`0x2ed00`) y `cars_update_all` (`0x2d008`):
      controles y física tienen kernels verificados, pero faltan productores de estado, actualización completa de
      superficies y composición del dispatcher. Cierre según H2, manteniendo regresiones de las siete pistas.
- [ ] **P1** USA tramos 48–50: diagnosticar salto del barranco y reaparición con referencia original
      (punto de partida adicional `0x00435910` en `Ign_win.exe`). Se ha observado IA que cae a ~70 km/h y queda
      atascada; reproducir el caso antes de atribuirlo a física, suelo o reaparición.
- [ ] **P1** Integración IA/física contra los muros: controlador y trazado originales ya portados y cotejados.
      El smoke de 200 s con ocho coches avanzó vueltas en las siete pistas, pero se observaron rivales lentos
      o atascados (por ejemplo ICELAND). Reproducir y localizar la primera divergencia de física/dispatcher;
      las ventanas originales de marcha atrás no garantizan desatascarse. Cierre de carreras completas según H3.
- [x] **P1** Planificador rotatorio de IA de `cars_update_all`: fase de controles a 36 Hz, pero decisión nueva
      por coche a **36/N Hz**, con N coches totales, incluidos humanos/inactivos. Resto de turnos: replay de
      pedales, dirección y solicitud de turbo; caja automática antes de controles, física a 72 Hz.
      Contador de atasco incrementado por N, sin espera artificial. Verificación contra fragmentos LE:
      **17 640 casos sin diferencias** y **32 comprobaciones de caché**. Cadencia en el ejecutable con N=1..8,
      replay independiente 21/21 y conducción humana recta/turbo exacta frente al binario previo (14/14).
      Smoke de 200 s con siete rivales en siete pistas, repetido con estados/trazas/imágenes idénticos y sin
      valores no finitos. No verifica la IA completa ni el dispatcher: `car+0x350` sigue sin modelar.
      La observación histórica «la geometría del error de rumbo aún es aproximada» debe leerse junto al porte
      posterior del trazado en esta fase.
      La referencia histórica no se regenera: la comparación registrada entonces fue 0/23,
      con 22 diferencias de estado y 23 de imagen, sin fallos de ejecución; no es una medición nueva.
- [x] **P1** Orden de carrera, posiciones, fin de carrera y número de vueltas por modo (`race.c`; 3 vueltas, modo 3 sin límite; clasificación propia: ver desviaciones).
      Cruce/fases/registro de tiempos verificados por fragmentos: 3 024 casos / 4 112 pasos.
      Pendientes geometría de sondas, productor del gate, récords guardados y avisos.
- [x] **P1** Evento de eliminación `0x2de30` (modo 3): el último coche de cada vuelta queda eliminado. Portada la lógica
      (`--elimination`) y el humo del último coche (`0x4fcf0`); "%s IS OUT" se encola (`0x4c7c0`) pero falta dibujarlo
      (`0x4c89c`); faltan explosión (`0x3fee8`), sonido y "WARNING, YOU'RE LAST!" (`0x4a320`).
- [x] **P1** Solape entre coches (`cars_update_all` `0x2d3c2`, |dx|,|dz| < 25): el de abajo queda aplastado
      (`0x31858`: malla ×1,2/×0,2/×1,6, COLL 6, dirección del humano invertida, humo; a los 200 ticks se recupera si no
      hay coches a < 30, salto de 6 y COLL 7) y efectos de golpe `0x2ea44` (impacto > 15). Pendientes en otras áreas:
      ensanchamiento en el render.
- [x] **P1** Kernels de colisión entre coches `0x1fdb4`/`0x1e638`/`0x1ebb4`: esquinas actuales/escaladas,
      impulso y contacto geométrico contrastados en 1 277 casos definidos con el original; un contacto degenerado
      indefinido queda separado. Verificación de kernels, no del dispatcher completo.
- [x] **P1** Destrucción y reaparición (`car_vertical` `0x2e397`, `car_update` `0x2fd63`, `sub_0003a788`): al aterrizar
      en superficies prohibidas, sin suelo bajo las 4 ruedas o 6 ticks con las 4 sobre tipos > 0x28, el coche explota
      (o se hunde) y 80 fotogramas después reaparece dos tramos más adelante, con explosión, piezas y humo
      (`0x3a788`), salpicadura (`0x3c208`) y destellos al volver (`0x3e6ec`). La colocación de `0x41430` deja el
      coche quieto, en el aire 250 unidades sobre la media de las caras de carretera, en primera, y le asigna el tramo
      del punto de reaparición (si vuelve a explotar, reaparece dos tramos más allá); su suelo sale de `0x32dc8` sobre la
      lista del motor. Disparador por objetos `0x12c` portado (peligros móviles; en ICELAND a < 30 del origen).
- [~] **P1** Entrada original: teclado `0x2c5b8` decodificado; dirección digital cotejada contra bytes LE
      (24 casos: signo, prioridad de derecha e inversión al aplastarse; ver `docs/formats.md`);
      pedales/arbitraje y selección de marchas verificados, manual 0–2 (M, Q/E). Backend nativo SDL integrado
      (`src/platform/input.c`, `tests/input_backend.c`): teclas BTZ, flancos, foco y gamepad/joystick.
      No emula IRQ/BIOS ni el puerto DOS; faltan acreditar composición/caché de `car_input`, callees intermedios
      y equivalencia de la ruta analógica original `0x4c9b4`. El freno `0x4e1bc` está verificado para coche no terminado.
- [x] **P2** Caja automática `0x502c4` sin audio verificada (curvas compartidas, ángulo, empate y contador/retardo);
      cambios manuales 0–2 y reset al reaparecer verificados. Inicialización completa y secuencia de callers aún fuera de prueba.
- [x] **P2** Dificultad `0xc5470` (agarre trasero ×1/1,15/1,3/1,3, `--difficulty N`) y tipo de control
      `0x1fefd0[coche]+4` = 1 (IA: perfil 11, bono de par, modelos de `0xc5520`). Pendiente: tipos 2 y 7.
- [x] **P2** Objetos especiales `0x12c–0x131` (`0x3383c` y `car_update` `0x2ef9d`/`0x30dbb`): explosión 0x12c, vuelta de
      campana 0x12d y géiser 0x131, empujón 0x12e, aplastamiento 0x12f, alud 0x130; con relámpago, `0x32dc8` (0x12d).
      Las superficies `0x6e–0x83` (muros en modo salto) están en los muros.
- [ ] **P2** Trucos restantes: identificar el efecto de SURMULE (`0x1fb2f4`), SLASKTRATT (`0x1fb2f8`),
      FILMJOLK (`0x1fb2fc`, `0x420cc`), SKUNK (`0x1fb300`, muy usado en efectos y `0x48a14`), SVINPOLE (`0x1fb304`) y
      STRINGS (`0x1fb308`).

### Fase 2 — Mundo vivo: objetos, colisiones y efectos

- [~] **P1** Animaciones **POS** (`pos_make_relative` `0x41244`, `pos_apply_frame` `0x412e8`): parser, deltas cíclicos,
      transformaciones y avance a 36 Hz integrados; la traslación cuenta en la rejilla del motor y en las colisiones
      (sin spans SRF, como el original). Conversión completa `0x41244` contrastada byte a byte: 169 casos, incluidas
      las siete pistas y registros compartidos. Pendiente el orden exacto de rotación del render.
- [x] **P1** Colisiones con objetos móviles: objetos de colisión (clases 0x96–0xc7, `0x24f22`) y `car_ground_contact`
      `0x350f0` (contacto `0x349e8`/`0x346cc`, vuelo y rebote `0x35f38`, balizas 0x9a); `collisions_pass` `0x3fd04`
      completo (animaciones de textura, mensajes `0x4c858`, humo de eliminación `0x4fcf0`); `0x36ce0` aporta la fase del
      géiser y `0x360f0` no interviene. Verificado contra bytes LE (ver "Contacto y objetos móviles").
- [x] **P1** Lista de objetos del motor `0x58c1c` (rejilla de 200×200 celdas, cadenas, capas) y consulta de suelo sobre
      ella `0x32dc8`; las clases (`0x247b0` deja `tipo & 0xfff` antes de crear el mundo) gobiernan todos los tipos.
- [x] **P1** Derrape `0x3731c`: marcas (anillo de 700, fila SHD por superficie `0xc34e0`) y polvo (efecto 1,
      `sub_00039d1c`, sprite de `SMOKE.TEX` mezclado con la SHD) y sonido de `GENERAL/SOUND/SKID`.
- [x] **P2** Objeto especial PLC clase `0xfa` (`0x2abdc`): animaciones de textura por mosaicos de 64×64 (`0x10508`,
      `0x10a00`, ritmo `0xc4598` por pista); el mar de CARIB, cascadas y luces. Constructor, helpers y avance
      contrastados con 218 escenas originales; teardown sin fugas y rollback de los 12 fallos de asignación.
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

- [ ] **P2** Rasterizador del motor Lisa 2 (`0x54000–0x60000`, ~12 900 instrucciones): orden de dibujo, recorte,
      relleno y z (o algoritmo del pintor).
- [ ] **P2** Sombreado **TAB**: cómo se elige el nivel por cara (bits bajos de `flags`: `0x11/0x13/0x15`; byte alto
      ≥ 64 = sin sombra) y luz por distancia o normal.
- [ ] **P2** Función de **PAN** (tabla de 64 KB `PAN[a*256+b]`; filas 16–159 a cero, así que no es la mezcla de las
      caras 18/19/22). **SHD**: fila 0x28 = sombra de coche (hecho); resto de filas por confirmar en el DOS.
- [x] **P1** Cámara de persecución original (`sub_00047f94`, `sub_000420cc`, focales de `race_setup_video`).
- [x] **P2** Cámara trasera (`car+0x5a4`, campos `+0x90/+0xa0/+0x70`) y cámaras fijas tras la meta (`0xc464c`).
- [x] **P1** Cuenta atrás y reloj de salida (`sub_0002b850`) con sus pitidos y el semáforo 3-2-1-GO del HUD.
- [x] **P1** Conversión del reloj por coche y transición normal verificadas contra instrucciones originales;
      18 450 casos / 74 178 pasos, HUD/turbo/fantasma conectados; timer DOS y pausa pendientes.
- [x] **P2** Once kernels de tablas de color `0x10d34`, `0x10e04`–`0x11494`, verificados contra salidas completas.
- [x] **P2** `camera_angles` `0x5891c`: look-at opcional, cuadrantes, ejes y puntos coincidentes, conectado al motor.
- [ ] **P2** `render_scene` `0x56410`: capas según los flags de configuración `[0x24c05c]+0x38..0x54`;
      quedan la reutilización de buffers al omitir etapas y el calentamiento de mips con sus llamadas a `rand`.
- [x] **P2** Resoluciones 320×200 / 640×480 / 800×600 (`--res`), mostradas a 4:3.
- [ ] **P2** Viewports por jugador (`race_setup_video` `0x492fc`): la geometría de `0x49724` (uno o dos jugadores) ya la
      usa el HUD; faltan las cámaras y el render por ventana.
- [x] **P2** Sprites (`geputget.c`, `0x771e0`) y señales (`*_SIGNS.PIC`): blit con la tabla PAN, sprites con matriz
      (`0x82950`, triángulos texturizados `0x78304`/`0x78ac0`) y triángulo plano `0x729f3`, idénticos a los bytes LE
      en `tests/hud_original.py` (6 009 triángulos, 624 sprites).
- [ ] **P3** Escalado moderno (pantalla completa, relación de aspecto, filtrado opcional).

### Fase 4 — HUD, menús y flujo de juego

- [x] **P1** Fuentes **LFT/FNT** (`load_fonts` `0x240f4`, `0x61220`): ranuras, parser LFT con anchos con signo,
      ancho y dibujo alineado (fijo/proporcional) y FNT (`0x53d60`); pruebas en `tests/hud_assets.c`.
- [x] **P1** HUD en carrera `0x4a320`: coordenadas y estados de las tres resoluciones, aguja y medidores (turbo
      incluido), palanca de cambios, señales y WRONG WAY, semáforo, barra de eliminación, lugar al terminar, mensajes
      multilingües (IS OUT, LAP/TRACK RECORD, WARNING, YOU'RE LAST!) y ventanas de dos jugadores.
- [~] **P1** Pantallas de resultados y puntuación (`render_main` `0x43fe0`: TRACK RESULTS, TRACK SCORE, TOTAL SCORE)
      y trofeo `POKAL.PIC`: implementación nativa en `src/frontend/race_ui.c`, conectada en `main.c`.
      Pruebas nativas de estados/puntos/empates, seis idiomas y tres resoluciones; 72 casos LE del umbral de
      calificación. Pendientes comparación visual DOS y ciclo completo H4; no se afirma igualdad de layout ni flujo.
- [~] **P1** Pausa, continuar, salir y reiniciar (`0x4395c`, `0x43e48`): estados y acciones nativos integrados
      en `race_ui.c`/`main.c`. Confirmación/cancelación y límites de reintentos cubiertos por `race_ui`;
      quedan validación SDL integral, flancos al reanudar y recorrido completo del campeonato H4.
- [x] **P1** Menús: `menu_load_gfx` `0x11504`, `menu_frame` `0x12bb8`, callbacks `0x160c4`/`0x170ac`, motor de menús
      `0x51b20`–`0x539f8` con las 25 páginas × 6 idiomas, intro CDP, fondo PSQ/PFM al píxel, carrusel 3D `0x19508`,
      récords, créditos, cajas `MENUBKG.DAT` y `IGN_DOS.BTZ`. Pruebas `menu_flow` y `menu_background`.
- [~] **P1** Configuración menú → carrera: pista, coche, corredores, dificultad, cambio, contrarreloj,
      persecución y música ya pasan; también se leen teclas BTZ, tipo de control y aceleración automática.
      Completar nombres, opciones gráficas y obstáculos; probar cada opción aplicada en H4.
- [~] **P1** Campeonato y flujo entre pistas: `race_ui.c` implementa puntos, clasificación y acciones;
      `main.c` conecta avance, reintentos y guardado/desbloqueos. Pendiente validar un campeonato completo,
      resultados empatados, eliminación/no clasificación y persistencia tras relanzar. Contrarreloj tiene pruebas propias;
      no se da por acreditado el resto de modos por compartir este flujo.
- [x] **P2** Pantallas de carga (`0x21d68`, `0x22238`): composición original y progreso en `race_ui.c`,
      conectados al frontend. Contraste LE de 42 combinaciones de idioma/pista y 4 284 fotogramas
      (porcentajes 0..100 y vuelta a cero); el host escala el resultado completo, sin emular VESA.
      Véase [Carga original](docs/formats.md#carga-original-0x21d68-y-0x22238).
      La equivalencia visual de resultados/pausa sigue pendiente y no se acredita con estas pruebas.
- [x] **P2** Configuración y récords en **BTZ** (`0x2b430`, `0x2cf60`, `IGN_DOS.BTZ`): formato completo, imagen por
      defecto del menú verificada contra el original, récords, avance del campeonato y persistencia (`save.c`). El HUD lee
      de la misma imagen el idioma y los récords de carrera para «TRACK RECORD».
- [x] **P3** Vídeos **CDP** (`IGN1.CDP`, `IGN2.CDP`, `IGN3_*.CDP`) y *Script Player* `0x549b0`.
- [ ] **P3** Capturas TGA `0x515f8`. La superposición de depuración `0x49fdc` ya está portada (cámara libre, F1).

### Fase 5 — Sonido y música

- [x] **P1** Sustituir el driver (Sound Blaster/GUS, `0x7a85c` y `0x60000+`) por SDL3 audio con un mezclador propio.
- [x] **P1** Cargador de muestras `0x63310` (`.WAV`, `.PAT` de Gravis) y formato **XI**; bancos por prefijo `NN_`.
- [x] **P1** Motor de cada coche (`0x50824`): **ENGINE.INF** + `00_A.WAV`/`01_A.WAV` con tono según las revoluciones
      (`car+0x288`, `car_throttle` `0x4e000`).
- [x] **P1** Efectos: BOOST, COLL, DIV, ROLL, SKID (`0x2a4ec`); reproducción posicional `0x49a8c` y canales `0x626fc`.
- [x] **P2** Ambiente por pista `LEVELS/<pista>/SOUND`: emisores PLC `0x190+n` (`0x2489c`, `0x360f0`), lluvia,
      viento y truenos.
- [x] **P2** Música `MUSIC/Track02–08.ogg` (antes pistas de CD, MSCDEX `0x63cf3…0x64710`): pista por nivel `0xc3fd8`,
      repetición `0x212f8`, menú (pista 7), podio (pista 8) y cambio de pista. Busy/play/stop son equivalentes nativos
      probados, no emulación de todas las rutinas CD; EOF y duración se conservan también con volumen cero.

### Fase 6 — Fantasmas y persistencia

- [x] **P2** Formato **GST**: lectura (`0x2adf8`), grabación por tick (`0x51204`) y guardado con relleno (`0x2ccc8`).
- [x] **P2** Reproducir el fantasma (`--ghost`): una muestra por tick, sin choques, con el modelo de la cabecera.
- [x] **P2** Modo contrarreloj (`[0x1feefc] == 2`): grabar al terminar si se bate `[0x1fee04]` y cargar al empezar
      (`--time-trial`, `tests/time_trial.py`).
- [ ] **P1** Complementar H1/H2 con una contrarreloj y un fantasma reales de DOS: comparar trayectoria y tiempos
      con entradas y estado inicial conocidos. `--ghost` reproduce muestras, no vuelve a simular los controles;
      por sí solo no verifica física ni IA. La IA requiere escenarios de referencia con rivales.
- [x] **P2** Récords de vuelta ("LAP RECORD") y su persistencia: tablas por pista de `render_main` `0x45938`, mejor
      vuelta inicial desde el récord guardado (`0x2a442`); los avisos aún no se dibujan en el HUD.

### Fase 7 — Multijugador

- [ ] **P2** Pantalla partida para 2 jugadores (viewports en `0x492fc`; `0x26154` avisa "does not support more than 2
      ca…").
- [ ] **P3** Red: localizar el código de red del original (IPX/módem/serie) o diseñar uno nuevo con UDP; nombres sobre
      los coches (tipo de control 2, `0x48a14`).

### Fase 8 — Plataformas y distribución

- [ ] **P1** Bundle `.app` de macOS con selector de la carpeta de datos (sin rutas fijas).
- [ ] **P2** Windows: CMake + SDL3 (MSVC/MinGW), CI con GitHub Actions para macOS y Windows.
- [ ] **P2** iOS: SDL3 en iOS, controles táctiles e importación de los datos por el usuario (no se pueden incluir).
- [~] **P3** Mandos y remapeo: backend SDL y teclas BTZ conectados; guardado en carpeta de usuario ya disponible.
      Validar dispositivos reales, conexión/desconexión y calibración desde el menú antes de cerrar el soporte.

### Fase 9 — Cierre del desensamblado

- [ ] **P2** Nombrar todas las funciones del juego (`< 0x60000`, ~300) y documentar sus estructuras.
- [ ] **P2** Clasificar el motor Lisa 2 (`0x54000–0x60000`, 109 funciones): API `LI_*`, memoria, sprites, fuentes,
      escena. Basta con equivalencias, no hace falta portarlo línea a línea.
- [ ] **P3** Verificar con firmas que `0x60000–0x86000` es solo libc, DOS/4G y drivers, y documentar las excepciones.

---

## Metodología

1. **Localizar** la función por cadenas (`xref`), globales (`refs`), constantes `double` o llamadas (`callers`).
2. **Leer** el ensamblado (`dis`), resolver constantes y tablas con `Image.find`, y anotar el resultado en
   `docs/functions.json` y `docs/formats.md`. Actualizar estado, notas y referencias en la misma ficha; las vistas se regeneran.
3. **Portar** a C manteniendo nombres y comentarios con la dirección original (p. ej. `/* sub_0004e2d8 */`).
4. **Verificar** sin ventana: simulaciones `--sim`, trazas `IGN_TRACE`, imágenes `--shot` y el visor `--model`, en
   las 7 pistas y sin avisos de compilación.
5. **Marcar las desviaciones** explícitamente en el código y en este README.

Convenciones del port: Y hacia abajo; 1 m = 21,76 unidades del mundo; rumbo 0 = +z; ángulo físico
`= π/2 − rumbo`; las coordenadas SRF y TRI son las del mundo + 25 600.
