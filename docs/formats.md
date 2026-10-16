# Ignition (UDS, 1997) — formatos y binario

Todo little-endian. Estado: **verificado** = decodificado y comprobado visualmente; **hipótesis** = solo cabecera observada.

## Ejecutable `MAINDOS.EXE`

- MZ + LE (DOS/4GW), Watcom C/C++32 (runtime 1988-1995). "Copyright (c) UDS, 1995-1996".
- Objetos (base lineal tras `tools/le2elf.py`):
  | Obj | Base | Tamaño | Uso |
  |---|---|---|---|
  | 1 | `0x00010000` | `0x75c4f` | código |
  | 2 | `0x00090000` | `0x1597` | datos |
  | 3 | `0x000a0000` | `0x1cf640` | datos + BSS + pila |
- Entrada `0x00055fbc`, pila `0x0026f640`. 41233 fixups 32-bit + 71 relativos.
- Rutas que usa: `LEVELS\%s%s.{TRI,SRF,COL,PIC,POS,MSH,TEX}`, `baltazar\data\*`, `fonts\*.lft`.

## Verificados

- **COL** (776 B): `u32 0x308`, `u32 0xb123` (tipo), 256×RGB. Valores de 6 bits (VGA) o de 8 bits según el archivo.
- **PIC**: cabecera `0x40` = `u32 tamaño_archivo`, `u8 0`, `u8 0x95`, `u16 ancho`, `u16 alto`, `u16 x`, `u16 y` (¿posición?), resto ceros.
  Después, *chunk* de paleta (COL, 0x308 B) y *chunk* de píxeles: `u32 ancho*alto+6`, `u16 1`, índices de 8 bits sin comprimir.
- **TEX**: índices de 8 bits sin cabecera, ancho 256 (pistas: 256×4096, una página de texturas de 256×256 cada 64 KB).
- **DAT** (`MENUBKG.DAT`): índices de 8 bits, ancho 320.
- **PLC** (colocación): `u32 n` + n × `{i32 offset_msh_en_dwords, u32 tipo, i32 x, i32 y, i32 z}` (20 B). Pistas y coches.
- **MSH** (mallas): por objeto `i32 nv, i32 nf`, nv × `{i32 x,y,z}`, nf × 44 B `{u32 flags, i32 v0,v1,v2, i32 u0,v0,u1,v1,u2,v2 (8.8), u32 página<<16}`.
  `u,v` van dentro de una página de 256×256 del TEX (`página = último campo >> 16`); índice 0 = transparente.
  Ejes: X/Z horizontales, **Y hacia abajo**. Los objetos son contiguos: 223/223 (USA) y 55/55 (CARS) verificados; render 3D correcto en las 7 pistas.
  La **Y del PLC apunta hacia arriba**: el mundo es `x = vx + px`, `y = vy − py`, `z = vz + pz`. Lo hacen la consulta
  de suelo (`sub_00020814`, `sub ebx, [obj+0x10]` en `0x20a2b`), `face_avg_height` (`−vy + py`) y `build_sections`
  (`0x28f77`); el objeto del motor guarda `py` sin cambiar (`0x56c8f`). Con el signo contrario las piezas vecinas no
  comparten vértices (USA: 123 frente a 1514) y se ven rendijas negras. `ign_load_model` guarda `−py`.
  Excepción del original: `load_tri` suma `vy + py` para la altura de las puertas (`0x1d32d`).
- **TAB/PAN/SHD** (64 KB): tablas 256×256 de consulta; el juego las alinea a 64 KB para indexar con `(a<<8)|b`.
  **TAB** = sombreado `TAB[color*256 + nivel]`: niveles 0–63 oscurecen (89 % → 54 % de brillo), ≥ 64 = color sin cambios.
  Se pasa al rasterizador en la estructura de estado `0xc4840` (`+0xc`). **PAN** lo usan la barra de progreso y `sub_00043fe0`.
- **CARS** (`CARS/CARS.PLC/MSH/TEX`): 55 objetos = 11 coches × 5 piezas; `tipo = coche*10 + pieza` (0 carrocería, 1–4 ruedas).
  CARS.TEX = 6 páginas de 256×256 + 64 bytes finales sin usar. Renderizado correcto con la paleta de la pista.
- **Montaje del coche** (`cars_init` 0x265f6, portado): la Y de la PLC no se usa. Carrocería (`0x1fecb4[coche]`) desplazada
  para que su vértice más bajo quede en y=0 (×3 en altura si `[0x1fb2f0]==1`); ruedas (`0x1dad30[coche·4+r]`) centradas en
  y=0 con desplazamiento x/z respecto a la carrocería (`0x1faf40`/`0x1fae90`). Ruedas +1/+2 = delanteras. 11/11 coches
  verificados (Mini, Porsche, jeep, policía, hot rod, autobús, furgoneta monstruo, Escarabajo, camión, muscle car, deportivo).
- **Pose por frame** (`sub_00048a14`, portado): cada coche es un objeto del motor (`0x1fef8c`) más 4 ruedas (`0x1fefa0`),
  sombra (`0x1fefb0`, cuadrilátero `car+0x2fc..0x334`, pendiente) y nombre. Ángulos en décimas de grado (rad·180/π·10).
  Ruedas: inclinación del suelo `car+0x108` (balanceo) / `+0x110` (cabeceo); las delanteras giran sus vértices sobre su
  eje vertical `car+0xb0` grados (dirección visual). **Las ruedas no ruedan.** Carrocería (`sub_00040ec8`): sigue
  `pendiente + inclinación dinámica` con filtro 0,25 por tick (`car+0x584`/`+0x58c`). Inclinación dinámica
  (`sub_0002e6f8`): aceleración por tick (`car+0x18/+0x28` − `+0x30/+0x40`, ±0,5) → `balanceo −= 3·a_lat`,
  `cabeceo −= 3·a_long`, ×0,97 (`+0xc8/+0xd0`), límites ±35° / ±20° (`+0xd8/+0xe0`).
- **Altura de dibujo**: carrocería y ruedas se colocan en `car+8` (eje a 5 unidades sobre el suelo), sin compensar el radio:
  los neumáticos (radio ~10) quedan medio hundidos en el asfalto. Las mallas de rueda se copian sin escalar.
- **Sombra** (portada): `car+0x2fc..0x314` = x de las 4 ruedas, `+0x31c..0x334` = z. Malla `0x1ac0a0`: 4 vértices
  (x, z·1,2), 2 triángulos; se coloca en el suelo (`car+0x120`) desplazada `(altura − suelo)` en x y en z (luz a 45°)
  con la inclinación de las ruedas. Desviación: oscurecida con una tabla de paleta al 50 % (falta portar TAB).
- **Nombre sobre el coche**: solo coches con tipo de control `0x1fefd0[coche]+4 == 2` (humanos en red/pantalla partida),
  nunca el coche 0; no aplica contra la IA.
- **Trucos** (`0x160c4`, palabras en `0xbf030`, flags `0x1fb2f0[i]`, se alternan al teclearlas): BANARNE (carrocerías
  ×3 de alto, portado), SURMULE, SLASKTRATT, FILMJOLK, SKUNK, SVINPOLE, STRINGS (pendientes).
- **Cielo**: no existe. La cámara del juego es cenital y el fondo es negro.
- **Cámara de persecución** (`[0x1fef9c]` + coche × 0xc8; init `sub_00027df8`, actualización `sub_00047f94` una vez por
  fotograma, uso en `sub_000420cc`): `+0x50` = 400 (altura base), `+0x54` = 400 (distancia ×2), `+0x40` = 420 y
  `+0x48` = 450 (cabeceo en décimas de grado), `+0x00` = `car+8 + 1400`, `+0x08` = rumbo. Por fotograma y coche: si el
  coche no ha terminado (`car+0x528 == 0`) se fijan los objetivos `+0x78/+0x88 = coche − (cos, sin)(+0x08)·+0x54/2`,
  `+0x80 = car+8`, `+0xa8 = car+0xf8`, `+0xb0 = car+0x110` y, delante, `+0x90/+0xa0 = coche + (cos, sin)(+0x08)·+0x54/2`,
  `+0xb8 = car+0xf8 + π`; al terminar quedan congelados. `+0x08 −= wrap(+0x08 − +0xa8)·0.125` y
  `+0x28 −= wrap(+0x28 − +0xb8)·0.125`, ambos en `(0, 2π]`;
  `f = (cos((+0x08 − α − π/2)·2 + π) + 1)·50 = 100·cos²(+0x08 − α)` (0 si el coche ha terminado), con α el ángulo del
  tramo `car+0x364` (`g_sections+4`; `+0x10` si el tramo es negativo; media de ambos si `car+0x36c == 3` ¿?), que
  `build_sections` calcula como `−atan2(x − x_sig, z − z_sig)` de los orígenes PLC del objeto del tramo y del siguiente
  (`0x647ef` hace `fxch` antes de `fpatan`: con `dz` apilado primero el resultado es `atan2(dx, dz)`), en `[0, 2π)`.
  Mirando a lo largo del tramo `f = 0`.
  `+0x48 += ((+0x00 − +0x80 − 400)·0.5 + 3f + +0xb0·3600/(10π) − +0x48)/k`;
  `+0x00 += (400 − (+0x00 − +0x80) + 2f − +0xc0)/k` con mínimo `+0x80 + 200`; `k` = 25 mientras el reloj
  `[0x1fb4e8] ≥ 0` (antes de la salida) y 10 en carrera (`0x47fab`);
  `+0x54 = trunc(+0x54 + (400 − 2f − +0x54)·0.1)`; `+0xc0` va hacia ±50·n (byte 2 del tipo PLC del objeto del tramo:
  1–10 sube, 11–20 baja) a `car+0x118·10/30` por fotograma; `+0x30` (golpe) ×0.98, o hacia 150 si el coche explota.
  Vista (`sub_000420cc`): posición `+0x78/+0x88`, altura `+0x00 + +0x38` (`+0x38` = 0), cabeceo
  `+0x40 + +0x30 + +0x48` décimas, ángulo `+0x68 = −+0x08·1800/π + 900`. Con `car+0x5a4` (tecla mantenida, `human_input`
  `0x2c9df`) usa la vista trasera: `+0x90/+0xa0` y `+0x70` (de `+0x28`).
  Focales por modo (`race_setup_video`, `+0x58/+0x5c`): 320×200 = 200/164, 640×480 = 425, 800×600 = 550.
- **Reloj de salida** (`sub_0002b850`, cada fotograma): mientras `[0x1fb4e8] > −1`, `[0x1fb4e8] = (ms − inicio)·0.036`
  (fotogramas de 1/36 s); al llegar a 200 (5,56 s) pasa a −1 = carrera. `[0x1fb4d8]` acumula el tiempo total en las
  mismas unidades. Antes de la salida: `sub_0004e1bc` bloquea las ruedas (`body+0xd4 = 1`), `car_throttle` no actualiza
  `body+0xd8` (`0x4df0e`), `car_input` rechaza el turbo (`0x4d921`) y `cars_update_all` solo llama a `ai_drive` desde
  150 + 20 = 170 (`0x2dd3c`). Cuenta atrás del HUD (`0x4b717`): sprite `[0x1fea3c]` (3) con el reloj en `[50, 100)`,
  `[0x1fea40]` (2) en `[100, 150)`, `[0x1fea44]` (1) en `[150, 200)` y `[0x1fea48]` (GO) en carrera mientras
  `[0x1fb4d8] < 250`, con un sonido en cada cambio. Las luces de salida parpadean con el reloj en `[60, 130)`
  (`0x42f94`).
- **Cámaras tras la meta** (`sub_000420cc` `0x42862`): al terminar el jugador `[0x1fb4d0] = 0` y `[0x1fee80] = 3`
  (`car_update` `0x2f37f`); 120 fotogramas después la vista pasa a la cámara `[0x1fee80]`, que TAB (código 0x0f)
  avanza 0→1→2→3→0. 0–2: tabla `0xc464c` (pista × 60 B, 3 × `float x, altura, z, cabeceo, ángulo`, coordenadas del
  motor y radianes), con la altura `+ sin([0x1fb4d8]·0.05)·8`. 3: `sub_00043590` elige el coche en carrera con mejor
  posición `car+0x39c` (el peor en eliminación) y cambia tras 20 fotogramas (`[0xc0b38]`, 100 al terminar); se ve con
  su vista trasera. Si ya no queda nadie en carrera vuelve a la cámara 0.
- **Texturas**: afines por defecto; la corrección de perspectiva es la opción PERSP. POLY (desactivada).
- **TAB**: el estado del rasterizador lleva el puntero (`[0xc484c]`, sin desplazamiento de fila); las caras texturizadas
  no se sombrean (como la primitiva 0x11 de `Ign_win.exe`).
- **SRF** (consulta de suelo, `sub_00020814`; verificado): cabecera 9×`i32` = `?, ?, alto_celda=512, ancho_celda=512, gx=101, gz=101, nspan, na, nb`.
  Celdas (12 B): `u32 offset_lista_B, u32 offset_lista_A, u16 nA, u16 nB` (offsets en bytes). Listas: `u32` offsets en bytes a spans.
  Span (24 B, trapecio en planta): `i32 x0, z0, pendiente_izq, pendiente_der (16.16), i16 dz, u16 cara (dwords desde el inicio del objeto MSH), u32 objeto*42`.
  Lista B: `z0 ≤ z ≤ z0+dz`; lista A: `z0+dz ≤ z ≤ z0`; en ambas `x0 + (t*izq>>16) ≤ x ≤ x0 + (t*der>>16)`, `t = z−z0`.
  Los recorridos `0x20c18` (B) y `0x20c81` (A) usan IMUL de 32 bits: se descartan los bits altos **antes**
  del desplazamiento aritmético SAR 16. Las sumas y la resta de coordenadas también envuelven a 32 bits.
  Por ejemplo, `32767*131072` envuelve a −131072 y produce −2, no 65534.
  `surface_spans_original` ejecuta ambas funciones completas, sin interceptar callees: 1 816 listas / 7 492 spans;
  compara los pares objeto/cara en orden, salida vacía, límites inclusivos y signed extremos.
  El objeto es el índice del pool del motor de 42 B, **no necesariamente el índice PLC**; `engine_to_plc` traduce
  ambos espacios. Coordenadas = mundo + 25600. El juego elige la cara más cercana a la altura del coche
  y lee `flags >> 16` como **tipo de superficie** (prefiere 0x28–0x4f). Centroides de caras encuentran su propia cara: 62–78 % (el resto son paredes).
- **POS** (animaciones de objetos, `sub_00041244`/`sub_000412e8`): `n_plc` × `i32` (−1 = sin animar; si no, offset en dwords
  relativo a la propia entrada). Animación: `i32 nframes, i32 frame_actual`, `nframes` × `{i32 x,y,z, rx,ry,rz}`;
  ángulos en décimas de grado (el juego resta 3600 si ≥ 3600). Las posiciones se convierten en deltas del fotograma
  siguiente, con el último enlazado al primero; al avanzar se resta el delta y se asignan los ángulos del fotograma.
  `pos_original` ejecuta `0x41244` completa y compara 169 buffers / 673 312 bytes: siete pistas, registros
  compartidos, deltas cíclicos y resta wrapping de 32 bits. La conversión conserva ángulos y cursores; un registro
  compartido se convierte tantas veces como entradas lo referencien. La validación nativa de offsets y tamaños no
  pretende reproducir accesos inválidos del ejecutable. En el port, `pos.c` copia y valida la tabla, avanza a 36 Hz y aplica las
  transformaciones de posición/rotación al render y a ciertas consultas geométricas. El orden Euler del motor no está
  confirmado: se usa `Z(Y(X(v)))` como aproximación; los spans SRF no se regeneran, así que la consulta de suelo y las
  colisiones para objetos trasladados siguen incompletas. Se animan todas las entradas POS porque aún no se porta el
  flag activo del objeto original.
- **TRI** (bordes de pista, `load_tri` 0x1ce30; cargador y tablas comparados con el original): `u32 n_tramos`, luego un bloque de 500 B por objeto PLC.
  Cada bloque: hasta 2 registros empaquetados de 22 B `{u8 flags, u16 vértice_msh, i32 x, i32 y, i32 z, 8 B}` (bordes izquierdo/derecho),
  seguidos de `double`s (pendientes/ángulos precalculados). X/Z = vértice MSH + posición PLC + 25600 en el 72–90 % de registros.
  `load_tri` toma los vértices de `+1` y `+0x17` como **bordes izquierdo/derecho** del tramo (puerta) y el byte `+0x6d`
  divide la puerta en 2 registros (1 = mitad central→derecha y luego izquierda→central; 2 = izquierda→central y luego central→derecha).
  Las coordenadas X/Z de estos registros son vértice + posición PLC + `0x6400`; Y es vértice + posición PLC sin ese origen
  (`0x1d30b..0x1d392`). `track_original.py` ejecuta el cargador original y compara las tablas de las siete pistas:
  1 565 registros normales de 75 bytes y 70 placeholders inválidos de un solo byte. No debe leerse geometría
  detrás del flag de un placeholder. Los `double`s adicionales del archivo no son necesarios para reconstruir
  las puertas: el cargador calcula sus ángulos e interiores.
- **GST** (fantasma, `GHOSTS\<nombre>.GST`; lector `0x2adf8`, grabador `0x51204`, escritor `0x2ccc8`; portado en `ghost.c`):
  exactamente `0x49d4c` = 302 412 B (`read_file_into` exige el tamaño). Cabecera: `i16 modelo, i16 pista, i16 tiempo` (carrera
  completa de 3 vueltas en centésimas, `car+0x3a4 × 100` truncado) y `char[4]` nombre (3 letras + NUL). Después 21 600 muestras
  de 7×`i16`, una por tick de 1/72 s (300 s máx.; si se llena, `[0x1fee00] = 1` y no se guarda): `x + (−25600)`, `y` (altura,
  `+5000` si `car+0x354`), `z − 25600`, ángulos `car+0x108 / +0xf8 / +0x110` en `[0, 2π) × 1024` y progreso
  `vuelta × (tramos + 10) + |tramo|` (sin el tramo si `|tramo| ≥ tramos/4` en la fase 0). Al guardar se rellena hasta el final
  copiando las palabras 0–5 de la última muestra (la 6 queda como estaba) y quedan 2 bytes a cero. Solo en contrarreloj
  (`[0x1feefc] == 2`) y si se bate `[0x1fee04] × 0.01`. Pistas: 0 CANADA/MOOSEJAW, 1 USA/GOLDRUSH, 2 CARIB/SNAKEISL,
  3 BRAZIL/LOSTRUIN, 4 AUSTRIA/YODELPEA, 5 ICELAND/CAPETHOR, 6 JAPAN/TOKYOBUL. Coincide con `Ign_win.exe`.

## Animación de texturas: constructor y helpers

`0x10508` construye descriptores de 24 bytes `{n, cur, prev, uv*, page*, faces*}` en la lista `0xab5f0`
(contador `0xab5f4`). `0x1098c` cuenta el prefijo de caras texturizadas; `0x10bc8` cuenta coincidencias y
`0x10b30` recoge los punteros. `0x10c5c`/`0x10cf8` comprueban página e inclusión de los tres pares UV en el
mosaico de 64×64, con límites signed inclusivos y sumas wrapping. El avance `0x10a00` conserva los deltas UV
de 32 bits; las caras compartidas por varios objetos aparecen varias veces y reciben todos sus incrementos.

`texanim_original` ejecuta constructor, helpers, avance y ambas entradas idénticas de liberación
(`0x10390`/`0x1044c`); sólo intercepta malloc/free de Watcom. Compara descriptores, bytes de caras y multiconjuntos
de punteros (incluidos alias) en 218 casos, siete de ellos pistas reales con ocho pasos.
La comparación no exige el mismo orden de colección, que no cambia las sumas UV.
**Desviación intencional:** el port libera también el contenedor `+0x14` que pierde el original, las animaciones
de cero fotogramas y la construcción parcial; deja el propietario vacío e idempotente.
`texanim_oom` falla cada una de las 12 asignaciones y verifica retorno de error, notificación, ausencia de fugas
y liberaciones inválidas. Por ello las dos entradas de teardown se anotan `ported`; los cinco helpers y el
constructor quedan `verified` para la construcción definida.

## Equivalencias nativas de reproducción CD

`cd_busy` `0x63cf3` → `ign_music_playing`, `cd_play_audio` `0x64332` → `ign_music_play`,
`cd_stop_audio` `0x64391` → `ign_music_stop`. Se reproduce la selección de pistas completas del juego mediante OGG,
no peticiones MSCDEX, TOC, cambios de disco ni intervalos MSF arbitrarios.
`music_native` usa el decodificador real y locks/dispositivo inyectados, sin callback ni altavoz real.
Prueba selección por nivel, aleatorio 2–8 sin pista de datos 1, lectura finita de todas las pistas, stop
idempotente, errores explícitos, EOF y repetición. Track02 termina tras el mismo número de muestras audible
y silenciada: volumen cero ya no congela el decodificador ni deja busy activo indefinidamente.
Estas tres entradas cuentan como `native`, no como instrucciones portadas/verificadas del DOS.

## IA de rivales (`sub_0001aea0`, ~1900 instr.)

- Llamada desde `cars_update_all` (0x2dd56) con `al` = índice de coche; termina en `car_input` (0x4d5f4) con
  acelerador `[esp+0x70]`, dirección = estado+0x74 y turbo.
- Estado por coche en `[0xeb7c0]` (0x7c B), puertas por tramo en `[0xeb7c4]`/`[0xeb7e0]`.
- Parámetros fuente en `0xbfb28` (0x48 B = 9 `double` por coche):
  `{0.9, 0.8, 0.4, 0.6, 0.75, 0.5, 5, 0.6, 0.6}`. `load_tri` deja el estado `+0x30` a cero,
  desplaza los últimos parámetros a `+0x38 = 5`, `+0x40 = 0.6` y trunca el último a **int32**
  `+0x48 = 0`. El layout C es semántico, no una copia binaria Watcom. Dificultad: `+0x00 × 1/0.7/0.4/0.4`;
  el caller de creación de rivales ajusta `+0x28 = 0.4 + 0.19·dificultad`. `TEST.AIS` no se lee.
