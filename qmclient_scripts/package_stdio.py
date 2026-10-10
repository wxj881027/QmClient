"""Shared UTF-8 console policy for packaging commands."""

from __future__ import annotations

import sys


def configure_stdio() -> None:
	# Windows CI 的重定向输出可能使用 cp1252；提示和路径都保持 UTF-8。
	for stream in (sys.stdout, sys.stderr):
		reconfigure = getattr(stream, "reconfigure", None)
		if reconfigure is not None:
			reconfigure(encoding="utf-8", errors="replace")
