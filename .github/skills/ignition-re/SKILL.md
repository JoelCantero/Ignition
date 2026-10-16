---
name: ignition-re
description: 'Reverse engineer and port Ignition (UDS 1997, MAINDOS.EXE, DOS/4GW LE, Watcom C) to the C11 + SDL3 port in this repository. Use when: decoding a function or global of MAINDOS.EXE, finding code by string/address/constant, reading tables or doubles from the executable, decoding a game data format (PLC, MSH, TEX, SRF, TRI, POS, TAB, PAN, SHD, LFT, CDP, GST, BTZ, INF), porting a game system (physics, AI, cars, collisions, render, HUD, menus, sound), verifying the port headless (--sim, --shot, --model), or updating docs/functions.json, docs/formats.md and the README roadmap.'
argument-hint: 'Address (e.g. 1aea0), system or data format to reverse engineer or port'
---

# Ignition reverse engineering and port

Workflow for turning `MAINDOS.EXE` code into faithful C in `src/`. The repository `README.md` holds the status,
coverage, binary map and roadmap; `docs/formats.md` holds formulas, offsets and verified formats.

## Facts to rely on

- Game data: `/Users/Shared/Epic Games/Ignition/ignition` (override with `IGNITION_DATA`). Never commit game files or
  derived dumps (images, PPM/PNG, `build/*.asm`).
- Executable: LE (DOS/4GW), Watcom C. Code object `0x10000–0x85c4f`, data `0x90000`, data+BSS `0xa0000`.
- Watcom register calling convention: args in `eax, edx, ebx, ecx`, rest on the stack; `cdecl` for varargs.
- `sub_00055f9c` (`__CHP`) before every `fistp` is FPU truncation, not game logic.
- Known runtime helpers: `atan2` `0x647ef` (st1 / st0 order: `atan2(dz, dx)` pattern), `pow` `0x64c2a`, `fmod`
  `0x65782`, `rand` `0x64774`, random float `0x51714`, `memset` `0x65367`, `sprintf` `0x62775`.
- Internal engine: "Lisa 2 Development System" (`LI_MOVEOBJECT` `0x56d24`, `LI_PLACEOBJECT` `0x56c74`, set mesh
  `0x56e9c`). Error strings `FEL VID LI_…` name the subsystem of the calling function.
- Angles in engine objects are tenths of a degree (`rad * 180/π * 10`).
- Port conventions: world Y points down; 1 m = 21.76 world units; physics tick 1/72 s; heading 0 = +z; physics
  heading `= π/2 − render heading`; SRF/TRI coordinates = world + 25600.
- Key structures: car `g_cars` `0x1fefe0` (`0x484c` B/car), body `g_bodies` `0x1fefe8` (`0x1d0` B/car), sections
  `g_sections` `0x1feff8` (24 B), engine object 42 B. Field tables are in `README.md` → "Estructuras de datos conocidas".

## Tools

Run from the repository root (`python3 -m venv .venv && .venv/bin/pip install capstone` if `.venv` is missing):

```sh
.venv/bin/python tools/disasm.py all > build/all.asm      # dump once, then grep (looping `dis` is very slow)
.venv/bin/python tools/disasm.py dis 1aea0                # one function
.venv/bin/python tools/disasm.py xref "LAP RECORD"        # functions using a string
.venv/bin/python tools/disasm.py callers 4d5f4            # who calls a function
.venv/bin/python tools/disasm.py refs 1fefe0              # who touches a global
.venv/bin/python tools/disasm.py mem a9e98 -n 4           # doubles (-f i/I/h/p/s for ints, pointers, strings)
.venv/bin/python tools/ignassets.py pic FILE.PIC -o build/ref   # data → PNG
```

Useful greps on `build/all.asm` (function name lines start with `; ----`):

```sh
awk '/^; ----/{f=$3} /fstp +qword ptr \[e[a-z]{2} \+ 0x584\]/{print f, $0}' build/all.asm   # writers of a field
awk '/^; ----/{f=$3} /0x19bc[0-9a-f]{2}/{print f, $0}' build/all.asm                         # users of a buffer range
```

Indexed or pointer-table globals may not appear as literals: search the data for the 32-bit value (LE fixups) or the
base minus/plus a small offset.

## Procedure

1. **Locate**: start from a string (`xref`), a global (`refs`), a `double` constant, a caller, or a field offset grep.
   Check the single catalog `docs/functions.json` and `docs/formats.md` first; do not re-derive known facts.
2. **Read**: disassemble, resolve constants and tables with `mem`, follow the FPU stack carefully (write the stack
   state after each `fxch`/`fsubp`/`fsubrp`). Prefer quoting the exact formula with its addresses.
3. **Record**: update the function/data record in `docs/functions.json` (name, status, roadmap, notes, implementation,
   tests and references); put extended formulas, offsets and verification in `docs/formats.md` and link them.
   Preserve unresolved `review` annotations until individually reconciled. Mark guesses with `¿?`.
4. **Port**: implement in the matching `src/` module, referencing the original address in a short comment
   (`/* sub_0004e2d8: … */`). Keep the original constants. Any intentional difference is a *deviation*: comment it
   in code and list it in `README.md`.
5. **Verify headless** (see below) on all 7 tracks; the build must stay warning-free.
6. **Update derived views**: after status/count changes run `.venv/bin/python tools/disasm.py catalog --refresh`,
   then `python3 tools/function_catalog.py sync-readme` and `python3 tools/port_dashboard.py`.
   Update the README binary map/roadmap prose if needed. Never recreate the removed TSV, coverage or symbols files;
   the JSON is the only editable function inventory. `python3 tools/function_catalog.py check` works without game data.

## Build and verification

```sh
cmake -S . -B build && cmake --build build 2>&1 | grep -E "warning|error"
for L in USA CANADA AUSTRIA BRAZIL CARIB ICELAND JAPAN; do printf "%s " $L; ./build/ignition $L --sim 200 auto --rivals 7 --shot /tmp/x.ppm | grep -oE "nan|lap [0-9]" | sort | uniq -c | tr '\n' ' '; echo; done
IGN_TRACE=1 ./build/ignition BRAZIL --sim 6 turbo --shot /tmp/x.ppm 2>&1 | grep "^t=" | awk 'NR%36==1'
./build/ignition USA --sim 0.3 0 --rivals 3 --cam 900 20 2700 0.0 0.25 --shot /tmp/g.ppm && python3 build/ppm2png.py /tmp/g.ppm
./build/ignition --shot /tmp/m.ppm --model 9 0.6 30 0.15 0 USA && python3 build/ppm2png.py /tmp/m.ppm
```

- `--sim SEG DIR`: `DIR` = −1/0/1, `auto` (AI) or `turbo`. `--model ID yaw steer_deg roll pitch LEVEL` needs `--shot`
  first; `IGN_LIFT=units` and `IGN_BANARNE=1` affect the viewer.
- `IGN_TRACE=1` prints per-tick state (position, height, drive/grip limit, turbo, km/h) and track statistics.
- `build/ppm2png.py` assumes 320×200; view the PNG to check rendering changes.
- Regression signal: no `nan`, laps similar to before, cars on asphalt at the grid.

## Pitfalls

- zsh: never put multi-argument values in a variable; `#` lines pasted into the terminal are not comments.
- Piping a check through `grep`/`tail` hides its exit code; run builds bare before relying on success.
- Engine object indices differ from PLC indices except in USA and CANADA (`map_engine_objects` in `assets.c`).
- The REA MCP server cannot open this LE executable through Hopper; use `tools/disasm.py`.
