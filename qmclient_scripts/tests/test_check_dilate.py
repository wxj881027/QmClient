from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from qmclient_scripts.check_dilate import is_distance_field_atlas


class DistanceFieldAtlasTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="qm-dilate-")
        self.addCleanup(self.directory.cleanup)
        self.image = Path(self.directory.name) / "icons.png"
        self.metadata = self.image.with_suffix(".json")

    def test_distance_field_metadata_identifies_its_own_atlas(self) -> None:
        for kind in ("sdf", "msdf", "mtsdf"):
            with self.subTest(kind=kind):
                self.metadata.write_text(
                    json.dumps({"kind": kind, "atlas": {"image": "qmclient/icons/icons.png"}}),
                    encoding="utf-8",
                )
                self.assertTrue(is_distance_field_atlas(self.image))

    def test_filename_alone_does_not_exempt_an_image(self) -> None:
        self.assertFalse(is_distance_field_atlas(self.image.with_name("icons_msdf.png")))

    def test_color_atlas_metadata_still_requires_dilation(self) -> None:
        self.metadata.write_text(
            json.dumps({"kind": "rgba", "atlas": {"image": "icons.png"}}), encoding="utf-8"
        )
        self.assertFalse(is_distance_field_atlas(self.image))

    def test_metadata_for_another_image_does_not_exempt_this_image(self) -> None:
        self.metadata.write_text(
            json.dumps({"kind": "mtsdf", "atlas": {"image": "other.png"}}), encoding="utf-8"
        )
        self.assertFalse(is_distance_field_atlas(self.image))

    def test_invalid_metadata_does_not_exempt_an_image(self) -> None:
        for metadata in ("{", "[]", '{"kind": "mtsdf"}'):
            with self.subTest(metadata=metadata):
                self.metadata.write_text(metadata, encoding="utf-8")
                self.assertFalse(is_distance_field_atlas(self.image))


if __name__ == "__main__":
    unittest.main()
