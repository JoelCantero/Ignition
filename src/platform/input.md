# SDL3 input backend

`input.c` is a native SDL3 replacement, **not a verbatim port of the DOS hardware library**.
It is used in both the frontend and interactive race. No game data or display is needed by
`input_backend_test`.

## Binding and lifecycle contract

- Keyboard identities are physical SDL scancodes translated to DOS set-1 byte identities;
  extended arrows differ from keypad keys. Unknown keys cannot accidentally bind byte zero.
- Saved P1 key bytes (`CFG_P1_KEYS`) and controls/auto-acceleration settings are honored.
  The existing WASD, Space, Q/E, R shortcuts remain aliases. M still toggles automatic/manual;
  F1, Tab, F5/F6 keep their debug/camera/music roles. Configured `/`/`.` shifts and Right Shift
  turbo now also work. A gear edge survives a complete press/release between physics ticks,
  but is consumed only once even when a render frame runs several physics ticks.
- Controls 0 uses keyboard, 1 the first joystick/gamepad, 2 independent raw pedals
  (mapped gamepads keep their standardized triggers), and 3 the second device. Absent devices
  fall back to keyboard. Standalone races accept the first device without needing the menu.
  The config API supports a second player/device, but the application still renders/drives
  one human car; it does not implement split screen.
- Mapped gamepads use left X steering, right/left trigger accelerator/brake, South/East
  full accelerator/brake, West turbo, North rear view, right/left shoulders up/downshift.
  D-pad steering also works. Raw joysticks use X steering and negative/positive Y
  accelerator/brake; buttons 0/1 pedals, 2 turbo, 3 rear, 5/4 gears, hat steering.
  Keyboard steering overrides analog steering when exactly one direction is held.
- A raw device's independent pedal axes 1/2 use the opening positions as released positions;
  release the pedals before connecting. `ign_input_calibrate_axis` accepts asymmetric
  min/center/max ranges, including an endpoint as the center. Device-specific axis/button
  remapping UI and the original calibration menu choreography are **not implemented**.
  Prefer SDL's gamepad mapping database for known devices.
- Keyboard repeats are supplied by SDL/OS, independently from initial-press latches;
  repeats navigate/type in the frontend but never repeatedly toggle debug controls or gears.
  A bounded FIFO retains DOS key identities. ASCII cheat history is fed by `SDL_EVENT_TEXT_INPUT`,
  not a US DOS character translation table; focus loss clears it.
- Focus loss/gain neutralizes held keys, pedals, buttons, pending shifts and history.
  Device events are ignored while unfocused. A held control needs a new SDL event after
  focus return; input is deliberately not resumed from stale physical state.
- Two devices are opened initially and hotplugged by stable SDL instance ID; mapped devices'
  duplicate raw joystick streams are ignored. Removal closes only that device and clears its
  state. Remapping reopens the handle. Extra devices beyond the two slots are ignored.
  SDL subsystem failures are reported by the caller, open failures by SDL logging.
  No keyboard IRQ, BIOS/DPMI vector, `in`/`out`, port `0x201`, or CPU-timing calibration is used.

## Original address crosswalk

Evidence: `tools/disasm.py dis ADDRESS` against MAINDOS.EXE; the addresses below are the
original linear code coordinates. These are replacement responsibilities, not claims of
instruction or hardware-timing equivalence.

