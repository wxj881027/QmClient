# C++ 覆盖率

使用 LLVM source-based coverage 采集 C++ 测试的 line、function、region 和 branch 数据。它要求 `clang`、`clang++`、`llvm-profdata`、`llvm-cov` 和 Ninja；建议在 Linux/CI 的独立构建目录运行。

```text
python qmclient_scripts/coverage/collect_llvm_coverage.py \
  --build-dir cmake-build-coverage \
  --output-dir tmp/coverage
```

`--build-dir` 必须指向独立的 coverage 构建目录；不要传入 `cmake-build-release`、`cmake-build-release-msvc` 或其他普通构建目录。脚本会切换编译器和覆盖率 flags，复用普通目录会污染其 CMake cache。

输出：

- `coverage.txt`：人类可读汇总，包含分支计数；
- `coverage.json`：LLVM 原始 JSON 报告，保留 line/function/region/branch 明细；
- `testrunner.profraw` / `testrunner.profdata`：可复用的采样数据。

Windows MSVC 的普通 `cmake-build-release` 不产生 LLVM coverage，不能用普通构建结果冒充覆盖率报告。Windows Python 传入无扩展名 runner 时会优先解析同路径的 `.exe`；若只找到 Linux/WSL ELF，会明确拒绝执行。

Windows 上的 Scoop LLVM 若在 CMake 配置阶段报缺少 `libatomic`，应改用带 `libatomic` 的 Linux/WSL/CI Clang 环境；脚本不会把该失败误报成覆盖率成功。

覆盖率构建和 runner 必须来自同一目标环境。若从 Windows Python 调用非 Windows 构建目录，使用 `--testrunner` 仅在 runner 可被当前系统执行时才有意义，不能把 Linux ELF 当作 Windows 可执行文件运行。
