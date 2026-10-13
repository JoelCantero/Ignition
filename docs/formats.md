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
  divide la puerta en 2 carriles (1 = mitad izquierda, 2 = mitad derecha). Ancho medio verificado: 295–405 unidades en las 7 pistas.
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

## Fin de carrera y eliminación (portado a `src/game/race.c`)

- `car_update` (`0x2f2f1`): vuelta + 1 si `|tramo| == count−1`, fase 3 y `car+0x604 == 0`. Si la vuelta ≥ 3 y el modo `[0x1feefc]` ≠ 3,
  el coche termina: `+0x528 = 1`, `+0x3a0 = +0x39c` (tiempo) y `[0x1fee60]++` (contador de llegada).
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
  tonos `ENGINE.INF[0|400 + i]·600 + 20000` Hz, volúmenes `[200|600 + i − 1]·650`. Revoluciones (`0x4e000`):
  `v/tope_marcha·66` (topes `0xc4fe0`) o tienden al acelerador en el aire/antes de la salida. Rodadura (`0x50464`):
  clase `tipo mod 10`, solo audible el coche visto. Derrape: clase `tipo mod 9`, `0xc3578`/`0xc35a0`. Emisores de
  ambiente: objetos PLC `0x190 + n` (sonido `n`), volumen 0,75·(1500 − d)/1500.
  `human_input` (`0x2c5b8`) solo mapea dirección, acelerador, freno, marchas, turbo y cámara trasera; no contiene
  una tecla de recolocación. El reinicio observado (`0x43e48`) pertenece al menú de pausa y reinicia la carrera,
  no recupera un coche durante ella. No se ha verificado aún una recuperación automática al caer o volcar.

## Entrada de teclado (`sub_0002c5b8` = `human_input`, decodificada)

- Registro de control por jugador en `[0x1fefd0]` (`0x4c` B): `+8` tipo (0 = humano), `+0xc` cambio manual, `+0x18` auto-acelerar,
  teclas (consultadas con `sub_00055c28`): `+0x1c` izquierda, `+0x1d` derecha, `+0x1e` acelerar, `+0x1f` frenar, `+0x20/+0x21` marcha
  arriba/abajo, `+0x22` turbo, `+0x23` cámara trasera (`car+0x5a4`).
- Acelerador y freno digitales = 100; dirección **digital** −100 / +100 / 0 en `car+0x344` (sin rampa; la suavidad la da el
  integrador `steer`). `car+0x9a4` = ±50 y `car+0x9a8` = ángulo del tramo + constante (`g_sections+4`, o `+0x10` si tramo < 0).
- `+0x18 == 1` y sin freno → acelerador 100. Si `car+0x558 > 0` (solape con otro coche) la dirección se invierte.
- Turbo: tecla y `+0x354 == +0x35c == +0x358 == 0` → petición `car+0x52c = 1`; pasa a 2 si `car+0x3a4 <` constante y `+0x530 == +0x53c == 0`.
- Cambio manual (`+0xc == 1`): flancos de `+0x20/+0x21` suben/bajan `car+0x27c` (0–2) y llaman a `car_gearbox` `0x502c4`.
- Termina en `car_input` (`0x4d5f4`) con (acelerador, freno, dirección, turbo). El port ya cubre este camino con `IgnInput`.
- `sub_00031858` (llamada al solapar, `0x2d4d9`/`0x312fe`) solo cambia la malla del coche (`[0x1fecb4]`, 'Error while changing car mesh')
  y reproduce un sonido; `sub_0002ea44` (impacto > 15) es solo efecto visual y sonoro.

## Caja de cambios y dificultad

- `sub_000502c4(coche)` (cambio manual): `idx = min(|v_long·3.6|, 199)`; con las curvas de par `0x1fb6d8` (marcha 1) y `0x1fb868`
  (marcha 2) como `int16`: si `par1[idx] > par2[idx]` → marcha 1; si no, marcha 2 salvo que `car+0x90 ¿?` (ángulo) esté en
  (−π/2, π/2) o > 3π/2 (entonces 1). Un contador (`ctrl+0x10`) debe superar el retardo `ctrl+0x14` antes de cambiar; suena un
  efecto si `ctrl+4 != 1`.
- Dificultad `[0x1fedd8]` ∈ 0..3 → `[0xc5470]` = {1.0, 1.15, 1.3, 1.3}, factor del agarre trasero (`car_update` `0x30811`).

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
- Atasco: si `|ángulo| ≤ [0xa7b88]` y (`body+0x98` < `[0xa7b90]` o `car+0x270`≠0) o casos de pista concretos (`[0x1fef10]`=1/4/5),
  `+0x60 += nº coches`; si no, `+0x60 = 0`. Con `+0x60 ≥ [0xa7ba0]` se resetea. Entre `[0xa7b98]` y `[0xa7ba8]` marcha atrás
  (dirección 0 o ±70 según `+0x74`; acelerador = −`[0xa7be0]…`), con `car+0x270`≠0 la dirección se anula.
- Turbo: si `car+0x534/538` == 100.0 (turbo lleno), sin `car+0x270`, `+0x20·[0xa7bb8]+[0xa7bc0] > curva` y la curva lejana > 3, se
  llama a `car_input` con turbo = 1 (`0x1cb12`), salvo exclusiones por pista/sección.
