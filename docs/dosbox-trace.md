# Original-game trace comparison

**Status: pending a real DOSBox-X capture.** DOSBox-X 2026.10.01 is installed and the debugger accepted commands, but
it has not been verified that `MAINDOS.EXE` reached a race. The DOS MZ entry was observed at `0824:038C`, and DOS
reported `MAINDOS` in its memory-control-block chain. After resuming execution, the debugger later stopped at
`F000:8DB4` in the BIOS idle loop. A delayed `ADDKEY` sequence was accepted and `MAINDOS` allocations appeared in
the DOS MCB chain, but no race screen or protected-mode game state was confirmed; a subsequent debugger-inserted
Enter did not advance execution. The BIOS keyboard queue was empty when first inspected. These observations do not
establish an LE load-address mapping. The instructions below establish the port-side format and comparison tool
only; they do not claim the original-memory sampling procedure has been verified.

## Port-side trace

```sh
IGN_TRACE=1 ./build/ignition USA --sim 20 auto --rivals 0 --shot /tmp/x.ppm 2>&1 | grep '^t=' > build/port.txt
```

This records one row per 1/72-second simulation tick, with `t`, `x`, `z`, heading, forward speed and `kmh`.

## Required original-side capture work

Before this task can be marked complete:

1. Run `MAINDOS.EXE` under a DOS/4GW-compatible DOSBox-X build with its debugger enabled.
2. Confirm how the debugger exposes LE object addresses after DOS/4GW loading. Do not assume a single additive base
   from an on-disk address; verify the code and data selectors/linear addresses in the running process.
3. Determine a repeatable way to sample the player's `g_cars` fields (`+0x00` x, `+0x10` z, `+0x118` km/h) at
   each physics update. Confirm the player-car index and tick cadence before collecting the reference.
4. Convert verified samples into `t=… x=… z=… kmh=…` rows; retain the debugger transcript and the conversion script
   alongside the trace, not in the repository.
5. Compare the reference and port:

   ```sh
   tools/trace_compare.py build/port.txt build/original.txt --tol x=2 z=2 kmh=1
   ```

The comparator reports the first field or row-count divergence and fails on missing fields or unequal trace lengths.
Its mechanics are tested with synthetic traces; the original capture procedure is not.
