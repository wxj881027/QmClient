# 请抬头享受阳光｜日子很好 我很我---------致咩子
from __future__ import annotations

import codecs
import importlib.util
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "qmclient_scripts/add_signature.py"
SPEC = importlib.util.spec_from_file_location("add_signature", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
ADD_SIGNATURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ADD_SIGNATURE)
SIGNATURE_BYTES = ADD_SIGNATURE.SIGNATURE.encode("utf-8")


class AddSignatureTest(unittest.TestCase):
	def test_preserves_utf8_bom_and_crlf(self) -> None:
		with tempfile.TemporaryDirectory(prefix="qm-signature-test-") as temp_dir:
			source_path = Path(temp_dir) / "source.cpp"
			source_path.write_bytes(codecs.BOM_UTF8 + b"#include <test.h>\r\n")

			self.assertTrue(ADD_SIGNATURE.add_signature(source_path))

			self.assertEqual(
				source_path.read_bytes(),
				codecs.BOM_UTF8 + b"// " + SIGNATURE_BYTES + b"\r\n#include <test.h>\r\n",
			)

	def test_keeps_shebang_first_and_preserves_lf(self) -> None:
		with tempfile.TemporaryDirectory(prefix="qm-signature-test-") as temp_dir:
			script_path = Path(temp_dir) / "build.sh"
			script_path.write_bytes(b"#!/bin/bash\necho test\n")

			self.assertTrue(ADD_SIGNATURE.add_signature(script_path))

			self.assertEqual(
				script_path.read_bytes(),
				b"#!/bin/bash\n# " + SIGNATURE_BYTES + b"\necho test\n",
			)


if __name__ == "__main__":
	unittest.main()
