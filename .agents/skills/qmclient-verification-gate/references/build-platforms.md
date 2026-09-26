# 平台构建参考

仅首次配置、构建目录异常或跨平台验证时读取；测试范围和串行约束见父 skill。

Windows 默认通过封装注入 MSVC 环境，canonical build 目录使用 Ninja：

```text
qmclient_scripts/cmake-windows.cmd -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
```

Linux/macOS：

```sh
cmake -G Ninja -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
```

Windows 宿主验证 Linux 时可用 WSL Ubuntu 的 GCC/G++、CMake、Ninja 和 Rust 环境。进入 WSL 后在真实仓库路径运行命令，使用独立 `cmake-build-linux-release`，不要复用 Windows 的 CMakeCache：

```sh
cmake -G Ninja -S . -B cmake-build-linux-release -DCMAKE_BUILD_TYPE=Release -DDOWNLOAD_GTEST=ON
cmake --build cmake-build-linux-release --target game-client -j 14
```

需要测试或打包时将目标换为 `run_cxx_tests`、`run_rust_tests` 或 `package_default`，串行执行。严格构建/分析目录由相应 gate 管理，不为普通小修改额外配置。