- `src/game/ai.c` porta la selección, objetivo, pendiente, acelerador/freno, dirección, esquiva, aplastamiento,
  marcha atrás y turbo. El cotejo del controlador completo se describe abajo; no equivale a verificar una carrera DOS.
- **Carril (`t_ai_lanes`, `0xbf9a0`)**: portadas las cuatro filas por configuración y la permutación de pistas
  `{1,2,3,6,4,0,5}`. En cada fila coincidente se escribe `selector` en `+0x66`, se consume un `rand()` y se guarda
  `r = (rand() & 0x7fff) / 32768` en `+0x6c`; la elección `+0x64` es 0 si `chance=0`, 1 si `chance=100`, o
  `(s20 * 0.7 + 0.3) * r * chance * 0.01 > 0.125`. Las filas repetidas (incluidas las de sección 2)
  **no** se procesan después de la primera coincidencia (`0x1b14d/0x1b15b/0x1b18e → 0x1b1a5`).
  Además, sólo el `selector` se lee con el ID de pista **sin permutar** (`0x1b0bf..0x1b0f7`); sección y
  probabilidad sí usan la permutación. En el port, `ign_fx_rand15` usa el LCG `seed = seed * 1103515245 + 12345`
  (`rand`, `0x64774`), compartido con el estado de `IgnFx`.
- `load_tri` se integra al arrancar: las secciones contienen índices **PLC**, no índices del pool del motor.
  `0x1d246..0x1d261` multiplica directamente ese índice por 20 para acceder al PLC; no aplica `engine_to_plc`.
  Hay dos slots canónicos: un principal TRI1/2 ocupa ambos y omite B; con principal normal, B ocupa el segundo
  **sin dividirse**, aunque su propio tipo TRI sea 1/2. Los dos globals `[0xeb7c4]`/`[0xeb7e0]` reciben el mismo
  puntero (`0x1d0b3/0x1d0b8`), no son dos tablas independientes de mitades de A y B.
  `ign_build_track` reproduce la enumeración y el recentrado entero de `build_sections`, distancias con ΔY×2,
  ramas desiguales (sentinel `-2`), sobrescritura negativa del fork, saltos y repeticiones terminales.
  `track_original.py` compara las **1 365 secciones** y sus ángulos/mapas con la ejecución original,
  no sólo la estructura del grafo. La asignación de slots conserva por separado las identidades de `g_sections`:
  un slot1 de un split puede ser válido aunque su identidad de objeto sea `-1/-2`.
- `ai_find_section` (`0x1decc`) ya tiene un helper portado (`ign_track_find_section`): interpreta sección, rama y
  avance como entradas de 16 bits; hace un único ajuste de wrap; cambia a la otra rama si la solicitada no existe;
  rechaza **siempre** los placeholders inválidos y avanza según el signo del paso. También rechaza una sección
  sin segundo slot inmediatamente después de otra que sí lo tiene. Conserva el slot cambiado por fallback
  durante la búsqueda y la excepción de CANADA (`track_id=0`, `0xab..0xad`).
  `track_original.py` compara **5 516 consultas** (secciones `-1..count`, ambos slots, pasos ±1),
  incluidos AX/EDX, globals de salida, registros preservados, pila y x87. `0x1dfee` restaura
  `[0xeb7c4]`; la función termina en `0x1dffb`. No acepta datos de geometría de un registro inválido.
- **Discrepancia pendiente fuera del cargador IA**: `object_gate`/`ign_track_gates` todavía aplican
  `surface.engine_to_plc` a los índices de sección, con centroide como fallback. Se conserva ese comportamiento
  anterior para estas puertas auxiliares; `track_gates` caracteriza esa implementación,
  **no demuestra su fidelidad**. La investigación de atascos confirmó que el bucle actual no las consume para
  conducir, contar progreso ni reaparecer: no explican los atascos reproducidos. El cargador IA ya no hace esa
  segunda conversión errónea. El mapeo
  engine→PLC sigue siendo necesario donde la entrada realmente sea un índice del pool del motor (p. ej. SRF).

### Geometría derivada de `load_tri` y mezcla de rumbo: verificación por fragmentos

El registro original mide `0x4b` bytes: invalidez `+0`, byte 0 del bloque TRI `+1` (`[0xeb780][plc]`,
`0x1d2eb..0x1d2f1`; índice con signo de `t_surface_grip`, no el tipo PLC), bordes L/R xyz i32
`+0x02..+0x16`, cuatro puntos interiores L/R xz i32 `+0x1a..+0x26`, tres rumbos float32
`+0x2a/+0x2e/+0x32`, tres pendientes float32 `+0x36/+0x3a/+0x3e`, dos ángulos transversales
float32 `+0x42/+0x46` y tipo TRI `+0x4a`. El sucesor se resuelve con `ai_find_section`,
incluyendo su slot y el ID real de pista.

Los rumbos son `atan2(Δz, Δx)`; las pendientes son **`atan2(Δy, Δx)`**, no la pendiente
respecto a la distancia horizontal. El centro entero de cada componente es
`a + trunc((b-a)/2)`, también para diferencias negativas impares. Los ángulos transversales
se calculan con las restas directas L→R y R→L, idénticas en ambos slots. Los interiores
son `trunc(borde + 18*cos/sin(ángulo_float32))` (`0x1d9d0..0x1da22`). Con el control word del
runtime `0x127f` (`[0xe6be0]`, 53 bits), `fcos/fsin` no se redondean por la precisión, pero
`fmul` y `faddp` sí redondean a binary64 antes de `__CHP`: el port usa esa suma binary64 directa.
Una versión anterior conservaba el residuo de 64 bits; sólo coincidía con el oracle ejecutado
con `0x037F` y fallaba con `0x127f` en 297 comparaciones (20 registros reales).

`tests/ai_geometry_original.py` ejecuta los dos fragmentos sin parchear
`0x1d80a→0x1da25` y `0x1da75→0x1dc8f`, con los helpers reales `atan2` y `__CHP` y CW `0x127f`.
Compara bits de los ocho float32 y los cuatro enteros: **4 934 comparaciones exactas**,
902 fixtures sintéticos y 1 565 registros reales, cada uno contra ambos fragmentos.
Los registros reales proceden del constructor del port: se verifica su geometría derivada,
**no** la selección completa de registros ejecutando todo el cargador DOS. Se comprueban
frontera de ejecución, escrituras permitidas, entradas intactas, pila y estado x87.
Los sintéticos incluyen coordenadas ±100 000; no se acredita todo el dominio i32.

`ign_ai_drive` consume el rumbo central almacenado, el centro entero y la puerta completa
reconstruida para su ratio lateral, incluso al resolver hacia una sección dividida.
`ai_route_choice` prueba esos consumidores y las transiciones split/normal; son pruebas
del port, no un oracle de todo `ai_drive`.

El fragmento `0x1be03→0x1bf60` mezcla rumbo con dirección del movimiento:
`travel = wrap(heading + 0.15*wrap(motion-heading))`, con constantes originales
π=`3.1415926535898` y 2π=`6.2831853071796`. Si la separación sin envolver supera π,
se sustituye por `2π-separación`; si supera `0.78539816339745`, devuelve el rumbo
original **sin wrap posterior**. En reposo la dirección de movimiento es cero.
Con CW `0x127f` la suma ya está redondeada a binary64 al compararla, así que el port compara
directamente el double (el residuo extendido anterior sólo coincidía con `0x037F`).
**1 184 casos** (umbrales y vecinos binary64, más 500 aleatorios), tolerancia absoluta
`2e-15` y error máximo medido 0; no se afirma igualdad bit a bit: con `motion` subnormal el rango
de exponente x87 puede dar `-0.0` donde binary64 da `+0.0`.

### Recorte del objetivo y curvatura (`0x1b1a5..0x1b823`): verificación por fragmento

- **Lookahead** (`0x1b1a5..0x1b278`): seis registros en `s + {1,2,3,4,6,8}` (tabla i16 `0xbf994`),
  guardados en `[esp+0..0x14]`. Índice con un único wrap (`≥ count → −count`) y después, si
  `índice ≥ count−3`, se fuerza a `count−1`. Ruta: `+0x64` si la sección no tiene segundo slot,
  si no la elección de carril `[esp+0x178]`; búsqueda con `ai_find_section(índice, ruta, 1)`.
  La presencia del segundo slot se consulta en la sección **actual**, no en cada sección futura.
  Se comparan tanto el índice como los consumidores de los registros en el oracle del controlador completo.
- `[esp+0x40]` (`0x1b4a2..0x1b4e9`): `rumbo_centro(r0) − rumbo_centro(actual)` (float32 `+0x2e`),
  con un wrap de una pasada (primero `< −π` suma 2π, luego `> π` suma −2π; constantes
  `3.1415926535898`/`6.2831853071796`).
- **Recorte** (`0x1b4f1..0x1b6f6`): para `k = 5…1`, centro entero de `r[k]` (`a + (b−a)/2` en 32 bits,
  truncando hacia cero), guardado en `[esp+0xe8]/[esp+0xe0]`, y `aim = atan2(cz − coche_z, cx − coche_x)`
  en `[esp+0xf0]`. Para cada `j = 0…k`: `dL = wrap(atan2(cz − Lz_in, cx − Lx_in) − aim)`,
  `dR = wrap(atan2(cz − Rz_in, cx − Rx_in) − aim)` con los interiores `+0x1a..+0x26`. Si `0 > dL·dR`
  la línea de visión pasa entre ambos interiores; si no (incluido 0 o NaN), el candidato queda bloqueado,
  `[esp+0x144] = 0 > dL ? −1 : 1` y se prueba `k−1`. Se acepta el primer `k` sin bloqueo; si todos se
  bloquean, `esi = 0` pero el centro guardado es el de `r[1]` (último probado). `[esp+0x144]` vale 0 sólo si
  `k = 5` pasa a la primera. El bucle `0x1b4e9..0x1b4ef` es código muerto.
- `[esp+0x130] = {1,2,3,4,6,8}[esi]`; `ebp = s + offset` con un wrap (entrada del término de pendiente).
- **Agarre** (`0x1b721..0x1b75d`): `max(0, (8 − t_surface_grip[(int8) actual+1]) · 0.125)` con el byte
  TRI del registro actual. En las siete pistas aparecen índices fuera de la tabla de 30: −21, 79, 90 y 97
  (BRAZIL 205/206 incluidos); leen datos estáticos de sólo lectura que, como double, son 0 o subnormales,
  así que el agarre es exactamente 1.0. El port copia esos cuatro patrones de bits; otro índice fuera de la
  tabla devolvería 0 (desviación no alcanzada por los TRI incluidos).
- **Curvatura** (`0x1b764..0x1b821`): `n = min(esi+1, 5)`; para `j < n`,
  `t = |rumbo(r[j]) − rumbo(r[j+1])|`, si `t > π` entonces `t = |t − 2π|`; `w = (s38·0.4 + 0.6)·1.6`;
  `suma = t·(w·t)/n + suma`. Luego (`0x1b823..0x1b86b`) `a8 = 1 + (agarre + 0.8)·suma`.
- Consumidores (incluidos en el oracle completo): el acelerador divide por `s10·6/(3·esi+6) + 1` (`0x1b995..0x1b9cf`);
  la dirección recalcula el mismo `aim` con `[esp+0xe0/0xe8]` (`0x1bde2..0x1bdfc`); el ajuste hacia el
  interior exige `[esp+0x144] = ∓1`, `esi > 2`, `a8 < 2` y `|[esp+0x40]| < 0.3` (`0x1c40d..0x1c5be`);
  el turbo usa `[esp+0x130] > 3`. Divisor del ajuste: con signo −1, `side·2.5` si `0 ≤ side`, si no 2.5;
  con signo +1, 2.5 si `side > 0`, si no `side·−2.5` (`0x1c57a..0x1c5a4`; el port lo tenía invertido y daba
  un divisor negativo). Antes el port usaba un bucle inventado que paraba al cambiar el signo
  de la curva y el tipo de superficie del coche para el agarre; ambos se sustituyeron.
- Las coordenadas de los registros son SRF (mundo + 25600) y el original las compara con `car+0x00/+0x10`;
  el port suma `IGN_SRF_ORIGIN` a la posición del coche antes del recorte.

`tests/ai_target_original.py` ejecuta `0x1b4a2→0x1b823` sin parchear, con `atan2` real (`0x647ef`) y
CW `0x127f`; sólo permite escrituras en la pila y comprueba registros, punteros, pila x87 y `ebp`.
**7 590 casos**: 2 130 sintéticos (carreteras curvas, rumbos con ruido y en ±π, 300 colineales
exactos con `coche = 2·interior − centro`, 300 enteros aleatorios ±100 000, bytes de agarre 0–29 y los
cuatro fuera de tabla) y 5 460 de las siete pistas (registros slot 0 en los índices recortados, cuatro
posiciones del coche por sección). Índice, offset, signo, centros, `aim`, cambio inicial, agarre y suma
coinciden bit a bit. Cubre 224/228 instrucciones; no ejecuta el wrap de `ebp` ni el recorte de agarre
negativo (inalcanzable: la tabla tiene máximo 4.5). Mutaciones del port (bucle `j < k`, signo desde `dR`,
`(t·t)·w`, aceptar producto cero, π redondeado, centro con `floor`) se detectan; `t·w·t` frente a `t·(w·t)` y
el valor exacto de los subnormales fuera de tabla son equivalentes y no se distinguen.

### Controlador completo y tablas originales

`tests/track_original.py` entra en `build_sections` (`0x28f08`), `load_tri` (`0x1ce30`) y
`ai_find_section` (`0x1decc`). Modela servicios externos de asignación, ficheros y runtime, pero ejecuta la
lógica y matemáticas originales. Compara topología, mapas, ángulos y registros de las siete pistas.
Los placeholders sólo tienen definido su byte de invalidez. La inicialización de parámetros del cargador
se comprueba para control tipo 0/dificultad 0; el controlador prueba las cuatro dificultades.

`tests/ai_drive_original.py` ejecuta desde `0x1aea0` hasta los cuatro argumentos de `car_input` en
`0x1cbf9`, y después el epílogo original `0x1cbfe..0x1cc42`. No sustituye selector, `rand`, `atan2`
ni `__CHP`. Comprueba pila, x87 (`0x127f`), RNG, estado y controles:
**4 323 casos definidos** (1 617 sintéticos y 2 706 sobre tablas extraídas del original de las siete pistas),
controles/enteros exactos y tolerancias absoluta/relativa `2e-13` para doubles.

Correcciones del controlador:

- El slot actual se elige con el lateral y elección **del turno anterior**, antes de actualizar la tabla de
  carriles y el cociente de distancias. El registro actual se consume directamente, sin selector.
- Pendiente `x90 = sqrt(max(1, 1 + (Y_actual_izq − Y_futuro_izq)·(s40·1.1)/(offset·40)))`,
  con fallback a slot0 si falta el solicitado; multiplica el acelerador antes del término de visibilidad.
- Ganancia de dirección ×1.2 para **modelos de coche** 5/8 (`[g_control_info]+0`, no el tipo de control).
  Se usa `car.spec`, el mismo índice ya usado por las características del coche.
  Aplastamiento invierte el giro sólo cuando
  `flat != 0 && flat < 32.4 − 9·dificultad`.
- La distancia del rival es entre centros, pero su ángulo usa **eje trasero rival → eje delantero propio**.
  La esquiva se atenúa por `(radio − distancia)/radio`, no por su inversa; invierte el empuje si
  el lateral previo del rival supera ±1.9 en la dirección del empuje. `car.ai_side` conserva ese estado
  entre turnos del planificador.
- El turbo ordinario exige `abs(dirección) < s20·5 + 3`, no `>`; se conserva el forzado por límite 500.
  El estado `+0x54` guarda el acelerador calculado también durante marcha atrás.

**Desviaciones de memoria indefinida:** se generan 4 535 candidatos. En 212 (188 sintéticos y 24 reales)
el original lee geometría fuera de un placeholder de un byte al reconstruir una puerta o consultar la
pendiente directamente. Un hook de lectura detecta y enumera esos accesos: no se comparan sus controles
dependientes de heap indefinido, pero sí elección de ruta, selector, RNG y aleatorio. Todos los candidatos
deben producir controles/estado nativos finitos. El port conserva la puerta seleccionada si sus extremos
de reconstrucción son inválidos y resuelve un sucesor válido para la pendiente; avisa una vez por pista.
Un slot actual ausente/inválido produce diagnóstico y controles neutros, en vez de leer fuera de memoria.
No se afirma equivalencia de esos estados indefinidos, de los efectos internos de `car_input` ni de una carrera DOS.

### Diagnóstico integrado de atascos (2026-10-16)

`IGN_TRACE=ai` captura la frontera real de cada decisión del planificador: estado IA y RNG **anteriores**,
los ocho coches (posiciones, rumbos, ejes y lateral), controles/estado resultantes y datos de contacto,
suelo, marcha y fuerza aplicada. Las líneas `AI_DIAG` tienen esquema 2 y se emiten después de la llamada
para no intercalarse con sus diagnósticos. No se invoca la IA una segunda vez ni se cambia la conducción.
`headless_trace_contract` comprueba que activar la captura conserva estados e imagen de los ocho coches.
El esquema 2 separa el rumbo normalizado de entrada IA del acumulado físico (`motion.body_heading`);
el analizador sigue aceptando las trazas históricas de esquema 1.

Antes de la corrección, `tests/ai_stalls.py` ejecutó **14 400 ticks exactos (200 s), ocho coches y siete pistas**. Reproduce cada
decisión capturada con `DriveOriginal`, reutilizando las tablas del oracle `track_original`:

| Pista | Decisiones | Comparaciones definidas | Lecturas indefinidas DOS | Controles que cambian al normalizar el rumbo |
|---|---:|---:|---:|---:|
| USA | 6 608 | 6 555 | 53 | 1 077 |
| CANADA | 7 140 | 6 928 | 212 | 3 193 |
| AUSTRIA | 7 090 | 7 077 | 13 | 799 |
| BRAZIL | 7 150 | 6 836 | 314 | 454 |
| CARIB | 6 977 | 6 974 | 3 | 3 091 |
| ICELAND | 7 150 | 7 116 | 34 | 3 028 |
| JAPAN | 7 110 | 7 094 | 16 | 342 |
| **Total** | **49 225** | **48 580** | **645** | **11 984** |

Las **48 580 comparaciones definidas no presentan discrepancias** en controles/estado frente a los mismos
inputs DOS. Las 645 lecturas indefinidas se enumeran, no se cuentan como aciertos. Esta comprobación valida
el controlador sobre trayectorias reales del port, **no la fidelidad de sus productores de estado**.

**Fallo confirmado en el binario anterior: faltaba la copia normalizada del rumbo.** En `cars_update_all`,
`0x2d9c7..0x2d9e8` llama a `angle_normalize` (`0x51938`) con `body+0x28` y guarda el resultado en
`car+0xf8`, antes de calcular ejes/ángulos y decidir controles. El helper suma/resta repetidamente
`2π = [0xaa632] = 6.2831853071796` hasta obtener `[0, 2π)`.
**No modifica el rumbo acumulado del cuerpo físico.** El port anterior no tenía esa copia separada y
`ign_ai_drive` consumía directamente `car.body.heading`.

`StallOriginal.car_heading` ejecuta ese fragmento de sincronización y el helper **sin parchearlos**,
comprobando límites de código/escrituras, pila y x87. En 29 283 decisiones el ángulo entregado a la IA está
fuera del rango original. Al sustituir únicamente ese input por la copia DOS normalizada, manteniendo
posición, velocidad, vecinos y estado IA capturados, cambian **11 984 controles** y **220 actualizaciones
del contador de atasco**. No son nuevas carreras corregidas: son contrafactuales locales sobre la trayectoria
anterior, cuyo historial de decisiones puede haber divergido ya.

Ejemplo CARIB: rival de índice 1, tick **402** (5,583 s), sección 7: el port entrega
`7.710515242219468`, mientras el original copiaría `1.4273299350398672`; la dirección pasa de
**−22 a −59** con el resto del estado idéntico. El contrato del rumbo ya difiere en el tick 2.
En CARIB, el coche de índice 3 permanece 8,667 s en cada una de las secciones 201 y 202, recorriendo
aproximadamente 1 350 unidades pero con avance neto de sólo 190 y 35. Alterna aceleración y marcha atrás;
la copia normalizada cambia los controles en 33/40 y 31/40 muestras de esas permanencias, respectivamente.
**No permanece continuamente por debajo de 10 km/h durante 3 s**. CANADA presenta una permanencia de
18,444 s en −160. Una velocidad final baja no basta para diagnosticar inmovilidad permanente.

**Corrección aplicada y contrastada:**

- `ign_car_sync_heading` porta `angle_normalize` `0x51938` mediante las mismas sumas/restas de
  `6.2831853071796`, no `fmod`. Su entrada física debe ser finita; conserva incluso el signo de cero.
  `IgnCar.drive_heading` representa `car+0xf8` y **no modifica** `IgnBody.heading` (`body+0x28`).
- Se sincroniza al inicializar/cambiar pose (incluidos fantasmas), tras cada paso físico y después
  de colisiones, antes de decidir controles. La IA, ejes, sondas de contacto, wreck y turbo direccional
  consumen la copia. No se ha cambiado el algoritmo del integrador físico.
- El kernel **N** de `car_original` ejecuta el fragmento `0x2d9c7..0x2d9ee` y el helper originales:
  **291 casos bit a bit**, CW `0x127f`, ceros con signo, límites/múltiplos de 2π y ángulos aleatorios.
  Comprueba tanto la copia como la conservación del cuerpo. La prueba nativa de IA también conserva
  la dirección al añadir seis vueltas al cuerpo sin alterar la copia.

Se repitió la misma duración y configuración en las siete pistas:

