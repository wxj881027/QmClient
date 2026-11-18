#!/usr/bin/env python3
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


def read_file(filename):
	with open(filename, encoding="utf-8") as file:
		return file.read()


def check_file(filename):
	if filename in EXCEPTIONS:
		return False
	guard_name = ("_".join(filename.split(PATH)[1].split("/"))[:-2]).upper() + "_H"
	header_guard_line1 = f"#ifndef {guard_name}"
	header_guard_line2 = f"#define {guard_name}"
	footer1 = f"#endif // {guard_name}"
	footer2 = "#endif"

	lines = read_file(filename).splitlines()

	for i, line in enumerate(lines):
		if line == "// This file can be included several times.":
			return False
		if line.startswith("//") or line.startswith("/*") or line.startswith("*/") or line.startswith("\t") or line == "":
			continue
		if line.startswith("#ifndef"):
			if line != header_guard_line1:
				print(f"错误：{filename} 的头文件保护宏不正确，当前为 {line}，应为 {header_guard_line1}")
				return True
			next_line = lines[i + 1] if i + 1 < len(lines) else None
			if next_line != header_guard_line2:
				print(f"错误：{filename} 的头文件保护宏定义不正确，当前为 {next_line}，应为 {header_guard_line2}")
				return True
			break
		else:
			print(f"错误：{filename} 缺少头文件保护宏，应为 {header_guard_line1}")
			return True
	else:
		print(f"错误：{filename} 缺少头文件保护宏，文件为空？")
		return True

	if lines[-1] != footer1 and lines[-1] != footer2:
		print(f"错误：{filename} 的结尾保护宏不正确，当前为 {lines[-1]}，应为 {footer1}")
		return True

	return False


def check_dir(directory):
	errors = 0
	file_list = os.listdir(directory)
	for file in file_list:
		path = directory + file
		if os.path.isdir(path):
			if file not in ("external", "generated", "rust-bridge"):
				errors += check_dir(path + "/")
		elif file.endswith(".h") and file != "keynames.h":
			errors += check_file(path)
	return errors


if __name__ == "__main__":
	sys.exit(int(check_dir(PATH) != 0))
