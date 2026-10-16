#!/usr/bin/env python3
"""Compare complete POS cyclic-relative conversion 0x41244 with unmodified LE bytes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ORIGINAL_SHA256, BODY, STACK, ESP, RETURN, FPCW, ROOT

DATA, DATA_SIZE = 0x700000, 0x1000000


class PosOriginal(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.uc.mem_map(DATA, DATA_SIZE)
        self.size = 0

    def code(self, uc, address, size, _):
        if not 0x41244 <= address <= 0x412E6:
            raise ValueError(f"Unexpected POS instruction 0x{address:x}")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        if not (STACK <= address and address + size <= STACK + 0x10000 or
                DATA <= address and address + size <= DATA + self.size):
            raise ValueError(f"Unexpected POS write 0x{address:x}")

    def replay(self, count, data):
        self.size = len(data)
        if self.size > DATA_SIZE:
            raise ValueError("POS oracle fixture too large")
        if data:
            self.uc.mem_write(DATA, data)
        self.uc.mem_write(BODY, struct.pack("<I", count))
        self.uc.mem_write(0x1FEFEC, struct.pack("<I", BODY))
        self.uc.mem_write(0x1FEBF4, struct.pack("<I", DATA))
        for name in ("EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"):
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), 0)
        self.uc.reg_write(self.reg.UC_X86_REG_ESP, ESP)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.mem_write(ESP, struct.pack("<I", RETURN))
        self.uc.emu_start(0x41244, RETURN, timeout=2_000_000, count=1000000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF):
            raise ValueError("POS return/stack/x87 contract failed")
        return bytes(self.uc.mem_read(DATA, len(data))) if data else b""


def fixtures(data_dir):
    rng = random.Random(0x41244)
    cases = [("empty", 0, b""), ("no-animations", 4, struct.pack("<4i", -1, -1, -1, -1))]
    for i in range(160):
        count = rng.randrange(1, 21)
        data = bytearray(struct.pack("<%di" % count, *([-1] * count)))
        blocks = []
        for _ in range(rng.randrange(1, 9)):
            start, frames = len(data), rng.randrange(1, 13)
            blocks.append(start)
            data.extend(struct.pack("<2i", frames, rng.randrange(frames)))
            for _ in range(frames):
                values = [rng.choice((-0x80000000, 0x7FFFFFFF, rng.randint(-100000, 100000))) for _ in range(6)]
                data.extend(struct.pack("<6i", *values))
        for o in range(count):
            if rng.randrange(4):
                start = rng.choice(blocks)
                struct.pack_into("<i", data, o * 4, start // 4 - o)
        data.extend(b"\xa5" * 16)
        cases.append((f"random-shared-{i}", count, bytes(data)))
    for name in ("USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN"):
        directory = data_dir / "LEVELS" / name
        count, = struct.unpack("<I", (directory / f"{name}.PLC").read_bytes()[:4])
        cases.append((name, count, (directory / f"{name}.POS").read_bytes()))
    return cases


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--original", type=Path, required=True)
    p.add_argument("--data", type=Path, required=True)
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    report = dict(status="running", original_sha256=ORIGINAL_SHA256, cases=0, bytes_compared=0,
                  timestamp=datetime.now(timezone.utc).isoformat(),
                  scope="Full unmodified 0x41244, exact complete POS buffers; cyclic positions, signed overflow, "
                        "shared records, seven tracks. Native rejects malformed offsets/frame extents.")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    try:
        cases = fixtures(args.data)
        payload = "".join(f"{count} {len(data)} {data.hex()}\n" for _, count, data in cases)
        result = subprocess.run([str(args.runner), "--runner"], input=payload, text=True,
                                capture_output=True, timeout=30)
        if result.returncode:
            raise ValueError(f"Native POS runner failed: {result.stderr}")
        lines = result.stdout.splitlines()
        if len(lines) != len(cases):
            raise ValueError("Native POS runner returned an incorrect number of buffers")
        oracle = PosOriginal(args.original)
        for (name, count, data), line in zip(cases, lines):
            expected = oracle.replay(count, data)
            if bytes.fromhex(line) != expected:
                raise ValueError(f"{name}: native/original POS buffer mismatch")
            report["cases"] += 1
            report["bytes_compared"] += len(data)
        for malformed in ("x", "-1 4 ffffffff", "1 4 00000000", "1 4 fffffffe", "1 8", "1 4 zzzzzzzz"):
            failed = subprocess.run([str(args.runner), "--runner"], input=malformed, text=True,
                                    capture_output=True, timeout=5)
            if failed.returncode == 0 or not failed.stderr:
                raise ValueError("Native POS runner did not reject malformed input explicitly")
        report.update(status="pass", fixture_sha256=hashlib.sha256(payload.encode()).hexdigest(),
                      runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                      visited_instructions=len(oracle.visited),
                      sources={str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in (ROOT / "src/game/pos.c", ROOT / "src/game/pos.h",
                                            ROOT / "tests/pos.c", Path(__file__))})
        print(f"pos_original: {report['cases']} cases / {report['bytes_compared']} bytes, exact complete buffers")
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
