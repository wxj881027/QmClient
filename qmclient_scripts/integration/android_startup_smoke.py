#!/usr/bin/env python3
"""在隔离 Android 模拟器中安装真实 APK，检查首屏、重启和前后台恢复。"""

import argparse
import io
import json
import re
import subprocess
import time
from pathlib import Path

from PIL import Image, ImageStat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if not args.serial.startswith("emulator-"):
        parser.error("This runner only operates its explicitly selected test emulator")
    args.output.mkdir(parents=True, exist_ok=True)
    package = "com.qmclient.client"
    component = package + "/.ClientActivity"
    results = []

    def adb(*command, binary=False, check=True):
        result = subprocess.run([args.adb, "-s", args.serial, *command], capture_output=True, timeout=60)
        if check and result.returncode:
            raise RuntimeError(result.stderr.decode(errors="replace") or result.stdout.decode(errors="replace"))
        return result.stdout if binary else result.stdout.decode(errors="replace")

    def alive():
        return bool(adb("shell", "pidof", package + ":client_process", check=False).strip())

    def save_log(name):
        log = adb("logcat", "-d", "-v", "threadtime")
        (args.output / (name + ".log")).write_text(log, encoding="utf-8")
        return log

    def wait_frame(name):
        deadline = time.monotonic() + args.timeout
        seen = False
        while time.monotonic() < deadline:
            running = alive()
            seen |= running
            log = save_log(name)
            if re.search(r"FATAL EXCEPTION|JNI DETECTED ERROR|Fatal signal|UnsatisfiedLinkError|NoSuchMethodError", log):
                raise RuntimeError("Android crash/JNI error; see " + name + ".log")
            if seen and not running:
                raise RuntimeError("Client process exited before its first usable frame")
            if running:
                data = adb("exec-out", "screencap", "-p", binary=True)
                image = Image.open(io.BytesIO(data)).convert("RGB")
                width, height = image.size
                center = image.crop((width // 5, height // 5, width * 4 // 5, height * 4 // 5))
                colors = center.getcolors(100000)
                contrast = max(ImageStat.Stat(center).stddev)
                windows = adb("shell", "dumpsys", "SurfaceFlinger", "--list")
                if package in windows and (colors is None or len(colors) > 64) and contrast > 8:
                    (args.output / (name + ".png")).write_bytes(data)
                    return {"scenario": name, "size": [width, height], "contrast": round(contrast, 2)}
            time.sleep(1)
        raise RuntimeError("No rendered client frame within the startup deadline")

    try:
        adb("install", "-r", str(args.apk.resolve()))
        for backend in ("default", "GLES"):
            adb("shell", "am", "force-stop", package)
            adb("logcat", "-c")
            launch = ["shell", "am", "start", "-W", "-n", component]
            if backend != "default":
                launch += ["--es", "gfx-backend", backend]
            adb(*launch)
            results.append(wait_frame(backend + "-cold-start"))
            adb("shell", "input", "keyevent", "KEYCODE_HOME")
            adb(*launch)
            results.append(wait_frame(backend + "-resume"))
            if not alive():
                raise RuntimeError("Client stopped after returning from the background")
        print(json.dumps(results, ensure_ascii=False, indent=2))
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        (args.output / "failure.txt").write_text(str(error), encoding="utf-8")
        save_log("failure")
        print(str(error))
        return 1
    finally:
        (args.output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        adb("shell", "am", "force-stop", package, check=False)


if __name__ == "__main__":
    raise SystemExit(main())
