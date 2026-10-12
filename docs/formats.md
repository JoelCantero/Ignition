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
- **SRF** (consulta de suelo, `sub_00020814`; verificado): cabecera 9×`i32` = `?, ?, alto_celda=512, ancho_celda=512, gx=101, gz=101, nspan, na, nb`.
  Celdas (12 B): `u32 offset_lista_B, u32 offset_lista_A, u16 nA, u16 nB` (offsets en bytes). Listas: `u32` offsets en bytes a spans.
  Span (24 B, trapecio en planta): `i32 x0, z0, pendiente_izq, pendiente_der (16.16), i16 dz, u16 cara (dwords desde el inicio del objeto MSH), u32 objeto*42`.
  Lista B: `z0 ≤ z ≤ z0+dz`; lista A: `z0+dz ≤ z ≤ z0`; en ambas `x0 + (t*izq>>16) ≤ x ≤ x0 + (t*der>>16)`, `t = z−z0`.
  El objeto es el índice PLC (pool del motor de 42 B). Coordenadas = mundo + 25600. El juego elige la cara más cercana a la altura del coche
  y lee `flags >> 16` como **tipo de superficie** (prefiere 0x28–0x4f). Centroides de caras encuentran su propia cara: 62–78 % (el resto son paredes).
- **POS** (animaciones de objetos, `sub_00041244`/`sub_000412e8`): `n_plc` × `i32` (−1 = sin animar; si no, offset en dwords
  relativo a la propia entrada). Animación: `i32 nframes, i32 frame_actual`, `nframes` × `{i32 x,y,z, rx,ry,rz}`;
  ángulos en décimas de grado (el juego resta 3600 si ≥ 3600). Fotograma 0 = referencia (el juego resta su posición al resto).
  Verificado byte a byte en las 7 pistas.
- **TRI** (bordes de pista, `load_tri` 0x1ce30; parcialmente verificado): `u32 n_tramos`, luego un bloque de 500 B por objeto PLC.
  Cada bloque: hasta 2 registros empaquetados de 22 B `{u8 flags, u16 vértice_msh, i32 x, i32 y, i32 z, 8 B}` (bordes izquierdo/derecho),
  seguidos de `double`s (pendientes/ángulos precalculados). X/Z = vértice MSH + posición PLC + 25600 en el 72–90 % de registros.
  `load_tri` toma los vértices de `+1` y `+0x17` como **bordes izquierdo/derecho** del tramo (puerta) y el byte `+0x6d`
  divide la puerta en 2 carriles (1 = mitad izquierda, 2 = mitad derecha). Ancho medio verificado: 295–405 unidades en las 7 pistas.

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

## Colisiones con muros (portadas)

- No hay geometría de muros: un muro es una **arista no compartida de una cara transitable** (tipo < 0x28, 0x64, 0x65).
- `sub_00033ee8`: objetos a < 600 unidades en x/z y < 350 en y; aristas de sus caras transitables; se descartan las que
  aparecen invertidas en otra cara (tolerancia 16). Se ejecuta si una rueda pisa tipo **0x50/0x51** (flag `car+0x57c`).
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

- Suelo del coche (`car_update` 0x303xx, portado): por rueda, l\u00edmite `100 + 0.5\u00b7km/h`; una rueda es an\u00f3mala si su suelo
  difiere del anterior m\u00e1s que el l\u00edmite o pisa tipo \u2265 0x50. Si son an\u00f3malas 1\u20133 ruedas toman `suelo_anterior \u2212 v`;
  si lo son las 4 se aceptan (cambio de nivel). Suelo = media de las 4.
- Desviaci\u00f3n: las caras con m\u00e1s de ~80\u00b0 de inclinaci\u00f3n (o degeneradas) no cuentan como suelo.
- Desviaci\u00f3n: el l\u00edmite de seguimiento del suelo (`0.05\u00b7km/h`) tambi\u00e9n se aplica hacia arriba; sin \u00e9l, los saltos de
  capa (puentes, cornisas) lanzan el coche a cientos de miles de unidades.
- Parrilla de salida (`sub_00026458`, portada): origen PLC del objeto tipo 0 mirando al tipo 1 (ángulo = `−atan2(p0−p1) − π/2`).
  Hueco `i` (tabla identidad en `0xc33d0`): `(i/2)·180 + 200` unidades atrás y 60 al lado según la paridad; con un solo coche,
  200 atrás y centrado. Verificado: 8 coches sobre asfalto en las 7 pistas.
- Vueltas completas comprobadas con `--sim 200 auto --rivals 3` en las 7 pistas (CANADA incluida: la IA sigue `gate_b` en la rama B).
- **Choques entre coches** (portado): `cars_update_all` (0x2d2f8) prueba cada par con |dx|,|dz| < 200 y |dy| < 70 unidades y llama
  a `sub_0001ebb4`: círculo de radio 4 m (`+0x130`), cruce de los 4×4 lados de las cajas (máx. 8 puntos; más = sin choque),
  contacto = media, línea = par de puntos más alejado. `sub_0001e638`: impulso mutuo con `e = 0.6·0.6` (`+0x10`),
  `j_A = m_B·(−(1+e)·v_n)/(m_A+m_B)/(1 + k_A·r_A·cos(a_A−θ))`, `k = 2·r·cos(θ−a)·m/I`, separación 0,25 m.
  Pendiente: el caso de solape (|dx|,|dz| < 25, `sub_00031858`) y los efectos de impacto > 15 (`sub_0002ea44`).
- **Turbo** (portado, `sub_0004e2d8`, llamado por `car_input` tras el acelerador): carga `car+0x534` (0–100, empieza llena),
  modo `+0x53c` (1 = botón, 2 = forzado). Inactivo: recarga `0xc4f70[coche]` por tick (0,4–0,6) hasta 100. Activo: −2,5 por tick
  (40 ticks); fuerza motriz `+= 0xc4e50[coche]·carga + max(0, (20 − v)·10000)`; el modo 2 usa `20000·carga` y suma
  `0.01·carga` a la velocidad. Activación (`car_input` 0x4d8eb, 4.º argumento = 1): solo con carga ≥ 100; si no, pitido
  y espera de 30 ticks (con carga > 20). Como la fuerza se limita al agarre delantero, al arrancar casi no se nota.
- **PIC de pista** (`USA.PIC` etc.): imagen 320×200 de la pista, no el fondo de la vista 3D.
- **TRI**: significado de los `double`s y de los registros que no coinciden (¿objetos animados?). **SRF**: campos 2–4 de los triángulos (¿plano/altura?) y el tercer valor de cada celda.
- **LFT**: `"LFT\0"`, `u16 100`, dimensiones de glifo. **FNT**: `u16 56`, `u16 10`, `0xffff`, anchos por glifo.
- **CDP**: vídeo, `"CDP\0"`, `u16 100`, `u16 frames`, …, `u16 320`, `u16 200`, paleta.
- **PSQ**, **PFM** (`"li01"`), **BTZ** (config `i32`), **AIS** (texto de IA).
- Audio: WAV PCM 8 bits mono 22050 Hz, PAT (GUS), XI (FastTracker II); música en pistas CD (`MUSIC/Track02-08.ogg`).
