#!/usr/bin/env python3

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from qmclient_scripts import repair_ninja_msvc_prefix


class RepairNinjaMsvcPrefixTest(unittest.TestCase):
    def test_probe_preserves_compiler_output_bytes(self):
        for encoding in ("utf-8", "gbk"):
            with self.subTest(encoding=encoding), tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
                build_dir = Path(tmp)
                (build_dir / "CMakeCache.txt").write_text("CMAKE_C_COMPILER:FILEPATH=cl.exe\n", encoding="utf-8")
                header = build_dir / "CMakeFiles" / "ShowIncludes" / "foo.h"
                prefix = "注意: 包含文件:  ".encode(encoding)
                output = b"main.c\r\n" + prefix + str(header.resolve()).encode(encoding) + b"\r\n"
                with mock.patch.object(
                    repair_ninja_msvc_prefix.subprocess,
                    "run",
                    return_value=subprocess.CompletedProcess([], 0, output, b""),
                ):
                    self.assertEqual(
                        repair_ninja_msvc_prefix._extract_showincludes_prefix(build_dir),
                        prefix,
                    )

    def test_missing_prefix_is_inserted_and_second_repair_is_unchanged(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
            rules = Path(tmp) / "rules.ninja"
            rules.write_bytes(b"rule compile\r\n  deps = msvc\r\n")
            self.assertTrue(repair_ninja_msvc_prefix._repair_rules_file(rules, b"Note: including file: "))
            first = rules.read_bytes()
            self.assertIn(b"msvc_deps_prefix = Note: including file: \r\n", first)
            self.assertFalse(repair_ninja_msvc_prefix._repair_rules_file(rules, b"Note: including file: "))
            self.assertEqual(rules.read_bytes(), first)

    def test_repair_preserves_raw_rules_and_resource_compiler_prefix(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
            rules = Path(tmp) / "rules.ninja"
            original = b'# raw comment: \xff\r\nmsvc_deps_prefix = old prefix\r\n  command = cmcldeps.exe RC $in $DEP_FILE $out "old prefix" "cl.exe" rc.exe\r\n'
            prefix = "注意: 包含文件:  ".encode("gbk")
            rules.write_bytes(original)
            self.assertTrue(repair_ninja_msvc_prefix._repair_rules_file(rules, prefix))
            self.assertEqual(rules.read_bytes(), original.replace(b"old prefix", prefix))

    def test_empty_prefix_does_not_replace_every_byte_in_rules(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
            rules = Path(tmp) / "rules.ninja"
            rules.write_bytes(b"msvc_deps_prefix = \nrule compile\n  deps = msvc\n")
            self.assertTrue(repair_ninja_msvc_prefix._repair_rules_file(rules, b"prefix: "))
            self.assertEqual(rules.read_bytes(), b"msvc_deps_prefix = prefix: \nrule compile\n  deps = msvc\n")

    def test_post_build_repair_requests_one_rebuild_then_succeeds(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
            build_dir = Path(tmp)
            (build_dir / "CMakeFiles").mkdir()
            (build_dir / "CMakeCache.txt").write_text("", encoding="utf-8")
            rules = build_dir / "CMakeFiles" / "rules.ninja"
            rules.write_bytes(b"msvc_deps_prefix = old prefix\n")
            with (
                mock.patch.object(sys, "argv", ["repair.py", "--post-build", "--build", str(build_dir)]),
                mock.patch.object(repair_ninja_msvc_prefix, "_extract_showincludes_prefix", return_value=b"new prefix"),
            ):
                self.assertEqual(repair_ninja_msvc_prefix.main(), 2)
                self.assertEqual(repair_ninja_msvc_prefix.main(), 0)

    def test_failed_dependency_query_does_not_commit_prefix_repair(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
            build_dir = Path(tmp)
            (build_dir / "CMakeFiles").mkdir()
            (build_dir / "CMakeCache.txt").write_text("", encoding="utf-8")
            rules = build_dir / "CMakeFiles" / "rules.ninja"
            original = b"msvc_deps_prefix = old prefix\n"
            rules.write_bytes(original)
            with (
                mock.patch.object(sys, "argv", ["repair.py", "--prepare-build", "--build", str(build_dir)]),
                mock.patch.object(repair_ninja_msvc_prefix, "_extract_showincludes_prefix", return_value=b"new prefix"),
                mock.patch.object(repair_ninja_msvc_prefix, "_invalidate_zero_dependency_objects", side_effect=subprocess.CalledProcessError(1, "ninja")),
                self.assertRaises(subprocess.CalledProcessError),
            ):
                repair_ninja_msvc_prefix.main()
            self.assertEqual(rules.read_bytes(), original)

    def test_prepare_build_argument_does_not_run_cmake_configure(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "tmp") as tmp:
            build_dir = Path(tmp) / "build"
            rules_dir = build_dir / "CMakeFiles"
            rules_dir.mkdir(parents=True)
            (build_dir / "CMakeCache.txt").write_text(
                "CMAKE_C_COMPILER:FILEPATH=cl.exe\nCMAKE_HOME_DIRECTORY:INTERNAL=E:/Coding/DDNet/QmClient\n",
                encoding="utf-8",
            )
            rules_file = rules_dir / "rules.ninja"
            rules_file.write_text(
                "msvc_deps_prefix = old prefix\nrule CXX_COMPILER__game\n  deps = msvc\n",
                encoding="utf-8",
            )

            with (
                mock.patch.object(
                    sys,
                    "argv",
                    [
                        "repair_ninja_msvc_prefix.py",
                        "--prepare-build",
                        "--build",
                        str(build_dir),
                    ],
                ),
                mock.patch.object(
                    repair_ninja_msvc_prefix,
                    "_extract_showincludes_prefix",
                    return_value=b"new prefix",
                ),
                mock.patch.object(
                    repair_ninja_msvc_prefix.subprocess,
                    "run",
                    side_effect=AssertionError("must not configure before build"),
                ),
            ):
                self.assertEqual(repair_ninja_msvc_prefix.main(), 0)

            self.assertIn(
                "msvc_deps_prefix = new prefix",
                rules_file.read_text(encoding="utf-8"),
            )


if __name__ == "__main__":
    unittest.main()
