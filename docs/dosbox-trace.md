# Capturing reference traces from the original game in DOSBox-X

Status: **tooling only**. No DOSBox-X build was available while writing this, so the capture procedure below is
untested. The comparison side (`tools/trace_compare.py`) is tested against the port's own traces.

## Port side

```sh
IGN_TRACE=1 ./build/ignition USA --sim 20 auto --rivals 0 --shot /tmp/x.ppm 2>&1 | grep '^t=' > build/port.txt
```

Each line is `t=… x=… z=… h=… v=… kmh=…` every 0.0139 s (one 1/72 s physics tick) in the port's world units.

## Original side

1. Install DOSBox-X with the debugger enabled and mount the game directory (`ignition`), then start `MAINDOS.EXE`.
2. The executable is DOS/4GW LE: linear addresses in `tools/symbols.txt` are LE-object addresses. DOS/4GW loads the
   code object at a run-time base, so find it once: break in the debugger (`Alt+Pause`), search for the `_cstart_`
   bytes (`DOS /4GW` banner string `RATIONAL DOS/4G` at `0xab29c` is a good anchor) with `MEMDUMPBIN` and compute
   `base = found - 0xab29c`.
3. Log per tick with a breakpoint on `cars_update_all` (`0x2d008` + base) using `BPM` / `LOGC`:
   `g_cars` pointer at `0x1fefe0`; per car x `+0x00`, z `+0x10` (doubles), kmh `+0x118`, heading `+0xf8`.
4. Convert the log to the port's `t=… x=… z=… kmh=…` lines (the original works in the same world units; heading is
   `π/2 − render heading`).
5. Compare: `tools/trace_compare.py build/port.txt build/original.txt --tol x=2 z=2 kmh=1`.

The first divergence reported is the first tick where the port differs from the original; start the physics review
from the function that writes that field (`awk '/^; ----/{f=$3} /<field offset>/{print f,$0}' build/all.asm`).