| Pista | Decisiones | Comparaciones definidas | Lecturas indefinidas DOS | Permanencias ≥5 s antes → después | Vueltas jugador automático antes → después |
|---|---:|---:|---:|---:|---:|
| USA | 7 018 | 6 967 | 51 | 4 → 0 | 3 → 3 |
| CANADA | 7 120 | 6 861 | 259 | 8 → 0 | 1 → 3 |
| AUSTRIA | 7 130 | 7 112 | 18 | 0 → 0 | 1 → 2 |
| BRAZIL | 7 088 | 6 989 | 99 | 0 → 0 | 2 → 2 |
| CARIB | 6 848 | 6 831 | 17 | 2 → 0 | 0 → 3 |
| ICELAND | 7 110 | 7 055 | 55 | 9 → 0 | 2 → 3 |
| JAPAN | 7 130 | 7 130 | 0 | 1 → 0 | 2 → 2 |
| **Total** | **49 444** | **48 945** | **499** | **24 → 0** | — |

Las 48 945 comparaciones definidas no presentan discrepancias de controles/estado. En las 49 444
decisiones no hay discrepancias de copia ni rumbos IA fuera de rango; el cuerpo sigue acumulado
y queda fuera de `[0, 2π)` en 29 959 muestras. Las 499 lecturas indefinidas siguen excluidas.
No se detectan permanencias de ≥5 s ni intervalos continuos de ≥3 s bajo 10 km/h, según las muestras
de decisión del analizador. Son carreras nuevas con trayectorias distintas, no el contrafactual anterior.

El smoke de siete pistas es finito y determinista (estados e imágenes idénticos entre repeticiones).
Otras **14 pruebas humanas** (recta/turbo, siete pistas, 576 ticks sin rivales) conservan estados e imágenes
del binario anterior. La validación conjunta pasa **27 pruebas** de IA/trazados, contactos, física,
entrada, reaparición, replay y contratos.

Esto corrige el fallo de conducción observado, **no demuestra equivalencia de toda la carrera DOS**
ni que todos los coches deban acabar en 200 s. `car_update`, el resto de `cars_update_all` y el dispatcher
completo de `car_input` siguen parciales. Si aparecen otros bloqueos, deben aislarse sus productores
de estado antes de atribuirlos a esos sistemas.

Reproducción, con `IGNITION_DATA` apuntando a la carpeta del juego y las dependencias de
`tests/requirements-original.txt` disponibles:

```sh
ctest --test-dir build -R '^track_original$' --output-on-failure
.venv/bin/python tests/ai_stalls.py --binary build/ignition --data "$IGNITION_DATA" \
  --original "$IGNITION_DATA/MAINDOS.EXE" --records build/track-original-records.json \
  --report build/ai-stalls-report.json --tracks USA CANADA AUSTRIA BRAZIL CARIB ICELAND JAPAN
```

El informe conserva hashes del ejecutable original, binario nativo, tablas, PLC/MSH/TRI y trazas;
incluye intervalos con sus muestras inicial/final y separa los resultados indefinidos.
Las trazas y tablas derivadas permanecen en `build/`, fuera del control de versiones.

## Fin de carrera y eliminación (portado a `src/game/race.c`)

- `car_update` (`0x2f2be`): vuelta + 1 si la sonda actual es cero, `|sonda anterior +0x370| == count−1`,
  fase 3 y `car+0x604 == 0`. Si la vuelta ≥ 3 y el modo `[0x1feefc]` ≠ 3,
  el coche termina: `+0x528 = 1`, `+0x3a0 = +0x39c` (**posición**) y `[0x1fee60]++` (contador de llegada).
- `[0x1fed78]` = nº de coches (init `0x2aaee`), `[0x1fee60]` = 0 al empezar (`0x2a995`).
- `sub_0002de30` (solo modo 3, desde `cars_update_all` `0x2d2bb`): vuelta máxima entre coches con `+0x528 == 0`; si **un solo**
  coche va por detrás y `+0x60c == 0.0` queda eliminado (explosión `0x3fee8`, sonido `0x49a8c`, `+0x354 = 1`, `+0x60c = 1.0`).
  Si quedan 1 coche, todos terminan. Mensajes en 6 idiomas (`[0x1ff6eb]`), p. ej. `%s IS OUT`.
- `car+0x554` es el contador 1–0x50 del giro del chasis tras un golpe (`0x30d05`), no una reaparición.
- **Destrucción** (portado): `car_vertical` `0x2e397`, al aterrizar, con el tipo de la rueda 0 (`car+0x170`): explota
  (`car+0x354`, efecto 3) en `0x64`, en `0x6e–0x82` o `0x14–0x1d` con modo salto (`car+0x274`), o si el escalón de
  suelo filtrado (`car+0x128 − car+0x120`) es < −35; se hunde (`car+0x358`, efecto 7) en `0x65`, o en `0x82–0x96` /
  `0x1e–0x27` con modo salto. `car_update` `0x2fd63`: las 4 ruedas sin suelo → explota; `0x2fd9a`: las 4 sobre tipos
  > 0x28 sin estar en el aire más de 5 ticks (`car+0x5c8`) → explota. Objetos `0x12c` → explota, `0x130` → efecto `0xa`
  (`car+0x35c`). `car+0x5fc` = alguna rueda sobre tipo > 0x13; si cambia respecto al tick anterior (`car+0x600`), el
  suelo anterior se iguala al actual (escalón 0, `0x30c27`).
- **Reaparición** (`sub_0003a788`, efecto de 80 fotogramas): fotograma 0 oculta el coche (`LI_HIDEOBJECT`), guarda
  `car+0x384/0x38c/0x394/0x37c` (x, altura, z, rumbo), lo pone en el aire y llena el turbo; los 20 primeros fotogramas
  la cámara tiembla (`+0x38 = (20 − f)·rand·25`). En los 40 últimos se desliza al objeto del tramo `|car+0x364| + 2`
  (tramo 0 al final; en la rama B su objeto): punto = media de los vértices de sus caras de tipo < 0x28, 250 por
  encima; rumbo = ángulo del tramo − π/2. x/z con la curva `0xc0678` (`sub_000413fc`), altura y rumbo lineales. Al final,
  si ningún coche en marcha está a < 120 de este en x, altura y z, se vuelve a colocar (`car+0x350 = 1`, turbo 100); los
  coches que ya terminaron o fueron eliminados se quedan esperando.
- **Derrape** (`sub_0003731c`, portado en `skid.c`): `car+0x90 = fmod(atan2(dz, dx) − rumbo, 2π)`; hay efectos si no está
  en el aire, la superficie tiene valor en `0xc34e0` (≠ −1) y (`|sin car+0x90| > sin 22,5°` con > 8 km/h, o
  `car+0x2f8 > 0`). `car+0x2f8` = 10 al patinar contra el movimiento (`car_throttle` `0x4dd05`), 30 con la tracción
  recortada por el agarre (`0x4df99`) y 10 frenando fuerte a > 2 m/s (`0x4e283`); baja 1 por tick. Polvo (efecto 1,
  opción `[0x1fed70]`, no en superficies 5/15/25): por fotograma y eje, uno en cada rueda (rumbo ± π/2 ×
  `[0x1fefb4]+8`), a la altura `car+8`. Plantilla `0x1fec14` (valores ×1024): `+0x10/+0x18` vx/vz = rand·2,
  `+0x14` vy = 1 (sube), `+0x1c` = 0,5 (amortiguación de vx/vz por fotograma), `+0x24` = 4 (ranura de textura 4 =
  `GENERAL/SMOKE.TEX`), `+0x2c` velocidad de animación s = 0,2 + 0,4·rand, `+0x30` tamaño = 50 + 100·rand, `+0x34`
  crecimiento = 4 + 4·rand por fotograma, `+0x38` = tabla `0xc4110` (8 fotogramas de 64×64: filas 0 y 64 de la
  página), `+0x58` vida = trunc(8/s). `sub_0003fee8`: 200 huecos de 0x64 B en `0x1d5f10`; los tipos 1, 2 y 8 solo se
  crean a < 3500 del coche seguido (`[0xc0b34]`); lleno → prioridad `0xc47f0`. `sub_00039d1c` (cada fotograma):
  dibuja y luego `x += vx`, `vx ×= +0x1c`, `vy += +0x20`, fotograma += s, tamaño += crecimiento, muere a la vida.
  Sprite (`sub_0005dea0`, primitiva 7): se descarta con z ≤ 200; matriz diagonal `4/z · tamaño · ([0x1feee8],
  [0x1feeec])` con `[0x1feee8] = 65535·ancho/320` y `[0x1feeec] = 65535·alto/200` (`sub_00049724`), es decir medio
  ancho en píxeles = 32·4·tamaño·(ancho/320)/z; mezcla `dst = SHD[texel·256 + dst]` (`[0x24bfa8]` = SHD del nivel,
  `load_level` `0x2279d`). Los texeles de SMOKE.TEX son 0 (identidad) y 46–49 (aclaran un 14–49 %).
  Marcas (opción `[0x1fed80]`): cada 30 unidades del punto del eje (`car+0x48` trasero, `car+0x68` delantero) se crea
  un objeto de 8 vértices y 4 caras (modo 0x14) en un anillo de 700 (`[0x1feeb8]`) que une la posición anterior con la
  actual para las dos ruedas. `0xc34e0[tipo]` = {38, 92, 92, 38, 42, 38, −1, 42, −1, 0} repetido cada 10: el port lo
  usa como fila de SHD (38/42/92 oscurecen 54/24/33 %, 0 es la identidad) ¿?. El primer fotograma de cada derrape
  (`car+0x298`/`+0x29c` a 0, `0x38425`) solo arranca el sonido y anota los bordes: sin marca ni polvo.
- **Efectos** (portados en `fx.c`; registros de 25 dwords, posiciones ×1024, una vez por fotograma de 1/36 s desde
  `collisions_pass`, tabla `0x3fcd8`): 1 sprite `0x39d1c` (con desplazamiento opcional en el marco de un coche,
  `+0x3c..+0x50`), 4 sprite con amortiguación en coma flotante `0x39fe8`, 5 pieza `0x3b744` (dos triángulos
  planos que giran y sueltan humo de `DARKSMOK`/`EXSMOKE` con probabilidad 0,15), 9 cuadrilátero animado `0x3d808`
  (modo 0x13: textura mezclada con la SHD) y 11 destello `0x3e6ec` que orbita al coche. Ranuras de textura
  (`0x1fe8e0`): 2 DIVSPR, 3 LIGHT, 4 SMOKE, 5 DARKSMOK, 6 BOOM, 7 EXSMOKE. Prioridad cuando no hay hueco: `0xc47f0`.
  - Explosión (`sub_0003a788`, en el eje trasero): fotograma 0 = bola `BOOM` (tipo 4, tamaño 0x89800) y 15 piezas
    con velocidad aleatoria; fotogramas impares hasta 20 = más bolas desplazadas ±50; cada fotograma, con
    probabilidad `1 − (0,8 + 0,0025·f)`, humo `DARKSMOK` que sube 2–6.
  - Hundimiento (`sub_0003c208`): 12 cuadriláteros de salpicadura (`LIGHT.TEX`, tabla `0xc4060`, 31 fotogramas) en
    la posición del coche (`0x3c2a0`).
  - Ambos, 20 fotogramas antes de reaparecer: 10 destellos `DIVSPR` alrededor del coche.
  - Turbo (`sub_0004e97c`, cada fotograma con el turbo activo): dos llamas `DIVSPR` (`0xc4550`) en la trasera
    (`car+0x5b8`), humo de nitro cada vez que `car+0x54c & 0xa == 0xa` en modo 2, llama doble alterna para el
    coche 3 y cuatro humos de escape para el coche 8 según la carga.
  - Puntos (caras de tipo 1 = `TAB[c·256]`, tipo 2 = `SHD[c·256 + dst]`, un píxel, z > 200): tipo 2 `0x3a2b4`
    (`+0x24` color ×1024 que cambia `+0x28` por fotograma, `+0x2c` modo) y tipo 8 `0x3a498` (4 puntos a ±10 que
    giran). Chispas de roce `0x3ea00`: rueda sobre `0x5a` a más de 100 km/h en el suelo → temporizador `car+0x33c`
    = 6 y 2 (humano) o 1 chispas por fotograma. Ruedas sobre 6/0x10/0x1a a más de 5 km/h (`0x3ee70`): 2 chispas
    tipo 8 por rueda, color 123.
  - Golpe entre coches (`0x2ea44`, impacto medio > 15): 6 piezas en el punto de contacto (`cuerpo+0x1c0/+0x1c8`).
  - Relámpago (tipo 6, `0x3bdac`, vida 6): malla fija `0xbfe90` (17 vértices, 20 caras planas color 251 y un
    cuadrilátero tipo 0x14 de fila SHD 0), giro aleatorio cada fotograma, 8 chispas y 4 humos al nacer; destello
    de paleta (`sub_00051508`) 0,4 / 0,8 / 0,4 hacia blanco, normal y 0,5 hacia (0x22, 0x22, 0x31).
  - Alud (`0x3db4c`, tipo `0xa`, `car+0x35c`): lo dispara el objeto PLC `0x130` bajo el centro del coche cerca de
    un objeto especial (`0x12c–0x15d`, lista `0x1fef58`). 30 fotogramas: x y z + min(30 − f, 10), altura +4 / −10,
    giro +0,2 / +0,25 rad; se oculta en el 30 y reaparece como los demás.
- **Clima** (`weather.c`): `0x26154` asigna 1 lluvia (CANADA), 2 nieve (AUSTRIA), 3 viento (USA). Zonas por el
  tramo del jugador (`0x400f4`): lluvia 45 < s < 120, viento 0 < |s| < 30, nieve 28 < |s| < 100. `0x403c8`: una
  pareja más cada 2 unidades de reloj hasta 50 (lluvia: dos líneas de 60 de alto, 800 de separación; nieve: dos
  sprites `DIVSPR` (0,117)–(31,148) de escala 100) en un mosaico de 1000 × 1000 repetido 3 × 3 alrededor de la celda
  de 1000 del coche, 200 por debajo; la lluvia baja 25 y deriva 5 por fotograma, la nieve baja 5 y oscila
  sin(fase)·20; la lluvia oscurece la paleta pareja·0,01 hacia (0x22, 0x22, 0x31). Con lluvia, 1/200 por fotograma
  cae un rayo sobre el objeto de un tramo 0–3 por delante (sonido de pista 2).
- **Sonido** (`audio.c`, `sound.c`): bancos de `0x2a4ec` (grupo 0 = ROLL, SKID, COLL, BOOST, DIV; 1 = pista;
  2 = coche), índice = prefijo `NN_`. `0x49a8c(grupo, sub, índice, bucles, vol, Hz, x, z)` multiplica el volumen
  por max(0, (1500 − 0,8·d)/1500) respecto al coche visto. Motor (`0x50824`): `i = 2·car+0x288` (1..199);
  tonos `ENGINE.INF[0|400 + i]·600 + 20000` Hz, volúmenes `[200|600 + i − 1]·650`. Revoluciones (`0x4dfbe`):
  `índice_final_par/tope_marcha·66` (topes humanos `0xc4fe0`, IA `0xc5400`), con reglas distintas de aire,
  salida, derrape y fin descritas en la verificación de revoluciones. Rodadura (`0x50464`):
  clase `tipo mod 10`, solo audible el coche visto. Derrape: clase `tipo mod 9`, `0xc3578`/`0xc35a0`. Emisores de
  ambiente: objetos PLC `0x190 + n` (sonido `n`), volumen 0,75·(1500 − d)/1500.
  `human_input` (`0x2c5b8`) solo mapea dirección, acelerador, freno, marchas, turbo y cámara trasera; no contiene
  una tecla de recolocación. El reinicio observado (`0x43e48`) pertenece al menú de pausa y reinicia la carrera,
  no recupera un coche durante ella. No se ha verificado aún una recuperación automática al caer o volcar.

## Entrada de teclado (`sub_0002c5b8` = `human_input`, decodificada)

- Registro de control por jugador en `[0x1fefd0]` (`0x4c` B): `+8` tipo (0 = humano), `+0xc` cambio (0 manual, 1 automático), `+0x18` auto-acelerar,
  teclas (consultadas con `sub_00055c28`): `+0x1c` izquierda, `+0x1d` derecha, `+0x1e` acelerar, `+0x1f` frenar, `+0x20/+0x21` marcha
  arriba/abajo, `+0x22` turbo, `+0x23` cámara trasera (`car+0x5a4`).
- Acelerador y freno digitales = 100; dirección **digital** −100 / +100 / 0 en `car+0x344` (sin rampa; la suavidad la da el
  integrador `steer`). `car+0x9a4` = ±50 y `car+0x9a8` = ángulo del tramo + constante (`g_sections+4`, o `+0x10` si tramo < 0).
- `+0x18 == 1` y sin freno → acelerador 100. Si `car+0x558 > 0` (solape con otro coche) la dirección se invierte.
- Turbo: tecla y `+0x354 == +0x35c == +0x358 == 0` → petición `car+0x52c = 1`; pasa a 2 si `car+0x3a4 <` constante y `+0x530 == +0x53c == 0`.
- `car_input` (`0x4d5f4`) recibe (acelerador, freno, dirección, turbo) antes de seleccionar la marcha siguiente.
  Cambio manual (`+0xc == 0`): después, flancos de `+0x20/+0x21` suben/bajan `car+0x27c` (0–2, incluye atrás).
  En automático (`+0xc == 1`) el caller llama a `car_gearbox` `0x502c4` si marcha != 0; existe una excepción
  `car+0x604 == 1`. El port conserva flancos como solicitudes `IgnInput.gear_shift`, no emula el sondeo de teclado.
- `sub_00031858` (llamada al solapar, `0x2d4d9`/`0x312fe`) solo cambia la malla del coche (`[0x1fecb4]`, 'Error while changing car mesh')
  y reproduce un sonido; `sub_0002ea44` (impacto > 15) es solo efecto visual y sonoro.

## Caja de cambios y dificultad

- `sub_000502c4(coche)` (automático): `idx = min(abs(trunc(v_long·3.6)), 199)`, producto con el
  `double` original 3.6 redondeado a binary64 por la palabra de control `0x127f`. Usa curvas **compartidas del modelo 0** en `0x1fb6d8` (marcha 1) y `0x1fb868`
  (marcha 2), comparación `int16` con signo, sin dividir el índice por dificultad.
  Si `par1[idx] > par2[idx]`, objetivo 1; en caso contrario objetivo 2 dentro del intervalo estricto
  `(-1.5707963267949, 1.5707963267949)` o por encima de `4.7123889803847`; fuera, objetivo 1.
  El ángulo es `car+0x90` (velocidad relativa al rumbo). El empate de par favorece 2 solo en esos intervalos.
- `ctrl+0x10` cuenta llamadas cuyo objetivo difiere de la marcha actual; se borra si coinciden.
  El cambio ocurre **cuando contador > retardo**, no al igualarlo. No se borra al cambiar:
  la siguiente llamada con objetivo coincidente lo borra. El bucle de inicialización `0x22b94..0x22cb5`
  escribe retardo `ctrl+0x14 = 30` para todos los controles. El port inicia contador 0 de forma determinista;
  el origen del valor inicial del contador no está verificado. La colocación al reaparecer
  borra el contador en `0x41e21`, conservando el retardo; este reset sí tiene oracle ejecutable.
  El sonido de cambio se ejecuta solo si `ctrl+4 != 1` y queda fuera del oracle.
- Dificultad `[0x1fedd8]` ∈ 0..3 → `[0xc5470]` = {1.0, 1.15, 1.3, 1.3}, factor del agarre trasero (`car_update` `0x30811`).

### Dispatcher de pedales y selección de marchas: verificación ejecutable

`tests/input_original.py` ejecuta bytes LE sin parchear ni sustituir callees en Unicorn/x87 y compara
salidas enteras **exactamente**, tolerancia cero, contra `tests/car_kernels.c` y las APIs de producción:

| Modo | Frontera original | Salida | Casos / pasos |
|---|---|---|---|
| D | `0x4d5f4..0x4d6a9`, antes del acelerador | acelerador dirigido | 180 / 180 |
| A | `0x4d6e2..0x4d8d8`, ejecuta el freno real | marcha, derrape, skid | 27 648 / 27 648 |
| K | `0x502c4` completo, con `__CHP` real | marcha y contador por llamada | 5 526 / 72 738 |
| H | `0x2cb29..0x2cb4b` y `0x2cbb2..0x2cbd2` | subida/bajada manual acotada | 6 / 6 |

Resultado: **33 360 casos / 100 572 pasos**, cero diferencias. El informe
`build/regression-clean/input-original-report.json` conserva SHA-256 del original, runner, fuentes y fixtures,
entorno, fronteras, exclusiones y ramas obligatorias ejecutadas. La allowlist rechaza instrucciones/escrituras
fuera de ámbito y verifica retorno/stack/estado x87. `ctrl+4 = 1` evita sonido por la rama original, no mediante stubs.
Se prueban ambas modalidades, tres marchas, pedales simultáneos, vecinos de umbrales de pedales/velocidad/ángulo,
índices 0..200 con signos y vecinos binary64, extremos `int16`, empates y secuencias de velocidad/ángulo/contador.

- D conserva la marcha **previa** para la fuerza: en marchas > 0 usa acelerar; en marcha 0 manual usa
  `-acelerar`, en marcha 0 automática usa `-frenar`.
- A manual aplica freno sin elegir marcha atrás. Automático limpia primero el freno; en los mismos intervalos
  angulares y con velocidad longitudinal **> 2.0**, frena. Si no, freno **> 5** selecciona 0.
  Después acelerar **> 50** con marcha 0 selecciona 1; por ello ambos pedales pueden acabar en 1.
- El tick del port aplica acelerador/revoluciones/turbo con la marcha previa, luego dirección/freno y volante,
  y finalmente cambio manual o automático. `tests/gearbox.c` comprueba el orden y que la caja no use las curvas
  locales ni la dificultad. `forward_gear` es estado de UX/traza del port, no entrada original de selección.
  La traza actual v5 incluye contador, retardo, relojes y registro de vueltas (112 campos); el replay rechaza versiones anteriores.

**Alcance limitado, no dispatcher completo**: no verifica sondeo de teclado/joystick, generación y latch de
flancos, sentinel de entradas cacheadas `0x1f02`, grabación/replay original, audio, callee especial del modelo 8,
generación de las curvas compartidas, inicialización de configuración, efectos de turbo y generación de solicitudes,
bandera de inactividad, frecuencia completa de llamadas/callers ni carrera DOS. Las curvas de K son
entradas idénticas suministradas; la generación que usa el port tiene evidencia estática, no una prueba nueva
contra los bytes de `torque_spline`. El reset de controles durante reaparición se verifica por separado abajo.

