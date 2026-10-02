from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from qmclient_scripts import cmake_windows


class WindowsBuildDirectoryTest(unittest.TestCase):
    def test_build_and_configure_paths_share_the_same_identity(self):
        directory = Path("tmp/build with spaces").resolve()
        for arguments in (["--build", str(directory)], ["--build=" + str(directory)], ["-B", str(directory)], ["-B" + str(directory)], ["--install", str(directory)], ["--install=" + str(directory)]):
            with self.subTest(arguments=arguments):
                self.assertEqual(cmake_windows.command_build_dir(arguments), directory)

    def test_helper_commands_have_no_build_directory(self):
        for arguments in (["-E", "echo", "hello"], ["-P", "script.cmake"], ["--version"], ["--help"]):
            with self.subTest(arguments=arguments):
                self.assertIsNone(cmake_windows.command_build_dir(arguments))

    def test_non_msvc_rules_do_not_trigger_msvc_compiler_probe(self):
        with tempfile.TemporaryDirectory(dir=cmake_windows.REPO_ROOT / "tmp") as tmp:
            directory = Path(tmp)
            self.assertFalse(cmake_windows.repair_msvc_rules(directory))
            rules_dir = directory / "CMakeFiles"
            rules_dir.mkdir()
            (rules_dir / "rules.ninja").write_text("rule compile\n  deps = gcc\n", encoding="utf-8")
            self.assertFalse(cmake_windows.repair_msvc_rules(directory))


if __name__ == "__main__":
    unittest.main()
