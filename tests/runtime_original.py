#!/usr/bin/env python3
"""Execute unchanged Watcom random, string and math routines against native contracts."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from le2elf import LE, convert

SHA256 = "179c654ba65281f08bbcda2b5943ab9f35c309ec718600f81104644d22db948c"
BUFFER, STACK, ESP, RETURN = 0x400000, 0x500000, 0x508000, 0x600000
SEED = 0xD7D94
ENTRIES = (0x51714, 0x6476E, 0x64774, 0x64798, 0x647B2, 0x647CD, 0x647DA,
           0x647EF, 0x64804, 0x64810, 0x65782, 0x6579C, 0x70078, 0x770D0, 0x77171)


class Original:
    def __init__(self, path):
        import unicorn
        from unicorn import x86_const
        if unicorn.__version__ != "2.1.4":
            raise ValueError(f"Expected Unicorn 2.1.4, got {unicorn.__version__}")
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != SHA256:
            raise ValueError("Unsupported original executable")
        self.api, self.reg = unicorn, x86_const
        self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        le = LE(raw)
        images, _ = convert(le)
        for obj in le.objects:
            size = (obj["vsize"] + 4095) & ~4095
            self.uc.mem_map(obj["base"], size)
            self.uc.mem_write(obj["base"], bytes(images[obj["index"]]))
            self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.write,
                             begin=obj["base"], end=obj["base"] + size - 1)
        self.uc.mem_map(BUFFER, 0x2000)
        self.uc.mem_map(STACK, 0x10000)
        self.uc.mem_map(RETURN, 0x1000)
        self.uc.mem_map(0x700000, 0x1000)
        self.uc.mem_write(0x700000, struct.pack("<QQQ", 0, 0x00CF9A000000FFFF, 0x00CF92000000FFFF))
        self.uc.reg_write(x86_const.UC_X86_REG_GDTR, (0, 0x700000, 23, 0))
        self.uc.reg_write(x86_const.UC_X86_REG_CS, 8)
        for name in ("DS", "ES", "SS"):
            self.uc.reg_write(getattr(x86_const, f"UC_X86_REG_{name}"), 16)
        self.uc.mem_write(RETURN + 0x100, b"\xdd\x1d" + struct.pack("<I", BUFFER + 0x1000))
        self.uc.mem_write(0xE68C8, b"\0")
        self.visits = Counter()
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.code, begin=0x10000, end=0x85C4F)
        self.version = unicorn.__version__
        if self.state() != 1:
            raise ValueError("Unexpected initial Watcom seed")
        if self.call(0x6476E) != SEED:
            raise ValueError("Incorrect original random state accessor")

    def code(self, uc, address, size, _):
        allowed = ((0x51714, 0x5175D), (0x6476E, 0x64820), (0x65782, 0x657AC),
                   (0x70078, 0x700AB), (0x770D0, 0x7718A), (0x7D292, 0x7D2C7))
        if not any(start <= address < end for start, end in allowed):
            raise ValueError(f"Unexpected original callee: 0x{address:x}")
        if address in ENTRIES:
            self.visits[address] += 1

    def write(self, uc, access, address, size, value, _):
        if not (SEED <= address and address + size <= SEED + 4):
            raise ValueError(f"Unexpected original global write: 0x{address:x}")

    def state(self):
        return struct.unpack("<I", self.uc.mem_read(SEED, 4))[0]

    def call(self, address, eax=0, edx=0, stack=b"", floating=False):
        r, uc = self.reg, self.uc
        landing = RETURN + 0x100 if floating else RETURN
        uc.mem_write(ESP, struct.pack("<I", landing) + stack)
        for name, value in (("EAX", eax), ("EDX", edx), ("EBX", 0), ("ECX", 0),
                            ("ESP", ESP), ("EBP", 0), ("ESI", 0), ("EDI", 0)):
            uc.reg_write(getattr(r, f"UC_X86_REG_{name}"), value)
        uc.reg_write(r.UC_X86_REG_EFLAGS, 2)
        uc.reg_write(r.UC_X86_REG_FPCW, 0x127F)
        uc.reg_write(r.UC_X86_REG_FPTAG, 0xFFFF)
        stop = landing + 6 if floating else landing
        try:
            uc.emu_start(address, stop, timeout=5_000_000, count=100000)
        except self.api.UcError as error:
            raise ValueError(f"Original 0x{address:x} failed at 0x{uc.reg_read(r.UC_X86_REG_EIP):x}, "
                             f"eax=0x{uc.reg_read(r.UC_X86_REG_EAX):x}, "
                             f"edx=0x{uc.reg_read(r.UC_X86_REG_EDX):x}") from error
        if uc.reg_read(r.UC_X86_REG_EIP) != stop:
            raise ValueError(f"Original 0x{address:x} stopped at 0x{uc.reg_read(r.UC_X86_REG_EIP):x}, expected 0x{stop:x}")
        if uc.reg_read(r.UC_X86_REG_ESP) != ESP + 4 + len(stack):
            raise ValueError("Unexpected stack cleanup")
        if uc.reg_read(r.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError("Original leaked x87 values")
        if floating:
            return struct.unpack("<d", uc.mem_read(BUFFER + 0x1000, 8))[0]
        return uc.reg_read(r.UC_X86_REG_EAX)

    def strings(self, kind, a, b):
        self.uc.mem_write(BUFFER, b"\xa5" * 1024)
        self.uc.mem_write(BUFFER + 16, a + b"\0")
        self.uc.mem_write(BUFFER + 2048, b + b"\0" + b"\xa5" * 16)
        entry = {"L": 0x77171, "C": 0x770D0, "A": 0x70078}[kind]
        value = self.call(entry, eax=BUFFER + 16, edx=BUFFER + 2048)
        if kind == "A":
            if value != BUFFER + 16:
                raise ValueError("strcat did not return destination")
            result = bytes(self.uc.mem_read(BUFFER + 16, len(a) + len(b) + 1))
            expected = a + b + b"\0"
        else:
            result = bytes(self.uc.mem_read(BUFFER + 16, len(a) + 1))
            expected = a + b"\0"
        if result != expected or bytes(self.uc.mem_read(BUFFER, 16)) != b"\xa5" * 16:
            raise ValueError("Incorrect string result or leading guard")
        end = BUFFER + 16 + len(expected)
        if bytes(self.uc.mem_read(end, 16)) != b"\xa5" * 16:
            raise ValueError("String routine overwrote trailing guard")
        if kind == "A":
            return (a + b).hex() or "-"
        if kind == "C":
            value = struct.unpack("<i", struct.pack("<I", value))[0]
            value = (value > 0) - (value < 0)
        return str(value)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--original", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--report", type=Path, required=True)
    args = ap.parse_args()
    original = Original(args.original)
    rng = random.Random(0x64774)
    commands, expected = [], []
    counts = Counter()
    seeds = [0, 1, 2, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]
    seeds += [rng.getrandbits(32) for _ in range(32)]
    for seed in seeds:
        for kind in ("I", "F"):
            count = 2048 if seed in (0, 1, 0xFFFFFFFF) else 65
            commands.append(f"R {seed} {count} {kind}")
            original.call(0x64798, eax=seed)
            if original.state() != seed:
                raise ValueError("Original srand did not store the seed")
            for _ in range(count):
                value = original.call(0x64774 if kind == "I" else 0x51714, floating=kind == "F")
                expected.append(("random", value, original.state()))
                counts["random"] += 1
    strings = [b"", b"a", b"Ignition", b"\x7f", b"\x80", b"\xff", b"a" * 255]
    strings += [bytes(rng.randrange(1, 256) for _ in range(n)) for n in (3, 4, 7, 8, 31, 32, 127, 256)]
    for a in strings:
        for b in strings:
            for kind in ("L", "C", "A"):
                commands.append(f"S {kind} {a.hex() or '-'} {b.hex() or '-'}")
                expected.append(("string", original.strings(kind, a, b)))
                counts["strings"] += 1
    for kind, entry in (("S", 0x647CD), ("A", 0x64804), ("T", 0x64810), ("F", 0x6579C)):
        values = [-1.0, -0.5, -0.0, 0.0, 0.5, 1.0]
        values += [rng.uniform(-1, 1) for _ in range(100)]
        for a in values:
            for b in (-2.0, -0.5, 0.5, 2.0):
                commands.append(f"M {kind} {a.hex()} {b.hex()}")
                stack = struct.pack("<d", a) if kind in ("S", "A") else struct.pack("<dd", a, b)
                value = original.call(entry, stack=stack, floating=True)
                expected.append(("math", value, kind, a))
                counts["math"] += 1
    result = subprocess.run([str(args.runner.resolve()), "--oracle"], input="\n".join(commands) + "\n",
                            text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    if len(lines) != len(expected):
        raise ValueError(f"Incorrect output shape: {len(lines)} != {len(expected)}")
    max_error = 0.0
    zero_sign_differences = Counter()
    for index, (line, want) in enumerate(zip(lines, expected)):
        if want[0] == "random":
            value, state = line.split()
            value = float.fromhex(value) if value.startswith(("0x", "-0x")) else int(value)
            correct = value == want[1] and int(state) == want[2]
        elif want[0] == "math":
            value = float.fromhex(line)
            error = abs(value - want[1])
            max_error = max(max_error, error)
            correct = math.isfinite(value) and error <= 4e-15
            if value == want[1] == 0:
                if math.copysign(1, value) != math.copysign(1, want[1]):
                    zero_sign_differences[want[2]] += 1
                sign_source = want[3] if want[2] == "F" else want[1]
                correct = math.copysign(1, value) == math.copysign(1, sign_source)
        else:
            correct = line == want[1]
        if not correct:
            raise ValueError(f"Case {index}: native {line}, original {want}")
    missing = set(ENTRIES) - original.visits.keys()
    if missing:
        raise ValueError(f"Unexecuted original entries: {sorted(missing)}")
    sources = ["src/platform/runtime.c", "src/platform/runtime.h", "tests/runtime.c",
               "tests/runtime_original.py", "src/game/fx.c", "src/game/fx.h"]
    hashes = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}
    report = {"cases": dict(counts), "total": len(expected), "original_sha256": SHA256,
              "runner_sha256": hashlib.sha256(args.runner.read_bytes()).hexdigest(),
              "input_sha256": hashlib.sha256("\n".join(commands).encode()).hexdigest(),
              "sources": hashes, "unicorn": original.version, "max_math_error": max_error,
              "zero_sign_differences": dict(zero_sign_differences),
              "visits": {f"{k:08x}": v for k, v in sorted(original.visits.items())},
              "limitations": ["Native math tolerance 4e-15 for finite in-domain fixtures only.",
                              "fmod zero follows C11 dividend sign; original emulated FPREM can return +0.",
                              "No DOS I/O, software FPU, runtime startup or complete game execution.",
                              "RNG kernel exactness does not imply identical shared-stream scheduling."]}
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"runtime original: {len(expected)} cases OK; max math error {max_error:g}")


if __name__ == "__main__":
    main()
