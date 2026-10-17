"""编译 Inno 安装内核和原生界面，生成单文件离线 QmClient-Setup.exe。"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid

from package_stdio import configure_stdio

REPO_ROOT = Path(__file__).resolve().parents[1]
INSTALLER_SCRIPT = REPO_ROOT / "qmclient_scripts/installer/QmClient.iss"


def setup_identity(app_id: str | None) -> str:
	if app_id is None:
		match = re.search(r'#define SetupAppId "\{\{([0-9A-Fa-f-]+)\}"', INSTALLER_SCRIPT.read_text(encoding="utf-8"))
		if match is None:
			raise ValueError("missing default SetupAppId in QmClient.iss")
		app_id = match.group(1)
	return str(uuid.UUID(app_id)).upper()


def numeric_version(version: str) -> str:
	match = re.fullmatch(r"(\d+)\.(\d+)(?:-preview\.(\d+))?", version)
	if match is None:
		raise ValueError("version must be X.Y or X.Y-preview.N")
	parts = [int(part or 0) for part in match.groups()]
	if any(part > 65535 for part in parts):
		raise ValueError("version components must fit Windows version resources")
	return ",".join(str(part) for part in [*parts, 0])


def find_iscc() -> str:
	compiler = shutil.which("iscc") or shutil.which("ISCC")
	if compiler:
		return compiler
	installed = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Inno Setup 6/ISCC.exe"
	if installed.is_file():
		return str(installed)
	raise ValueError("Inno Setup ISCC is required to build the installation engine")


def build_setup(source: Path, output: Path, native_build: Path, version: str, *, compiler: str | None = None, app_id: str | None = None, jobs: int = 6) -> Path:
	numeric = numeric_version(version)
	identity = setup_identity(app_id)
	if jobs < 1:
		raise ValueError("jobs must be positive")
	source = source.resolve(strict=True)
	if not source.is_dir() or not (source / "DDNet.exe").is_file():
		raise ValueError("source must contain the prepared normal-client setup payload")
	output = output.resolve()
	native_build = native_build.resolve()
	if native_build == output:
		raise ValueError("native build and final output must be separate directories")
	compiler = compiler or find_iscc()
	engine_output = native_build / "engine"
	engine_output.mkdir(parents=True, exist_ok=True)
	result = subprocess.run([
		compiler, f"/DSourceDir={source}", f"/DOutputDir={engine_output}", f"/DAppVersion={version}",
		"/DSetupAppId={{" + identity + "}", str(INSTALLER_SCRIPT),
	], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
	engine_log = native_build / "engine-compile.log"
	engine_log.write_bytes(result.stdout)
	if result.returncode or b"Warning:" in result.stdout:
		raise RuntimeError(f"Setup engine compilation failed or emitted warnings; see {engine_log}")
	engine = engine_output / "QmClient-Setup.exe"
	if not engine.is_file() or engine.stat().st_size == 0:
		raise RuntimeError("Setup compiler did not produce the installation engine")
	wrapper = str(REPO_ROOT / "qmclient_scripts/cmake-windows.cmd")
	commands = [
		[wrapper, "-G", "Ninja", "-S", str(REPO_ROOT / "src/qm/setup"), "-B", str(native_build), "-DCMAKE_BUILD_TYPE=Release",
		 f"-DQM_SETUP_ENGINE={engine}", f"-DQM_SETUP_VERSION={version}", f"-DQM_SETUP_NUMERIC_VERSION={numeric}", f"-DQM_SETUP_APP_ID={identity}"],
		[wrapper, "--build", str(native_build), "--target", "qmclient-setup", "-j", str(jobs)],
	]
	with (native_build / "native-build.log").open("wb") as log:
		for command in commands:
			result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False, cwd=REPO_ROOT)
			if result.returncode:
				raise RuntimeError(f"Native setup compilation failed; see {log.name}")
	native = native_build / "QmClient-Setup.exe"
	if not native.is_file() or native.stat().st_size == 0:
		raise RuntimeError("Native build did not produce QmClient-Setup.exe")
	output.mkdir(parents=True, exist_ok=True)
	final = output / "QmClient-Setup.exe"
	staged = output / "QmClient-Setup.exe.publishing"
	shutil.copyfile(native, staged)
	staged.replace(final)
	return final


def main() -> int:
	configure_stdio()
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--build-dir", type=Path, required=True)
	parser.add_argument("--version", required=True)
	parser.add_argument("--iscc")
	parser.add_argument("--app-id", help="isolated UUID for development installation tests")
	parser.add_argument("--jobs", type=int, default=6)
	args = parser.parse_args()
	print(build_setup(args.source, args.output, args.build_dir, args.version, compiler=args.iscc, app_id=args.app_id, jobs=args.jobs))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
