#!/usr/bin/env python3
"""从随包 Phosphor TTF 全量烘焙 UI 图标 MTSDF 图集（离线资源管线）。

每个样式一次官方 msdf-atlas-gen 调用（charset = 该样式全部 PUA 码点），
再把生成的字形逐个裁剪进 64×64 网格单元，产出与运行时契约一致的
data/qmclient/icons/qm_icons_<style>_msdf.{png,json}（icons 按官方图标名索引）。

duotone 的双层编码来自字体本身：偶数码点是 primary 层，奇数码点（cp+1）是
secondary 层。RGB 放 primary 的 MSDF，Alpha 放 secondary 的真 SDF，
manifest 声明 secondary_mask: alpha —— 与着色器契约保持不变。

用法（官方工具构建见 qm_build_icon_msdf_official.py；需 freetype/png/zlib DLL 在 PATH）：
  py -3 qmclient_scripts/qm_build_icon_msdf_font.py \
    --tool cmake-build-official/bin/Release/msdf-atlas-gen.exe \
    --fonts-dir data/qmclient/fonts/Phosphor \
    --codepoints datasrc/qm_icons/phosphor.codepoints \
    --output data/qmclient/icons --styles duotone light regular bold fill
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

FIELD_SIZE = 48
PX_RANGE = 6
PADDING = 8  # 官方工具的字形内边距（pxrange 出血空间）
GRID_MARGIN = 12  # 网格单元的额外间距：字形外轮廓可略超 em 框（实测最大 ~70px）
CELL_SIZE = FIELD_SIZE + GRID_MARGIN * 2

STYLES = ("duotone", "light", "regular", "bold", "fill")
FONT_NAMES = {
    "duotone": "Phosphor-Duotone.ttf",
    "light": "Phosphor-Light.ttf",
    "regular": "Phosphor-Regular.ttf",
    "bold": "Phosphor-Bold.ttf",
    "fill": "Phosphor-Fill.ttf",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="从 Phosphor TTF 烘焙全量图标 MTSDF 图集")
    parser.add_argument("--tool", type=Path, required=True, help="官方 msdf-atlas-gen 可执行文件")
    parser.add_argument("--fonts-dir", type=Path, required=True)
    parser.add_argument("--codepoints", type=Path, required=True, help="官方 name->codepoint 映射")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--styles", nargs="+", default=list(STYLES), choices=STYLES)
    parser.add_argument(
        "--morph-frames",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Bold 图集额外烘焙眼睛 morph 的 MSDF 中间帧（默认开启；--no-morph-frames 关闭）",
    )
    return parser.parse_args()


def load_codepoints(path: Path) -> dict[str, int]:
    icons: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, hex_code = line.split()
        icons[name] = int(hex_code, 16)
    return icons


def font_cmap(path: Path) -> set[int]:
    from fontTools.ttLib import TTFont

    return {cp for cp in TTFont(str(path)).getBestCmap() if cp > 0}


def write_charset(path: Path, codepoints: list[int]) -> None:
    # 官方解析器支持十六进制数值，避免特殊字符破坏字符串语法。
    path.write_text("\n".join(f"0x{cp:X}" for cp in codepoints) + "\n", encoding="utf-8")


def run_official(tool: Path, font: Path, charset: Path, json_out: Path, image_out: Path, page_size: int) -> tuple[dict, Path]:
    """通过唯一的图标 MTSDF 适配器调用官方工具，返回页面 manifest 和 PNG。"""
    adapter = Path(__file__).with_name("qm_icon_msdf_official.py")
    subprocess.run([
        sys.executable, str(adapter), "--tool", str(tool), "--font", str(font),
        "--charset", str(charset), "--output", str(json_out), "--image", str(image_out),
        "--font-name", "Phosphor", "--size", str(FIELD_SIZE), "--px-range", str(PX_RANGE),
        "--padding", str(PADDING), "--width", str(page_size), "--height", str(page_size),
    ], check=True)
    return json.loads(json_out.read_text(encoding="utf-8")), image_out


def page_size_for(count: int) -> int:
    # 每字形约占一格 64²，加打包损耗；4096² 容纳约 3300 格，全量 1512 图标绰绰有余。
    needed = count * CELL_SIZE * CELL_SIZE
    for size in (2048, 4096, 8192):
        if size * size >= needed * 1.35:
            return size
    return 8192


def bake_morph_frames(args: argparse.Namespace, atlas, columns: int, base_index: int, icons_out: dict, temp_dir: Path) -> list[dict]:
    """把眼睛 morph 的 MSDF 中间帧烤进当前图集，返回 manifest 的 morph_frames 段。

    帧的来源见 qm_build_icon_morph_frames：几何 morph 的中间形状写成临时 TTF 后由官方
    工具烤成 MSDF，因此形变走与图标相同的抗锯齿路径。显示框在 eye 与 eye-slash 两个
    图标之间按进度插值，帧 0 / 末帧因此与图标同尺寸基准，全程没有整体缩放跳变。
    """
    import qm_build_icon_morph_frames as morph_frames
    from PIL import Image

    font_path = temp_dir / "morph_frames.ttf"
    codepoints, progress_values = morph_frames.build_frame_font_from_plan(font_path, args.fonts_dir, args.codepoints)
    charset = temp_dir / "morph_frames.charset.txt"
    write_charset(charset, codepoints)
    manifest, png = run_official(
        args.tool, font_path, charset, temp_dir / "morph_frames.json", temp_dir / "morph_frames.png", page_size_for(len(codepoints))
    )
    page = Image.open(png).convert("RGBA")
    glyphs = manifest["glyphs"]

    start_box = icons_out["eye"]
    end_box = icons_out["eye-slash"]
    entries: list[dict] = []
    for index, (codepoint, progress) in enumerate(zip(codepoints, progress_values)):
        glyph = glyphs[str(codepoint)]
        gx, gy, gw, gh = int(glyph["x"]), int(glyph["y"]), int(glyph["w"]), int(glyph["h"])
        cell = base_index + index
        cell_x = (cell % columns) * CELL_SIZE
        cell_y = (cell // columns) * CELL_SIZE
        # 显示框按进度在 eye / eye-slash 之间插值，中间帧因此可能比该帧位图大 1~3px
        # （取 max 保证框一定包住位图）。这几像素若采到未清理的图集背景，就是实机上那圈
        # 白框：背景色 (0,0,0,255) 的 alpha 在 MTSDF 真 SDF 语义下是「远在形状内部」，
        # 着色器解出覆盖率 1 → 渲染成实色。先把整个格子清成「远在形状之外」(0,0,0,0)，
        # 框内位图以外的部分才恒为透明。静态图标的框等于位图、采不到背景，故不受影响。
        atlas.paste((0, 0, 0, 0), (cell_x, cell_y, cell_x + CELL_SIZE, cell_y + CELL_SIZE))
        atlas.paste(page.crop((gx, gy, gx + gw, gy + gh)), (cell_x + (CELL_SIZE - gw) // 2, cell_y + (CELL_SIZE - gh) // 2))
        box_w = max(int(round(start_box["w"] + (end_box["w"] - start_box["w"]) * progress)), gw)
        box_h = max(int(round(start_box["h"] + (end_box["h"] - start_box["h"]) * progress)), gh)
        entries.append(
            {
                "name": f"eye-morph-{index}",
                "progress": round(progress, 6),
                "x": cell_x + (CELL_SIZE - box_w) // 2,
                "y": cell_y + (CELL_SIZE - box_h) // 2,
                "w": box_w,
                "h": box_h,
            }
        )
    print(f"  morph frames: {len(entries)} baked at cells {base_index}..{base_index + len(entries) - 1}")
    return entries


def bake_style(style: str, args: argparse.Namespace, icons: dict[str, int]) -> None:
    from PIL import Image

    font = args.fonts_dir / FONT_NAMES[style]
    if not font.is_file():
        raise SystemExit(f"missing bundled font: {font}")
    cmap = font_cmap(font)

    # duotone：primary=偶数码点，secondary=奇数码点（cp+1）
    # 新旧官方名可能共用同一码点：多个名字共享同一个网格格子。
    primaries: dict[str, int] = {}
    secondaries: dict[str, int] = {}
    for name, cp in sorted(icons.items()):
        if cp not in cmap:
            print(f"  skip {name}: U+{cp:04X} not in {font.name}")
            continue
        primaries[name] = cp
        if style == "duotone" and (cp + 1) in cmap:
            secondaries[name] = cp + 1
    if not primaries:
        raise SystemExit(f"{style}: no requested codepoints covered by {font.name}")
    missing = sorted(set(icons) - set(primaries))
    if missing:
        raise SystemExit(f"{style}: icons missing from font cmap: {missing[:8]}")

    with tempfile.TemporaryDirectory(prefix="qm-icons-font-") as temp:
        temp_dir = Path(temp)

        def bake_page(codepoints: list[int], tag: str) -> tuple[dict, Image.Image]:
            charset = temp_dir / f"{tag}.charset.txt"
            write_charset(charset, codepoints)
            manifest, png = run_official(args.tool, font, charset, temp_dir / f"{tag}.json", temp_dir / f"{tag}.png", page_size_for(len(codepoints)))
            return manifest, Image.open(png).convert("RGBA")

        primary_manifest, primary_png = bake_page(sorted(primaries.values()), f"{style}_primary")
        packed = {int(key) for key in primary_manifest["glyphs"]}
        unpacked = [cp for cp in primaries.values() if cp not in packed]
        if unpacked:
            raise SystemExit(f"{style}: primary page dropped U+{unpacked[0]:04X}; page too small")
        secondary_manifest, secondary_png = None, None
        if style == "duotone" and secondaries:
            secondary_manifest, secondary_png = bake_page(sorted(secondaries.values()), f"{style}_secondary")

        # 方形网格布局：格子按唯一码点分配（同码点的别名共享格子），名字排序保证确定性。
        names = sorted(primaries)
        cell_of_cp: dict[int, int] = {}
        for name in names:
            cp = primaries[name]
            if cp not in cell_of_cp:
                cell_of_cp[cp] = len(cell_of_cp)
        columns = 1
        while columns * columns < len(cell_of_cp):
            columns += 1
        # 眼睛 morph 的中间帧只随 Bold 图集烘焙（morph 是 Bold-only 特性），
        # 与图标同格布局，运行时可共用同一张纹理。
        frame_count = 0
        if style == "bold" and args.morph_frames:
            import qm_build_icon_morph_frames as morph_frames

            frame_count = morph_frames.FRAME_COUNT
        if frame_count:
            while columns * columns < len(cell_of_cp) + frame_count:
                columns += 1
        rows = (len(cell_of_cp) + frame_count + columns - 1) // columns
        atlas_w = columns * CELL_SIZE
        atlas_h = rows * CELL_SIZE
        atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 255))
        icons_out: dict[str, dict[str, int]] = {}
        glyphs_primary = primary_manifest["glyphs"]
        glyphs_secondary = secondary_manifest["glyphs"] if secondary_manifest else {}

        pasted: set[int] = set()
        for name in names:
            cp = primaries[name]
            index = cell_of_cp[cp]
            glyph = glyphs_primary[str(cp)]
            gx, gy = int(glyph["x"]), int(glyph["y"])
            gw, gh = int(glyph["w"]), int(glyph["h"])
            piece = primary_png.crop((gx, gy, gx + gw, gy + gh))
            if style == "duotone":
                # Alpha 通道换成 secondary 层的真 SDF，按平面原点 (bx,by) 对齐两层
                # （两层字形包围盒不同，位图尺寸也不同）；没有 secondary 的图标
                # 保持全 0（远离字形 → 覆盖度恒 0），与旧管线的 required=False 语义一致。
                sec_cp = secondaries.get(name)
                alpha_field = Image.new("L", (gw, gh), 0)
                if sec_cp is not None and str(sec_cp) in glyphs_secondary:
                    sg = glyphs_secondary[str(sec_cp)]
                    sx, sy = int(sg["x"]), int(sg["y"])
                    sw, sh = int(sg["w"]), int(sg["h"])
                    sec_crop = secondary_png.crop((sx, sy, sx + sw, sy + sh)).getchannel("A")
                    dx = int(round(float(sg["bx"]) - float(glyph["bx"])))
                    dy = int(round(float(sg["by"]) - float(glyph["by"])))
                    alpha_field.paste(sec_crop, (dx, dy))
                piece = Image.merge("RGBA", (piece.getchannel("R"), piece.getchannel("G"), piece.getchannel("B"), alpha_field))
            column = index % columns
            row = index // columns
            cell_x = column * CELL_SIZE
            cell_y = row * CELL_SIZE
            # 字形位图（含 pxrange 内边距）居中放进格子；同码点别名只贴一次
            offset_x = (CELL_SIZE - gw) // 2
            offset_y = (CELL_SIZE - gh) // 2
            if cp not in pasted:
                pasted.add(cp)
                atlas.paste(piece, (cell_x + offset_x, cell_y + offset_y))
            icons_out[name] = {
                "x": cell_x + offset_x,
                "y": cell_y + offset_y,
                "w": gw,
                "h": gh,
            }

        args.output.mkdir(parents=True, exist_ok=True)
        image_name = f"qm_icons_{style}_msdf.png"
        manifest = {
            "version": 2,
            "kind": "mtsdf",
            "distance_field": "mtsdf",
            "alpha_sdf": True,
            "px_range": PX_RANGE,
            "source": "Phosphor font (bundled TTF) baked with msdf-atlas-gen MTSDF",
            "atlas": {
                "image": f"qmclient/icons/{image_name}",
                "width": atlas_w,
                "height": atlas_h,
                "padding": PADDING,
            },
            "icons": icons_out,
        }
        morph_frames_out: list[dict] = []
        if frame_count:
            morph_frames_out = bake_morph_frames(args, atlas, columns, len(cell_of_cp), icons_out, temp_dir)
            manifest["morph_frames"] = morph_frames_out
        if style == "duotone":
            manifest["secondary_mask"] = "alpha"
        atlas.save(args.output / image_name, optimize=True)
        (args.output / f"qm_icons_{style}_msdf.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"  {style}: {len(icons_out)} icons -> {atlas_w}x{atlas_h}")


def main() -> int:
    args = parse_args()
    if not args.tool.is_file():
        raise SystemExit(f"official tool not found: {args.tool}")
    icons = load_codepoints(args.codepoints)
    print(f"codepoints: {len(icons)} named icons")
    for style in args.styles:
        print(f"baking {style}...")
        bake_style(style, args, icons)
    return 0


if __name__ == "__main__":
    sys.exit(main())

