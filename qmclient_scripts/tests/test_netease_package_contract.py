#!/usr/bin/env python3
"""保证网易云/汽水注入运行时随标准便携包发布。"""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


class NeteasePackageContractTest(unittest.TestCase):
    def test_netease_runtime_targets_are_packaged(self) -> None:
        cmake_source = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        package_start = cmake_source.index("set(CPACK_TARGETS)")
        package_end = cmake_source.index("set(CPACK_DIRS", package_start)
        package_targets = cmake_source[package_start:package_end]

        for target in (
            "qm-nmt-helper",
            "qm-nmt-hook64",
            "qm-nmt-bootstrap",
            "qm-soda-helper",
            "qm-music-helper",
        ):
            with self.subTest(target=target):
                self.assertIn(target, package_targets)


if __name__ == "__main__":
    unittest.main()
