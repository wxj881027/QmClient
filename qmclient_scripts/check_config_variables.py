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

import os
import re
import sys

CONFIG_FILES = {
    "ddnet": "src/engine/shared/config_variables.h",
    "tclient": "src/engine/shared/config_variables_tclient.h",
    "qmclient": "src/engine/shared/config_variables_qmclient.h",
}


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
                    for variable_code in variables_not_found.copy():
                        if regex_cache[variable_code].search(content):
                            variables_not_found.remove(variable_code)
    return variables_not_found


def check_config_file(name, filepath):
    if not os.path.exists(filepath):
        print(f"警告：未找到配置文件 {filepath}")
        return 0
    lines = read_all_lines(filepath)
    config_variables = parse_config_variables(lines)
    if not config_variables:
        print(f"提示：{filepath} 中未解析到配置变量")
        return 0
    config_variables_not_found = find_config_variables(config_variables)
    for variable_code in config_variables_not_found:
        print(
            f"  [{name}] 未使用配置项：'{config_variables[variable_code]}' (m_{variable_code})"
        )
    if config_variables_not_found:
        return len(config_variables_not_found)
    print(f"  [{name}] 通过：未发现未使用配置项（共 {len(config_variables)} 个）")
    return 0


def main():
    import argparse

    os.chdir(os.path.dirname(__file__) + "/..")

    parser = argparse.ArgumentParser(description="检查 QmClient 配置变量是否被实际使用")
    parser.add_argument(
        "--ddnet", action="store_true", help="只检查 DDNet 上游配置文件"
    )
    parser.add_argument(
        "--qm", action="store_true", help="只检查 QmClient / TClient / 栖梦自有配置文件"
    )
    args = parser.parse_args()

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
        total_unused += check_config_file(name, filepath)

    if total_unused:
        print(f"\n失败：共发现 {total_unused} 个未使用配置项。")
        return 1
    print("\n通过：所有配置项均已被使用。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
