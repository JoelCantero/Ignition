#!/usr/bin/env python3
"""Differential replay of unmodified LE body_step bytes, not a DOS/game emulator."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import platform
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from le2elf import LE, convert

ORIGINAL_SHA256 = "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"
BODY, STACK, RETURN = 0x400000, 0x500000, 0x600000
ESP = STACK + 0x8000
BODY_SIZE = 0x1D0
ABS_TOL, REL_TOL = 2e-11, 2e-12
# This is the Watcom layout, deliberately independent of the native C layout.
FIELDS = [
    ("mass", 0x00), ("inertia", 0x08), ("restitution", 0x10),
    ("a", 0x18), ("b", 0x20), ("heading", 0x28), ("yaw_rate", 0x30),
    ("px", 0x68), ("pz", 0x70), ("vx", 0x78), ("vz", 0x80),
    ("v_fwd", 0x98), ("v_lat", 0xA0), ("steer", 0xAC),
    ("pitch", 0xB4), ("roll", 0xBC), ("grip_f", 0xC4), ("grip_r", 0xCC),
    ("drive", 0xD8), ("fmax_f", 0xE0), ("fmax_r", 0xE8),
    ("radius", 0x130), ("impact", 0x1B8), ("contact_x", 0x1C0), ("contact_z", 0x1C8),
] + [(f"corner_x[{i}]", 0xF0 + i * 8) for i in range(4)] + [
    (f"corner_z[{i}]", 0x110 + i * 8) for i in range(4)
]
OUTPUTS = {
    "heading", "yaw_rate", "px", "pz", "vx", "vz", "v_fwd", "v_lat",
    "fmax_f", "fmax_r",
}


class Original:
    def __init__(self, executable):
        import unicorn
        from unicorn import x86_const
        if unicorn.__version__ != "2.1.4":
            raise ValueError(f"Expected Unicorn 2.1.4, found {unicorn.__version__}")
        self.emulator = unicorn
        self.reg = x86_const
        data = executable.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != ORIGINAL_SHA256:
            raise ValueError(f"Unsupported original SHA-256: {digest}")
        le = LE(data)
        images, _ = convert(le)
        self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for obj in le.objects:
            self.uc.mem_map(obj["base"], (obj["vsize"] + 4095) & ~4095)
            self.uc.mem_write(obj["base"], bytes(images[obj["index"]]))
        self.uc.mem_map(BODY, 0x1000)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(RETURN, 0x1000)
        constants = struct.unpack("<6d", self.uc.mem_read(0xA7C6C, 48))
        if constants != (1.570798, 1 / 72, 9.81, 0.2, 2.0, 3.0):
            raise ValueError(f"Unexpected physics constants: {constants}")
        # Select the original hardware-x87 atan2 path; no helper is replaced.
        if self.uc.mem_read(0xE68C8, 1)[0] & 1:
            raise ValueError("Original selects the software-FPU runtime path")
        self.visited = set()
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.code)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write)

    def code(self, uc, address, size, _):
        if not (0x1E090 <= address <= 0x1E634 or 0x647EF <= address <= 0x64803):
            raise ValueError(f"Unexpected code execution at 0x{address:x}")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        if not (BODY <= address and address + size <= BODY + BODY_SIZE or
                STACK <= address and address + size <= STACK + 0x10000):
            raise ValueError(f"Unexpected original write at 0x{address:x}")

    def start(self, values):
        self.initial = bytearray(b"\xa5" * BODY_SIZE)
        for name, offset in FIELDS:
            struct.pack_into("<d", self.initial, offset, values[name])
        struct.pack_into("<i", self.initial, 0xA8, values["saturated"])
        struct.pack_into("<i", self.initial, 0xD4, values["sliding"])
        self.uc.mem_write(BODY, bytes(self.initial))
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, 0x037F)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)

    def step(self, airborne, slope_k):
        self.uc.mem_write(ESP, struct.pack("<Id", RETURN, slope_k))
        self.uc.reg_write(self.reg.UC_X86_REG_EAX, BODY)
        self.uc.reg_write(self.reg.UC_X86_REG_EDX, airborne)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        try:
            self.uc.emu_start(0x1E090, RETURN, timeout=1_000_000, count=10000)
        except self.emulator.UcError as error:
            address = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"Original emulation failed at 0x{address:x}: {error}") from error
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError("Original failed to return within execution limits")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 12:
            raise ValueError("Original did not obey Watcom RET 8 stack cleanup")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError("Original leaked x87 stack values")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != 0x037F:
            raise ValueError("Original changed the x87 control word")
        raw = self.uc.mem_read(BODY, BODY_SIZE)
        mutable = set(range(0xA8, 0xAC))
        for name, offset in FIELDS:
            if name in OUTPUTS:
                mutable.update(range(offset, offset + 8))
        if any(raw[i] != self.initial[i] for i in range(BODY_SIZE) if i not in mutable):
            raise ValueError("Original changed an input field or unknown body byte")
        result = {name: struct.unpack_from("<d", raw, offset)[0] for name, offset in FIELDS}
        result["saturated"] = struct.unpack_from("<i", raw, 0xA8)[0]
        result["sliding"] = struct.unpack_from("<i", raw, 0xD4)[0]
        return result


def fixtures():
    base = {name: (i + 1) * 0.125 for i, (name, _) in enumerate(FIELDS)}
    base.update(mass=500, inertia=1000, a=1.2, b=1.3, heading=0, yaw_rate=0,
                px=0, pz=0, vx=0, vz=0, steer=0, pitch=0, roll=0,
                grip_f=1, grip_r=1, drive=0, saturated=7, sliding=0)
    cases = []

    def add(name, ticks=1, airborne=0, slope_k=1, **changes):
        cases.append((name, ticks, airborne, slope_k, dict(base, **changes)))

    add("rest")
    add("forward", vx=20, drive=1000)
    add("reverse", vx=-20, drive=-1000)
    add("negative-drive-unclipped", vx=20, drive=-10000)
    limit = 500 * 9.81
    for name, force in [("below", math.nextafter(limit, -math.inf)),
                        ("equal", limit), ("above", math.nextafter(limit, math.inf)),
                        ("large", 10000)]:
        add(f"drive-{name}", drive=force)
    for sign in (-1, 1):
        add(f"yaw-unlimited-{sign}", vx=20, steer=sign * 0.005)
        add(f"yaw-limited-{sign}", vx=20, steer=sign * 0.6)
        add(f"yaw-return-{sign}", vx=20, yaw_rate=sign * 10)
        add(f"lateral-{sign}", vx=10, vz=sign * 4)
        add(f"lateral-small-{sign}", vx=10, vz=sign * 0.001)
    add("zero-grip", vx=10, vz=2, grip_f=0, grip_r=0, steer=0.4, drive=1000)
    yaw_limit = limit * 3 / 1000 / 72
    friction_limit = 9.81 / 72
    for sign in (-1, 1):
        for label, direction in [("below", -math.inf), ("equal", None), ("above", math.inf)]:
            yaw = yaw_limit if direction is None else math.nextafter(yaw_limit, direction)
            speed = friction_limit if direction is None else math.nextafter(friction_limit, direction)
            add(f"yaw-boundary-{sign}-{label}", yaw_rate=sign * yaw)
            add(f"friction-boundary-{sign}-{label}", heading=-1.570798, vx=sign * speed)
    add("sliding-rest", sliding=1)
    add("sliding-signed-zero", sliding=1, vx=-0.0, vz=-0.0)
    for vx, vz in [(10, 0), (-10, 0), (0, 10), (0, -10),
                   (10, 8), (-10, 8), (-10, -8), (10, -8), (0.001, 0.001)]:
        add(f"sliding-{vx}-{vz}", sliding=1, vx=vx, vz=vz, heading=0.7,
            pitch=0.17, roll=-0.11)
    for airborne in (0, 1):
        for pitch, roll in [(0.3, 0), (0, -0.3), (0.3, -0.2), (-0.3, 0.2)]:
            for slope_k in (1, 4):
                add(f"slope-{airborne}-{pitch}-{roll}-{slope_k}",
                    airborne=airborne, slope_k=slope_k, pitch=pitch, roll=roll,
                    heading=0.7, vx=15, vz=12, yaw_rate=0.3, drive=1000)
    for sliding, airborne in [(0, 0), (1, 0), (0, 1), (1, 1)]:
        add(f"trajectory-{sliding}-{airborne}", ticks=144, sliding=sliding,
            airborne=airborne, vx=18, vz=8, heading=0.3, yaw_rate=-0.2,
            steer=0.12, drive=1200, pitch=0.2, roll=-0.1, slope_k=4)
    rng = random.Random(0x1E090)
    for i in range(128):
        add(f"seeded-{i}", ticks=8, airborne=i % 4 == 0, sliding=i % 3 == 0,
            slope_k=1 if i % 2 else 4, mass=rng.uniform(250, 1800),
            inertia=rng.uniform(400, 5000), a=rng.uniform(0.5, 2),
            b=rng.uniform(0.5, 2), heading=rng.uniform(-math.pi, math.pi),
            yaw_rate=rng.uniform(-4, 4), px=rng.uniform(-1000, 1000),
            pz=rng.uniform(-1000, 1000), vx=rng.uniform(-40, 40),
            vz=rng.uniform(-40, 40), steer=rng.uniform(-0.6, 0.6),
            pitch=rng.uniform(-0.4, 0.4), roll=rng.uniform(-0.4, 0.4),
            grip_f=rng.uniform(0.05, 1.5), grip_r=rng.uniform(0.05, 1.5),
            drive=rng.uniform(-20000, 20000))
    return cases


def verify(executable, runner):
    original = Original(executable)
    cases = fixtures()
    inputs = []
    for _, ticks, airborne, slope_k, values in cases:
        inputs.append(" ".join([str(ticks), str(int(airborne)), repr(slope_k),
                                str(values["saturated"]), str(int(values["sliding"]))] +
                               [repr(values[name]) for name, _ in FIELDS]))
    native = subprocess.run([str(runner)], input="\n".join(inputs) + "\n",
                            text=True, capture_output=True, check=True, timeout=30)
    rows = native.stdout.splitlines()
    expected_rows = sum(case[1] for case in cases)
    if len(rows) != expected_rows:
        raise ValueError(f"Native output has {len(rows)} ticks, expected {expected_rows}")
    max_errors = {name: 0.0 for name, _ in FIELDS}
    row_index = 0
    for case_name, ticks, airborne, slope_k, values in cases:
        original.start(values)
        for tick in range(ticks):
            expected = original.step(int(airborne), slope_k)
            row = rows[row_index].split()
            row_index += 1
            if len(row) != len(FIELDS) + 2:
                raise ValueError(f"Malformed native output: {case_name} tick {tick + 1}")
            actual = {"saturated": int(row[0]), "sliding": int(row[1])}
            actual.update((name, float(value)) for (name, _), value in zip(FIELDS, row[2:]))
            for name, value in expected.items():
                other = actual[name]
                if not math.isfinite(value) or not math.isfinite(other):
                    raise ValueError(f"Nonfinite {case_name} tick {tick + 1}: {name}")
                tolerance = ABS_TOL + REL_TOL * abs(value) if name in OUTPUTS else 0
                if abs(value - other) > tolerance:
                    raise ValueError(
                        f"{case_name} tick {tick + 1}: {name}: original={value:.17g}, "
                        f"port={other:.17g}, delta={abs(value - other):.6g}, "
                        f"tolerance={tolerance:.6g}")
                if name in max_errors:
                    max_errors[name] = max(max_errors[name], abs(value - other))
    required = {
        0x1E0C7, 0x1E13D, 0x1E18C, 0x1E1A0, 0x1E204, 0x1E220, 0x1E229,
        0x1E269, 0x1E287, 0x1E383, 0x1E39E, 0x1E3D1, 0x1E46F,
        0x1E4B1, 0x1E4CD, 0x1E534, 0x1E58C, 0x1E5A8, 0x647FA,
    }
    if not required <= original.visited:
        raise ValueError(f"Missing original paths: {sorted(required - original.visited)}")
    import unicorn
    return {
        "status": "pass", "routine": "0x1e090", "cases": len(cases), "ticks": expected_rows,
        "original_sha256": ORIGINAL_SHA256, "unicorn_version": unicorn.__version__,
        "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
        "fixture_input_sha256": hashlib.sha256(("\n".join(inputs) + "\n").encode()).hexdigest(),
        "source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [ROOT / "src/game/physics.c", ROOT / "src/game/physics.h",
                         ROOT / "tests/body_step.c", Path(__file__),
                         ROOT / "tools/le2elf.py", ROOT / "tests/requirements-original.txt"]
        },
        "observed_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.platform(), "python_version": platform.python_version(),
        "absolute_tolerance": ABS_TOL, "relative_tolerance": REL_TOL,
        "max_absolute_errors": max_errors,
        "required_addresses_executed": [hex(address) for address in sorted(required)],
        "scope": "Isolated body_step, hardware x87, CW=0x037f; not whole-game equivalence",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.report:
        args.report.write_text(json.dumps({"status": "running"}) + "\n")
    try:
        report = verify(args.original, args.runner.resolve())
    except (ValueError, OSError, ModuleNotFoundError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        message = str(error)
        if isinstance(error, subprocess.CalledProcessError):
            message += f"\nNative stderr: {error.stderr}"
        if args.report:
            args.report.write_text(json.dumps({"status": "fail", "error": message}) + "\n")
        print(f"FAIL: {message}", file=sys.stderr)
        return 1
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
