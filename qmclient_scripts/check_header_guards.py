#!/usr/bin/env python3
"""
QmClient 头文件保护宏检查工具

基于上游 scripts/check_header_guards.py 适配，支持 QmClient 的混合命名约定：
  - 上游 DDNet 文件：按路径生成（如 ENGINE_CLIENT_FOO_H）
  - TaterClient 继承文件：使用 TCLIENT 前缀（如 GAME_CLIENT_COMPONENTS_TCLIENT_*_H）
  - QmClient 自研文件：使用 QMCLIENT 前缀（如 GAME_CLIENT_COMPONENTS_QMCLIENT_*_H）
  - QmUi 框架文件：使用 QM_UI 前缀（如 GAME_CLIENT_QM_UI_*_H）
  - RiClient 继承文件：使用 RCLIENT 前缀（如 GAME_CLIENT_COMPONENTS_RCLIENT_*_H）

用法：
  python qmclient_scripts/check_header_guards.py           # 检查所有头文件
  python qmclient_scripts/check_header_guards.py --qm-only # 仅检查 QmClient 特有目录
  python qmclient_scripts/check_header_guards.py --fix     # 输出修复建议
"""

import os
import sys

os.chdir(os.path.dirname(__file__) + "/..")

PATH = "src/"

EXCEPTIONS = [
    "src/base/unicode/confusables.h",
    "src/base/unicode/confusables_data.h",
    "src/base/unicode/tolower.h",
    "src/base/unicode/tolower_data.h",
    "src/tools/config_common.h",
]

GUARD_OVERRIDES = {
    "src/game/client/components/qmclient/qmclient.h": "GAME_CLIENT_COMPONENTS_QMCLIENT_QMCLIENT_H",
    "src/game/client/components/qmclient/input_overlay.h": "GAME_CLIENT_COMPONENTS_TCLIENT_INPUT_OVERLAY_H",
    "src/game/client/components/qmclient/colored_parts.h": "GAME_CLIENT_COMPONENTS_TCLIENT_COLORED_PARTS_H",
    "src/game/client/components/qmclient/translate.h": "GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATE_H",
    "src/game/client/components/qmclient/data_version.h": "GAME_CLIENT_COMPONENTS_TCLIENT_DATA_VERSION_H",
    "src/game/client/components/qmclient/voice_core.h": "GAME_CLIENT_COMPONENTS_RCLIENT_VOICE_H",
    "src/game/client/components/qmclient/voice_component.h": "GAME_CLIENT_COMPONENTS_QMCLIENT_VOICE_COMPONENT_H",
    "src/game/client/components/qmclient/scripting/impl.h": "GAME_CLIENT_COMPONENTS_TCLIENT_SCRIPTING_SCRIPTING_H",
    "src/game/client/components/qmclient/scripting.h": "GAME_CLIENT_COMPONENTS_TCLIENT_CHAISCRIPT_COMPONENT_H",
    "src/game/client/components/qmclient/lyrics_component.h": "GAME_CLIENT_COMPONENTS_TCLIENT_LYRICS_COMPONENT_H",
    "src/game/client/components/qmclient/collision_hitbox.h": "GAME_CLIENT_COMPONENTS_TCLIENT_COLLISION_HITBOX_H",
    "src/game/client/components/qmclient/jelly_tee.h": "GAME_CLIENT_COMPONENTS_QMCLIENT_JELLY_TEE_H",
    "src/game/client/QmUi/QmTree.h": "GAME_CLIENT_QM_UI_QM_TREE_H",
    "src/game/client/QmUi/QmLegacy.h": "GAME_CLIENT_QM_UI_QM_LEGACY_H",
    "src/game/client/QmUi/QmRender.h": "GAME_CLIENT_QM_UI_QM_RENDER_H",
    "src/game/client/QmUi/QmRt.h": "GAME_CLIENT_QM_UI_QM_RT_H",
    "src/game/client/QmUi/QmAnim.h": "GAME_CLIENT_QM_UI_QM_ANIM_H",
    "src/game/client/QmUi/QmLayout.h": "GAME_CLIENT_QM_UI_QM_LAYOUT_H",
}

QMCLIENT_DIRS = [
    "src/game/client/components/qmclient/",
    "src/game/client/components/tclient/",
    "src/game/client/QmUi/",
]

def path_to_guard(filename):
    return "_".join(filename.split(PATH)[1].split("/"))[:-2].upper() + "_H"

def get_expected_guard(filename):
    if filename in GUARD_OVERRIDES:
        return "#ifndef " + GUARD_OVERRIDES[filename]
    return "#ifndef " + path_to_guard(filename)

def is_qmclient_file(filename):
    return any(filename.startswith(d) for d in QMCLIENT_DIRS)

def check_file(filename, show_fix=False):
    if filename in EXCEPTIONS:
        return False
    error = False
    with open(filename, encoding="utf-8") as file:
        for line in file:
            if line == "// This file can be included several times.\n":
                break
            stripped = line.lstrip()
            if stripped.startswith("/") or stripped.startswith("*") or stripped == "" or stripped == "\r\n" or stripped == "\n":
                continue
            expected_guard = get_expected_guard(filename)
            path_guard = "#ifndef " + path_to_guard(filename)
            if line.startswith("#ifndef"):
                if line[:-1] != expected_guard:
                    error = True
                    source = "QMClient override" if filename in GUARD_OVERRIDES else "path-based"
                    print(f"Wrong header guard in {filename}")
                    print(f"  is:      {line[:-1]}")
                    print(f"  expect:  {expected_guard} ({source})")
                    if line[:-1] != path_guard and filename not in GUARD_OVERRIDES:
                        print(f"  path:    {path_guard}")
                    if show_fix:
                        print(f"  fix:     sed -i 's/{line[:-1].replace('#ifndef ', '')}/{expected_guard.replace('#ifndef ', '')}/' {filename}")
            else:
                error = True
                print(f"Missing header guard in {filename}, should be: {expected_guard}")
            break
    return error

def check_dir(directory, qm_only=False, show_fix=False):
    errors = 0
    file_list = os.listdir(directory)
    for file in file_list:
        path = directory + file
        if os.path.isdir(path):
            if file not in ("external", "generated", "rust-bridge"):
                errors += check_dir(path + "/", qm_only, show_fix)
        elif file.endswith(".h") and file != "keynames.h":
            if qm_only and not is_qmclient_file(path):
                continue
            errors += check_file(path, show_fix)
    return errors

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Check QmClient header guards")
    parser.add_argument('--qm-only', action='store_true', help='Only check QmClient-specific directories')
    parser.add_argument('--fix', action='store_true', help='Show fix suggestions')
    args = parser.parse_args()

    errors = check_dir(PATH, qm_only=args.qm_only, show_fix=args.fix)
    if errors:
        print(f"\nFound {errors} header guard issue(s).")
    else:
        print("All header guards are correct.")
    return int(errors != 0)

if __name__ == '__main__':
    sys.exit(main())
