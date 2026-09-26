# 性能优化工作流

本文件用于性能优化、卡顿调查、长帧归因和页面降温任务。目标是用生产级性能量化系统驱动改动，而不是凭体感猜测。

## 入口原则

- 宣称性能改善前采集可比较基线；静态可确认的正确性修复可先推进，缺少实测时不宣称性能提升。
- 先归因，再决定技术路线。
- 先减少交互帧工作，再考虑缓存。
- FBO 不作为页面性能优化默认路线；仅当本次任务确实涉及它时评估实际收益与成本，不预设结论。
- 任何性能结论都必须能对应到日志、报表、固定场景或 A/B 对照。

## 必备基线

页面性能实测可使用以下配置；其他性能问题选择对应的现有观测方式：

```text
qm_perf_debug 1
qm_perf_logfile 1
qm_perf_debug_threshold_ms 4
```

报告必须说明：

- 操作路径：例如打开 Settings、切到 Tee、滚动一屏、刷新 server browser。
- 环境：平台、renderer、窗口模式、刷新率、UI scale。
- 样本可信度：是否存在采样偏差、是否同一操作路径、是否有官方 DDNet baseline。
- 指标：p50、p95、p99、max、spike count、归因类别。

## 固定场景

不同操作路径的日志只能用于趋势参考，不能作为严格回归结论。下表 16/33ms 为调查示例，验收阈值须结合刷新率、设备与同场景基线；字段缺失先区分未采集和实际无工作。性能改动优先复用以下场景，并保持采集环境和操作签名一致。

| ID | 操作路径 | 主要归因 | 调查信号（需结合基线） |
| --- | --- | --- | --- |
| `PERF-SETTINGS-TEE-SWITCH` | 打开 Settings，切到 Tee 页，等待首屏稳定 | page switch、section、work drain | p99 > 16ms 或连续长帧 |
| `PERF-DEMO-FIRST-ENTER` | 首次进入 Demo Browser，打开含大量 demo 的目录 | page switch、list frame | 单帧 > 33ms 或 list frame 无 `rows_skipped` |
| `PERF-SERVER-SCROLL` | 进入 Server Browser，滚动一屏 | list frame | `rows_iterated` 明显大于 `rows_rendered`，或 `rows_rendered` 明显大于 `rows_visible` |
| `PERF-ASSETS-DRAIN` | 进入 Assets / Workshop，等待缩略图和资源完成 | work drain、device | 交互帧出现无 stop reason 的集中 publish |
| `PERF-TEE-SCROLL` | Tee 页快速滚动一屏 | list frame、work drain | 已显示 preview 回退到 loading，或 drain 无预算原因 |

使用报表工具时保留实际输出及比较所需摘要；单项调查可直接保留日志和指标。记录客户端版本、renderer、窗口模式、刷新率和 UI scale。与官方 DDNet 或上一轮 QmClient 对比时，必须说明 operation signature 是否一致。

## 归因分类

长帧优先归到以下类别之一：

| 类别 | 典型字段 | 处理方向 |
| --- | --- | --- |
| Page switch | `event=page_switch` | 拆分切页同步工作，避免切页帧集中重建 |
| List frame | `event=list_frame` | 只处理可见行，缓存排序/筛选 plan |
| UI rebuild | `event=section`、`dirty`、`text_new` | 收紧 dirty，复用文本和布局 |
| Work drain | `event=work_drain`、`stop` | 分帧 drain，记录 stop reason |
| Resource | decode/upload/publish | jobs 化或预算化，不阻塞交互帧 |
| Device bound | `perf/device` | 判断 GPU/CPU/IO 是否真的打满 |

不能归因的长帧先调查现有日志或 profiler；只有确有观测缺口时再补 telemetry，不为满足流程先扩建系统。

## 优化顺序

1. **不做**：跳过不可见 section、不可见列表行和无关后台结果。
2. **少做**：文本、布局、排序、筛选和 section plan 只在 dirty 时重建。
3. **分帧做**：merge、upload、publish、decode 结果消费按预算推进。
4. **异步做**：文件 I/O、图片 decode、列表 plan build 等不依赖 GPU context 的工作进入 jobs。
5. **证明后缓存**：缓存必须证明命中率、收益和内存/显存成本。

## 验收

性能优化完成时必须给出：

- 优化前后同一操作路径的 perf report 或等价日志摘要。
- 改动是否降低 p95/p99、spike count 或明确改善归因类别。
- 如果指标没有改善，说明保留改动的非性能理由；否则回退或改为后续调查。
- 对应验证由 `qmclient-verification-gate` 选择，已有证据可复用。
