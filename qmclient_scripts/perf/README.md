# 客户端性能日志离线诊断

本目录保留真实客户端帧耗时、卡顿归因、配置变化和采样偏差的 HTML/JSON 分析能力。代码性能质量优先使用 Google Benchmark，工作流见 [性能验证参考](../../.agents/skills/qmclient-verification-gate/references/performance.md)；常规代码验收不要求生成 HTML，CPU 微基准也不能代替这些运行场景数据。

在控制台输入 `qm_perf_debug 1`，或在「设置 → QmClient → HUD → 调试模式」开启唯一总开关。关闭使用 `qm_perf_debug 0`，游戏内立即生效；每次开启创建新的日志和会话 ID。

## 自动采集

- 完整渲染间隔按最多 64 帧或累计 1 秒批量写入 `perf/frame`，包括快帧和慢帧。首帧用作时间基点。
- 主线程、渲染阶段明细使用 300 FPS 对应的帧预算自动筛选；普通明细每秒最多 1000 条，超出数量写入日志。完整帧统计、FPS 窗口和卡顿汇总不受此限制。
- 卡顿目标保持 300 FPS。连续低帧合并为同一区间，恢复稳定一秒后结束；持续低帧每 10 秒写出分段，包含所有顶层组件的 update/render CPU 耗时和最慢帧的功能开关。
- 图标诊断按窗口汇总；关闭采集、退出或重启时，先写完剩余帧、卡顿窗口、配置和会话结束标记，再关闭文件。
- `qm_perf_logfile`、`qm_perf_stutter_diagnostics`、`qm_perf_debug_threshold_ms` 已移除，使用 `qm_perf_debug` 即可。

## 当前配置

每个会话开始时枚举运行时全部客户端配置变量，包含 DDNet、QmClient、TClient 的整数、关闭项、默认值、颜色和字符串，不包含按键绑定。分类依据变量声明所在配置头，不只看命名前缀。

采集中每秒检查一次变化，关闭时再检查一次。短于检查间隔且已恢复的瞬时变化可能不被记录。密码、Token、翻译服务 API Key、SecretId/SecretKey、Spotify Cookie 等在写日志前脱敏。长字符串按 UTF-8 边界分段，分析时重组；缺失分段会被标记，不能当作完整值使用。

HTML 中可按配置名或值搜索，分 DDNet / QmClient / TClient 查看当前值、开始采集时的值和修改状态。JSON 摘要包含同样的 `configuration` 字段。原日志保留采集到的变化，分析产物保留初始值和最新完整值。

## 分析

~~~bash
cd qmclient_scripts/perf
bun install
bun analyze.ts
bun analyze.ts path/to/qm_perf_xxx.log
bun analyze.ts path/to/qm_perf_xxx.log --output path/to/report.html --no-compare
~~~

默认查找系统 DDNet 数据目录下 `dumps/QmClient_Perf/` 的最新日志；自定义存储目录或便携版请直接传入日志路径。Windows 默认为 `%APPDATA%/DDNet/dumps/QmClient_Perf/`。

默认产物写入该目录的 `Perf_Report/`，包括 `*_report.html`、同名 `*_summary.json` 和供 debug bundle 拾取的 `perf_summary.json`。指定 `--output` 时仅生成指定 HTML 和同目录同名摘要。

分析器流式读取，最多保留 100000 个帧样本、50000 条普通明细和最近 20000 条窗口/会话事件，另保留各配置初始值和最新完整值。帧和普通明细超过上限后使用均匀蓄水池抽样；报告标记 `analysis_sampled`，统计作为估计值，不输出抽样后的严格通过结论。原始日志不被改写。

小日志默认与上一份日志比较；任一日志分析时发生抽样则跳过会话对比。`--no-compare` 可避免读取历史日志。不同页面、操作或系统环境的结果仅能作趋势提示。

支持历史 key=value 和 JSON 日志。历史日志未包含统一帧样本时仍使用原统计口径，并保留采样偏差提示；新会话使用完整渲染间隔计算帧指标，组件/菜单阶段仅用于归因。CPU 回调耗时不等于逐模块 GPU 时间；限帧、VSync、后台限帧和菜单节流会单独标记。

## 维护

~~~bash
bun run analyze
bun run test
~~~

修改诊断链路时按实际影响维护 C++ 行为测试、`test.ts` 及 HTML/JSON 输出；跨语言字段通过真实日志与解析结果验证，不用源码字符串证明运行时兼容。TypeScript 逻辑变化补 `npx --no-install tsc --noEmit`，需已安装本地依赖；仅文档变化不运行分析器测试。
