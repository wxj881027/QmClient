#!/usr/bin/env python3
"""核对压缩后 APK 是否保留 SDL 声明的 JNI Java 回调，不代替 Android 运行测试。"""

import argparse
import re
import struct
import zipfile
from pathlib import Path


def uleb(data, offset):
    value = 0
    for shift in range(0, 35, 7):
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError("Invalid DEX ULEB128")


def dex_methods(data):
    if not data.startswith(b"dex\n") or int(data[4:7]) > 40:
        raise ValueError("Unsupported DEX header")
    string_count, string_offset, type_count, type_offset = struct.unpack_from("<4I", data, 56)
    proto_count, proto_offset, _, _, _, method_offset, class_count, class_offset = struct.unpack_from("<8I", data, 72)
    strings = []
    for index in range(string_count):
        offset = struct.unpack_from("<I", data, string_offset + 4 * index)[0]
        _, offset = uleb(data, offset)
        # JNI 类名、方法名和描述符均为 ASCII；其他 DEX 字符串不参与匹配。
        strings.append(data[offset:data.index(0, offset)].decode("utf-8", "replace"))
    types = [strings[struct.unpack_from("<I", data, type_offset + 4 * i)[0]] for i in range(type_count)]
    protos = []
    for index in range(proto_count):
        _, result, parameters = struct.unpack_from("<3I", data, proto_offset + 12 * index)
        args = []
        if parameters:
            count = struct.unpack_from("<I", data, parameters)[0]
            args = [types[struct.unpack_from("<H", data, parameters + 4 + 2 * i)[0]] for i in range(count)]
        protos.append("(" + "".join(args) + ")" + types[result])
    found = set()
    for index in range(class_count):
        class_index = struct.unpack_from("<I", data, class_offset + 32 * index)[0]
        offset = struct.unpack_from("<I", data, class_offset + 32 * index + 24)[0]
        if not offset:
            continue
        counts = []
        for _ in range(4):
            count, offset = uleb(data, offset)
            counts.append(count)
        for _ in range(counts[0] + counts[1]):
            _, offset = uleb(data, offset)
            _, offset = uleb(data, offset)
        for count in counts[2:]:
            method_index = 0
            for _ in range(count):
                delta, offset = uleb(data, offset)
                method_index += delta
                _, offset = uleb(data, offset)
                _, offset = uleb(data, offset)
                owner, proto, name = struct.unpack_from("<HHI", data, method_offset + 8 * method_index)
                if owner != class_index:
                    raise ValueError("DEX method owner differs from its class")
                found.add((types[owner], strings[name], protos[proto]))
    return found


def required_methods(sdl_root):
    core = (sdl_root / "src/core/android/SDL_android.c").read_text(encoding="utf-8")
    hid = (sdl_root / "src/hidapi/android/hid.cpp").read_text(encoding="utf-8")
    required = set()
    # 以原生端实际注册和查找的 JNI 声明为准，避免 SDL 保留规则中的历史重载造成误报。
    tables = dict(re.findall(r'JNINativeMethod\s+(\w+)\s*\[[^]]*\]\s*=\s*\{(.*?)\n\};', core + "\n" + hid, re.S))
    registrations = re.findall(r'register_methods\(env,\s*"([^"]+)",\s*(\w+),', core)
    if len(registrations) < 5:
        raise ValueError("SDL JNI registration layout changed")
    for owner, table in registrations:
        if table not in tables:
            raise ValueError(f"Missing native registration table: {table}")
        for name, signature in re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"', tables[table]):
            required.add(("L" + owner + ";", name, signature))
    owners = {
        "mActivityClass": "SDLActivity", "mAudioManagerClass": "SDLAudioManager",
        "mControllerManagerClass": "SDLControllerManager", "g_HIDDeviceManagerCallbackClass": "HIDDeviceManager",
    }
    for variable, name in owners.items():
        pattern = r'Get(?:Static)?MethodID\(\s*(?:env,\s*)?' + variable + r',\s*"([^"]+)",\s*"([^"]+)"'
        callbacks = re.findall(pattern, core + "\n" + hid)
        if not callbacks:
            raise ValueError(f"SDL JNI callback declarations changed: {variable}")
        java = (sdl_root / "android-project/app/src/main/java/org/libsdl/app" / (name + ".java")).read_text(encoding="utf-8")
        declarations = set(re.findall(r'^\s*(?:(?:public|protected|private|static|final|synchronized|native)\s+)+[\w.$<>\[\]]+\s+(\w+)\s*\(', java, re.M))
        # Android 框架继承的回调不在 APK 中定义，也不受应用 R8 压缩影响。
        required.update(("Lorg/libsdl/app/" + name + ";", method, signature) for method, signature in callbacks if method in declarations)
    return required


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("sdl_root", type=Path)
    args = parser.parse_args()
    required = required_methods(args.sdl_root)
    found = set()
    with zipfile.ZipFile(args.apk) as archive:
        for name in archive.namelist():
            if re.fullmatch(r"classes\d*\.dex", name):
                found.update(dex_methods(archive.read(name)))
    missing = required - found
    for owner, name, signature in sorted(missing):
        print(f"Missing JNI callback: {owner} {name}{signature}")
    if missing:
        return 1
    print(f"APK JNI contract: {len(required)} SDL callbacks preserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
