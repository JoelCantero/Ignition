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
  El objeto es el índice PLC (pool del motor de 42 B). Coordenadas = mundo + 25600. El juego elige la cara más cercana a la altura del coche
  y lee `flags >> 16` como **tipo de superficie** (prefiere 0x28–0x4f). Centroides de caras encuentran su propia cara: 62–78 % (el resto son paredes).
- **POS** (animaciones de objetos, `sub_00041244`/`sub_000412e8`): `n_plc` × `i32` (−1 = sin animar; si no, offset en dwords
  relativo a la propia entrada). Animación: `i32 nframes, i32 frame_actual`, `nframes` × `{i32 x,y,z, rx,ry,rz}`;
  ángulos en décimas de grado (el juego resta 3600 si ≥ 3600). Las posiciones se convierten en deltas del fotograma
  siguiente, con el último enlazado al primero; al avanzar se resta el delta y se asignan los ángulos del fotograma.
  Verificado byte a byte en las 7 pistas. En el port, `pos.c` copia y valida la tabla, avanza a 36 Hz y aplica las
  transformaciones de posición/rotación al render y a ciertas consultas geométricas. El orden Euler del motor no está
  confirmado: se usa `Z(Y(X(v)))` como aproximación; los spans SRF no se regeneran, así que la consulta de suelo y las
  colisiones para objetos trasladados siguen incompletas. Se animan todas las entradas POS porque aún no se porta el
  flag activo del objeto original.
- **TRI** (bordes de pista, `load_tri` 0x1ce30; parcialmente verificado): `u32 n_tramos`, luego un bloque de 500 B por objeto PLC.
  Cada bloque: hasta 2 registros empaquetados de 22 B `{u8 flags, u16 vértice_msh, i32 x, i32 y, i32 z, 8 B}` (bordes izquierdo/derecho),
  seguidos de `double`s (pendientes/ángulos precalculados). X/Z = vértice MSH + posición PLC + 25600 en el 72–90 % de registros.
  `load_tri` toma los vértices de `+1` y `+0x17` como **bordes izquierdo/derecho** del tramo (puerta) y el byte `+0x6d`
  divide la puerta en 2 registros (1 = mitad central→derecha y luego izquierda→central; 2 = izquierda→central y luego central→derecha).
  Las coordenadas X/Z de estos registros son vértice + posición PLC + `0x6400`; Y es vértice + posición PLC sin ese origen
  (`0x1d30b..0x1d392`). `track.c` extrae estas coordenadas y subdivisiones. `track_real_assets` verifica en las siete
  pistas que los registros TRI conservan la identidad engine→PLC y se asignan a una topología de ruta válida; esto es
  una prueba estructural del port, no una comparación runtime exhaustiva con las tablas del original ni equivalencia del
  selector completo de carril.
- **GST** (fantasma, `GHOSTS\<nombre>.GST`; lector `0x2adf8`, grabador `0x51204`, escritor `0x2ccc8`; portado en `ghost.c`):
  exactamente `0x49d4c` = 302 412 B (`read_file_into` exige el tamaño). Cabecera: `i16 modelo, i16 pista, i16 tiempo` (carrera
  completa de 3 vueltas en centésimas, `car+0x3a4 × 100` truncado) y `char[4]` nombre (3 letras + NUL). Después 21 600 muestras
  de 7×`i16`, una por tick de 1/72 s (300 s máx.; si se llena, `[0x1fee00] = 1` y no se guarda): `x + (−25600)`, `y` (altura,
  `+5000` si `car+0x354`), `z − 25600`, ángulos `car+0x108 / +0xf8 / +0x110` en `[0, 2π) × 1024` y progreso
  `vuelta × (tramos + 10) + |tramo|` (sin el tramo si `|tramo| ≥ tramos/4` en la fase 0). Al guardar se rellena hasta el final
  copiando las palabras 0–5 de la última muestra (la 6 queda como estaba) y quedan 2 bytes a cero. Solo en contrarreloj
  (`[0x1feefc] == 2`) y si se bate `[0x1fee04] × 0.01`. Pistas: 0 CANADA/MOOSEJAW, 1 USA/GOLDRUSH, 2 CARIB/SNAKEISL,
  3 BRAZIL/LOSTRUIN, 4 AUSTRIA/YODELPEA, 5 ICELAND/CAPETHOR, 6 JAPAN/TOKYOBUL. Coincide con `Ign_win.exe`.

