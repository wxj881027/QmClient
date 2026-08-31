# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""Shared source-key extraction for QmClient language tooling."""

from __future__ import annotations

import ast
import json
import os
import re
import subprocess
import warnings
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
SOURCE_EXTENSIONS = {".c", ".cpp", ".h", ".hpp"}
SOURCE_PATHS = (PROJECT_ROOT / "src",)
AUDIT_PATHS = (PROJECT_ROOT / "src", SCRIPT_DIR)
AUDIT_REPORT_FILE = SCRIPT_DIR / "extracted_audit_report.json"
SOURCE_RECORD_CACHE_FILE = SCRIPT_DIR / "extracted_records_cache.json"
EXTRACTOR_LOGIC_FILES = {
    "qmclient_scripts/languages_qmclient/source_keys.py",
}

CPP_STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
LOCALIZE_CALL_RE = re.compile(r"\b(?:Localize|Localizable)\s*\(")
REGISTER_CALL_RE = re.compile(r"\bRegister\s*\(")
CONFIG_MACRO_CALL_RE = re.compile(r"\bMACRO_CONFIG_(?:INT|COL|STR)\s*\(")
CONFIG_MACRO_HELP_HEADERS = (
    "src/engine/shared/config_variables.h",
    "src/engine/shared/config_variables_qmclient.h",
    "src/engine/shared/config_variables_tclient.h",
)
CONFIG_OR_COMMAND_TOKEN_RE = re.compile(r"^(?:\+?[a-z][a-z0-9_./:-]*|[A-Z0-9_./:-]+)$")
PATH_OR_URL_RE = re.compile(
    r"^(?:https?://|[a-z0-9_./-]+\.(?:cfg|csv|exe|json|png|txt|toml|wav|webp|zip)|"
    r"(?:data|qmclient|maps|skins|ui|gui|audio|assets)/)",
    re.IGNORECASE,
)
LOG_OR_EVENT_RE = re.compile(
    r"(?:^event=|(?:^|[ _-])error(?:=|:)|(?:^|[ _-])failed(?:=|:)|"
    r"(?:^|[ _-])duration_ms=|(?:^|[ _-])operation=|%p|0x%0)",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class SourceKeyRecord:
    key: str
    category: str
    source: Path | None = None
    context: str = ""
    line: int = 0

    def identity(self) -> tuple[str, str]:
        return (self.key, self.context)


@dataclass(frozen=True)
class SourceKeySummary:
    localize_or_localizable: int
    register_help: int
    indirect: int
    extra: int
    cjk: int
    total_records: int
    total_unique: int


@dataclass(frozen=True)
class StringAuditRecord:
    file: Path
    line: int
    text: str
    category: str
    reason: str


@dataclass(frozen=True)
class StringAuditReport:
    must_i18n: list[StringAuditRecord]
    business_data: list[StringAuditRecord]
    test_only: list[StringAuditRecord]
    needs_review: list[StringAuditRecord]
    violation: list[StringAuditRecord]

    def summary(self) -> dict[str, int]:
        return {
            "must_i18n": len(self.must_i18n),
            "business_data": len(self.business_data),
            "test_only": len(self.test_only),
            "needs_review": len(self.needs_review),
            "violation": len(self.violation),
        }


EXTRA_LOCALIZE_STRINGS = {
    "%c Team %d",
    "%d players",
    "%d teams",
    "- Save codes in order:",
    "- Save owners in order:",
    "- You have %d saves on this map!",
    "Axiom auto login failed",
    "Axiom auto login failed, retrying",
    "Axiom auto login succeeded",
    "Auto reply",
    "Hold left click for free camera",
    "Live director",
    "No director players available",
    "Pet",
    "QmClient",
    "Save failed!",
    "Team save in progress. You'll be able to load with '/load %s'",
    "Team save in progress. You'll be able to load with '/load %s' if save is successful or with '/load %s' if it fails",
    "Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' on %s to continue",
    "Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' to continue",
    "Temporary free camera",
    "Trying Axiom auto login",
    "Update notice",
    "You are already on the latest version",
    "Your current version is outdated. Please update from the QQ group.",
    "_ or ' ' = blank spacer",
    "a = View angle",
    "c = Player position",
    "d = Prediction latency",
    "f = Frame rate",
    "i = Receive rate",
    "j = Latency jitter",
    "k = Resend loss",
    "l = Local time",
    "n = Prediction latency",
    "o = Send rate",
    "p = Ping latency",
    "q = Connection quality",
    "r = Race time",
    "u = Snapshot latency",
    "v = Velocity",
    "x = DDNet CPU% / total CPU%",
    "y = DDNet memory usage",
    "z = Zoom",
}


def decode_cpp_string(value: str) -> str:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", SyntaxWarning)
        return ast.literal_eval(f'"{value}"')


def normalize_language_key(value: str) -> str:
    return value.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")


def decode_language_key(value: str) -> str:
    return normalize_language_key(decode_cpp_string(value))


def _line_number(content: str, index: int) -> int:
    return content.count("\n", 0, index) + 1


def strip_cpp_comments(content: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(content):
        ch = content[i]
        if ch == '"':
            out.append(ch)
            i += 1
            while i < len(content):
                out.append(content[i])
                if content[i] == "\\" and i + 1 < len(content):
                    i += 1
                    out.append(content[i])
                elif content[i] == '"':
                    i += 1
                    break
                i += 1
        elif ch == "/" and i + 1 < len(content) and content[i + 1] == "/":
            i += 2
            while i < len(content) and content[i] not in "\r\n":
                i += 1
        elif ch == "/" and i + 1 < len(content) and content[i + 1] == "*":
            i += 2
            while i + 1 < len(content) and not (
                content[i] == "*" and content[i + 1] == "/"
            ):
                if content[i] in "\r\n":
                    out.append(content[i])
                i += 1
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def _find_matching_delimiter(
    content: str, open_index: int, open_char: str, close_char: str
) -> int:
    depth = 1
    i = open_index + 1
    while i < len(content):
        ch = content[i]
        if ch == '"':
            i += 1
            while i < len(content):
                if content[i] == "\\" and i + 1 < len(content):
                    i += 2
                    continue
                if content[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if ch == open_char:
            depth += 1
        elif ch == close_char:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def _find_matching_paren(content: str, open_index: int) -> int:
    return _find_matching_delimiter(content, open_index, "(", ")")


def _split_top_level_args(arg_src: str) -> list[str]:
    args: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    i = 0
    while i < len(arg_src):
        ch = arg_src[i]
        if ch == '"':
            i += 1
            while i < len(arg_src):
                if arg_src[i] == "\\" and i + 1 < len(arg_src):
                    i += 2
                    continue
                if arg_src[i] == '"':
                    break
                i += 1
        elif ch == "(":
            paren_depth += 1
        elif ch == ")":
            paren_depth -= 1
        elif ch == "[":
            bracket_depth += 1
        elif ch == "]":
            bracket_depth -= 1
        elif ch == "{":
            brace_depth += 1
        elif ch == "}":
            brace_depth -= 1
        elif ch == "," and paren_depth == 0 and bracket_depth == 0 and brace_depth == 0:
            args.append(arg_src[start:i].strip())
            start = i + 1
        i += 1
    tail = arg_src[start:].strip()
    if tail:
        args.append(tail)
    return args


def _decode_string_argument(argument: str) -> str | None:
    argument = argument.strip()
    if len(argument) < 2 or not argument.startswith('"') or not argument.endswith('"'):
        return None
    return decode_language_key(argument[1:-1])


def _extract_string_literals(argument: str) -> list[str]:
    return [
        decode_language_key(value) for value in CPP_STRING_LITERAL_RE.findall(argument)
    ]


def _is_inside_string_literal(content: str, index: int) -> bool:
    in_string = False
    line_start = content.rfind("\n", 0, index)
    i = 0 if line_start == -1 else line_start + 1
    while i < index:
        if content[i] == '"':
            in_string = not in_string
            i += 1
            continue
        if in_string and content[i] == "\\" and i + 1 < index:
            i += 2
            continue
        i += 1
    return in_string


def extract_localize_keys(content: str) -> set[tuple[str, str]]:
    return {
        (record.key, record.context) for record in extract_localize_key_records(content)
    }


def extract_localize_key_records(content: str) -> list[SourceKeyRecord]:
    records: list[SourceKeyRecord] = []
    seen: set[tuple[str, str, int]] = set()
    for match in LOCALIZE_CALL_RE.finditer(content):
        if _is_inside_string_literal(content, match.start()):
            continue
        open_paren = content.find("(", match.start())
        close_paren = _find_matching_paren(content, open_paren)
        if close_paren == -1:
            continue
        args = _split_top_level_args(content[open_paren + 1 : close_paren])
        if not args:
            continue
        context = ""
        if len(args) > 1:
            decoded_context = _decode_string_argument(args[1])
            if decoded_context is not None:
                context = decoded_context
        line = _line_number(content, match.start())
        for key in _extract_string_literals(args[0]):
            identity = (key, context, line)
            if identity in seen:
                continue
            seen.add(identity)
            records.append(
                SourceKeyRecord(key, "localize_or_localizable", None, context, line)
            )
    return records


def extract_qm_card_registry_records(content: str) -> list[SourceKeyRecord]:
    records: list[SourceKeyRecord] = []
    entry_re = re.compile(r'^\s*\{\s*"(?:qm|tclient|deck):', re.MULTILINE)
    for match in entry_re.finditer(content):
        open_brace = content.find("{", match.start())
        close_brace = _find_matching_delimiter(content, open_brace, "{", "}")
        if close_brace == -1:
            continue
        args = _split_top_level_args(content[open_brace + 1 : close_brace])
        if len(args) < 6:
            continue
        line_number = _line_number(content, match.start())
        for index in (4, 6):
            if index >= len(args):
                continue
            literals = _extract_string_literals(args[index])
            if len(literals) == 1 and literals[0]:
                records.append(
                    SourceKeyRecord(
                        literals[0], "card_registry", None, "", line_number
                    )
                )
    return records


def extract_qm_runtime_card_records(content: str) -> list[SourceKeyRecord]:
    records: list[SourceKeyRecord] = []
    for match in re.finditer(r"\bAddCard\s*\(", content):
        open_paren = content.find("(", match.start())
        close_paren = _find_matching_paren(content, open_paren)
        if close_paren == -1:
            continue
        args = _split_top_level_args(content[open_paren + 1 : close_paren])
        if len(args) < 4:
            continue
        stable_id = _decode_string_argument(args[1])
        if stable_id is None or not stable_id.startswith("qm:"):
            continue
        line_number = _line_number(content, match.start())
        for index in (2, 3):
            key = _decode_string_argument(args[index])
            if key:
                records.append(
                    SourceKeyRecord(key, "card_runtime", None, "", line_number)
                )
    return records


def extract_register_help_strings(content: str) -> set[str]:
    keys: set[tuple[str, str]] = set()
    for record in extract_register_help_records(content):
        keys.add((record.key, record.context))
    return {key for key, _context in keys}


def extract_register_help_records(content: str) -> list[SourceKeyRecord]:
    records: list[SourceKeyRecord] = []
    seen: set[tuple[str, int]] = set()
    for match in REGISTER_CALL_RE.finditer(content):
        if _is_inside_string_literal(content, match.start()):
            continue
        open_paren = content.find("(", match.start())
        close_paren = _find_matching_paren(content, open_paren)
        if close_paren == -1:
            continue
        args = _split_top_level_args(content[open_paren + 1 : close_paren])
        if len(args) < 6:
            continue
        help_text = _decode_string_argument(args[5])
        if help_text:
            line = _line_number(content, match.start())
            identity = (help_text, line)
            if identity in seen:
                continue
            seen.add(identity)
            records.append(SourceKeyRecord(help_text, "register_help", None, "", line))
    return records


def extract_function_body(content: str, function_name: str) -> str:
    match = re.search(rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*\{{", content)
    if not match:
        return ""
    start = match.end()
    depth = 1
    i = start
    while i < len(content) and depth > 0:
        if content[i] == "{":
            depth += 1
        elif content[i] == "}":
            depth -= 1
        i += 1
    return content[start : i - 1]


def extract_known_indirect_strings(path: Path, content: str) -> set[str]:
    return {record.key for record in extract_known_indirect_records(path, content)}


def extract_known_indirect_records(path: Path, content: str) -> list[SourceKeyRecord]:
    records: list[SourceKeyRecord] = []
    normalized = _normalized_relpath(path)
    string_literal = r'"((?:[^"\\]|\\.)*)"'

    if normalized.endswith("src/game/client/components/qmclient/menus_qmclient.cpp"):
        localized_wrapper_re = re.compile(
            r"\b(?:RenderCheckbox|RenderDropDown|RenderIntOption|RenderLabel|RenderLyricsSlider|"
            r"RenderLyricSlider|RenderPassword|RenderSection|RenderSlider|RenderText|RenderValue)\s*\("
        )
        for match in localized_wrapper_re.finditer(content):
            if _is_inside_string_literal(content, match.start()):
                continue
            open_paren = content.find("(", match.start())
            close_paren = _find_matching_paren(content, open_paren)
            if close_paren == -1:
                continue
            line_number = _line_number(content, match.start())
            for argument in _split_top_level_args(content[open_paren + 1 : close_paren]):
                key = _decode_string_argument(argument)
                if key and looks_like_localized_wrapper_label(key):
                    records.append(
                        SourceKeyRecord(key, "indirect", path, "", line_number)
                    )

        helper_pattern = re.compile(
            rf"DoFocus(?:SectionLabel|Checkbox)\([^;\n]*,\s*{string_literal}\s*\)"
        )
        for match in helper_pattern.finditer(content):
            records.append(
                SourceKeyRecord(
                    decode_language_key(match.group(1)),
                    "indirect",
                    path,
                    "",
                    _line_number(content, match.start()),
                )
            )

    if normalized.endswith("src/game/client/components/tclient/menus_tclient.cpp"):
        for match in re.finditer(r"\bAddCard\s*\(", content):
            if _is_inside_string_literal(content, match.start()):
                continue
            open_paren = content.find("(", match.start())
            close_paren = _find_matching_paren(content, open_paren)
            if close_paren == -1:
                continue
            args = _split_top_level_args(content[open_paren + 1 : close_paren])
            if len(args) < 2:
                continue
            title = _decode_string_argument(args[1])
            if title:
                records.append(
                    SourceKeyRecord(
                        title,
                        "indirect",
                        path,
                        "",
                        _line_number(content, match.start()),
                    )
                )

    if normalized.endswith(
        "src/game/client/components/qmclient/monitoring/monitoring.cpp"
    ):
        for function_name in (
            "LocalizeGradeSummary",
            "LocalizeCauseDetail",
            "GradeBadgeText",
        ):
            body = extract_function_body(content, function_name)
            for value in re.findall(rf"return\s+{string_literal}\s*;", body):
                match = re.search(re.escape(value), body)
                records.append(
                    SourceKeyRecord(
                        decode_language_key(value),
                        "indirect",
                        path,
                        "",
                        _line_number(
                            content,
                            content.find(body) + (match.start() if match else 0),
                        ),
                    )
                )
        for match in re.finditer(
            rf"\{{\s*{string_literal}\s*,\s*m_Snapshot\.", content
        ):
            records.append(
                SourceKeyRecord(
                    decode_language_key(match.group(1)),
                    "indirect",
                    path,
                    "",
                    _line_number(content, match.start()),
                )
            )
        for match in re.finditer(
            rf"\{{\s*{string_literal}\s*,\s*a[A-Za-z]+Buf\s*\}}", content
        ):
            records.append(
                SourceKeyRecord(
                    decode_language_key(match.group(1)),
                    "indirect",
                    path,
                    "",
                    _line_number(content, match.start()),
                )
            )

    if normalized.endswith(
        "src/game/client/components/qmclient/hud_notifications/hud_notification_static_rules.h"
    ):
        static_rule_pattern = re.compile(
            rf"\bX\(\s*{string_literal}\s*,\s*{string_literal}\s*\)"
        )
        for match in static_rule_pattern.finditer(content):
            records.append(
                SourceKeyRecord(
                    decode_language_key(match.group(2)),
                    "indirect",
                    path,
                    "",
                    _line_number(content, match.start()),
                )
            )

    if normalized.endswith(
        "src/game/client/components/qmclient/hud_notifications/hud_notification_catalog.cpp"
    ):
        catalog_pattern = re.compile(
            rf"\{{\s*EServerMessageRoute::[A-Za-z]+,\s*"
            rf"EServerMessageClass::[A-Za-z]+,\s*"
            rf"EServerMessageDomain::[A-Za-z]+,\s*"
            rf"(?:true|false),\s*{string_literal}\s*\}}"
        )
        for match in catalog_pattern.finditer(content):
            if not match.group(1):
                continue
            records.append(
                SourceKeyRecord(
                    decode_language_key(match.group(1)),
                    "indirect",
                    path,
                    "",
                    _line_number(content, match.start()),
                )
            )

    if normalized.endswith("src/game/client/components/qmclient/qm_music_hook_registry.h"):
        # 音乐 Hook 注册表条目:{&g_Config.m_..., "按钮 id", "显示文案", L"进程名"}
        hook_entry_re = re.compile(r"\{\s*&g_Config\.m_[A-Za-z0-9_]+\s*,")
        for match in hook_entry_re.finditer(content):
            open_brace = content.find("{", match.start())
            close_brace = content.find("}", open_brace)
            if close_brace == -1:
                continue
            args = _split_top_level_args(content[open_brace + 1 : close_brace])
            for index in (1, 2):
                if index >= len(args):
                    continue
                literals = _extract_string_literals(args[index])
                if len(literals) == 1 and literals[0]:
                    records.append(
                        SourceKeyRecord(
                            literals[0],
                            "indirect",
                            path,
                            "",
                            _line_number(content, match.start()),
                        )
                    )

    if any(normalized.endswith(header) for header in CONFIG_MACRO_HELP_HEADERS):
        for match in CONFIG_MACRO_CALL_RE.finditer(content):
            open_paren = content.find("(", match.start())
            close_paren = _find_matching_paren(content, open_paren)
            if close_paren == -1:
                continue
            args = _split_top_level_args(content[open_paren + 1 : close_paren])
            if not args:
                continue
            description = _decode_string_argument(args[-1])
            if not description:
                continue
            records.append(
                SourceKeyRecord(
                    description,
                    "indirect",
                    path,
                    "",
                    _line_number(content, match.start()),
                )
            )

    if normalized.endswith("src/game/client/components/tclient/statusbar.cpp"):
        body = extract_function_body(content, "ConnectionGradeLabel")
        for value in re.findall(rf"return\s+{string_literal}\s*;", body):
            match = re.search(re.escape(value), body)
            records.append(
                SourceKeyRecord(
                    decode_language_key(value),
                    "indirect",
                    path,
                    "",
                    _line_number(
                        content, content.find(body) + (match.start() if match else 0)
                    ),
                )
            )

    if normalized.endswith("src/game/client/components/menus.h"):
        body = extract_function_body(content, "AssetsEditorColorBlendModeName")
        for value in re.findall(rf"return\s+{string_literal}\s*;", body):
            match = re.search(re.escape(value), body)
            records.append(
                SourceKeyRecord(
                    decode_language_key(value),
                    "indirect",
                    path,
                    "Assets editor blend mode",
                    _line_number(
                        content, content.find(body) + (match.start() if match else 0)
                    ),
                )
            )

    if normalized.endswith("src/game/client/components/tclient/statusbar.h"):
        for match in re.finditer(r"\bCStatusItem\s*\(", content):
            open_paren = content.find("(", match.start())
            close_paren = _find_matching_paren(content, open_paren)
            if close_paren == -1:
                continue
            call_args = content[open_paren + 1 : close_paren]
            for index, literal_match in enumerate(
                CPP_STRING_LITERAL_RE.finditer(call_args)
            ):
                if index == 0:
                    continue
                value = decode_language_key(literal_match.group(1))
                if not value.strip():
                    continue
                records.append(
                    SourceKeyRecord(
                        value,
                        "indirect",
                        path,
                        "",
                        _line_number(content, open_paren + 1 + literal_match.start()),
                    )
                )

    if normalized.endswith("src/game/client/components/tclient/menus_tclient.cpp"):
        for pattern in (
            rf"\bS\.m_pName\s*=\s*{string_literal}",
            rf"\bRenderBoxedFullSection\(\s*{string_literal}\s*,",
            rf"\bConfigureSplitCachedStaticLayer\([^;]*,\s*{string_literal}\s*,",
        ):
            for match in re.finditer(pattern, content, re.S):
                records.append(
                    SourceKeyRecord(
                        decode_language_key(match.group(1)),
                        "indirect",
                        path,
                        "",
                        _line_number(content, match.start(1)),
                    )
                )

    unique: dict[tuple[str, str, Path | None, int], SourceKeyRecord] = {}
    for record in records:
        unique[(record.key, record.context, record.source, record.line)] = record
    return sorted(
        unique.values(),
        key=lambda record: (
            record.source.as_posix() if record.source else "",
            record.line,
            record.key.casefold(),
        ),
    )


def iter_source_files(paths: tuple[Path, ...] = SOURCE_PATHS) -> list[Path]:
    files: list[Path] = []
    for root in paths:
        if root.is_file():
            files.append(root)
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix in SOURCE_EXTENSIONS:
                files.append(path)
    return sorted(files)


def read_source_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="utf-8-sig")


def collect_source_key_records(
    paths: tuple[Path, ...] = SOURCE_PATHS,
    extra_strings: set[str] | None = None,
) -> list[SourceKeyRecord]:
    records: list[SourceKeyRecord] = []
    for key in sorted(
        EXTRA_LOCALIZE_STRINGS if extra_strings is None else extra_strings
    ):
        records.append(SourceKeyRecord(key, "extra", None))
    for path in iter_source_files(paths):
        content = strip_cpp_comments(read_source_text(path))
        records.extend(
            SourceKeyRecord(
                record.key, record.category, path, record.context, record.line
            )
            for record in extract_localize_key_records(content)
        )
        records.extend(
            SourceKeyRecord(
                record.key, record.category, path, record.context, record.line
            )
            for record in extract_register_help_records(content)
        )
        records.extend(
            SourceKeyRecord(
                record.key, record.category, path, record.context, record.line
            )
            for record in extract_qm_card_registry_records(content)
        )
        records.extend(
            SourceKeyRecord(
                record.key, record.category, path, record.context, record.line
            )
            for record in extract_qm_runtime_card_records(content)
        )
        records.extend(extract_known_indirect_records(path, content))
    return sorted(
        records,
        key=lambda record: (
            record.key.casefold(),
            record.category,
            record.source.as_posix() if record.source else "",
            record.line,
        ),
    )


def _source_record_sort_key(record: SourceKeyRecord) -> tuple[str, str, str, int, str]:
    return (
        record.key.casefold(),
        record.category,
        record.source.as_posix() if record.source else "",
        record.line,
        record.context,
    )


def _extra_source_key_records(
    extra_strings: set[str] | None = None,
) -> list[SourceKeyRecord]:
    return [
        SourceKeyRecord(key, "extra", None)
        for key in sorted(
            EXTRA_LOCALIZE_STRINGS if extra_strings is None else extra_strings
        )
    ]


def collect_file_source_key_records(path: Path) -> list[SourceKeyRecord]:
    if not path.exists() or path.suffix not in SOURCE_EXTENSIONS:
        return []
    content = strip_cpp_comments(read_source_text(path))
    records: list[SourceKeyRecord] = []
    records.extend(
        SourceKeyRecord(record.key, record.category, path, record.context, record.line)
        for record in extract_localize_key_records(content)
    )
    records.extend(
        SourceKeyRecord(record.key, record.category, path, record.context, record.line)
        for record in extract_register_help_records(content)
    )
    records.extend(
        SourceKeyRecord(record.key, record.category, path, record.context, record.line)
        for record in extract_qm_card_registry_records(content)
    )
    records.extend(
        SourceKeyRecord(record.key, record.category, path, record.context, record.line)
        for record in extract_qm_runtime_card_records(content)
    )
    records.extend(extract_known_indirect_records(path, content))
    return sorted(records, key=_source_record_sort_key)


def write_source_record_cache(path: Path, records: list[SourceKeyRecord]) -> None:
    data = [
        {
            "key": record.key,
            "category": record.category,
            "source": _normalized_relpath(record.source) if record.source else None,
            "context": record.context,
            "line": record.line,
        }
        for record in sorted(records, key=_source_record_sort_key)
    ]
    path.write_text(
        json.dumps({"records": data}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def read_source_record_cache(path: Path) -> list[SourceKeyRecord]:
    data = json.loads(path.read_text(encoding="utf-8"))
    records: list[SourceKeyRecord] = []
    for item in data.get("records", []):
        source = item.get("source")
        records.append(
            SourceKeyRecord(
                item["key"],
                item.get("category", "localize_or_localizable"),
                None
                if source is None
                else (
                    PROJECT_ROOT / source
                    if not Path(source).is_absolute()
                    else Path(source)
                ),
                item.get("context", ""),
                int(item.get("line", 0)),
            )
        )
    return sorted(records, key=_source_record_sort_key)


def _changed_file_key(path: Path) -> str:
    return _normalized_relpath(path)


def merge_source_key_records_for_changed_files(
    cached_records: list[SourceKeyRecord],
    changed_files: tuple[Path, ...],
    extra_strings: set[str] | None = None,
) -> list[SourceKeyRecord]:
    changed_keys = {_changed_file_key(path) for path in changed_files}
    merged = [
        record
        for record in cached_records
        if record.source is None or _changed_file_key(record.source) not in changed_keys
    ]
    for path in changed_files:
        merged.extend(collect_file_source_key_records(path))
    merged = [record for record in merged if record.category != "extra"]
    merged.extend(_extra_source_key_records(extra_strings))
    return sorted(merged, key=_source_record_sort_key)


def _git_changed_paths() -> list[str]:
    commands = (
        ("git", "diff", "--name-only"),
        ("git", "diff", "--cached", "--name-only"),
        ("git", "ls-files", "--others", "--exclude-standard"),
    )
    names: set[str] = set()
    for command in commands:
        result = subprocess.run(
            command,
            cwd=PROJECT_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        names.update(
            line.strip() for line in result.stdout.splitlines() if line.strip()
        )
    return sorted(names)


def git_changed_language_source_files() -> tuple[Path, ...]:
    paths: list[Path] = []
    for name in _git_changed_paths():
        path = PROJECT_ROOT / name
        if path.suffix in SOURCE_EXTENSIONS or (
            path.suffix == ".py"
            and _normalized_relpath(path).startswith(
                "qmclient_scripts/languages_qmclient/"
            )
        ):
            paths.append(path)
    return tuple(sorted(paths))


def extractor_logic_changed(changed_files: tuple[Path, ...]) -> bool:
    return any(
        _normalized_relpath(path) in EXTRACTOR_LOGIC_FILES for path in changed_files
    )


def collect_incremental_source_key_records(
    cache_path: Path = SOURCE_RECORD_CACHE_FILE,
    changed_files: tuple[Path, ...] | None = None,
    extra_strings: set[str] | None = None,
) -> tuple[list[SourceKeyRecord], tuple[Path, ...], bool]:
    if changed_files is None:
        try:
            changed_files = git_changed_language_source_files()
        except (subprocess.CalledProcessError, FileNotFoundError):
            changed_files = ()
            cached_records = []
        else:
            cached_records = (
                read_source_record_cache(cache_path) if cache_path.exists() else []
            )
    else:
        cached_records = (
            read_source_record_cache(cache_path) if cache_path.exists() else []
        )

    if not cached_records:
        records = collect_source_key_records(extra_strings=extra_strings)
        return records, changed_files, True

    if extractor_logic_changed(changed_files):
        records = collect_source_key_records(extra_strings=extra_strings)
        return records, changed_files, True

    records = merge_source_key_records_for_changed_files(
        cached_records, changed_files, extra_strings=extra_strings
    )
    return records, changed_files, False


def has_cjk(value: str) -> bool:
    return any("\u3400" <= ch <= "\u4dbf" or "\u4e00" <= ch <= "\u9fff" for ch in value)


def looks_human_readable(value: str) -> bool:
    if not value or len(value.strip()) < 2:
        return False
    if has_cjk(value):
        return True
    return " " in value and any(ch.isalpha() for ch in value)


def looks_like_localized_wrapper_label(value: str) -> bool:
    text = value.strip()
    if looks_human_readable(text):
        return True
    if text in {"d", "ms", "s", "deg", "%", "px", ""}:
        return False
    if CONFIG_OR_COMMAND_TOKEN_RE.match(text) and any(
        token in text for token in ("_", "/", ".", ":", "-")
    ):
        return False
    return len(text) >= 2 and bool(re.fullmatch(r"[A-Za-z][A-Za-z+]*", text))


def _looks_like_business_literal(value: str) -> tuple[bool, str]:
    text = value.strip()
    if not text:
        return True, "empty or whitespace literal"
    if "&&" in text or "||" in text:
        return True, "source expression string fragment"
    if re.fullmatch(
        r"(?:[&|=!<>]=?|&&|\|\|)\s*[A-Za-z_][A-Za-z0-9_]*\s*(?:[=!<>]=?)?", text
    ):
        return True, "source expression string fragment"
    if text.startswith(("<", "</", "<!doctype", ".")) or any(
        token in text for token in ("</", "<div", "<style", "font-family:", "class=")
    ):
        return True, "HTML or CSS template fragment"
    if PATH_OR_URL_RE.search(text):
        return True, "path, file name, or URL data"
    if "/" in text and ";" in text:
        return True, "MIME type or protocol metadata"
    if LOG_OR_EVENT_RE.search(text):
        return True, "log or telemetry payload"
    if (
        re.search(r"\b(?:invalid|unknown|failed|error)\b", text, re.IGNORECASE)
        and "%" in text
    ):
        return True, "diagnostic or assertion message"
    if re.search(r"\b[a-z][a-z0-9_]*=%", text) or re.search(
        r"\b[a-z][a-z0-9_]*=%[0-9.]*[sdifu]", text
    ):
        return True, "log or telemetry key/value payload"
    if (
        re.fullmatch(r"[a-z][a-z0-9_:-]*(?:\s+(?:%[sdif]|[a-z0-9_:-]+))+", text)
        and "%" in text
    ):
        return True, "console command format template"
    if CONFIG_OR_COMMAND_TOKEN_RE.match(text) and (
        "_" in text or "/" in text or "." in text or text.startswith("+")
    ):
        return True, "command, config, or machine token"
    if text.startswith("/") and any(ch in text for ch in ("%d", "%s", " ")):
        return True, "chat or console command template"
    if re.fullmatch(
        r"(?:chai|say|vote|force_vote|remove_vote|add_vote|add_bindwheel|war_[a-z_]+|remove_war_[a-z_]+|bindchat)\b.*",
        text,
    ):
        return True, "chat, vote, or bind command data"
    if re.search(r"\b(?:bind|exec|toggle|unbind)\b", text) and (
        "\\" in text or '"' in text or text.startswith(("bind ", "exec "))
    ):
        return True, "console command template"
    if text.endswith((" Bps", " KiB", " MiB")) or re.fullmatch(
        r"[%0-9. *()+/\-,:↓↑]+[a-zA-Z%]*", text
    ):
        return True, "numeric display format template"
    if "%" in text and re.fullmatch(r"[\s%0-9A-Za-z\[\]().:|+*/,_-]+", text):
        return True, "display formatting template"
    if text.startswith(("[", "{")) and any(ch in text for ch in (":", "=")):
        return True, "structured payload template"
    if "Localize(" in text or "LoadingDotsCount" in text:
        return True, "string literal extractor fragment"
    if text in {
        "%s: %s",
        "%s: ",
        "%s> ",
        "rcon> ",
        "> %s",
        "— %s",
        "*** %s",
        "%s | %s",
        "%s | %s | %s",
        "%s (%d)",
        "[%s] %s",
        "[%s] [%s] %s",
        "%s = %s",
        "%s / %s",
        "%s: %d",
        "%s（/%s %s）",
        "%s: %s – %s",
        "#%d  %s – %s",
        "0d 00:00:00",
        "00d 00:00:00",
        "000d 00:00:00",
        " KiB",
        " on ",
        "rcon> ",
    }:
        return True, "display formatting template"
    return False, ""


def _line_looks_like_business_data(line_text: str) -> tuple[bool, str]:
    stripped = line_text.strip()
    if "Localize(" in stripped:
        return True, "localization context metadata"
    if any(
        token in stripped
        for token in (
            "log_info(",
            "log_error(",
            "log_warn(",
            "log_debug(",
            "log_trace(",
            "dbg_msg(",
            "dbg_assert(",
            "dbg_assert_failed(",
            "static_assert(",
            "QmPerfLogPayload(",
            "LogPerfStage(",
            "LogControlsPerfStage(",
            "Console()->Register(",
            "Console()->Chain(",
            "Console()->ExecuteLine(",
            "SetError(",
            "str_find(",
            "str_find_nocase(",
            "str_comp(",
            "str_comp_num(",
            "str_comp_nocase(",
            "str_comp_nocase_num(",
            "str_startswith(",
            "str_startswith_nocase(",
            "str_endswith(",
            "str_endswith_nocase(",
            "std::string(",
            "Json[",
            "Writer.WriteAttribute(",
            "Storage()->",
            "FileExists(",
            "FolderExists(",
        )
    ):
        return True, "machine/log/matcher call argument"
    if re.search(r"\bstr_format\(\s*a[A-Za-z]*(?:Extra|Payload|Debug|Perf)", stripped):
        return True, "debug or telemetry format payload"
    if re.search(
        r"\bstr_format\(\s*a[A-Za-z]*(?:Buf|Cmd|Command|Error|Payload)", stripped
    ):
        return True, "command, diagnostic, or payload format template"
    if re.search(
        r"\bstr_format\(\s*a[A-Za-z]*(?:Extra|Focus|Warmup|Gate|Miss|Request)", stripped
    ):
        return True, "debug or telemetry format payload"
    if (
        "QmPerfAppendJsonField(" in stripped
        or "QmPerfAppendPayloadJsonFields(" in stripped
    ):
        return True, "telemetry JSON field data"
    return False, ""


def summarize_source_key_records(records: list[SourceKeyRecord]) -> SourceKeySummary:
    unique_identities = {record.identity() for record in records}
    unique_keys = {key for key, _context in unique_identities}
    return SourceKeySummary(
        localize_or_localizable=sum(
            1 for record in records if record.category == "localize_or_localizable"
        ),
        register_help=sum(
            1 for record in records if record.category == "register_help"
        ),
        indirect=sum(1 for record in records if record.category == "indirect"),
        extra=sum(1 for record in records if record.category == "extra"),
        cjk=sum(1 for key in unique_keys if has_cjk(key)),
        total_records=len(records),
        total_unique=len(unique_identities),
    )


def collect_source_keys() -> list[str]:
    keys: set[str] = set()
    for record in collect_source_key_records():
        keys.add(record.key)
    return sorted(keys)


def collect_source_key_identities() -> set[tuple[str, str]]:
    return {record.identity() for record in collect_source_key_records()}


def _normalized_relpath(path: Path) -> str:
    try:
        return os.path.relpath(path, PROJECT_ROOT).replace("\\", "/")
    except ValueError:
        return path.as_posix()


def _is_test_path(path: Path) -> bool:
    normalized = _normalized_relpath(path)
    name = path.name
    return "/src/test/" in f"/{normalized}" or (
        normalized.startswith("qmclient_scripts/languages_qmclient/")
        and name.startswith("test_")
        and name.endswith(".py")
    )


def _extract_cpp_string_literal_records(content: str) -> list[tuple[str, int]]:
    stripped = strip_cpp_comments(content)
    records: list[tuple[str, int]] = []
    for match in CPP_STRING_LITERAL_RE.finditer(stripped):
        try:
            text = decode_language_key(match.group(1))
        except (SyntaxError, ValueError):
            continue
        records.append((text, _line_number(stripped, match.start())))
    return records


def _extract_python_string_literal_records(content: str) -> list[tuple[str, int]]:
    try:
        tree = ast.parse(content)
    except SyntaxError:
        return []
    records: list[tuple[str, int]] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            records.append(
                (normalize_language_key(node.value), getattr(node, "lineno", 0))
            )
    return records


def _iter_audit_files(paths: tuple[Path, ...]) -> list[Path]:
    files: list[Path] = []
    for root in paths:
        if root.is_file():
            files.append(root)
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix in SOURCE_EXTENSIONS or path.suffix == ".py":
                files.append(path)
    return sorted(files)


def _is_notification_matcher_line(normalized: str, line_text: str) -> bool:
    if not normalized.endswith(
        "src/game/client/components/qmclient/hud_notifications/hud_notification_rules.cpp"
    ):
        return False
    if "Localize(" in line_text:
        return False
    return any(
        token in line_text
        for token in (
            "str_comp(",
            "str_comp_num(",
            "str_startswith(",
            "str_endswith(",
            "str_find(",
            "ExtractWrappedValue(",
            "str_length(",
        )
    )


def _business_data_records_from_path(
    path: Path, content: str
) -> list[StringAuditRecord]:
    normalized = _normalized_relpath(path)
    records: list[StringAuditRecord] = []
    lines = strip_cpp_comments(content).splitlines()

    def add_business(text: str, line: int, reason: str) -> None:
        records.append(StringAuditRecord(path, line, text, "business_data", reason))

    def is_localized_alias(text: str) -> bool:
        token_pattern = re.compile(
            r"(?:constexpr\s+)?(?:const\s+)?char\s*\*\s*(?:const\s+)?"
            r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"((?:[^"\\]|\\.)*)"'
        )
        aliases = {
            name
            for name, encoded in token_pattern.findall("\n".join(lines))
            if decode_language_key(encoded) == text
        }
        return any(f"Localize({name})" in content for name in aliases)

    def appears_as_localize_argument_elsewhere(text: str) -> bool:
        escaped = re.escape(text.replace("\\", "\\\\").replace('"', '\\"'))
        return re.search(rf"Localize\(\s*\"{escaped}\"", content) is not None

    if normalized.endswith(
        "src/game/client/components/qmclient/hud_notifications/hud_notification_static_rules.h"
    ):
        static_rule_pattern = re.compile(
            r'\bX\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)'
        )
        for match in static_rule_pattern.finditer("\n".join(lines)):
            alias = decode_language_key(match.group(1))
            line = _line_number("\n".join(lines), match.start())
            records.append(
                StringAuditRecord(
                    path,
                    line,
                    alias,
                    "business_data",
                    "notification compatibility alias",
                )
            )
        return records

    if normalized.endswith(
        (
            "src/game/client/components/qmclient/hud_notifications/hud_notification_static_alias_rules.h",
            "src/game/client/components/qmclient/hud_notifications/hud_notification_static_upstream_rules.h",
        )
    ):
        static_rule_pattern = re.compile(
            r'\bX\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*[A-Za-z_][A-Za-z0-9_]*\s*\)'
        )
        for match in static_rule_pattern.finditer("\n".join(lines)):
            text = decode_language_key(match.group(1))
            line = _line_number("\n".join(lines), match.start())
            records.append(
                StringAuditRecord(
                    path,
                    line,
                    text,
                    "business_data",
                    "notification static matcher data",
                )
            )
        return records

    if normalized.endswith("src/game/client/components/assets_resource_registry.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "resource alias or registry data",
                    )
                )
        return records

    if normalized.endswith(
        (
            "src/game/client/components/scoreboard.cpp",
            "src/game/client/components/tclient/bindchat.cpp",
            "src/game/client/components/tclient/bindwheel.cpp",
            "src/game/client/components/voting.cpp",
            "src/game/client/components/qmclient/voice/voice_utils.cpp",
            "src/game/client/components/qmclient/monitoring/monitoring_device_perf.cpp",
            "src/game/client/components/tclient/swap_countdown_message.cpp",
            "src/game/client/race_parse.cpp",
            "src/game/client/qm_command_router.cpp",
        )
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if normalized.endswith("src/game/client/components/scoreboard.cpp") and (
                "SoundCategory" in line_text or "say /spec" in text or has_cjk(text)
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "scoreboard sound category or command data",
                    )
                )
            elif normalized.endswith("src/game/client/components/tclient/bindchat.cpp"):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "bindchat command preset data",
                    )
                )
            elif normalized.endswith(
                "src/game/client/components/tclient/bindwheel.cpp"
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "bindwheel command serialization data",
                    )
                )
            elif normalized.endswith("src/game/client/components/voting.cpp") and (
                text.startswith(("force_vote ", "remove_vote ", "add_vote "))
                or text in {"vote yes", "vote no"}
                or text in {"%s图", "DDNet Vote", "No reason given"}
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "vote command, matcher, notification title, or preview fixture data",
                    )
                )
            elif normalized.endswith(
                "src/game/client/components/qmclient/voice/voice_utils.cpp"
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "voice permission matcher data",
                    )
                )
            elif normalized.endswith(
                "src/game/client/components/qmclient/monitoring/monitoring_device_perf.cpp"
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "platform performance counter path",
                    )
                )
            elif normalized.endswith(
                "src/game/client/components/tclient/swap_countdown_message.cpp"
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "swap countdown server message matcher data",
                    )
                )
            elif normalized.endswith("src/game/client/race_parse.cpp"):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "race server message parser data",
                    )
                )
            elif normalized.endswith("src/game/client/qm_command_router.cpp") and (
                line_text.strip().startswith("pConsole->Register(")
                or "Dummy command ignored" in text
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "console command signature or diagnostic data",
                    )
                )
        if records:
            return records

    if normalized.endswith("src/game/client/components/menus_ingame.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                re.fullmatch(r"DDmaX (?:Easy|Next|Pro|Nut)", text)
                or text == "My IGN: %s\\n"
                or "apTypeKeys" in line_text
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "server type key or clipboard data template",
                    )
                )
            elif text in {"DDmaX ", "Address: ddnet://%s\\n", "Map: %s\\n"}:
                add_business(text, line, "clipboard or server type formatting data")
        if records:
            return records

    if normalized.endswith("src/game/client/components/menus_browser.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if (
                re.fullmatch(r"DDmaX (?:Easy|Next|Pro|Nut)", text)
                or text == "%d/5 ★"
                or text == ", SettingsPerfContextName(), "
                or text in {"Address: ddnet://%s\\n", "Map: %s\\n"}
                or "&&" in text
                or "||" in text
            ):
                add_business(
                    text,
                    line,
                    "server browser category, rating, clipboard, or source expression data",
                )
        if records:
            return records

    if normalized.endswith(
        "src/game/client/components/qmclient/hud_notifications/hud_notification_rules.cpp"
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            if not looks_human_readable(text):
                continue
            records.append(
                StringAuditRecord(
                    path,
                    line,
                    text,
                    "business_data",
                    "notification message matcher or compatibility fragment",
                )
            )
        return records

    if normalized.endswith(
        "src/game/client/components/qmclient/translate/translate.cpp"
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            if looks_human_readable(text) or has_cjk(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "translation service prompt or provider data",
                    )
                )
        return records

    if normalized.endswith(
        "src/game/client/components/qmclient/translate/translate_parse.cpp"
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            if looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "translation API response parser diagnostic text",
                    )
                )
        return records

    if (
        "src/game/client/components/qmclient/qm_lyrics/" in normalized
        and normalized.endswith(".cpp")
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if any(
                token in line_text
                for token in (
                    "m_aLastError",
                    "pErr",
                    "str_copy(",
                    "str_format(",
                    "json_object_get(",
                )
            ) and (looks_human_readable(text) or "%" in text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "lyrics API response parser diagnostic text",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/QmUi/QmCardRegistry.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            records.append(
                StringAuditRecord(
                    path,
                    line,
                    text,
                    "business_data",
                    "settings card registry metadata or search keyword data",
                )
            )
        # Card titles that are user-visible are separately covered by Localizable/Localize.
        # Continue with generic rules for the remaining source files.

    if normalized.endswith("src/game/client/components/ghost.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            records.append(
                StringAuditRecord(
                    path, line, text, "business_data", "ghost parser diagnostic text"
                )
            )

    if normalized.endswith("src/game/client/components/qmclient/menus_qmclient.cpp"):
        # 贡献者、服务商、语言名、地图名和歌词预览是运行时数据，不是翻译源文案。
        DataTexts = {
            "栖梦(璇梦),夏日,DYL",
            "腾讯云",
            "中文",
            "日本語",
            "繁體中文",
            "tab=function module=pie_menu",
            "DDmaX Easy",
            "DDmaX Next",
            "DDmaX Pro",
            "DDmaX Nut",
            "AMLL TTML DB",
            "Apple Music",
            "Stop and stare",
            "I think I'm moving but I go nowhere",
            "Yeah I know that everyone gets scared",
        }
        sponsor_array_start = content.find("s_apSponsors[]")
        sponsor_array_end = content.find("};", sponsor_array_start)
        sponsor_line_start = _line_number(content, sponsor_array_start)
        sponsor_line_end = _line_number(content, sponsor_array_end)
        for text, line in _extract_cpp_string_literal_records(content):
            if text in DataTexts or sponsor_line_start <= line <= sponsor_line_end:
                records.append(
                    StringAuditRecord(
                        path, line, text, "business_data", "contributor or preview sample data"
                    )
                )

    if normalized.endswith("src/game/client/gameclient.cpp"):
        DataTexts = {
            "s[tuning] ?f[value]",
            "i[zone] s[tuning] f[value]",
        }
        for text, line in _extract_cpp_string_literal_records(content):
            if text in DataTexts:
                records.append(
                    StringAuditRecord(
                        path, line, text, "business_data", "diagnostic or formatting data"
                    )
                )

    if normalized.endswith("src/game/client/components/menus.cpp"):
        DataTexts = {"sv_motd ", "sv_motd \"", "sv_map Tutorial", "sv_register 0"}
        for text, line in _extract_cpp_string_literal_records(content):
            if text in DataTexts:
                records.append(
                    StringAuditRecord(
                        path, line, text, "business_data", "server command fixture data"
                    )
                )

    if normalized.endswith("src/game/client/components/menus.h"):
        DataTexts = {"活动", "极限", "训练", "娱乐"}
        for text, line in _extract_cpp_string_literal_records(content):
            if text in DataTexts:
                records.append(
                    StringAuditRecord(
                        path, line, text, "business_data", "server game type matcher data"
                    )
                )

    if normalized.endswith("src/game/client/components/menus_browser.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if text == ", SettingsPerfContextName(), ":
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "performance call syntax fragment",
                    )
                )
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "SettingsPerfContextName()" in line_text:
                records.append(
                    StringAuditRecord(
                        path, line, text, "business_data", "performance context metadata"
                    )
                )

    if normalized.endswith("src/game/client/components/qmclient/axiom_auto_login.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "QmTextContainsAny(" in line_text:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "Axiom server login response matcher",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/chat.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                has_cjk(text)
                or text
                in {
                    "DDNet Chat",
                    "Welcome to QmClient",
                    "Let's go!",
                    "Team save in progress. You'll be able to load with '/load *** *** ***'",
                    "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails",
                    "Team successfully saved by ***. Use '/load *** *** ***' to continue",
                    "expected all or team as mode",
                }
                or text == "%s（/%s %s）"
                or "s_aPreviewLines" in line_text
            ):
                add_business(
                    text,
                    line,
                    "chat command preview, preview fixture, or streamer mask data",
                )
        return records

    if normalized.endswith("src/game/client/components/binds.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if any(
                token in line_text
                for token in (
                    "Bind(",
                    "log_info_color(",
                    "dbg_assert(",
                    "ExecuteLine",
                    "str_comp",
                    "str_startswith",
                )
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "bind command, log, or assertion data",
                    )
                )
        return records

    if normalized.endswith("src/game/client/components/debughud.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "debug HUD diagnostic text",
                    )
                )
        return records

    if normalized.endswith("src/game/client/components/console.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if any(
                token in line_text
                for token in (
                    "Html.append(",
                    "str_append(",
                    "str_format(",
                    "ColorCharToTextColor(",
                )
            ) and (looks_human_readable(text) or text.strip()):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "console prompt, export, or formatting template",
                    )
                )
            elif text in {"rcon> ", "xxxx-xx-xx xx:xx:xx x chat/client: — ", " on "}:
                add_business(
                    text, line, "console prompt, export, or formatting template"
                )
        # Continue with generic rules for this file.

    if normalized.endswith(
        "src/game/client/components/qmclient/monitoring/monitoring.cpp"
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            if text in {"avg %.0f ↓%.0f ↑%.0f%s", "avg %.*f ↓%.*f ↑%.*f%s", " KiB"}:
                add_business(text, line, "monitoring numeric display format template")
        if records:
            return records

    if normalized.endswith("src/game/client/components/hud.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if (
                is_localized_alias(text)
                or text
                == "3 Tiles Edge Jump:\\nLeft Jump: .34|.31|.16\\nLeft Double Jump: .41|.28|.25|.13\\nRight Jump: .63|.66|.81\\nRight Double Jump: .56|.69|.72|.84"
                or text in {"%s->%s Swap:%d秒", "%s->%s 可交换!", "开关#%d:%d秒"}
                or text
                in {
                    "Pure Music",
                    "Counting stars under the island",
                    "Dreaming about the things that we could be",
                }
            ):
                add_business(
                    text,
                    line,
                    "HUD config default, preview sample, localized alias, or dynamic display template",
                )
        if records:
            return records

    if normalized.endswith("src/game/client/components/menus_settings.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                text == ",\\nloaded: %"
                or "SetPreviewLine(" in line_text
                or text in {"'%s' entered and joined the game", "Hey, how are you %s?"}
            ):
                add_business(text, line, "settings debug or chat preview fixture text")
        # Continue with generic rules for true UI labels.

    if normalized.endswith("src/game/client/components/menus_settings_assets.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if text == "': Result += ":
                add_business(text, line, "source expression string fragment")
            elif text in {
                "Invalid entity bg preview response",
                "Entity bg preview data is missing",
                "Entity bg preview base url is empty",
                "Invalid workshop response",
                "Workshop api returned error",
                "Workshop asset list is missing",
                "Workshop json parse failed",
                "Workshop request aborted",
                "Workshop request failed",
                "Entity bg preview json parse failed",
                "Entity bg preview request failed",
            }:
                add_business(text, line, "assets workshop or preview diagnostic text")
            elif (
                "str_copy(aError" in line_text
                or "str_copy(aPreviewError" in line_text
                or "str_format(aError" in line_text
                or "str_format(aPreviewError" in line_text
                or "dbg_msg(" in line_text
            ) and looks_human_readable(text):
                add_business(text, line, "assets workshop or preview diagnostic text")
            elif appears_as_localize_argument_elsewhere(text):
                add_business(text, line, "localized toolbar width calculation key")
        if records:
            return records

    if normalized.endswith("src/game/client/components/hud_editor.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if has_cjk(text) or looks_human_readable(text):
                add_business(text, line, "HUD editor sample text")
        return records

    if normalized.endswith("src/game/client/components/jump_hint_utils.h"):
        for text, line in _extract_cpp_string_literal_records(content):
            if "JUMP_HINT_DEFAULT_TEXT" in (
                lines[line - 1] if 0 < line <= len(lines) else ""
            ):
                add_business(text, line, "jump hint default config text")
        if records:
            return records

    if normalized.endswith("src/game/client/components/voting.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if text == "%s图" or text == "DDNet Vote" or text == "No reason given":
                add_business(
                    text,
                    line,
                    "vote matcher, notification title, or preview fixture text",
                )
        if records:
            return records

    if normalized.endswith(
        (
            "src/game/client/components/qmclient/qmclient.cpp",
            "src/game/client/components/tclient/tclient.cpp",
            "src/game/client/components/tclient/menus_tclient.cpp",
        )
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                text
                in {"快醒醒!", "但是", "不过", "然而", "可是", "[rename] ", "[regex] "}
                or "LogQmClient" in line_text
                or text
                in {
                    "auth token updated",
                    "response did not contain auth token",
                    "users payload could not be parsed",
                }
                or text
                in {"Spectate a player", "No reason given", "SollyBunny / bun bun"}
            ):
                add_business(
                    text,
                    line,
                    "auto-reply matcher, command metadata, preview fixture, or QmClient telemetry text",
                )
        if records:
            return records

    if normalized.endswith("src/game/client/gameclient.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                text
                in {
                    "nameless tee",
                    "prediction error",
                    "Team successfully saved by %s. Use '/load %s' to continue",
                    "Team successfully saved by %s. Use '/load %s' on %s to continue",
                }
                or "Console()->Print(" in line_text
            ):
                add_business(
                    text,
                    line,
                    "fallback name, debug log, console diagnostic, or server save message data",
                )
        if records:
            return records

    if normalized.endswith("src/game/client/components/qmclient/voice/voice_core.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                "LogDiagnosticErrorOnce(" in line_text
                or "m_pConsole->Print(" in line_text
                or text
                in {
                    "Failed to open UDP socket",
                    "No output devices available",
                    "No capture devices available",
                    "Input devices:",
                    "Output devices:",
                    "Microphone permission denied on Android",
                }
            ):
                add_business(text, line, "voice diagnostic log or console output text")
        if records:
            return records

    if normalized.endswith("src/game/client/components/skins.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "dbg_assert" in line_text or "m_SkinsUsageList" in text:
                add_business(text, line, "skin manager assertion diagnostic text")
            elif text in {"Default preset", "Server preset"}:
                add_business(text, line, "skin queue built-in preset storage name")
        if records:
            return records

    if normalized.endswith("src/game/client/components/menus.h"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "return " in line_text and any(
                token in text
                for token in (
                    "DDmaX",
                    "古典",
                    "简单图",
                    "中阶图",
                    "高阶",
                    "疯狂",
                    "分身",
                    "单人",
                )
            ):
                add_business(
                    text, line, "server browser normalized game type display name"
                )
        if records:
            return records

    if normalized.endswith("src/game/client/components/system_media_controls.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "APPLE_MUSIC_ARTIST_ALBUM_SEPARATOR" in line_text:
                add_business(text, line, "Apple Music metadata parser separator")
        if records:
            return records

    if "/src/game/client/components/qmclient/qm_lyrics/" in f"/{normalized}":
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if (
                'HeaderString("User-Agent"' in line_text
                or "Error(" in line_text
                or text == "': pOut->append("
            ) and looks_human_readable(text):
                add_business(
                    text,
                    line,
                    "lyrics provider request metadata or internal diagnostic text",
                )
        if records:
            return records

    if normalized.endswith("src/game/client/skin.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if "valid filenames" in text:
                add_business(text, line, "skin validation log diagnostic text")
        if records:
            return records

    if normalized.endswith("src/game/client/components/statboard.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if "flag was captured by" in text:
                add_business(text, line, "statboard server message parser fragment")
        if records:
            return records

    if normalized.endswith("src/game/client/qm_ime_candidate_popup.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if has_cjk(text) or text == "国g":
                add_business(text, line, "IME candidate layout sample text")
        if records:
            return records

    if normalized.endswith("src/game/client/components/qmclient/scripting/impl.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if "Boxed_Value" in text:
                add_business(text, line, "scripting exception diagnostic text")
        if records:
            return records

    if normalized.endswith("src/game/client/sixup_translate_game.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "0.7 protocol compatibility game message data",
                    )
                )
        return records

    if normalized.endswith(
        "src/game/client/components/menus_ingame_touch_controls.cpp"
    ):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "Localize(" in line_text and looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "localization context metadata",
                    )
                )
            elif any(
                token in line_text
                for token in ("dbg_assert(", "dbg_assert_failed(", "static_assert(")
            ) and looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "assertion or compile-time diagnostic text",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/camera.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "Localize(" in line_text and looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "localization context metadata",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/qmclient/menus_qmclient.cpp"):
        module_search_start = content.find("auto ModuleSearchKeywords =")
        module_search_end = content.find("auto ApplyModuleSearch", module_search_start)
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            line_index = content.find(line_text)
            if (
                has_cjk(text)
                and module_search_start != -1
                and line_index != -1
                and (
                    module_search_end == -1
                    or module_search_start <= line_index < module_search_end
                )
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "settings module search keyword data",
                    )
                )
            elif any(
                token in line_text
                for token in (
                    "str_startswith_nocase(",
                    "str_find(",
                    "str_find_nocase(",
                    "str_comp(",
                    "str_comp_nocase(",
                    "str_append(",
                    "CommandBindCache.",
                    "m_Binds.Bind(",
                    "Console()->ExecuteLine(",
                )
            ) and (
                not looks_human_readable(text)
                or text.startswith("[")
                or text.startswith("toggle ")
                or CONFIG_OR_COMMAND_TOKEN_RE.match(text)
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "settings matcher, command, or serialized config token",
                    )
                )
            elif (
                module_search_start != -1
                and line_index != -1
                and (
                    module_search_end == -1
                    or module_search_start <= line_index < module_search_end
                )
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "settings module search keyword data",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/menus_browser.cpp"):
        game_type_tokens = {
            "DDmaX",
            "DDmaX Easy",
            "DDmaX Next",
            "DDmaX Pro",
            "DDmaX Nut",
            "Oldschool",
            "Novice",
            "Moderate",
            "Brutal",
            "Insane",
            "Dummy",
            "Solo",
            "Race",
            "Fun",
            "Event",
            "f-ddrace",
            "freeze",
            "ddracenet",
            "ddnet",
            "0xf",
            "ddrace",
            "mkrace",
            "fastcap",
        }
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if text in game_type_tokens and any(
                token in line_text
                for token in ("str_find_nocase(", "str_comp_nocase(", "return ")
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "server browser game type matcher data",
                    )
                )
            elif text in {'solo; nameless tee; kobra 2"', 'CHN; [A]"'}:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "server browser search example data",
                    )
                )
            elif text in {"Address: ddnet://%s\\n", "Map: %s\\n", "%d/5 ★"}:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "server browser copy/detail formatting template",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/menus_settings_controls.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "Localizable(" in line_text and looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "predefined bind command data",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/menus_settings.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "SetPreviewLine(" in line_text:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "chat settings preview sample data",
                    )
                )
            elif "str_format(" in line_text and "AssetScanStatus" in line_text:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "asset scan diagnostic summary format",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/menus_settings_assets.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if any(
                token in line_text
                for token in (
                    "str_copy(aError",
                    "str_copy(pErr",
                    "str_copy(aPreviewError",
                    "str_format(aError",
                    "str_format(aPreviewError",
                    "dbg_msg(",
                )
            ) and looks_human_readable(text):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "asset workshop or preview diagnostic text",
                    )
                )
            elif "ComputeToolbarButtonWidth(" in line_text and looks_human_readable(
                text
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "localized toolbar layout measurement key",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/tclient/fast_practice.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if "EchoPractice(" in line_text:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "fast practice local command response text",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/tclient/bindchat.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if any(
                token in line_text
                for token in (
                    "Bindchat(",
                    "Console()->Print",
                    "str_format(",
                    "str_startswith(",
                    "str_copy(",
                    "str_append(",
                )
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "bindchat command, config, or console output data",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/voting.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            line_text = lines[line - 1] if 0 < line <= len(lines) else ""
            if any(
                token in line_text
                for token in (
                    "Console()->ExecuteLine",
                    "Console()->Print",
                    "str_format(",
                    "str_append(",
                    "str_copy(",
                )
            ):
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "voting command or console output data",
                    )
                )
        # Continue with generic rules for this file.

    if normalized.endswith("src/game/client/components/menus_ingame.cpp"):
        for text, line in _extract_cpp_string_literal_records(content):
            if text in {"Address: ddnet://%s\\n", "Map: %s\\n", "DDmaX "}:
                records.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        "server detail or game type formatting data",
                    )
                )
        # Continue with generic rules for this file.

    return records