### Solicitudes de turbo: verificación por fragmentos originales

El modo J de `tests/input_original.py` compara `ign_car_turbo_request` de producción:
**10 728 casos / 82 440 pasos**, eventos/modo/espera enteros exactos, sin tolerancia.
Con D/A/K/H el informe suma **44 088 casos / 183 012 pasos**. Se suministran peticiones 0/1/2,
modos 0/1/2, humano/IA, fin 0/1, carga vecina binary64 de 20 y 100, reloj negativo/cero/positivo
(incluidos vecinos de cero), espera negativa/cero/1/2/29/30/31 y secuencias de 65 llamadas
retenidas, alternadas con liberaciones y alternadas 0/1/2. La carga no se consume en J:
el kernel aritmético U se verifica por separado.

- Petición 1 (`0x4d8e1`): inicia modo 1 solo si carga **>= 100**, modo 0,
  reloj `[0x1fb4e8] < 0` y `car+0x528 == 0`; limpia espera `+0x548`.
- Si no inicia: modo 0, espera **<= 0**, carga **> 20** y control humano (`ctrl+4 != 1`)
  alcanzan el aviso de negativa. Tras sonido, `0x4da8f` escribe espera 30.
- Solo petición 1 ejecuta `0x4daa1..0x4dac4`: espera positiva decrece en uno,
  incluso con modo activo. Por ello una negativa termina con **29**, no 30.
  Una liberación (petición 0) conserva la espera; espera negativa no decrece.
- Petición 2 (`0x4dad1..0x4daf2`): fuerza modo 2 y espera 0, sin gates de carga,
  modo previo, fin ni reloj. La generación de esta petición por el caller queda fuera de J.

**No es una ejecución continua del dispatcher.** La decisión se ejecuta desde `0x4d8e1`
hasta las fronteras `0x4d949` (llamada a efectos), `0x4da41` (preparación de audio de negativa),
`0x4daf2` (efectos forzados), `0x4daa1` o `0x4db89` (sin evento).
La negativa compone independientemente la instrucción original `0x4da8f..0x4da9a`,
con dirección de coche suministrada, y petición 1 compone el contador desde `0x4daa1`
hasta `0x4daca`/`0x4db89`. No se parchean bytes, no se instalan stubs ni se ejecutan
los callees de sonido/efectos. El informe registra las fronteras y cantidad de ejecuciones de cada fragmento,
hashes y ramas obligatorias; cada fragmento comprueba EIP/ESP/x87 y allowlist de escrituras.
La composición supone que los callees excluidos no alteran los campos de decisión: sus posibles
efectos secundarios, handles de sonido y ejecución completa siguen **sin verificar**.

El tick del port mantiene aritmética de carga **antes** de arbitrar la petición.
`tests/gearbox.c` comprueba activación/consumo en ticks sucesivos, agotamiento, liberación,
negativa antes de salida y exclusión de IA. `turbo_started` es un evento del port consumido por
el caller de efectos; J verifica la frontera que lo origina, no la implementación de esos efectos.
La generación humana y su latch se verifican por separado en P; quedan pendientes
sondeo, reloj original, frecuencia de callers y carrera DOS.

### Generación humana de turbo y latch: verificación por fragmentos

El modo P de `tests/input_original.py` compara `ign_car_turbo_input` de producción y
el commit de `turbo_key`: **5 184 casos / 31 104 pasos**, petición/latch enteros exactos.
El informe D/A/K/H/J/P suma **49 272 casos / 214 116 pasos**.

- Se entra en `0x2c93a`, después de `key_poll` (`0x55c28`), con su resultado suministrado
  en EAX y el índice activo 0 en `[0x1feff0]`. **Solo EAX == 1** representa tecla pulsada.
- `0x2c953..0x2c96e` escribe petición `car+0x52c = 1` si los tres flags de wreck
  `+0x354` (explosión), `+0x35c` (deslizamiento) y `+0x358` (hundimiento) son cero.
  Cualquier bloqueo o tecla liberada escribe 0 (`0x2c985`).
- `0x2c99e..0x2c9ca`: con `car+0x3a4 < [0xa8cf6]` (**0,25**, double),
  petición 1, latch anterior `+0x530 == 0` y modo `+0x53c == 0`, cambia petición a 2.
  No consulta el reloj global de salida, carga ni flag de fin. Tiempo negativo también
  cumple el umbral; exactamente 0,25 no lo cumple.
- Se detiene en `0x2c9d4`, antes de otros controles/cámara. Independientemente se ejecuta
  `0x2cc9b..0x2ccb5`: copia petición `+0x52c` a latch `+0x530`, conservando **0/1/2**,
  no un booleano. Cada uno de los dos fragmentos se ejecutó 31 104 veces.

Los fixtures combinan muestras 0/1/2, latch y modo 0/1/2, las ocho combinaciones de los
tres flags, tiempo -1, vecinos binary64 de cero y 0,25 y tiempo 1. Cada caso ejecuta
seis pasos con tecla constante, liberación/pulsación alternada o muestras cíclicas 0/1/2.
Se exige cobertura de todas las decisiones, fronteras EIP/ESP/x87, allowlist de escrituras,
salida exacta y hashes de código/runner/fixtures. Las pruebas de contrato rechazan muestras,
estados, tiempos no finitos, patrones y cantidades fuera del dominio declarado.

**Composición, no ejecución continua de `human_input`.** No se parchean instrucciones ni
se sustituye `key_poll`; tampoco se ejecutan cámara, dispatcher, caja o audio entre generación
y latch. Se suministra al segundo fragmento la petición del primero. Las referencias directas
localizadas a `+0x52c` no muestran escrituras dentro del dispatcher, pero efectos indirectos
de callees excluidos no quedan demostrados por esta prueba.

En el port, `wrecked` agrega los tres estados originales; P compara cada combinación original
con ese agregado, no sus transiciones físicas. El tick genera la petición humana **antes**
de aritmética de carga y arbitraje y escribe el latch **después** de selección de marchas.
Esto evita convertir una nueva pulsación en petición 2 porque el turbo se agotó durante el tick.
`tests/gearbox.c` comprueba ese caso, pulsación previa a salida, tecla mantenida, liberación,
nueva pulsación, umbral exacto, ausencia de gate de fin y separación de solicitudes de IA.

El tiempo de esta API es explícito y finito. Producción usa ahora `car.race_elapsed`;
su conversión y actualización se verifican en el bloque siguiente. También quedan fuera
el sondeo real y su frecuencia, otros dispositivos/IA, reset original de latches, callees
intermedios y ticks que retornan anticipadamente por wreck. No se añade estado persistente:
`+0x52c` se representa por una variable local. El trace actual es v5/112 campos.

## Reloj por coche y transición normal de salida: verificación ejecutable

`tests/race_time_original.py` ejecuta instrucciones LE sin parchear en Unicorn/x87 y
compara ocho campos exactamente con producción: **18 450 casos / 74 178 pasos**.
El informe `build/regression-clean/race-time-original-report.json` conserva hashes,
cobertura de instrucciones y fronteras, y límites explícitos. Modo C del runner:
conversión, countdown, reset normal, reloj por coche, generación humana y latch.

- `frame_time 0x2b7d6..0x2b7f9`: resta `g_timer_ms (0xab5dc)` menos
  `g_clock_origin_ms (0x1fef04)` en 32 bits con wrap, interpreta el resultado con signo
  y multiplica por **0,001**, guardando segundos binary64 en `0x1fb540`.
- Decisión de `race_clock 0x2b857..0x2b8b0`: si el contador previo es <= -1,
  lo conserva; si no, la misma diferencia por **0,036** alcanza salida al ser >= 200.
  Con origen cero, 5555 ms sigue en countdown; 5556 ms alcanza salida normal.
- En la frontera normal `0x2b8cd`, el oracle compone el sample ya calculado,
  la función completa `reset_race_times 0x2a410` y el commit `0x2b8d9..0x2b906`.
  El reset mueve el origen a los milisegundos actuales, pero **no recalcula** los
  segundos globales: la actualización de ese frame copia todavía el sample previo.
  Borra `car+0x3a4`, no `+0x3ac` ni `+0x3d4`; los coches congelados conservan estos últimos.
- `update_car_clocks 0x4c738` completo, con un coche: no escribe si `out (+0x528) != 0`
  o si `lap (+0x374) >= 3` fuera de modo 3 (eliminación). Wreck no congela el reloj.
  Escribe `race_elapsed (+0x3a4) = sample` y
  `lap_elapsed (+0x3ac) = sample - lap_origin (+0x3d4)`.

Los fixtures cubren vecinos de 250 ms/salida, modos 0/3, laps 0/2/3/4, out/wreck,
teclas/latches 0/1/2, turbo 0/1/2 y wrap signed/unsigned32 en secuencias.
La prueba es **composicional**, no una ejecución continua del frame completo.
Excluye salida forzada anticipada, pausa, interrupciones DOS, asignación/bucles
multicoche, otras estadísticas del reset, respawn y frecuencia/interleaving de callers.

**Desviación temporal explícita**: el port genera milisegundos virtuales deterministas
`floor(ticks * 1000 / 72)` y toma el sample al final del tick compartido; no emula
el timer DOS ni la separación original entre frames gráficos y lotes físicos.
Salida virtual: tick 400 = 5555 ms (no salida), tick 401 = 5569 ms (salida con sample
5,569 s); el siguiente tick toma 14 ms desde el nuevo origen.
Turbo humano, HUD total/vuelta y tiempo final del fantasma usan los relojes del coche.

## Inicialización y reaparición: reset de controles verificado

`tests/respawn_original.py`, modo X del runner, compara **21 campos exactamente** con
instrucciones LE sin parchear ni reemplazar callees: **7 422 casos / 31 344 pasos**.
El informe `build/regression-clean/respawn-original-report.json` conserva hashes de original,
runner, fuentes y fixtures, ramas ejecutadas, fronteras y exclusiones. Las escrituras están
acotadas; los bytes restantes de coche y control se comparan antes/después de cada reset.

- Inicialización `0x27451..0x27497`: carga `+0x534 = 100`, modo `+0x53c = 0`,
  espera `+0x548 = 0` y contador de efectos `+0x54c = 0`. El fragmento conserva latch
  `+0x530`; no se afirma que la asignación/inicialización completa lo conserve.
- Explosión `0x3a7f5..0x3a812`, hundimiento `0x3c26e..0x3c28b` y deslizamiento
  `0x3dbb4..0x3dbce`: carga 100 y modo 0 en cada llamada, conservando espera/latch/efectos.
  `ign_race_wrecks` aplica ese refresco antes incluso de una búsqueda de punto fallida.
- Colocación: `0x41de5..0x41e8d`, `0x41e93..0x41e9d`, `0x41ee5..0x41f0d`.
  Marcha `+0x27c = 1`, contador `ctrl+0x10 = 0`, revoluciones `+0x288 = 0`,
  salto `+0x274 = 0`, aterrizaje `+0x278 = 0`, skid `+0x2f8 = 0`,
  chispas `+0x33c = 0` y slip `+0x90 = 0`. Conserva retardo `ctrl+0x14`,
  turbo y los tres relojes. La escritura de suelo previo en `0x41ddf` queda fuera.
  `forward_gear` es estado de UX del port, no salida original.
- Secuencias posteriores: generación P, arbitraje J, manual H o automática K y latch,
  cinco o 33 actualizaciones sobre estado post-placement suministrado. K ejecuta desde
  `0x502cc` hasta antes del audio/commit/epílogo; cuando hace falta, se compone el commit
  `0x50449..0x5045b` conservando los registros de la decisión. Audio no se ejecuta ni sustituye.

Los fixtures incluyen carga 0, vecinos binary64 de 20/100, espera -1/0/1/30,
contador de marcha 0/30/31, modos/latches 0/1/2, humano/IA, out, tecla mantenida,
liberación y pulsación nueva. El retardo 30 no permite cambiar hasta la llamada 31.
`tests/gearbox.c` comprueba la conexión de producción, preservación byte a byte de estado
ajeno al kernel, refresco repetido, colocación sobre un triángulo SRF sintético y fallo
de suelo sin mutación. Estas últimas pruebas son del port, no equivalencia geométrica DOS.

**Exclusiones**: inicialización/asignación completa y configuración inicial del control,
geometría, selección/elegibilidad de wreck (incluido gate de coche terminado y excepción
`car+0x604 == 1`), flags/refill de finalización y FX completos, throttle/consumo de turbo,
origen de solicitudes de IA, audio y frecuencia/interleaving del tick completo.
No se afirma verificar `respawn_place 0x41430` entero ni el ciclo completo de destrucción.
El bloque siguiente verifica la copia del sample global a `lap_origin`, no el interleaving
completo de progreso, geometría y timer del juego.

## Vueltas y registro de tiempos: verificación por fragmentos

`tests/lap_original.py` ejecuta bytes LE sin parchear en Unicorn/x87 y compara
**18 campos, 3 024 casos / 4 112 pasos, tolerancia cero**, con el kernel de producción
`ign_race_progress_sample` (modo L del runner). El informe
`build/regression-clean/lap-original-report.json` conserva hashes del original, runner,
fixtures y fuentes, fronteras ejecutadas, cobertura obligatoria y exclusiones.
La allowlist controla instrucciones/escrituras, ESP y estado x87; además comprueba
preservación del resto del coche, cuerpo y controles.

- `0x2f2be..0x2f378/0x2f3a9/0x2f617`: requiere fase 3, sonda actual cero,
  `abs(car+0x370) == count-1` y `car+0x604 == 0`. Incrementa vuelta y borra fase.
  Con vuelta >= 3 fuera de modo 3, escribe out, copia posición `+0x39c` a `+0x3a0`
  e incrementa el contador de finalizados. El snapshot de posición es distinto del orden
  de llegada propio del port.
- `0x2f3a9..0x2f436/0x2f4f9`: copia `lap_elapsed (+0x3ac)` a última vuelta `+0x3b4`;
  si la vuelta incrementada es < 4, escribe su slot `+0x3bc/+0x3c4/+0x3cc`.
  Copia `g_elapsed_seconds (0x1fb540)` a `lap_origin (+0x3d4)`, **no** el reloj del coche.
  No borra `lap_elapsed`: el siguiente update de reloj calcula `sample-origin`, o queda
  congelado si el coche terminó. Mejora `g_best_lap (0x1fb390)` solo con `<`, para humano o IA.
- `0x2f617..0x2f645`: actualiza la sonda anterior `+0x370` salvo sentinel 10000.
  `0x2fb84..0x2fbdf` acepta, independientemente, el tramo de navegación `+0x364`,
  conservándolo ante sentinel. Los offsets auxiliares `+0x368/+0x36c` no tienen
  contraparte portada en este bloque.
- `0x2fc1c..0x2fd63`: una transición por llamada usando `abs(+0x364)` y
  `q = floor(count/4)`: fase 0 en `[q,2q)` pasa a 1, fase 1 en `[2q,3q)` a 2 y
  fase 2 en `[3q,4q)` a 3. Los tramos de resto fuera de `4q` no activan fase 3.

Fixtures con count 1..23, signos, límites, sentinel, gates, modos 0..3, vueltas 0..4,
vecinos binary64 del mejor tiempo, empates y secuencias de hasta 30 pasos.
Los controles negativos rechazan campos alterados, no finitos, ancho incorrecto,
enteros fraccionarios y fixtures inválidos; un fallo sobrescribe informes antiguos.
`tests/gearbox.c` verifica además el adapter de suelo, fin único, dos coches compartiendo
mejor vuelta, actualización/congelación del reloj y reset de los tiempos registrados.

**Desviaciones y límites**: el adapter del port usa `ground_object` para ambas sondas;
el oracle las suministra independientemente, sin probar su geometría. El productor de
`+0x604`, elegibilidad out/wreck del caller, cámara de llegada, avisos localizados,
comparación de récord de carrera `0x2f4f9`, persistencia e interleaving completo están excluidos.
El original inicia el mejor tiempo desde `[0x1ff367 + pista*0x78]`; el port usa `DBL_MAX`
como récord de **sesión**. No es un récord guardado ni prueba de una carrera DOS completa.
La traza v5 añade última vuelta, sonda/gate, posición final, tres slots y mejor vuelta
compartida: 112 campos. Replay independiente 21/21 y smoke de 200 s con siete rivales
7/7, sin demostrar que todas las carreras terminen. La regresión histórica permanece
1/23 (estado 10, imagen 22, ejecución 0); solo `CANADA 3 rivals 40s` cambia respecto
al bloque de reset. La referencia histórica se conserva intacta.

## IA original `0x1aea0`: álgebra exacta (portada en `ai.c`)

Estado (0x7c B, copiado de `0xbfb28`): `+0x00`=0.9 (×1/×0.7/×0.4/×0.4 según dificultad 0/1/2/3), `+0x08`=0.8, `+0x10`=0.4,
`+0x18`=0.6, `+0x20`=0.75, `+0x28`=0.5, `+0x30`=0, `+0x38`=5.0, `+0x40`=0.6, `+0x48`=0 (int32).
Con `k` índice de lookahead, `n=min(k+1,5)` y `w=(s38·0.4+0.6)·1.6`:
`a8 = 1+(grip+0.8)·Σ(turn·(w·turn)/n)`; `T = ((1+s00)·0.5+0.3)·x90/(s10·6/(3k+6)+1)/((a8-1)·0.4+1)`;
`x88 = max(0,s18·60+120-kmh)/(s18·60+120)·s18·1.5+1`; `T = (T·x88+(x88-1)·0.25)·100` (máx 100);
freno `(kmh-ov)·50·0.01` si `ov·0.27777777777777773 < v`; marcha atrás: acelerador 0, freno `min(1.5T,100)`;
turbo si `s20·0.13+1.07 > a8`, `off>3` y `|dir| < s20·5+3` (o `ov==500` en dificultad 0).
El factor de pendiente decodificado es `x90 = sqrt(max(1, 1 + ΔY·slopeinflu·1.1/(40·off)))`; `ΔY` se calcula entre
las coordenadas Y (`record+0x06`) de los registros de puerta seleccionados por la ruta y el carril a distancia `off`.
El port aplica esta fórmula a las alturas izquierdas de los slots originales. La aproximación anterior con
alturas medias no se usa. Los placeholders inválidos se resuelven como desviación nativa explícita, descrita arriba.

## IA original `0x1aea0`: etapa de salida (decodificado)

- Estado por coche `[0xeb7c0]` (0x7c B): `+0x20` agresividad ¿?, `+0x28` radio de alcance (×`[0xa7b30]`), `+0x58` valor lateral del
  propio respecto a los bordes, `+0x60` contador de atasco (i32), `+0x74` dirección calculada (double).
- Rival más cercano: mínimo de `hypot(Δx,Δz)` sobre los demás coches (`0x1c12d`). Si el ángulo relativo (atan2) menos el rumbo
  ∈ (`[0xa7b20]`,`[0xa7b28]`) y la distancia < radio, se mezcla una corrección de esquiva en `dir` (`0x1c2a2–0x1c409`).
- Dirección base: `dir = Δángulo(rumbo objetivo − rumbo coche) · k · (1 + (aggr+…)·…)`, ×`[0xa7af8]` si modelo 5/8, ×factor por
  dificultad `[0x1ff164]` (1: `[0xa7b00]`, 2: `[0xa7a70]`, 3: `[0xa7b08]`); recorte a ±100. Se invierte si `car+0x558 != 0`
  (aplastamiento) y `car+0x558 < 32.4 − 9·dificultad`.
- Atasco (`0x1c630..0x1c777`): si `abs(error_rumbo)>1.4` **o**
  (`v_longitudinal<6.944444444444445` **y** `airborne==0`) **o** (`contador>43.2` **y** zona especial),
  `+0x60 += N` con N coches **totales**; en caso contrario se pone a cero. Si el resultado es `>=151.2`,
  también se pone a cero. La velocidad lleva signo, no valor absoluto. Zona especial: USA `84<abs(sección)<95`
  y `side<0`; AUSTRIA `61<sección<65` **con signo**; ICELAND `146<=abs(sección)<=148`.
  Reversa en `43.2<contador<97.19999999999997`, sin giro antes de `53.99999999999999`;
  `97.19999999999997<contador<108` anula la dirección. Se conserva la anulación en el aire.
  El port ya no añade edad/espera artificial. El error original mezcla rumbo y velocidad con factor 0.15 y
  usa el ángulo TRI `record+0x2e`, ya calculado y consumido por el port. El oracle de atasco suministra
  el error; geometría/mezcla tienen sus oracles separados y `ai_drive_original` prueba además el controlador completo.
- Turbo: si `car+0x534/538` == 100.0 (turbo lleno), sin `car+0x270`, `+0x20·[0xa7bb8]+[0xa7bc0] > curva` y la curva lejana > 3, se
  llama a `car_input` con turbo = 1 (`0x1cb12`), salvo exclusiones por pista/sección.
- Salida: `car_input(eax=acelerador, ecx=turbo, edx=freno/atrás, ebx=dirección)` en `0x1cbf9`;
  el índice del coche se conserva por el caller en el estado global de entrada. Al final `+0x54 = trunc(acelerador)`
  y `+0x68 = abs(sección)`.
- Búsqueda de carril (`0x1decc`), consumo de slots, pendiente y esquiva ya se contrastan en conjunto
  (véase «Controlador completo y tablas originales»); quedan fuera los efectos del callee `car_input`
  y el interleaving de una carrera DOS completa.

## Planificador de IA y replay de controles (`cars_update_all`, parcial)

`[0x1fed68]` (paridad) y `[0x1fed8c]` (selector) empiezan a cero. En fase 0 se incrementa el selector, con
wrap al número **total** `[0x1fefe4]`, y al final se alterna la paridad (`0x2dd99..0x2de0a`).
La fase 1 aplica controles después de física/colisiones: decisión nueva solo para el índice elegido y
`clock<0 || clock>170`; durante la cuenta atrás o en otros índices se repiten los controles anteriores.
No se saltan los slots de humanos ni coches inactivos: controles a 36 Hz, decisiones a 36/N Hz.
`progress_lock==1` habilita el control de IA; otros valores no lo hacen por sí solos.
La caja automática precede tanto a la decisión como al replay, también en reversa con ese lock.

