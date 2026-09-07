import json
from pathlib import Path
import shutil
import tempfile
import unittest

from generate_qm_icon_manifest import generate, unique_object


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data/qmclient/icons"


class IconManifestTest(unittest.TestCase):
    def test_real_atlases_generate_deterministically(self):
        output = generate(SOURCE)
        self.assertEqual(output, generate(SOURCE))
        for name in ("CHEVRON_DOWN", "EYE", "EYE_OFF"):
            self.assertIn(f"\t{name},", output)
        self.assertEqual(output.count('"qmclient/icons/qm_icons_bold_'), 3)

    def test_rejects_duplicate_json_fields(self):
        with self.assertRaises(ValueError):
            json.loads('{"icons":{},"icons":{}}', object_pairs_hook=unique_object)

    def test_rejects_outside_bounds_and_scale_drift(self):
        (ROOT / "tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="qm-icon-test-", dir=ROOT / "tmp") as temporary:
            directory = Path(temporary)
            for file in SOURCE.glob("qm_icons_bold_*"):
                shutil.copyfile(file, directory / file.name)
            file = directory / "qm_icons_bold_2x.json"
            original = json.loads(file.read_text(encoding="utf-8"))
            document = json.loads(json.dumps(original))
            document["icons"]["eye"]["x"] = document["atlas"]["width"]
            file.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "outside atlas"):
                generate(directory)
            document = json.loads(json.dumps(original))
            document["icons"]["eye"]["x"] += 1
            file.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "differ between"):
                generate(directory)


if __name__ == "__main__":
    unittest.main()
