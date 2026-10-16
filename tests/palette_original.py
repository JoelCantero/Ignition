#!/usr/bin/env python3
"""Differential replay of unmodified LE palette_blend (0x51508) and palette_lerp (0x51a9c) bytes, not a DOS/game
emulator."""
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
ROUTINE, CHOP, CHOP_END = 0x51508, 0x55F9C, 0x55FB8
LERP, LERP_END = 0x51A9C, 0x51AF8
LERP_A, LERP_B, LERP_OUT = 0x402000, 0x402400, 0x402800
SRC, KEEP, STACK, RETURN = 0x400000, 0x401000, 0x500000, 0x600000
ESP = STACK + 0x8000
BUFFER = 0x207AFE  # static result palette, returned in EAX
# The Watcom runtime's x87 init (0x56294, XI table entry 0xe7070) loads CW 0x127f from [0xe6be0] at 0x6f651:
# 53-bit precision. The port matches that word only; under the FNINIT default 0x037f (64-bit) the product would
# not be rounded before the chop and some cases differ.
CONTROL_WORDS = (0x127F,)


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
        self.uc.mem_map(SRC, 0x3000)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(RETURN, 0x1000)
        constants = struct.unpack("<H", self.uc.mem_read(0xE6BE0, 2))[0]
        if constants != 0x127F:
            raise ValueError(f"Unexpected runtime control word: 0x{constants:x}")
        self.visited = set()
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.code)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write)

    def code(self, uc, address, size, _):
        if not (ROUTINE <= address <= 0x515F3 or CHOP <= address <= CHOP_END or LERP <= address <= LERP_END):
            raise ValueError(f"Unexpected code execution at 0x{address:x}")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        if not (BUFFER <= address and address + size <= BUFFER + 0x300 or
                LERP_OUT <= address and address + size <= LERP_OUT + 0x300 or
                STACK <= address and address + size <= STACK + 0x10000):
            raise ValueError(f"Unexpected original write at 0x{address:x}")

    def blend(self, palette, rgb, frac, keep, control):
        self.uc.mem_write(SRC, bytes(palette))
        self.uc.mem_write(BUFFER, b"\xa5" * 0x300)
        if keep is not None:
            self.uc.mem_write(KEEP, struct.pack("<256i", *keep))
        self.uc.mem_write(ESP, struct.pack("<IdI", RETURN, frac, KEEP if keep is not None else 0))
        for name, value in zip(("EAX", "EDX", "EBX", "ECX"), (SRC, *rgb)):
            self.uc.reg_write(getattr(self.reg, f"UC_X86_REG_{name}"), value & 0xFFFFFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, control)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        try:
            self.uc.emu_start(ROUTINE, RETURN, timeout=5_000_000, count=100_000)
        except self.emulator.UcError as error:
            address = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"Original emulation failed at 0x{address:x}: {error}") from error
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError("Original failed to return within execution limits")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 16:
            raise ValueError("Original did not obey Watcom RET 0xC stack cleanup")
        if self.uc.reg_read(self.reg.UC_X86_REG_EAX) != BUFFER:
            raise ValueError("Original did not return its static palette buffer")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError("Original leaked x87 stack values")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != control:
            raise ValueError("Original changed the x87 control word")
        return bytes(self.uc.mem_read(BUFFER, 0x300))

    def lerp(self, a, b, t):
        self.uc.mem_write(LERP_A, bytes(a))
        self.uc.mem_write(LERP_B, bytes(b))
        self.uc.mem_write(LERP_OUT, b"\xa5" * 0x300)
        self.uc.mem_write(ESP, struct.pack("<I", RETURN))
        for name, value in zip(("EAX", "EDX", "EBX", "ECX"), (LERP_A, LERP_B, LERP_OUT, t)):
            self.uc.reg_write(getattr(self.reg, f"UC_X86_REG_{name}"), value & 0xFFFFFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        try:
            self.uc.emu_start(LERP, RETURN, timeout=5_000_000, count=100_000)
        except self.emulator.UcError as error:
            address = self.uc.reg_read(self.reg.UC_X86_REG_EIP)
            raise ValueError(f"Original emulation failed at 0x{address:x}: {error}") from error
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError("Original failed to return within execution limits")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4:
            raise ValueError("Original did not obey a plain RET")
        if bytes(self.uc.mem_read(LERP_A, 0x300)) != bytes(a) or bytes(self.uc.mem_read(LERP_B, 0x300)) != bytes(b):
            raise ValueError("Original changed an input palette")
        return bytes(self.uc.mem_read(LERP_OUT, 0x300))


def fixtures():
    rng = random.Random(0x51508)
    ramp = [(i * 7 + 13) & 0xFF for i in range(768)]
    vga = [(i * 5) % 64 for i in range(768)]
    cases = []

    def add(name, palette, rgb, frac, keep=None):
        cases.append((name, palette, rgb, frac, keep))

    # The constant arguments of the known callers (0x3c0b5.., 0x40584, 0x40809, 0x493f9).
    for frac in (0.4, 0.8, 0.5, 0.0, 1.0):
        add(f"flash-white-{frac}", ramp, (0xFF, 0xFF, 0xFF), frac)
    for i in range(0, 51, 5):
        frac = i * 0.02 * 0.5
        add(f"tint-blue-{i}", vga, (0x22, 0x22, 0x31), frac)
    # Exact-product rounding: k * nextafter(m / k) near an integer.
    for k in (3, 5, 6, 7, 9, 10, 11, 13, 49, 51, 255):
        for m in (1, 2, k - 1):
            for direction in (-math.inf, math.inf):
                frac = math.nextafter(m / k, direction)
                add(f"rounding-{k}-{m}-{direction}", [0] * 768, (k, 0xFF - k, k), frac)
                add(f"rounding-neg-{k}-{m}-{direction}", [k] * 768, (0, k, 0), frac)
    add("negative-frac", ramp, (0, 0x80, 0xFF), -0.75)
    add("over-one", ramp, (0x10, 0x20, 0x30), 1.75)
    add("wide-target", ramp, (300, -40, 0x1FF), 1.0)
    add("nan", ramp, (0xFF, 0, 0xFF), math.nan)
    add("inf", ramp, (0xFF, 0, 0xFF), math.inf)
    add("huge", ramp, (0xFF, 0, 0xFF), 1e12)
    keep = [rng.choice((0, 0, 1, -1, 7)) for _ in range(256)]
    add("keep-mixed", ramp, (0x22, 0x22, 0x31), 0.6, keep)
    add("keep-none", ramp, (0x22, 0x22, 0x31), 0.6, [0] * 256)
    add("keep-all", ramp, (0x22, 0x22, 0x31), 0.6, [1] * 256)
    for i in range(96):
        palette = [rng.randrange(256) for _ in range(768)]
        rgb = tuple(rng.randrange(256) for _ in range(3))
        frac = rng.choice((rng.random(), rng.uniform(-1, 2), round(rng.random(), 2)))
        add(f"seeded-{i}", palette, rgb, frac, keep if i % 8 == 0 else None)
    return cases


def lerp_fixtures():
    rng = random.Random(0x51A9C)
    a = [(i * 7 + 13) & 0xFF for i in range(768)]
    b = [(255 - i * 3) & 0xFF for i in range(768)]
    cases = []
    # sub_00012bb8 passes chop() of a menu fade clock; cover the whole range, the copy edge and outliers.
    for t in (0, 1, 2, 64, 127, 128, 129, 200, 254, 255, 256, 257, 1000, -1, -128, -256, -1000,
              2**31 - 1, -2**31, 0x00808081, -0x00808081):
        cases.append((f"lerp-{t}", a, b, t))
    for i in range(64):
        cases.append((f"lerp-seeded-{i}", [rng.randrange(256) for _ in range(768)],
                      [rng.randrange(256) for _ in range(768)], rng.randrange(-300, 300)))
    return cases


def lerp_line(case):
    _, a, b, t = case
    return " ".join(["L", str(t), bytes(a).hex(), bytes(b).hex()])


def line(case):
    _, palette, rgb, frac, keep = case
    fields = ["B", f"{struct.unpack('<Q', struct.pack('<d', frac))[0]:016x}", *map(str, rgb),
              "1" if keep is not None else "0", bytes(palette).hex()]
    if keep is not None:
        fields += map(str, keep)
    return " ".join(fields)


def verify(executable, runner):
    original = Original(executable)
    cases = fixtures()
    lerps = lerp_fixtures()
    inputs = "\n".join([line(case) for case in cases] + [lerp_line(case) for case in lerps]) + "\n"
    native = subprocess.run([str(runner), "--runner"], input=inputs, text=True,
                            capture_output=True, check=True, timeout=30)
    rows = native.stdout.splitlines()
    if len(rows) != len(cases) + len(lerps):
        raise ValueError(f"Native output has {len(rows)} rows, expected {len(cases) + len(lerps)}")
    for control in CONTROL_WORDS:
        for (name, palette, rgb, frac, keep), row in zip(cases, rows):
            expected = original.blend(palette, rgb, frac, keep, control).hex()
            if row != expected:
                index = next(i for i in range(0, len(row), 2) if row[i:i + 2] != expected[i:i + 2]) // 2
                raise ValueError(f"{name} CW=0x{control:04x}: byte {index} (colour {index // 3}): "
                                 f"original={expected[index * 2:index * 2 + 2]}, "
                                 f"port={row[index * 2:index * 2 + 2]}")
    for (name, a, b, t), row in zip(lerps, rows[len(cases):]):
        expected = original.lerp(a, b, t).hex()
        if row != expected:
            index = next(i for i in range(0, len(row), 2) if row[i:i + 2] != expected[i:i + 2]) // 2
            raise ValueError(f"{name}: byte {index}: original={expected[index * 2:index * 2 + 2]}, "
                             f"port={row[index * 2:index * 2 + 2]}")
    required = {0x51AB4, 0x51AF2, 0x51ACA, 0x51ADE,
                0x51518, 0x51537, 0x51540, 0x51542, 0x5156f, 0x515a0, 0x515d1, 0x515d7, 0x515e8,
                0x55FAD}
    if not required <= original.visited:
        raise ValueError(f"Missing original paths: {sorted(hex(a) for a in required - original.visited)}")
    import unicorn
    return {
        "status": "pass", "routines": ["0x51508", "0x51a9c"], "blend_cases": len(cases),
        "lerp_cases": len(lerps),
        "control_words": [f"0x{control:04x}" for control in CONTROL_WORDS],
        "original_sha256": ORIGINAL_SHA256, "unicorn_version": unicorn.__version__,
        "runner_sha256": hashlib.sha256(runner.read_bytes()).hexdigest(),
        "fixture_input_sha256": hashlib.sha256(inputs.encode()).hexdigest(),
        "source_sha256": {
            str(path.resolve().relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [ROOT / "src/game/palette.c", ROOT / "src/game/palette.h",
                         ROOT / "tests/palette.c", Path(__file__), ROOT / "tools/le2elf.py"]
        },
        "observed_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.platform(), "python_version": platform.python_version(),
        "required_addresses_executed": [hex(address) for address in sorted(required)],
        "scope": ("Isolated palette_blend + sub_00055f9c and palette_lerp, hardware x87; exact bytes; "
                  "callers not covered"),
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
