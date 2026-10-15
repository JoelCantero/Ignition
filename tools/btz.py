#!/usr/bin/env python3
"""Inspect Ignition configuration and records (IGN_DOS.BTZ).

  tools/btz.py info [FILE]             known settings, participants and the record tables of the 7 tracks
  tools/btz.py records [FILE] [--track N]   only the records
  tools/btz.py roundtrip [FILE] [-o OUT]    parse every field, rebuild the file and compare the bytes

FILE defaults to $IGNITION_DATA/IGN_DOS.BTZ. Layout (loader 0x2293c, writers 0x2cf60 and 0x2b430): raw image of
the 0x597 bytes at 0x1ff158, packed (unaligned int32 and binary64). See docs/formats.md, section BTZ.
"""
import argparse
import os
import struct
import sys

SIZE = 0x597
DATA = os.environ.get("IGNITION_DATA", "/Users/Shared/Epic Games/Ignition/ignition")
LEVELS = ["CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"]
MODES = ["championship", "single race ¿?", "time trial", "elimination"]
LANGUAGES = ["English", "Deutsch", "Italiano", "Español", "Svenska", "Français"]
RECORDS, STRIDE, SLOTS = 0x1BF, 0x78, 5

# (offset, count, name, global/meaning). Every byte not listed here is kept as opaque data.
FIELDS = [
    (0x000, 1, "unknown_000", "30 in the shipped file"),
    (0x004, 1, "players", "[0x1fef14] human players - 1"),
    (0x008, 1, "mode", "[0x1feefc] 0..3"),
    (0x00C, 1, "class", "[0x1fedd8] ¿? row of the championship table 0xc45b4"),
    (0x010, 1, "ghost", "[0x1fee4c] ghost on (only kept in time trial)"),
    (0x014, 1, "opt_014", "[0x1fed64] (forced 1 in championship)"),
    (0x018, 1, "unknown_018", ""),
    (0x01C, 9, "car", "participant+0x00 car model"),
    (0x040, 2, "unknown_040", ""),
    (0x048, 9, "opt_048", "participant+0x0c"),
    (0x0F3, 1, "track", "track id, championship round when mode 0"),
    (0x0F7, 1, "opt_0f7", "[0x1feed8] (0 = track default, -1 = random)"),
    (0x0FB, 1, "opt_0fb", "[0x1feef4] = value * 10"),
    (0x0FF, 1, "opt_0ff", "[0x1fee94] = value * 10"),
    (0x103, 1, "unknown_103", ""),
    (0x107, 1, "opt_107", "[0x1fef08]/[0x1fefdc], written back by 0x2cf60"),
    (0x10B, 1, "opt_10b", "[0x1fefcc], written back by 0x2cf60"),
    (0x10F, 1, "opt_10f", "[0x1fee84]"),
    (0x113, 1, "opt_113", "[0x1fed80]"),
    (0x117, 1, "opt_117", "[0x1fed70]"),
    (0x11B, 2, "unknown_11b", ""),
    (0x123, 2, "human_08", "participant+0x08 of the human cars"),
    (0x12B, 2, "human_18", "participant+0x18 of the human cars"),
    (0x133, 8, "grid", "0xc33d0 grid order (6 - place after a championship race)"),
    (0x153, 1, "cars", "[0x1fefe4] cars in the race"),
    (0x157, 1, "unknown_157", ""),
    (0x15B, 1, "opt_15b", "[0x1fee48]"),
    (0x15F, 8, "computer", "participant+0x04 (1 = computer ¿?)"),
    (0x197, 1, "opt_197", "[0x1febe0]"),
    (0x587, 1, "opt_587", "[0x1ff6df] written by render_main"),
    (0x58B, 1, "opt_58b", "[0x1ff6e3] written by render_main"),
    (0x593, 1, "language", "[0x1ff6eb] 0 EN, 1 DE, 2 IT, 3 ES, 4 SV, 5 FR"),
]
NAMES, NAME_LEN, MAX_CARS = 0x6C, 15, 9
KEYS, KEY_LEN = 0x17F, 8  # 2 humans x 8 key bytes -> participant+0x1c..0x23


def cstr(b):
    return b.split(b"\0")[0].decode("latin-1")