def _string_audit_sort_key(record: StringAuditRecord) -> tuple[str, int, str, str]:
    return (
        _normalized_relpath(record.file),
        record.line,
        record.text.casefold(),
        record.reason,
    )


def _dedupe_audit_records(records: list[StringAuditRecord]) -> list[StringAuditRecord]:
    unique: dict[tuple[str, int, str, str, str], StringAuditRecord] = {}
    for record in records:
        key = (
            _normalized_relpath(record.file),
            record.line,
            record.text,
            record.category,
            record.reason,
        )
        unique[key] = record
    return sorted(unique.values(), key=_string_audit_sort_key)


def build_string_audit_report(
    paths: tuple[Path, ...] = AUDIT_PATHS,
) -> StringAuditReport:
    must_i18n: list[StringAuditRecord] = []
    business_data: list[StringAuditRecord] = []
    test_only: list[StringAuditRecord] = []
    needs_review: list[StringAuditRecord] = []
    violation: list[StringAuditRecord] = []

    claimed: set[tuple[str, int, str]] = set()

    source_key_records = collect_source_key_records(
        paths=tuple(path for path in paths if path.suffix != ".py"),
    )
    for record in source_key_records:
        if record.source is None:
            continue
        cjk_source_key_violation = has_cjk(record.key) and record.category != "indirect"
        audit_record = StringAuditRecord(
            record.source,
            record.line,
            record.key,
            "violation" if cjk_source_key_violation else "must_i18n",
            "source key contains CJK"
            if cjk_source_key_violation
            else f"active source key ({record.category})",
        )
        claimed.add((_normalized_relpath(record.source), record.line, record.key))
        if audit_record.category == "violation":
            violation.append(audit_record)
        else:
            must_i18n.append(audit_record)

        if record.context:
            context_record = StringAuditRecord(
                record.source,
                record.line,
                record.context,
                "business_data",
                "localization context metadata",
            )
            context_key = (
                _normalized_relpath(context_record.file),
                context_record.line,
                context_record.text,
            )
            claimed.add(context_key)
            business_data.append(context_record)

    for path in _iter_audit_files(paths):
        if path.suffix == ".py":
            content = path.read_text(encoding="utf-8")
            literal_records = _extract_python_string_literal_records(content)
        else:
            content = read_source_text(path)
            literal_records = _extract_cpp_string_literal_records(content)

        if _is_test_path(path):
            for text, line in literal_records:
                if not looks_human_readable(text):
                    continue
                key = (_normalized_relpath(path), line, text)
                if key in claimed:
                    continue
                claimed.add(key)
                test_only.append(
                    StringAuditRecord(
                        path, line, text, "test_only", "test fixture or assertion text"
                    )
                )
            continue

        for record in _business_data_records_from_path(path, content):
            key = (_normalized_relpath(record.file), record.line, record.text)
            if key in claimed:
                continue
            claimed.add(key)
            business_data.append(record)

        normalized = _normalized_relpath(path)
        if "/src/game/client/" not in f"/{normalized}":
            continue

        stripped_lines = (
            strip_cpp_comments(content).splitlines() if path.suffix != ".py" else []
        )
        for text, line in literal_records:
            key = (normalized, line, text)
            if key in claimed:
                continue
            literal_is_business, literal_reason = _looks_like_business_literal(text)
            line_text = ""
            if path.suffix != ".py":
                line_text = (
                    stripped_lines[line - 1] if 0 < line <= len(stripped_lines) else ""
                )
            line_is_business, line_reason = _line_looks_like_business_data(line_text)
            if literal_is_business or line_is_business:
                claimed.add(key)
                business_data.append(
                    StringAuditRecord(
                        path,
                        line,
                        text,
                        "business_data",
                        literal_reason or line_reason,
                    )
                )
                continue
            if not looks_human_readable(text):
                continue
            claimed.add(key)
            needs_review.append(
                StringAuditRecord(
                    path,
                    line,
                    text,
                    "needs_review",
                    "unclassified human-readable client string",
                )
            )

    return StringAuditReport(
        _dedupe_audit_records(must_i18n),
        _dedupe_audit_records(business_data),
        _dedupe_audit_records(test_only),
        _dedupe_audit_records(needs_review),
        _dedupe_audit_records(violation),
    )


