from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

GATE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GATE_DIR))

from checks import qm_tclient_tokens
from lib.report import ResultCollector


class QmTClientTokensGateTest(unittest.TestCase):
	def scan(self, text: str):
		with TemporaryDirectory() as directory:
			path = Path(directory) / "sample.cpp"
			path.write_text(text, encoding="utf-8")
			return qm_tclient_tokens._scan_file(path)

	def test_float_literal_forms_are_detected(self):
		violations = self.scan("float A = 1.25f;\nfloat B = 0.5F;\nfloat C = .5f;\nfloat D = 1e-3f;\nfloat E = 5.f;\n")
		self.assertEqual([raw for _, raw, _ in violations], ["1.25f", "0.5F", ".5f", "1e-3f", "5.f"])

	def test_structural_values_comments_and_valid_allow_are_accepted(self):
		violations = self.scan(
			"float A = 1.0f;\n"
			"float B = 0.0f;\n"
			"// 0.22 s in prose\n"
			'const char *pText = "1.5";\n'
			"float C = 2.0f; // qm-token-allow: physical constant\n"
			"float D = 2.0f; // qm-token-allow no colon and no reason\n"
		)
		self.assertEqual([raw for _, raw, _ in violations], ["2.0f"])

	def test_allow_marker_inside_string_does_not_whitelist(self):
		violations = self.scan('const char *p = "qm-token-allow: hidden";\nfloat A = 2.0f;\n')
		self.assertEqual([raw for _, raw, _ in violations], ["2.0f"])

	def test_header_files_are_scanned(self):
		with TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "QmCardCatalogTClientInternal.h").write_text("float A = 2.0f;\n", encoding="utf-8")
			results = ResultCollector()
			with mock.patch.object(qm_tclient_tokens, "TCARD_DIR", root):
				qm_tclient_tokens.run(results, ["src/game/client/QmUi/cards/QmCardCatalogTClientInternal.h"])
		self.assertEqual(len(results.items), 1)
		self.assertEqual(results.items[0].level, "FAIL")
		self.assertIn("QmCardCatalogTClientInternal.h", results.items[0].detail)

	def test_clean_pages_pass(self):
		with TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "QmCardCatalogTClientMain.cpp").write_text("float A = 1.0f;\n", encoding="utf-8")
			results = ResultCollector()
			with mock.patch.object(qm_tclient_tokens, "TCARD_DIR", root):
				qm_tclient_tokens.run(results, ["src/game/client/QmUi/cards/QmCardCatalogTClientMain.cpp"])
		self.assertEqual(len(results.items), 1)
		self.assertEqual(results.items[0].level, "PASS")


if __name__ == "__main__":
	unittest.main()
