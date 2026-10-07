"""生成 data/fonts/fonts_store/font_catalog.json —— 字体商店在线字体目录。

数据来源：
1. google/fonts 仓库文件树（api.github.com 一次递归 tree 请求），得到每个字体的
   仓库相对路径；下载 URL 在客户端运行时按 ghproxy.net / ghfast.top /
   raw.githubusercontent.com 链拼接。
2. google-font-metadata（npm 包，经 registry.npmmirror.com 获取，国内可达）提供
   字体族显示名与分类；失败时回退 fonts.google.com 元数据，再失败则由目录 slug
   推导显示名。

用法：
    python qmclient_scripts/generate_font_catalog.py
    # 离线重生成（使用已下载的缓存）：
    python qmclient_scripts/generate_font_catalog.py \
        --tree tmp/fontcatalog/gf_tree.json \
        --meta tmp/fontcatalog/package/data/google-fonts-v2.json

目录条目为「每个字体文件一条」：
    {"n": 族名, "c": 分类, "f": google/fonts 仓库相对路径, "v": 1(可变字体才有)}
可变字体单文件覆盖全部字重，优先排在该族首位（运行时预览取首个文件）。
条目可含 "s"：按 UI_SUBSET_WHITELIST 过滤后的脚本子集（japanese/korean/...），
供商店按界面语言排序与徽标标注；仅 latin 覆盖的族不写。

国内直链（可选字段 "u"）：对「族内仅一个文件且为静态」的拉丁族，向 Google Fonts
中国镜像 CSS API（fonts.googleapis.cn，国内合规可达）请求该族，解析出完整 TTF
直链（fonts.gstatic.cn，同样国内可达）写入条目；客户端下载时把该直链排在 GitHub
加速链之前。排除项与原因：
- CJK 族（subsets 含中文/日文/韩文）：镜像老接口对 CJK 只返回拉丁子集文件，
  缺汉字字形，必须走 google/fonts 仓库完整文件；
- 可变字体族：老接口返回静态 400 实例，会丢失字重轴；
- 多文件族：无法保证各样式文件与镜像返回的一一对应。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.parse
import urllib.request
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "data" / "fonts" / "fonts_store" / "font_catalog.json"
TREE_URL = "https://api.github.com/repos/google/fonts/git/trees/main?recursive=1"
GFM_PKG_URL = "https://registry.npmmirror.com/google-font-metadata/latest"
GF_META_URL = "https://fonts.google.com/metadata/fonts"
LICENSE_DIRS = ("ofl", "apache", "ufl")
VARIABLE_NAME_RE = re.compile(r"\[[^\]]*wght[^\]]*\]")
# Google Fonts 中国镜像 CSS API：老式接口返回完整 TTF 直链（fonts.gstatic.cn）。
GSTATIC_CSS_URL = "https://fonts.googleapis.cn/css?family="
GSTATIC_TTF_RE = re.compile(r"url\((https://fonts\.gstatic\.cn/[^)]+\.ttf)\)")
# CJK 判定：镜像老接口对 CJK 族只返回拉丁子集（缺汉字），必须排除。
CJK_SUBSET_MARKERS = ("chinese-simplified", "chinese-traditional", "japanese", "korean")
# 商店徽标/排序只关心客户端语言文件能映射到的脚本子集（见 menus_tclient.cpp
# fontstore::LanguageSubsetKey），按白名单写入条目 "s" 字段控制目录体积；
# latin/latin-ext 是默认假设不写。
UI_SUBSET_WHITELIST = ("arabic", "chinese-simplified", "chinese-traditional", "cyrillic", "greek", "japanese", "korean")

# 标准字重文件名的样式段（"Family-SemiBold.ttf" 的 "SemiBold"），用于静态字体排序。
_STATIC_ORDER = {
    "thin": 0,
    "extralight": 1,
    "light": 2,
    "regular": 3,
    "medium": 4,
    "semibold": 5,
    "bold": 6,
    "extrabold": 7,
    "black": 8,
}


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "QmClient-font-catalog/1.0"})
    with urllib.request.urlopen(request, timeout=180) as response:
        return response.read()


def load_tree(path: str | None) -> list[str]:
    if path:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    else:
        data = json.loads(fetch(TREE_URL).decode("utf-8"))
    if data.get("truncated"):
        raise SystemExit("github tree response is truncated; rerun later")
    paths = [entry["path"] for entry in data["tree"] if entry.get("type") == "blob"]
    return [p for p in paths if p.endswith(".ttf") and "/" in p and p.split("/", 1)[0] in LICENSE_DIRS]


def load_family_meta(path: str | None) -> dict[str, dict]:
    """返回 slug -> {"family": 显示名, "category": 分类, "subsets": 语言列表}。"""
    if path:
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
    else:
        # 优先 npmmirror（国内可达），失败回退 Google 官方元数据。
        try:
            pkg = json.loads(fetch(GFM_PKG_URL).decode("utf-8"))
            tarball = pkg["dist"]["tarball"]
            # 只取包内 data/google-fonts-v2.json，避免落盘整个 tarball。
            import io
            import tarfile

            blob = fetch(tarball)
            with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as archive:
                member = next(m for m in archive.getmembers() if m.name.endswith("data/google-fonts-v2.json"))
                raw = json.load(archive.extractfile(member))
        except Exception as error:  # noqa: BLE001 - 回退链最后一环要兜住一切
            print(f"google-font-metadata unavailable ({error}); falling back to fonts.google.com", file=sys.stderr)
            text = fetch(GF_META_URL).decode("utf-8")
            if text.startswith(")]}'"):
                text = text.split("\n", 1)[1]
            official = json.loads(text)
            raw = {
                item["family"].lower().replace(" ", "-"): {
                    "family": item["family"],
                    "category": str(item.get("category", "")).lower().replace("_", "-"),
                    "subsets": [str(s) for s in item.get("subsets", [])],
                }
                for item in official.get("familyMetadataList", [])
            }
            return raw
    return {
        # google/fonts 仓库目录 slug 不含连字符（abhayalibre），npm 元数据 id 含连字符
        # （abhaya-libre）：两侧统一去掉 '-' 再匹配。
        re.sub("-", "", slug): {
            "family": entry.get("family", slug),
            "category": entry.get("category", ""),
            "subsets": [str(s) for s in entry.get("subsets", [])],
        }
        for slug, entry in raw.items()
    }


def prettify_slug(slug: str) -> str:
    return " ".join(part.capitalize() for part in slug.split("-"))


def static_rank(file_name: str) -> tuple[int, str]:
    stem = file_name[:-4]
    style = stem.rsplit("-", 1)[-1] if "-" in stem else ""
    return (_STATIC_ORDER.get(style.lower(), 9), style.lower())


def resolve_gstatic_url(family: str, timeout: float = 15.0) -> str | None:
    """向中国镜像 CSS API 请求该族，返回完整 TTF 直链；不可用返回 None。

    老式接口对未知族返回 404，对 CJK 族返回拉丁子集 —— 后者由调用方按 subsets
    预先排除，这里只做请求与正则解析。
    """
    query = urllib.parse.quote(family)
    request = urllib.request.Request(
        GSTATIC_CSS_URL + query,
        headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/126.0 Safari/537.36"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            css = response.read().decode("utf-8", errors="replace")
    except Exception:  # noqa: BLE001 - 单族失败只降级为无直链
        return None
    match = GSTATIC_TTF_RE.search(css)
    return match.group(1) if match else None


def resolve_gstatic_urls(families: list[str], cache_path: Path, workers: int = 16) -> dict[str, str | None]:
    """并发解析各族的 gstatic 直链，带本地缓存（断点重跑不重复请求）。"""
    cache: dict[str, str | None] = {}
    if cache_path.exists():
        try:
            cache = json.loads(cache_path.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001 - 缓存损坏时全部重拉
            cache = {}
    pending = [family for family in families if family not in cache]
    if pending:
        print(f"resolving gstatic.cn direct links for {len(pending)} families ({workers} workers)...", file=sys.stderr)
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for family, url in zip(pending, pool.map(resolve_gstatic_url, pending)):
                cache[family] = url
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(json.dumps(cache, ensure_ascii=False, indent=0), encoding="utf-8", newline="\n")
    return cache


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", help="本地 google/fonts tree JSON（缺省时在线拉取）")
    parser.add_argument("--meta", help="本地 google-fonts-v2.json（缺省时在线拉取）")
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    parser.add_argument("--gstatic-cache", default=str(REPO_ROOT / "tmp" / "fontcatalog" / "gstatic_urls.json"))
    parser.add_argument("--no-gstatic", action="store_true", help="跳过中国镜像直链解析")
    args = parser.parse_args()

    files = load_tree(args.tree)
    meta = load_family_meta(args.meta)

    by_slug: dict[str, list[str]] = defaultdict(list)
    for path in files:
        parts = path.split("/")
        # 只收族目录根文件：ofl/<族>/Xxx.ttf；static/ 子目录（ofl/<族>/static/...）
        # 是可变字体的静态实例展开，单条可变字体文件已覆盖全部字重，全部剔除。
        if len(parts) < 3 or "static" in parts[2:-1]:
            continue
        by_slug[parts[1]].append(path)

    def family_info(slug: str) -> tuple[str, str, list[str], bool]:
        info = meta.get(slug)
        family = info["family"] if info and info.get("family") else prettify_slug(slug)
        category = info.get("category", "") if info else ""
        subsets = info.get("subsets", []) if info else []
        is_cjk = any(marker in subsets for marker in CJK_SUBSET_MARKERS)
        return family, category, subsets, is_cjk

    # 中国镜像直链只解析「族内仅一个文件且为静态」的拉丁族：内容保证与请求一致
    # （老接口固定返回 400 Regular 静态实例），失败/超时的族自然只保留 GitHub 链。
    direct_urls: dict[str, str | None] = {}
    if not args.no_gstatic:
        candidates = []
        for slug, paths in by_slug.items():
            if len(paths) != 1 or VARIABLE_NAME_RE.search(paths[0].rsplit("/", 1)[-1]):
                continue
            if family_info(slug)[3]:
                continue
            candidates.append(family_info(slug)[0])
        direct_urls = resolve_gstatic_urls(sorted(set(candidates)), Path(args.gstatic_cache))

    fonts: list[dict] = []
    missing_names = 0
    direct_count = 0
    for slug, paths in by_slug.items():
        family, category, subsets, is_cjk = family_info(slug)
        if not meta.get(slug):
            missing_names += 1

        def sort_key(path: str) -> tuple:
            name = path.rsplit("/", 1)[-1]
            variable = bool(VARIABLE_NAME_RE.search(name))
            # 可变字体最优先（单文件覆盖全部字重），正体先于斜体；其次静态 Regular，
            # 再按样式段排序。预览与默认下载都取族内首条。
            upright_first = 0 if "ital" not in name.lower() else 1
            if variable:
                return (0, upright_first, "")
            return (1, upright_first, f"{static_rank(name)}{name}")

        for path in sorted(paths, key=sort_key):
            entry: dict = {"n": family, "c": category, "f": path}
            if VARIABLE_NAME_RE.search(path.rsplit("/", 1)[-1]):
                entry["v"] = 1
            elif len(paths) == 1 and not is_cjk:
                direct = direct_urls.get(family)
                if direct:
                    entry["u"] = direct
                    direct_count += 1
            ui_subsets = [s for s in subsets if s in UI_SUBSET_WHITELIST]
            if ui_subsets:
                entry["s"] = ui_subsets
            fonts.append(entry)

    # 输出文件名（下载落盘名 = 路径基名）必须唯一，避免用户字体目录相互覆盖；
    # 个别字体在仓库里有新旧两套目录（如 NotoSansNKo），冲突时后出现的加 slug 前缀。
    seen_basenames: set[str] = set()
    for entry in fonts:
        base = entry["f"].rsplit("/", 1)[-1]
        if base in seen_basenames:
            slug = entry["f"].split("/")[1]
            base = f"{slug}-{base}"
            entry["f"] = f"{entry['f'].rsplit('/', 1)[0]}/{base}"
        seen_basenames.add(base)

    catalog = {"version": 1, "fonts": fonts}
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(catalog, ensure_ascii=False, separators=(",", ":")), encoding="utf-8", newline="\n")

    families = len({entry["n"].lower() for entry in fonts})
    variable_count = sum(entry.get("v", 0) for entry in fonts)
    size_kb = out_path.stat().st_size / 1024
    print(f"families={families} files={len(fonts)} variable={variable_count} gstatic_direct={direct_count} nameless_slug_fallback={missing_names}")
    print(f"wrote {out_path} ({size_kb:.0f} KB)")
    probe = [e for e in fonts if e["n"].lower() in ("jetbrains mono", "noto serif sc", "noto sans sc")]
    for entry in probe:
        print("probe:", entry)


if __name__ == "__main__":
    main()
