#!/usr/bin/env python3
"""Check exact no-rival replay determinism, not equivalence with the DOS game.

Runs car 9, difficulty 0, at 640x480 on all seven tracks with auto (40 s),
straight throttle (8 s) and turbo (8 s). Captures the initial state and every
tick in IGN_TRACE=full, compares two fresh processes, and retains provenance.
An optional --reference-binary compares a separately built port instead.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

import regress

ROOT = Path(regress.ROOT)
sys.path.insert(0, str(ROOT / "tools"))
from trace_compare import compare_rows, load_text

FIELDS = set("""trace_schema tick t
in_accel in_brake in_left in_right in_turbo in_analog in_a_accel in_a_brake in_a_steer in_manual_gear in_gear_shift
id spec x y z heading kmh height ground ground_prev fall_speed grip_avg airborne ground_object surface_type special_ground
level_change jump_mode wall_hit wall_impact nwalls gear forward_gear gear_ticks gear_delay steer_visual revs
turbo_charge turbo_mode turbo_wait turbo_key creep creep_ticks catchup catchup_clock section phase lap position
race_elapsed lap_elapsed lap_origin
out wrecked wrecks wreck_kind wreck_frame landed offroad step_x step_z
b_mass b_inertia b_restitution b_a b_b b_heading b_yaw_rate b_px b_pz b_vx b_vz b_v_fwd b_v_lat b_saturated
b_steer b_pitch b_roll b_grip_f b_grip_r b_sliding b_drive b_fmax_f b_fmax_r b_radius b_impact b_contact_x b_contact_z
""".split()) | {prefix + str(i) for prefix in ("wheel_type", "b_corner_x", "b_corner_z") for i in range(4)}


def validate_trace(text, ticks):
    rows = load_text(text)
    if len(rows) != ticks + 1:
        raise ValueError("expected initial state and %d ticks, got %d rows" % (ticks, len(rows)))
    for i, row in enumerate(rows):
        if row.keys() != FIELDS or row["trace_schema"] != 4:
            raise ValueError("row %d: incomplete or unsupported full trace schema" % i)
        if row["tick"] != i or row["t"] != i * (1.0 / 72.0):
            raise ValueError("row %d: incorrect tick/time alignment" % i)
    return rows


def data_manifest(data):
    if not data.is_dir():
        raise ValueError("game data directory does not exist: %s" % data)
    files = {p.relative_to(data).as_posix(): regress.sha256(p)
             for p in sorted(data.rglob("*")) if p.is_file()}
    if not files:
        raise ValueError("empty game data directory")
    digest = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return {"sha256": digest, "files": files}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", type=Path, default=ROOT / "build/ignition")
    ap.add_argument("--reference-binary", type=Path)
    ap.add_argument("--data", type=Path, default=os.getenv("IGNITION_DATA", "/Users/Shared/Epic Games/Ignition/ignition"))
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("-k", default="", help="select case names containing this text")
    args = ap.parse_args()
    cases = [(track + " " + mode, track, mode, seconds)
             for track in regress.TRACKS for mode, seconds in (("auto", 40), ("straight", 8), ("turbo", 8))
             if args.k in track + " " + mode]
    if not cases:
        ap.error("no matching cases")
    report = {"status": "running", "authority": "port-replay-only", "cases": {},
              "platform": platform.platform(), "python": platform.python_version(),
              "data_directory": str(args.data.resolve())}
    regress.write_report(args.report, report)
    try:
        binaries = [args.binary.resolve(), (args.reference_binary or args.binary).resolve()]
        report["binaries"] = [{"path": str(b), "sha256": regress.sha256(b)} for b in binaries]
        report["sources"] = {p.relative_to(ROOT).as_posix(): regress.sha256(p) for p in sorted(
            list((ROOT / "src").rglob("*.c")) + list((ROOT / "src").rglob("*.h")) +
            [ROOT / "CMakeLists.txt", Path(__file__), ROOT / "tests/regress.py", ROOT / "tools/trace_compare.py"])}
        report["data"] = data_manifest(args.data)
        artifacts = args.report.with_suffix("")
        artifacts.mkdir(exist_ok=True)
        env = regress.controlled_environment(args.data, "full")
        report["environment"] = {k: v for k, v in env.items() if k.startswith("IGN")}
        total_ticks, failed = 0, 0
        for name, track, mode, seconds in cases:
            cmd = [track, "--sim-ticks", str(seconds * 72), "0" if mode == "straight" else mode,
                   "--rivals", "0", "--car", "9", "--difficulty", "0", "--res", "640x480"]
            captures, rows = [], []
            case = {"arguments": cmd, "captures": [], "status": "running"}
            report["cases"][name] = case
            for i, binary in enumerate(binaries):
                _, result = regress.run_case(str(binary), name, cmd, env=env, trace=True)
                path = artifacts / ("%s-%s-%d.txt" % (track, mode, i))
                path.write_text(result.pop("output"))
                text = result.pop("trace")
                result["trace_sha256"] = hashlib.sha256(text.encode()).hexdigest()
                result["capture_path"] = str(path.resolve())
                case["captures"].append(result)
                if result["rc"] or result["nan"] or result["image"] is None or len(result["states"]) != 1:
                    raise ValueError("%s capture %d: failed process, invalid state or missing framebuffer" % (name, i))
                rows.append(validate_trace(text, seconds * 72))
                captures.append(result)
            diff, _ = compare_rows(*rows, {}, all_fields=True)
            image_changed = captures[0]["image"] != captures[1]["image"]
            state_changed = captures[0]["states"] != captures[1]["states"]
            case.update(status="fail" if diff or image_changed or state_changed else "pass",
                        ticks=seconds * 72, fields=len(rows[0][0]), first_divergence=diff,
                        image_changed=image_changed, final_state_changed=state_changed)
            failed += case["status"] == "fail"
            total_ticks += seconds * 72 * 2
            print("%s %s: %d ticks/run, %d fields%s" %
                  (case["status"], name, case["ticks"], case["fields"], " first divergence: %s" % diff if diff else ""))
            regress.write_report(args.report, report)
        if report["data"] != data_manifest(args.data):
            raise ValueError("game data changed during capture")
        if any(b["sha256"] != regress.sha256(b["path"]) for b in report["binaries"]):
            raise ValueError("binary changed during capture")
        if any(digest != regress.sha256(ROOT / path) for path, digest in report["sources"].items()):
            raise ValueError("source changed during capture")
        report.update(status="fail" if failed else "pass", passed=len(cases) - failed,
                      total=len(cases), executed_ticks=total_ticks)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        for case in report["cases"].values():
            if case["status"] == "running":
                case.update(status="fail", error=str(exc))
        report.update(status="fail", error=str(exc))
        print("solo replay failed: %s" % exc, file=sys.stderr)
    regress.write_report(args.report, report)
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
