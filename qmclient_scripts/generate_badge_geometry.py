"""将现有徽章 SVG 的路径离线展开为几何四边形，不增加客户端解析依赖。"""

import math
import pathlib
import re
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src/game/client/QmUi/QmBadgeGeometryData.h"
TOKEN = re.compile(r"[MLCHVZ]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?")


def flatten(data, tolerance):
    tokens = TOKEN.findall(data)
    paths, points, index, current = [], [], 0, (0.0, 0.0)

    def curve(a, b, c, d, depth=0):
        distance = lambda p: abs((d[0] - a[0]) * (a[1] - p[1]) - (a[0] - p[0]) * (d[1] - a[1])) / max(math.dist(a, d), 1e-9)
        if depth >= 12 or max(distance(b), distance(c)) <= tolerance:
            points.append(d)
            return
        midpoint = lambda p, q: ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        ab, bc, cd = midpoint(a, b), midpoint(b, c), midpoint(c, d)
        abc, bcd = midpoint(ab, bc), midpoint(bc, cd)
        center = midpoint(abc, bcd)
        curve(a, ab, abc, center, depth + 1)
        curve(center, bcd, cd, d, depth + 1)

    while index < len(tokens):
        command = tokens[index]
        if command not in "MLCHVZ":
            raise ValueError(f"不支持隐式路径指令: {command}")
        index += 1
        count = {"M": 2, "L": 2, "C": 6, "H": 1, "V": 1, "Z": 0}[command]
        values = list(map(float, tokens[index:index + count]))
        index += count
        if command == "M":
            if points:
                paths.append((points, False))
            current = tuple(values)
            points = [current]
        elif command == "L":
            current = tuple(values)
            points.append(current)
        elif command == "H":
            current = (values[0], current[1])
            points.append(current)
        elif command == "V":
            current = (current[0], values[0])
            points.append(current)
        elif command == "C":
            end = tuple(values[4:])
            curve(current, tuple(values[:2]), tuple(values[2:4]), end)
            current = end
        else:
            paths.append((points, True))
            points = []
    if points:
        paths.append((points, False))
    return paths


def stroke_outline(points, closed, width):
    if len(points) > 1 and math.dist(points[0], points[-1]) < 1e-7:
        points = points[:-1]
    if len(points) < 2:
        return []
    normals = []
    for a, b in zip(points, points[1:] + ([points[0]] if closed else [])):
        length = max(math.dist(a, b), 1e-9)
        normals.append((-(b[1] - a[1]) / length, (b[0] - a[0]) / length))
    left, right = [], []
    for index, point in enumerate(points):
        before = normals[(index - 1) % len(normals)] if index or closed else normals[0]
        after = normals[index % len(normals)] if index < len(normals) else normals[-1]
        n = (before[0] + after[0], before[1] + after[1])
        scale = width / 2 / max(0.25, n[0] * after[0] + n[1] * after[1])
        left.append((point[0] + n[0] * scale, point[1] + n[1] * scale))
        right.append((point[0] - n[0] * scale, point[1] - n[1] * scale))
    if closed:
        return [left, list(reversed(right))]
    # 首尾圆帽并入同一轮廓，半透明描边在接缝处不会重复叠加。
    result = left[:]
    angle = math.atan2(normals[-1][1], normals[-1][0])
    result += [(points[-1][0] + math.cos(angle - step * math.pi / 8) * width / 2,
                points[-1][1] + math.sin(angle - step * math.pi / 8) * width / 2) for step in range(1, 8)]
    result += list(reversed(right))
    angle = math.atan2(-normals[0][1], -normals[0][0])
    result += [(points[0][0] + math.cos(angle - step * math.pi / 8) * width / 2,
                points[0][1] + math.sin(angle - step * math.pi / 8) * width / 2) for step in range(1, 8)]
    return [result]


