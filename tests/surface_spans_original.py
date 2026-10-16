#!/usr/bin/env python3
"""Compare both complete original SRF walkers, including ordered output lists, with native span tests."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from physics_original import Original, ORIGINAL_SHA256, BODY, STACK, ESP, RETURN, FPCW, ROOT

SPANS, OBJECTS, MESHES, OUTPUT, LIST = (BODY + n for n in (0x100, 0x800, 0x900, 0xA00, 0xC00))


class SurfaceOriginal(Original):
    def code(self, uc, address, size, _):
        if not 0x20C18 <= address <= 0x20CE9:
            raise ValueError(f"Unexpected SRF instruction 0x{address:x}")
        self.visited.add(address)

    def write(self, uc, access, address, size, value, _):
        if not (STACK <= address and address + size <= STACK + 0x10000 or
                OUTPUT <= address and address + size <= OUTPUT + 256):
            raise ValueError(f"Unexpected SRF write 0x{address:x}")

    def replay(self, descending, x, z, spans):
        self.uc.mem_write(OUTPUT, b"\xa5" * 256)
        pairs = {}
        for i, span in enumerate(spans):
            obj, mesh, face = OBJECTS + (i % 2) * 16, MESHES + (i % 2) * 16, i * 11 + 2
            self.uc.mem_write(obj + 4, struct.pack("<I", mesh))
            self.uc.mem_write(SPANS + i * 24, struct.pack("<4ihHI", *span, face, obj))
            self.uc.mem_write(LIST + i * 4, struct.pack("<I", SPANS + i * 24))
            pairs[(obj, mesh + face * 4)] = i
        values = dict(EAX=0, EBX=z, ECX=len(spans), EDX=x, ESI=LIST, EDI=OUTPUT, EBP=0, ESP=ESP)
        for name, value in values.items():
            self.uc.reg_write(getattr(self.reg, "UC_X86_REG_" + name), value & 0xFFFFFFFF)
        self.uc.reg_write(self.reg.UC_X86_REG_FPCW, FPCW)
        self.uc.reg_write(self.reg.UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.mem_write(ESP, struct.pack("<I", RETURN))
        self.uc.emu_start(0x20C81 if descending else 0x20C18, RETURN, timeout=1_000_000, count=10000)
        if (self.uc.reg_read(self.reg.UC_X86_REG_EIP) != RETURN or
                self.uc.reg_read(self.reg.UC_X86_REG_ESP) != ESP + 4 or
                self.uc.reg_read(self.reg.UC_X86_REG_FPCW) != FPCW or
                self.uc.reg_read(self.reg.UC_X86_REG_FPTAG) != 0xFFFF):
            raise ValueError("SRF return/stack/x87 contract failed")
        end = self.uc.reg_read(self.reg.UC_X86_REG_EDI)
        if end < OUTPUT or end > OUTPUT + len(spans) * 8 or (end - OUTPUT) % 8:
            raise ValueError("Invalid original SRF output extent")
        data = bytes(self.uc.mem_read(OUTPUT, 256))
        if data[end - OUTPUT:] != b"\xa5" * (OUTPUT + 256 - end):
            raise ValueError("SRF walker overwrote the output tail")
        return [pairs[pair] for pair in struct.iter_unpack("<2I", data[:end - OUTPUT])]


def i32(value):
    return (value + 0x80000000) % 0x100000000 - 0x80000000


def fixtures():
    cases = []
    rng = random.Random(0x20C18)
    for descending in (0, 1):
        cases.append((descending, 0, 0, []))
        for dz in (-100, 0, 100):
            for z in (min(0, dz) - 1, min(0, dz), 0, max(0, dz), max(0, dz) + 1):
                for x in (-101, -100, -1, 0, 1, 100, 101):
                    cases.append((descending, x, z, [(0, 0, -65536, 65536, dz)]))
        for x in (-2, 65534):
            cases.append((descending, x, -32767 if descending else 32767,
                          [(0, 0, -131072 if descending else 131072,
                            -131072 if descending else 131072, -32767 if descending else 32767)]))
        for _ in range(800):
            z0 = rng.choice((rng.randint(-100000, 100000), 0x7FFFFFF0, -0x7FFFFFF0))
            dz = rng.randint(0, 32767) * (-1 if descending else 1)
            z = i32(z0 + rng.choice((0, dz, dz // 2, -1, 1)))
            x0 = rng.randint(-0x80000000, 0x7FFFFFFF)
            slopes = [rng.choice((-0x80000000, -65536, -1, 0, 1, 65536, 131072, 0x7FFFFFFF)) for _ in range(2)]
            spans = [(x0, z0, *slopes, dz)]
            for _ in range(rng.randrange(8)):
                spans.append((rng.randint(-100000, 100000), z0, rng.randint(-0x80000000, 0x7FFFFFFF),
                              rng.randint(-0x80000000, 0x7FFFFFFF), dz))
            delta = i32(z - z0)
            edge = i32(x0 + (i32(delta * slopes[0]) >> 16))
            x = i32(edge + rng.choice((-1, 0, 1)))
            cases.append((descending, x, z, spans))
    return cases


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--original", type=Path, required=True)
    p.add_argument("--runner", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    report = dict(status="running", original_sha256=ORIGINAL_SHA256, cases=0, spans=0,
                  timestamp=datetime.now(timezone.utc).isoformat(),
                  scope="Unmodified full 0x20c18 and 0x20c81; ordered object/face outputs, empty lists, "
                        "signed bounds, wrapping IMUL/ADD and SAR; no intercepted callees.")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    try:
        cases = fixtures()
        payload = "".join(" ".join(map(str, (d, x, z, *span))) + "\n"
                          for d, x, z, spans in cases for span in spans)
        result = subprocess.run([str(args.runner), "--runner"], input=payload, text=True,
                                capture_output=True, timeout=30)
        if result.returncode:
            raise ValueError(f"Native SRF runner failed: {result.stderr}")
        outputs = result.stdout.splitlines()
        if len(outputs) != sum(len(c[3]) for c in cases) or any(v not in ("0", "1") for v in outputs):
            raise ValueError("Native SRF runner returned an invalid result shape")
        oracle, offset = SurfaceOriginal(args.original), 0
        for i, (descending, x, z, spans) in enumerate(cases):
            native = [j for j in range(len(spans)) if outputs[offset + j] == "1"]
            expected = oracle.replay(descending, x, z, spans)
            if native != expected:
                raise ValueError(f"SRF case {i}: native={native}, original={expected}")
            offset += len(spans)
            report["cases"] += 1
            report["spans"] += len(spans)
        for malformed in ("x", "2 0 0 0 0 0 0 0", "0 0 0 0 0 0 0 32768", "0 0"):
            failed = subprocess.run([str(args.runner), "--runner"], input=malformed, text=True,
                                    capture_output=True, timeout=5)
            if failed.returncode == 0 or not failed.stderr:
                raise ValueError("Native SRF runner did not reject malformed input explicitly")
        report.update(status="pass", fixture_sha256=hashlib.sha256(payload.encode()).hexdigest(),
                      runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest(),
                      visited_instructions=len(oracle.visited),
                      sources={str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in (ROOT / "src/game/assets.c", ROOT / "src/game/assets.h",
                                            ROOT / "tests/surface_spans.c", Path(__file__))})
        print(f"surface_spans_original: {report['cases']} lists / {report['spans']} spans, exact ordered outputs")
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
