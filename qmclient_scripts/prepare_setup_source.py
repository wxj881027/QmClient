#!/usr/bin/env python3
"""Prepare the minimal Windows Setup payload from a built client directory."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path, PurePosixPath

if not __package__:
	sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qmclient_scripts.package_stdio import configure_stdio


def prepare(source: Path, data: Path, output: Path) -> None:
	source = source.resolve(strict=True)
	data = data.resolve(strict=True)
	output = output.resolve()
	cache = source / "CMakeCache.txt"
	if cache.is_file():
		options = dict(line.split("=", 1) for line in cache.read_text(encoding="utf-8").splitlines() if "=" in line and not line.startswith(("#", "//")))
		if any(options.get(f"{name}:BOOL", "OFF").upper() in {"ON", "TRUE", "YES", "1"} for name in ("QMCLIENT_PORTABLE", "QMCLIENT_TEST_STORAGE")):
			raise ValueError("Setup requires a normal client build; portable and isolated test builds cannot be installed")
	if not source.is_dir() or not data.is_dir():
		raise ValueError("source and data must be directories")
	temporary = output.with_name(f".{output.name}.tmp").resolve()
	# 输出允许位于构建目录内，但不能覆盖输入，也不能进入数据树造成递归复制。
	for destination in (output, temporary):
		if destination == source or destination in source.parents or destination == data or destination in data.parents or data in destination.parents:
			raise ValueError("output and temporary directory must not overwrite inputs or lie inside data")

	manifest = source / "qmclient-setup-runtime.txt"
	if not manifest.is_file():
		raise ValueError("missing CMake Setup runtime manifest; reconfigure the client build")
	names = manifest.read_text(encoding="utf-8").splitlines()
	# 清单只允许构建目录同级运行时文件，拒绝目录穿越和重复项。
	if not names or any(not name or "/" in name or "\\" in name or ":" in name or Path(name).suffix.casefold() not in {".exe", ".dll"} for name in names):
		raise ValueError("invalid Setup runtime manifest")
	if len({name.casefold() for name in names}) != len(names):
		raise ValueError("duplicate Setup runtime manifest entry")

	generated_manifest = source / "qmclient-setup-generated.txt"
	if not generated_manifest.is_file():
		raise ValueError("missing CMake Setup generated asset manifest; reconfigure the client build")
	generated_files = generated_manifest.read_text(encoding="utf-8").splitlines()
	for name in generated_files:
		parts = PurePosixPath(name).parts
		# 构建生成的 shader 只能覆盖 data 下的资源，不能写入程序或配置路径。
		if len(parts) < 2 or parts[0] != "data" or any(part in {".", ".."} for part in name.split("/")) or "\\" in name or ":" in name:
			raise ValueError("invalid Setup generated asset manifest")
	if len({name.casefold() for name in generated_files}) != len(generated_files):
		raise ValueError("duplicate Setup generated asset manifest entry")

	shutil.rmtree(temporary, ignore_errors=True)
	temporary.mkdir(parents=True)
	try:
		for name in names:
			path = source / name
			if not path.is_file():
				kind = "executable" if path.suffix.casefold() == ".exe" else "runtime DLL"
				raise ValueError(f"missing required {kind}: {name}")
			shutil.copy2(path, temporary / name)
		dlls = sorted(source.glob("*.dll"), key=lambda path: path.name.casefold())
		if not dlls:
			raise ValueError("no runtime DLLs found in build directory")
		for path in dlls:
			shutil.copy2(path, temporary / path.name)
		shutil.copytree(data, temporary / "data")
		for name in generated_files:
			path = source / name
			if not path.is_file():
				raise ValueError(f"missing required generated asset: {name}")
			destination = temporary / name
			destination.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(path, destination)
		shutil.rmtree(output, ignore_errors=True)
		temporary.replace(output)
	finally:
		shutil.rmtree(temporary, ignore_errors=True)


def main() -> int:
	configure_stdio()
	parser = argparse.ArgumentParser(description="Prepare the minimal QmClient Setup payload")
	parser.add_argument("--source", type=Path, required=True, help="build directory")
	parser.add_argument("--data", type=Path, required=True, help="repository data directory")
	parser.add_argument("--output", type=Path, required=True)
	args = parser.parse_args()
	prepare(args.source, args.data, args.output)
	print(f"已准备 Setup 载荷: {args.output}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
