"""从独立的 Tatoeba 人工注音导出生成固定样本；不向生产词典写入评测文本。"""

from __future__ import annotations

import argparse
import bz2
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.request
import zipfile

from build_rime_dict import REPO_ROOT, SOURCES, is_cjk, normalize_syllable, parse_chars


SNAPSHOTS = {
    "cmn_sentences_detailed.tsv.bz2": "7af009b234bbb21ed6a254c2b55fa7c81c9dd53509d0982cd92c1097573f9af5",
    "cmn_transcriptions.tsv.bz2": "7b071d2f45ca0507cb4bd1a7588b6fe7a8bd3af6b4a0f6224ca24ff1c3032277",
    "wordfreq-3.1.1.whl": "4b1c6ecffc6198be3396d5cf871c4423ca71c907c231348d352dd54d62b97473",
    "msgpack-1.1.2.whl": "9ade919fac6a3e7260b7f64cea89df6bec59104987cbea34d34a2fa15d74310b",
}
SOURCE_URLS = {
    "cmn_sentences_detailed.tsv.bz2": "https://downloads.tatoeba.org/exports/per_language/cmn/cmn_sentences_detailed.tsv.bz2",
    "cmn_transcriptions.tsv.bz2": "https://downloads.tatoeba.org/exports/per_language/cmn/cmn_transcriptions.tsv.bz2",
    "wordfreq-3.1.1.whl": "https://files.pythonhosted.org/packages/24/61/62835c475d69872d30689f284497853fe33fe1d6dd18f57346d13305861d/wordfreq-3.1.1-py3-none-any.whl",
    "msgpack-1.1.2.whl": "https://files.pythonhosted.org/packages/6e/f1/abd09c2ae91228c5f3998dbd7f41353def9eac64253de3c8105efa2082f7/msgpack-1.1.2-cp314-cp314-win_amd64.whl",
}
SYLLABLE = re.compile(r"([a-zA-ZüÜvV:]+)[0-5]")


def digest(text: str) -> str:
    return hashlib.sha256(("QmIME independent corpus v1:" + text).encode()).hexdigest()


def codes(syllables: list[str]) -> dict[str, str]:
    return {
        "full": "".join(syllables),
        "initial": "".join(value[0] for value in syllables),
        "mixed": "".join(value if index % 2 == 0 else value[0] for index, value in enumerate(syllables)),
    }


def aligned_reading(text: str, transcription: str, chars: dict[str, list[str]]) -> list[str] | None:
    syllables = [normalize_syllable(value.replace(":", "\u0308")) for value in SYLLABLE.findall(transcription)]
    chinese = "".join(value for value in text if is_cjk(value))
    # 非汉字与标点必须在两份文本中都没有承载其它字母/数字，避免错位配对。
    if any(value.isalnum() and not is_cjk(value) for value in text):
        return None
    if len(chinese) != len(syllables) or any(value in {"m", "n", "ng", ""} for value in syllables):
        return None
    if any(value not in chars.get(char, []) for char, value in zip(chinese, syllables)):
        return None
    return syllables


