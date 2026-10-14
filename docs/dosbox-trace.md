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
The legacy row labelled `t=0` is already the state **after** the first tick, with rounded values.
Do not align it with an initial-state sample from the original.

For precision and repeatable durations, use:

```sh
IGN_TRACE=full ./build/ignition USA --sim-ticks 1440 auto --rivals 0 --shot /tmp/x.ppm \
  2> build/port-full.txt
```

Full schema version 4 has 104 numeric fields: controls, sampled car/body state and four wheel/corner entries.
It adds `race_elapsed`, `lap_elapsed` and `lap_origin` in seconds (original car+0x3a4/+0x3ac/+0x3d4).
Version 3 had 101 fields including `gear_ticks` and `gear_delay` (original ctrl+0x10/+0x14). Version 2 had 99 fields including
persistent `grip_avg` (original car+0x148); version 1 had 98. The replay rejects older versions rather than
silently comparing incomplete state. Grip retention and delayed automatic shifts depend on this state.
It emits an initial row (`tick=0,t=0`), followed by one row after each complete update, at 17-digit precision.
Tick `n` has timestamp `n/72`; countdown updates are included when requested. This is not a dump of all game
memory, nor a verified original-game sampling boundary. `--sim-ticks` executes an integer tick count;
the legacy `--sim` still accumulates `float` time and may execute an extra tick (577 at 8 seconds).

`tests/solo_replay.py` checks the exact schema, tick count and equality between two fresh port processes;
its JSON report and retained captures carry port-only authority. `--reference-binary` accepts another port build
with the same schema. A mismatch does not establish which build matches DOS.

`tools/trace_compare.py --all-fields` compares all fields present in matching schemas. Unspecified tolerances
are zero, but its default `x=2 z=2 kmh=1` still applies; use `--tol x=0 z=0 kmh=0` for exact equality.
Missing fields, unequal lengths, non-finite numbers and misaligned timestamps/ticks fail. Legacy compound
`drive=force/limit` and `turbo=mode/charge` values are retained as separate numeric fields.

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