`car_input` con EAX=`0x1f02` recupera cuatro i32 de `[0x1dade0 + índice*16]`: acelerador, freno, dirección y
solicitud de turbo (`0x4d5f4..0x4d655`). El port conserva la caché por coche y el argumento de turbo 0/1/2
ya resuelto: el replay no vuelve a muestrear la tecla humana, ni repite cambios manuales de marcha.
Una decisión nueva tampoco cambia la configuración de caja manual del conductor humano.
Los dos bucles del ejecutable conservan la última entrada humana para una transición a `progress_lock==1`.

`tests/ai_schedule_original.py` ejecuta fragmentos LE sin sustituir callees: 17 640 casos sin diferencias en
selección/fase/gates y atasco; 32 comprobaciones adicionales de caché multicoche. Se detiene antes de los
callees de decisión, replay y caja, que no quedan verificados por este oracle. El gate temprano de wreck
se aporta como fixture; `car+0x350` sigue desconocido y **no se equipara a `out`**.
`tests/ai_schedule.c` prueba integración nativa, RNG, replay, orden de caja y física sin controles.
El contrato headless comprueba la cadencia de entrada del autopilot con N=1..8 en el ejecutable.
Conducción humana recta/turbo: 14/14 comparaciones exactas frente al binario anterior, 576 ticks y 112 campos.
Replay independiente: 21/21. Smoke: siete pistas, 200 s y siete rivales, dos ejecuciones con estados, traza y
framebuffer idénticos y sin valores no finitos. Persisten atascos: no prueba carreras completas en DOS,
geometría fiel de conducción ni equivalencia de toda la IA. Contra el baseline histórico pasan 1/23:
22 casos difieren en estado y 22 en imagen, sin fallos de ejecución; había divergencias previas y el
planificador cambia deliberadamente la conducción de IA. No se regenera ese baseline.

## Hipótesis (pendiente)

- **TAB**: cómo elige el rasterizador el nivel de sombra (el byte alto de `flags` es casi siempre ≥ 64, así que no basta). **SHD/PAN**: función exacta.
- **MSH**: `flags >> 16` = tipo de superficie (asfalto, tierra…; usado por la física). Bits bajos (`0x11`, `0x13`, `0x15`) pendientes. **PLC**: tipo `0xfa` = objeto especial tratado en `sub_0002abdc`.
- **Coche** (`sub_0002ed00`, ~2300 instr., llamado por `sub_0002d008`): suelo bajo 4 ruedas, agarre, cuerpo físico. Integrador portado (ver abajo).

## Física (portada a `src/game/physics.c`)

- Paso fijo **1/72 s**: temporizador en ms × 0.036 (unidades de 1/36 s), un paso cada 0.5 unidades.
- Cuerpo físico separado `[0x1fefe8]` (0x1d0 B/coche), unidades SI; `mundo = metros × 21.76`; km/h = v × 3.6.
- `sub_0001e090`: modelo bicicleta. Guiñada hacia `tan(dir)·v/(a+b)` limitada por `3·Ff/I·dt`; fuerza motriz ≤ `g·agarre_f·m`;
  fricción lateral trasera ≤ `g·agarre_r·dt`; gravedad por pendiente (pitch/roll); derrape con `0.2·v·sin(Δ)`.
- Init (`sub_00026458`): masa `0xc4e20`, inercia 8000, ejes = |rueda.x|/21.76 × 1.5 (del.) / × 2 (tras.).
- Agarre (`car_update`, `0x3070a..0x30874`): si alguna de las 4 superficies queda fuera de 0..29, conserva
  **toda** la media anterior (`car+0x148`, inicial 0,4) y ambos agarres del cuerpo; no sustituye ruedas por asfalto.
  Con todas válidas, media de `0xc33f0[superficie]`. Humanos: delante ×`0xc4dc0[coche]`, detrás además
  ×`0xc5470[dificultad]`. Rivales: delante ×`0xc4e18` (perfil 11, 1,3), detrás además
  ×`0xc5670[dificultad][0xc57d0[coche]]`; **no** usan el multiplicador humano.
  El índice por modelo es `{8,3,10,5,4,6,2,7,9,1,0,0}` y las filas de `0xc5670` son:
  `{1.4,1.37,1.4,1.4,1.33,1.37,1.33,1.29,1.29,1.24,1.24}`,
  `{1.55,1.5,1.55,1.55,1.45,1.5,1.45,1.4,1.4,1.3,1.3}`,
  `{1.7,1.65,1.7,1.7,1.6,1.65,1.6,1.55,1.55,1.45,1.45}` (última fila repetida para dificultad 3).
- Par (`sub_00053e70`): spline de Hermite por marcha (`0xc4fd0`, 4 puntos), 200 valores por km/h; marcha 0 = atrás.
- Acelerador (`sub_0004db90`): `par[km/h] × 1000 × %acel`, suavizado 0.2; sin acelerar `−v·500`.
- Dirección (`sub_000500f0`): entrada −100..100; objetivo `5/agarre × entrada% × límite`.
  Límite 0,3; solo si `|km/h| < vmax`, añade `(vmax−km/h)/div`, usando velocidad **con signo**.
  Suavizado por coche (`0xc4e80/0xc4ee0/0xc4f40`), tasa reducida a la mitad al soltar dirección.
  Analógico y rivales fijan directamente el ángulo; rivales usan límites del perfil 11.
  Dirección visual `visual += (10·0.01·entrada·4−visual)·0.1`.

### Palabra de control x87

La tabla XI de Watcom registra en `0xe706e` la entrada `00 01 94 62 05 00` (cercana, prioridad 1):
`0x56294` salta a `0x6f634`, que hace `fninit` y en `0x6f651` `fldcw [0xe6be0]`, con `[0xe6be0] = 0x127f`:
precisión de 53 bits (double), redondeo al más cercano y excepciones enmascaradas. La otra lectura
(`0x6f8bb`, vía `sub_000821d0`) carga el mismo valor y nadie lo escribe. `__CHP` (`0x55f9c`) usa `0x1f7f`
(truncar, 64 bits) solo alrededor de `frndint` y lo restaura; el operando ya llega redondeado a double.
La cola compartida del rasterizador (`0x7616c`/`0x765c3`, llamada por unas 60 rutinas de spans) baja a
**24 bits** (`and ax, 0xfcff`) mientras dibuja y restaura la palabra al salir.

Todos los oracles fijan `0x127f`, comprueban que coincide con `[0xe6be0]` y que la rutina la conserva.
Con `0x037f` (64 bits) cambiaban 1 435 de 1 658 ticks de `body_step`, 47 973 pasos del coche y 588 decisiones
de la caja de cambios; reloj, reaparición, vueltas e IA no cambiaban. El port se compila con
`-ffp-contract=off`: cada operación x87 se redondea a double y una multiplicación-suma fusionada no.

### Verificación ejecutable de `body_step`

