#!/usr/bin/env python3
"""Catalog integrity and offline maintenance, without game assets or Capstone."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import function_catalog as catalog_tools


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.catalog = catalog_tools.load_catalog()

    def test_published_inventory_and_unique_metrics(self):
        metrics = catalog_tools.summarize(self.catalog)
        functions = [s for s in self.catalog["symbols"] if s["kind"] == "function"]
        self.assertEqual(metrics["functions"], 1112)
        self.assertEqual(len(self.catalog["symbols"]) - len(functions), 62)
        self.assertEqual(sum(metrics["statuses"].values()), metrics["functions"])
        self.assertEqual(metrics["game_functions"], metrics["functions"] - metrics["statuses"]["library"])
        self.assertEqual(metrics["game_instructions"], 109799)
        self.assertGreater(sum(s["instructions"] for s in functions), metrics["unique_instructions"])
        self.assertEqual(metrics["inventory_sha256"], catalog_tools.inventory_fingerprint(self.catalog))

    def test_legacy_duplicate_notes_and_conflicts_survive(self):
        frame = next(s for s in self.catalog["symbols"] if s["address"] == "0002b734")
        self.assertTrue(any("signed wrapping" in note for note in frame["notes"]))
        self.assertIn("ms * 0.036", frame["notes"])
        self.assertTrue(any(r["field"] == "notes" for r in frame["review"]))
        self.assertTrue(any(s["kind"] == "data" and s["name"] == "g_level_pos" for s in self.catalog["symbols"]))
        metrics = catalog_tools.summarize(self.catalog)
        self.assertEqual(metrics["review_functions"],
                         sum(bool(s["review"]) for s in self.catalog["symbols"] if s["kind"] == "function"))

    def test_runtime_native_batch_has_individual_evidence_and_preserves_pending_work(self):
        runtime = [s for s in self.catalog["symbols"] if s.get("roadmap", "").startswith("runtime.")]
        self.assertEqual(len(runtime), 306)
        resolved = [s for s in runtime if s["status"] in ("native", "verified")]
        self.assertEqual(sum(s["status"] == "native" for s in resolved), 30)
        self.assertEqual(sum(s["status"] == "verified" for s in resolved), 8)
        for symbol in resolved:
            self.assertTrue(symbol["implementation"], symbol["address"])
            self.assertIn("tests/runtime.c", symbol["tests"])
            self.assertIn("docs/formats.md#biblioteca-c-y-dos4g-equivalentes-nativos", symbol["references"])
            self.assertEqual(symbol["classification"]["confidence"], "documented")
        for address in ("00065330", "00065367", "000700d0", "0007011a"):
            symbol = next(s for s in runtime if s["address"] == address)
            self.assertEqual(symbol["status"], "verified")
            self.assertIn("src/platform/runtime.c", symbol["implementation"])
            self.assertIn("tests/runtime_original.py", symbol["tests"])

    def test_documented_reference_paths_exist(self):
        for symbol in self.catalog["symbols"]:
            for field in ("implementation", "tests", "references"):
                for path in symbol[field]:
                    self.assertTrue((ROOT / path.partition("#")[0]).is_file(), (symbol["address"], field, path))

    def test_duplicate_addresses_and_json_keys_are_rejected(self):
        self.catalog["symbols"].append(copy.deepcopy(self.catalog["symbols"][0]))
        with self.assertRaisesRegex(ValueError, "Duplicate catalog address"):
            catalog_tools.validate_catalog(self.catalog)
        with self.assertRaisesRegex(ValueError, "Duplicate JSON key"):
            catalog_tools.parse_catalog('{"schema_version":1,"schema_version":1}')

    def test_invalid_status_and_boolean_counts_are_rejected(self):
        for changes in ({"status": "done"}, {"status": []}, {"instructions": True}, {"instructions": -1}):
            with self.subTest(changes=changes):
                data = copy.deepcopy(self.catalog)
                data["symbols"][0].update(changes)
                with self.assertRaises(ValueError):
                    catalog_tools.validate_catalog(data)

    def test_invalid_metadata_lists_are_rejected(self):
        self.catalog["symbols"][0]["tests"] = [43]
        with self.assertRaisesRegex(ValueError, "tests must be a list"):
            catalog_tools.validate_catalog(self.catalog)

    def test_data_cannot_silently_become_a_counted_function(self):
        data = next(s for s in self.catalog["symbols"] if s["kind"] == "data")
        data["status"] = "ported"
        with self.assertRaisesRegex(ValueError, "cannot carry function state"):
            catalog_tools.validate_catalog(self.catalog)

    def test_status_change_requires_refresh_but_is_not_overwritten(self):
        self.catalog["symbols"][0]["status"] = "ported"
        before = copy.deepcopy(self.catalog)
        with self.assertRaisesRegex(ValueError, "snapshot is stale"):
            catalog_tools.validate_catalog(self.catalog)
        catalog_tools.validate_catalog(self.catalog, verify_snapshot=False)
        self.assertEqual(self.catalog, before)

    def test_notes_names_and_tasks_do_not_invalidate_instruction_snapshot(self):
        first = self.catalog["symbols"][0]
        first["notes"].append("Additional analysis without changing the port state.")
        first["name"] = "renamed_main"
        first["roadmap"] = "phase4.loop.lifecycle"
        catalog_tools.validate_catalog(self.catalog)

    def test_every_function_has_a_specific_area_and_evidence(self):
        functions = [s for s in self.catalog["symbols"] if s["kind"] == "function"]
        self.assertEqual(len(functions), 1112)
        self.assertEqual({s["roadmap"] for s in functions}, set(self.catalog["subsystems"]))
        for symbol in functions:
            self.assertNotEqual(symbol["roadmap"], "unmapped")
            self.assertGreaterEqual(symbol["roadmap"].count("."), 2)
            self.assertTrue(symbol["classification"]["evidence"])
            self.assertTrue(symbol["classification"]["previous_roadmap"])
        metrics = catalog_tools.summarize(self.catalog)
        self.assertEqual(metrics["mapped"], metrics["game_functions"])
        self.assertEqual(metrics["mapped_all"], metrics["functions"])
        self.assertEqual(sum(metrics["classification"].values()), metrics["game_functions"])
        self.assertEqual(sum(metrics["classification_all"].values()), metrics["functions"])
        self.assertGreater(metrics["classification_all"]["unresolved"], 0)

    def test_classification_cannot_be_missing_empty_or_unregistered(self):
        for mutation in ("missing", "evidence", "confidence", "subsystem", "coarse"):
            with self.subTest(mutation=mutation):
                data = copy.deepcopy(self.catalog)
                symbol = data["symbols"][0]
                if mutation == "missing":
                    del symbol["classification"]
                elif mutation == "evidence":
                    symbol["classification"]["evidence"] = []
                elif mutation == "confidence":
                    symbol["classification"]["confidence"] = "certain"
                elif mutation == "subsystem":
                    symbol["roadmap"] = "phase1.not-a-real-subsystem"
                else:
                    data["subsystems"]["phase1.coarse"] = {"title": "Coarse", "description": "Not a leaf"}
                with self.assertRaises(ValueError):
                    catalog_tools.validate_catalog(data)

    def test_area_confidence_does_not_promote_port_state_or_unique_coverage(self):
        symbol = self.catalog["symbols"][0]
        before = (symbol["status"], copy.deepcopy(self.catalog["coverage"]))
        symbol["classification"]["confidence"] = "unresolved"
        symbol["classification"]["rationale"] = "Provisional area; no semantic proof."
        catalog_tools.validate_catalog(self.catalog)
        self.assertEqual((symbol["status"], self.catalog["coverage"]), before)

    def test_suspect_candidate_and_library_heuristic_are_explicit(self):
        suspect = next(s for s in self.catalog["symbols"] if s["address"] == "000541c0")
        self.assertEqual(suspect["status"], "unknown")
        self.assertEqual(suspect["classification"]["confidence"], "unresolved")
        renderer = next(s for s in self.catalog["symbols"] if s["address"] == "00075090")
        self.assertEqual(renderer["status"], "library")
        self.assertEqual(renderer["roadmap"], "phase3.raster.perspective")
        self.assertGreater(catalog_tools.summarize(self.catalog)["library_nonruntime_subsystems"], 0)

    def test_out_of_range_coverage_is_rejected(self):
        self.catalog["coverage"]["ported_instructions"] = self.catalog["coverage"]["unique_instructions"] + 1
        with self.assertRaisesRegex(ValueError, "out of range"):
            catalog_tools.validate_catalog(self.catalog)

    def test_unsorted_records_are_rejected(self):
        self.catalog["symbols"][:2] = reversed(self.catalog["symbols"][:2])
        with self.assertRaisesRegex(ValueError, "sorted by address"):
            catalog_tools.validate_catalog(self.catalog)

    def test_readme_summary_update_is_idempotent_and_preserves_other_text(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "README.md"
            original = (ROOT / "README.md").read_text()
            path.write_text(original + "\nUnrelated documentation stays here.\n")
            catalog_tools.sync_readme(self.catalog, path)
            updated = path.read_text()
            self.assertIn("Unrelated documentation stays here.", updated)
            self.assertIn("## Metodología", updated)
            catalog_tools.sync_readme(self.catalog, path)
            self.assertEqual(path.read_text(), updated)
            metrics = catalog_tools.summarize(self.catalog)
            self.assertIn(f'| `verified` | {metrics["statuses"]["verified"]} |', updated)

    def test_invalid_readme_is_not_partially_written(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "README.md"
            path.write_text("Not a README metric table\n")
            with self.assertRaisesRegex(ValueError, "exactly one metric row"):
                catalog_tools.sync_readme(self.catalog, path)
            self.assertEqual(path.read_text(), "Not a README metric table\n")


def function(catalog, address):
    return next(s for s in catalog["symbols"] if s["address"] == address)


class MilestonePlanTests(unittest.TestCase):
    def setUp(self):
        self.catalog = catalog_tools.load_catalog()
        self.report = catalog_tools.check_plan(self.catalog)
        self.info = self.report["functions"]

    def test_published_plan_is_consistent_and_covers_every_function(self):
        self.assertEqual(list(self.catalog["milestones"]), [f"H{i}" for i in range(7)])
        self.assertEqual(self.catalog["executable"]["entry"], "00055fbc")
        new = [a for m in self.report["milestones"].values() for a in m["new"]]
        self.assertEqual(len(new), len(set(new)))
        self.assertEqual(len(new) + len(self.report["unplanned"]) + len(self.report["unreferenced"]),
                         len(self.info))
        for address in self.report["unplanned"]:
            # Live code outside every milestone may only be infrastructure covered by a native caller.
            self.assertIn(self.info[address]["treatment"], ("native", "not-needed"), address)
        for milestone in self.report["milestones"].values():
            self.assertFalse(milestone["unused_deferrals"])

    def test_reviewed_dependencies_are_planned_where_the_simulation_needs_them(self):
        for address, milestone in (("0004c2d8", "H2"), ("0004f874", "H2"), ("00056d24", "H2"),
                                   ("0002e6f8", "H2"), ("000564d8", "H3"), ("0003fd04", "H3"),
                                   ("00051204", "H4")):
            self.assertEqual(self.info[address]["milestone"], milestone, address)
            self.assertTrue(function(self.catalog, address)["plan"]["rationale"].strip())
        self.assertEqual(self.info["00056d24"]["treatment"], "equivalent")
        self.assertEqual(self.info["000541c0"]["treatment"], "not-needed")

    def test_callees_precede_callers_within_a_milestone(self):
        order = {a: i for i, a in enumerate(self.report["milestones"]["H2"]["new"])}
        self.assertLess(order["0004db90"], order["0004d5f4"])  # car_throttle before car_input
        self.assertLess(order["0004d5f4"], order["0002ed00"])  # car_input before car_update
        self.assertLess(order["0002ed00"], order["0002d008"])  # car_update before cars_update_all

    def test_unreferenced_code_is_not_needed_and_native_state_implies_native_treatment(self):
        for address in ("00014000", "00019ac8", "00010e04"):
            self.assertIn(address, self.report["unreferenced"])
            self.assertEqual(self.info[address]["treatment"], "not-needed")
            self.assertTrue(self.info[address]["done"])
        self.assertEqual(self.info["0001034c"]["treatment"], "native")
        self.assertTrue(self.info["0001034c"]["done"])

    def test_plan_never_changes_published_state_or_coverage(self):
        before = copy.deepcopy(self.catalog)
        catalog_tools.plan_milestones(self.catalog)
        self.assertEqual(self.catalog, before)

    def test_dependency_inversion_requires_review(self):
        self.catalog["subsystems"]["phase1.controls.throttle"]["milestone"] = "H5"
        with self.assertRaisesRegex(ValueError, "0004db90 car_throttle: homed in H5 but required by H2"):
            catalog_tools.check_plan(self.catalog)

    def test_deferral_removes_a_dependency_and_invalid_deferrals_fail(self):
        h2 = self.catalog["milestones"]["H2"]
        self.assertIn("00058c1c", self.report["milestones"]["H2"]["new"])
        h2["deferred"].append({"target": "00058c1c", "reason": "test"})
        report = catalog_tools.plan_milestones(self.catalog)
        self.assertNotIn("00058c1c", report["milestones"]["H2"]["new"])
        self.assertFalse(report["errors"])
        for target, message in (("00014000", "deferral 00014000 is never reached"),
                                ("phase1.controls.throttle", "belongs to the milestone itself")):
            with self.subTest(target=target):
                data = copy.deepcopy(self.catalog)
                data["milestones"]["H2"]["deferred"].append({"target": target, "reason": "test"})
                with self.assertRaisesRegex(ValueError, message):
                    catalog_tools.check_plan(data)

    def test_pins_must_match_the_first_requiring_milestone(self):
        function(self.catalog, "0004c2d8")["plan"]["milestone"] = "H4"
        with self.assertRaisesRegex(ValueError, "0004c2d8: pinned to H4 but first required by H2"):
            catalog_tools.check_plan(self.catalog)
        catalog = catalog_tools.load_catalog()
        self.assertTrue(self.info["00058c1c"]["done"])
        function(catalog, "00058c1c")["plan"] = {"milestone": "H3", "rationale": "test"}
        with self.assertRaisesRegex(ValueError, "00058c1c: pinned to H3 but first required by H2"):
            catalog_tools.check_plan(catalog)
        catalog = catalog_tools.load_catalog()
        function(catalog, "00014000")["plan"] = {"treatment": "port", "rationale": "test"}
        with self.assertRaisesRegex(ValueError, "without static references"):
            catalog_tools.check_plan(catalog)

    def test_invalid_plan_schema_is_rejected(self):
        mutations = (
            lambda c: function(c, "0004c2d8")["plan"].update(treatment="rewrite"),
            lambda c: function(c, "0004c2d8")["plan"].pop("rationale"),
            lambda c: c["subsystems"]["phase1.controls.throttle"].update(treatment="maybe"),
            lambda c: c["subsystems"]["phase1.controls.throttle"].update(milestone="H9"),
            lambda c: c["milestones"]["H2"]["deferred"].append({"target": "nowhere", "reason": "x"}),
            lambda c: c["milestones"]["H2"]["depends_on"].append("H3"),
            lambda c: function(c, "0004c2d8")["calls"].append("deadbeef"),
            lambda c: function(c, "0004c2d8").update(indirect_root="yes"),
            lambda c: next(s for s in c["symbols"] if s["kind"] == "data").update(calls=[]),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                data = copy.deepcopy(self.catalog)
                mutate(data)
                with self.assertRaises(ValueError):
                    catalog_tools.validate_catalog(data)

    def test_milestone_status_is_derived_from_checklist_functions_and_dependencies(self):
        rows = {r["id"]: r for r in catalog_tools.milestone_rows(self.catalog, self.report)}
        self.assertEqual(rows["H0"]["status"], "closed")
        self.assertEqual(rows["H1"]["status"], "open")
        self.assertEqual(rows["H2"]["status"], "in-progress")  # functions done, closure criteria not met
        h1 = self.catalog["milestones"]["H1"]
        for item in h1["checklist"]:
            item.update(done=True, evidence=["test"])
        self.assertEqual({r["id"]: r for r in catalog_tools.milestone_rows(self.catalog)}["H1"]["status"], "closed")
        self.catalog["milestones"]["H0"]["checklist"][0]["done"] = False
        rows = {r["id"]: r for r in catalog_tools.milestone_rows(self.catalog)}
        self.assertEqual((rows["H0"]["status"], rows["H1"]["status"]), ("in-progress", "in-progress"))

    def test_done_checklist_items_require_evidence(self):
        for mutate in (lambda item: item.update(evidence=[]), lambda item: item.update(done="yes"),
                       lambda item: item.pop("evidence")):
            data = copy.deepcopy(self.catalog)
            mutate(data["milestones"]["H0"]["checklist"][0])
            with self.assertRaisesRegex(ValueError, "checklist"):
                catalog_tools.validate_catalog(data)

    def test_readme_milestone_table_matches_the_catalog(self):
        text = (ROOT / "README.md").read_text(encoding="utf-8")
        table = catalog_tools.readme_milestone_table(self.catalog, self.report)
        self.assertIn(f"{catalog_tools.MILESTONE_BEGIN}\n{table}\n{catalog_tools.MILESTONE_END}", text,
                      "Run python3 tools/function_catalog.py sync-readme")


if __name__ == "__main__":
    unittest.main()
