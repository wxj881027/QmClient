#!/usr/bin/env python3
"""Regenerate the opt-in menu MSDF resources from bundled fonts."""

import argparse
from collections import Counter
import hashlib
from pathlib import Path
import subprocess
import tempfile

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SIZE = 2048
FONTS = (("DejaVuSans.ttf", 0), ("SourceHanSans.ttc", 2))


def menu_characters():
	translations = ROOT / "data/languages/simplified_chinese.txt"
	counts = Counter(char for line in translations.read_text(encoding="utf-8").splitlines()
		if line.startswith("== ") for char in line[3:] if "\u3400" <= char <= "\u9fff")
	return list(range(32, 127)) + list(range(160, 256)) + [0x2026, 0x2212, 0xFFFD], [
		*range(32, 127), *range(0x3000, 0x3012), *range(0xFF01, 0xFF61), 0x2026,
		*(ord(char) for char in sorted(counts, key=lambda char: (-counts[char], ord(char)))[:1450]),
	]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("generator", type=Path)
	args = parser.parse_args()
	with tempfile.TemporaryDirectory(prefix="menu-msdf-", dir=ROOT / "tmp") as directory:
		root = Path(directory)
		pixels, rows = root / "atlas.rgb", root / "rows.txt"
		command = [args.generator.resolve(), pixels, rows]
		faces = []
		for number, ((name, index), points) in enumerate(zip(FONTS, menu_characters(), strict=True)):
			font = ROOT / "data/fonts" / name
			points_path = root / f"points-{number}.txt"
			points_path.write_text("\n".join(map(str, sorted(set(points)))) + "\n", encoding="ascii")
			command.extend((font, str(index), points_path))
			faces.append((index, hashlib.sha256(font.read_bytes()).hexdigest()))
		subprocess.run(command, check=True)
		image = Image.frombytes("RGB", (SIZE, SIZE), pixels.read_bytes())
		image.convert("RGBA").save(ROOT / "data/qmclient/menu_msdf.png", optimize=True)
		with (ROOT / "src/engine/client/qm_menu_msdf_data.inc").open("w", encoding="utf-8", newline="\n") as output:
			output.write("// 由 qmclient_scripts/menu_msdf/build.py 生成。\n")
			output.write("inline constexpr SFace s_aFaces[] = {\n")
			for index, digest in faces:
				output.write(f'\t{{{index}, "{digest}"}},\n')
			output.write("};\ninline constexpr SGlyph s_aGlyphs[] = {\n")
			for row in rows.read_text(encoding="ascii").splitlines():
				output.write("\t{" + ", ".join(row.split()) + "},\n")
			output.write("};\n")


if __name__ == "__main__":
	main()
