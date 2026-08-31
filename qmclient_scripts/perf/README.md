> 请抬头享受阳光｜日子很好 我很我---------致咩子
# QmClient Perf — 性能日志分析工具

## 快速开始

```bash
cd qmclient_scripts/perf
bun install
bun analyze.ts          # 自动读取最新日志
bun analyze.ts path/to/qm_perf_xxx.log  # 指定日志文件
```

## 前置条件

游戏运行时需开启：

```
qm_perf_debug 1
qm_perf_logfile 1
```

也可以直接在 设置 → QmClient → HUD → 调试模式 卡片中一键开启（总开关等效于同时设置 `qm_perf_debug`、`qm_perf_logfile`、`qm_perf_stutter_diagnostics` 三个开关，并可单独微调与设置采样阈值 `qm_perf_debug_threshold_ms`）。

开关在游戏内即时生效：打开即立刻创建日志文件并开始落盘，关闭即立刻停止写入并关闭文件；同一次运行中重复开关会生成独立的时间戳文件。

日志输出到 `%APPDATA%/DDNet/dumps/QmClient_Perf/qm_perf_*.log`。

## 客户端卡顿诊断

开启 `qm_perf_stutter_diagnostics 1`（或调试模式总开关）：

```
qm_perf_stutter_diagnostics 1
```

诊断以 300 FPS（每帧 `1000 / 300 = 3.333...ms`）为目标。连续低于目标的帧会合并为同一区间；恢复稳定一秒后结束，持续低帧则每 10 秒写出一个分段。日志中的 `perf/stutter` 包含窗口摘要、所有顶层客户端组件的 `OnUpdate` / `OnRender` CPU 耗时汇总，以及最慢帧对应的已启用 `qm_` / `tc_` / `cl_` 整数功能开关。

组件墙钟时间反映 CPU 回调成本，不等于逐模块 GPU 时间。`graphics_swap` 偏高只能指向 GPU、驱动或 VSync 压力；启用限帧、VSync、后台限帧或菜单 idle throttle 时，报告会优先标记帧率限制，不把组件排名描述为确定原因。诊断数据仅写入本地性能日志，不包含密码、聊天内容或玩家信息。

功能开关、当前页面和组件回调耗时都取自同一最慢帧窗口。通用 `CComponent` 没有统一的“是否真的绘制了 HUD”查询接口，因此无法可靠判断的逐功能 HUD/设置可见性会写为 `unknown`，不会根据回调耗时伪造可见状态。

## 输出

生成与日志同名的 `_report.html` 文件，浏览器打开即可查看交互式报表。

## 生产级使用约定

- 默认采集阈值使用 `qm_perf_debug_threshold_ms 4`；4.17ms 只表示 240Hz 帧预算线。
- HTML 报表用于人工分析，`*_summary.json` 用于按日志归档，固定名 `perf_summary.json` 用于 debug bundle 自动拾取。
- 自动选择的上一份日志只作为趋势提示；只有 page/system operation signature 一致时，才可以作为严格对比依据。
- 空日志、缺字段、畸形行和采样偏差必须看作质量警告，不能解读为性能优秀。

## 固定场景

性能优化 PR 的前后对比应使用相同的页面、操作步骤和系统环境，避免把不同操作路径的日志当成严格回归数据。

## 报表内容

- KPI 卡片: p50 / p95 / p99 / Max / 240Hz / 120Hz / 60Hz 合规率
- Session 自动对比（自动使用当前日志的上一份日志）
- 采样偏差提示（使用 p5 估计采样阈值）
- 帧时间趋势图、直方图 + KDE、QQ 图、百分位图
- 页面级耗时分解与尖峰帧详情
- 页面性能归因: page switch / list frame / UI rebuild / work drain
- Section Top-10（基于当前 `perf/section` 样本，仍受 C++ 覆盖面限制）
- 交互窗口、Tee 收敛、设备资源表格

## 开发

```bash
bun run analyze    # 等同于 bun analyze.ts
bun run test       # 使用 test/sample.log 和内联边界样本验证解析/报表
```
