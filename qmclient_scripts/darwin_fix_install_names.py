import argparse
import subprocess
import os
import re
import glob


def get_install_name(otool, dylib_path):
    output = subprocess.check_output([otool, "-D", dylib_path]).decode().splitlines()
    if len(output) >= 2:
        return output[1].strip()
    return None


def get_dependencies(otool, dylib_path):
    output = subprocess.check_output([otool, "-L", dylib_path]).decode().splitlines()
    deps = []
    for line in output[1:]:
        line = line.strip()
        match = re.match(r"^\s*(.*?)\s*\(", line)
        if match:
            deps.append(match.group(1).strip())
    return deps


def fix_dylib_install_names(otool, install_name_tool, frameworks_dir, executable):
    dylibs = glob.glob(os.path.join(frameworks_dir, "*.dylib"))

    dylib_basenames = {}
    for dylib in dylibs:
        basename = os.path.basename(dylib)
        dylib_basenames[basename] = dylib

    for dylib in dylibs:
        basename = os.path.basename(dylib)
        current_id = get_install_name(otool, dylib)
        desired_id = f"@rpath/{basename}"

        if current_id and current_id != desired_id:
            print(f"Fixing install_name: {current_id} -> {desired_id} ({basename})")
            subprocess.check_call([install_name_tool, "-id", desired_id, dylib])

    for dylib in dylibs:
        basename = os.path.basename(dylib)
        deps = get_dependencies(otool, dylib)

        for dep in deps:
            dep_basename = os.path.basename(dep)
            if dep_basename in dylib_basenames:
                desired_dep = f"@rpath/{dep_basename}"
                if dep != desired_dep:
                    print(f"Fixing dependency in {basename}: {dep} -> {desired_dep}")
                    subprocess.check_call([install_name_tool, "-change", dep, desired_dep, dylib])

    if executable and os.path.exists(executable):
        deps = get_dependencies(otool, executable)
        for dep in deps:
            dep_basename = os.path.basename(dep)
            if dep_basename in dylib_basenames:
                desired_dep = f"@rpath/{dep_basename}"
                if dep != desired_dep:
                    print(f"Fixing dependency in executable: {dep} -> {desired_dep}")
                    subprocess.check_call([install_name_tool, "-change", dep, desired_dep, executable])


def main():
    p = argparse.ArgumentParser(description="Fix install_name for dylibs in macOS App Bundle")
    p.add_argument('otool', help="Path to otool")
    p.add_argument('install_name_tool', help="Path to install_name_tool")
    p.add_argument('frameworks_dir', help="Path to Contents/Frameworks directory")
    p.add_argument('--executable', help="Path to main executable (optional)", default=None)
    args = p.parse_args()

    fix_dylib_install_names(args.otool, args.install_name_tool, args.frameworks_dir, args.executable)


if __name__ == '__main__':
    main()