def trapezoids(contours, clip):
    left, top, right, bottom = clip
    edges = [(a, b) for p in contours for a, b in zip(p, p[1:] + p[:1]) if abs(a[1] - b[1]) > 1e-8]
    levels = sorted({top, bottom} | {p[1] for contour in contours for p in contour if top < p[1] < bottom})
    # 在分片边界的交点处分带，保留斜边被裁剪后的真实形状。
    intersections = set(levels)
    for a, b in edges:
        if abs(a[0] - b[0]) < 1e-9:
            continue
        for boundary in (left, right):
            t = (boundary - a[0]) / (b[0] - a[0])
            y = a[1] + t * (b[1] - a[1])
            if 0 < t < 1 and top < y < bottom:
                intersections.add(y)
    levels = sorted(intersections)
    for y0, y1 in zip(levels, levels[1:]):
        if y1 - y0 < 1e-7:
            continue
        ym = (y0 + y1) / 2
        crossing = []
        for a, b in edges:
            if min(a[1], b[1]) < ym < max(a[1], b[1]):
                x = lambda y: a[0] + (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1])
                crossing.append((x(ym), x(y0), x(y1), 1 if b[1] > a[1] else -1))
        winding, start = 0, None
        for _, x0, x1, delta in sorted(crossing):
            previous = winding
            winding += delta
            if not previous and winding:
                start = (x0, x1)
            elif previous and not winding:
                l0, l1 = max(left, min(right, start[0])), max(left, min(right, start[1]))
                r0, r1 = max(left, min(right, x0)), max(left, min(right, x1))
                if max(r0 - l0, r1 - l1) > 1e-7:
                    yield (l0 - left, y0, r0 - left, y0, l1 - left, y1, r1 - left, y1)


def mesh(name, path, clip, developer=False):
    root = ET.parse(ROOT / path).getroot()
    scale = 0.25 if developer else 1.0
    output = []

    def visit(element, opacity=1.0):
        tag = element.tag.rsplit("}", 1)[-1]
        if tag in {"defs", "mask", "clipPath"}:
            return
        opacity *= float(element.get("opacity", "1"))
        if tag == "path":
            paths = flatten(element.attrib["d"], 0.045 / scale)
            paths = [([(x * scale, y * scale) for x, y in points], closed) for points, closed in paths]
            for kind in ("fill", "stroke"):
                color = element.get(kind, "none")
                if color == "none":
                    continue
                contours = [points for points, _ in paths] if kind == "fill" else [outline for points, closed in paths for outline in stroke_outline(points, closed, float(element.get("stroke-width", "1")) * scale)]
                for quad in trapezoids(contours, clip):
                    # 开发者保留原配色，其余图案由玩家所选配色着色。
                    output.append((quad, color[1:] if developer else "ffffff", opacity))
        for child in element:
            visit(child, opacity)

    if developer:
        # 胶囊底色与边框由共享几何入口绘制，此处只展开内部图案与 Dev 字样。
        visit(next(element for element in root if element.tag.endswith("g")))
    else:
        visit(root)
    lines = [f"inline const SQmBadgeGeometryQuad g_aQmBadge{name}[] = {{"]
    for quad, color, opacity in output:
        coords = ", ".join(f"{v:.5f}f" for v in quad)
        lines.append(f"\t{{{{{coords}}}, 0x{color}, {opacity:.5f}f}},")
    lines.append("};")
    print(f"{name}: {len(output)} 个几何四边形")
    return lines


def main():
    lines = ["// 由 qmclient_scripts/generate_badge_geometry.py 从既有 SVG 路径生成，不手工修改。",
             "#ifndef GAME_CLIENT_QMUI_QMBADGEGEOMETRYDATA_H", "#define GAME_CLIENT_QMUI_QMBADGEGEOMETRYDATA_H", "", '#include "QmBadgeGeometry.h"', "", "// clang-format off"]
    lines += mesh("Developer", "data/qmclient/developer_badge.svg", (1, 1, 90, 23), True)
    lines += mesh("Logo", "data/qmclient/title_badges/logo.svg", (0, 0, 16, 16))
    for style in ("Moon", "Sun"):
        for part, left, right in (("Left", 0, 40), ("Middle", 40, 85), ("Right", 85, 117)):
            lines += mesh(style + part, f"data/qmclient/title_badges/{style.lower()}.svg", (left, 0, right, 24))
    lines += ["// clang-format on", "", "#endif", ""]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
