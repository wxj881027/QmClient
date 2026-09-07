from pathlib import Path
import unittest

from collect_qm_coverage import merge_document, summarize


class CoverageSummaryTest(unittest.TestCase):
    def test_line_union_and_branch_instances_are_distinct(self):
        root = Path(__file__).resolve().parents[1]
        source = root / "src/game/client/ui/card_ui_model.cpp"
        document = {
            "files": [{"file": str(source), "lines": [
                {"line_number": 10, "count": 1, "function_name": "f", "branches": [{"count": 1}, {"count": 0}]},
                {"line_number": 11, "count": 0, "branches": []},
            ]}]
        }
        files = {}
        merge_document(document, "object-one", files, root)
        document["files"][0]["lines"][0]["count"] = 0
        document["files"][0]["lines"][1]["count"] = 1
        merge_document(document, "object-two", files, root)
        per_file, totals = summarize(files)
        self.assertEqual(totals["lines"], 2)
        self.assertEqual(totals["lines_covered"], 2)
        self.assertEqual(totals["branches"], 4)
        self.assertEqual(totals["branches_covered"], 2)
        self.assertEqual(totals["branches_percent"], 50)
        self.assertEqual(per_file["src/game/client/ui/card_ui_model.cpp"]["uncovered_lines"], [])

    def test_ignores_tests_and_upstream_files(self):
        root = Path(__file__).resolve().parents[1]
        files = {}
        merge_document({"files": [
            {"file": str(root / "src/test/qmclient_ui_foundation_test.cpp"), "lines": []},
            {"file": str(root / "src/game/client/gameclient.cpp"), "lines": []},
        ]}, "object", files, root)
        self.assertEqual(files, {})
        _, totals = summarize(files)
        self.assertIsNone(totals["lines_percent"])
        self.assertIsNone(totals["branches_percent"])


if __name__ == "__main__":
    unittest.main()
