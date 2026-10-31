"""独立评测编码、对齐和结果统计的离线行为测试。"""

import csv
from pathlib import Path
import tempfile
import unittest

from compare_corpus_eval import compare, summarize
from prepare_corpus_eval import aligned_reading, codes, digest


class CorpusInput(unittest.TestCase):
    def setUp(self):
        self.chars = {"你": ["ni"], "好": ["hao"], "吗": ["ma"], "女": ["nv"], "儿": ["er"], "重": ["zhong", "chong"], "庆": ["qing"]}

    def test_numeric_tones_and_punctuation_align_with_chinese(self):
        self.assertEqual(aligned_reading("你好吗？", "ni3hao3 ma5 ?", self.chars), ["ni", "hao", "ma"])

    def test_polyphone_reading_is_taken_from_external_annotation(self):
        self.assertEqual(aligned_reading("重庆", "chong2qing4", self.chars), ["chong", "qing"])
        self.assertIsNone(aligned_reading("重庆", "chong2jing3", self.chars))

    def test_umlaut_numeric_transcription_is_preserved(self):
        self.assertEqual(aligned_reading("女儿", "nu:3 er2", self.chars), ["nv", "er"])

    def test_alignment_rejects_mixed_scripts_missing_syllables_and_unknown_readings(self):
        for text, reading in [("你A好", "ni3 A hao3"), ("你好", "ni3"), ("你好", "li3 hao3"), ("你5好", "ni3 wu3 hao3")]:
            with self.subTest(text=text, reading=reading):
                self.assertIsNone(aligned_reading(text, reading, self.chars))

    def test_three_input_forms_are_generated_without_consulting_candidates(self):
        self.assertEqual(codes(["ni", "hao", "ma"]), {"full": "nihaoma", "initial": "nhm", "mixed": "nihma"})
        self.assertEqual(digest("你好"), digest("你好"))
        self.assertNotEqual(digest("你好"), digest("好吗"))


class CorpusResults(unittest.TestCase):
    def test_missing_rank_is_not_counted_as_top_k(self):
        rows = [{"rank": str(rank), "microseconds": "2000"} for rank in (0, 1, 5, 9)]
        result = summarize(rows)
        self.assertEqual((result["count"], result["top1"], result["top5"], result["top9"]), (4, 1, 2, 3))
        self.assertEqual(result["input_ms_p50"], 2.0)

    def write(self, path, rows):
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\t")
            writer.writerow(["id", "kind", "code", "expected", "first", "rank", "microseconds", "candidates"])
            writer.writerows(rows)

    def test_comparison_records_improvements_and_regressions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = [["1", "word-full", "nihao", "你好", "拟好", "2", "1000", "拟好|你好"], ["2", "word-full", "haoma", "好吗", "好吗", "1", "3000", "好吗"]]
            new = [["1", "word-full", "nihao", "你好", "你好", "1", "2000", "你好"], ["2", "word-full", "haoma", "好吗", "号码", "2", "4000", "号码|好吗"]]
            self.write(root / "before.tsv", old)
            self.write(root / "after.tsv", new)
            result = compare(root / "before.tsv", root / "after.tsv", root / "report.md")
            self.assertEqual((result["top1_improved"], result["top1_regressed"]), (1, 1))
            self.assertTrue((root / "report.json").is_file())

    def test_comparison_rejects_changed_sample_or_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = ["1", "word-full", "nihao", "你好", "你好", "1", "1000", "你好"]
            self.write(root / "before.tsv", [row])
            self.write(root / "after.tsv", [["2", *row[1:]]])
            with self.assertRaisesRegex(ValueError, "样本或顺序"):
                compare(root / "before.tsv", root / "after.tsv", root / "report.md")
            self.assertFalse((root / "report.md").exists())


if __name__ == "__main__":
    unittest.main()
