#!/usr/bin/env python3
"""Compare frame_time 0x2b734 with the unmodified LE, including the pause-origin write."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess

import unicorn

from hud_original import Original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    original = Original(args.original)
    uc = original.uc
    returned_delta = [None]
    def capture_delta(machine, address, _size, _user):
        if address == 0x2B840:
            sp = machine.reg_read(original.reg.UC_X86_REG_ESP)
            returned_delta[0] = struct.unpack("<d", machine.mem_read(sp + 8, 8))[0]
    uc.hook_add(unicorn.UC_HOOK_CODE, capture_delta)
    uc.reg_write(original.reg.UC_X86_REG_FPCW, 0x127F)
    rng = random.Random(0x2B734)
    cases = [(0.0, 0, n, pause) for n in (0, 1, 10, 27, 28, 300, 301, 1000) for pause in (0, 1)]
    for _ in range(500):
        now = rng.randrange(1000, 10000000)
        delta = rng.choice((0.0, 0.01, 0.5, 0.999, 1.0, 1.001, 1.5, 10.799, 10.8, 10.801, 100.0))
        cases.append((now * 0.036 - delta, rng.randrange(0, now), now, rng.randrange(2)))
    text = "".join(f"{previous:.17g} {origin} {now} {pause}\n" for previous, origin, now, pause in cases)
    result = subprocess.run([str(args.runner.resolve()), "--frame"], input=text, text=True, capture_output=True,
                            check=True)
    lines = result.stdout.splitlines()
    if len(lines) != len(cases):
        raise AssertionError("Native runner returned a different number of samples")
    failures = []
    for index, ((previous, origin, now, pause), line) in enumerate(zip(cases, lines)):
        uc.mem_write(0x1FB538, struct.pack("<d", previous))
        uc.mem_write(0x1FB540, struct.pack("<d", 0.0))
        for address, value in ((0xAB5DC, now), (0x1FEF04, origin), (0xC0B20, pause),
                               (0xC0B14, 0), (0xC0B18, 0), (0xC0B1C, 0)):
            uc.mem_write(address, struct.pack("<i", value))
        original.call(0x2B734)
        uc.reg_write(original.reg.UC_X86_REG_FPTAG, 0xFFFF)
        expected = [returned_delta[0],
                    struct.unpack("<d", uc.mem_read(0x1FB540, 8))[0],
                    struct.unpack("<i", uc.mem_read(0x1FEF04, 4))[0],
                    struct.unpack("<i", uc.mem_read(0xC0B14, 4))[0],
                    struct.unpack("<i", uc.mem_read(0xC0B1C, 4))[0]]
        values = line.split()
        native = [float(values[0]), float(values[1]), *map(int, values[2:])]
        if native != expected:
            failures.append({"case": index, "input": cases[index], "original": expected, "native": native})
    qualifications = [(a, b, players) for a in range(1, 7) for b in range(1, 7) for players in (0, 1)]
    inputs = "".join(f"{a - 1} {b - 1} {players}\n" for a, b, players in qualifications)
    native = subprocess.check_output([str(args.runner.resolve()), "--qualified"], input=inputs, text=True)
    if len(native.splitlines()) != len(qualifications):
        raise AssertionError("Qualification runner omitted samples")
    for (a, b, players), value in zip(qualifications, native.splitlines()):
        for address, content in ((0x1FEFE0, struct.pack("<I", 0x400000)),
                                  (0x4003A0, struct.pack("<i", a)),
                                  (0x404BEC, struct.pack("<i", b)),
                                  (0x1FEF14, struct.pack("<i", players)),
                                  (0x1FEFE4, struct.pack("<i", 2))):
            uc.mem_write(address, content)
        original.run(0x44A24, 0x44A5D, edx=2, ebx=0)
        expected = uc.reg_read(original.reg.UC_X86_REG_EBX)
        if int(value) != expected:
            failures.append({"qualification": [a, b, players], "original": expected, "native": int(value)})
    report = {"function": "0002b734", "original_sha256": original_sha(), "cases": len(cases),
              "qualification_cases": len(qualifications),
              "failures": failures}
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"frame_time original: {len(cases)} cases; qualification: {len(qualifications)}; {len(failures)} failures")
    return bool(failures)


def original_sha():
    from hud_original import ORIGINAL_SHA256
    return ORIGINAL_SHA256


if __name__ == "__main__":
    raise SystemExit(main())
