#!/usr/bin/env python3
"""Verify bounded input/gear decisions against unmodified Ignition LE instructions."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import platform
import struct
import subprocess
import sys

from car_original import CAR, CAR_SIZE, INFO
from physics_original import (Original, BODY, BODY_SIZE, ESP, STACK, RETURN, ROOT, ORIGINAL_SHA256,
                              FPCW, EXTENDED_FPCW)

FIELDS = {"E": ("human_steer",), "D": ("throttle",), "A": ("gear", "sliding", "skid_timer"),
          "K": ("gear", "gear_ticks"), "H": ("gear",),
          "J": ("request_event", "turbo_mode", "turbo_wait"),
          "P": ("turbo_request", "turbo_key")}


class InputOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(CAR, 0x5000)
        self.uc.mem_map(INFO, 0x1000)
        self.visits = {k: set() for k in FIELDS}
        self.active = None
        self.boundaries = set()
        self.fragment_executions = {}

    def code(self, uc, address, size, _):
        ranges = {"E": [(0x2C5B8, 0x2C906), (0x55C28, 0x55C38),
                        (0x51760, 0x517F6), (0x55F9C, 0x55FB8)],
                  "D": [(0x4D5F4, 0x4D6A7)],
                  "A": [(0x4D6E2, 0x4D8CD), (0x4E1BC, 0x4E2D7)],
                  "K": [(0x502C4, 0x50463), (0x55F9C, 0x55FB8)],
                  "H": [(0x2CB29, 0x2CB45), (0x2CBB2, 0x2CBCC)],
                  "J": [(0x4D8E1, 0x4D949), (0x4D9DD, 0x4DA41),
                        (0x4DA8F, 0x4DA8F), (0x4DAA1, 0x4DAC4), (0x4DAD1, 0x4DAF2)],
                  "P": [(0x2C93A, 0x2C9CA), (0x2CC9B, 0x2CCAF)]}
        if self.active in ("J", "P") and address in self.boundaries:
            uc.emu_stop()
            return
        if not any(lo <= address <= hi for lo, hi in ranges[self.active]):
            raise ValueError(f"Unexpected {self.active} execution at 0x{address:x}")
        self.visits[self.active].add(address)

    def write(self, uc, access, address, size, value, _):
        if not any(lo <= address and address + size <= hi for lo, hi in self.allowed):
            raise ValueError(f"Unexpected {self.active} write at 0x{address:x}")

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address):
        return struct.unpack("<i", self.uc.mem_read(address, 4))[0]

    def start(self, mode, values):
        self.active = mode
        self.uc.mem_write(CAR, b"\xa5" * CAR_SIZE)
        self.uc.mem_write(BODY, b"\xa5" * BODY_SIZE)
        self.uc.mem_write(INFO, bytes(76))
        self.put(0x1FEFE0, "I", CAR)
        self.put(0x1FEFE8, "I", BODY)
        self.put(0x1FEFD0, "I", INFO)
        self.put(0x1FEFF0, "i", 0)
        # D/A/K select the sound-free original branch; J supplies both control kinds.
        self.put(INFO + 4, "i", values.get("ai", 1))
        self.put(INFO + 12, "i", values.get("automatic", 1))
        self.put(INFO + 16, "2i", values.get("counter", 0), values.get("delay", 0))
        self.put(CAR + 0x27C, "i", values.get("gear", 1))
        self.put(CAR + 0x528, "i", values.get("out", 0))
        self.put(CAR + 0x90, "d", values.get("slip", 0))
        self.put(BODY + 0x98, "d", values.get("speed", 0))
        self.put(BODY + 0xD4, "i", values.get("sliding", 0))
        self.put(CAR + 0x2F8, "i", values.get("skid", 7))
        self.put(0x1FB4E8, "d", values.get("clock", -1 if values.get("started", 1) else 0))
        self.allowed = [(STACK, STACK + 0x10000)]
        if mode == "E":
            self.put(INFO + 8, "i", 0)
            self.put(INFO + 0x18, "i", 0)
            self.put(INFO + 0x1C, "4B", 0xCB, 0xCD, 0xC8, 0xD0)
            self.uc.mem_write(0x20ACB0, bytes(256))
            self.put(0x20ACB0 + 0xCB, "B", values["left"])
            self.put(0x20ACB0 + 0xCD, "B", values["right"])
            self.put(CAR + 0x344, "i", 73)
            self.put(CAR + 0x364, "i", 0)
            self.put(CAR + 0x558, "i", values["flat"])
            self.put(CAR + 0x9A4, "i", 0)
            self.put(0x1FEFF8, "I", 0x1FB6D8)
            self.put(0x1FB6D8 + 4, "d", 0)
            self.allowed += [(CAR + 0x344, CAR + 0x348), (CAR + 0x9A4, CAR + 0x9B0),
                             (INFO + 0x24, INFO + 0x28), (0x1FEFF0, 0x1FEFF4)]
        elif mode == "P":
            self.put(CAR + 0x52C, "i", -123)
            self.put(CAR + 0x530, "i", values["latch"])
            self.put(CAR + 0x53C, "i", values["mode"])
            self.put(CAR + 0x3A4, "d", values["elapsed"])
            for offset, field in ((0x354, "exploded"), (0x358, "sunk"), (0x35C, "slid")):
                self.put(CAR + offset, "i", values[field])
            self.allowed += [(CAR + 0x52C, CAR + 0x534)]
        elif mode == "J":
            self.put(CAR + 0x534, "d", values["charge"])
            self.put(CAR + 0x53C, "i", values["mode"])
            self.put(CAR + 0x548, "i", values["wait"])
            self.allowed += [(CAR + 0x53C, CAR + 0x540), (CAR + 0x548, CAR + 0x54C)]
        elif mode == "D":
            self.allowed += [(0x1FECEC, 0x1FECF4), (0x1DADE0, 0x1DADF0)]
        elif mode == "A":
            self.allowed += [(CAR + 0x27C, CAR + 0x280), (CAR + 0x2F8, CAR + 0x2FC),
                             (BODY + 0xD4, BODY + 0xD8), (0x1FEFE8, 0x1FEFEC),
                             (0x1FEFF0, 0x1FEFF4)]
        elif mode == "K":
            pattern = values["pattern"]
            for i in range(200):
                self.put(0x1FB6D8 + 2 * i, "h", i - 100)
                self.put(0x1FB868 + 2 * i, "h",
                         (i if pattern == 0 else 199 - i if pattern in (1, 2) else 0) - 100)
                if pattern == 3:
                    self.put(0x1FB6D8 + 2 * i, "h", -32768 if i % 2 else 32767)
                    self.put(0x1FB868 + 2 * i, "h", 32767 if i % 2 else -32768)
            self.allowed += [(CAR + 0x27C, CAR + 0x280), (INFO + 16, INFO + 20)]
        else:
            self.allowed += [(CAR + 0x27C, CAR + 0x280)]

    def step(self, mode, values, tick=0):
        for register in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + register), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, self.fpcw)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.put(ESP, "I", RETURN)
        if mode == "P":
            key = (values["key"] if values["pattern"] == 0 else
                   (values["key"] if tick % 2 else 0) if values["pattern"] == 1 else tick % 3)
            self.uc.reg_write(self.reg.UC_X86_REG_EAX, key)
            self.fragment(0x2C93A, (0x2C9D4,))
            request = self.get(CAR + 0x52C)
            self.fragment(0x2CC9B, (0x2CCB5,))
            return [request, self.get(CAR + 0x530)]
        if mode == "J":
            return self.request_step(values, tick)
        if mode == "E":
            entry, end, stack = 0x2C5B8, 0x2C90C, ESP - 32
        elif mode == "D":
            self.uc.reg_write(self.reg.UC_X86_REG_EAX, values["accel"])
            self.uc.reg_write(self.reg.UC_X86_REG_EDX, values["brake"])
            entry, end, stack = 0x4D5F4, 0x4D6A9, ESP - 20
        elif mode == "A":
            self.uc.reg_write(self.reg.UC_X86_REG_EDI, values["accel"])
            self.uc.reg_write(self.reg.UC_X86_REG_ESI, values["brake"])
            entry, end, stack = 0x4D6E2, 0x4D8D8, ESP
        elif mode == "K":
            if values["pattern"] == 2:
                speed = 0 if tick % 3 == 0 else values["speed"] * (1 if tick % 3 == 1 else -1)
                self.put(BODY + 0x98, "d", speed)
                self.put(CAR + 0x90, "d", values["slip"] if tick % 4 == 0 else math.pi)
            entry, end, stack = 0x502C4, RETURN, ESP + 4
        else:
            entry, end, stack = ((0x2CB29, 0x2CB4B, ESP) if values["shift"] > 0 else
                                 (0x2CBB2, 0x2CBD2, ESP))
        try:
            self.uc.emu_start(entry, end, timeout=1_000_000, count=10000)
        except self.emulator.UcError as error:
            eip = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"{mode} failed at 0x{eip:x}: {error}") from error
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != end or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != stack or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != self.fpcw):
            raise ValueError(f"{mode} execution boundary/x87 contract failed")
        if mode == "E":
            return [self.get(CAR + 0x344)]
        if mode == "D":
            return [struct.unpack("<i", struct.pack("<I", self.uc.reg_read(self.reg.UC_X86_REG_EAX)))[0]]
        if mode == "A":
            if self.get(0x1FEFE8) != BODY or self.get(0x1FEFF0) != 0:
                raise ValueError("Brake changed active body/index")
            return [self.get(CAR + 0x27C), self.get(BODY + 0xD4), self.get(CAR + 0x2F8)]
        if mode == "K":
            return [self.get(CAR + 0x27C), self.get(INFO + 16)]
        return [self.get(CAR + 0x27C)]

    def fragment(self, entry, boundaries):
        self.boundaries = set(boundaries)
        try:
            self.uc.emu_start(entry, RETURN, timeout=1_000_000, count=10000)
        except self.emulator.UcError as error:
            raise ValueError(f"Turbo {self.active} fragment failed: {error}") from error
        end = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
        if (end not in self.boundaries or self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != self.fpcw):
            raise ValueError(f"Turbo {self.active} fragment boundary/x87 contract failed at 0x{end:x}")
        key = f"0x{entry:x}->0x{end:x}"
        self.fragment_executions[key] = self.fragment_executions.get(key, 0) + 1
        return end

    def request_step(self, values, tick):
        request = (values["request"] if values["pattern"] == 0 else
                   (values["request"] if tick % 2 else 0) if values["pattern"] == 1 else tick % 3)
        self.uc.reg_write(self.reg.UC_X86_REG_EBP, request)
        end = self.fragment(0x4D8E1, (0x4D949, 0x4DA41, 0x4DAA1, 0x4DAF2, 0x4DB89))
        event = {0x4D949: 1, 0x4DA41: 2, 0x4DAF2: 3}.get(end, 0)
        if event == 2:
            # Independent post-audio state commit; no sound callee or return value is emulated.
            self.uc.reg_write(self.reg.UC_X86_REG_ESI, CAR)
            self.uc.reg_write(self.reg.UC_X86_REG_EAX, 0)
            self.fragment(0x4DA8F, (0x4DA9A,))
        if request == 1:
            # Independent countdown fragment, with the decision/commit state supplied above.
            self.fragment(0x4DAA1, (0x4DACA, 0x4DB89))
        return [event, self.get(CAR + 0x53C), self.get(CAR + 0x548)]


def neighbours(value):
    return (math.nextafter(value, -math.inf), value, math.nextafter(value, math.inf))


def fixtures():
    cases = []
    for left, right, flat in itertools.product(range(2), range(2), (-1, 0, 1, 199, 200, 201)):
        cases.append(("E", 1, dict(left=left, right=right, flat=flat)))
    slips = [0, math.pi, 2 * math.pi, *neighbours(-1.5707963267949),
             *neighbours(1.5707963267949), *neighbours(4.7123889803847)]
    for gear, automatic, accel, brake in itertools.product(range(3), range(2),
                                                         (0, 1, 50, 51, 100), (0, 5, 6, 50, 51, 100)):
        cases.append(("D", 1, dict(gear=gear, automatic=automatic, accel=accel, brake=brake,
                                   started=1, slip=0, speed=0, sliding=0, skid=7)))
    for gear, automatic, accel, brake, slip, speed, started in itertools.product(
            range(3), range(2), (0, 50, 51, 100), (0, 5, 6, 50, 51, 80, 81, 100), slips,
            (-3, 0, *neighbours(2.0), 3), range(2)):
        cases.append(("A", 1, dict(gear=gear, automatic=automatic, accel=accel, brake=brake,
                                   started=started, slip=slip, speed=speed, sliding=1, skid=7)))
    speeds = [0, -100, 100, *neighbours(100 / 3.6), *neighbours(-100 / 3.6),
              *neighbours(199 / 3.6)]
    for gear, pattern, slip, speed, delay in itertools.product(
            range(3), (-1, 0, 1), slips, speeds, (0, 1, 30)):
        cases.append(("K", delay + 3, dict(gear=gear, counter=0, delay=delay,
                                          speed=speed, slip=slip, pattern=pattern)))
    for gear, shift in itertools.product(range(3), (-1, 1)):
        cases.append(("H", 1, dict(gear=gear, shift=shift)))
    for gear, slip, delay, counter in itertools.product(range(3), slips, (0, 1, 30), (0, 1, 30, 31)):
        cases.append(("K", 40, dict(gear=gear, counter=counter, delay=delay,
                                    speed=100 / 3.6, slip=slip, pattern=2)))
    for index in range(201):
        for speed in neighbours(index / 3.6):
            for sign in (-1, 1):
                cases.append(("K", 3, dict(gear=1, counter=0, delay=0,
                                           speed=sign * speed, slip=0, pattern=3)))
    charges = (0, *neighbours(20.0), *neighbours(100.0), 101)
    clocks = (-1, math.nextafter(0, -math.inf), 0, math.nextafter(0, math.inf), 1)
    for request, mode, charge, wait, ai, out, clock in itertools.product(
            range(3), range(3), charges, (-3, 0, 1, 2, 29, 30, 31), range(2), range(2), clocks):
        cases.append(("J", 4, dict(request=request, mode=mode, charge=charge, wait=wait,
                                  ai=ai, out=out, clock=clock, pattern=0)))
    for request, mode, charge, wait, ai, pattern in itertools.product(
            range(3), range(3), (20, 50, 100, 101), (0, 1, 30), range(2), (0, 1, 2)):
        cases.append(("J", 65, dict(request=request, mode=mode, charge=charge, wait=wait,
                                   ai=ai, out=0, clock=-1, pattern=pattern)))
    for key, latch, mode, exploded, sunk, slid, elapsed, pattern in itertools.product(
            range(3), range(3), range(3), range(2), range(2), range(2),
            (-1, *neighbours(0.0), *neighbours(0.25), 1), range(3)):
        cases.append(("P", 6, dict(key=key, latch=latch, mode=mode, exploded=exploded,
                                  sunk=sunk, slid=slid, elapsed=elapsed, pattern=pattern)))
    return cases


def encode(mode, ticks, v):
    if mode == "E":
        values = [v[k] for k in ("left", "right", "flat")]
    elif mode == "P":
        values = [ticks, *(v[k] for k in ("key", "latch", "mode", "exploded", "sunk", "slid",
                                         "elapsed", "pattern"))]
    elif mode == "J":
        values = [ticks, *(v[k] for k in ("request", "mode", "charge", "wait", "ai", "out", "clock", "pattern"))]
    elif mode in ("D", "A"):
        values = [v[k] for k in ("gear", "accel", "brake", "automatic", "started", "slip",
                                 "speed", "sliding", "skid")]
    elif mode == "K":
        values = [ticks, *(v[k] for k in ("gear", "counter", "delay", "speed", "slip", "pattern"))]
    else:
        values = [v["gear"], v["shift"]]
    return mode + " " + " ".join(format(v, ".17g") for v in values)


def compare(mode, row, expected):
    actual = [float(v) for v in row.split()]
    if len(actual) != len(FIELDS[mode]) or not all(math.isfinite(v) and v.is_integer() for v in actual):
        raise ValueError(f"Malformed {mode} output")
    return [dict(field=field, port=a, original=b)
            for field, a, b in zip(FIELDS[mode], actual, expected) if a != b]


def verify(executable, runner):
    original = InputOriginal(executable)
    cases = fixtures()
    text = "\n".join(encode(mode, ticks, v) for mode, ticks, v in cases) + "\n"
    result = subprocess.run([str(runner)], input=text, text=True, capture_output=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != sum(ticks for _, ticks, _ in cases):
        raise ValueError("Input oracle output row count mismatch")
    failures, cursor, starts = [], 0, []
    for index, (mode, ticks, values) in enumerate(cases):
        starts.append(cursor)
        original.start(mode, values)
        for tick in range(ticks):
            expected = original.step(mode, values, tick)
            failures.extend(dict(case=index, mode=mode, tick=tick, **diff)
                            for diff in compare(mode, rows[cursor], expected))
            cursor += 1
    # Negative control: with 64-bit precision the original truncates the unrounded v*3.6 product, so the
    # gearbox index sweep must expose gear differences the 53-bit run does not have.
    # A fresh emulator: cached translations from later modes would run past earlier stop addresses.
    control_original, control = InputOriginal(executable), 0
    control_original.fpcw = EXTENDED_FPCW
    for (mode, ticks, values), start in zip(cases, starts):
        if mode == "K" and values["pattern"] == 3:
            control_original.start(mode, values)
            for tick in range(ticks):
                control += bool(compare(mode, rows[start + tick], control_original.step(mode, values, tick)))
    if not control:
        raise ValueError("Extended-precision control word did not change any gearbox decision")
    required = {"E": {0x55C28, 0x2C71C, 0x2C809, 0x2C8B9, 0x2C904},
                "D": {0x4D66F, 0x4D69F, 0x4D6A3, 0x4D6A7},
                "A": {0x4D703, 0x4D70F, 0x4D79A, 0x4D821, 0x4D8BD,
                      0x4E238, 0x4E247, 0x4E265, 0x4E2AD},
                "K": {0x55F9C, 0x50311, 0x50374, 0x5037B, 0x50381, 0x50388,
                      0x503BD, 0x503C9, 0x50449, 0x50454},
                "H": {0x2CB42, 0x2CBC9},
                "J": {0x4D90B, 0x4D919, 0x4D92A, 0x4D937, 0x4D93D, 0x4D943,
                      0x4D9F8, 0x4DA05, 0x4DA1A, 0x4DA3F, 0x4DA8F, 0x4DABB, 0x4DAC4,
                      0x4DAD4, 0x4DAE0, 0x4DAEB},
                "P": {0x2C93A, 0x2C940, 0x2C953, 0x2C95C, 0x2C965, 0x2C96E, 0x2C985,
                      0x2C99E, 0x2C9AF, 0x2C9B8, 0x2C9C1, 0x2C9CA, 0x2CCAF}}
    for mode, addresses in required.items():
        if not addresses <= original.visits[mode]:
            raise ValueError(f"Missing {mode} branches: {sorted(addresses - original.visits[mode])}")
    import unicorn
    return dict(status="fail" if failures else "pass", cases=len(cases), steps=cursor, failures=failures,
                kernels={mode: dict(cases=sum(m == mode for m, _, _ in cases),
                                    steps=sum(t for m, t, _ in cases if m == mode)) for mode in FIELDS},
                original_sha256=ORIGINAL_SHA256, runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                fixture_input_sha256=hashlib.sha256(text.encode()).hexdigest(),
                source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in (ROOT / "src/game/car.c", ROOT / "src/game/car.h",
                                         ROOT / "tests/car_kernels.c", ROOT / "tests/physics_original.py",
                                         ROOT / "tests/car_original.py", ROOT / "tools/le2elf.py",
                                         Path(__file__).resolve())},
                observed_at=datetime.now(timezone.utc).isoformat(), host=platform.platform(),
                python=platform.python_version(), unicorn=unicorn.__version__, tolerance=0,
                x87_control_word=hex(FPCW), extended_control_word_differing_steps=control,
                required_addresses_executed={k: [hex(a) for a in sorted(v)] for k, v in required.items()},
                turbo_fragment_executions=original.fragment_executions,
                turbo_event_boundaries={"0x4d949": "start effect call (event 1)",
                                        "0x4da41": "refusal audio setup (event 2)",
                                        "0x4daf2": "forced effect call (event 3)",
                                        "0x4daa1/0x4db89": "no external event (event 0)"},
                scope={"E": "0x2c5b8..0x2c90c: digital steering, real key polling 0x55c28 "
                            "from supplied keyboard state; section 0, zero pedals/auto-accel; "
                            "compares car+0x344 including simultaneous keys and flattened inversion",
                       "D": "0x4d5f4..0x4d6a9, fresh supplied pedals, before throttle call",
                       "A": "0x4d6e2..0x4d8d8, executes real car_brake, audio-free ctrl+4=1",
                       "K": "complete 0x502c4 with real __CHP and ctrl+4=1, supplied shared int16 curves",
                       "H": "manual shift arithmetic 0x2cb29..0x2cb4b / 0x2cbb2..0x2cbd2",
                       "J": "composed original decision to effect/audio boundary, independent refusal commit "
                            "0x4da8f..0x4da9a and countdown 0x4daa1..0x4daca/0x4db89; no callee stubs",
                       "P": "0x2c93a..0x2c9d4 after supplied key sample; independent latch "
                            "0x2cc9b..0x2ccb5 without intervening dispatcher/camera/gear calls"},
                exclusions=["Keyboard hardware acquisition, joystick polling and shared gear key latch",
                            "E auxiliary section heading/side outputs, auto-acceleration and pedal mapping",
                            "Cached input sentinel 0x1f02 and input recording/playback",
                            "Human shift audio and control model 8 extra callee",
                            "Generation of shared torque curves, configuration initialization",
                            "Turbo audio/effect callees, their state side effects and sound handles; "
                            "J/P are fragment compositions, not contiguous dispatcher/human input execution",
                            "AI turbo request generation, original elapsed-time production and inactivity flag",
                            "Full dispatcher call sequence, whole tick and DOS race"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "runner", "report"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    args.report.write_text('{"status":"running"}\n')
    try:
        report = verify(args.original, args.runner.resolve())
    except (ValueError, OSError, ModuleNotFoundError, subprocess.SubprocessError) as error:
        report = dict(status="fail", error=str(error))
        print(f"FAIL: {error}", file=sys.stderr)
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k in ("status", "cases", "steps", "error", "kernels")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