def prepare(cache: Path, output: Path, char_source: Path, word_count: int, sentence_count: int, download: bool = False) -> None:
    for name, expected in SNAPSHOTS.items():
        if not (cache / name).is_file() and download:
            cache.mkdir(parents=True, exist_ok=True)
            # 导出会按周更新。只接受本轮已冻结的快照，不静默换一套评测样本。
            with urllib.request.urlopen(SOURCE_URLS[name], timeout=60) as response:
                data = response.read(64 * 1024 * 1024 + 1)
            if len(data) > 64 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"远程快照已变化或下载不完整：{name}；请保留/恢复原离线缓存")
            (cache / name).write_bytes(data)
        if hashlib.sha256((cache / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"评测固定输入 SHA-256 不符：{name}")
    sys.path.insert(0, str(cache / "msgpack-1.1.2.whl"))
    import gzip
    import msgpack
    with zipfile.ZipFile(cache / "wordfreq-3.1.1.whl") as package:
        mapping = msgpack.unpackb(gzip.decompress(package.read("wordfreq/data/_chinese_mapping.msgpack.gz")), raw=False, strict_map_key=False)
    char_digest = next(expected for name, _, expected in SOURCES if name == "chars.txt")
    if hashlib.sha256(char_source.read_bytes()).hexdigest() != char_digest:
        raise ValueError("评测单字读音源 SHA-256 不符")
    chars = parse_chars(char_source.read_text(encoding="utf-8-sig"))
    with bz2.open(cache / "cmn_sentences_detailed.tsv.bz2", "rt", encoding="utf-8") as stream:
        sentences = {fields[0]: (fields[2], fields[3]) for line in stream if len(fields := line.rstrip("\n").split("\t")) == 6}
    groups: dict[str, dict[str, tuple[str, list[str], str, str, str]]] = {"word": {}, "sentence": {}}
    rejected: Counter = Counter()
    with bz2.open(cache / "cmn_transcriptions.tsv.bz2", "rt", encoding="utf-8") as stream:
        for line in stream:
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 5 or fields[1:3] != ["cmn", "Latn"] or fields[3] in {"", "\\N"} or fields[0] not in sentences:
                rejected["missing_author_or_sentence"] += 1
                continue
            identifier, _, _, author, reading = fields
            text, owner = sentences[identifier]
            if text.translate(mapping) != text:
                rejected["traditional"] += 1
                continue
            syllables = aligned_reading(text, reading, chars)
            if syllables is None:
                rejected["unaligned_reading"] += 1
                continue
            chinese = "".join(value for value in text if is_cjk(value))
            # 人工空格注音里的词边界提供词样本；预期文本与读音均来自外部语料。
            offset = 0
            for token in reading.split():
                values = [normalize_syllable(value.replace(":", "\u0308")) for value in SYLLABLE.findall(token)]
                word = chinese[offset:offset + len(values)]
                offset += len(values)
                if 2 <= len(word) <= 6:
                    groups["word"].setdefault(word, (word, values, identifier, owner, author))
            for match in re.finditer(r"[\u3400-\u4dbf\u4e00-\u9fff]+", text):
                phrase = match.group()
                offset = sum(is_cjk(value) for value in text[:match.start()])
                values = syllables[offset:offset + len(phrase)]
                if 4 <= len(phrase) <= 12:
                    groups["sentence"].setdefault(phrase, (phrase, values, identifier, owner, author))
    # 相同中文只进入一个集合；取样和分组只依赖内容 hash，不依赖任一候选输出。
    selected = []
    selected_texts = set()
    for kind, maximum in (("sentence", sentence_count), ("word", word_count)):
        for text, row in sorted(groups[kind].items(), key=lambda pair: digest(pair[0]))[:maximum]:
            if text not in selected_texts:
                selected.append((kind, row))
                selected_texts.add(text)
    output.mkdir(parents=True, exist_ok=True)
    files = {name: (output / (name + ".tsv")).open("w", encoding="utf-8", newline="\n") for name in ("dev", "heldout")}
    authors = ["id\ttext\tsentence_author\ttranscription_author\turl"]
    counts: Counter = Counter()
    try:
        for kind, (text, values, identifier, owner, author) in selected:
            split = "dev" if int(digest(text)[-8:], 16) % 5 == 0 else "heldout"
            for code_kind, code in codes(values).items():
                files[split].write(f"{identifier}-{digest(text)[:10]}\t{kind}-{code_kind}\t{code}\t{text}\n")
                counts[f"{split}/{kind}-{code_kind}"] += 1
            authors.append(f"{identifier}\t{text}\t{owner}\t{author}\thttps://tatoeba.org/en/sentences/show/{identifier}")
    finally:
        for stream in files.values():
            stream.close()
    (output / "attribution.tsv").write_text("\n".join(authors) + "\n", encoding="utf-8", newline="\n")
    summary = {"sources": SNAPSHOTS, "source_urls": SOURCE_URLS, "character_readings_sha256": char_digest, "license": "Tatoeba contributors, CC BY 2.0 FR: https://tatoeba.org/en/downloads", "mapping_credit": "wordfreq 3.1.1, Copyright 2022 Robyn Speer, CC BY-SA 4.0; https://github.com/rspeer/wordfreq/blob/912caf64b657478d1dff1138efdc078947d54bb1/NOTICE.md", "counts": dict(sorted(counts.items())), "rejected": dict(rejected), "available": {key: len(value) for key, value in groups.items()}}
    for name in ("dev.tsv", "heldout.tsv"):
        summary[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", type=Path, default=REPO_ROOT / "tmp/ime/downloads/corpus")
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / "tmp/ime/slice/corpus-evaluation")
    parser.add_argument("--chars", type=Path, default=REPO_ROOT / "tmp/ime/downloads/data/chars.txt")
    parser.add_argument("--words", type=int, default=1500)
    parser.add_argument("--sentences", type=int, default=750)
    parser.add_argument("--download", action="store_true", help="仅缺失时下载并校验固定快照；Tatoeba 周更后需恢复原缓存")
    args = parser.parse_args()
    prepare(args.cache_dir, args.output_dir, args.chars, args.words, args.sentences, args.download)
