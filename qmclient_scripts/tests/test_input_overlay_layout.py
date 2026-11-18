from __future__ import annotations

import json
import unittest
from pathlib import Path

from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = REPO_ROOT / "data/input_overlay.json"
KEYBOARD_LAYOUT_PATH = REPO_ROOT / "data/input overlay-Zac/wasd.json"
KEYBOARD_IMAGE_PATH = REPO_ROOT / "data/input overlay-Zac/wasd.png"
MOUSE_LAYOUT_PATH = REPO_ROOT / "data/input overlay-Zac/mouse.json"
MOUSE_IMAGE_PATH = REPO_ROOT / "data/input overlay-Zac/mouse.png"


class InputOverlayLayoutTest(unittest.TestCase):
    def test_keyboard_contains_only_requested_keys_in_physical_rows(self) -> None:
        expected_rows = ("12345", "qwer", "asdf", "zxcv")
        layout = json.loads(KEYBOARD_LAYOUT_PATH.read_text(encoding="utf-8"))
        elements = layout["elements"]
        heights = sorted({element["pos"][1] for element in elements})
        actual_rows = [
            "".join(
                element["id"]
                for element in sorted(elements, key=lambda item: item["pos"][0])
                if element["pos"][1] == height
            )
            for height in heights
        ]
        self.assertEqual(actual_rows, list(expected_rows))
        for element in elements:
            pos_x, pos_y = element["pos"]
            _, _, width, height = element["mapping"]
            self.assertGreaterEqual(min(pos_x, pos_y), 0)
            self.assertLessEqual(pos_x + width, layout["overlay_width"])
            self.assertLessEqual(pos_y + height, layout["overlay_height"])

    def test_keyboard_sprite_mappings_fit_inside_atlas(self) -> None:
        layout = json.loads(KEYBOARD_LAYOUT_PATH.read_text(encoding="utf-8"))
        config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        pressed_offset_y = config["layouts"][0]["pressed_offset_y"]
        with Image.open(KEYBOARD_IMAGE_PATH) as image:
            atlas = image.convert("RGBA")
            image_width, image_height = image.size
            for element in layout["elements"]:
                map_x, map_y, map_width, map_height = element["mapping"]
                with self.subTest(key=element["id"]):
                    self.assertLessEqual(map_x + map_width, image_width)
                    self.assertLessEqual(
                        map_y + pressed_offset_y + map_height,
                        image_height,
                    )
                    for state_offset in (0, pressed_offset_y):
                        sprite = atlas.crop(
                            (
                                map_x,
                                map_y + state_offset,
                                map_x + map_width,
                                map_y + state_offset + map_height,
                            )
                        )
                        self.assertIsNotNone(sprite.getchannel("A").getbbox())

    def test_mouse_layout_is_only_shifted_right_of_keyboard(self) -> None:
        config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        keyboard, mouse = config["layouts"]

        self.assertEqual(keyboard["layout"], "input overlay-Zac/wasd.json")
        self.assertEqual(mouse["layout"], "input overlay-Zac/mouse.json")
        self.assertEqual(keyboard["pressed_offset_y"], 61)
        width = json.loads(KEYBOARD_LAYOUT_PATH.read_text(encoding="utf-8"))["overlay_width"]
        self.assertEqual(mouse["offset"], {"x": width + 35, "y": 0})

    def test_stationary_mouse_shares_button_states_without_an_arrow(self) -> None:
        layout = json.loads(MOUSE_LAYOUT_PATH.read_text(encoding="utf-8"))
        stationary = json.loads(
            MOUSE_LAYOUT_PATH.with_name("mouse-no-movement.json").read_text(encoding="utf-8")
        )
        self.assertEqual(REPO_ROOT / "data" / stationary["image"], MOUSE_IMAGE_PATH)
        self.assertEqual(stationary["elements"], [e for e in layout["elements"] if e["type"] != 9])

    def test_mouse_layout_exposes_all_official_states(self) -> None:
        layout = json.loads(MOUSE_LAYOUT_PATH.read_text(encoding="utf-8"))
        expected_elements = (
            ("body", 0, [1, 1, 283, 242], [2, 179], False),
            ("lmb", 0, [287, 1, 139, 174], [2, 0], False),
            ("lmb", 3, [287, 178, 139, 174], [2, 0], True),
            ("rmb", 0, [429, 1, 139, 174], [146, 0], False),
            ("rmb", 3, [429, 178, 139, 174], [146, 0], True),
            ("mmb", 0, [571, 1, 48, 95], [117, 79], False),
            ("mmb", 3, [622, 1, 48, 95], [117, 79], True),
            ("wheel_up", 4, [673, 1, 48, 95], [117, 79], True),
            ("wheel_down", 4, [724, 1, 48, 95], [117, 79], True),
            ("smb2", 0, [775, 1, 40, 62], [0, 210], False),
            ("smb2", 3, [775, 66, 40, 62], [0, 210], True),
            ("smb1", 0, [818, 1, 41, 62], [11, 273], False),
            ("smb1", 3, [818, 66, 41, 62], [11, 273], True),
            ("arrow", 9, [862, 1, 56, 56], [115, 258], False),
        )
        elements = layout["elements"]
        actual_elements = tuple(
            (
                element["id"],
                element["type"],
                element["mapping"],
                element["pos"],
                element.get("active_only", False),
            )
            for element in elements
        )

        self.assertEqual(actual_elements, expected_elements)
        self.assertEqual(layout["overlay_width"], 285)
        self.assertEqual(layout["overlay_height"], 421)

        with Image.open(MOUSE_IMAGE_PATH) as image:
            atlas = image.convert("RGBA")
            for element in elements:
                map_x, map_y, map_width, map_height = element["mapping"]
                with self.subTest(element=element["id"], mapping=element["mapping"]):
                    self.assertLessEqual(map_x + map_width, image.width)
                    self.assertLessEqual(map_y + map_height, image.height)
                    sprite = atlas.crop(
                        (map_x, map_y, map_x + map_width, map_y + map_height)
                    )
                    self.assertIsNotNone(sprite.getchannel("A").getbbox())


if __name__ == "__main__":
    unittest.main()