def _audit_record_file_key(record: StringAuditRecord) -> str:
    return _normalized_relpath(record.file)


def merge_string_audit_report_for_changed_files(
    cached_report: StringAuditReport,
    changed_files: tuple[Path, ...],
) -> StringAuditReport:
    changed_keys = {_normalized_relpath(path) for path in changed_files}
    current_report = build_string_audit_report(paths=changed_files)

    def merge_records(
        old_records: list[StringAuditRecord],
        new_records: list[StringAuditRecord],
    ) -> list[StringAuditRecord]:
        kept = [
            record
            for record in old_records
            if _audit_record_file_key(record) not in changed_keys
        ]
        kept.extend(new_records)
        return _dedupe_audit_records(kept)

    return StringAuditReport(
        merge_records(cached_report.must_i18n, current_report.must_i18n),
        merge_records(cached_report.business_data, current_report.business_data),
        merge_records(cached_report.test_only, current_report.test_only),
        merge_records(cached_report.needs_review, current_report.needs_review),
        merge_records(cached_report.violation, current_report.violation),
    )


def collect_incremental_string_audit_report(
    audit_path: Path = AUDIT_REPORT_FILE,
    changed_files: tuple[Path, ...] | None = None,
) -> tuple[StringAuditReport, tuple[Path, ...], bool]:
    if changed_files is None:
        try:
            changed_files = git_changed_language_source_files()
        except (subprocess.CalledProcessError, FileNotFoundError):
            changed_files = ()

    if not audit_path.exists():
        return build_string_audit_report(), changed_files, True

    cached_report = read_string_audit_report(audit_path)
    if not changed_files:
        return cached_report, changed_files, False

    if extractor_logic_changed(changed_files):
        return build_string_audit_report(), changed_files, True

    return (
        merge_string_audit_report_for_changed_files(cached_report, changed_files),
        changed_files,
        False,
    )