`tests/physics_original.py` carga los objetos LE reubicados mediante el parser existente `tools/le2elf.py`;
no parchea ni ejecuta el archivo original como proceso del sistema. Identidad admitida:
SHA-256 `179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c`.
Se emula x86-32 con Unicorn 2.1.4, EAX = cuerpo, EDX = indicador de aire, `slope_k` double en ESP+4,
retorno centinela y palabra de control x87 `0x127f` (véase [Palabra de control x87](#palabra-de-control-x87)). El cuerpo Watcom se serializa por offsets, incluidos
los doubles no alineados a partir de `+0xac`; nunca se copia la estructura C nativa.

- Constantes originales comprobadas en `0xa7c6c..0xa7c94`: `1.570798`, `1/72`, `9.81`, `0.2`, `2`, `3`.
  El ángulo lateral conserva `1.570798`, no π/2.
- `0x1e0c7..0x1e10d`: `Ff/Fr = g·agarre_f/r·m·cos(pitch)·cos(roll)`.
  `0x1e135..0x1e233`: objetivo de guiñada `tan(steer)·v_fwd/(a+b)`, o
  `sin(h_anterior−atan2(vz,vx))·velocidad·0.2` en derrape; cambio máximo `3·Ff/I·dt`,
  o `2·Ff/I·dt` en derrape.
- `0x1e25a..0x1e283`: recorte de fuerza **solo por arriba**; igualdad con Ff activa `saturated`.
  `drive` del cuerpo no se sobrescribe. `v_fwd` usa el rumbo anterior; `v_lat` en tierra sin derrape
  se guarda antes de aplicar fricción, y en derrape se conserva.
- `0x1e4a9..0x1e523` y `0x1e584..0x1e626`: proyección de posición:
  `ax = atan(tan(pitch)·cos(h) + tan(roll)·sin(h))`,
  `az = atan(tan(pitch)·sin(h) + tan(roll)·cos(h))`;
  `px += cos(ax)·vx·dt`, `pz += cos(az)·vz·dt`, con el rumbo ya integrado.
- En el aire se conservan velocidades y guiñada; se integra posición/rumbo, se anulan Ff/Fr y se activa
  `saturated`. El `atan2` original `0x647ef` se ejecuta, no se sustituye por Python/libm;
  solo se admite su ruta x87 (`[0xe68c8] & 1 == 0`).
- Se exigen retorno `RET 8`, pila x87 vacía al terminar, control x87 conservado, escritura solo en
  cuerpo/pila y ningún cambio en campos de entrada ni en bytes desconocidos del cuerpo.
  Cada tick tiene límites de instrucciones/tiempo y falla ante salidas no finitas.

190 casos, 1 658 ticks y 35 campos comparados por tick; trayectorias de 144 ticks y entradas aleatorias
con semilla fija, junto con límites `nextafter`, signos, cuatro cuadrantes, reposo y agarre cero.
Se exige ejecutar ramas de giro limitado en ambos sentidos, giro directo, derrape en reposo/movimiento,
recorte motriz/lateral, aire, `FPATAN` y los cinco `D9 D8` del integrador.
Tolerancia por salida: `2e-14 + 1e-14·|original|` (transcendentes x87 frente a libm);
indicadores y campos inmutables exactos. En macOS ARM64 el mayor error observado fue aproximadamente
`3.64e-12` en fuerza (relativo `2e-16`) y `1.07e-14` en posición. Unicorn 2.1.4 calcula `FSIN`, `FCOS`,
`FPTAN`, `FPATAN` y `FSINCOS` con `double` del host (los 11 bits bajos de la mantisa extendida quedan a cero)
e ignora la precisión de la palabra de control en ellas, así que este oracle no reproduce el último bit de un
387 real y no puede exigir igualdad exacta. El informe en `build/physics-original-report.json`
registra el resultado y su procedencia; una ejecución fallida no mantiene un informe de éxito anterior.

No se necesitó alterar la fórmula del port. Esta evidencia solo cubre los estados finitos probados,
masas/inercia/distancia de ejes positivas y la ruta x87 seleccionada; no demuestra equivalencia para
cualquier entrada ni verifica CPU DOS real, otras palabras de control, el runtime FPU software,
el integrador vertical, configuración por coche/superficie, colisiones o el bucle completo de carrera.

### Verificación ejecutable de dirección, agarre y altura

`tests/car_original.py` reutiliza la carga LE y la identidad del oracle anterior. Ejecuta los bytes originales
sin parchearlos y compara con los kernels **de producción** de `src/game/car.c` mediante `tests/car_kernels.c`.
No sustituye llamadas de sonido ni efectos por resultados inventados: la ruta de aterrizaje se detiene en
`0x2e397`, antes de esas llamadas. En las demás rutas verticales y en dirección se exige retorno; el bloque
de agarre termina en `0x30874`.

| Límite | Casos | Pasos | Mayor error absoluto observado |
|---|---:|---:|---:|
| `car_steer`, `0x500f0` | 732 | 5 856 | `0` |
| Agarre, `0x3070a..0x30874` | 672 | 1 536 | `0` |
| Aritmética vertical, `0x2e234` | 252 | 1 008 | `0` |

Total: **1 656 casos / 8 400 pasos**, con tolerancia cero bajo `0x127f`. El objetivo de dirección agrupa
`5/agarre · (entrada·0,01 · límite)` como `0x501e8..0x50203`; otra asociación difiere en el último bit.
Incluye los 12 modelos, ambos controladores, cuatro dificultades, velocidades con signo, límites de velocidad,
rebote y caída, entradas aleatorias con semilla fija, y secuencias válido→inválido→válido en cada rueda.
El oracle exige ramas concretas, límites de ejecución, pila/control x87 conservados y escrituras solo en
campos permitidos/pila. Guarda hashes de entradas, original, fuentes y runner en `car-original-report.json`.
Los controles negativos comprueban salidas perturbadas/malformadas, indicadores, informe persistente de fallo,
fixtures nativos inválidos y rechazo real de un ejecutable con otro digest.

La ejecución anterior a las correcciones pasó dirección y vertical, pero detectó 1 116 diferencias de campos
de agarre: conservación ante tipos inválidos y tablas específicas de rivales. Se corrigieron esas dos reglas,
sin alterar el integrador corporal ni regenerar la instantánea de regresión. La regresión histórica mantiene
1/23 éxitos: ahora 10 casos difieren en estado y 22 en imagen, ninguno falla al ejecutar.

Estos tres bloques **no** verifican todo `car_update`, consultas geométricas, acelerador/turbo, daños de aterrizaje, efectos,
colisiones ni una carrera DOS. Excluye la desviación declarada de subidas con `level_change` (comprobada
separadamente como contrato del port). El umbral `suelo+30 < altura` pertenece al caller `car_update`, no al
kernel vertical. Son estados finitos muestreados bajo x87 emulado `0x127f`, no equivalencia para cualquier entrada.

### Verificación ejecutable de aceleración, freno y turbo

El mismo oracle y runner añaden tres kernels de producción: `ign_car_throttle`, `ign_car_brake` e
`ign_car_turbo`. En estos tres modos no se ejecutan revoluciones ni se sustituyen llamadas de sonido/efectos por resultados
inventados. La aceleración termina en `0x4dfbe`, antes de revoluciones; el freno exige retorno de la rutina
`0x4e1bc` para coche no terminado; el turbo entra en `0x4e3a7` y termina en `0x4e656`, excluyendo efectos.
Se ejecuta la conversión `__CHP` original y se comprueban frontera, ESP, control/pila x87 y escrituras permitidas.

| Límite | Casos | Pasos | Mayor error absoluto observado |
|---|---:|---:|---:|
| Aceleración, `0x4db90..0x4dfbe` | 6 829 | 30 174 | `0` |
| Freno, `0x4e1bc` | 72 | 288 | `0` |
| Aritmética de turbo, `0x4e3a7..0x4e656` | 2 016 | 96 768 | `0` |

Nuevos bloques: **8 917 casos / 127 230 pasos**. Suite de seis kernels: **10 573 / 135 630**.
La tolerancia es cero para todos los campos, con controles negativos para cada modo. El informe conserva hashes del original, fuentes, runner y entradas, además de
cobertura por bloque. Incluye modelos, controladores, dificultades, marchas, límites `nextafter` de índices
de par, patinaje, catch-up y freno, y secuencias de carga/agotamiento/recarga del turbo.

Correcciones verificadas:

- El índice de par trunca `v_fwd*3.6`, toma el valor absoluto, divide por `[1, 1.15, 1.3, 1.3]`, trunca y
  limita a 199. Con la palabra de control del juego (`0x127f`) el producto y el cociente se redondean a binary64
  antes de `__CHP`, igual que en C. Una versión anterior emulaba la truncación del producto extendido: era un
  artefacto del oracle con `0x037f` y cambiaba el índice en fronteras como `10/3.6`. Se prueban ambos signos y
  vecinos de índices 0..210; el control negativo con `0x037f` exige diferencias en ese barrido.
- Patinaje: límites estrictos originales `1.5707963267949` y `4.7123889803847`, no `PI/2` y `3*PI/2`;
  con velocidad en km/h > 2 y acelerador positivo se usa índice cero y derrape 10. Saturación 1 en suelo
  fija derrape 30. El suavizado de fuerza del 20 % ocurre tras la salida: reloj original negativo (−1 en
  carrera), no negativo antes de ella.
- Freno: entrada positiva > 50 bloquea deslizamiento; antes de salida también se bloquea. Con entrada
  > 80 y velocidad longitudinal > 2 se fija derrape 10. El caller del port suministra la entrada de freno
  independiente, incluida la analógica, en vez de deducirla del acelerador combinado.
- Turbo: tras restar 2.5, carga <= 0 limpia carga, modo y frames y retorna **sin** impulso residual.
  El suplemento de baja velocidad es `max(0, float((20-v_fwd)*10000))`, almacenado en binary32.
  Modo 1 usa fuerza por modelo humano o perfil IA 11; modo 2 usa `20000*carga` y añade velocidad en el rumbo
  suministrado al kernel. Inactivo, carga < 100 suma recarga; un exceso se limita en el tick siguiente.

Las tablas de par sintéticas `torque[gear][i] = i-5+gear` son entradas idénticas a ambos lados:
**no** verifican `torque_spline` ni la generación de curvas. Se excluyen hardware mode 2, coche terminado,
catch-up desactivado/race-mode 2, revoluciones y el flag original `car+0x5a8` sin representación en el port.
El turbo evita la rama de sonido de recarga humana usando índice de coche 2; U no verifica solicitudes
(arbitraje cubierto por J en la sección anterior),
sonido/efectos ni que el caller produzca el mismo rumbo original `car+0xf8`. Tampoco verifica todo el
dispatcher de entradas, el coche completo o una carrera DOS.

La regresión histórica permanece en **1/23**, con 10 diferencias de estado, 22 de imagen y cero fallos de
ejecución; no se regeneró el baseline. En esta etapa, el replay entre builds Release independientes pasó
21 escenarios / 56 448 ticks con 99 campos (v2) y framebuffer/estado final idénticos. El bloque de entradas
documentado arriba repitió la prueba con 104 campos (v4); el de vueltas pasa con 112 (v5). El smoke de 200 s y siete
rivales pasa en las siete pistas con ocho resúmenes finitos por pista, pero persisten atascos; ninguno de
estos dos checks demuestra equivalencia de carreras con DOS.

### Verificación ejecutable de revoluciones

`ign_car_update_revs` reproduce el bloque original `0x4dfbe..0x4e1a7`, dentro de `car_throttle`.
El modo R del oracle entra directamente con EBX = índice de coche, ECX = base de coches, índice final
de par en `[esp+0x1c]` y entrada de acelerador en `[esp+0x20]`. El modo Q entra en `0x4db90`,
ejecuta el acelerador y continúa hasta la misma frontera de revoluciones. No parchea código ni omite
llamadas dentro de estas fronteras; exige escrituras permitidas, ESP y pila/control x87 como los otros modos.
Q compara los seis campos del acelerador, revoluciones y el índice final, este último exactamente.

| Límite | Casos | Pasos | Mayor error absoluto observado |
|---|---:|---:|---:|
| Revoluciones R, `0x4dfbe..0x4e1a7` | 25 016 | 134 208 | `1.43e-14` |
| Acelerador + revoluciones Q, `0x4db90..0x4e1a7` | 6 941 | 32 734 | `3.64e-12` |

Total adicional: **31 957 / 166 942**; suite completa: **42 530 casos / 302 572 pasos**.
Q repite deliberadamente los casos de acelerador para comprobar la conexión, no es otro kernel independiente.
Incluye todos los índices 0..199, 12 modelos, ambos controladores y tres marchas; umbrales de entrada
80/81, relación 1.5 y velocidad 60; combinaciones de salida, aire, derrape y fin; valores aleatorios
con semilla fija y secuencias de entrada/velocidad/salida/aire/saturación. Hay controles negativos
de revoluciones e índice, salidas malformadas y fixtures nativos inválidos.

Fórmula y orden originales:

1. `r = índice_final_par / tope_marcha`. Usa el índice después de truncación x87, dificultad, límite y
   patinaje del acelerador, **no** un segundo cálculo desde velocidad. Tope humano:
   `0xc4fe0 + modelo*96 + marcha*32`; IA: `0xc5400 + marcha*32` (perfil 11).
2. Solo si `r <= 1.5`, humano en aire o reloj de salida distinto de −1:
   `revs += (abs(entrada) - revs)/6`; humano en suelo y reloj −1: `revs = r*66`.
3. Solo si `r <= 1.5`, IA en aire **y entrada > 80**: `revs += (entrada - revs)/6`;
   en cualquier otro caso IA: `revs = r*66`, incluso antes de salida.
4. Independientemente de `r`, con derrape 1 y km/h < 60: `revs += (100 - revs)/6`.
5. Después, solo humano terminado (`out = 1`): `revs += -revs/6`. IA no tiene esta reducción.

Constantes: `0xaa2bd` = 1.5, `0xaa2e5` = 66, `0xaa2ed` = 1/6, `0xaa2f5` = 60,
`0xaa2fd` = 100 y `0xaa305` = −1. Estado: `car+0x288` double, derrape `+0x298`,
aire `+0x270`, marcha `+0x27c`, fin `+0x528`, km/h original double `+0x118`.
La primera comparación con las reglas humanas aplicadas a IA falló; la versión corregida pasa.
`ign_car_throttle` devuelve el índice final y el tick lo entrega a revoluciones sin añadir estado persistente.

Alcance: estados finitos muestreados, indicadores 0/1 y reloj de salida −1/0. Se suministran velocidades
representables en binary32 a ambos lados porque `car.kmh` del port es float: los vecinos usados en
60 son `59.999996185302734` y `60.000003814697266`. **No** se verifica la producción original de
velocidad binary64 ni su estrechamiento; un vecino binary64 de 60 puede redondearse a 60 en el port.
R admite coches terminados para probar la reducción aislada; Q mantiene coche no terminado y hardware mode 0:
no verifica el override de entrada por fin/hardware mode 2 ni el dispatcher, selección de marcha,
generación de curvas de par, sonido de motor o una carrera DOS. El replay de 21 escenarios / 56 448 ticks
entre builds Release independientes y el smoke finito de siete pistas se repitieron tras esta corrección.
La regresión histórica sigue en 1/23 (10 estados, 22 imágenes, cero fallos de ejecución), sin actualizarla.

### Auditoría de la decompilación Ghidra

La exportación local `decompiled-20261013-expanded` contiene 1 143 entradas candidatas y 547 avisos.
`decompileCompleted` y la existencia de un archivo C **no prueban** que su flujo esté completo.
Clasificación de las 547 líneas de aviso:

| Categoría | Avisos | Alcance / tratamiento |
|---|---:|---|
| Globals superpuestos | 339 | 339 funciones; tipar estructuras y alias, no crear variables independientes por cada nombre |
| Flujo indirecto / tablas de salto | 144 | 71 funciones; destinos pendientes de verificación |
| Bloques eliminados como inalcanzables | 36 | 9 funciones; contrastar ensamblado antes de descartar ramas |
| Instrucciones inválidas / `halt_baddata` | 19 | 5 entradas; el pseudocódigo puede quedar cortado |
| Sanity de tablas de salto | 5 | Candidatos del entorno `0x8250c` / `0x82576`, no flujo fiable |
| Propagación de tipos no convergente | 1 | `read` en `0x65682` |
| Instrucciones solapadas | 3 | `0x6f43c`, `0x7edb9`, `0x855b1`; revisar límites |

**Causa confirmada de los cortes en física:** Ghidra no traduce `D9 D8` (`FSTPNCE ST(0), ST(0)`)
después de `FPTAN`. Este último deja 1 sobre la tangente en la pila x87; la instrucción siguiente
descarta ese 1. Sitios de `body_step`: `0x1e13d`, `0x1e4b1`, `0x1e4cd`, `0x1e58c`, `0x1e5a8`.
También aparece el patrón antes de los errores de `0x673fc` y `0x67cf6`.
La prueba diferencial ejecuta esos bytes sin modificarlos y contrasta la física C; **no repara**
el lenguaje de Ghidra ni convierte su pseudocódigo truncado en una fuente fiable.

**Candidato falso confirmado:** `0x8121b` empieza en una tabla de palabras
`ffff, 7fff, ffff, 3fff, ffff, 1fff, ...`, después del `RET` de `0x8121a`.
Debe excluirse como autoridad de código; sigue presente en la exportación histórica.
`0x855b1` conserva instrucciones sospechosas/solapadas y diagnóstico pendiente.
Los avisos de flujo, tipos y los otros candidatos no quedan resueltos por verificar `body_step`.
La cobertura de cuerpos de Ghidra (94,926438 %) y la del desensamblado Capstone son métricas distintas,
y ninguna mide cuánto comportamiento del juego se ha reconstruido.

## Colisiones con muros (portadas)

- No hay geometría de muros: un muro es una **arista no compartida de una cara transitable** (tipo < 0x28, 0x64, 0x65;
  en modo salto también 0x6e–0x83).
- **Activación** (`car_update` `0x2f856`–`0x2fa9b`, por rueda; `above` = el coche está ≥ 20 sobre el suelo de esa rueda):
  0x50, 0x5a, 0x5b siempre; 0x51, 0x5c, 0x5d si no `above`; 0x6f si no `above` y sin modo salto; 0x6e, 0x78, 0x79, 0x82,
  0x83, 0x8c, 0x8d sin modo salto. Ponen `car+0x57c = 1`, que `cars_update_all` borra tras el choque. **Modo salto**
  `car+0x274`: 1 al pisar 0x0a–0x13; 0 al pisar 0x00–0x09 sin `above`. 0x5a cubre miles de caras (bordes, taludes y
  medianas): con él el coche rebota contra el borde de la carretera en lugar de salirse.
- `sub_00033ee8` (`collect_walls`): objetos con tipo < 100 a < 600 en x/z (700 en modo salto) y altura cercana a la del
  coche, con un alcance por pista (`[0x1fef10]`: CANADA, USA, AUSTRIA, ICELAND 350; CARIB 200; BRAZIL 450; JAPAN 125;
  2500 en modo salto). El código hace `|car+8 + y_PLC|` (`0x34027`), pero con esa suma JAPAN y CARIB no tendrían ningún
  muro; los alcances cubren justo los vértices de carretera de cada pista con `|altura − y_PLC|` (AUSTRIA 346 < 350,
  JAPAN 71 < 125, CARIB 128 < 200), así que el port usa la resta. ¿Signo de `car+8` en el motor? pendiente.
  Se descartan las aristas que aparecen invertidas en otra cara (tolerancia 16). La lista (≤ 500, `car+0x9b0`) se
  recalcula solo si el coche se ha movido más de 150 en x o z desde la última (`car+0x4834/+0x483c`, `0x2d77a`).
- `sub_0001f708`: corta los 4 lados de la caja del coche (`cuerpo+0xf0/+0x110`, aristas `0xbfe60` = 0-1,1-2,2-3,3-0)
  con los muros; con ≥ 2 cortes usa su media y el ángulo entre los dos más alejados.
- `sub_0001f3fc`: impulso con restitución `0.6×0.3`, brazo `k = cos(θ−a)·r·m/I` limitado a ±0.1, empuje de 0.25 m.
- Tipos de superficie: 0x00–0x09 suelo (agarre `0xc33f0`), 0x0a–0x13 salto, 0x28–0x4f no transitable, 0x50 borde,
  0x12c–0x131 especiales (meta, rampas…), 0x6e–0x83 solo en modo especial.
- Vueltas: `car+0x364` tramo actual, `car+0x378` fase, `car+0x374` vuelta; récord "LAP RECORD" en 6 idiomas.

## Vertical y selección de suelo (portadas)

- `sub_00020814` elige la cara con sonda a altura del coche + 50: coste = distancia si está debajo, distancia² si encima
  (altura media de los 3 vértices); las caras 0x28–0x4f solo ganan si no hay otra. La altura final es el plano exacto.
- `sub_0002e234` (unidades del mundo, arriba positivo): `v += 0.2; h -= v`; si `h < suelo+5` se apoya: en tierra `v = caída del suelo`
  (0 si |km/h| < 50 y |caída| > 4, o si la caída > `|0.05·km/h|` → salto); al aterrizar `v > 8 ? −0.2v : 0.5v`, mínimo −15.
  En el aire si `suelo + 30 < h` (`car_update` `0x311aa`, `car+0x270`); entonces el integrador no aplica tracción.
- Tramos: `sub_00028f08` construye la secuencia desde el objeto de meta (tipo 0x12c) y guarda el número de tramo
  en el objeto del motor (`+0x1e`), que la consulta de suelo devuelve (`+0x38` del resultado) → `car+0x370`. Portado (`src/game/track.c`).

## Tipo PLC y tramos (portados)

- `sub_000247b0` divide el tipo PLC: bits 0–11 = **clase**, 12–15, 16–23 y 24–31 = atributos (tabla `[0x1fef7c]`).
- Clases: 0 salida, 1 dirección de salida, 2/3/7/9 carretera (3 = bifurcación, 7 = salto), 4/5 ramas, 6 unión, 8 ignorado,
  ≥ 50 decorado (150–159, 200, 250, 300 meta, 350–359, 400–413…).
- Centro de un objeto de carretera: media de sus caras transitables cuyo tipo acaba en 0–4 o 6 (si no hay, todas las transitables).
- Recorrido: salida (tramo 0) → objeto más cercano no visitado de clase 2/3/7/9 (la altura cuenta doble). En clase 3,
  rama A por clases 4/6 (tramos +) y rama B por 5/6 (tramos −) hasta la clase 6. Tramos: USA 139, JAPAN 238, CANADA 175,
  AUSTRIA 200, BRAZIL 204, CARIB 207, ICELAND 172. Recorrido de USA comprobado visualmente (circuito cerrado).
- Vueltas (`car_update`): fases 0→1→2→3 en los intervalos de cuartos descritos arriba;
  vuelta + 1 en fase 3 al cruzar de la última sonda de tramo a una sonda cero, con gate libre.
- La cabecera de TRI **no** es el número de tramos.

## Objetos del motor vs PLC

- SRF referencia objetos del motor (`índice × 42`). El motor registra los objetos PLC en orden pero se salta algunos
  decorados, así que solo en USA y CANADA coinciden los índices. Se reconstruye la correspondencia asignando a cada objeto
  del motor el siguiente PLC cuya malla admite todos sus desplazamientos de cara: autocoincidencia 88–98 % en todas las pistas.

## Motor Lisa 2 y rasterizador (portados a `src/render/lisa*.c`, verificados píxel a píxel)

Rango `0x54000–0x60000` (motor) más el rasterizador `0x60e3d` y sus rutinas (`0x70400–0x77000`, `0x824b4`, `0x825b8`,
objeto 2 en `0x90000–0x90a7c`). Verificación: `tests/render_original.py` ejecuta con Unicorn el `render_scene` y el
rasterizador originales sobre los datos reales del nivel (asignadores `0x6482c`/`0x56320`/`0x5629e` interceptados) y
`tests/render_shot.c` / `tests/lisa_prims.c` producen lo mismo con el port. Resultado: cada primitiva aleatoria
(0x11 ×2000, 0x12/0x14/0x15 ×1500, 0x13 ×1000, 0xf ×2000, puntos ×300, línea 0xd ×400 con recorte, sprite 7 ×300)
y cada fotograma completo de pista (7 pistas, cámaras de persecución y barridos aleatorios a 320×200, 640×480 y
800×600, objetos girados, PERSP. POLY sí/no, espejo, zonas y alcance) es idéntico byte a byte.

- **Objetos del motor** (42 B, `[0x24c058]`): `+0` propio, `+4` malla, `+8` ranura de textura, `+0xc` x, y, z
  (`float`, x/z = mundo + 25600, y hacia arriba), `+0x18` ángulos (décimas de grado), `+0x1e` clase, `+0x22` radio,
  `+0x24` zona, `+0x26` siguiente. Rejilla de celdas de 256 unidades (`[0x24c048]`, ancho `[0x24c014]`), cada celda una
  lista con inserción en cabeza. `LI_CREATEOBJECT 0x56b28` (radio −1 = vértice más lejano), `LI_PLACEOBJECT 0x56c74`,
  `LI_MOVEOBJECT 0x56d24` (solo reengancha si cambia la celda), `LI_SETMESH 0x56e9c`, `LI_REMOVEOBJECT 0x56f8c`.
  Un objeto muerto en cabeza de celda oculta toda la celda.
- **`render_scene` `0x56410`**: recorte en 8.8 (`0xc87ec..`), luego capas por banderas de `[0x24c05c]`: `+0x38`
  `camera_angles` (mirar a un punto), `+0x3c` recogida `0x58ebc`, `+0x40` transformación `0x5a2c8` (`0x5a8c8` si la
  focal > 300), `+0x50` manejadores de cara `0x5cb48` (tabla `0xc86c4`), `+0x54` orden `0x5b9c4`; `+0x44..+0x4c` no se
  consultan. Devuelve la lista `[0x24bffc]` que dibuja `0x60e3d` (tabla `0xd7bd0`). Tras crear mips o liberar el motor,
  `[0xc879c]` hace que 99 fotogramas llamen a `0x58864`, que solo lee téxeles al azar de los mips (consume `rand`).
- **Recogida** `0x58ebc`: patrón de celdas en cono `0xc5d8c` desplazado por `0xc5e40` según el rumbo y el cabeceo en
  pasos de 10° (`src/render/lisa_cone.inc`); con alcance `+0xa4` ≠ 0 un cuadrado de `n×n` celdas adelantado n/3 en la
  dirección de la vista. Zona `[0x24bff0]` ≠ 0: se saltan objetos de otra zona ≠ 0. Después, esfera contra la vista con
  la matriz `0x5b7a8` (filas ×16384): solo rechaza por detrás y en vertical.
- **Alcance `+0xa4`**: `sub_0002489c` (`0x25ff1`) pone 25 en CARIB y 0 en el resto; `race_frame` (`0x42130`) lo cambia
  por el tramo con signo del coche visto: CANADA 0x24<s<0x2e → 30, 0x7c<s<0x8c → 35; USA −0x7c<s<−0x71 → 30; BRAZIL
  0<s<0x1e → 25, 0xbb<s<0xc5 → 30; AUSTRIA 6<s<0xe → 35; ICELAND 0x1b<s<0x46 → 30; si no, 0 (JAPAN siempre 0).
- **Zona `[0x24bff0]`** (`race_frame` `0x430b6`): bits 12–15 del tipo PLC del objeto del tramo del coche visto; 0 a
  los 120 fotogramas de terminar.
- **Proyección**: `sx = cx − fx·x′/z` (eje x reflejado respecto al port anterior, que salía en espejo),
  `sy = cy − fy·y′/z`, en 8.8; z se guarda como `4(z + near)` (near 25 a 320×200, 10 en las otras). Filas de cámara en
  `float` truncadas; la variante focal > 300 escala por 256 y desplaza 2. Dificultad ≥ 3 → zoom −1 (espejo: invierte
  el signo del producto de caras ocultas).
- **Tipos de cara** (palabras de malla; `flags & 0xff` = tipo, `flags >> 16` = superficie): 1/2 puntos (3 palabras),
  7/8 sprites (9), 12 (4), 13 línea (6), 15 plano y 20 sombra (5), el resto 11. `0x11` = 17 texturizada,
  `0x13` = 19 mezclada por SHD, `0x15` = 21 calcomanía: los bits bajos de MSH son el tipo de cara.
  - 17 (`0x5cc58` con `+0x58` = 0, `0x5c144` con PERSP. POLY; mips `0x5ef14`/`0x5f670`, alternativa `0x5d084`): rechazo
    por caja, caras ocultas con el producto en 1/16 de píxel, `Σz ≤ 600` fuera, cubeta `Σz>>4`. Con PERSP. POLY, las
    cercanas (cubeta ≤ 720) de clases de carretera (< 100, 200, 300–302) → primitiva 0x14 con peso 4z por vértice,
    fundido hacia la media entre las cubetas 480 y 720; el resto → 0x11 afín.
  - 21 (`0x5e358`, mips `0x60210`): como 17 pero 80 cubetas más cerca.
  - 18/22 → mezcla con la tabla con clave `[0x24c004]` (`T[a·256+b] = a ? a : b`, `sub_11494`: el téxel 0 es
    transparente); 19/23 → SHD `[0x24bfa8]` (`dst = SHD[téxel·256 + dst]`); 22/23 80 cubetas más cerca. Primitiva 0x15
    (perspectiva) en las mismas condiciones que 0x14 salvo superficie 0x4d; si no, 0x12.
  - 20 (`0x5dc08`, palabras tipo, v0, v1, v2, fila): primitiva 0x13, `dst = SHD[fila·256 + dst]` (sombras: fila 0x28;
    derrapes: fila por superficie `0xc34e0`; 0 = identidad).
  - 7/8 sprites: z > 200, cubeta `trunc(2.8 z)>>4`; 7 escala `4/z·tamaño·([0x1feee8], [0x1feeec])`
    (`trunc(W·65535·0.003125)`, `trunc(H·65535·0.005)`) y mezcla por SHD; 8 tamaño fijo `fy·(0.003952569, 0.004820206)`
    en píxeles enteros y mezcla por **PAN** (`[0x24bfec]`): PAN es la tabla de mezcla de esos sprites, no de sombreado.
  - 1 punto `TAB[color·256 + luz]`, 2 punto `SHD[color·256 + dst]` (z > 200, cubeta `3z>>4`); 15 plano `TAB[color·256]`;
    13 línea con color 8.8 cuyo byte alto es la fila de la tabla de la cara, cubeta `(z0+2z1)>>4`, fuera si `z0+z1 ≤ 400`.
- **TAB** (`[0xc484c]`): solo la usan los puntos de tipo 1 y los planos 0xf. Las caras texturizadas 0x11/0x14 escriben
  el téxel tal cual (no hay nivel de sombra por cara): por eso el byte alto ≥ 64 de `flags` no es un nivel, es la superficie.
- **Cubetas** (6000, de lejos a cerca, cada lista desde su última inserción): clase ≥ 100 −92 (clase 210, ruedas, −84;
  tipo 20 −76), recortadas a 0..5999.
- **Rasterizador**: 0x11/0x12 `0x73c58`/`0x744c8` (gradientes `0x90000`, bordes `0x90240..0x90a00`, tramos `0x824b4` y
  mezcla `0x825b8`), 0x13 `0x74d04`, 0x14/0x15 `0x7586c` (x87 a 24 bits emulado exactamente; tras un tramo 0x15 se
  restaura la precisión extendida, aproximada con `double`), 0xf `0x729f3`, 1/2 `0x70400`/`0x70454`, 7 `0x704a0`,
  0xd `0x71cc7`.

## Pendiente / desviaciones

- Suelo del coche (`car_update` 0x2eea4–0x30d98, portado): una consulta por esquina de la caja de la carrocería ×1,1
  (`car+0x2fc/+0x31c`), escalada por `car+0x5cc = 1 + km/h / 220` y 50 por encima del coche; si alguna esquina pisa un
  tipo > 0x13 (`car+0x5fc`) se repiten a la mitad de distancia. Sin `+0x5fc`, los muros 0x51/0x5c (no desde arriba) y
  0x6f (además sin modo salto) no cuentan en la media. Por esquina, límite `100 + 0.5·km/h`; es anómala si su suelo
  difiere del anterior más que el límite o pisa tipo ≥ 0x50. Si son anómalas 1–3 toman `v − suelo_anterior`; si lo
  son las 4 se aceptan (cambio de nivel). En el suelo, cabeceo `= asin(clamp(Δ/recorrido, ±0,95))` y balanceo
  `asin(0,6·clamp(…))` (0x309a7), cada uno a 0,1 rad por tick como máximo; en el aire el morro baja 0,01 por tick
  hasta π/4 (0x30d98). Una sola consulta por tick, antes de `body_step`.
- Desviación: la altura de una cara se acota a la de sus vértices; solo se descartan las caras degeneradas.
- Desviación: con las 4 esquinas en cambio de nivel, una subida mayor que `|0.05·km/h| + 1` no da velocidad vertical
  (taludes 0x5a y huecos de la consulta del port lanzaban el coche a miles de unidades).
- Parrilla de salida (`sub_00026458`, portada): origen PLC del objeto tipo 0 mirando al tipo 1 (ángulo = `−atan2(p0−p1) − π/2`).
  Hueco `i` (tabla identidad en `0xc33d0`): `(i/2)·180 + 200` unidades atrás y 60 al lado según la paridad; con un solo coche,
  200 atrás y centrado. En AUSTRIA la carretera gira detrás de la salida y los huecos 2–7 caen sobre la ladera (tipo
  0x5a), donde la regla de 6 ticks fuera de pista los hace explotar al arrancar: pendiente comprobar si `g_sections[0]`
  es realmente el objeto de tipo 0.
- Comprobación histórica de vueltas con `--sim 200 auto --rivals 3` en las 7 pistas (CANADA: `gate_b` en la rama B).
  Tras corregir el agarre contra bytes originales, la prueba de 200 s con siete rivales termina sin fallos de
  proceso y con resúmenes finitos, pero varios coches siguen atascados; no se mantiene una garantía de completar carreras.
- **Choques entre coches** (portado): `cars_update_all` (0x2d2f8) prueba cada par con |dx|,|dz| < 200 y |dy| < 70 unidades y llama
  a `sub_0001ebb4`: círculo de radio 4 m (`+0x130`), cruce de los 4×4 lados de las cajas (máx. 8 puntos; más = sin choque),
  contacto = media, línea = par de puntos más alejado. `sub_0001e638`: impulso mutuo con `e = 0.6·0.6` (`+0x10`),
  `j_A = m_B·(−(1+e)·v_n)/(m_A+m_B)/(1 + k_A·r_A·cos(a_A−θ))`, `k = 2·r·cos(θ−a)·m/I`, separación 0,25 m.
  Pendiente: el caso de solape (|dx|,|dz| < 25, `sub_00031858`) y los efectos de impacto > 15 (`sub_0002ea44`).
- **Caracter\u00edsticas por coche** (verificadas contra el ejecutable, \u00edndice = modelo `[0x1fefd0 + coche\u00b70x4c]`, 0\u201310
  en `CARS.PLC`): agarre `0xc4dc0`, masa `0xc4e20`, fuerza del turbo `0xc4e50`, direcci\u00f3n `0xc4e80/0xc4ee0/0xc4f40`,
  recarga del turbo `0xc4f70`, curvas de par `0xc4fd0` (3 marchas \u00d7 8 `i32`; `x2` = velocidad punta de la marcha para
  las revoluciones), ejes `|rueda.x| \u00b7 0,04596 \u00b7 1,5 / 2` desde los PLC de las ruedas (`0x1faf40`), inercia 8000,
  restituci\u00f3n 0,6. El \u00edndice 11 no es un modelo: es el perfil de los coches de la IA.
- **Coches de la IA** (`[0x1fefd0]+4 == 1`, `sub_00022d08`): modelo `0xc5520[dificultad][k]` (si coincide con el de un
  humano, el siguiente libre desde el \u00edndice 5); bono de par `car+0x524 = 0xc55c0[dificultad][0xc57d0[modelo]]`;
  par\u00e1metro de IA `+0x28` del modelo `= 0,19\u00b7dificultad + 0,4`. Usan el perfil 11 para el l\u00edmite de direcci\u00f3n (40 km/h,
  /90) y la fijan sin suavizar (como la entrada anal\u00f3gica), la fuerza del turbo (145) y las velocidades punta de las
  revoluciones.
- **Acelerador** (`car_throttle` 0x4db90): índice = `min(199, trunc(|trunc(v*3.6)| / 0xc5450[dificultad]))`; con patinaje
  contra el movimiento \u00edndice 0. Si la curva da \u2264 3, `car+0x520` cuenta y cada `8\u00b7car+0x51c + 15` ticks sube
  `car+0x51c`; si no, se divide entre 2. El humano recibe cada 182 fotogramas `car+0x594 = (posici\u00f3n \u2212 1)\u00b71,5`
  (`[0xc0b50] = 1`). Objetivo = `(par + 0x51c + 0x594 + 0x524)\u00b71000\u00b7acelerador/100`, o \u221225000 con la marcha atr\u00e1s
  puesta avanzando (freno); se acerca un 20 % por tick tras la salida; soltado, `\u2212v\u00b7500`.
- **Revoluciones** (`car_throttle`, bloque `0x4dfbe`): usa su índice final de par, no recalcula desde velocidad.
  Relación con el tope de marcha <= 1.5 da régimen `relación*66` o suavizado 1/6 hacia la entrada;
  humano en aire/antes de salida usa valor absoluto, IA solo en aire y entrada > 80. Después, derrape
  bajo 60 km/h tiende a 100 y solo humano terminado tiende a cero (véase la verificación anterior).
- **Freno** (`sub_0004e1bc`, coche no terminado): entrada positiva > 50 bloquea `body.sliding`, igual que
  reloj de salida no negativo; entrada > 80 y velocidad longitudinal > 2 fija derrape 10.
- **Turbo** (aritmética verificada, `sub_0004e2d8`, llamado por `car_input` tras el acelerador): carga `car+0x534` (0–100, empieza llena),
  modo `+0x53c` (1 = botón, 2 = forzado). Inactivo: recarga `0xc4f70[coche]` por tick (0,4–0,6) hasta 100. Activo: −2,5 por tick
  (40 ticks); al agotarse limpia carga/modo/frames y retorna sin fuerza. Fuerza motriz
  `+= 0xc4e50[humano o perfil IA 11]·carga + max(0, float((20 − v)·10000))`; el modo 2 usa `20000·carga` y suma
  `0.01·carga` a la velocidad. Activación (`car_input` 0x4d8eb, 4.º argumento = 1):
  carga ≥ 100, modo 0, reloj negativo y sin haber terminado. Negativa solo para humano, modo 0,
  espera <= 0 y carga > 20: escribe 30 y decrementa inmediatamente a 29.
  La espera decrece solo en petición 1; soltar conserva el contador. Véase el oracle por fragmentos anterior.
  Recarga: `< 100` suma, `> 100` se fija a 100.
  Arranque turbo (`human_input` 0x2c99e, generación/latch verificados con tiempo suministrado):
  sondeo igual a 1, sin wreck, latch anterior 0 y tiempo `car+0x3a4 < 0,25`,
  con el modo previo a 0, piden 2 y fuerzan el modo 2 sea cual sea la carga, sin gate de salida.
  El latch se escribe tras despacho/marchas. Al empezar un turbo, `sub_0004e688` suelta 10
  bocanadas de SMOKE (`0xc41a0`, tamaño `0x32000`). La IA lo pide con la carga exactamente a 100 (`0x1ca15`).
- **Colocación tras la reaparición** (`sub_00041430`, el fotograma siguiente a `car+0x350 = 1`): mismo punto que el
  deslizamiento (con la búsqueda hacia delante de los huecos −2 de la rama B); una consulta de suelo en el punto y,
  si no hay, otra 20 unidades hacia −x (que también mueve el coche). Copia ese registro a las 4 ruedas, suelo actual y
  anterior = altura del plano, coche quieto (`+0x18..+0x44`, cuerpo `+0x30`, `+0x78..+0x9c`, `+0xac`, `+0xd4`), sin
  inclinación, en el aire, primera marcha y contador de cambio 0 (retardo conservado),
  revoluciones, km/h, modo salto, derrape y chispas a 0, y
  `car+0x364 = car+0x368 =` tramo del objeto (`[0x1fefd8]`).
- **Humo de la pista** (`collisions_pass` 0x3fe4e, portado en `fx.c`): lista `0x1fefac` (tipos PLC 0x15e–0x167 y 0x130,
  clase = tipo − 0x15e) en `sub_0003636c` y lista `0x1fef58` (0x12c–0x15d) en `sub_00036ce0`. Cada uno cuenta fotogramas y
  suelta un sprite tipo 1: 0x15e/0x15f (SMOKE/DARKSMOK, `[0x1fed70]`) cada `rand·3+2`; 0x164 y 0x130 (SMOKE inferior,
  además `[0x1fed64]`); 0x161 humo empujado hacia −z (`vz = rand·2 − 9`, gravedad −0,1) cada `rand·4+3`; 0x166 niebla
  lenta (150 fotogramas) cada `rand·10+10`; 0x167 columnas DARKSMOK cada `rand·15+15`; 0x12e humo y, 1 de cada 10,
  una pieza (tipo 5) que sube 5–10; 0x131 géiser: 41 fotogramas tras el anterior, 7 bocanadas que suben 10 por fotograma.
- **Polvo al aterrizar** (`sub_0003f37c`, por coche y fotograma): con `car+0x278 == 1` y la rueda `+0x158` (o `+0x15c`)
  sobre un tipo marcado en `0xc3558` (1, 3, 4, 11, 13, 14, 21, 23, 24), 4 sprites SMOKE en las ruedas (rumbo ± π/2),
  `vy = −1`, tamaño 50–100, vida 40. Como la fuerza se limita al agarre delantero, al arrancar casi no se nota.
- **PIC de pista** (`USA.PIC` etc.): imagen 320×200 de la pista, no el fondo de la vista 3D.
- **TRI**: significado de los `double`s y de los registros que no coinciden (¿objetos animados?). **SRF**: campos 2–4 de los triángulos (¿plano/altura?) y el tercer valor de cada celda.
- **LFT/FNT y HUD de carrera**: véase la sección «HUD de carrera, fuentes y sprites».
- **CDP**, **PSQ**, **PFM** y **MenuBkg.dat**: descifrados y portados, véase [Frontend](#frontend-portado-a-srcfrontend).
- **BTZ**: véase la sección `IGN_DOS.BTZ` más abajo. **AIS** (texto de IA).
- Audio: WAV PCM 8 bits mono 22050 Hz, PAT (GUS), XI (FastTracker II); música en pistas CD (`MUSIC/Track02-08.ogg`).

## Objetos de colisión, contacto, objetos especiales y `collisions_pass`

Portado en `src/game/pos.c` (objetos móviles, especiales, `collisions_pass`), `src/game/car.c` (contacto,
aplastamiento, reaparición) y `src/game/assets.c` (consultas de suelo y lista del motor). Verificación:
`tests/contact.c` (unitaria), `tests/contact_real_assets.c` (7 pistas) y `tests/contact_original.py` (bytes LE con
Unicorn, `IGN_VERIFY_ORIGINAL=ON`).

- **Orden de carga y clases.** La máquina de estados `[0x1fee08]` de `sub_00020d60` ejecuta 1 → `0x22170`, 2 →
  `0x221dc` (`split_plc_types 0x247b0`, tramos, `cars_init`, creación del mundo `0x2489c`) y 3 → `level_prepare`
  (POS, TRI, efectos, animaciones de textura `0x2abdc`, fantasma, SRF). `0x247b0` **reescribe el tipo del PLC por su
  clase** (`tipo & 0xfff`) y guarda en `[0x1fef7c]` (12 B por objeto) la capa (bits 12–15) y los bytes 2 y 3. Todas
  las comparaciones de tipo del mundo ven la clase: los 0x112c de USA (182, 209) y JAPAN (250) son peligros 0x12c y
  los 0x115f de USA (199, 211) emisores de humo.
- **Consulta de suelo SRF** (`sub_00020814`): puntos = vértice MSH + posición actual del objeto del pool
  (`+0xc/+0x10/+0x14`, que `LI_MOVEOBJECT` actualiza con POS); **la rotación no interviene** (solo la usa el render).
  Coste entero: `face_avg_height` = `−(v0y+v1y+v2y)/3` (división truncada) `+ y_pool`, `d = media − sonda`,
  `d < 0 ? −d : d·d` (`imul`). Sonda `trunc(altura + 50)`. Resultado `0xebad8`: `+0` tipo, `+4/+8/+0xc` normal entera
  `(p0−p1)×(p2−p1)`, `+0x10/+0x1c/+0x28` puntos (motor, y abajo), `+0x34` objeto, `+0x38` tramo. **Ningún objeto
  animado por POS ni de colisión tiene spans SRF** (7 pistas): para ellos cuentan la lista del motor, `0x32dc8`,
  `0x3383c` y `car_ground_contact`. Esto sustituye a la nota de "spans no regenerados" de la sección POS.
- **Rejilla y lista del motor.** `engine_alloc_objects 0x569ec` con el mundo de 51 200: celdas de 256 unidades, 200 ×
  200 (`[0x24c048]`, ancho `[0x24c014]`); `LI_PLACEOBJECT 0x56c74` y `LI_MOVEOBJECT 0x56d24` (este solo si cambia de
  celda) insertan el objeto **en la cabeza** de la cadena `+0x26` de su celda. La creación del mundo coloca primero las
  clases < 100 y luego el resto, ambas en orden PLC. `sub_00058c1c` construye la lista `[0x24c01c]` (cuenta en
  `vista+0x60`) recorriendo un patrón fijo de filas (`0xc5d8c`: fila y columna de inicio −10/−4, luego pares
  (celdas, desplazamiento de la fila siguiente) hasta −5000) desplazado por `0xc5e40[yaw·0,01][pitch·0,01]`; cada
  celda desde su cabeza. Con `[0x24bff0] ≠ 0` solo entran los objetos de capa (palabra `+0x24`) 0 o igual a ella;
  `cars_update_all 0x2d685` y `respawn_place 0x41863` la toman de la capa del objeto del tramo del coche / de
  reaparición, mientras `[0x1fb4d0] < 120` (−1 en carrera; luego fotogramas desde la meta del jugador).
  `sub_0003383c` y `respawn_place` usan la vista (850, 0), que suma una fila.
- **`sub_00032dc8`** (`eax=x, edx=altura, ebx=z, ecx=coche/−1`, sonda en pila, `ret 4`): sobre la lista del motor,
  objetos de clase < 100 a < 600 (900 en modo salto del coche `[0x1feff0]`) en x/z: caras cuyo centro está a < 200 y
  cuya proyección x/z cubre el punto (test de cruces entero) y que no son muros 0x28–0x4f, hasta 8 registros de 16
  dwords en `0x1ff6f0` (más: mensaje y desbordamiento). Objetos de clase 0x12c–0x15d, solo si el resultado anterior
  `[0x1ff970]` está en `[0, 0x15e)`, a < 190 en altura: el último con una cara bajo el punto gana. Si no hay especial,
  la cara con menor `y + sonda − media_arriba ≥ 0` (primer mínimo estricto, normal no vertical); resultado como el de
  `0x20814`, o −1. La usan `respawn_place` (sonda 50 sobre la altura de reaparición, reintento 20 hacia −x) y
  `car_update` mientras vive un rayo (`[0x1fede4]`, puesto tras el primer fotograma de `0x3bdac` y borrado al final):
  0x12d lanza el coche. No hay caras ni objetos 0x12d en los datos.
- **Objetos de colisión** (creación del mundo `0x24f22–0x251fa`, lista `[0x1fefc0]`, `[0x1feeb4]` registros de 0x98 B):
  clases 0x96–0xc7 (con la opción de detalle `[0x1fed64]`); `+0` = clase − 0x96, `+4` PLC, `+8` en vuelo,
  `+0xc/+0x14/+0x1c` posición (`x+0x6400`, y PLC hacia arriba, `z+0x6400`), `+0x24` velocidad, `+0x3c` ángulos
  (décimas de grado), `+0x54` giro, `+0x6c` cuerpo de contacto (vx, vz, w), `+0x84` masa, `+0x88` inercia,
  `+0x8c` impacto, `+0x90` fase, `+0x94` periodo. 0x96 y 0x9a: masa 0,3, inercia 100, periodo 30 y fase compartida
  0, 4, 8… (vuelve a 0 al llegar a 30); 0x98: 25 / 8000; 0x99: 2 / 1500; otra clase: "Ingen info om kollisions
  objektet!!" y fin. USA 17, CANADA 22, AUSTRIA 23, BRAZIL 0, CARIB 24, ICELAND 9, JAPAN 53.
- **`car_ground_contact` 0x350f0** (desde `collisions_pass` si `[0x1feeb4] > 0`, una vez por fotograma):
  - En reposo, contra cada coche no destruido con `|car+8 − y| < 50` y `|dx|, |dz| < 150`: caja del coche
    (`car+0x2fc/+0x31c` girada por `car+0xf8`, floats `0x207aa0`) contra el cuadrado ±30 del objeto con
    `sub_000349e8`; si hay contacto, `sub_000346cc` entre el coche (`car+0`, `+0x10`, velocidad `car+0x640/+0x648`,
    `body+0x30`, masa 10, inercia 4000 de `cars_init 0x273b3`, impacto `car+0x63c`) y el objeto. **Del coche solo se
    conservan el giro `body+0x30` y el impacto**. El objeto sale con `vx, vz` del impulso, `vy = 2 + 2·rand`, giro
    `5·impacto` y en vuelo; con impacto del coche > 10 suena COLL 1 con volumen `min(0x10000, trunc((|i| − 10)·3000))`.
  - En vuelo (`0x356b8`): `vx·0,99`, `vy − 0,4`, `vz·0,99`; ángulos `+= giro` y a `[0, 3600)`; giro `·0,98`;
    posición `+= v` acotada a `[0, 51200]` en x/z y `[−10000, 10000]` en y; `ground_query(trunc x, trunc y + 150,
    trunc z)`. Si el plano `h` cumple `h + 15 > y`: con rapidez > 2 rebota (`sub_00035f38` con la normal
    `(p0−p1)×(p2−p1)` con y invertida, luego `0,9 / 0,6 / 0,9`, `vy ≤ 20`, `y = h + 15`, giro `= 5·rapidez`); si no,
    queda en reposo. Después `LI_MOVEOBJECT` con la pose truncada (cambia de celda en la rejilla).
  - Clase 0x9a: durante el primer quinto del periodo, sprite de LIGHT.TEX (ranura 3, tamaño 400, vida 2, en `y + 20`)
    con la tabla `0xc4010` (fase 0), `0xc4030` o `0xc4050` (último fotograma); la fase avanza cada fotograma.
- **`sub_000349e8`** (polígonos float x/z): cruce de aristas con signos de productos float32; denominadores float32;
  más de 8 cruces = sin contacto. Media de los cruces → `[0x1fee9c]/[0x1feea8]`; par más alejado (primer máximo
  estricto; la distancia sin redondear se compara con el máximo float) → `atan2(z_i − z_j, x_i − x_j)` en `[0x1fee98]`.
- **`sub_000346cc`**: brazos `a = atan2(c − p)` y `r` (floats), velocidades del punto `v ∓ r·w·(sin a, cos a)`;
  sin contacto si el signo de la velocidad normal difiere del lado de A. `j0` float, impactos `|trunc(j0)|`;
  `k = cos(θ − a)·r·m/I` (float); `jA = mB/(mA+mB)·(−1,5·j0) / (1 + cos(a−θ)·r·k)` (float),
  `jB` igual con `mA`; `A.w += jA·kA`, `A.v += jA·(−sin θ, cos θ)`, `B.w −= kB·jB`, `B.v −= jB·(−sin θ, cos θ)`.
- **`sub_00035f38`**: gira la velocidad `−atan2(nz, nx)` en torno a y y `−atan2(ny, |n_xz|)` en x/y; si la
  componente normal es ≤ 0 la invierte y deshace los giros.
- **`sub_00032d8c`** (una vez por fotograma): `car+0x640/+0x648 = (paso del eje trasero + del delantero)·0,5`
  (`car+0x58/+0x78`, `+0x60/+0x80`); ejes `car+0x48/+0x68` a `car+0x5b0` detrás y `car+0x5ac` delante
  (`cars_update_all 0x2da32`). `cars_init 0x267be`: `car+0x5ac = |x|` de la rueda 1 y `car+0x5b0 = |x|` de la rueda 3
  respecto a la carrocería en CARS.PLC (tipo = coche·10 + pieza).
- **Objetos especiales** (clases 0x12c–0x15d, lista `0x1fef58` de 16 B: PLC, tipo, contador, fase): en `car_update`
  (`0x2ef9d`, con la sonda de la primera esquina), por cada entrada a < 200 en x y z del coche (posición actual): en
  ICELAND (`[0x1fef10] == 5`) un objeto de clase 0x12c a < 30 en x, altura y z hace explotar el coche; si no,
  `sub_0003383c(trunc x, trunc altura, trunc z, coche)` y, si devuelve ≥ 0x12c, termina el recorrido.
  Consumidores (`0x30dbb–0x31303`): 0x12c explosión (`car+0x354`), 0x130 deslizamiento (`car+0x35c`); 0x12d o
  0x131 con un géiser en erupción a < 80 (fase `+0xc` de `sub_00036ce0`) → si está en el suelo y `car+0x554 == 0`:
  `car+0x20 −= 8` (como mucho −10), `car+0x554 = 1` y una pieza (tipo 5, vida 30, gravedad −460); mientras
  `car+0x554 > 0` cada tick `car+0x110 = fmod(n·0,078, 2π)` (vuelta de campana) y tras 80 ticks se nivela.
  0x12e (en el suelo y a < 50 de él): `car+0x20 −= 4,5 + 3·rand`, `body+0x30 −= 2,5 + 1,5·rand`, en el aire,
  `car+0x20 ≥ −10`, y GENERAL sample 8 a 0x10000 si `[0x1fee3c] == 0` (luego 15, decrece en cada `car_update`).
  0x12f: aplastamiento. Datos: 0x12c móviles en USA (182, 204, 209), CANADA (208–211), CARIB (332–335), ICELAND
  (192–193) y JAPAN (250); 0x12e USA 181; 0x12f USA 173–174 y BRAZIL 299/301; 0x130 AUSTRIA (14); 0x131 ICELAND (5).
- **`sub_0003383c`**: sobre la lista del motor de la vista (850, 0) en el coche; objetos de clase (palabra `+0x1e`)
  en `[0x12c, 0x15e)`; con `d = punto − posición`, si `|dy| < 190` activa una puerta **que ya no se borra** para los
  siguientes; con `|dx|, |dz| <` 600 (900 en modo salto) y la puerta activa, test de cruces entero sobre los
  vértices sin rotar; el último objeto con una cara bajo el punto da la clase (si no, −1). Resultado en `0x1ffca8`.
- **Aplastamiento** (`sub_00031858`): copia la malla de la carrocería escalada x·1,2, y·0,2, z·1,6 (cambio de malla
  `0x56e9c`) y suena COLL 6. Disparadores: en el bucle de pares de `cars_update_all` (`0x2d3c2`), dos coches no
  destruidos ni aplastados a < 25 en x y z (y < 70 en altura): el de menor `car+0x20` (el de abajo); y el objeto 0x12f.
  Mientras dura (`car+0x558` cuenta ticks): `human_input 0x2c8f5` invierte la dirección, la IA (`0x1c5d8`) la invierte
  parte del tiempo, y uno de cada seis ticks sale una bocanada de SMOKE (tamaño 150–300, vida 50). A los 200 ticks, si
  no hay otro coche a < 30 en x y z, recupera la malla, salta 6 si está en el suelo y suena COLL 7; si lo hay, espera.
- **Animaciones de textura** (`0x2abdc` en `level_prepare`, `sub_00010508`): cada objeto de clase 0xfa define los
  fotogramas con sus caras texturizadas iniciales (hasta la primera no texturizada): mosaicos de 64×64
  (`u, v & ~0x3fff`, página `extra & 0xffff0000`); un fotograma que repite un mosaico anterior no recibe caras. Cada
  cara texturizada del mundo (misma regla) con la página igual y sus tres u/v en `[u, u+0x4000] × [v, v+0x4000]`
  pertenece a ese fotograma. `sub_00010a00` mueve las caras del fotograma i al mosaico del fotograma `(i + n) mod
  fotogramas` (n desde 1). `collisions_pass` suma 1 a `[0x1feef0]` por fotograma y al llegar a `0xc4598[pista]`
  (CANADA 6, USA 20, CARIB 2, BRAZIL 4, AUSTRIA 12, ICELAND 9, JAPAN 12) da un paso y resta el periodo. CANADA 7
  animaciones (179 caras), USA 3 (302), BRAZIL 7 (177), CARIB 1 (868, el mar), ICELAND 3 (32).
- **Mensajes del HUD** (`[0x1fefb8]`, 5 × 0x78 B: activo, texto, `+0x68` duración, `+0x6c` medio periodo de parpadeo,
  `+0x70` edad, `+0x74` vista): `0x4c7c0` usa la primera libre; `0x4c858` (en `collisions_pass`) los envejece y los
  libera al pasar su duración; `0x4c89c` los dibuja mientras `fmod(edad, 2·parpadeo) ≤ parpadeo`. Encolan
  "LAP RECORD"/"TRACK RECORD" (`car_update 0x2f43a`/`0x2f548`, 6 idiomas) y "%s IS OUT" (`0x2e100`), 80 fotogramas,
  parpadeo 8.
- **Humo de eliminación** (`sub_0004fcf0`, `collisions_pass` en modo 3 con el reloj de carrera < 0): el coche en
  carrera cuya posición es `[0x1fed78]` (el último) suelta por la trasera (`car+0x5b8`) dos sprites EXSMOKE (ranura 7,
  tamaño 250, crecimiento −6, vida 14) y, la mitad de los fotogramas, uno DARKSMOK (ranura 5, vida 25).
- **Desviaciones**: los desplazamientos de `0x3383c`/`0x32dc8` se resuelven antes del tick del coche (en el original,
  dentro de `car_update`, con el suelo de ese tick); `car+0x584` al nivelarse toma el balanceo en las unidades del
  port; el render solo aplica la altura del aplastamiento (`body_stretch`), no x·1,2 / z·1,6; la IA no invierte la
  dirección al estar aplastada (`ai.c`); los mensajes no se dibujan (`hud.c`), usan "PLAYER/CPU n" en lugar de los
  nombres y no hay "TRACK RECORD" (el port no guarda récords de pista); con un objeto especial `0x32dc8` deja en el
  resultado la clase y los puntos con la posición del objeto (el original escribe la posición y los vértices crudos);
  más de 8 caras en `0x32dc8` se descartan (el original desborda). En `sub_000346cc` el float `jB0 = −1,25·j0` (masas
  10 y 2) puede caer en el punto medio de dos floats y el x87 desempata con su redondeo de 80 bits de `1/(mA+mB)`:
  41 de 1 500 casos difieren en 1 ulp.
- **Verificación LE** (`tests/contact_original.py`, semilla 0x350f0): 11 303 casos, todos dentro de tolerancia y
  11 262 exactos — polígonos 1 503, impulsos 1 500 (1 459 exactos), reflexión 1 500, vuelo 1 500, rebote 1 500,
  `sub_0003383c` 1 500 y `sub_00032dc8` 1 500 sobre listas sintéticas (`0x58c1c` sustituida por un `ret`), lista del
  motor `sub_00058c1c` 300 (rejillas sintéticas con cadenas y capas) y `sub_00010a00` 500. Corrigió dos errores del
  port: la comparación del par más alejado y `r·k` en float32.

## Configuración y récords: `IGN_DOS.BTZ` (portado en `save.c`, `tools/btz.py`)

Volcado crudo de los **0x597 = 1 431 bytes** de `0x1ff158` (`g_btz`), empaquetado: `i32` y `double` sin alinear.
El menú trabaja con su propia copia en `0xe9bfc` (`g_menu_btz`) y la escribe al lanzar la carrera (`0x130eb`,
`0x1314e`); la carrera la lee entera en `race_config_load` `0x2293c` (`read_file_into`, nombre en `[0xc0c64]` =
`ign_dos.btz`; sin fichero sale con "Error while trying to read"). `btz_save` `0x2cf60` (al final de cada carrera,
desde `race_end_save` `0x2ccc8`) y `shutdown_save` `0x2b430` la escriben entera con `write_file` `0x61078`
(`fopen "wb"` + un `fwrite`).

**Arranque del menú** (`menu_load_gfx` `0x126be..0x12762`): `btz_menu_defaults` `0x1a864` rellena la copia (lo que no
toca queda a cero: BSS), copia los 25 valores iniciales de los controles de opciones y la tabla de récords de fábrica
`0xbf628`; después lee los 4 primeros bytes de `ign_dos.btz` y solo si valen **30** (versión) carga el fichero entero.
Luego fija `+0x103 = 3 − +0x107` y limita `+0x00c ≤ +0x587`. Al principio (`0x115f8`) lee solo el idioma
(`+0x593`) si la versión es 30.

| Offset | Tipo | Destino / significado | Defecto `0x1a864` |
|---|---|---|---|
| `+0x000` | `i32` | versión: 30 | 30 |
| `+0x004` | `i32` | `[0x1fef14]` jugadores humanos − 1 (`[0x1feea0]` = valor + 1) | 0 |
| `+0x008` | `i32` | modo `[0x1feefc]` (salto `0x2292c`): 0 campeonato, 1 ¿carrera suelta?, 2 contrarreloj, 3 eliminación | 0 |
| `+0x00c` | `i32` | clase del campeonato `[0x1fedd8]` 0..3 (5, 6, 7 y 7 pistas): fila de `0xc45b4`; ≤ `+0x587` | 0 |
| `+0x010` | `i32` | `[0x1fee4c]` fantasma; a 0 fuera de contrarreloj o si más de un participante tiene `+0x04 ≠ 1` | 1 |
| `+0x014`, `+0x018` | `i32` | `[0x1fed64]` (1 en campeonato), ¿? | 1, 1 |
| `+0x01c` | `i32[9]` | participante `+0x00` (modelo de coche) | 0 2 3 4 5 7 8 8 0 |
| `+0x040`, `+0x044` | `i32` | coche elegido en el menú por los jugadores 1 y 2 (limitado a los disponibles, `0x167df`) | 0 |
| `+0x048` | `i32[9]` | participante `+0x0c` | 1 |
| `+0x06c` | `char[9][15]` | nombres, `sprintf("%s")` a participante `+0x3c` | `PL1` … `PL2` |
| `+0x0f3` | `i32` | pista (`[0x1fef10]`); en campeonato, ronda: pista = `0xc45b4[clase][ronda]` | 0 |
| `+0x0f7` | `i32` | `[0x1feed8]`: 0 = valor de la pista `0xc3fd8` (tope `[0x25a934]`), −1 = aleatorio, otro = fijo | 0 |
| `+0x0fb`, `+0x0ff` | `i32` | `[0x1feef4]`, `[0x1fee94]` = valor × 10 (`[0x1fee28]` = `[0x1fee94] != 0`) | 5, 10 |
| `+0x103` | `i32` | `3 − +0x107` (menú) | 3 |
| `+0x107`, `+0x10b` | `i32` | `[0x1fef08]`/`[0x1fefdc]` y `[0x1fefcc]`; `0x2cf60` los reescribe | 0, 0 |
| `+0x10f`, `+0x113`, `+0x117` | `i32` | `[0x1fee84]`, `[0x1fed80]`, `[0x1fed70]` | 0, 1, 1 |
| `+0x11b`, `+0x11f` | `i32` | solo menú | 2, 2 |
| `+0x123`, `+0x12b` | `i32[2]` | participante `+0x08` y `+0x18` de los humanos; `cheat_check` pone `+0x123/+0x127` a 1, 2 o 3 | 0 |
| `+0x133` | `i32[8]` | orden de salida `0xc33d0` | 0 … 7 |
| `+0x153` | `i32` | coches en carrera `[0x1fefe4]` | 1 |
| `+0x15b` | `i32` | `[0x1fee48]` | 0 |
| `+0x15f` | `i32[8]` | participante `+0x04` (1 = ¿ordenador?) | 0 1 1 1 1 1 1 1 |
| `+0x17f` | `u8[2][8]` | códigos de tecla DOS de los humanos → participante `+0x1c..+0x23` (los dos primeros se cruzan si clase ≥ 3); jugador 1 con flechas | `cb cd c8 d0 35 34 36 28`, `2e 30 21 2f 2b 2a 2c 1f` |
| `+0x18f`, `+0x193`, `+0x197` | `i32` | solo menú, solo menú, `[0x1febe0]` | 0 |
| `+0x19b`, `+0x1aa` | `char[15]` | cadenas del menú ¿? | `"G1"`, `"12"` |
| `+0x1bf` | 7 × `0x78` | **récords por pista** (`g_track_records` = `0x1ff317`), índice = id de pista del GST (0 CANADA … 6 JAPAN) | `0xbf628` |
| `+0x507` | `i32[25]` | valores de los controles de opciones (`+0x14` de los controles de `0xea47c`) | 0, salvo el 10 = 1 |
| `+0x57f`, `+0x583` | `i32` | pantalla de récords `0x1911c`: pista mostrada y tabla (carrera/vuelta) | 0 |
| `+0x587` | `i32` | `[0x1ff6df]` clase más alta desbloqueada | 0 |
| `+0x58b` | `i32` | `[0x1ff6e3]` 1 tras ganar la clase 3 | 0 |
| `+0x58f` | `i32` | solo menú | 0 |
| `+0x593` | `i32` | idioma `[0x1ff6eb]`: 0 inglés, 1 alemán, 2 italiano, 3 español, 4 sueco, 5 francés | 0 |

Récords de fábrica (`0xbf628`, carrera / vuelta, el primero de cinco): CANADA OSK 145 / 48, USA PEZ 118 / 39,
CARIB JIG 175 / 58, BRAZIL EMP 203 / 67, AUSTRIA PQL 203 / 67, ICELAND SHR 175 / 58, JAPAN KOT 233 / 77 s.

Bloque de récords de una pista (`0x78` B): `char[5][4]` nombres de carrera, `double[5]` tiempos de carrera (`+0x14`,
segundos), `char[5][4]` nombres de vuelta (`+0x3c`), `double[5]` tiempos de vuelta (`+0x50`), el mejor primero.
`reset_race_times` `0x2a442` siembra `g_best_lap` (`0x1fb390`) con la vuelta 0 de la pista. Avisos en carrera
(`car_update`): "LAP RECORD" (`0x2f43a`, 6 idiomas) cuando una vuelta mejora `g_best_lap`; "TRACK RECORD"
(`0x2f548`) al completar la vuelta 3 fuera de eliminación con `car+0x3a4` < tiempo de carrera 0. Inserción
(`render_main` `0x45938..0x45f20`, una vez por coche con `car+0x528 == 1`, solo humanos): si `t <` hueco 4 (`fcomp`
+ `jae`: empate o NaN no entran) se escribe en el hueco 4 con `sprintf("%s", nombre)` y se hace una pasada de
burbuja 4→1 intercambiando mientras el inferior sea estrictamente menor. Lo mismo para la tabla de vueltas con los
tres huecos `car+0x3bc/+0x3c4/+0x3cc`.

**Campeonato** (modo 0). `render_main` `0x44a2a`: clasificado si el coche 0 (o el 1 con dos jugadores) acaba entre
los 4 primeros (`car+0x3a0 < 4`; el bucle no avanza el puntero, así que solo mira esos dos coches). `0x44a66`: la
ronda 4 de la clase 0, la 5 de la 1 o la 6 de las clases 2/3 completa la clase ("YOU HAVE COMPLETED THIS
DIFFICULTY LEVEL"); si no, "WELL DONE!" y `[0x1fee54] = pista + 1`. `race_end_save` `0x2ccc8`: con
`[0x1fee54] > pista`, ronda `+0x0f3` + 1 (máx. 6), suma los puntos de `0xc548c[puesto]` a `[0x1aebb0]` (fuera del
BTZ) y escribe `+0x133[i] = 6 − puesto`. Desbloqueos (`0x44fd2..0x45095`, solo con un jugador y el coche 0 primero
en la clasificación): ronda 6 y clase 3 → `+0x58b = 1`; ronda 6 y clase ≤ 2 → `+0x587 ≥ 3`, clase 3; ronda 5 y
clase ≤ 1 → `+0x587 ≥ 2`, clase 2; ronda 4 y clase 0 → `+0x587 ≥ 1`, clase 1. `btz_save` `0x2cf60`: fuera del
campeonato con más de un coche hace `ncoches` intercambios del orden de salida con dos índices
`floor(rand01 × ncoches)` y reescribe `+0x107`/`+0x10b`.

**Contrarreloj** (`[0x1feefc] == 2` y `[0x1fee4c] == 1`): `ghost_load` `0x2adf8` lee `GHOSTS\<nombre>.GST`; si
falla, `[0x1fee4c] = 0` y `[0x1fee04] = 3 000 000`; si no, `[0x1fee04]` = tiempo de la cabecera (antes de mirar la
pista), `[0x1fed84]` = modelo, nombre a `0x1fb4b8`, y si la pista de la cabecera no es `[0x1fef10]` no copia las
muestras y pone `[0x1fee00] = 1` (por tanto no se guardará). Al terminar, `0x2ccc8` graba el fantasma solo si
`[0x1fee00] == 0` y `[0x1fee04] × 0.01 > car+0x3a4`. Sin la opción de fantasma, `[0x1fee04]` no tiene otro
escritor (BSS a 0 ¿?) y nunca se graba.

**Port**: `IgnBtz` conserva la imagen como bytes. `tools/btz.py roundtrip` reconstruye desde cero los 1 397 bytes
mapeados y no queda ningún byte no nulo opaco en el fichero distribuido. `ign_btz_default` es la imagen del menú
recién arrancado y `tests/btz_defaults_original.py` (opcional, Unicorn) la compara byte a byte con
`0x1a864` + controles + `0xbf628` ejecutados sobre el original. Verificado: el `IGN_DOS.BTZ` real carga, decodifica,
recodifica y guarda idéntico (`save_btz`); tras una carrera sin récord el fichero escrito es idéntico; una
eliminación terminada solo baraja los huecos del orden de salida (`time_trial`). Persistencia en
`SDL_GetPrefPath("UDS", "Ignition")` (o `IGN_SAVE_DIR`): `IGN_DOS.BTZ` (si falta o su versión no es 30, el fichero del
juego, que solo se lee, o la imagen por defecto) y `GHOSTS/<nombre>.GST` (primero ahí, luego en la carpeta del juego).
La ventana la usa siempre; sin ventana solo con `--time-trial` o `IGN_SAVE_DIR`. Al terminar el jugador se insertan
los récords y se guarda (como `render_main`); cuando todos han terminado se aplica `0x2ccc8`/`0x2cf60` y se guarda
otra vez. **Desviaciones**: nombres de récord truncados a 3 letras (el original desbordaría al hueco siguiente); un
coche eliminado o una vuelta vacía (0) no entran en las tablas; el nombre del jugador es el participante 0; los
récords se guardan ya al terminar el jugador (el original espera al final de la carrera); los desbloqueos
(`ign_champ_unlock`) no se llaman porque el port no lleva la clasificación por puntos del campeonato (`[0x1aebb0]`,
fuera del BTZ) ni encadena pistas; el modo de la carrera es el de la línea de órdenes, no `+0x008`; contrarreloj
sin rivales; la comparación `× 0.01` se hace en `double`, no en x87 de 80 bits; `rand01` es `rand()` de C.

## Frontend (portado a `src/frontend/`)

Flujo (`sub_00020d60`): `[0x1fede8]==1` → `menu_load_gfx` `0x11504` (pantalla LOADING, `MENU.COL`, `MENU.TAB` como LUT
de sprites `[0x257e48]`, imágenes, valores por defecto `0x1a864` + `IGN_DOS.BTZ`, arranque del motor de menús) →
`menu_frame` `0x12bb8` cada fotograma (1 seguir, 2 carrera, 3 salir) → `menu_shutdown` `0x13a5c`. Tras una carrera se
vuelve a cargar el menú; con `[0xbed18]==0` la página de idioma se confirma sola (Enter inyectado) y en lugar de la
intro hay un iris de apertura.

**Tiempo.** El menú cuenta en *ticks* de 1/36 s (`timer_ms·0,036`), con `dt` 0 → 0,01 y > 10,8 → 10,8, en pasos
enteros más uno fraccionario (que puede valer 0). Los contadores de animación suman 27 por paso aunque el paso sea 0.
El motor de menús tiene su propio reloj a 25 Hz (`timer_ms·0,04`, máximo 12) y su bloqueo de entrada resta 25 por paso.

**Intro (primera entrada).** Enter en la página de idioma carga `IGN1`, `IGN2`, `IGN3_0..3.CDP` (150, 129, 97, 60, 40
y 46 fotogramas) a 15,04 fps (2,3931 ticks); paleta negro→blanco (0–28,8), blanco→vídeo (28,8–57,6), vídeo→blanco
desde 1249,2 (= 522 fotogramas), blanco→`MENU.COL` desde 1306,8; entrada desbloqueada a 1312,56, fin a 1335,6.
`3_INTRO.WAV` suena dos veces a 22 050 Hz. Cualquier tecla salta a 1278. Al terminar empieza el pase de créditos.

**Créditos (inactividad).** Tras 1080 ticks (30 s) sin teclas: 16 diapositivas de 168 ticks (`0xbf57c`:
`{título, nombre c, b, a}`, nombres `0xbf538`), la 1 y la 2 son imágenes de `N_SYSGFX.PIC`; "PRESS ANY KEY" parpadea
500 de cada 809 ms. El reloj vuelve de 3780 a 1080.

**Motor de menús "Baltazar"** (`0x51b20`–`0x539f8`, `gui.c`). Página 0x18 B `{dibujo 0x170ac, teclas 0x160c4, keymap
0xbee1c, n, items, sel}`; ítem 0x1c B empaquetado `{int *valor (en g_cfg), min, max, Element *, i16 callback (>0
acción 1-based en 0xbf01c, −1 = volver), i32 arg, EditDesc *, i16 habilitado}`; elemento 0x18 B `{sprite (siempre
NULL), texto, x/y de texto relativos al centro, fuente}`. Índices: etiqueta `sel^1` (+2, +2n si deshabilitado), valor
`etiqueta + 2 + 2·(v − min)`. EditDesc 0x16 B: destino, x/y, fuentes, longitud, cursor `|`, tipo (1 texto, 2 texto al
entrar, 3 tecla). La navegación no está en los ítems: la tabla `0xbe514` (25 filas × 16 `{i16 coincide, i16 acción}`
por profundidad; coincide 0 = cualquiera, si no `sel+1`; acción ≥ 0 id de menú en `0xaeb6c + idioma·0x3110`, < 0
acción −acción) decide por el camino recorrido, así que los modos de juego reutilizan las páginas de coche y pista.
Keymap: Esc/Retroceso −6, Enter +6, ↑ −1, ↓ +1, ← −3, → +3 (los ±3 rotan el valor). El callback de teclas devuelve
`0x441` para consumir. Las 25 páginas × 6 idiomas, textos, tablas de teclas, nombres de tecla, créditos y récords por
defecto se extraen del ejecutable con `tools/gen_menu_data.py` a `src/frontend/menu_data.c`.

Páginas (índice `k` de `0xea47c`): 0 principal, 1 opciones, 2 multijugador, 3 modo de juego, 4/5 coche J1/J2,
6 pista, 7/8 red (inaccesibles), 9 gráficos, 10 sonido, 11/12 opciones de jugador, 13/14 teclas, 15 opciones de juego,
16 récords, 17–20 calibración de joystick y pedales, 21 idioma (raíz), 22–24 confirmaciones SALIR/RESTAURAR/BORRAR.

**Dibujo de página** (`0x170ac` con `0x155d0` en línea). Capa de texto 320×200 (`[0xea408]`) que solo se rehace
cuando algo cambia: cajas de `MenuBkg.dat` (etiquetas estilo 0, valores y campos estilo 1; caja = `x−13`, fila
`y/19`, desplazamiento `y%19−12`) y textos con la LUT identidad `[0x1fec0c]` (`lut[s<<8|d] = s`: los glifos son
opacos y su fondo es el color de la caja). Cada fotograma: fondo PFM (17 fps) copiado al búfer, logo en (75,2), capa
de texto mezclada con `MENU.TAB` en un rectángulo por página, banderas (la alemana nunca se dibuja), rótulo del coche
(186×61 de `BILAR.PIC`) que encoge y crece al cambiar, carrusel de tres coches 3D (x 78/159/240, escala 1,01/1,61,
una vuelta en 3,4/1,9 s), miniatura de pista (115×99 de `TRK_SPR.PIC`) y esquinas de 20×20 de `N_SYSGFX.PIC`.

**Callback de teclas** (`0x160c4`): trucos (`BANARNE`, `SURMULE` dificultad MIRROR, `SLASKTRATT` los 11 coches,
`FILMJOLK`, `SKUNK`, `SVINPOLE`, `STRINGS`) en `0x1fb2f0`; fin de créditos; salto de la intro; sonidos 1_FLIP (±1…±5,
50 000) y 0_SEL (±6); CONTROLS: solo un jugador con joystick/pedales; confirmar idioma (páginas del idioma, lista de
coches con los bloqueados `{10, 9, 6, 1}` según `[0xea183]`/`[0xea187]`, intro o iris); KEY ALREADY ASSIGNED (bloqueo
1100, 2_NO); animaciones de coche (bloqueo 400) y de pista.

**IGN_DOS.BTZ**: el menú edita la imagen descrita en
[Configuración y récords](#configuración-y-récords-ign_dosbtz-portado-en-savec-toolsbtzpy) (`settings.c` sobre
`save.c`) y la escribe al salir o al empezar una carrera, después de `0x198bc` (cursores → carrera: 6 corredores salvo
contrarreloj, coches 2..7 fijos, etiquetas de piloto `0xbf5fc`, pista `+0xf3` en el orden CANADA, USA, CARIB, BRAZIL,
AUSTRIA, ICELAND, JAPAN). El port la guarda en la carpeta de usuario, como la carrera, y solo lee la del juego.

**MenuBkg.dat** (0x9358 B, raíz del juego): atlas de baldosas almacenadas de abajo arriba, 16×19 (esquinas `0x000`,
`0x130`, `0x260`, `0x390`; remates `0x8e80`, `0x8fb0`, `0x90e0`, `0x9210`) y 32×19 (uniones izquierda `0x4c0+d·0x260`,
`0x2600+(d−1)·0x260`, derecha `0x44e0+…`, `0x6ae0+…`), color 0 transparente, y 24 colores de relleno en `0x9340`. El
estilo 1 remapea `c1→f5 c3→bd c4→be d5→b4 d6→bb fd→bc c0→bd c2→f4`. Una caja une sus esquinas con la de la fila de
arriba si los bordes distan ≤ 13 (izquierda) o ≤ 15 (derecha).

**CDP**: `"CDP\0"`, `u16 100`, `u16 fotogramas`, `u16 bucle`, `u16 320`, `u16 200`, `u16`, paleta 768 B de 8 bits,
fotogramas desde `0x310`. Códigos: `00–f5` píxel, `f6 b` píxel escapado, `f7–fa` saltar 2–5, `fb n8` y `fc n16` saltar,
`fd n8 v` repetición (0 = 256), `fe n16 v` repetición de 16 bits, `ff` fin. El primer fotograma de cada clip es
`fe 00 fa v` (pantalla lisa) seguido de cambios.

**PSQ / PFM** (fondo del menú). PSQ: `{u32 primer, u32 cuántos, u32 0}` en bucle (`DEFAULT2`: 11 tramos, fotogramas
0–208). PFM: trozos `{tag[4], u32 tamaño}` hasta `lixx`; `li01` vértices `{s16 x, y}` (×16 → 24.8, 1/16 de píxel),
`li00` fotograma `{u8 tipo, u8 n, registros}` hasta `ff` (7 sprite escalado = dos triángulos 18, 11 línea, 13 se
ignora, 15 triángulo plano con `MENU.TAB[c<<8]`, 17 texturizado opaco, 18 texturizado por tabla de sombra
`sh[t<<8|d]`, 19 plano por tabla de sombra), `li02` texturas globales `{u16 u0..v2 (8.8), u32 página}`, `li03` sprites.
Páginas: 1 MB de `CANADA.TEX`, `CARS.TEX` y una de cada efecto (29); tablas: `CANADA.SHD` y una generada (fila 0
identidad, fila r sólida). El búfer de fondo no se borra nunca entre fotogramas. Los rasterizadores (tramos de
derecha a izquierda, UV solo en el borde derecho con tabla de 1/8 de píxel, acarreo de v hacia la fracción de u,
recorte solo por arriba, abajo y derecha) se portaron literalmente y se compararon con el código original ejecutado en
un emulador: los 209 fotogramas y 4 900 primitivas aleatorias sin un píxel distinto. La prueba `menu_background`
fija el hash de los 209 fotogramas.
