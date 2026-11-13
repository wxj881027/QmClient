# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""
QmClient 配置变量使用检查工具。

基于上游 scripts/check_config_variables.py 适配，支持 QmClient 的配置变量文件：
  - src/engine/shared/config_variables.h
  - src/engine/shared/config_variables_tclient.h
  - src/engine/shared/config_variables_qmclient.h

用法：
  python qmclient_scripts/check_config_variables.py
  python qmclient_scripts/check_config_variables.py --ddnet
  python qmclient_scripts/check_config_variables.py --qm
"""

import ast
import json
import os
import re
import sys
from pathlib import Path

CONFIG_FILES = {
    "ddnet": "src/engine/shared/config_variables.h",
    "tclient": "src/engine/shared/config_variables_tclient.h",
    "qmclient": "src/engine/shared/config_variables_qmclient.h",
}
COMPATIBILITY_FILE = Path(__file__).resolve().with_name("config_compatibility.json")


def read_all_lines(filename):
    with open(filename, "r", encoding="utf-8") as file:
        return file.readlines()


def parse_config_variables(lines):
    pattern = r"^MACRO_CONFIG_[A-Z]+\((.*?), (.*?),.*"
    matches = {}
    for line in lines:
        match = re.match(pattern, line)
        if match:
            matches[match.group(1)] = match.group(2)
    return matches


def load_compatibility_rules(filepath=COMPATIBILITY_FILE):
    """保存兼容项独立声明，不作为活动引用计数。"""
    data = json.loads(Path(filepath).read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise ValueError("保存兼容声明的 schema_version 必须为 1")
    groups = data.get("saved_client_configs")
    if not isinstance(groups, dict):
        raise ValueError("保存兼容声明缺少 saved_client_configs")
    for name, rules in groups.items():
        if name not in CONFIG_FILES or not isinstance(rules, dict):
            raise ValueError(f"保存兼容声明的配置分组无效：{name}")
        for variable_code, rule in rules.items():
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", variable_code):
                raise ValueError(f"保存兼容成员名无效：{variable_code}")
            if not isinstance(rule, dict) or set(rule) != {"script_name", "kind", "reason"}:
                raise ValueError(f"保存兼容声明字段无效：{variable_code}")
            if not isinstance(rule["script_name"], str) or not re.fullmatch(r"[a-z_][a-z0-9_]*", rule["script_name"]):
                raise ValueError(f"保存兼容命令名无效：{variable_code}")
            if rule["kind"] not in ("INT", "COL", "STR"):
                raise ValueError(f"保存兼容类型无效：{variable_code}")
            if not isinstance(rule["reason"], str) or not rule["reason"].strip():
                raise ValueError(f"保存兼容声明必须说明保留原因：{variable_code}")
    return groups


def parse_config_declarations(lines):
    """提取类型与标志，保留带逗号、括号或转义引号的宏参数边界。"""
    declarations = {}
    for line in lines:
        match = re.match(r"^MACRO_CONFIG_([A-Z]+)\(", line)
        if not match:
            continue
        arguments = []
        start = match.end()
        depth = 0
        quote = None
        escaped = False
        for index in range(start, len(line)):
            character = line[index]
            if quote is not None:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == quote:
                    quote = None
            elif character in ("\"", "'"):
                quote = character
            elif character == ")" and depth == 0:
                arguments.append(line[start:index].strip())
                kind = match.group(1)
                flag_index = {"INT": 5, "COL": 3, "STR": 4}.get(kind)
                if flag_index is not None and len(arguments) > flag_index:
                    declarations[arguments[0]] = (kind, arguments[flag_index])
                break
            elif character in "([{":
                depth += 1
            elif character in ")]}":
                depth -= 1
            elif character == "," and depth == 0:
                arguments.append(line[start:index].strip())
                start = index + 1
    return declarations


def config_flag_names(expression):
    def names(node):
        if isinstance(node, ast.Name) and re.fullmatch(r"CFGFLAG_[A-Z0-9_]+", node.id):
            return {node.id}
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
            return names(node.left) | names(node.right)
        if isinstance(node, ast.Constant) and node.value == 0:
            return set()
        raise ValueError("保存兼容标志必须是明确的 CFGFLAG 按位或表达式")

    return names(ast.parse(expression, mode="eval").body)


def classify_config_variables(variables, unused, declarations, compatibility):
    """兼容项必须维持原声明并且没有活动消费者，失配仍阻断检查。"""
    compatible = set()
    errors = []
    for variable_code, rule in compatibility.items():
        problem = None
        if variable_code not in variables:
            problem = "对应声明已不存在"
        elif variables[variable_code] != rule["script_name"]:
            problem = "命令名与兼容声明不匹配"
        elif variable_code not in declarations or declarations[variable_code][0] != rule["kind"]:
            problem = "配置类型与兼容声明不匹配"
        else:
            try:
                flags = config_flag_names(declarations[variable_code][1])
                if not {"CFGFLAG_CLIENT", "CFGFLAG_SAVE"}.issubset(flags):
                    problem = "必须保留 CFGFLAG_CLIENT 和 CFGFLAG_SAVE"
            except (SyntaxError, ValueError):
                problem = "无法确认保存兼容标志"
        if problem is None and variable_code not in unused:
            problem = "重新出现静态值引用，需要移出仅保存兼容分类"
        if problem is not None:
            errors.append(f"{variable_code}: {problem}")
        else:
            compatible.add(variable_code)
    return set(unused) - set(compatibility), compatible, errors


def generate_regex(variable_code, script_name):
    command_name = re.escape(script_name)
    return (
        rf"(\b[A-Za-z_]\w*(?:\.|->)m_{variable_code}\b|"
        rf"Config\(\)->m_{variable_code}\b|"
        rf"\bCConfig::m_{variable_code}\b|"
        rf"Console\(\)->(?:Register|Chain)\(\s*\"{command_name}\"|"
        rf"Console\(\)->ExecuteLine\([^;]*\"{command_name}\")"
    )


def find_config_variables(config_variables, source_root="src"):
    variables_not_found = set(config_variables)
    regex_cache = {}
    for variable_code in variables_not_found.copy():
        regex_cache[variable_code] = re.compile(
            generate_regex(variable_code, config_variables[variable_code])
        )
    for root, _, files in os.walk(source_root):
        if not variables_not_found:
            break
        for file in files:
            if not variables_not_found:
                break
            if file.endswith((".cpp", ".h")) and "external" not in root:
                filepath = os.path.join(root, file)
                with open(filepath, "r", encoding="utf-8") as f:
                    content = f.read()
                    # 先筛出可能命中的成员和命令，再保留原正则核对上下文。
                    # 避免对每个未使用配置项重复扫描所有大文件。
                    members = set(re.findall(r"\bm_(\w+)", content))
                    for variable_code in variables_not_found.copy():
                        if variable_code not in members and f'"{config_variables[variable_code]}' not in content:
                            continue
                        if regex_cache[variable_code].search(content):
                            variables_not_found.remove(variable_code)
    return variables_not_found


def check_config_file(name, filepath, compatibility=None, source_root="src"):
    compatibility = compatibility or {}
    if not os.path.exists(filepath):
        print(f"警告：未找到配置文件 {filepath}")
        return len(compatibility)
    lines = read_all_lines(filepath)
    config_variables = parse_config_variables(lines)
    if not config_variables:
        print(f"提示：{filepath} 中未解析到配置变量")
    unused = find_config_variables(config_variables, source_root=source_root)
    config_variables_not_found, compatible, errors = classify_config_variables(
        config_variables, unused,
        parse_config_declarations(lines) if compatibility else {}, compatibility
    )
    for variable_code in sorted(config_variables_not_found):
        print(
            f"  [{name}] 未使用配置项：'{config_variables[variable_code]}' (m_{variable_code})"
        )
    for variable_code in sorted(compatible):
        rule = compatibility[variable_code]
        print(f"  [{name}] 仅保存兼容：'{rule['script_name']}' (m_{variable_code})；{rule['reason']}")
    for error in errors:
        print(f"  [{name}] 保存兼容声明失效：{error}")
    if config_variables_not_found or errors:
        return len(config_variables_not_found) + len(errors)
    print(f"  [{name}] 通过：活动配置 {len(config_variables) - len(compatible)} 个，保存兼容配置 {len(compatible)} 个")
    return 0


def main():
    import argparse

    os.chdir(os.path.dirname(__file__) + "/..")

    parser = argparse.ArgumentParser(description="检查配置的静态引用及仅保存兼容声明")
    parser.add_argument(
        "--ddnet", action="store_true", help="只检查 DDNet 上游配置文件"
    )
    parser.add_argument(
        "--qm", action="store_true", help="只检查 QmClient / TClient / 栖梦自有配置文件"
    )
    args = parser.parse_args()

    try:
        compatibility = load_compatibility_rules()
    except (OSError, ValueError) as error:
        print(f"保存兼容声明读取失败：{error}")
        return 1

    total_unused = 0

    if args.ddnet:
        files_to_check = {"ddnet": CONFIG_FILES["ddnet"]}
    elif args.qm:
        files_to_check = {
            "tclient": CONFIG_FILES["tclient"],
            "qmclient": CONFIG_FILES["qmclient"],
        }
    else:
        files_to_check = CONFIG_FILES

    for name, filepath in files_to_check.items():
        print(f"\n检查 {filepath} ...")
        total_unused += check_config_file(name, filepath, compatibility.get(name, {}))

    if total_unused:
        print(f"\n失败：共发现 {total_unused} 个配置使用或保存兼容问题。")
        return 1
    print("\n通过：活动配置均找到引用，保存兼容配置符合声明。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