## IA de rivales (`sub_0001aea0`, ~1900 instr.)

- Llamada desde `cars_update_all` (0x2dd56) con `al` = índice de coche; termina en `car_input` (0x4d5f4) con
  acelerador `[esp+0x70]`, dirección = estado+0x74 y turbo.
- Estado por coche en `[0xeb7c0]` (0x7c B), puertas por tramo en `[0xeb7c4]`/`[0xeb7e0]`.
- Parámetros en `0xbfb30` (0x48 B = 9 `double` por coche), iguales para todos:
  `{0.8, 0.4, 0.6, 0.75, 0.5, 5, 0.6, 0.6, 0.9}` ↔ campos de `CARS/TEST.AIS` (acc, carebed, caresight, careother,
  maintain, aggr, sight, curveinflu, slopeinflu). `TEST.AIS` no se lee en el juego.
- **Desviación**: `src/game/ai.c` es una aproximación propia (sigue las puertas TRI, frena según la curvatura en los
  próximos 5+ tramos, marcha atrás si no avanza 150 u en 1,5 s). La ley de control original sigue pendiente.
- **Carril (`t_ai_lanes`, `0xbf9a0`)**: portadas las cuatro filas por configuración y la permutación de pistas
  `{1,2,3,6,4,0,5}`. En cada fila coincidente se escribe `selector` en `+0x66`, se consume un `rand()` y se guarda
  `r = (rand() & 0x7fff) / 32768` en `+0x6c`; la elección `+0x64` es 0 si `chance=0`, 1 si `chance=100`, o
  `(s20 * 0.7 + 0.3) * r * chance * 0.01 > 0.125`. Las filas repetidas (incluidas las de sección 2) se
  procesan y consumen RNG por separado. En el port, `ign_fx_rand15` usa el LCG `seed = seed * 1103515245 + 12345`
  (`rand`, `0x64774`), compartido con el estado de `IgnFx`.
- `load_tri` se integra al arrancar: registros geométricos extraídos por objeto engine→PLC, incluidas subdivisiones TRI
  de tipo 1/2. `ign_build_track` replica ahora las reglas observadas de `build_sections` para buscar vecinos por origen
  PLC (distancia euclídea con ΔY×2), contar avances excluyendo el merge de clase 6, conservar el merge alcanzado por B
  y mapear el objeto de bifurcación en paralelo en ambas rutas. También conserva los objetos siguientes explícitos para
  calcular los ángulos de ambas ramas. La prueba `track_topology` cubre selección por origen frente a centroide,
  bifurcación compartida, ángulo de rama B y continuación desde un merge B distinto del merge A. Aún falta cotejar toda la tabla `g_sections` y la asignación de registros
  divididos/carriles en las siete pistas; no se afirma equivalencia integral.
- `ai_find_section` (`0x1decc`) ya tiene un helper portado (`ign_track_find_section`): interpreta sección, rama y
  avance como entradas de 16 bits; hace un único ajuste de wrap; cambia a la otra rama si la solicitada no existe;
  ante un registro TRI inválido acepta el actual cuando la condición de bifurcación original lo permite, o avanza
  según el signo del paso. En CANADA (`track_id=0`), las secciones `0xab..0xad` fuerzan el avance en ese caso.
  `ai.c` usa el helper al consultar los registros/puertas de lookahead. La prueba `track_route_selection` cubre fallback,
  aceptación/avance, wrap y excepción de CANADA. El enlace de ramas y los objetos siguientes conservan la topología
  observada; `track_real_assets` valida la estructura generada y el mapeo engine→PLC de TRI para las siete pistas, pero
  no coteja la tabla runtime original ni la correspondencia exacta de cada registro dividido con los carriles. Por tanto,
  esto no acredita aún la selección de ruta completa equivalente.
