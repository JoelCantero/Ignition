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
        self.assertEqual(metrics["game_instructions"], 108665)
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


if __name__ == "__main__":
    unittest.main()
