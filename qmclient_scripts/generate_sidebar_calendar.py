"""从香港天文台公农历对照表提取侧栏需要的四个年度日期。"""

import concurrent.futures
import datetime
import pathlib
import re
import urllib.request


ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = ROOT / "tmp" / "sidebar-calendar"
OUTPUT = ROOT / "src/game/client/components/qmclient/qm_sidebar_calendar_data.h"
FIRST_YEAR, LAST_YEAR = 2025, 2100
SOURCE = "https://www.hko.gov.hk/en/gts/time/calendar/text/files/T{}e.txt"


def extract_dates(year, text):
    dates = {}
    month = None
    seen_months = set()
    for line in text.splitlines():
        fields = re.split(r"\s{2,}", line.strip())
        if len(fields) < 3 or not re.fullmatch(r"\d{4}/\d{1,2}/\d{1,2}", fields[0]):
            continue
        current = datetime.date(*map(int, fields[0].split("/")))
        ordinal = (current - datetime.date(year, 1, 1)).days + 1
        lunar = fields[1]
        first = re.fullmatch(r"(\d+)(?:st|nd|rd|th) Lunar Month", lunar)
        if first:
            number = int(first[1])
            # 天文台英文表的闰月重复月份名称，并不总是附带 Leap 标记。
            month = number if number not in seen_months else None
            seen_months.add(number)
            day = 1
        elif "Lunar Month" in lunar:
            # 闰月不重复触发节日。
            month, day = None, 1
        elif lunar.isdigit():
            day = int(lunar)
        else:
            raise ValueError(f"{year}: 无法识别的农历日期 {lunar!r}")
        if month == 1 and day == 1:
            dates["spring"] = ordinal
        if month == 5 and day == 5:
            dates["dragon"] = ordinal
        if month == 8 and day == 15:
            dates["autumn"] = ordinal
        if "Bright & Clear" in line:
            dates["qingming"] = ordinal
    if set(dates) != {"spring", "qingming", "dragon", "autumn"}:
        raise ValueError(f"{year}: 年度日期不完整 {dates}")
    return year, dates


def load_year(year):
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / f"T{year}e.txt"
    if path.exists():
        text = path.read_text(encoding="utf-8-sig")
    else:
        with urllib.request.urlopen(SOURCE.format(year), timeout=25) as response:
            text = response.read().decode("utf-8-sig")
        # 解析成功后才保存，网络错误页不会污染缓存。
        result = extract_dates(year, text)
        path.write_text(text, encoding="utf-8", newline="\n")
        return result
    return extract_dates(year, text)


def main():
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        years = sorted(pool.map(load_year, range(FIRST_YEAR, LAST_YEAR + 1)))
    lines = [
        "// 由 qmclient_scripts/generate_sidebar_calendar.py 生成，不手工修改。",
        "// 来源：https://www.hko.gov.hk/en/gts/time/conversion.htm （香港天文台，2026-10-25）。",
        "// 列为春节、清明、端午、中秋的公历年内序号；不把闰月当作节日。",
        "#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SIDEBAR_CALENDAR_DATA_H",
        "#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SIDEBAR_CALENDAR_DATA_H",
        "",
        "#include <cstdint>",
        "// clang-format off",
        "",
        f"inline constexpr int QM_SIDEBAR_CALENDAR_FIRST_YEAR = {FIRST_YEAR};",
        f"inline constexpr uint16_t QM_SIDEBAR_CALENDAR_DATES[{len(years)}][4] = {{",
    ]
    for year, values in years:
        row = ", ".join(str(values[key]) for key in ("spring", "qingming", "dragon", "autumn"))
        lines.append(f"\t{{{row}}}, // {year}")
    lines += ["};", "// clang-format on", "", "#endif", ""]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"已生成 {FIRST_YEAR}–{LAST_YEAR} 年的侧栏节日日期。")


if __name__ == "__main__":
    main()