- Al construir puertas, el índice de objeto del motor se traduce a índice PLC mediante
  `surface.engine_to_plc` antes de leer el bloque TRI (`4 + plc_index * 500`). No se presupone que ambos índices
  coincidan; los índices ausentes o bloques fuera de rango usan el centro del objeto como fallback. La prueba
  `track_gates` usa un mapeo no idéntico para verificar esta correspondencia.

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

- `sub_000502c4(coche)` (automático): `idx = min(abs(trunc_x87(v_long·3.6)), 199)`, producto con el
  `double` original 3.6. Usa curvas **compartidas del modelo 0** en `0x1fb6d8` (marcha 1) y `0x1fb868`
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
`+0x18`=0.6, `+0x20`=0.75, `+0x28`=0.5, `+0x38`=5.0, `+0x40`=0.6, `+0x48`=0.6.
`a8 = 1+(grip+0.8)·Σ|Δángulo|·(s38·0.4+0.6)·1.6/n`; `T = ((1+s00)·0.5+0.3)·x90/(s10·6/(3n+6)+1)/((a8-1)·0.4+1)`;
`x88 = max(0,s18·60+120-kmh)/(s18·60+120)·s18·1.5+1`; `T = (T·x88+(x88-1)·0.25)·100` (máx 100);
freno `(kmh-ov)·0.5` si `ov·0.2777 < v`; marcha atrás: acelerador 0, freno `min(1.5T,100)`;
turbo si `s20·0.13+1.07 > a8`, `off>3` y `|dir| > s20·5+3` (o `ov==500` en dificultad 0).
El factor de pendiente decodificado es `x90 = sqrt(max(1, 1 + ΔY·slopeinflu·1.1/(40·off)))`; `ΔY` se calcula entre
las coordenadas Y (`record+0x06`) de los registros de puerta seleccionados por la ruta y el carril a distancia `off`.
Aunque los vértices TRI proporcionan sus alturas, el port aún no aplica esta fórmula: sustituir los registros
seleccionados por la altura media de cada puerta cambió demasiado la regresión determinista (15 de 23 casos), así que
esa aproximación se descartó hasta reconstruir la selección de registros/carriles.

## IA original `0x1aea0`: etapa de salida (decodificado)

- Estado por coche `[0xeb7c0]` (0x7c B): `+0x20` agresividad ¿?, `+0x28` radio de alcance (×`[0xa7b30]`), `+0x58` valor lateral del
  rival más cercano, `+0x60` contador de atasco (i32), `+0x74` dirección calculada (double, ±100).
- Rival más cercano: mínimo de `hypot(Δx,Δz)` sobre los demás coches (`0x1c12d`). Si el ángulo relativo (atan2) menos el rumbo
  ∈ (`[0xa7b20]`,`[0xa7b28]`) y la distancia < radio, se mezcla una corrección de esquiva en `dir` (`0x1c2a2–0x1c409`).
- Dirección base: `dir = Δángulo(rumbo objetivo − rumbo coche) · k · (1 + (aggr+…)·…)`, ×`[0xa7af8]` si control 5/8, ×factor por
  dificultad `[0x1ff164]` (1: `[0xa7b00]`, 2: `[0xa7a70]`, 3: `[0xa7b08]`); recorte a ±100. Se invierte si `car+0x558 != 0`
  (giro de choque) y `|car+0x558| < 0x48−dif·…`.
- Atasco (`0x1c630..0x1c777`): si `abs(error_rumbo)>1.4` **o**
  (`v_longitudinal<6.944444444444445` **y** `airborne==0`) **o** (`contador>43.2` **y** zona especial),
  `+0x60 += N` con N coches **totales**; en caso contrario se pone a cero. Si el resultado es `>=151.2`,
  también se pone a cero. La velocidad lleva signo, no valor absoluto. Zona especial: USA `84<abs(sección)<95`
  y `side<0`; AUSTRIA `61<sección<65` **con signo**; ICELAND `146<=abs(sección)<=148`.
  Reversa en `43.2<contador<97.19999999999997`, sin giro antes de `53.99999999999999`;
  `97.19999999999997<contador<108` anula la dirección. Se conserva la anulación en el aire.
  El port ya no añade edad/espera artificial. El error original mezcla rumbo y velocidad con factor 0.15 y
  usa el ángulo TRI `record+0x2e`; el port aún reconstruye geométricamente ese ángulo. El oracle suministra
  el error, por lo que no verifica esa geometría.
