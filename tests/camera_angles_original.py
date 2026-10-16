#!/usr/bin/env python3
"""Execute full camera_angles and its unmodified pow/CHP callees, comparing integer angle outputs exactly."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original as PhysicsOriginal, BODY, ESP, FPCW, RETURN, STACK, ORIGINAL_SHA256

ROOT = Path(__file__).resolve().parents[1]
CONFIG = BODY
ENTRY = 0x5891C


class Original(PhysicsOriginal):
    def code(self, uc, address, size, _):
        ranges = ((ENTRY, 0x58C18), (0x55F9C, 0x55FB8), (0x64C2A, 0x64DB7), (0x7D68F, 0x7D731))
        if not any(lo <= address <= hi for lo, hi in ranges):
            raise ValueError(f"Unexpected camera code 0x{address:x}")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        ranges = ((STACK, 0x10000), (CONFIG + 0x18, 24), (0x24C05C, 4))
        if not any(start <= address and address + size <= start + count for start, count in ranges):
            raise ValueError(f"Unexpected camera write 0x{address:x}")

    def run(self, case):
        x, y, z, tx, ty, tz, roll, yaw = case
        raw = bytearray(b"\xa5" * 0xC8)
        struct.pack_into("<3d", raw, 0, x, y, z)
        struct.pack_into("<3d", raw, 0x18, -789.25, yaw, 456.5)
        struct.pack_into("<4i", raw, 0x70, tx, ty, tz, roll)
        self.uc.mem_write(CONFIG, bytes(raw))
        self.uc.mem_write(0x24C05C, struct.pack("<I", CONFIG))
        self.uc.mem_write(STACK, b"\xa5" * 0x10000)
        self.uc.mem_write(ESP, struct.pack("<I", RETURN))
        for name in ("EAX", "EDX", "EBX", "ECX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, f"UC_X86_REG_{name}"), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.emu_start(ENTRY, RETURN, timeout=2_000_000, count=100000)
        if self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN:
            raise ValueError("Camera did not return within execution limits")
        if self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4:
            raise ValueError("Camera changed stack balance")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW:
            raise ValueError("Camera changed the control word")
        if self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF:
            raise ValueError("Camera leaked x87 stack values")
        out = bytes(self.uc.mem_read(CONFIG, len(raw)))
        if out[:0x18] != raw[:0x18] or out[0x30:] != raw[0x30:]:
            raise ValueError("Camera changed input/unknown bytes")
        return struct.unpack_from("<3d", out, 0x18)


def cases():
    for x, y, z in itertools.product((-10.5, -1.0, -0.5, 0.0, 0.5, 1.0, 10.5), repeat=3):
        yield (x, y, z, 0, 0, 0, 123, 456.25)
    for roll in (-2147483648, -3600, -1, 0, 1, 3600, 2147483647):
        yield (0, 0, 0, 0, 0, 0, roll, -123.5)
    rng = random.Random(0x5891C)
    for _ in range(1200):
        yield (*[rng.uniform(-51200, 51200) for _ in range(3)],
               *[rng.randint(-51200, 51200) for _ in range(3)], rng.randint(-3600, 3600),
               rng.uniform(-7200, 7200))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--report", type=Path, required=True)
    args = ap.parse_args()
    original = Original(args.original)
    inputs = list(cases())
    result = subprocess.run([str(args.runner.resolve()), "--runner"], text=True, capture_output=True,
                            input="\n".join(" ".join(map(str, case)) for case in inputs) + "\n", check=True)
    outputs = [tuple(map(float, line.split())) for line in result.stdout.splitlines()]
    if len(outputs) != len(inputs):
        raise ValueError("Incorrect native output count")
    digest = hashlib.sha256()
    for i, (case, native) in enumerate(zip(inputs, outputs)):
        expected = original.run(case)
        if expected != native:
            raise ValueError(f"Camera case {i}: {case}; original {expected}, native {native}")
        digest.update(struct.pack("<3d", *expected))
    sources = ("src/render/lisa.c", "src/render/lisa.h", "tests/camera_angles.c", "tests/camera_angles_original.py")
    args.report.write_text(json.dumps({
        "original_sha256": ORIGINAL_SHA256, "unicorn": "2.1.4", "control_word": hex(FPCW),
        "function": hex(ENTRY), "cases": len(inputs), "exact": True,
        "scope": "all quadrants, axes, coincident points, sub-unit differences, roll extremes; native render_scene look-at flag",
        "domain": "finite engine positions +/-51200; truncations fit int32",
        "outputs_sha256": digest.hexdigest(),
        "sources_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
        "runner_sha256": hashlib.sha256(args.runner.read_bytes()).hexdigest(),
        "executed_instructions": len(original.visited),
    }, indent=2) + "\n")
    print(f"{len(inputs)} exact original camera cases, including unmodified pow/CHP callees")


if __name__ == "__main__":
    main()
