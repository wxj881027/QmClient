from __future__ import annotations

import unittest

from qmclient_scripts.languages_qmclient.chinese_text_style import normalize_simplified_chinese_text


class ChineseTextStyleTest(unittest.TestCase):
    def test_preserves_paired_title_punctuation(self) -> None:
        self.assertEqual(normalize_simplified_chinese_text("?!卡键!?"), "?!卡键!?")

    def test_normalizes_text_inside_title_punctuation(self) -> None:
        self.assertEqual(normalize_simplified_chinese_text("?!卡键HUD!?"), "?!卡键 HUD!?")

    def test_normalizes_unpaired_sentence_punctuation(self) -> None:
        self.assertEqual(normalize_simplified_chinese_text("卡键!"), "卡键！")
        self.assertEqual(normalize_simplified_chinese_text("卡键?"), "卡键？")


if __name__ == "__main__":
    unittest.main()
