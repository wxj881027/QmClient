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
CONFIG_VARIABLES_PATH = REPO_ROOT / "src/engine/shared/config_variables_qmclient.h"
INPUT_OVERLAY_SOURCE_PATH = (
    REPO_ROOT / "src/game/client/components/qmclient/input_overlay.cpp"
)
INPUT_OVERLAY_MENU_PATH = (
    REPO_ROOT / "src/game/client/components/qmclient/menus_qmclient.cpp"
)


class InputOverlayLayoutTest(unittest.TestCase):
    def test_keyboard_contains_only_requested_keys_in_physical_rows(self) -> None:
        expected_rows = (
            ("escape", "1", "2", "3", "4", "5", "F12"),
            ("tab", "q", "w", "e", "r", "t"),
            ("capslock", "a", "s", "d", "f", "g"),
            ("lshift", "z", "x", "c", "v"),
            ("lctrl", "lgui", "lalt", "space"),
        )
        expected_positions = {
            "escape": [-1, -1],
            "1": [60, -1],
            "2": [121, -1],
            "3": [182, -1],
            "4": [243, -1],
            "5": [304, -1],
            "F12": [365, -1],
            "tab": [-1, 60],
            "q": [91, 60],
            "w": [152, 60],
            "e": [213, 60],
            "r": [274, 60],
            "t": [335, 60],
            "capslock": [-1, 121],
            "a": [106, 121],
            "s": [167, 121],
            "d": [228, 121],
            "f": [289, 121],
            "g": [350, 121],
            "lshift": [-1, 182],
            "z": [136, 182],
            "x": [197, 182],
            "c": [258, 182],
            "v": [319, 182],
            "lctrl": [-1, 243],
            "lgui": [75, 243],
            "lalt": [152, 243],
            "space": [228, 243],
        }
        layout = json.loads(KEYBOARD_LAYOUT_PATH.read_text(encoding="utf-8"))
        elements = layout["elements"]

        self.assertCountEqual(
            [element["id"] for element in elements],
            [key for row in expected_rows for key in row],
        )
        positions = {element["id"]: element["pos"] for element in elements}
        self.assertEqual(positions, expected_positions)

        self.assertEqual(layout["overlay_width"], 432)
        self.assertEqual(layout["overlay_height"], 300)

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
        self.assertEqual(mouse["offset"], {"x": 467, "y": 0})

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
            ("arrow", 9, [862, 1, 100, 100], [102, 236], False),
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

    def test_runtime_wires_independent_keyboard_and_mouse_scale_controls(self) -> None:
        config_variables = CONFIG_VARIABLES_PATH.read_text(encoding="utf-8")
        render_source = INPUT_OVERLAY_SOURCE_PATH.read_text(encoding="utf-8")
        menu_source = INPUT_OVERLAY_MENU_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "MACRO_CONFIG_INT(QmInputOverlayMouseScale, "
            "qm_input_overlay_mouse_scale, 20, 1, 200,",
            config_variables,
        )
        self.assertIn(
            "const float MouseScale = "
            "g_Config.m_QmInputOverlayMouseScale / 100.0f;",
            render_source,
        )
        self.assertIn("Layout.m_IsMouseLayout", render_source)
        self.assertIn('"Keyboard size"', menu_source)
        self.assertIn('"Mouse size"', menu_source)
        self.assertIn("m_QmInputOverlayMouseScale", menu_source)

    def test_runtime_restores_percentage_position_controls(self) -> None:
        config_variables = CONFIG_VARIABLES_PATH.read_text(encoding="utf-8")
        render_source = INPUT_OVERLAY_SOURCE_PATH.read_text(encoding="utf-8")
        menu_source = INPUT_OVERLAY_MENU_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "MACRO_CONFIG_INT(QmInputOverlayPosX, "
            "qm_input_overlay_pos_x, 71, 0, 100,",
            config_variables,
        )
        self.assertIn(
            "MACRO_CONFIG_INT(QmInputOverlayPosY, "
            "qm_input_overlay_pos_y, 80, 0, 100,",
            config_variables,
        )
        self.assertIn("g_Config.m_QmInputOverlayPosX", render_source)
        self.assertIn("g_Config.m_QmInputOverlayPosY", render_source)
        self.assertIn('"Horizontal position"', menu_source)
        self.assertIn('"Vertical position"', menu_source)
        self.assertIn("m_QmInputOverlayPosX", menu_source)
        self.assertIn("m_QmInputOverlayPosY", menu_source)


if __name__ == "__main__":
    unittest.main()
