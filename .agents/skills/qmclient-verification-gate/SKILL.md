---
name: qmclient-verification-gate
description: QmClient 代码交付或选择验证命令时使用；按变更风险选择测试和 gate，复用已有证据，约束共享构建目录串行执行。
---

# QmClient 验证

选能覆盖本轮风险的最小检查集合。测试通过后继续交付；只有新改动、失败或未解决问题才扩大或重复验证。

## 检查范围

| 改动 | 验证 |
| --- | --- |
| 纯文档、skills、规则 | 核对内容、引用、状态和命令；不跑代码 gate |
| 局部低风险代码 | 相关行为测试或已有过滤测试，必要的目标构建，以及 quick gate |
| 共享接口、生命周期、线程、格式或跨模块行为 | 相关回归与对应语言全量入口；涉及消费者一并验证 |
| 翻译或生成链 | 按 `qmclient-i18n-workflow` 选择生成与验证；脚本逻辑变化补相关测试 |
| 性能日志/报表代码 | `qmclient_scripts/perf` 下 `bun test.ts`；TS 改动补 `npx tsc --noEmit`，跨语言字段补合同验证 |
| 代码提交前 | 优先 default gate；用户限制范围或环境不足时记录实际未覆盖项 |
| 准发布或明确综合验证 | full gate；CI 等价构建任务才选 build 模式 |

可复现 bug 优先写能揭示回归的失败测试；不用源码字符串或实现镜像代替行为测试。无适用单测的视觉小改用目标构建与实际场景验证。

## Gate 与证据复用

在仓库根运行，Windows 用 `py -3` 或 `python`，Linux/macOS 用 `python3`：

```text
python qmclient_scripts/gate/check_gate.py --mode quick
python qmclient_scripts/gate/check_gate.py --mode default
python qmclient_scripts/gate/check_gate.py --mode full
```

- quick 做源码卫生检查，不构建、不跑真实测试。
- default 包含 quick 层检查及 C++、Rust 全量测试；full 在其上增加重检查。
- 选一次能覆盖需求的入口；计划跑 default 时不先单独重跑 quick 和两套全量测试。
- 同一代码状态、环境和范围下已有成功证据可复用。检查之后有相关改动才重跑受影响部分；不靠重复运行证明认真。
- 过滤测试可作为低风险局部修改的验收证据，准确称“相关测试通过”；不能称“全量通过”或笼统保证无回归。
- 必要检查失败时查明原因；不要削弱测试、刷 allowlist 或隐藏失败来放行；确认测试本身错误或需求已变化时，可以修正并说明依据。

## 构建与测试入口

Windows 使用封装入口，避免依赖会话已加载 MSVC 环境：

```text
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target game-client -j 14
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_cxx_tests -j 14
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_rust_tests -j 14
```

Linux/macOS 使用 `cmake --build cmake-build-release --target <目标> -j 14`。首次配置与 WSL 目录选择见 [build-platforms.md](references/build-platforms.md)。已有 build 目录只在需要时重新配置。

同一 build 目录的 `game-client`、`testrunner`、`run_cxx_tests`、`run_rust_tests`、`package_default` 及会调用它们的 gate 必须串行；并行需独立 build 目录。

过滤测试先确保 `testrunner` 已针对当前源码构建，再从 build 目录运行：PowerShell 用 `./testrunner.exe --gtest_filter=<suite.test>`，Linux/macOS 用 `./testrunner --gtest_filter=<suite.test>`。测试临时产物使用 build 下 `tmp/tests/`；源码合同测试不能依赖当前工作目录查找源码。

## 视觉与证据

UI/HUD/动画改变后构建并运行当前工作区的开发实例，检查目标场景；布局涉及缩放时加一个非默认比例，交互仅查相关输入和状态。视觉交付按需保留截图；无法启动或缺少设备时说明实际缺口。

记录执行命令、关键结果、覆盖范围和剩余问题即可；小任务写最终回复，长任务写已有计划或报告。未执行的运行时、视觉、性能或跨平台检查不能用构建成功替代，也不要把本来不适用的检查列成缺口。
