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
# How each original function is satisfied, and which published states count as satisfying it.
TREATMENTS = {
    "port": frozenset({"ported", "verified"}),
    "equivalent": frozenset({"ported", "verified", "native"}),
    "native": frozenset({"native", "verified", "ported"}),
    "not-needed": frozenset(FUNCTION_STATUSES),
}
# Treatments whose callees are not required: the replacement or omission covers the whole subtree.
LEAF_TREATMENTS = frozenset({"native", "not-needed"})


def _nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def _is_address(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{8}", value) is not None


def inventory_fingerprint(catalog):
    rows = [(s["address"], s["status"], s["instructions"]) for s in catalog["symbols"]
            if s["kind"] == "function"]
    return hashlib.sha256(json.dumps(sorted(rows), separators=(",", ":")).encode()).hexdigest()


def validate_catalog(catalog, verify_snapshot=True):
    if (not isinstance(catalog, dict) or type(catalog.get("schema_version")) is not int or
            catalog["schema_version"] != 3):
        raise ValueError("Unsupported function catalog schema_version")
    executable = catalog.get("executable", {})
    if (not isinstance(executable, dict) or not isinstance(executable.get("sha256"), str) or
            not re.fullmatch(r"[0-9a-f]{64}", executable["sha256"])):
        raise ValueError("Invalid catalog executable SHA-256")
    symbols = catalog.get("symbols")
    if not isinstance(symbols, list) or not symbols:
        raise ValueError("Catalog must contain a nonempty symbols list")
    milestones = catalog.get("milestones")
    if not isinstance(milestones, dict) or not milestones:
        raise ValueError("Catalog must define milestones")
    for index, (key, milestone) in enumerate(milestones.items()):
        if (key != f"H{index}" or not isinstance(milestone, dict) or
                set(milestone) != {"title", "goal", "criteria", "depends_on", "entry_points", "deferred"} or
                any(not _nonempty(milestone[field]) for field in ("title", "goal", "criteria"))):
            raise ValueError(f"Invalid milestone definition: {key}")
        earlier = list(milestones)[:index]
        if (not isinstance(milestone["depends_on"], list) or
                any(d not in earlier for d in milestone["depends_on"]) or
                not isinstance(milestone["entry_points"], list) or
                any(not _is_address(a) for a in milestone["entry_points"]) or
                not isinstance(milestone["deferred"], list) or
                any(not isinstance(d, dict) or set(d) != {"target", "reason"} or
                    not _nonempty(d["target"]) or not _nonempty(d["reason"]) for d in milestone["deferred"])):
            raise ValueError(f"Invalid milestone dependencies, entry points or deferrals: {key}")
    subsystems = catalog.get("subsystems")
    if not isinstance(subsystems, dict) or not subsystems:
        raise ValueError("Catalog must define subsystems")
    for key, subsystem in subsystems.items():
        if (not re.fullmatch(r"(?:phase[0-9]+|runtime)(?:\.[a-z0-9-]+){2,}", key) or
                not isinstance(subsystem, dict) or
                any(not _nonempty(subsystem.get(field)) for field in ("title", "description")) or
                subsystem.get("treatment") not in TREATMENTS or
                "milestone" not in subsystem or
                subsystem["milestone"] is not None and subsystem["milestone"] not in milestones):
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
            for field in ("calls", "pointer_refs"):
                edges = symbol.get(field)
                if (not isinstance(edges, list) or any(not _is_address(e) or e == address for e in edges) or
                        edges != sorted(set(edges))):
                    raise ValueError(f"{address}: {field} must be a sorted list of other function addresses")
            if type(symbol.get("indirect_root")) is not bool:
                raise ValueError(f"{address}: indirect_root must be a boolean")
            plan = symbol.get("plan")
            if plan is not None and (
                    not isinstance(plan, dict) or not set(plan) <= {"treatment", "milestone", "rationale"} or
                    not _nonempty(plan.get("rationale")) or not {"treatment", "milestone"} & set(plan) or
                    "treatment" in plan and plan["treatment"] not in TREATMENTS or
                    "milestone" in plan and plan["milestone"] not in milestones):
                raise ValueError(f"{address}: invalid plan override")
        elif any(field in symbol for field in ("status", "instructions", "roadmap", "classification",
                                               "calls", "pointer_refs", "indirect_root", "plan")):
            raise ValueError(f"{address}: data/code labels cannot carry function state")
    if [s["address"] for s in symbols] != sorted(addresses):
        raise ValueError("Catalog symbols must be sorted by address")
    if not any(s["kind"] == "function" for s in symbols):
        raise ValueError("Catalog contains no functions")
    functions = {s["address"] for s in symbols if s["kind"] == "function"}
    if executable.get("entry") not in functions:
        raise ValueError("Catalog executable entry must be a catalog function")
    for symbol in symbols:
        if symbol["kind"] == "function" and any(
                e not in functions for e in symbol["calls"] + symbol["pointer_refs"]):
            raise ValueError(f'{symbol["address"]}: call graph references an unknown function')
    for key, milestone in milestones.items():
        if any(a not in functions for a in milestone["entry_points"]):
            raise ValueError(f"{key}: entry point is not a catalog function")
        if any(d["target"] not in functions and d["target"] not in subsystems for d in milestone["deferred"]):
            raise ValueError(f"{key}: deferral target must be a function address or subsystem")
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


def plan_milestones(catalog):
    """Assign every function to the earliest milestone that statically requires it.

    A milestone requires its entry points, the functions of subsystems homed in it and, transitively, their
    direct callees and pointer references. Functions treated as `native`/`not-needed` are required but not
    expanded; deferred targets are not followed for that milestone. Functions not reachable from the executable
    entry or an indirect root have no static reference at all and are `not-needed`; live functions that no
    milestone reaches are reported as unplanned (indirect calls are not resolved).
    """
    functions = {s["address"]: s for s in catalog["symbols"] if s["kind"] == "function"}
    subsystems, milestones = catalog["subsystems"], catalog["milestones"]
    order = {key: index for index, key in enumerate(milestones)}
    live = set()
    stack = [catalog["executable"]["entry"]] + [a for a, s in functions.items() if s["indirect_root"]]
    while stack:
        address = stack.pop()
        if address not in live:
            live.add(address)
            stack.extend(functions[address]["calls"] + functions[address]["pointer_refs"])
    info = {}
    for address, s in functions.items():
        plan = s.get("plan", {})
        unreferenced = address not in live
        # A published native state already records a native replacement; explicit plans still take precedence.
        default = "native" if s["status"] == "native" else subsystems[s["roadmap"]]["treatment"]
        treatment = "not-needed" if unreferenced else plan.get("treatment", default)
        info[address] = {
            "treatment": treatment, "home": plan.get("milestone", subsystems[s["roadmap"]]["milestone"]),
            "pinned": "milestone" in plan, "done": s["status"] in TREATMENTS[treatment],
            "unreferenced": unreferenced, "milestone": None, "required_by": [],
        }
    report = {"milestones": {}, "errors": [], "unplanned": [], "unreferenced": sorted(set(functions) - live)}
    for address in report["unreferenced"]:
        if "plan" in functions[address]:
            report["errors"].append(f"{address}: plan override on a function without static references")
    for key, milestone in milestones.items():
        deferred = {d["target"] for d in milestone["deferred"]}
        used = set()
        if any(a not in live for a in milestone["entry_points"]):
            report["errors"].append(f"{key}: entry point without static references")
        roots = sorted(set(milestone["entry_points"]) |
                       {a for a, i in info.items() if i["home"] == key and not i["unreferenced"]})
        visited, postorder = set(), []
        for root in roots:
            stack = [(root, False)]
            while stack:
                address, expanded = stack.pop()
                if expanded:
                    postorder.append(address)
                    continue
                if address in visited:
                    continue
                if address != root and (address in deferred or functions[address]["roadmap"] in deferred):
                    used.add(address if address in deferred else functions[address]["roadmap"])
                    continue
                visited.add(address)
                stack.append((address, True))
                if info[address]["treatment"] not in LEAF_TREATMENTS:
                    s = functions[address]
                    stack.extend((e, False) for e in reversed(s["calls"] + s["pointer_refs"]) if e not in visited)
        for address in visited:
            info[address]["required_by"].append(key)
            if info[address]["milestone"] is None:
                info[address]["milestone"] = key
        new = [a for a in postorder if info[a]["milestone"] == key]
        report["milestones"][key] = {
            "required": len(visited), "new": new,
            "pending": [a for a in new if not info[a]["done"]],
            "unused_deferrals": sorted(deferred - used),
        }
        for target in sorted(deferred - used):
            report["errors"].append(f"{key}: deferral {target} is never reached")
    for address, i in sorted(info.items()):
        if i["unreferenced"]:
            continue
        if i["milestone"] is None:
            report["unplanned"].append(address)
            if i["pinned"]:
                report["errors"].append(f"{address}: pinned to {i['home']} but not required by it")
        elif i["home"] is not None and order[i["milestone"]] < order[i["home"]] and not i["done"]:
            report["errors"].append(
                f"{address} {functions[address]['name']}: homed in {i['home']} but required by {i['milestone']}; "
                "pin it with a reviewed plan or defer the dependency")
        elif i["pinned"] and i["milestone"] != i["home"]:
            report["errors"].append(f"{address}: pinned to {i['home']} but first required by {i['milestone']}")
    report["functions"] = info
    return report


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
