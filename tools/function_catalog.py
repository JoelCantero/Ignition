#!/usr/bin/env python3
"""Read, validate and query the single editable catalog of original functions and data."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "docs/functions.json"
STATUS_ORDER = ("unknown", "identified", "understood", "port-pending", "partial",
                "ported", "verified", "native", "library")
FUNCTION_STATUSES = frozenset(STATUS_ORDER)
CLASSIFICATION_CONFIDENCES = ("documented", "inferred", "unresolved")
COVERAGE_FIELDS = ("unique_instructions", "library_instructions", "ported_instructions",
                   "partial_instructions", "covered_code_bytes", "code_bytes")


def inventory_fingerprint(catalog):
    rows = [(s["address"], s["status"], s["instructions"]) for s in catalog["symbols"]
            if s["kind"] == "function"]
    return hashlib.sha256(json.dumps(sorted(rows), separators=(",", ":")).encode()).hexdigest()


def validate_catalog(catalog, verify_snapshot=True):
    if (not isinstance(catalog, dict) or type(catalog.get("schema_version")) is not int or
            catalog["schema_version"] != 2):
        raise ValueError("Unsupported function catalog schema_version")
    executable = catalog.get("executable", {})
    if (not isinstance(executable, dict) or not isinstance(executable.get("sha256"), str) or
            not re.fullmatch(r"[0-9a-f]{64}", executable["sha256"])):
        raise ValueError("Invalid catalog executable SHA-256")
    symbols = catalog.get("symbols")
    if not isinstance(symbols, list) or not symbols:
        raise ValueError("Catalog must contain a nonempty symbols list")
    subsystems = catalog.get("subsystems")
    if not isinstance(subsystems, dict) or not subsystems:
        raise ValueError("Catalog must define subsystems")
    for key, subsystem in subsystems.items():
        if (not re.fullmatch(r"(?:phase[0-9]+|runtime)(?:\.[a-z0-9-]+){2,}", key) or
                not isinstance(subsystem, dict) or
                any(not isinstance(subsystem.get(field), str) or not subsystem[field].strip()
                    for field in ("title", "description"))):
            raise ValueError(f"Invalid subsystem definition: {key}")
    addresses = set()
    for symbol in symbols:
        if not isinstance(symbol, dict):
            raise ValueError("Catalog symbol must be an object")
        address = symbol.get("address")
        if not isinstance(address, str) or not re.fullmatch(r"[0-9a-f]{8}", address):
            raise ValueError(f"Invalid catalog address: {address!r}")
        if address in addresses:
            raise ValueError(f"Duplicate catalog address: {address}")
        addresses.add(address)
        if symbol.get("kind") not in ("function", "data", "code_label"):
            raise ValueError(f"{address}: invalid symbol kind")
        if not isinstance(symbol.get("name"), str) or not re.fullmatch(r"\S+", symbol["name"]):
            raise ValueError(f"{address}: invalid symbol name")
        for field in ("aliases", "notes", "implementation", "tests", "references"):
            values = symbol.get(field)
            if not isinstance(values, list) or any(not isinstance(v, str) or not v.strip() for v in values):
                raise ValueError(f"{address}: {field} must be a list of nonempty strings")
        reviews = symbol.get("review")
        if not isinstance(reviews, list) or any(
                not isinstance(r, dict) or set(r) != {"field", "legacy_value", "reason"} or
                any(not isinstance(v, str) or not v for v in r.values()) for r in reviews):
            raise ValueError(f"{address}: invalid review annotations")
        if symbol["kind"] == "function":
            if not isinstance(symbol.get("status"), str) or symbol["status"] not in FUNCTION_STATUSES:
                raise ValueError(f"{address}: invalid function status")
            if type(symbol.get("instructions")) is not int or symbol["instructions"] < 0:
                raise ValueError(f"{address}: invalid instruction count")
            if not isinstance(symbol.get("roadmap"), str) or not symbol["roadmap"].strip():
                raise ValueError(f"{address}: invalid roadmap")
            if symbol["roadmap"] not in subsystems:
                raise ValueError(f"{address}: unknown subsystem {symbol['roadmap']!r}")
            classification = symbol.get("classification")
            if (not isinstance(classification, dict) or
                    not isinstance(classification.get("confidence"), str) or
                    classification["confidence"] not in CLASSIFICATION_CONFIDENCES or
                    any(not isinstance(classification.get(field), str) or not classification[field].strip()
                        for field in ("rationale", "previous_roadmap"))):
                raise ValueError(f"{address}: invalid classification")
            evidence = classification.get("evidence")
            if (not isinstance(evidence, list) or not evidence or
                    any(not isinstance(item, str) or not item.strip() for item in evidence)):
                raise ValueError(f"{address}: classification requires individual evidence")
        elif any(field in symbol for field in ("status", "instructions", "roadmap", "classification")):
            raise ValueError(f"{address}: data/code labels cannot carry function state")
    if [s["address"] for s in symbols] != sorted(addresses):
        raise ValueError("Catalog symbols must be sorted by address")
    if not any(s["kind"] == "function" for s in symbols):
        raise ValueError("Catalog contains no functions")
    coverage = catalog.get("coverage")
    if not isinstance(coverage, dict) or any(
            type(coverage.get(k)) is not int or coverage[k] < 0 for k in COVERAGE_FIELDS):
        raise ValueError("Invalid unique coverage metrics")
    game = coverage["unique_instructions"] - coverage["library_instructions"]
    if (game < 0 or coverage["ported_instructions"] > game or coverage["partial_instructions"] > game or
            coverage["covered_code_bytes"] > coverage["code_bytes"]):
        raise ValueError("Unique coverage metrics are out of range")
    if verify_snapshot and coverage.get("inventory_sha256") != inventory_fingerprint(catalog):
        raise ValueError("Coverage snapshot is stale; run .venv/bin/python tools/disasm.py catalog --refresh")
    return catalog


def parse_catalog(text, verify_snapshot=True):
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result

    return validate_catalog(json.loads(text, object_pairs_hook=unique_object), verify_snapshot)


def load_catalog(path=CATALOG, verify_snapshot=True):
    return parse_catalog(Path(path).read_text(encoding="utf-8"), verify_snapshot)


def summarize(catalog):
    functions = [s for s in catalog["symbols"] if s["kind"] == "function"]
    game = [s for s in functions if s["status"] != "library"]
    coverage = catalog["coverage"]
    return {
        "functions": len(functions), "game_functions": len(game),
        "statuses": dict(Counter(s["status"] for s in functions)),
        "completed": sum(s["status"] in ("ported", "verified") for s in game),
        "mapped": sum(s["roadmap"] != "unmapped" for s in game),
        "mapped_all": sum(s["roadmap"] != "unmapped" for s in functions),
        "classification": dict(Counter(s["classification"]["confidence"] for s in game)),
        "classification_all": dict(Counter(s["classification"]["confidence"] for s in functions)),
        "subsystems": len(catalog["subsystems"]),
        "library_nonruntime_subsystems": sum(
            s["status"] == "library" and not s["roadmap"].startswith("runtime.") for s in functions),
        "review_functions": sum(bool(s["review"]) for s in functions),
        "game_instructions": coverage["unique_instructions"] - coverage["library_instructions"],
        **coverage,
    }


def sync_readme(catalog, path):
    metrics = summarize(catalog)

    def number(value):
        return f"{value:,}".replace(",", " ")

    def percent(numerator, denominator):
        return f"{100 * numerator / denominator if denominator else 0:.1f}".replace(".", ",")

    m = metrics
    rows = {
        "Funciones candidatas detectadas": f'**{number(m["functions"])}** (bytes de código cubiertos: '
            f'{percent(m["covered_code_bytes"], m["code_bytes"])} %; {m["statuses"].get("library", 0)} clasificadas como runtime)',
        "Instrucciones únicas desensambladas": f'**{number(m["unique_instructions"])}** '
            f'({number(m["library_instructions"])} de runtime/libc/DOS/4G)',
        "Código del juego marcado como portado/verificado": f'**{number(m["ported_instructions"])} de '
            f'{number(m["game_instructions"])} instrucciones ({percent(m["ported_instructions"], m["game_instructions"])} %)**; '
            f'**{m["completed"]} de {m["game_functions"]} funciones ({percent(m["completed"], m["game_functions"])} %)**',
        "Código del juego marcado como parcial": f'**{number(m["partial_instructions"])} instrucciones en '
            f'{m["statuses"].get("partial", 0)} funciones**; excluido del numerador portado',
        "Funciones de juego vinculadas explícitamente a una tarea del roadmap": f'**{m["mapped"]} de '
            f'{m["game_functions"]} ({percent(m["mapped"], m["game_functions"])} %)**; '
            f'{m["classification"].get("documented", 0)} documentadas, '
            f'{m["classification"].get("inferred", 0)} inferidas y '
            f'{m["classification"].get("unresolved", 0)} por resolver; no es cobertura de portado',
    }
    text = Path(path).read_text(encoding="utf-8")
    for label, value in rows.items():
        pattern = rf"^\| {re.escape(label)} \| .* \|$"
        text, count = re.subn(pattern, lambda _: f"| {label} | {value} |", text, flags=re.MULTILINE)
        if count != 1:
            raise ValueError(f"README must have exactly one metric row: {label}")
    for status in STATUS_ORDER:
        pattern = rf"^(\| `{re.escape(status)}` \| )\d+( \|.*)$"
        text, count = re.subn(pattern, lambda match: match[1] + str(m["statuses"].get(status, 0)) + match[2],
                             text, flags=re.MULTILINE)
        if count != 1:
            raise ValueError(f"README must have exactly one status row: {status}")
    Path(path).write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=CATALOG)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check")
    sub.add_parser("summary")
    sub.add_parser("sync-readme")
    query = sub.add_parser("list")
    query.add_argument("--status", choices=STATUS_ORDER)
    query.add_argument("--roadmap", help="roadmap prefix, e.g. phase1")
    query.add_argument("--confidence", choices=CLASSIFICATION_CONFIDENCES)
    sub.add_parser("show").add_argument("address")
    args = parser.parse_args()
    try:
        catalog = load_catalog(args.catalog, verify_snapshot=args.command not in ("list", "show"))
        if args.command == "sync-readme":
            sync_readme(catalog, ROOT / "README.md")
            print("Updated README function summary from docs/functions.json")
        elif args.command in ("check", "summary"):
            print(json.dumps(summarize(catalog), indent=2, sort_keys=True))
        elif args.command == "show":
            address = f"{int(args.address, 16):08x}"
            symbol = next((s for s in catalog["symbols"] if s["address"] == address), None)
            if symbol is None:
                raise ValueError(f"No catalog symbol at {address}")
            print(json.dumps(symbol, indent=2, ensure_ascii=False))
        else:
            for s in catalog["symbols"]:
                if s["kind"] != "function" or args.status and s["status"] != args.status:
                    continue
                if args.roadmap and not s["roadmap"].startswith(args.roadmap):
                    continue
                if args.confidence and s["classification"]["confidence"] != args.confidence:
                    continue
                print(f'{s["address"]}  {s["status"]:12} {s["instructions"]:5}  {s["name"]}  '
                      f'{s["roadmap"]}  [{s["classification"]["confidence"]}]')
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"Function catalog failed: {error}\n")


if __name__ == "__main__":
    main()
