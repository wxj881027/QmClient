#!/usr/bin/env python3
"""校验和生成 QmClient 的 i18n source。"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError as error:  # pragma: no cover - Python 3.11+ is required.
    raise SystemExit("qmclient_i18n.py 需要 Python 3.11 或更高版本。") from error


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = REPO_ROOT / "translations/i18n/qmclient"
DEFAULT_SOURCE = SOURCE_ROOT / "qmclient.toml"
QM_SOURCE_ROOT = REPO_ROOT / "src/game/client/components/qmclient"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"}
KEY_RE = re.compile(r"^qm\.[a-z0-9_]+(?:\.[a-z0-9_]+)*$")
LOCALIZE_RE = re.compile(
    r'Localize\s*\(\s*"((?:\\.|[^"\\])*)"'
    r'(?:\s*,\s*"((?:\\.|[^"\\])*)")?\s*\)'
)


def decode_cpp_string(value: str) -> str:
    """Decode the escapes relevant to a C++ string without corrupting UTF-8."""
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
    decoded: list[str] = []
    index = 0
    while index < len(value):
        if value[index] != "\\" or index + 1 >= len(value):
            decoded.append(value[index])
            index += 1
            continue
        index += 1
        escaped = value[index]
        if escaped == "x":
            hex_digits = value[index + 1 : index + 3]
            if len(hex_digits) == 2 and all(char in "0123456789abcdefABCDEF" for char in hex_digits):
                decoded.append(chr(int(hex_digits, 16)))
                index += 3
                continue
        decoded.append(escapes.get(escaped, "\\" + escaped))
        index += 1
    return "".join(decoded)


def load_source(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source_file:
            data = tomllib.load(source_file)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ValueError(f"{path}: 无法读取 TOML: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"{path}: 顶层必须是 TOML table。")
    return data


def validate_source(path: Path) -> tuple[dict[str, Any], list[str]]:
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

    keys: set[str] = set()
    for index, message in enumerate(messages, start=1):
        prefix = f"messages[{index}]"
        if not isinstance(message, dict):
            errors.append(f"{prefix} 必须是 table。")
            continue

        key = message.get("key")
        fallback = message.get("fallback")
        context = message.get("context")
        if not isinstance(key, str) or not KEY_RE.fullmatch(key):
            errors.append(f"{prefix}.key 不是稳定的 qm.* key。")
        elif key in keys:
            errors.append(f"{prefix}.key 重复: {key}")
        else:
            keys.add(key)
        if not isinstance(fallback, str) or not fallback:
            errors.append(f"{prefix}.fallback 必须是非空字符串。")
        if not isinstance(context, str):
            errors.append(f"{prefix}.context 必须是字符串。")

    return data, errors


def iter_qm_sources() -> list[Path]:
    if not QM_SOURCE_ROOT.is_dir():
        return []
    return sorted(
        path
        for path in QM_SOURCE_ROOT.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )


def extract_fallbacks() -> set[tuple[str, str]]:
    fallbacks: set[tuple[str, str]] = set()
    for path in iter_qm_sources():
        text = path.read_text(encoding="utf-8-sig")
        for match in LOCALIZE_RE.finditer(text):
            fallback = decode_cpp_string(match.group(1))
            context = decode_cpp_string(match.group(2) or "")
            fallbacks.add((fallback, context))
    return fallbacks


def validate_code_coverage(
    data: dict[str, Any], extracted: set[tuple[str, str]]
) -> list[str]:
    messages = data.get("messages", [])
    source_fallbacks = {
        (message["fallback"], message["context"])
        for message in messages
        if isinstance(message, dict)
        and isinstance(message.get("fallback"), str)
        and isinstance(message.get("context"), str)
    }
    missing = sorted(extracted - source_fallbacks)
    return [
        f"Qm 源码中的 Localize 文案未登记: {fallback!r}"
        + (f" (context={context!r})" if context else "")
        for fallback, context in missing
    ]


def build_catalog(data: dict[str, Any]) -> dict[str, Any]:
    messages = {}
    for message in sorted(data["messages"], key=lambda item: item["key"]):
        messages[message["key"]] = {
            "fallback": message["fallback"],
            "context": message["context"],
        }
    return {
        "format_version": data["meta"]["format_version"],
        "namespace": data["meta"]["namespace"],
        "source_language": data["meta"]["source_language"],
        "messages": messages,
    }


def command_validate(source_path: Path) -> int:
    try:
        data, errors = validate_source(source_path)
        extracted = extract_fallbacks()
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    except (OSError, UnicodeError) as error:
        print(f"无法扫描 QmClient 源码: {error}", file=sys.stderr)
        return 1
    errors.extend(validate_code_coverage(data, extracted))
    if errors:
        print("\n".join(f"{source_path}: {error}" for error in errors), file=sys.stderr)
        return 1
    print(
        f"QmClient i18n validation passed: "
        f"{len(data['messages'])} message(s), {len(extracted)} source fallback(s)."
    )
    return 0


def command_generate(source_path: Path, output_path: Path | None) -> int:
    try:
        data, errors = validate_source(source_path)
        extracted = extract_fallbacks()
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    except (OSError, UnicodeError) as error:
        print(f"无法扫描 QmClient 源码: {error}", file=sys.stderr)
        return 1
    errors.extend(validate_code_coverage(data, extracted))
    if errors:
        print("\n".join(f"{source_path}: {error}" for error in errors), file=sys.stderr)
        return 1

    catalog = json.dumps(build_catalog(data), ensure_ascii=False, indent=2) + "\n"
    if output_path is None:
        print(catalog, end="")
        return 0
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(catalog, encoding="utf-8", newline="\n")
    print(f"Generated {output_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        choices=("validate", "generate"),
        help="validate 校验 source；generate 生成确定性的 JSON catalog",
    )
    parser.add_argument(
        "--source",
        type=Path,
        default=DEFAULT_SOURCE,
        help="QmClient TOML source 路径",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="generate 的输出路径；不传则写到 stdout",
    )
    args = parser.parse_args()

    source_path = args.source.resolve()
    if args.command == "validate":
        return command_validate(source_path)
    return command_generate(args.source.resolve(), args.output.resolve() if args.output else None)


if __name__ == "__main__":
    raise SystemExit(main())
