#!/usr/bin/env python3
"""QmClient i18n source and official language-file workflow.

The client keeps using DDNet's Localize implementation. This script owns the
maintenance side only: scan source strings, call DeepSeek for missing entries,
write reviewed translations to TOML, and generate data/languages/*.txt.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError as error:  # pragma: no cover
    raise SystemExit("qmclient_i18n.py 需要 Python 3.11 或更高版本。") from error


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = REPO_ROOT / "translations/i18n/qmclient"
DEFAULT_SOURCE = SOURCE_ROOT / "qmclient.toml"
QM_SOURCE_ROOT = REPO_ROOT / "src/game/client/components/qmclient"
LANGUAGE_ROOT = REPO_ROOT / "data/languages"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"}
CALL_RE = re.compile(
    r"\b(?:Localize|Localizable)\s*\(\s*"
    r'"((?:\\.|[^"\\])*)"'
    r'(?:\s*,\s*"((?:\\.|[^"\\])*)")?\s*\)'
)
PLACEHOLDER_RE = re.compile(
    r"%%|%(?:[-+0# ]*)(?:\d+|\*)?(?:\.(?:\d+|\*))?"
    r"(?:hh|ll|h|l|j|z|t|L|w|I32|I64)?[cCdiouxXeEfgGaAnpsSZ]"
    r"|\{[A-Za-z0-9_]+\}"
)
LANGUAGE_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def decode_cpp_string(value: str) -> str:
    escapes = {
        "\\": "\\",
        '"': '"',
        "'": "'",
        "a": "\a",
        "b": "\b",
        "f": "\f",
        "n": "\n",
        "r": "\r",
        "t": "\t",
        "v": "\v",
        "0": "\0",
    }
    result: list[str] = []
    index = 0
    while index < len(value):
        if value[index] != "\\" or index + 1 >= len(value):
            result.append(value[index])
            index += 1
            continue
        escaped = value[index + 1]
        if escaped == "x" and index + 3 < len(value):
            digits = value[index + 2 : index + 4]
            if all(char in "0123456789abcdefABCDEF" for char in digits):
                result.append(chr(int(digits, 16)))
                index += 4
                continue
        result.append(escapes.get(escaped, "\\" + escaped))
        index += 2
    return "".join(result)


def toml_quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def load_source(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source_file:
            data = tomllib.load(source_file)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ValueError(f"{path}: 无法读取 TOML: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"{path}: 顶层必须是 TOML table。")
    return data


def source_messages(data: dict[str, Any]) -> list[dict[str, Any]]:
    messages = data.get("messages", [])
    return [message for message in messages if isinstance(message, dict)]


def iter_qm_sources() -> list[Path]:
    if not QM_SOURCE_ROOT.is_dir():
        return []
    return sorted(
        path
        for path in QM_SOURCE_ROOT.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )


def extract_source_records() -> set[tuple[str, str]]:
    records: set[tuple[str, str]] = set()
    for path in iter_qm_sources():
        text = path.read_text(encoding="utf-8-sig")
        for match in CALL_RE.finditer(text):
            records.add(
                (
                    decode_cpp_string(match.group(1)),
                    decode_cpp_string(match.group(2) or ""),
                )
            )
    return records


def placeholders(value: str) -> tuple[str, ...]:
    return tuple(PLACEHOLDER_RE.findall(value))


def validate_source(path: Path, *, require_coverage: bool = True) -> tuple[dict[str, Any], list[str]]:
    data = load_source(path)
    errors: list[str] = []
    meta = data.get("meta")
    if not isinstance(meta, dict):
        errors.append("缺少 [meta] table。")
    else:
        if meta.get("namespace") != "qmclient":
            errors.append("meta.namespace 必须为 qmclient。")
        if meta.get("source_language") != "en":
            errors.append("meta.source_language 当前必须为 en。")
        if not isinstance(meta.get("format_version"), int):
            errors.append("meta.format_version 必须为整数。")

    messages = data.get("messages")
    if not isinstance(messages, list) or not messages:
        errors.append("必须存在非空 [[messages]] 数组。")
        return data, errors

    identities: set[tuple[str, str]] = set()
    registered: set[tuple[str, str]] = set()
    for index, message in enumerate(messages, start=1):
        prefix = f"messages[{index}]"
        if not isinstance(message, dict):
            errors.append(f"{prefix} 必须是 table。")
            continue
        key = message.get("key")
        context = message.get("context", "")
        translations = message.get("translations", {})
        if not isinstance(key, str) or not key:
            errors.append(f"{prefix}.key 必须是非空英文 source key。")
        if not isinstance(context, str):
            errors.append(f"{prefix}.context 必须是字符串。")
            context = ""
        if isinstance(key, str) and key:
            identity = (key, context)
            if identity in identities:
                errors.append(f"{prefix}.key/context 重复: {key!r}, {context!r}")
            else:
                identities.add(identity)
                registered.add(identity)
        if not isinstance(translations, dict):
            errors.append(f"{prefix}.translations 必须是 table。")
            continue
        for language, translation in translations.items():
            if not isinstance(language, str) or not LANGUAGE_RE.fullmatch(language):
                errors.append(f"{prefix}.translations 的语言名无效: {language!r}")
            if not isinstance(translation, str) or not translation:
                errors.append(f"{prefix}.translations.{language} 必须是非空字符串。")
            elif isinstance(key, str) and placeholders(key) != placeholders(translation):
                errors.append(f"{prefix}.translations.{language} 的占位符与 source 不一致。")

    if require_coverage:
        for fallback, context in sorted(extract_source_records() - registered):
            errors.append(
                f"源码文案未登记: {fallback!r}"
                + (f" (context={context!r})" if context else "")
            )
    return data, errors


def dump_source(data: dict[str, Any]) -> str:
    lines = [
        "# QmClient i18n maintenance source.",
        "# Runtime remains DDNet Localize; data/languages/*.txt is generated.",
        "",
        "[meta]",
        'namespace = "qmclient"',
        'source_language = "en"',
        f"format_version = {data['meta']['format_version']}",
        "",
    ]
    for message in sorted(source_messages(data), key=lambda item: (item["key"], item.get("context", ""))):
        lines.extend(
            [
                "[[messages]]",
                f"key = {toml_quote(message['key'])}",
                f"context = {toml_quote(message.get('context', ''))}",
            ]
        )
        translations = message.get("translations", {})
        if translations:
            values = ", ".join(
                f"{language} = {toml_quote(translations[language])}"
                for language in sorted(translations)
            )
            lines.append(f"translations = {{ {values} }}")
        lines.append("")
    return "\n".join(lines)


def scan_source(path: Path, write: bool) -> int:
    data = load_source(path)
    messages = source_messages(data)
    known = {(message.get("key"), message.get("context", "")) for message in messages}
    added = 0
    for fallback, context in sorted(extract_source_records() - known):
        messages.append({"key": fallback, "context": context})
        known.add((fallback, context))
        added += 1
    data["messages"] = messages
    if write and added:
        path.write_text(dump_source(data), encoding="utf-8", newline="\n")
    print(f"source scan: {added} new message(s)" + (" written" if write and added else ""))
    return 0


def migrate_source(path: Path, write: bool) -> int:
    """Convert the former qm.* + fallback layout to official source-key layout."""
    data = load_source(path)
    merged: dict[tuple[str, str], dict[str, Any]] = {}
    for message in source_messages(data):
        source = message.get("fallback", message.get("key", ""))
        context = message.get("context", "")
        if not isinstance(source, str) or not source:
            continue
        identity = (source, context)
        target = merged.setdefault(identity, {"key": source, "context": context})
        for language, translation in message.get("translations", {}).items():
            target.setdefault("translations", {})[language] = translation
    data["messages"] = list(merged.values())
    if write:
        path.write_text(dump_source(data), encoding="utf-8", newline="\n")
    print(f"source migration: {len(data['messages'])} message(s)" + (" written" if write else " preview"))
    return 0


def language_list(value: str) -> list[str]:
    languages = [item.strip() for item in value.split(",") if item.strip()]
    if not languages or any(not LANGUAGE_RE.fullmatch(item) for item in languages):
        raise ValueError(f"无效语言列表: {value}")
    return languages


def request_batch(
    batch: list[dict[str, Any]], language: str, base_url: str, model: str, api_key: str, timeout: float
) -> dict[int, str]:
    payload = {
        "model": model,
        "temperature": 0.2,
        "messages": [
            {
                "role": "system",
                "content": "Translate DDNet UI strings. Return JSON only: {\"translations\":[{\"index\":0,\"text\":\"...\"}]}. Preserve every placeholder, URL, escape, and line break. Do not add explanations.",
            },
            {
                "role": "user",
                "content": json.dumps(
                    {"target_language": language, "items": [{"index": i, "source": item["key"], "context": item.get("context", "")} for i, item in enumerate(batch)]},
                    ensure_ascii=False,
                ),
            },
        ],
    }
    url = base_url.rstrip("/") + "/chat/completions"
    request = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            result = json.loads(response.read().decode("utf-8"))
    except (OSError, urllib.error.HTTPError, json.JSONDecodeError) as error:
        raise RuntimeError(f"DeepSeek 请求失败: {error}") from error
    if not isinstance(result, dict):
        raise RuntimeError("DeepSeek 返回的响应不是 JSON object。")
    choices = result.get("choices")
    if not isinstance(choices, list) or not choices or not isinstance(choices[0], dict):
        raise RuntimeError("DeepSeek 响应缺少 choices。")
    message = choices[0].get("message", {})
    content = message.get("content", "") if isinstance(message, dict) else ""
    if not isinstance(content, str) or not content:
        raise RuntimeError("DeepSeek 响应缺少可解析的 message.content。")
    content = re.sub(r"^```(?:json)?\s*|\s*```$", "", content.strip())
    try:
        decoded = json.loads(content)
    except json.JSONDecodeError as error:
        raise RuntimeError("DeepSeek 返回的 content 不是 JSON。") from error
    if not isinstance(decoded, dict) or not isinstance(decoded.get("translations"), list):
        raise RuntimeError("DeepSeek JSON 缺少 translations 数组。")
    translations: dict[int, str] = {}
    for item in decoded["translations"]:
        if not isinstance(item, dict) or not isinstance(item.get("text"), str):
            continue
        try:
            item_index = int(item["index"])
        except (KeyError, TypeError, ValueError) as error:
            raise RuntimeError("DeepSeek translation item 缺少有效 index。") from error
        if item_index in translations:
            raise RuntimeError(f"DeepSeek 返回重复 translation index: {item_index}")
        translations[item_index] = item["text"]
    return translations


def translate_source(path: Path, languages: list[str], write: bool, rewrite: bool, base_url: str, model: str, api_key_env: str, batch_size: int, timeout: float) -> int:
    data, errors = validate_source(path, require_coverage=True)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    api_key = os.environ.get(api_key_env, "")
    if not api_key:
        print(f"缺少环境变量 {api_key_env}；仅检查 source，不发送请求。", file=sys.stderr)
        return 1
    messages = source_messages(data)
    for language in languages:
        pending = [message for message in messages if rewrite or language not in message.get("translations", {})]
        if not pending:
            print(f"{language}: no missing translations")
            continue
        for start in range(0, len(pending), batch_size):
            batch = pending[start : start + batch_size]
            translations = request_batch(batch, language, base_url, model, api_key, timeout)
            for index, message in enumerate(batch):
                translation = translations.get(index)
                if translation is None or placeholders(message["key"]) != placeholders(translation):
                    raise RuntimeError(f"{language}: 第 {start + index} 条翻译缺失或占位符不一致。")
                message.setdefault("translations", {})[language] = translation
        print(f"{language}: translated {len(pending)} message(s)")
    if write:
        path.write_text(dump_source(data), encoding="utf-8", newline="\n")
        print(f"updated {path}")
    else:
        print("preview only; add --write to modify TOML")
    return 0


def merge_runtime_file(
    path: Path,
    updates: dict[tuple[str, str], str],
    append_entries: dict[tuple[str, str], str],
) -> tuple[str, int]:
    """Update translated entries and append missing Qm entries without clobbering official text."""
    lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    output: list[str] = []
    seen: set[tuple[str, str]] = set()
    index = 0
    while index < len(lines):
        start = index
        line = lines[index]
        if not line or line.startswith("#"):
            output.append(line)
            index += 1
            continue

        context = ""
        if line.startswith("[") and line.endswith("]"):
            context = line[1:-1]
            index += 1
            if index >= len(lines):
                output.extend(lines[start:index])
                break
            source = lines[index]
            index += 1
        else:
            source = line
            index += 1
        if index >= len(lines) or not lines[index].startswith("== "):
            output.extend(lines[start:index])
            continue

        replacement_index = index
        index += 1
        identity = (source, context)
        output.extend(lines[start:replacement_index])
        output.append(f"== {updates[identity]}" if identity in updates else lines[replacement_index])
        seen.add(identity)

    missing = [identity for identity in append_entries if identity not in seen]
    if missing:
        if output and output[-1] != "":
            output.append("")
        for offset, (source, context) in enumerate(sorted(missing, key=lambda item: (item[1].casefold(), item[0].casefold(), item[1], item[0]))):
            if context:
                output.append(f"[{context}]")
            elif source.startswith("["):
                output.append("[]")
            output.extend([source, f"== {append_entries[(source, context)]}"])
            if offset + 1 != len(missing):
                output.append("")
    return "\n".join(output).rstrip("\n") + ("\n" if output else ""), len(missing)


def generate_languages(path: Path, languages: list[str], output_dir: Path) -> int:
    data, errors = validate_source(path, require_coverage=True)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    output_dir.mkdir(parents=True, exist_ok=True)
    for language in languages:
        runtime_path = LANGUAGE_ROOT / f"{language}.txt"
        updates: dict[tuple[str, str], str] = {}
        append_entries: dict[tuple[str, str], str] = {}
        for message in source_messages(data):
            identity = (message["key"], message.get("context", ""))
            translation = message.get("translations", {}).get(language)
            if isinstance(translation, str) and translation:
                updates[identity] = translation
                append_entries[identity] = translation
            else:
                append_entries[identity] = message["key"]
        output_path = output_dir / f"{language}.txt"
        source_text = runtime_path if output_path != runtime_path else output_path
        merged, missing = merge_runtime_file(source_text, updates, append_entries)
        output_path.write_text(merged, encoding="utf-8", newline="\n")
        print(f"Generated {output_path} ({len(append_entries)} Qm entries, {missing} appended)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("validate", "scan", "migrate", "translate", "generate"))
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--write", action="store_true", help="允许 scan/translate 写回 TOML")
    parser.add_argument("--rewrite", action="store_true", help="重新翻译已有译文")
    parser.add_argument("--languages", default="simplified_chinese")
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / "tmp/qmclient-languages")
    parser.add_argument("--base-url", default="https://api.deepseek.com")
    parser.add_argument("--model", default="deepseek-chat")
    parser.add_argument("--api-key-env", default="DEEPSEEK_API_KEY")
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()
    source_path = args.source.resolve()
    try:
        if args.command == "validate":
            _, errors = validate_source(source_path, require_coverage=True)
            if errors:
                print("\n".join(errors), file=sys.stderr)
                return 1
            data = load_source(source_path)
            print(f"QmClient i18n validation passed: {len(source_messages(data))} message(s), {len(extract_source_records())} source record(s).")
            return 0
        if args.command == "scan":
            return scan_source(source_path, args.write)
        if args.command == "migrate":
            return migrate_source(source_path, args.write)
        languages = language_list(args.languages)
        if args.command == "translate":
            return translate_source(source_path, languages, args.write, args.rewrite, args.base_url, args.model, args.api_key_env, max(1, args.batch_size), args.timeout)
        return generate_languages(source_path, languages, args.output_dir.resolve())
    except (OSError, RuntimeError, UnicodeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
