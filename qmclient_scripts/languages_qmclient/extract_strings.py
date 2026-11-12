# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""Extract localization source keys for reporting/generation only."""

from __future__ import annotations

import os
import argparse
from pathlib import Path

import source_keys

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
STRINGS_FILE = SCRIPT_DIR / "extracted_strings.txt"
AUDIT_REPORT_FILE = source_keys.AUDIT_REPORT_FILE
SOURCE_RECORD_CACHE_FILE = source_keys.SOURCE_RECORD_CACHE_FILE


def collect_strings() -> list[str]:
    return source_keys.collect_source_keys()


def format_record(record: source_keys.SourceKeyRecord) -> str:
    if record.context:
        return f"[{record.context}]\t{record.key}"
    return record.key


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract QmClient localization source keys."
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="rescan all source files and rebuild the incremental cache",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.full:
        records = source_keys.collect_source_key_records()
        audit_report = source_keys.build_string_audit_report()
        changed_files = ()
        full_scan = True
    else:
        records, changed_files, records_full_scan = (
            source_keys.collect_incremental_source_key_records()
        )
        if records_full_scan:
            audit_report = source_keys.build_string_audit_report()
            audit_full_scan = True
        else:
            audit_report, _audit_changed_files, audit_full_scan = (
                source_keys.collect_incremental_string_audit_report(
                    changed_files=changed_files
                )
            )
        full_scan = records_full_scan or audit_full_scan

    summary = source_keys.summarize_source_key_records(records)
    unique_identities = sorted(
        {(record.key, record.context) for record in records},
        key=lambda item: (item[1].casefold(), item[0].casefold(), item[1], item[0]),
    )
    sorted_strings = [
        format_record(source_keys.SourceKeyRecord(key, "", None, context))
        for key, context in unique_identities
    ]
    with STRINGS_FILE.open("w", encoding="utf-8", newline="\n") as file:
        for key in sorted_strings:
            file.write(key + "\n")
    source_keys.write_source_record_cache(SOURCE_RECORD_CACHE_FILE, records)
    source_keys.write_string_audit_report(AUDIT_REPORT_FILE, audit_report)

    rel_outpath = os.path.relpath(STRINGS_FILE, PROJECT_ROOT)
    rel_audit_path = os.path.relpath(AUDIT_REPORT_FILE, PROJECT_ROOT)
    rel_cache_path = os.path.relpath(SOURCE_RECORD_CACHE_FILE, PROJECT_ROOT)
    mode = "full" if full_scan else "incremental"
    print(f"Source scan mode: {mode}")
    if changed_files:
        print(f"Changed language source files scanned: {len(changed_files)}")
    else:
        print("Changed language source files scanned: 0")
    print(
        f"Extracted {len(sorted_strings)} unique localization strings to {rel_outpath}"
    )
    print(f"Wrote string audit report to {rel_audit_path}")
    print(f"Wrote source record cache to {rel_cache_path}")
    print("Category summary:")
    print(f"  Localize/Localizable records: {summary.localize_or_localizable}")
    print(f"  Register help records: {summary.register_help}")
    print(f"  Card registry records: {summary.card_registry}")
    print(f"  Card runtime records: {summary.card_runtime}")
    print(f"  Indirect records: {summary.indirect}")
    print(f"  Extra records: {summary.extra}")
    print(f"  CJK unique strings: {summary.cjk}")
    print(f"  Total records before dedupe: {summary.total_records}")
    print(f"  Total unique strings: {summary.total_unique}")
    print("Audit summary:")
    for category, count in audit_report.summary().items():
        print(f"  {category}: {count}")
    for index, key in enumerate(sorted_strings, start=1):
        print(f"  {index:3d}. {key}")


if __name__ == "__main__":
    main()