def parse(data):
    if len(data) != SIZE:
        raise ValueError("%d bytes, expected %d" % (len(data), SIZE))
    out = {"raw": bytearray(data)}
    for off, n, name, _ in FIELDS:
        v = list(struct.unpack_from("<%di" % n, data, off))
        out[name] = v[0] if n == 1 else v
    out["names"] = [data[NAMES + i * NAME_LEN:NAMES + (i + 1) * NAME_LEN] for i in range(MAX_CARS)]
    out["keys"] = [data[KEYS + i * KEY_LEN:KEYS + (i + 1) * KEY_LEN] for i in range(2)]
    out["records"] = []
    for t in range(len(LEVELS)):
        base = RECORDS + t * STRIDE
        out["records"].append({
            "race_name": [data[base + i * 4:base + i * 4 + 4] for i in range(SLOTS)],
            "race": list(struct.unpack_from("<5d", data, base + 0x14)),
            "lap_name": [data[base + 0x3C + i * 4:base + 0x40 + i * 4] for i in range(SLOTS)],
            "lap": list(struct.unpack_from("<5d", data, base + 0x50)),
        })
    return out


def build(p):
    """Writes every decoded field over the opaque image, so a mismatch shows a decoding error."""
    data = bytearray(p["raw"])
    for off, n, name, _ in FIELDS:
        v = p[name] if n > 1 else [p[name]]
        struct.pack_into("<%di" % n, data, off, *v)
    for i, nm in enumerate(p["names"]):
        data[NAMES + i * NAME_LEN:NAMES + (i + 1) * NAME_LEN] = nm
    for i, k in enumerate(p["keys"]):
        data[KEYS + i * KEY_LEN:KEYS + (i + 1) * KEY_LEN] = k
    for t, r in enumerate(p["records"]):
        base = RECORDS + t * STRIDE
        for i in range(SLOTS):
            data[base + i * 4:base + i * 4 + 4] = r["race_name"][i]
            data[base + 0x3C + i * 4:base + 0x40 + i * 4] = r["lap_name"][i]
        struct.pack_into("<5d", data, base + 0x14, *r["race"])
        struct.pack_into("<5d", data, base + 0x50, *r["lap"])
    return bytes(data)


def mapped_mask():
    m = [False] * SIZE
    spans = [(off, n * 4) for off, n, _, _ in FIELDS]
    spans += [(NAMES, NAME_LEN * MAX_CARS), (KEYS, KEY_LEN * 2), (RECORDS, STRIDE * len(LEVELS))]
    for off, n in spans:
        for i in range(off, off + n):
            m[i] = True
    return m


def show_records(p, tracks):
    for t in tracks:
        r = p["records"][t]
        print("%-8s race: %s" % (LEVELS[t], "  ".join(
            "%s %7.2f" % (cstr(n), v) for n, v in zip(r["race_name"], r["race"]))))
        print("%-8s lap:  %s" % ("", "  ".join(
            "%s %7.2f" % (cstr(n), v) for n, v in zip(r["lap_name"], r["lap"]))))


def info(p):
    for off, n, name, note in FIELDS:
        v = p[name]
        if name == "mode" and 0 <= v < len(MODES):
            note += " (%s)" % MODES[v]
        if name == "language" and 0 <= v < len(LANGUAGES):
            note += " (%s)" % LANGUAGES[v]
        print("+0x%03x %-12s %-28s %s" % (off, name, v, note))
    print("participants:", ", ".join(cstr(n) for n in p["names"]))
    print("keys:", " ".join(k.hex() for k in p["keys"]))
    print("unmapped: +0x19b..+0x1bf, +0x507..+0x587, +0x58f (frontend ¿?)")
    show_records(p, range(len(LEVELS)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for cmd in ("info", "records", "roundtrip"):
        s = sub.add_parser(cmd)
        s.add_argument("file", nargs="?", default=os.path.join(DATA, "IGN_DOS.BTZ"))
        if cmd == "records":
            s.add_argument("--track", type=int)
        if cmd == "roundtrip":
            s.add_argument("-o", "--output")
    a = ap.parse_args()
    data = open(a.file, "rb").read()
    try:
        p = parse(data)
    except ValueError as e:
        print("%s: %s" % (a.file, e), file=sys.stderr)
        return 1
    if a.cmd == "info":
        info(p)
    elif a.cmd == "records":
        show_records(p, [a.track] if a.track is not None else range(len(LEVELS)))
    else:
        # 1) parse+build over the original image must give the same bytes;
        # 2) build over a zeroed image must reproduce every decoded byte (only unmapped bytes may differ).
        out = build(p)
        if a.output:
            open(a.output, "wb").write(out)
        diff = [i for i in range(SIZE) if out[i] != data[i]]
        p["raw"] = bytearray(SIZE)
        rebuilt = build(p)
        mapped = mapped_mask()
        lost = [i for i in range(SIZE) if mapped[i] and rebuilt[i] != data[i]]
        opaque = sum(1 for i in range(SIZE) if not mapped[i] and data[i])
        print("%s: %d bytes, %d differ after parse+build; %d/%d decoded bytes rebuilt from zero, "
              "%d non-zero bytes left opaque" % (a.file, SIZE, len(diff), sum(mapped) - len(lost), sum(mapped),
                                                 opaque))
        return 1 if diff or lost else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