| Original | Observed responsibility | Native equivalent / boundary |
| --- | --- | --- |
| `0x559d0` | Clear keyboard callback pointers | Explicit SDL event dispatch; no DOS callback registration |
| `0x559e4` | Initialize held/latch/repeat tables, typed ring, repeat timings and ISR | `ign_input_init`, `ign_input_reset`; SDL/OS owns repeat timings |
| `0x55ae4` | Poll raw keys, produce press/repeat latches, invoke callback, append translated characters | `ign_input_event`, `ign_input_key_pressed`, `ign_input_key_repeat`; SDL text input replaces DOS character lookup |
| `0x55c28` | Read a held-key byte | `ign_input_key_down` |
| `0x55c3c` | Read and clear initial-press latch | `ign_input_key_pressed` |
| `0x55c60` | Read and clear repeat latch | `ign_input_key_repeat` |
| `0x55c84` | Invoke the keyboard callback without arguments | Direct host/frontend event delivery; DOS callback ABI intentionally absent |
| `0x55ca0` | Append DOS-translated key character to rolling typed history | `ign_input_event` text-input branch; DOS translation is deliberately not emulated |
| `0x55cd0` | Search typed history and clear it on match (`kbd_typed`) | `ign_input_typed` |
| `0x55d04` | Install IRQ1 handler, clear raw tables | `ign_input_init`, SDL event subsystem |
| `0x55d98` | Test whether any raw key is held | `ign_input_any_key`; anomalous recursive uninitialized branch not reproduced |
| `0x55e04` | IRQ1 make/break decode, E0 extension, raw state and 16-key ring | `ign_input_dos_scancode`, `ign_input_event`, `ign_input_read_key`; Pause/PrintScreen normalized, not byte-sequence emulated |
| `0x55eec` | Restore original keyboard interrupt vector | `ign_input_shutdown`; no vector exists on host |
| `0x63ae0` | Initialize two joystick state records | `ign_input_init`, `open_device` |
| `0x63bb0` | Poll and copy calibrated axes/buttons into enabled device records | `ign_input_event`, `ign_input_sample` |
| `0x63c74` | Return zero (stub) | SDL lifecycle requires no corresponding action |
| `0x63c78` | Reset first joystick calibration | `open_device` defaults / `ign_input_calibrate_axis` |
| `0x63c80` | Reset second joystick calibration | Same, slot 1 |
| `0x63c88` | Reset both calibrations | Same per-slot APIs |
| `0x63c90` | Sample first port axes/buttons and accumulate calibration extrema | SDL events + explicit calibration API; DOS interactive extrema procedure not reproduced |
| `0x63c98` | Sample second port axes/buttons and accumulate extrema | Same for slot 1 |
| `0x63ca0` | Invoke four-axis calibration sampler at `0x7cf35` | SDL independent pedal axes + explicit calibration API |
| `0x63ca8` | Finalize first joystick center/range, enable it | `open_device`, `ign_input_calibrate_axis` |
| `0x63cc0` | Finalize second center/range, enable it | Same for slot 1 |
| `0x63cd8` | Finalize both and enable them | Same per-slot APIs |
| `0x7cd9c` | Reset first-port min/max/center/ranges and timing bound | `open_device` calibration defaults; CPU timing bound has no SDL counterpart |
| `0x7cdf8` | Reset second-port calibration | Same for slot 1 |
| `0x7ce54` | Reset both | Same per-slot APIs |
| `0x7ce61` | Accumulate first-port raw extrema and return first pressed button + 1 | SDL event state and explicit calibration; interactive accumulation unimplemented |
| `0x7cec7` | **Embedded return-value dword, not a function entry** | No native function; disassembling it falls through into `0x7cecb` |
| `0x7cecb` | Accumulate second-port extrema and button selection | Same calibration boundary |
| `0x7cf31` | **Embedded return-value dword, not a function entry** | Actual following routine begins at `0x7cf35` |
| `0x7cf9b` | **Embedded return-value dword, not a function entry** | Belongs to four-axis sampler `0x7cf35`; following code starts `0x7cf9f` |
| `0x7cf9f` | Capture first-port center, compute spans clamped to >=1, choose timeout | `ign_input_calibrate_axis`; invalid spans rejected, no DOS timeout |
| `0x7d042` | Capture second-port center/ranges | Same for slot 1 |
| `0x7d0e5` | Finalize both | Same per-slot APIs |
| `0x7d0f2` | Read game-port buttons; time four RC axes; scale `(raw-center)*215/span`, clamp +/-100; decode extra control lookup | SDL signed axes/triggers and `ign_input_axis`; SDL deadzone/asymmetric span deliberately replace RC timing, factor 215 and lookup |

The catalog should not count the three embedded return-value dwords as independently ported
functions. Similarly, original calibration dialogs, DOS callbacks and timer-repeat tuning
are not source-equivalent merely because SDL replaces their hardware responsibilities.

## Verification

`input_backend_test` covers initial/repeated/held/released keys, configured bindings and
legacy aliases, FIFO saturation, typed history, inter-tick gear taps, simultaneous gear
requests, focus/window isolation, invalid/asymmetric/endpoint calibration, clamped axes,
auto-acceleration/brake precedence, synthetic SDL events, actual SDL virtual-device
enumeration and hotplug/removal, keyboard fallback, mapped/raw stream deduplication,
gamepad triggers/buttons, edge consumption and repeat-safe shutdown.