- Turbo: si `car+0x534/538` == 100.0 (turbo lleno), sin `car+0x270`, `+0x20·[0xa7bb8]+[0xa7bc0] > curva` y la curva lejana > 3, se
  llama a `car_input` con turbo = 1 (`0x1cb12`), salvo exclusiones por pista/sección.
- Salida: `car_input(coche, ecx=turbo, edx=freno/atrás, ebx=dirección, …)` en `0x1cbf9`; al final `+0x58 = trunc(acelerador)`.
- Pendiente de portar: búsqueda de carril (`0x1decc`, tablas `[0xeb7c4]/[0xeb7e0]`), velocidad objetivo por curvatura (bucle
  de 6 secciones `0xbf992`, `t_surface_grip`), registro por sección con ángulos `+0x2a..+0x46` calculados en `load_tri`
  (`atan2` entre bordes de secciones consecutivas, y puntos de carril `+0x1a..+0x26` = borde + `cos/sin·[0xa7c18]`).

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

### Verificación ejecutable de `body_step`

`tests/physics_original.py` carga los objetos LE reubicados mediante el parser existente `tools/le2elf.py`;
no parchea ni ejecuta el archivo original como proceso del sistema. Identidad admitida:
SHA-256 `179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c`.
Se emula x86-32 con Unicorn 2.1.4, EAX = cuerpo, EDX = indicador de aire, `slope_k` double en ESP+4,
retorno centinela y palabra de control x87 `0x037f`. El cuerpo Watcom se serializa por offsets, incluidos
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
Tolerancia por salida: `2e-11 + 2e-12·|original|` (redondeo x87 frente a double/libm);
indicadores y campos inmutables exactos. En macOS ARM64 el mayor error observado fue aproximadamente
`3.64e-12` en fuerza y `1.14e-13` en posición. El informe en `build/physics-original-report.json`
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
| `car_steer`, `0x500f0` | 732 | 5 856 | `7.11e-15` |
| Agarre, `0x3070a..0x30874` | 672 | 1 536 | `1.78e-15` |
| Aritmética vertical, `0x2e234` | 252 | 1 008 | `1.43e-14` |

Total: **1 656 casos / 8 400 pasos**, con tolerancia `2e-11 + 2e-12·|original|` e indicadores exactos.
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
kernel vertical. Son estados finitos muestreados bajo x87 emulado `0x037f`, no equivalencia para cualquier entrada.

### Verificación ejecutable de aceleración, freno y turbo

El mismo oracle y runner añaden tres kernels de producción: `ign_car_throttle`, `ign_car_brake` e
`ign_car_turbo`. En estos tres modos no se ejecutan revoluciones ni se sustituyen llamadas de sonido/efectos por resultados
inventados. La aceleración termina en `0x4dfbe`, antes de revoluciones; el freno exige retorno de la rutina
`0x4e1bc` para coche no terminado; el turbo entra en `0x4e3a7` y termina en `0x4e656`, excluyendo efectos.
Se ejecuta la conversión `__CHP` original y se comprueban frontera, ESP, control/pila x87 y escrituras permitidas.

| Límite | Casos | Pasos | Mayor error absoluto observado |
|---|---:|---:|---:|
| Aceleración, `0x4db90..0x4dfbe` | 6 829 | 30 174 | `1.82e-12` |
| Freno, `0x4e1bc` | 72 | 288 | `0` |
| Aritmética de turbo, `0x4e3a7..0x4e656` | 2 016 | 96 768 | `9.32e-10` |

Nuevos bloques: **8 917 casos / 127 230 pasos**. Suite de seis kernels: **10 573 / 135 630**.
La tolerancia sigue siendo `2e-11 + 2e-12·|original|`; indicadores y contadores son exactos, con controles
negativos para cada modo. El informe conserva hashes del original, fuentes, runner y entradas, además de
cobertura por bloque. Incluye modelos, controladores, dificultades, marchas, límites `nextafter` de índices
de par, patinaje, catch-up y freno, y secuencias de carga/agotamiento/recarga del turbo.