- Salida: `car_input(coche, ecx=turbo, edx=freno/atrás, ebx=dirección, …)` en `0x1cbf9`; al final `+0x58 = trunc(acelerador)`.
- Pendiente de portar: búsqueda de carril (`0x1decc`, tablas `[0xeb7c4]/[0xeb7e0]`), velocidad objetivo por curvatura (bucle
  de 6 secciones `0xbf992`, `t_surface_grip`), registro por sección con ángulos `+0x2a..+0x46` calculados en `load_tri`
  (`atan2` entre bordes de secciones consecutivas, y puntos de carril `+0x1a..+0x26` = borde + `cos/sin·[0xa7c18]`).

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
- Agarre (`car_update`): media de `0xc33f0[superficie]` de las 4 ruedas × `0xc4dc0[coche]` (tras. × dificultad `0xc5470`).
- Par (`sub_00053e70`): spline de Hermite por marcha (`0xc4fd0`, 4 puntos), 200 valores por km/h; marcha 0 = atrás.
- Acelerador (`sub_0004db90`): `par[km/h] × 1000 × %acel`, suavizado 0.2; sin acelerar `−v·500`.
- Dirección (`sub_000500f0`): entrada ±100; `5/agarre × entrada% × (0.3 + (vmax−v)/div)`, suavizado por coche (`0xc4e80/0xc4ee0/0xc4f40`).

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
  (0 si |km/h| < 50 y |caída| > 4, o si la caída > 0.05·km/h → salto); al aterrizar `v > 8 ? −0.2v : 0.5v`, mínimo −15.
  En el aire si `suelo + 30 < h` (`car+0x270`); entonces el integrador no aplica tracción.
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
- Vueltas (`car_update`): fase 0→1→2→3 al pasar ¼, ½, ¾ de los tramos; vuelta + 1 en fase 3 al pisar el último tramo.
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
- Vueltas completas comprobadas con `--sim 200 auto --rivals 3` en las 7 pistas (CANADA incluida: la IA sigue `gate_b` en la rama B).
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
- **Acelerador** (`car_throttle` 0x4db90): \u00edndice = `|trunc(v\u00b73,6)| / 0xc5450[dificultad]` (\u2264 199); con patinaje
  contra el movimiento \u00edndice 0. Si la curva da \u2264 3, `car+0x520` cuenta y cada `8\u00b7car+0x51c + 15` ticks sube
  `car+0x51c`; si no, se divide entre 2. El humano recibe cada 182 fotogramas `car+0x594 = (posici\u00f3n \u2212 1)\u00b71,5`
  (`[0xc0b50] = 1`). Objetivo = `(par + 0x51c + 0x594 + 0x524)\u00b71000\u00b7acelerador/100`, o \u221225000 con la marcha atr\u00e1s
  puesta avanzando (freno); se acerca un 20 % por tick tras la salida; soltado, `\u2212v\u00b7500`.
- **Turbo** (portado, `sub_0004e2d8`, llamado por `car_input` tras el acelerador): carga `car+0x534` (0–100, empieza llena),
  modo `+0x53c` (1 = botón, 2 = forzado). Inactivo: recarga `0xc4f70[coche]` por tick (0,4–0,6) hasta 100. Activo: −2,5 por tick
  (40 ticks); fuerza motriz `+= 0xc4e50[coche]·carga + max(0, (20 − v)·10000)`; el modo 2 usa `20000·carga` y suma
  `0.01·carga` a la velocidad. Activación (`car_input` 0x4d8eb, 4.º argumento = 1): solo con carga ≥ 100 y sin haber
  terminado; si no, pitido y espera de 30 ticks (con carga > 20). Recarga: `< 100` suma, `> 100` se fija a 100.
  Arranque turbo (`human_input` 0x2c99e): una pulsación nueva (`car+0x530`) antes de 0,25 s de carrera (`car+0x3a4`)
  con el modo a 0 pide 2 y fuerza el modo 2 sea cual sea la carga. Al empezar un turbo, `sub_0004e688` suelta 10
  bocanadas de SMOKE (`0xc41a0`, tamaño `0x32000`). La IA lo pide con la carga exactamente a 100 (`0x1ca15`).
- **Colocación tras la reaparición** (`sub_00041430`, el fotograma siguiente a `car+0x350 = 1`): mismo punto que el
  deslizamiento (con la búsqueda hacia delante de los huecos −2 de la rama B); una consulta de suelo en el punto y,
  si no hay, otra 20 unidades hacia −x (que también mueve el coche). Copia ese registro a las 4 ruedas, suelo actual y
  anterior = altura del plano, coche quieto (`+0x18..+0x44`, cuerpo `+0x30`, `+0x78..+0x9c`, `+0xac`, `+0xd4`), sin
  inclinación, en el aire, primera marcha, revoluciones, km/h, modo salto, derrape y chispas a 0, y
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
- **CDP**: vídeo, `"CDP\0"`, `u16 100`, `u16 frames`, …, `u16 320`, `u16 200`, paleta.
- **PSQ**, **PFM** (`"li01"`), **BTZ** (config `i32`), **AIS** (texto de IA).
- Audio: WAV PCM 8 bits mono 22050 Hz, PAT (GUS), XI (FastTracker II); música en pistas CD (`MUSIC/Track02-08.ogg`).