def audit_report_to_dict(report: StringAuditReport) -> dict[str, object]:
    def serialize(records: list[StringAuditRecord]) -> list[dict[str, object]]:
        return [
            {
                "file": _normalized_relpath(record.file),
                "line": record.line,
                "text": record.text,
                "category": record.category,
                "reason": record.reason,
            }
            for record in records
        ]

    return {
        "summary": report.summary(),
        "must_i18n": serialize(report.must_i18n),
        "business_data": serialize(report.business_data),
        "test_only": serialize(report.test_only),
        "needs_review": serialize(report.needs_review),
        "violation": serialize(report.violation),
    }


def write_string_audit_report(path: Path, report: StringAuditReport) -> None:
    path.write_text(
        json.dumps(audit_report_to_dict(report), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def read_string_audit_report(path: Path) -> StringAuditReport:
    data = json.loads(path.read_text(encoding="utf-8"))

    def parse_records(category: str) -> list[StringAuditRecord]:
        return [
            StringAuditRecord(
                PROJECT_ROOT / item["file"]
                if not Path(item["file"]).is_absolute()
                else Path(item["file"]),
                int(item["line"]),
                item["text"],
                item.get("category", category),
                item["reason"],
            )
            for item in data.get(category, [])
        ]

    return StringAuditReport(
        parse_records("must_i18n"),
        parse_records("business_data"),
        parse_records("test_only"),
        parse_records("needs_review"),
        parse_records("violation"),
    )