Correcciones verificadas:

- El índice de par trunca primero `v_fwd*3.6` en precisión extendida x87, toma el valor absoluto, divide por
  `[1, 1.15, 1.3, 1.3]`, trunca y limita a 199. Redondear antes el producto a binary64 puede cambiar el índice
  (por ejemplo, `10/3.6`). El port usa el residuo FMA y el umbral de redondeo extendido, también en la división,
  sin depender de que `long double` tenga precisión x87. Se prueban ambos signos y vecinos de índices 0..210.
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
- **Acelerador** (`car_throttle` 0x4db90): índice = `min(199, trunc(|trunc_x87(v*3.6)| / 0xc5450[dificultad]))`; con patinaje
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
- **LFT** (port parcial verificado): cabecera `"LFT\0"`, `u16 100`, `u16` cantidad de glifos en `+6`,
  `u16` ancho y alto en `+8/+0x0a`; 224 offsets `u32` desde `+0x0c`, 224 anchos `u16` desde `+0x68c` y
  bitmaps desde `+0x84c`. Cada bitmap es una matriz de índices de 8 bits (ancho × alto); índice 0 no pinta.
  `src/render/hud.c` valida los rangos y dibuja los glifos `YELLOW`, `SPEED` y `POS_S` sobre el panel original.
  **FNT**: `u16 56`, `u16 10`, `0xffff`, anchos por glifo.
- **HUD de carrera** (port parcial de `sub_0004a320`): la composición añade recortes de `H_PAN1.PIC` después de
  pista, coches, efectos y clima. Presenta el reloj total (`IgnRace.total`, unidades de 1/36 s), el tiempo de vuelta
  desde la salida, la posición y la velocidad. El panel usa píxel 0 como transparente y mapea su paleta a la de la
  pista por color más cercano. Aún no reproduce las coordenadas/condiciones originales, la aguja ni el llenado dinámico
  del turbo, los avisos de `H_PAN2.PIC` ni los estados multilingües.
- **CDP**, **PSQ**, **PFM**, **BTZ** y **MenuBkg.dat**: descifrados y portados, véase [Frontend](#frontend-portado-a-srcfrontend).
- **AIS** (texto de IA).
- Audio: WAV PCM 8 bits mono 22050 Hz, PAT (GUS), XI (FastTracker II); música en pistas CD (`MUSIC/Track02-08.ogg`).

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

**IGN_DOS.BTZ** = volcado del bloque `g_cfg` `0xe9bfc`, 0x597 bytes, se acepta si el primer `int` es 30. Campos en
`src/frontend/settings.h`: modo, dificultad (máximo = nivel desbloqueado `+0x587`), fantasma, obstáculos, coche por
plaza `+0x1c`, coches de menú J1/J2 `+0x40/+0x44`, cambio `+0x48`, nombres `+0x6c` (9 × 15), pista `+0xf3`, pista
CD/volúmenes `+0xf7/+0xfb/+0xff`, opciones gráficas `+0x103..+0x117`, controles `+0x123..+0x12f`, orden de salida
`+0x133`, corredores `+0x153`, humano/IA `+0x15f`, teclas J1/J2 `+0x17f/+0x187`, récords `+0x1bf` (7 pistas ×
`{char nombre[5][4]; double total[5]; char nombre[5][4]; double vuelta[5]}`), cursores de las 25 páginas `+0x507`,
pantalla de récords `+0x57f/+0x583`, desbloqueos `+0x587/+0x58b`, idioma `+0x593`. Se escribe al salir o al empezar
una carrera, después de `0x198bc` (cursores → carrera: 6 corredores salvo contrarreloj, coches 2..7 fijos,
etiquetas de piloto `0xbf5fc`). La pista `+0xf3` usa el orden CANADA, USA, CARIB, BRAZIL, AUSTRIA, ICELAND, JAPAN
(`0x22f38`).

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
