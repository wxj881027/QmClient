#!/usr/bin/env python3
"""Build the pinned upstream msdf-atlas-gen tool for offline icon MTSDF baking."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


REPOSITORY = "https://github.com/Chlumsky/msdf-atlas-gen.git"
COMMIT = "6148900d59423059bafde2f51a0cb303184404bd"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).parents[1])
    parser.add_argument("--build-dir", type=Path, default=Path("cmake-build-msdf-atlas-gen"))
    parser.add_argument("--source-dir", type=Path, default=Path("tmp/msdf-atlas-gen"))
    args = parser.parse_args()

    source = args.source_dir
    if not (source / ".git").is_dir():
        source.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--recurse-submodules", REPOSITORY, str(source)], check=True)
    subprocess.run(["git", "-C", str(source), "fetch", "--depth", "1", "origin", COMMIT], check=True)
    subprocess.run(["git", "-C", str(source), "checkout", "--detach", COMMIT], check=True)
    subprocess.run(["git", "-C", str(source), "submodule", "update", "--init", "--recursive"], check=True)

    configure = [
        "cmake", "-S", str(source), "-B", str(args.build_dir),
        "-DMSDF_ATLAS_USE_VCPKG=OFF",
        "-DMSDF_ATLAS_USE_SKIA=OFF",
        "-DMSDF_ATLAS_NO_ARTERY_FONT=ON",
        "-DMSDF_ATLAS_BUILD_STANDALONE=ON",
        "-DMSDFGEN_DISABLE_PNG=ON",
        "-DMSDFGEN_DISABLE_SVG=ON",
        f"-DFREETYPE_INCLUDE_DIRS={args.repo_root / 'ddnet-libs/freetype/include'}",
        f"-DFREETYPE_LIBRARY={args.repo_root / 'ddnet-libs/freetype/windows/lib64/freetype.lib'}",
    ]
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(args.build_dir), "--config", "Release", "-j", "14"], check=True)
    print(args.build_dir / "bin/Release/msdf-atlas-gen.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
