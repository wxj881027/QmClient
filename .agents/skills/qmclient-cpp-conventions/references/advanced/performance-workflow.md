# 性能实现与诊断参考

用于性能优化、性能敏感代码设计和卡顿调查。性能验证命令、Google Benchmark 基线与证据统一见 [验证 skill 的性能参考](../../../qmclient-verification-gate/references/performance.md)，本文件只维护实现取舍与诊断边界。

## 实现取舍

先识别本次成本发生在哪个生产路径，再选择技术路线；可独立测量的代码优先复用现有 Google Benchmark，普通正确性修复缺少实测时不宣称性能提升。

1. 跳过不可见 section、不可见列表行和无关后台结果。
2. 文本、布局、排序、筛选和 section plan 只在相关 dirty 变化时重建。
3. merge、upload、publish 和 decode 结果消费按实际帧预算推进。
4. 文件 I/O、图片 decode、列表 plan build 等不依赖 GPU context 的工作按现有 jobs 与生命周期规则异步执行。
5. 引入缓存时验证命中率、实际收益、失效机制和内存/显存成本。

同类成本由共享生产模块负责；为了可测性提取真实职责边界，不复制算法、不为 benchmark 增加无关抽象。FBO 仅在本次任务确实涉及时评估，不预设收益。

## 长帧归因

按真实操作调查切页同步重建、列表不可见项工作、UI dirty/文本重建、work drain 集中发布、资源 decode/upload 和设备瓶颈。复用日志或 profiler，只有存在明确观察缺口时再补 telemetry。

可选场景包括 Settings/Tee 首次切页、含大量文件的 Demo Browser 首次进入、Server Browser 滚动、Assets/Workshop 发布和 Tee 快速滚动；只选择本次改变的路径。不要将示例 16/33ms 当通用阈值，也不要将 CPU callback 耗时当 GPU 时间。

## 诊断链路边界

客户端采集目前只需 `qm_perf_debug` 总开关，实际开关、日志路径、采样和 HTML/JSON 使用见 [perf/README.md](../../../../../qmclient_scripts/perf/README.md)。旧的 `qm_perf_logfile`、`qm_perf_stutter_diagnostics`、`qm_perf_debug_threshold_ms` 已移除，不再作为操作步骤。

修改采集、解析或报表时保留以下约束：

- 字段名、单位、事件和缺省语义以实际发出端与解析器为准，保持现有历史日志兼容；不为了参考模板统一改名或换算。
- `page_switch` 边界事件不混入耗时归因总量；缺少事件或字段不意味着没有成本，空日志、畸形行和缺失数据显示 unavailable/N/A。
- KPI、verdict、文字和图表共享统计语义；明确样本上限、采样偏差和环境差异，发生抽样时不能输出严格回归结论。
- 热路径控制统计、格式化和 payload 构造成本；完整帧样本、详情限流、关闭/退出 flush 和会话边界按实际机制验证。
- 采集配置在日志落盘前脱敏，分段字符串的缺失不能当完整值；离线报告保持自包含。
- C++/TypeScript 协作通过真实日志与解析结果约束，不把源码片段存在当成日志兼容或统计正确性的证明。
