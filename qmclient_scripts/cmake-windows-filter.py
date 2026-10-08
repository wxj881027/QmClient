# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
import sys
from pathlib import Path


def write_filtered_log(log_path: Path, output, encoding: str = "mbcs") -> None:
    prefixes = (
        "注意: 包含文件:".encode("utf-8"),
        "Note: including file:".encode("ascii"),
    )
    try:
        prefixes += ("注意: 包含文件:".encode(encoding),)
    except (LookupError, UnicodeEncodeError):
        # 英文代码页无法编码中文前缀，非 Windows 平台也可能没有 mbcs。
        pass

    with log_path.open("rb") as log_file:
        for line in log_file:
            if line.lstrip().startswith(prefixes):
                continue
            output.write(line)


def main() -> int:
    if len(sys.argv) != 2:
        return 1

    write_filtered_log(Path(sys.argv[1]), sys.stdout.buffer)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
