"""比较相同固定样本在真实 provider 下的候选输出；不把简称歧义称为字库覆盖率。"""

from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import statistics


def read_results(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if not rows:
        raise ValueError(f"空评测结果：{path}")
    return rows


def summarize(rows: list[dict[str, str]]) -> dict:
    ranks = [int(row["rank"]) for row in rows]
    times = sorted(int(row["microseconds"]) / 1000 for row in rows)
    return {
        "count": len(rows),
        **{f"top{limit}": sum(1 <= rank <= limit for rank in ranks) for limit in (1, 5, 9)},
        "input_ms_p50": round(statistics.median(times), 3),
        "input_ms_p95": round(times[int((len(times) - 1) * 0.95)], 3),
    }


def compare(before: Path, after: Path, output: Path) -> dict:
    old, new = read_results(before), read_results(after)
    def identity(row):
        return tuple(row[key] for key in ("id", "kind", "code", "expected"))
    if [identity(row) for row in old] != [identity(row) for row in new]:
        raise ValueError("两轮评测样本或顺序不同")
    grouped = defaultdict(lambda: ([], []))
    improved, regressed = [], []
    for left, right in zip(old, new):
        grouped[left["kind"]][0].append(left)
        grouped[left["kind"]][1].append(right)
        if left["rank"] != "1" and right["rank"] == "1":
            improved.append({"kind": left["kind"], "code": left["code"], "expected": left["expected"], "before": left["first"], "after": right["first"]})
        elif left["rank"] == "1" and right["rank"] != "1":
            regressed.append({"kind": left["kind"], "code": left["code"], "expected": left["expected"], "before": left["first"], "after": right["first"]})
    result = {"before": str(before), "after": str(after), "overall": {"before": summarize(old), "after": summarize(new)}, "groups": {key: {"before": summarize(values[0]), "after": summarize(values[1])} for key, values in sorted(grouped.items())}, "top1_improved": len(improved), "top1_regressed": len(regressed)}
    lines = ["# 独立语料候选对比", "", "相同拼音输入能否还原原始中文的严格匹配统计。简拼存在歧义；结果不能解释为玩家实际输入正确率或词库覆盖百分比。每项输入经真实 Qm provider 与纯核心 librime，不提交、不学习。", "", "| 类型 | 数量 | 首选（前→后） | 前五（前→后） | 前九（前→后） | 整串输入中位耗时 ms（前→后） |", "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for key, values in result["groups"].items():
        left, right = values["before"], values["after"]
        count = left["count"]
        cells = [f'{left[f"top{limit}"]}/{count} ({100 * left[f"top{limit}"] / count:.1f}%) → {right[f"top{limit}"]}/{count} ({100 * right[f"top{limit}"] / count:.1f}%)' for limit in (1, 5, 9)]
        lines.append(f'| {key} | {count} | {" | ".join(cells)} | {left["input_ms_p50"]} → {right["input_ms_p50"]} |')
    lines += ["", f"首选从未命中变为命中：{len(improved)}；从命中变为未命中：{len(regressed)}。", "", "全部改善和退化样本保存在相邻 JSON 中；不只挑选成功例子。耗时是整串按键的探针观察值，包含逐键候选刷新，不是帧或 UI 实测。"]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    (output.with_suffix(".json")).write_text(json.dumps({**result, "improvements": improved, "regressions": regressed}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compare(args.before, args.after, args.output)
