#!/usr/bin/env python3
"""过滤 Windows/MSVC 构建日志中的 include 跟踪噪音。"""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        return 1

    prefixes = (
        "注意: 包含文件:".encode("mbcs"),
        "注意: 包含文件:".encode("utf-8"),
        b"Note: including file:",
    )
    with Path(sys.argv[1]).open("rb") as log_file:
        for line in log_file:
            if not any(line.lstrip().startswith(prefix) for prefix in prefixes):
                sys.stdout.buffer.write(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
