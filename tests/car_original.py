#!/usr/bin/env python3
"""Replay original car arithmetic kernels; not complete car_update or input dispatch."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import platform
import random
import struct
import subprocess
import sys

from physics_original import (Original, BODY, BODY_SIZE, ESP, STACK, RETURN, ROOT, ORIGINAL_SHA256,
                              FPCW, EXTENDED_FPCW)

CAR, INFO = 0x410000, 0x420000
CAR_SIZE = 0x484C
# Bit-exact under the game's 53-bit x87 control word (no FMA contraction in the port).
ABS_TOL, REL_TOL = 0.0, 0.0
FIELDS = {"N": ("drive_heading", "body_heading"),
          "S": ("steer_visual", "steer"), "G": ("grip_avg", "grip_f", "grip_r"),
          "V": ("height", "fall_speed", "drop", "airborne", "landed"),
          "T": ("drive", "creep", "creep_ticks", "catchup", "catchup_clock", "skid_timer"),
          "B": ("sliding", "skid_timer"),
          "U": ("charge", "mode", "frames", "drive", "vx", "vz"),
          "R": ("revs",),
          "Q": ("drive", "creep", "creep_ticks", "catchup", "catchup_clock", "skid_timer", "revs", "index")}
EXACT = {"N": (), "S": (), "G": (), "V": (3, 4), "T": (1, 2, 5), "B": (0, 1), "U": (1, 2),
         "R": (), "Q": (1, 2, 5, 7)}


class CarOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(CAR, 0x5000)
        self.uc.mem_map(INFO, 0x1000)
        self.active = None
        self.visits = {}
        self.stopped_at_landing = False

    def code(self, uc, address, size, _):
        ranges = {"N": [(0x2D9C7, 0x2D9E8), (0x5192E, 0x5197C)],
                  "S": [(0x500F0, 0x502C1)], "G": [(0x3070A, 0x3086E)],
                  "V": [(0x2E234, 0x2E397), (0x2E600, 0x2E65D)],
                  "T": [(0x4DB90, 0x4DFB4), (0x55F9C, 0x55FB8)],
                  "B": [(0x4E1BC, 0x4E2D7)],
                  "U": [(0x4E3A7, 0x4E5CA), (0x4E634, 0x4E64B)],
                  "R": [(0x4DFBE, 0x4E1A1)],
                  "Q": [(0x4DB90, 0x4E1A1), (0x55F9C, 0x55FB8)]}
        if not any(lo <= address <= hi for lo, hi in ranges[self.active]):
            raise ValueError(f"Unexpected {self.active} execution at 0x{address:x}")
        self.visits.setdefault(self.active, set()).add(address)
        if self.active == "V" and address == 0x2E397:
            # Observe the arithmetic boundary, without replacing sound/fx callees.
            self.stopped_at_landing = True
            uc.emu_stop()

    def write(self, uc, access, address, size, value, _):
        if not any(lo <= address and address + size <= hi for lo, hi in self.allowed):
            raise ValueError(f"Unexpected {self.active} write at 0x{address:x}")

    def put(self, address, fmt, *values):
        self.uc.mem_write(address, struct.pack("<" + fmt, *values))

    def get(self, address, fmt):
        return struct.unpack("<" + fmt, self.uc.mem_read(address, struct.calcsize("<" + fmt)))[0]

    def start_case(self, mode, values):
        self.active = mode
        self.uc.mem_write(CAR, b"\xa5" * CAR_SIZE)
        self.uc.mem_write(BODY, b"\xa5" * BODY_SIZE)
        self.uc.mem_write(INFO, bytes(76))
        self.put(0x1FEFE0, "I", CAR)
        self.put(0x1FEFE8, "I", BODY)
        self.put(0x1FEFD0, "I", INFO)
        self.put(0x1FEFF0, "i", 0)
        self.put(0x1FEFE4, "i", 1)
        self.put(CAR + 0x528, "i", 0)
        self.put(INFO, "3i", values.get("spec", 0), values.get("ai", 0), values.get("analog", 0))
        if mode == "N":
            self.put(BODY + 0x28, "d", values["heading"])
            self.allowed = [(CAR + 0xF8, CAR + 0x100)]
        elif mode == "S":
            self.put(CAR + 0xA8, "d", 10.0)
            self.put(CAR + 0x118, "d", values["kmh"])
            self.put(CAR + 0x148, "d", values["grip"])
            self.put(CAR + 0xB0, "d", values["visual"])
            self.put(BODY + 0xAC, "d", values["steer"])
            self.allowed = [(CAR + 0xB0, CAR + 0xB8), (BODY + 0xAC, BODY + 0xB4),
                            (0x1FEFF0, 0x1FEFF4)]
        elif mode == "G":
            self.put(CAR + 0x150, "4i", *values["types"])
            self.put(CAR + 0x148, "d", 0.4)
            self.put(BODY + 0xC4, "2d", 7.0, 8.0)
            self.put(0x1FEDD8, "i", values["difficulty"])
            self.allowed = [(CAR + 0x148, CAR + 0x150), (BODY + 0xC4, BODY + 0xD4)]
        elif mode == "V":
            for offset, name in ((8, "height"), (0x20, "fall_speed"), (0x118, "kmh"),
                                 (0x120, "ground"), (0x128, "ground_prev")):
                self.put(CAR + offset, "d", values[name])
            self.put(CAR + 0x270, "i", values["airborne"])
            self.allowed = [(CAR + 8, CAR + 16), (CAR + 0x20, CAR + 0x28),
                            (CAR + 0x270, CAR + 0x274), (0x1FEFE0, 0x1FEFE8)]
        elif mode in ("T", "Q"):
            self.put(CAR + 0x27C, "i", values["gear"])
            self.put(CAR + 0x51C, "3i", values["creep"], values["creep_ticks"], values["bonus"])
            self.put(CAR + 0x594, "2d", values["catchup"], values["catchup_clock"])
            self.put(CAR + 0x39C, "i", values["position"])
            self.put(CAR + 0x90, "d", values["slip"])
            self.put(CAR + 0x118, "d", values["kmh"])
            self.put(CAR + 0x2F8, "i", values["skid"])
            self.put(BODY + 0xD8, "d", values["drive"])
            self.put(0x1FEDD8, "i", values["difficulty"])
            self.put(0xC0B50, "i", 1)
            self.put(0x1FEEFC, "i", 0)
            for gear in range(3):
                self.put(0x1FB548 + values["spec"] * 1200 + gear * 400, "200h",
                         *(i - 5 + gear for i in range(200)))
            self.allowed = [(CAR + 0x51C, CAR + 0x524), (CAR + 0x594, CAR + 0x5A4),
                            (CAR + 0x5A8, CAR + 0x5AC), (CAR + 0x2F8, CAR + 0x2FC),
                            (BODY + 0xD8, BODY + 0xE0)]
            if mode == "Q":
                self.put(CAR + 0x288, "d", values["revs"])
                self.put(CAR + 0x298, "i", values["skidding"])
                self.allowed += [(CAR + 0x288, CAR + 0x290), (0x1FEFE0, 0x1FEFE4),
                                 (0x1FEFF0, 0x1FEFF4)]
        elif mode == "R":
            self.put(CAR + 0x27C, "i", values["gear"])
            self.put(CAR + 0x288, "d", values["revs"])
            self.put(CAR + 0x270, "i", values["airborne"])
            self.put(CAR + 0x298, "i", values["skidding"])
            self.put(CAR + 0x528, "i", values["out"])
            self.put(CAR + 0x118, "d", values["kmh"])
            self.put(0x1FB4E8, "d", -1 if values["started"] else 0)
            self.allowed = [(CAR + 0x288, CAR + 0x290), (0x1FEFE0, 0x1FEFE4),
                            (0x1FEFF0, 0x1FEFF4)]
        elif mode == "B":
            self.put(BODY + 0x98, "d", values["speed"])
            self.put(BODY + 0xD4, "i", values["sliding"])
            self.put(CAR + 0x2F8, "i", values["skid"])
            self.put(0x1FB4E8, "d", -1 if values["started"] else 0)
            self.allowed = [(BODY + 0xD4, BODY + 0xD8), (CAR + 0x2F8, CAR + 0x2FC),
                            (0x1FEFE8, 0x1FEFEC), (0x1FEFF0, 0x1FEFF4)]
        elif mode == "U":
            # Index 2 takes the original clamp path without calling a human recharge sound.
            self.put(0x1FEFE0, "I", CAR - 2 * CAR_SIZE)
            self.put(0x1FEFE8, "I", BODY - 2 * BODY_SIZE)
            self.put(0x1FEFD0, "I", INFO - 2 * 76)
            self.put(0x1FEFF0, "i", 2)
            self.put(0x1FEF14, "i", 0)
            self.put(CAR + 0x534, "d", values["charge"])
            self.put(CAR + 0x53C, "i", values["mode"])
            self.put(CAR + 0x54C, "i", values["frames"])
            self.put(CAR + 0xF8, "d", values["heading"])
            self.put(BODY + 0x98, "d", values["speed"])
            self.put(BODY + 0xD8, "d", values["drive"])
            self.put(BODY + 0x78, "2d", values["vx"], values["vz"])
            self.allowed = [(CAR + 0x534, CAR + 0x540), (CAR + 0x54C, CAR + 0x550),
                            (BODY + 0xD8, BODY + 0xE0), (BODY + 0x78, BODY + 0x88)]
        self.allowed.append((STACK, STACK + 0x10000))

    def step(self, mode, values):
        self.stopped_at_landing = False
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, self.fpcw)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.put(ESP, "I", RETURN)
        if mode == "N":
            self.uc.reg_write(self.reg.UC_X86_REG_ESI, 0)
            self.uc.reg_write(self.reg.UC_X86_REG_EDI, 0)
        if mode == "G":
            self.put(ESP + 0x344, "i", 0)
            self.put(CAR + 0x150, "4i", *values["types"])
        if mode in ("T", "Q"):
            self.put(BODY + 0x98, "d", values["speed"])
            self.put(BODY + 0xA8, "i", values["saturated"])
            self.put(CAR + 0x270, "i", values["airborne"])
            self.put(0x1FB4E8, "d", -1 if values["started"] else 0)
            self.put(0x1FB4D8, "d", values["clock"])
            self.uc.reg_write(self.reg.UC_X86_REG_EDX, int(values["gear"] > 0))
        if mode == "U":
            self.uc.reg_write(self.reg.UC_X86_REG_ESI, 2)
        if mode == "R":
            self.put(ESP + 0x1C, "i", values["index"])
            self.put(ESP + 0x20, "i", values["input"])
            self.uc.reg_write(self.reg.UC_X86_REG_EBX, 0)
            self.uc.reg_write(self.reg.UC_X86_REG_ECX, CAR)
        self.uc.reg_write(self.reg.UC_X86_REG_EAX, values.get("input", 0))
        entry, end = {"N": (0x2D9C7, 0x2D9EE),
                      "S": (0x500F0, RETURN), "G": (0x3070A, 0x30874), "V": (0x2E234, RETURN),
                      "T": (0x4DB90, 0x4DFBE), "B": (0x4E1BC, RETURN), "U": (0x4E3A7, 0x4E656),
                      "R": (0x4DFBE, 0x4E1A7), "Q": (0x4DB90, 0x4E1A7)}[mode]
        try:
            self.uc.emu_start(entry, end, timeout=1_000_000, count=10000)
        except self.emulator.UcError as error:
            eip = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"{mode}: original emulation failed at 0x{eip:x}: {error}") from error
        eip = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
        expected_end = 0x2E397 if self.stopped_at_landing else end
        expected_esp = (ESP - 48 if self.stopped_at_landing else ESP - 56 if mode in ("T", "Q") else
                        ESP if mode in ("G", "U", "R", "N") else ESP + 4)
        if eip != expected_end or self.uc.reg_read(self.reg.UC_X86_REG_ESP) != expected_esp:
            raise ValueError(f"{mode}: unexpected execution boundary EIP=0x{eip:x}")
        if (self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != self.fpcw):
            raise ValueError(f"{mode}: x87 stack/control word changed")
        if mode == "N":
            return [self.get(CAR + 0xF8, "d"), self.get(BODY + 0x28, "d")]
        if mode == "S":
            return [self.get(CAR + 0xB0, "d"), self.get(BODY + 0xAC, "d")]
        if mode == "G":
            return [self.get(CAR + 0x148, "d"), self.get(BODY + 0xC4, "d"), self.get(BODY + 0xCC, "d")]
        if mode == "R":
            return [self.get(CAR + 0x288, "d")]
        if mode in ("T", "Q"):
            output = [self.get(BODY + 0xD8, "d"), self.get(CAR + 0x51C, "i"), self.get(CAR + 0x520, "i"),
                      self.get(CAR + 0x594, "d"), self.get(CAR + 0x59C, "d"), self.get(CAR + 0x2F8, "i")]
            if mode == "Q":
                output += [self.get(CAR + 0x288, "d"), self.get(ESP - 56 + 0x1C, "i")]
            return output
        if mode == "B":
            return [self.get(BODY + 0xD4, "i"), self.get(CAR + 0x2F8, "i")]
        if mode == "U":
            return [self.get(CAR + 0x534, "d"), self.get(CAR + 0x53C, "i"), self.get(CAR + 0x54C, "i"),
                    self.get(BODY + 0xD8, "d"), self.get(BODY + 0x78, "d"), self.get(BODY + 0x80, "d")]
        return [self.get(CAR + 8, "d"), self.get(CAR + 0x20, "d"), self.get(ESP - 40, "d"),
                self.get(CAR + 0x270, "i"), int(self.stopped_at_landing)]


def fixtures():
    cases = []
    tau = 6.2831853071796
    angles = [0.0, -0.0]
    for turns in (-64, -10, -3, -2, -1, 0, 1, 2, 3, 10, 64):
        angle = turns * tau
        angles += [math.nextafter(angle, -math.inf), angle, math.nextafter(angle, math.inf)]
    heading_rng = random.Random(0x51938)
    angles += [heading_rng.uniform(-64 * tau, 64 * tau) for _ in range(256)]
    for i, angle in enumerate(angles):
        cases.append(("N", f"heading-{i}", 1, dict(heading=angle)))
    for spec in range(12):
        for ai, analog in ((0, 0), (0, 1), (1, 0)):
            for speed in (-40, 0, 20, 40, 130):
                for input_value in (-100, 0, 100):
                    values = dict(spec=spec, ai=ai, analog=analog, kmh=speed, input=input_value,
                                  grip=0.4 if speed == 0 else 1.37, visual=-7, steer=0.2)
                    cases.append(("S", f"steer-{spec}-{ai}-{analog}-{speed}-{input_value}", 8, values))
    for spec in range(12):
        for ai in (0, 1):
            for difficulty in range(4):
                for types in ((0, 0, 0, 0), (1, 9, 14, 29), (20, 21, 22, 23),
                              (-1, 0, 0, 0), (0, 30, 0, 0), (100, 100, 100, 100)):
                    cases.append(("G", f"grip-{spec}-{ai}-{difficulty}-{types}", 1,
                                  dict(spec=spec, ai=ai, difficulty=difficulty, types=types)))
                sequence = [(1, 9, 14, 29)]
                for wheel in range(4):
                    for invalid in (-1, 30):
                        types = [0, 0, 0, 0]
                        types[wheel] = invalid
                        sequence.append(tuple(types))
                sequence.append((20, 21, 22, 23))
                cases.append(("G", f"grip-retained-{spec}-{ai}-{difficulty}", len(sequence),
                              dict(spec=spec, ai=ai, difficulty=difficulty, types=sequence[0],
                                   sequence=sequence)))
    for kmh in (0, 49, 50, 100, -50):
        for drop in (-36, -4, 0, 4, 5, 20):
            for height, fall, airborne in ((5, 0, 0), (5, 7, 1), (5, 8, 1), (5, 8.01, 1),
                                           (5, 100, 1), (40, -2, 1)):
                cases.append(("V", f"vertical-{kmh}-{drop}-{height}-{fall}-{airborne}", 4,
                              dict(kmh=kmh, height=height, ground=0, ground_prev=drop,
                                   fall_speed=fall, airborne=airborne, level_change=0)))
    for threshold in (20, 40, 130):
        for speed in (threshold - 0.0000152587890625, threshold + 0.0000152587890625):
            for spec in range(12):
                cases.append(("S", f"steer-boundary-{spec}-{speed}", 8,
                              dict(spec=spec, ai=0, analog=0, kmh=speed, input=99,
                                   grip=1.37, visual=0, steer=-0.2)))
    for fall in (7.799999, 7.8, 7.800001, 74.799999, 74.8, 74.800001):
        for kmh in (49.999996185302734, 50, 50.000003814697266, -50):
            for drop in (math.nextafter(4, 0), 4, math.nextafter(4, math.inf)):
                cases.append(("V", f"vertical-boundary-{fall}-{kmh}-{drop}", 4,
                              dict(kmh=kmh, height=5, ground=0, ground_prev=drop,
                                   fall_speed=fall, airborne=1, level_change=0)))
    rng = random.Random(0x3070A)
    for i in range(120):
        speed = struct.unpack("<f", struct.pack("<f", rng.uniform(-150, 200)))[0]
        cases.append(("S", f"steer-random-{i}", 8,
                      dict(spec=rng.randrange(12), ai=rng.randrange(2), analog=rng.randrange(2),
                           kmh=speed, input=rng.randint(-100, 100), grip=rng.uniform(0.4, 6),
                           visual=rng.uniform(-40, 40), steer=rng.uniform(-3, 3))))
    for spec in range(12):
        for ai in (0, 1):
            for difficulty in range(4):
                for gear in range(3):
                    for speed in (-80, -2, 0, 2, 20, 80):
                        v = dict(spec=spec, ai=ai, difficulty=difficulty, gear=gear, creep=3, creep_ticks=38,
                                 bonus=7, catchup=2.5, catchup_clock=0, position=4, drive=1234,
                                 skid=7, slip=0, kmh=30, input=100, speed=speed, clock=183,
                                 started=1, airborne=0, saturated=0)
                        cases.append(("T", f"throttle-{spec}-{ai}-{difficulty}-{gear}-{speed}", 8, v))
    base = dict(spec=3, ai=0, difficulty=1, gear=1, creep=0, creep_ticks=14, bonus=0,
                catchup=9, catchup_clock=0, position=8, drive=1234, skid=7, kmh=30,
                input=100, speed=20, clock=182, started=1, airborne=0, saturated=0, slip=0)
    for key, values in {"input": (-100, -1, 0, 1, 50, 51, 100), "started": (0, 1),
                        "slip": (math.pi / 2, 1.5707963267949, math.nextafter(1.5707963267949, math.inf),
                                 3 * math.pi / 2, math.nextafter(4.7123889803847, -math.inf),
                                 4.7123889803847), "clock": (math.nextafter(182, -math.inf), 182,
                                 math.nextafter(182, math.inf)),
                        "speed": (math.nextafter(10 / 3.6, -math.inf), 10 / 3.6,
                                  math.nextafter(10 / 3.6, math.inf), 1000),
                        "kmh": (2, 2.000000238418579), "airborne": (0, 1),
                        "saturated": (0, 1)}.items():
        for value in values:
            cases.append(("T", f"throttle-boundary-{key}-{value}", 40, dict(base, **{key: value})))
    for difficulty, divisor in enumerate((1, 1.15, 1.3, 1.3)):
        for index in range(211):
            for sign in (-1, 1):
                boundary = sign * index * divisor / 3.6
                for speed in (math.nextafter(boundary, -math.inf), boundary,
                              math.nextafter(boundary, math.inf)):
                    cases.append(("T", f"torque-index-{difficulty}-{index}-{sign}-{speed}", 3,
                                  dict(base, difficulty=difficulty, speed=speed)))
    for airborne in (0, 1):
        for saturated in (0, 1):
            for input_value in (0, 100):
                cases.append(("T", f"throttle-saturation-{airborne}-{saturated}-{input_value}", 4,
                              dict(base, slip=3, airborne=airborne, saturated=saturated, input=input_value)))
    sequence = [dict(input=input_value, speed=speed, started=started, clock=clock)
                for input_value, speed, started, clock in
                ((100, 0, 0, 0), (100, 0, 1, 182), (100, 0, 1, 183),
                 (0, 20, 1, 200), (-100, 20, 1, 365), (100, -20, 1, 366))]
    cases.append(("T", "throttle-changing-input-clock-speed", len(sequence), dict(base, sequence=sequence)))
    for input_value in (0, 50, 51, 80, 81, 100):
        for started in (0, 1):
            for speed in (-20, 0, math.nextafter(2, -math.inf), 2, math.nextafter(2, math.inf), 20):
                cases.append(("B", f"brake-{input_value}-{started}-{speed}", 4,
                              dict(input=input_value, started=started, speed=speed, sliding=1, skid=7)))
    for spec in range(12):
        for ai in (0, 1):
            for mode in (0, 1, 2):
                for charge in (0, math.nextafter(2.5, -math.inf), 2.5, math.nextafter(2.5, math.inf),
                               99.9, 100, math.nextafter(100, math.inf)):
                    for speed in (-10, math.nextafter(20, -math.inf), 20, math.nextafter(20, math.inf)):
                        cases.append(("U", f"turbo-{spec}-{ai}-{mode}-{charge}-{speed}", 48,
                                      dict(spec=spec, ai=ai, mode=mode, charge=charge, frames=19, speed=speed,
                                           drive=1234, vx=3, vz=-4, heading=(spec - 6) * 0.3)))
    for mode, name, ticks, v in list(cases):
        if mode == "T":
            cases.append(("Q", "revs-integrated-" + name, ticks, dict(v, revs=37, skidding=0)))
    for ai in (0, 1):
        for airborne in (0, 1):
            for started in (0, 1):
                for input_value in (-100, 80, 81, 100):
                    for skidding in (0, 1):
                        cases.append(("Q", f"revs-integrated-branches-{ai}-{airborne}-{started}-"
                                      f"{input_value}-{skidding}", 16,
                                      dict(base, ai=ai, airborne=airborne, started=started,
                                           input=input_value, skidding=skidding, revs=37)))
    for spec in range(12):
        for ai in (0, 1):
            for gear in range(3):
                for index in range(200):
                    cases.append(("R", f"revs-index-{spec}-{ai}-{gear}-{index}", 3,
                                  dict(spec=spec, ai=ai, gear=gear, index=index, input=100, started=1,
                                       airborne=0, skidding=0, out=0, kmh=80, revs=37)))
                for input_value in (-100, -81, -80, -1, 0, 1, 80, 81, 100):
                    for started in (0, 1):
                        for airborne in (0, 1):
                            for skidding, out in ((0, 0), (1, 0), (0, 1), (1, 1)):
                                cases.append(("R", f"revs-branches-{spec}-{ai}-{gear}-{input_value}-"
                                              f"{started}-{airborne}-{skidding}-{out}", 8,
                                              dict(spec=spec, ai=ai, gear=gear, index=30, input=input_value,
                                                   started=started, airborne=airborne, skidding=skidding,
                                                   out=out, kmh=30, revs=37)))
    # Supply the same binary32 km/h to both kernels; original speed production is outside this boundary.
    for kmh in (-60, 60 - 0.000003814697265625, 60, 60 + 0.000003814697265625):
        for ai in (0, 1):
            cases.append(("R", f"revs-skid-speed-{ai}-{kmh}", 48,
                          dict(spec=9, ai=ai, gear=1, index=199, input=0, started=1,
                               airborne=0, skidding=1, out=1, kmh=kmh, revs=123)))
    for i in range(240):
        cases.append(("R", f"revs-random-{i}", 32,
                      dict(spec=rng.randrange(12), ai=rng.randrange(2), gear=rng.randrange(3),
                           index=rng.randrange(200), input=rng.randint(-100, 100), started=rng.randrange(2),
                           airborne=rng.randrange(2), skidding=rng.randrange(2), out=rng.randrange(2),
                           kmh=struct.unpack("<f", struct.pack("<f", rng.uniform(-150, 200)))[0],
                           revs=rng.uniform(-40, 200))))
    rev_sequence = [dict(input=input_value, speed=speed, started=started, airborne=airborne,
                         saturated=saturated, clock=clock)
                    for input_value, speed, started, airborne, saturated, clock in
                    ((100, 0, 0, 0, 0, 0), (100, 20, 1, 0, 0, 183), (80, 20, 1, 1, 0, 184),
                     (81, 20, 1, 1, 1, 185), (-100, -20, 1, 1, 0, 186), (0, 80, 1, 0, 1, 365),
                     (100, 1000, 1, 0, 0, 366), (100, 10 / 3.6, 1, 0, 0, 367))] * 4
    for spec in range(12):
        for ai in (0, 1):
            for slip in (0, 3):
                cases.append(("Q", f"revs-integrated-transitions-{spec}-{ai}-{slip}", len(rev_sequence),
                              dict(base, spec=spec, ai=ai, slip=slip, revs=0, skidding=1,
                                   sequence=rev_sequence)))
    return cases


def encode(mode, ticks, v):
    if mode == "N":
        values = [v["heading"]]
    elif mode == "S":
        values = [ticks, v["spec"], v["ai"], v["analog"], v["input"], v["kmh"], v["grip"], v["visual"], v["steer"]]
    elif mode == "G":
        sequence = v.get("sequence", [v["types"]])
        values = [v["spec"], v["ai"], v["difficulty"], ticks, *(t for types in sequence for t in types)]
    elif mode == "V":
        values = [ticks, v["kmh"], v["height"], v["ground"], v["ground_prev"], v["fall_speed"],
                  v["airborne"], v["level_change"]]
    elif mode in ("T", "Q"):
        values = [ticks, v["spec"], v["ai"], v["difficulty"], v["gear"], v["creep"], v["creep_ticks"],
                  v["catchup"], v["catchup_clock"], v["drive"], v["bonus"], v["position"], v["skid"],
                  v["slip"], v["kmh"]]
        if mode == "Q":
            values += [v["revs"], v["skidding"]]
        steps = [dict(v, **s) for s in v["sequence"]] if "sequence" in v else [v] * ticks
        values += [s[k] for s in steps for k in ("input", "speed", "clock", "started", "airborne", "saturated")]
    elif mode == "B":
        values = [ticks, v["input"], v["started"], v["speed"], v["sliding"], v["skid"]]
    elif mode == "U":
        values = [ticks, v["spec"], v["ai"], v["mode"], v["charge"], v["frames"], v["speed"],
                  v["drive"], v["vx"], v["vz"], v["heading"]]
    elif mode == "R":
        values = [ticks, v["spec"], v["ai"], v["gear"], v["input"], v["index"], v["started"],
                  v["airborne"], v["skidding"], v["kmh"], v["revs"], v["out"]]
    return mode + " " + " ".join(format(x, ".17g") for x in values)


def compare_output(mode, row, expected):
    actual = [float(x) for x in row.split()]
    if len(actual) != len(expected) or not all(math.isfinite(x) for x in actual + expected):
        raise ValueError("Malformed or nonfinite car kernel output")
    differences = []
    maximum = 0.0
    for field, (a, b) in enumerate(zip(actual, expected)):
        error = abs(a - b)
        maximum = max(maximum, error)
        tolerance = 0 if field in EXACT[mode] else ABS_TOL + REL_TOL * abs(b)
        if error > tolerance or (mode == "N" and struct.pack("<d", a) != struct.pack("<d", b)):
            differences.append(dict(field=FIELDS[mode][field], port=a, original=b, tolerance=tolerance))
    return differences, maximum


def verify(executable, runner):
    original = CarOriginal(executable)
    cases = fixtures()
    inputs = "\n".join(encode(mode, ticks, v) for mode, _, ticks, v in cases) + "\n"
    result = subprocess.run([str(runner)], input=inputs, text=True, capture_output=True, check=True, timeout=60)
    rows = result.stdout.splitlines()
    if len(rows) != sum(c[2] for c in cases):
        raise ValueError("Native output row count mismatch")
    failures, maxima, cursor, starts = [], dict.fromkeys(FIELDS, 0.0), 0, []
    for mode, name, ticks, values in cases:
        starts.append(cursor)
        original.start_case(mode, values)
        for tick in range(ticks):
            current = values
            if "sequence" in values:
                current = (dict(values, types=values["sequence"][tick]) if mode == "G" else
                           dict(values, **values["sequence"][tick]))
            expected = original.step(mode, current)
            differences, maximum = compare_output(mode, rows[cursor], expected)
            cursor += 1
            maxima[mode] = max(maxima[mode], maximum)
            failures.extend(dict(case=name, tick=tick + 1, **diff) for diff in differences)
    # Negative control: with 64-bit precision the original truncates the unrounded v*3.6 product, so the
    # torque-index sweep must expose index differences the 53-bit run does not have.
    # A fresh emulator: cached translations from later modes would run past earlier stop addresses.
    control_original, control = CarOriginal(executable), 0
    control_original.fpcw = EXTENDED_FPCW
    for (mode, name, ticks, values), start in zip(cases, starts):
        if name.startswith("torque-index-"):
            control_original.start_case(mode, values)
            for tick in range(ticks):
                control += bool(compare_output(mode, rows[start + tick], control_original.step(mode, values))[0])
    if not control:
        raise ValueError("Extended-precision control word did not change any torque index")
    required = {"N": {0x2D9C7, 0x2D9E8, 0x51938, 0x51944, 0x51972, 0x5192E},
                "S": {0x5019A, 0x501C1, 0x501DD, 0x5021B, 0x5026A, 0x50296},
                "G": {0x3077B, 0x307E1, 0x30821, 0x30727, 0x30747},
                "V": {0x2E28A, 0x2E2B6, 0x2E316, 0x2E340, 0x2E34D, 0x2E370, 0x2E397, 0x2E600},
                "T": {0x55F9C, 0x4DCF4, 0x4DD41, 0x4DD82, 0x4DDA8, 0x4DE08, 0x4DE3B,
                      0x4DE91, 0x4DF19, 0x4DF55, 0x4DFB4},
                "B": {0x4E238, 0x4E247, 0x4E265, 0x4E2AD},
                "U": {0x4E3E1, 0x4E442, 0x4E490, 0x4E4BC, 0x4E4ED, 0x4E569, 0x4E598, 0x4E634},
                "R": {0x4E03A, 0x4E086, 0x4E096, 0x4E0B9, 0x4E0ED, 0x4E130, 0x4E14E, 0x4E189},
                "Q": {0x4DD41, 0x4E03A, 0x4E086, 0x4E0B9, 0x4E130, 0x4E14E, 0x4E189}}
    for mode, addresses in required.items():
        if not addresses <= original.visits[mode]:
            raise ValueError(f"Missing {mode} original paths: {sorted(addresses - original.visits[mode])}")
    import unicorn
    return dict(status="fail" if failures else "pass", cases=len(cases), steps=cursor,
                failures=failures, max_absolute_errors=maxima, original_sha256=ORIGINAL_SHA256,
                kernels={mode: dict(cases=sum(c[0] == mode for c in cases),
                                    steps=sum(c[2] for c in cases if c[0] == mode),
                                    max_absolute_error=maxima[mode]) for mode in FIELDS},
                runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                fixture_input_sha256=hashlib.sha256(inputs.encode()).hexdigest(),
                source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in (ROOT / "src/game/car.c", ROOT / "src/game/car.h",
                                         ROOT / "src/game/physics.c", ROOT / "tests/car_kernels.c",
                                         ROOT / "tests/physics_original.py", ROOT / "tools/le2elf.py", Path(__file__))},
                observed_at=datetime.now(timezone.utc).isoformat(), host=platform.platform(),
                python=platform.python_version(), unicorn=unicorn.__version__,
                absolute_tolerance=ABS_TOL, relative_tolerance=REL_TOL, x87_control_word=hex(FPCW),
                extended_control_word_differing_steps=control,
                required_addresses_executed={k: [hex(a) for a in sorted(v)] for k, v in required.items()},
                scope={"N": "Unmodified cars_update_all 0x2d9c7..0x2d9ee and angle_normalize 0x51938; "
                            "binary64 car+0xf8 output, unchanged accumulated body+0x28, signed zero and boundaries",
                       "S": "Complete car_steer 0x500f0, input -100..100, car+0xa8=10",
                       "G": "Unmodified car_update block 0x3070a..0x30874, supplied surface types",
                       "V": "car_vertical arithmetic until 0x2e397 (landing) or return; no sound/fx/landing hazards",
                       "T": "0x4db90..0x4dfbe: drive, creep, catchup, skid; real __CHP; supplied unique torque[i]=i-5+gear",
                       "B": "0x4e1bc return: brake/sliding and skid; unfinished car",
                       "U": "0x4e3a7..0x4e656: charge/force/velocity; index 2 avoids human recharge sound",
                       "R": "0x4dfbe..0x4e1a7: rev arithmetic, supplied final torque index and binary32 km/h, "
                            "human/AI, clock -1/0 and flags 0/1",
                       "Q": "0x4db90..0x4e1a7: drive and rev integration, unfinished car, hardware mode 0"},
                exclusions=["Full car_input, gear/input arbitration and car+0x5a8 activity flag",
                            "Hardware mode 2 shaping and finished-car input override",
                            "Catchup disabled/mode 2 and generation of torque tables",
                            "Turbo requests, effects, sound; whole dispatcher beyond tested heading copy",
                            "Original binary64 km/h production/narrowing and arbitrary race clocks/flags",
                            "Ground geometry and probes", "Landing effects and wrecks",
                            "car_update airborne threshold", "Declared level_change upward-following deviation",
                            "Whole car_update/cars_update_all and DOS race"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    args.report.write_text(json.dumps({"status": "running"}) + "\n")
    try:
        report = verify(args.original, args.runner.resolve())
    except (ValueError, OSError, ModuleNotFoundError, subprocess.SubprocessError) as error:
        report = dict(status="fail", error=str(error))
        print(f"FAIL: {error}", file=sys.stderr)
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k in ("status", "cases", "steps", "max_absolute_errors", "error")}))
    if report.get("failures"):
        print("First divergence: " + json.dumps(report["failures"][0]), file=sys.stderr)
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
