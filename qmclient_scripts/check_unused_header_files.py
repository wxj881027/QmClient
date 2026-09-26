# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
import os
import re
import sys


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".ipp",
    ".m",
    ".mm",
}


def iter_source_files(directory):
    for root, _, files in os.walk(directory):
        for file in files:
            if os.path.splitext(file)[1].lower() in SOURCE_EXTENSIONS:
                yield os.path.join(root, file)


def included_headers(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            match = INCLUDE_RE.match(line)
            if match:
                yield os.path.basename(match.group(1))


def find_unused_header_files(directory):
    header_files = set()
    used_files = set()

    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".h"):
                header_files.add(file)

    for path in iter_source_files(directory):
        for header in included_headers(path):
            if header in header_files:
                used_files.add(header)

    used_files.add("pch.h")

    return header_files - used_files


def main():
    directory = "src"
    unused_headers = find_unused_header_files(directory)

    if unused_headers:
        for header in sorted(unused_headers):
            print(f"Error: Header file '{header}' is unused.")
        return 1

    print("Success: No header files are unused.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
