"""词典生成器的离线行为测试，不访问互联网或真实用户目录。"""

import tempfile
import unittest
from pathlib import Path

from build_rime_dict import SOURCES, make_corpus_entries, make_entries, make_english_entries, load_curated_phrases, load_curated_readings, normalize_syllable, parse_chars, parse_phrases, read_sources, render_game_aliases, render_licenses


class BuildRimeDictionary(unittest.TestCase):
    def test_tones_are_removed_without_losing_umlaut(self):
        self.assertEqual(normalize_syllable("nǚ"), "nv")
        self.assertEqual(normalize_syllable("lüè"), "lve")
        self.assertEqual(normalize_syllable("SHÀNG"), "shang")

    def test_all_character_readings_are_preserved_and_deduplicated(self):
        self.assertEqual(parse_chars("U+91CD: zhòng,chóng,zhǒng # 重"), {"重": ["zhong", "chong"]})

    def test_phrase_reading_overrides_character_primary(self):
        chars = {"重": ["zhong", "chong"], "庆": ["qing"]}
        phrases = parse_phrases("重庆: chóng qìng")
        self.assertEqual(make_entries("重庆 100 ns", chars, phrases), [("重庆", "chong qing", 100)])

    def test_missing_phrase_uses_primary_character_readings(self):
        self.assertEqual(make_entries("你好 10 x", {"你": ["ni"], "好": ["hao"]}, {}), [("你好", "ni hao", 10)])

    def test_filtering_and_frequency_limit_select_usable_words(self):
        chars = {"你": ["ni"], "好": ["hao"]}
        source = "你好 100 n\n你 200 n\n好 1 n\nA你 500 n\n未知 500 n"
        self.assertEqual(make_entries(source, chars, {}, max_entries=1), [("你", "ni", 200)])

    def test_equal_frequencies_have_deterministic_order(self):
        chars = {"你": ["ni"], "好": ["hao"]}
        first = make_entries("好 10 n\n你 10 n", chars, {})
        second = make_entries("你 10 n\n好 10 n", chars, {})
        self.assertEqual(first, second)

    def test_offline_cache_corruption_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            (cache / SOURCES[0][0]).write_bytes(b"corrupted")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                read_sources(cache, False)

    def test_generated_notices_keep_unicode_and_initial_mit_attribution(self):
        texts = {name: f"Full notice for {name}" for name in
                 ("wanxiang-LICENSE", "wanxiang-LICENSE-SCOPE.md", "wanxiang-NOTICE.md", "jieba-LICENSE", "phrases-LICENSE", "chars-LICENSE", "unicode-LICENSE", "python-pinyin-LICENSE", "rime-LICENSE", "english-12dicts-README")}
        texts["unicode-LICENSE"] = "UNICODE LICENSE V3\nFull Unicode permission"
        texts["python-pinyin-LICENSE"] = "Original MIT copyright: mozillazg, Hotoo"
        notices = render_licenses(texts)
        self.assertIn(texts["unicode-LICENSE"], notices)
        self.assertIn(texts["python-pinyin-LICENSE"], notices)
        self.assertIn("https://www.unicode.org/license.txt", notices)
        self.assertIn("tone removal", notices)
        self.assertIn("Wanxiang Pinyin by amzxyz, CC BY 4.0", notices)
        self.assertIn(texts["wanxiang-LICENSE-SCOPE.md"], notices)

    def test_corpus_retains_reading_frequencies_instead_of_character_primary(self):
        rows, _ = make_corpus_entries(["重庆\tchóng qìng\t1000\n重\tzhòng\t900\n重\tchóng\t50"], [], [])
        self.assertEqual(rows, [("重庆", "chong qing", 1000), ("重", "zhong", 900), ("重", "chong", 50)])

    def test_corpus_duplicate_rows_and_tones_do_not_inflate_weights(self):
        rows, _ = make_corpus_entries(["啊\tā\t100\n啊\tá\t50\n", "啊\tā\t100"], [], [])
        self.assertEqual(rows, [("啊", "a", 100)])

    def test_corpus_filters_bad_weights_non_chinese_and_misaligned_reading(self):
        rows, statistics = make_corpus_entries(["你好\tnǐ hǎo\t300\n中文A\tzhong wen a\t1000\n你好\tni\t5000\n世界\tshì jiè\t12N"], [], [])
        self.assertEqual(rows, [("你好", "ni hao", 300)])
        self.assertEqual(statistics["excluded_text"], 1)
        self.assertEqual(statistics["excluded_reading"], 1)
        self.assertEqual(statistics["excluded_weight"], 1)

    def test_corpus_only_backfills_missing_words_at_low_frequency(self):
        rows, _ = make_corpus_entries(["重要\tzhòng yào\t100"], [("重要", "chong yao", 9000), ("少见", "shao jian", 500)], [])
        self.assertEqual(rows, [("重要", "zhong yao", 100), ("少见", "shao jian", 10)])

    def test_priority_uses_corpus_reading_unless_user_supplies_an_explicit_one(self):
        source = ["重庆\tchóng qìng\t100"]
        rows, _ = make_corpus_entries(source, [], [("重庆", "zhong qing", 300)])
        self.assertEqual(rows, [("重庆", "chong qing", 301)])
        rows, _ = make_corpus_entries(source, [], [("重庆", "zhong qing", 300)], {"重庆": "zhong qing"})
        self.assertIn(("重庆", "zhong qing", 300), rows)

    def test_equal_manual_priorities_keep_the_corpus_frequency_order(self):
        source = "钩子\tgōu zi\t1800\n勾子\tgōu zi\t100"
        rows, _ = make_corpus_entries([source], [], [("钩子", "gou zi", 7000), ("勾子", "gou zi", 7000)])
        self.assertEqual([word for word, _, _ in rows], ["钩子", "勾子"])
        self.assertGreater(rows[0][2], rows[1][2])

    def test_complete_code_aliases_accept_chinese_without_changing_main_frequencies(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "aliases.tsv"
            source.write_text("hj\tHJ\nzg\t中国\n", encoding="utf-8")
            result = render_game_aliases(source)
            self.assertIn("HJ\thj\t100\n", result)
            self.assertIn("中国\tzg\t100\n", result)
            source.write_text("zg\t中国 word\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                render_game_aliases(source)

    def test_corpus_order_is_independent_of_file_and_row_order(self):
        first = "好\thǎo\t50\n你\tnǐ\t50"
        second = "你好\tnǐ hǎo\t100"
        self.assertEqual(make_corpus_entries([first, second], [], [])[0], make_corpus_entries([second, first], [], [])[0])

    def test_corpus_expands_syllabic_nasals_and_preserves_long_chat_phrases(self):
        rows, _ = make_corpus_entries(["嗯\tń\t100\n谢谢你告诉我这个消息\txiè xiè nǐ gào sù wǒ zhè ge xiāo xi\t500"], [], [])
        self.assertIn(("嗯", "en", 100), rows)
        self.assertIn(("谢谢你告诉我这个消息", "xie xie ni gao su wo zhe ge xiao xi", 500), rows)

    def test_raw_frequency_ratio_survives_dictionary_conversion(self):
        entries = make_entries("你 200000 x\n好 20 x", {"你": ["ni"], "好": ["hao"]}, {})
        weights = {word: weight for word, _, weight in entries}
        self.assertEqual(weights["你"] // weights["好"], 10000)

    def test_syllabic_nasal_does_not_override_mixed_pinyin_abbreviation(self):
        chars = {"呣": ["m", "mou"], "嗯": ["n", "ng"]}
        self.assertEqual(make_entries("呣 10 x\n嗯 10 x", chars, {}), [("呣", "mou", 10)])
        self.assertEqual(make_entries("嗯 10 x", chars, {"嗯": "en"}), [("嗯", "en", 10)])

    def test_curated_phrase_is_added_beyond_base_limit_without_weakening_existing_word(self):
        chars = {"你": ["ni"], "好": ["hao"]}
        entries = make_entries("你 200000 x", chars, {}, max_entries=1, curated={"你好": 90000, "你": 1})
        self.assertEqual(entries, [("你", "ni", 200000), ("你好", "ni hao", 90000)])

    def test_curated_reading_keeps_game_term_polyphone(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "terms.tsv"
            source.write_text("重生\t7000\tchong sheng\n", encoding="utf-8")
            entries = make_entries("", {"重": ["zhong", "chong"], "生": ["sheng"]}, load_curated_readings(source), curated=load_curated_phrases(source))
            self.assertEqual(entries, [("重生", "chong sheng", 7000)])

    def test_malformed_manual_reading_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "terms.tsv"
            source.write_text("重生\t7000\tchong\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                load_curated_readings(source)

    def test_duplicate_priority_cannot_silently_undo_higher_priority(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "terms.tsv"
            source.write_text("hello\t40000\nhello\t30000\n", encoding="utf-8")
            self.assertEqual(load_curated_phrases(source), {"hello": 40000})

    def test_english_words_filter_annotations_names_phrases_and_keep_priorities(self):
        entries = make_english_entries("hello\nhelp\nLondon\nhello world\nhelp!\na\nhello\n", {"hello": 30000, "ddnet": 7000})
        self.assertEqual(entries, [("ddnet", 7000), ("hello", 30000), ("help", 100)])


if __name__ == "__main__":
    unittest.main()
