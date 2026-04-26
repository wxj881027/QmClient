#!/usr/bin/env python3
"""
QmClient 配置变量使用检查工具

基于上游 scripts/check_config_variables.py 适配，支持 QmClient 的配置变量文件：
  - src/engine/shared/config_variables.h          (DDNet 上游，无前缀)
  - src/engine/shared/config_variables_tclient.h  (TaterClient 继承，Tc 前缀)
  - src/engine/shared/config_variables_qmclient.h (QmClient 主配置，Qm 前缀)
  - src/engine/shared/config_variables_qmclient_extra.h (QmClient 自研，Qm 前缀)

用法：
  python qmclient_scripts/check_config_variables.py          # 检查所有配置文件
  python qmclient_scripts/check_config_variables.py --ddnet  # 仅检查 DDNet 上游
  python qmclient_scripts/check_config_variables.py --qm     # 仅检查 QmClient 自有
"""

import os
import re
import sys

os.chdir(os.path.dirname(__file__) + "/..")

CONFIG_FILES = {
    'ddnet': 'src/engine/shared/config_variables.h',
    'tclient': 'src/engine/shared/config_variables_tclient.h',
    'qmclient': 'src/engine/shared/config_variables_qmclient.h',
    'qmclient_extra': 'src/engine/shared/config_variables_qmclient_extra.h',
    'qimeng': 'src/engine/shared/config_variables_qimeng.h',
}

def read_all_lines(filename):
    with open(filename, 'r', encoding='utf-8') as file:
        return file.readlines()

def parse_config_variables(lines):
    pattern = r'^MACRO_CONFIG_[A-Z]+\((.*?), (.*?),.*'
    matches = {}
    for line in lines:
        match = re.match(pattern, line)
        if match:
            matches[match.group(1)] = match.group(2)
    return matches

def generate_regex(variable_code):
    return fr'(g_Config\.m_{variable_code}\b|Config\(\)->m_{variable_code}\b|m_pConfig->m_{variable_code}\b)'

def find_config_variables(config_variables):
    variables_not_found = set(config_variables)
    regex_cache = {}
    for variable_code in variables_not_found.copy():
        regex_cache[variable_code] = re.compile(generate_regex(variable_code))
    for root, _, files in os.walk('src'):
        if not variables_not_found:
            break
        for file in files:
            if not variables_not_found:
                break
            if file.endswith(('.cpp', '.h')) and 'external' not in root:
                filepath = os.path.join(root, file)
                with open(filepath, 'r', encoding='utf-8') as f:
                    content = f.read()
                    for variable_code in variables_not_found.copy():
                        if regex_cache[variable_code].search(content):
                            variables_not_found.remove(variable_code)
    return variables_not_found

def check_config_file(name, filepath):
    if not os.path.exists(filepath):
        print(f"Warning: Config file not found: {filepath}")
        return 0
    lines = read_all_lines(filepath)
    config_variables = parse_config_variables(lines)
    if not config_variables:
        print(f"Info: No config variables found in {filepath}")
        return 0
    config_variables_not_found = find_config_variables(config_variables)
    for variable_code in config_variables_not_found:
        print(f"  [{name}] The config variable '{config_variables[variable_code]}' (m_{variable_code}) is unused.")
    if config_variables_not_found:
        return len(config_variables_not_found)
    print(f"  [{name}] Success: No unused config variables found ({len(config_variables)} total).")
    return 0

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Check QmClient config variable usage")
    parser.add_argument('--ddnet', action='store_true', help='Only check DDNet upstream config')
    parser.add_argument('--qm', action='store_true', help='Only check QmClient-specific config (qmclient + qimeng)')
    args = parser.parse_args()

    total_unused = 0

    if args.ddnet:
        files_to_check = {'ddnet': CONFIG_FILES['ddnet']}
    elif args.qm:
        files_to_check = {
            'tclient': CONFIG_FILES['tclient'],
            'qmclient': CONFIG_FILES['qmclient'],
            'qmclient_extra': CONFIG_FILES['qmclient_extra'],
            'qimeng': CONFIG_FILES['qimeng'],
        }
    else:
        files_to_check = CONFIG_FILES

    for name, filepath in files_to_check.items():
        print(f"\nChecking {filepath} ...")
        total_unused += check_config_file(name, filepath)

    if total_unused:
        print(f"\nError: {total_unused} unused config variable(s) found.")
        return 1
    print("\nAll config variables are used.")
    return 0

if __name__ == '__main__':
    sys.exit(main())
