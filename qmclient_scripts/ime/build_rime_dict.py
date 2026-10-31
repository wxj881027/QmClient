"""从固定版本数据生成内置拼音词典并保留上游许可；默认使用离线缓存。"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
from pathlib import Path
import re
import unicodedata
import urllib.request


REPO_ROOT = Path(__file__).resolve().parents[2]
CHINESE_PRIORITY_SOURCES = ("curated_phrases.txt", "chat_phrases.tsv", "ddnet_phrases.tsv", "user_chat_supplement.tsv")
# 名称、固定提交中的路径、SHA-256。普通构建不运行本脚本或访问网络。
SOURCES = (
    ("wanxiang-jichu.dict.yaml", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/dicts/jichu.dict.yaml", "b0d4fea9ec962a00b0ead8d99e9ae9c71e9aec9cb73163400e56b05dfe6afbca"),
    ("wanxiang-zi.dict.yaml", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/dicts/zi.dict.yaml", "7a3d887463bc256cc319833baa7f4f5b8d01863a0c78ba257d46283e94d27dfe"),
    ("wanxiang-duoyin.dict.yaml", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/dicts/duoyin.dict.yaml", "f89a50949e93ac60746aa3058f67bc3a6bbd4a9a5c2d43652df0cb2ae973d555"),
    ("wanxiang-LICENSE", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/LICENSE", "9e5f1b3c610b9c2da5c313bf81d577a7d1acec686bdb0384edefa6df0f90cd94"),
    ("wanxiang-LICENSE-SCOPE.md", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/LICENSE-SCOPE.md", "41ba4ac41b6a28a117c172dd22fc8cf31ff3902f79737cc22e22b9412af624f4"),
    ("wanxiang-NOTICE.md", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/NOTICE.md", "6c624b06a54d95ad2a33a3200481107f9f8d029dd3e7a224988653daa04bca13"),
    ("wanxiang-README.md", "amzxyz/rime-wanxiang-data/6ea60e43339de4257bf815529e0fccac0704670a/README.md", "c5248ecbd8f32d7b2e7425275799f673d505df91cb76fb6323b9f837ba314a1c"),
    ("english-3esl.txt", "en-wl/wordlist/5ef55f9c42730ebe4394a78b77855468a6f15dd2/alt12dicts/3esl.txt", "3361477aeeb36dd104ea1c7a2bcf3d8c94fa14a25fd51f89c7e0c5f79f5f35d4"),
    ("english-12dicts-README", "en-wl/wordlist/5ef55f9c42730ebe4394a78b77855468a6f15dd2/alt12dicts/README-orig", "dc7fe31c4d3b98de723ff93839cc14da946bf9a61e6ef0ca0cff6991e26f19ed"),
    ("jieba-dict.txt", "fxsjy/jieba/1e20c89b66f56c9301b0feed211733ffaa1bd72a/jieba/dict.txt", "7197c3211ddd98962b036cdf40324d1ea2bfaa12bd028e68faa70111a88e12a8"),
    ("jieba-LICENSE", "fxsjy/jieba/1e20c89b66f56c9301b0feed211733ffaa1bd72a/LICENSE", "18ba0984839f85853b29fadaf992f7dba8fd0ca0fbeae34de2b8735222dc7a37"),
    ("phrases.txt", "mozillazg/phrase-pinyin-data/cee0ed6e6e4898580cafd2bd5e3723e20b214aa0/pinyin.txt", "dcc769607c220b312fea3e71cb63421298b4b891b1f7356a95ab58f2c96fff81"),
    ("phrases-LICENSE", "mozillazg/phrase-pinyin-data/cee0ed6e6e4898580cafd2bd5e3723e20b214aa0/LICENSE", "89ac55df747e4776088c3e77531ef61b973a1a59dd8e6a4548a58996da9a4f70"),
    ("chars.txt", "mozillazg/pinyin-data/fa9761fff402f8560196b1ba085c437c52b56d7c/pinyin.txt", "6170ab4ff7c9740fe765b1d35e6d3ad43554dd02874199388db513c44dcddf9d"),
    ("chars-LICENSE", "mozillazg/pinyin-data/fa9761fff402f8560196b1ba085c437c52b56d7c/LICENSE", "9c048697be2502a16e8bcb282d5d465a07295b2def0ffb05a269c5d39dbe1586"),
    ("rime-LICENSE", "rime/librime/1.17.0/LICENSE", "f67d27a6d2d586fcfed4b4c886a83747095396a39b6641e18e855086be2ec400"),
    ("unicode-LICENSE", "https://www.unicode.org/license.txt", "e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96"),
    ("python-pinyin-LICENSE", "mozillazg/python-pinyin/366de0363ff1fb9a718ce668448bea59de09a4bf/LICENSE.txt", "1e6c90014b4912815c296ee64bb6f6280af47e6d4c5d80e86232dfc5defe764c"),
    ("chars-README.md", "mozillazg/pinyin-data/fa9761fff402f8560196b1ba085c437c52b56d7c/README.md", "775c581af29c3069421bfbe6cd641856bb0141487a02a7c39a1842cedd875a6d"),
    ("phrases-README.md", "mozillazg/phrase-pinyin-data/cee0ed6e6e4898580cafd2bd5e3723e20b214aa0/README.md", "79e9b1a13e6948af49c055a75a792df7844429bb8de3d38c75f383886e100654"),
    ("phrases-Makefile", "mozillazg/phrase-pinyin-data/cee0ed6e6e4898580cafd2bd5e3723e20b214aa0/Makefile", "7f85532b4a9b5c8fb4a5b7d7ecd9dab5fe67e85efd933ff69a4d31ca18da7177"),
)


def normalize_syllable(text: str) -> str:
    # 先保留 ü，再去声调；nǚ 等带声调字符必须先分解才能识别分音符。
    text = unicodedata.normalize("NFD", text.lower().strip())
    text = text.replace("u\u0308", "v")
    text = "".join(char for char in text if not unicodedata.combining(char))
    return text if re.fullmatch(r"[a-z]+", text) else ""


def parse_chars(text: str) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        code, readings = line.split(":", 1)
        char = chr(int(code.removeprefix("U+"), 16))
        values = list(dict.fromkeys(normalize_syllable(value) for value in readings.split(",")))
        values = [value for value in values if value]
        if values:
            result[char] = values
    return result


def parse_phrases(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        phrase, readings = line.split(":", 1)
        phrase = phrase.strip()
        syllables = [normalize_syllable(value) for value in readings.split()]
        if len(syllables) == len(phrase) and all(syllables):
            result.setdefault(phrase, " ".join(syllables))
    return result


def is_cjk(char: str) -> bool:
    value = ord(char)
    return 0x3400 <= value <= 0x4DBF or 0x4E00 <= value <= 0x9FFF or 0x20000 <= value <= 0x323AF


def make_entries(jieba: str, chars: dict[str, list[str]], phrases: dict[str, str], max_entries: int = 200_000, min_frequency: int = 2, curated: dict[str, int] | None = None) -> list[tuple[str, str, int]]:
    def reading(word: str) -> str | None:
        code = phrases.get(word)
        if code is not None and not any(syllable in {"m", "n", "ng"} for syllable in code.split()):
            return code
        # 鼻音单独成音节会让 mdan 的 m 优先被当成“呣”，压住“名单”的简拼。
        # 通用拼音词典使用完整音节；多音字原表仍完整保留供领域词匹配。
        values = [next((value for value in chars.get(char, []) if value not in {"m", "n", "ng"}), "") for char in word]
        return " ".join(values) if all(values) else None
    words: dict[str, int] = {}
    for line in jieba.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        word, frequency_text = fields[:2]
        frequency = int(frequency_text)
        if 1 <= len(word) <= 8 and frequency >= min_frequency and all(is_cjk(char) for char in word):
            words[word] = max(words.get(word, 0), frequency)
    entries = []
    for word, frequency in sorted(words.items(), key=lambda pair: (-pair[1], pair[0])):
        code = reading(word)
        if code is None:
            continue
        # Rime 会按总词频计算概率；这里保留频次比例，避免再次取对数抹平常用词和生僻词的差距。
        weight = max(1, min(1_000_000_000, frequency))
        entries.append((word, code, weight))
        if len(entries) >= max_entries:
            break
    # 口语短语优先表：jieba 缺少的词（如“好难”“才是”）补入并给较高权重，
    # 避免 Rime 用单字权重拼出“好南”这类反常识的整句候选。
    if curated:
        by_word = {word: (word, code, weight) for word, code, weight in entries}
        for word, curated_weight in curated.items():
            if not (1 <= len(word) <= 8 and all(is_cjk(char) for char in word)):
                continue
            code = reading(word)
            if code is None:
                continue
            previous = by_word.get(word)
            weight = max(curated_weight, previous[2] if previous is not None else 0)
            by_word[word] = (word, code, min(1_000_000_000, weight))
        entries = list(by_word.values())
    return entries


def make_corpus_entries(sources: list[str], fallback: list[tuple[str, str, int]],
                        priorities: list[tuple[str, str, int]],
                        explicit_readings: dict[str, str] | None = None) -> tuple[list[tuple[str, str, int]], dict[str, int]]:
    """保留语料词库的词形/读音权重，以低权重补回旧库独有词，不对评测输入设分支。"""
    entries: dict[tuple[str, str], int] = {}
    statistics: Counter = Counter()
    for text in sources:
        for line in text.splitlines():
            if not line or line.startswith("#") or "\t" not in line:
                continue
            fields = line.split("\t")
            word, reading = fields[:2]
            if not (1 <= len(word) <= 12 and all(is_cjk(char) for char in word)):
                statistics["excluded_text"] += 1
                continue
            syllables = [normalize_syllable(value) for value in reading.split()]
            # 鼻音展开成完整音节，避免 m/n/ng 抢占简拼；不猜测儿化或错位注音。
            syllables = [{"m": "me", "n": "en", "ng": "eng"}.get(value, value) for value in syllables]
            if len(syllables) != len(word) or not all(syllables):
                statistics["excluded_reading"] += 1
                continue
            if len(fields) >= 3 and not fields[2].isdigit():
                statistics["excluded_weight"] += 1
                continue
            weight = min(1_000_000_000, max(1, int(fields[2]) if len(fields) >= 3 else 1))
            key = (word, " ".join(syllables))
            # 去声调后的同词同音和跨表重复取最大值；不把重复行误当多份语料相加。
            entries[key] = max(entries.get(key, 0), weight)
            statistics["accepted_source_rows"] += 1
    if not entries:
        raise ValueError("语料词典为空")
    statistics["corpus_entries"] = len(entries)
    words = {word for word, _ in entries}
    for word, code, weight in fallback:
        if word not in words:
            entries[word, code] = max(1, round(weight * 0.02))
            statistics["fallback_entries"] += 1
    best: dict[str, tuple[str, int]] = {}
    for (word, code), weight in entries.items():
        if word not in best or weight > best[word][1]:
            best[word] = (code, weight)
    for word, code, weight in priorities:
        # 人工明确的读音才覆盖语料读音；普通优先词沿用语料主读音，保留多音字区别。
        code = (explicit_readings or {}).get(word, best[word][0] if word in best else code)
        native_weight = entries.get((word, code), 0)
        # 同级人工优先词保留语料中的相对顺序，避免都被抹成相同权重后按字形排队。
        entries[word, code] = min(1_000_000_000, max(native_weight, weight) + min(native_weight, weight) // 100)
        statistics["priority_entries"] += 1
    result = sorted(((word, code, weight) for (word, code), weight in entries.items()), key=lambda value: (-value[2], value[0], value[1]))
    statistics["output_entries"] = len(result)
    return result, dict(statistics)


def load_curated_phrases(path: Path) -> dict[str, int]:
    if not path.is_file():
        return {}
    curated: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split("\t")
        word = fields[0].strip()
        weight = 30_000
        if len(fields) > 1:
            try:
                weight = int(fields[1].strip())
            except ValueError:
                raise ValueError(f"非法常用短语权重：{line}")
        curated[word] = max(curated.get(word, 0), max(1, min(1_000_000_000, weight)))
    return curated


def make_english_entries(text: str, curated: dict[str, int] | None = None) -> list[tuple[str, int]]:
    # 3esl 面向日常英语；不导入专名、短语、缩写注记或带标点的词条。
    entries = {word: 100 for line in text.splitlines()
               if re.fullmatch(r"[a-z]{2,28}", word := line.strip())}
    for word, weight in (curated or {}).items():
        if re.fullmatch(r"[a-z]{2,28}", word):
            entries[word] = max(entries.get(word, 0), weight)
    return sorted(entries.items())


def load_curated_readings(path: Path) -> dict[str, str]:
    result = {}
    if not path.is_file():
        return result
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("#", 1)[0].strip().split("\t")
        if len(fields) < 3:
            continue
        word, code = fields[0], fields[2]
        if len(word) != len(code.split()) or not all(re.fullmatch("[a-z]+", syllable) for syllable in code.split()):
            raise ValueError(f"词组读音与文字不匹配：{line}")
        result[word] = code
    return result


def render_game_aliases(path: Path) -> str:
    text = "# Qm 已确认的完整输入快捷词；不改变基础词频，不参与汉语音节划分。\n---\nname: qm_game_aliases\nversion: \"2\"\nsort: by_weight\nuse_preset_vocabulary: false\ncolumns: [text, code, weight]\n...\n"
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        code, word = line.split("\t")
        if not re.fullmatch("[a-z]{2,16}", code) or not (re.fullmatch("[A-Za-z]{2,16}", word) or 1 <= len(word) <= 12 and all(is_cjk(char) for char in word)):
            raise ValueError(f"非法完整输入快捷词：{line}")
        text += f"{word}\t{code}\t100\n"
    return text



def source_url(source: str) -> str:
    return source if source.startswith("https://") else "https://raw.githubusercontent.com/" + source


def read_sources(cache: Path, download: bool) -> dict[str, str]:
    texts = {}
    for name, source, expected in SOURCES:
        path = cache / name
        if not path.exists():
            if not download:
                raise ValueError(f"离线源文件缺失：{path}；使用 --download 准备固定输入")
            with urllib.request.urlopen(source_url(source), timeout=45) as response:
                data = response.read(64 * 1024 * 1024 + 1)
            if len(data) > 64 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"下载校验失败：{name}")
            cache.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"固定输入 SHA-256 不符：{path}")
        texts[name] = data.decode("utf-8-sig")
    return texts


def render_licenses(texts: dict[str, str]) -> str:
    licenses = (
        "QmClient built-in pinyin data\n\n"
        "Chinese corpus lexicon: Wanxiang Pinyin by amzxyz, CC BY 4.0.\n"
        "https://github.com/amzxyz/rime-wanxiang-data/tree/6ea60e43339de4257bf815529e0fccac0704670a\n"
        "Adapted data only: jichu/zi/duoyin dictionaries. No Wanxiang program, plugin,\n"
        "Lua code or grammar model is included. The Chinese derivative dictionary is\n"
        "distributed under CC BY 4.0; the other upstream notices remain applicable.\n"
        "Changes: remove tones, expand syllabic nasals, filter non-CJK/misaligned rows,\n"
        "merge duplicate word/pronunciation rows by max weight, add low-weight jieba\n"
        "fallbacks and Qm chat/game priorities, and convert to the Qm dictionary.\n\n"
        "Additional user-authorized local supplement: manually reviewed common chat/game\n"
        "short phrases only; the original backup, personal records and identifiers are\n"
        "not included. These priority values are not measured corpus frequencies.\n\n"
        "jieba v0.42.1, phrase-pinyin-data v0.19.0, pinyin-data v0.15.0: MIT repository licenses.\n"
        "pinyin-data includes Unicode Unihan 16.0.0 data; the Unicode License V3 is retained below.\n"
        "phrase-pinyin-data identifies hotoo/pinyin and python-pinyin as its initial sources;\n"
        "their MIT attribution (including Hotoo) is retained in the original python-pinyin license.\n"
        "Only phrase-pinyin-data pinyin.txt is used, not large_pinyin.txt or cc_cedict.txt.\n"
        "See the pinned upstream README and Makefile URLs for that file distinction.\n"
        "Changes: CJK/length/frequency filtering, phrase-first readings, tone removal, u-diaeresis\n"
        "encoded as v, word weights, and conversion to Rime dictionary/character-table formats.\n"
        "librime 1.17.0 core: BSD-3-Clause. The separately prepared runtime is accompanied by\n"
        "RIME-LICENSES.txt with its static dependencies and bundled header notices.\n\n"
        "Fixed inputs and provenance (SHA-256):\n"
    )
    for name, source, digest in SOURCES:
        licenses += f"{name}: {digest}\n{source_url(source)}\n"
    for name in ("wanxiang-LICENSE", "wanxiang-LICENSE-SCOPE.md", "wanxiang-NOTICE.md", "jieba-LICENSE", "phrases-LICENSE", "chars-LICENSE", "unicode-LICENSE", "python-pinyin-LICENSE", "rime-LICENSE", "english-12dicts-README"):
        licenses += f"\n{'=' * 72}\n{name}\n{'=' * 72}\n{texts[name].strip()}\n"
    return licenses


def generate(cache: Path, output: Path, download: bool = False) -> tuple[int, int]:
    texts = read_sources(cache, download)
    chars = parse_chars(texts["chars.txt"])
    phrases = parse_phrases(texts["phrases.txt"])
    explicit_readings = {}
    for name in CHINESE_PRIORITY_SOURCES:
        explicit_readings.update(load_curated_readings(REPO_ROOT / "qmclient_scripts/ime" / name))
    phrases.update(explicit_readings)
    curated: dict[str, int] = {}
    for name in CHINESE_PRIORITY_SOURCES:
        for word, weight in load_curated_phrases(REPO_ROOT / "qmclient_scripts/ime" / name).items():
            curated[word] = max(curated.get(word, 0), weight)
    fallback = make_entries(texts["jieba-dict.txt"], chars, phrases)
    priorities = make_entries("", chars, phrases, curated=curated)
    entries, statistics = make_corpus_entries([texts[name] for name in ("wanxiang-zi.dict.yaml", "wanxiang-jichu.dict.yaml", "wanxiang-duoyin.dict.yaml")], fallback, priorities, explicit_readings)
    for word, code, _ in entries:
        for char, reading in zip(word, code.split()):
            values = chars.setdefault(char, [])
            if reading not in values:
                values.append(reading)
    english = make_english_entries(texts["english-3esl.txt"], load_curated_phrases(REPO_ROOT / "qmclient_scripts/ime/english_phrases.tsv"))
    if not entries or not chars:
        raise ValueError("固定输入没有生成词条或单字读音")
    dictionary = "# 由 qmclient_scripts/ime/build_rime_dict.py 生成，请勿手改。\n# Wanxiang Pinyin / amzxyz, CC BY 4.0；完整署名、改动和许可见 LICENSES.txt。\n---\nname: qm_pinyin\nversion: \"3\"\nsort: by_weight\nuse_preset_vocabulary: false\ncolumns: [text, code, weight]\n...\n"
    dictionary += "".join(f"{word}\t{code}\t{weight}\n" for word, code, weight in entries)
    readings = "# pinyin-data v0.15.0 + Wanxiang / amzxyz (CC BY 4.0); U+码点<TAB>无声调读音\n"
    readings += "".join(f"U+{ord(char):04X}\t{' '.join(values)}\n" for char, values in sorted(chars.items()))
    licenses = render_licenses(texts)
    output.mkdir(parents=True, exist_ok=True)
    english_text = "# 12dicts 3esl（Alan Beale，public domain）+ Qm 日常/游戏词；word<TAB>priority\n"
    english_text += "".join(f"{word}\t{weight}\n" for word, weight in english)
    aliases = render_game_aliases(REPO_ROOT / "qmclient_scripts/ime/game_aliases.tsv")
    for name, content in (("qm_pinyin.dict.yaml", dictionary), ("qm_game_aliases.dict.yaml", aliases), ("qm_pinyin_chars.txt", readings), ("qm_english_words.tsv", english_text), ("LICENSES.txt", licenses)):
        (output / name).write_text(content, encoding="utf-8", newline="\n")
    print(f"中文词条 {len(entries)}；英文 {len(english)}；单字 {len(chars)}；词典 {len(dictionary.encode('utf-8'))} 字节；读音表 {len(readings.encode('utf-8'))} 字节")
    print(f"语料转换统计：{statistics}")
    return len(entries), len(chars)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", type=Path, default=REPO_ROOT / "tmp/ime/downloads/data")
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / "data/ime")
    parser.add_argument("--download", action="store_true", help="允许下载固定版本输入；默认离线")
    args = parser.parse_args()
    try:
        generate(args.cache_dir, args.output_dir, args.download)
    except (OSError, ValueError) as error:
        parser.exit(1, f"生成失败：{error}\n")


if __name__ == "__main__":
    main()
