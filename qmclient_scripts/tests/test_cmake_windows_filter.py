from __future__ import annotations

import importlib.util
import io
from pathlib import Path
import tempfile
import unittest


_SPEC = importlib.util.spec_from_file_location(
    "cmake_windows_filter",
    Path(__file__).resolve().parents[1] / "cmake-windows-filter.py",
)
cmake_windows_filter = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(cmake_windows_filter)


class WindowsLogFilterTest(unittest.TestCase):
    def filter_log(self, content: bytes, encoding: str) -> bytes:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "compiler.log"
            path.write_bytes(content)
            output = io.BytesIO()
            cmake_windows_filter.write_filtered_log(path, output, encoding)
            return output.getvalue()

    def test_english_code_page_preserves_compiler_diagnostics(self):
        diagnostic = b"source.cpp(7): error C3861: identifier not found\r\n"
        self.assertEqual(self.filter_log(diagnostic, "cp1252"), diagnostic)

    def test_gbk_chinese_include_lines_are_removed(self):
        includes = " 注意: 包含文件: C:/headers/test.h\r\n".encode("gbk")
        self.assertEqual(self.filter_log(includes + b"[2/8] Building\r\n", "gbk"), b"[2/8] Building\r\n")

    def test_utf8_include_lines_are_removed_with_english_code_page(self):
        includes = "注意: 包含文件: C:/headers/test.h\n".encode("utf-8")
        self.assertEqual(self.filter_log(includes, "cp1252"), b"")

    def test_indented_english_include_lines_are_removed(self):
        self.assertEqual(
            self.filter_log(b"  Note: including file: C:/headers/test.h\r\n", "cp1252"),
            b"",
        )

    def test_non_include_bytes_are_preserved_without_decoding(self):
        diagnostic = "source.cpp(7): 错误 C3861\r\n".encode("gbk")
        self.assertEqual(self.filter_log(diagnostic, "cp1252"), diagnostic)

    def test_missing_native_codec_still_filters_portable_prefixes(self):
        content = b"Note: including file: C:/headers/test.h\nFAILED: object\n"
        self.assertEqual(self.filter_log(content, "unavailable-native-codec"), b"FAILED: object\n")


if __name__ == "__main__":
    unittest.main()
