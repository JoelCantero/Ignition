#!/usr/bin/env python3
"""Build an offline dashboard from the published port inventory (no game assets)."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from function_catalog import parse_catalog, summarize

SOURCES = ("docs/functions.json", "README.md")


def build_data(root=ROOT):
    sources = {name: (root / name).read_text(encoding="utf-8") for name in SOURCES}
    catalog = parse_catalog(sources[SOURCES[0]])
    metrics = summarize(catalog)
    rows = [
        {"address": s["address"], "function": s["name"], "status": s["status"],
         "instructions": s["instructions"], "roadmap": s["roadmap"],
         **{key: s[key] for key in ("classification", "notes", "implementation", "tests", "references", "review", "aliases")}}
        for s in catalog["symbols"] if s["kind"] == "function"
    ]

    phases = []
    roadmap = sources["README.md"].partition("## Roadmap\n")[2].partition("\n## ")[0]
    for line in roadmap.splitlines():
        heading = re.match(r"### Fase (\d+) — (.+)", line)
        task = re.match(r"- \[([ x~])\] (.+)", line)
        if heading:
            phases.append({"number": int(heading[1]), "title": heading[2], "done": 0, "partial": 0, "pending": 0})
        elif task and phases:
            phases[-1][{"x": "done", "~": "partial", " ": "pending"}[task[1]]] += 1
    if not phases:
        raise ValueError("No roadmap phases found in README.md")
    digest = hashlib.sha256()
    for name, text in sources.items():
        digest.update(name.encode("utf-8") + b"\0" + text.encode("utf-8") + b"\0")
    return {
        "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "fingerprint": digest.hexdigest()[:12],
        "sources": list(SOURCES),
        "metrics": {
            "ported": metrics["ported_instructions"], "instructions": metrics["game_instructions"],
            "partial": metrics["partial_instructions"],
            "coveredBytes": metrics["covered_code_bytes"], "codeBytes": metrics["code_bytes"],
            "mapped": metrics["mapped"], "gameFunctions": metrics["game_functions"],
            "mappedAll": metrics["mapped_all"],
            "classification": metrics["classification"],
            "classificationAll": metrics["classification_all"],
            "libraryNonruntime": metrics["library_nonruntime_subsystems"],
        },
        "subsystems": catalog["subsystems"],
        "functions": rows,
        "phases": phases,
    }


def render(data):
    template = (ROOT / "tools/port_dashboard.template.html").read_text(encoding="utf-8")
    payload = json.dumps(data, ensure_ascii=True, separators=(",", ":")).replace("<", "\\u003c")
    return template.replace("__PORT_DATA__", payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, default=ROOT / "docs/port-dashboard.html")
    args = parser.parse_args()
    try:
        data = build_data()
    except (OSError, ValueError) as error:
        parser.exit(1, "Dashboard generation failed: %s\n" % error)
    args.output.write_text(render(data), encoding="utf-8")
    print("Dashboard: %s (%d functions, snapshot %s)" % (
        args.output, len(data["functions"]), data["fingerprint"],
    ))


if __name__ == "__main__":
    main()
