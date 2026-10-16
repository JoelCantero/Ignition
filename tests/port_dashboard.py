#!/usr/bin/env python3
"""Offline dashboard data and generation checks, using only the standard library."""
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("port_dashboard", ROOT / "tools/port_dashboard.py")
dashboard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dashboard)


class DashboardTests(unittest.TestCase):
    def test_published_metrics_and_inventory(self):
        data = dashboard.build_data()
        game = [row for row in data["functions"] if row["status"] != "library"]
        self.assertEqual(data["metrics"]["gameFunctions"], len(game))
        self.assertEqual(len(data["functions"]), 1112)
        self.assertEqual(data["metrics"]["instructions"], 109739)
        self.assertEqual(data["metrics"]["mapped"], len(game))
        self.assertEqual(data["metrics"]["mappedAll"], len(data["functions"]))
        self.assertEqual({phase["number"] for phase in data["phases"]}, set(range(10)))
        # Per-function instruction totals overlap and must not supply unique coverage.
        self.assertGreater(sum(row["instructions"] for row in game), data["metrics"]["instructions"])

    def test_all_individual_records_and_leaf_categories_are_exposed(self):
        data = dashboard.build_data()
        for row in data["functions"]:
            self.assertIn(row["roadmap"], data["subsystems"])
            self.assertTrue(row["classification"]["evidence"])
            for field in ("notes", "implementation", "tests", "references", "review", "aliases"):
                self.assertIsInstance(row[field], list)
        ai = {r["roadmap"] for r in data["functions"] if r["roadmap"].startswith("phase1.ai-route.")}
        self.assertGreaterEqual(len(ai), 6)
        self.assertEqual(sum(data["metrics"]["classificationAll"].values()), 1112)
        self.assertGreater(data["metrics"]["classificationAll"]["unresolved"], 0)

    def test_render_is_self_contained_and_embeds_exact_data(self):
        data = dashboard.build_data()
        html = dashboard.render(data)
        payload = re.search(r'<script id="port-data" type="application/json">(.*?)</script>', html, re.S)[1]
        self.assertEqual(json.loads(payload), data)
        self.assertNotIn("__PORT_DATA__", html)
        self.assertNotRegex(html, r'<(?:script|link|img)\b[^>]*(?:src|href)=["\']https?://')
        self.assertIn('id="search"', html)
        self.assertIn('id="result-count"', html)
        self.assertIn('id="confidence"', html)
        self.assertIn('id="system-search"', html)

    def test_script_closing_text_is_escaped(self):
        html = dashboard.render({"name": "</script><script>alert(1)</script>"})
        self.assertIn(r"\u003c/script>", html)
        self.assertNotIn("</script><script>alert(1)", html)

    def test_published_dashboard_matches_sources(self):
        html = (ROOT / "docs/port-dashboard.html").read_text(encoding="utf-8")
        saved = json.loads(re.search(r'<script id="port-data" type="application/json">(.*?)</script>', html, re.S)[1])
        current = dashboard.build_data()
        saved.pop("generated")
        current.pop("generated")
        self.assertEqual(saved, current, "Regenerate the dashboard after updating its sources")

    def test_stale_catalog_coverage_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "docs").mkdir()
            for name in dashboard.SOURCES:
                text = (ROOT / name).read_text(encoding="utf-8")
                if name.endswith(".json"):
                    catalog = json.loads(text)
                    catalog["symbols"][0]["status"] = "ported"
                    text = json.dumps(catalog)
                (root / name).write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "snapshot is stale"):
                dashboard.build_data(root)


if __name__ == "__main__":
    unittest.main()
