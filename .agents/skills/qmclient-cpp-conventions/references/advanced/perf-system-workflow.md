# 性能量化系统工作流

`qmclient_scripts/perf` 和 C++ `perf/*` 日志是生产级性能诊断系统，不再按 MVP 处理。系统迭代必须优先保证可信度、可用性和长期可维护性。

## 质量目标

- 报表信号一致：KPI、verdict、narrative、图表阈值必须共享同一语义。
- 日志合同稳定：字段名、单位、事件语义变更必须有测试保护。
- 边界数据可解释：空日志、采样偏差、缺失字段、畸形行不能生成误导性结论。
- 观察开销受控：热路径日志不得在低于阈值时构造大 payload 或做昂贵格式化。
- 离线可用：报表应自包含，不能要求用户额外安装复杂工具才能看结论。

## 日志合同

新增或修改 C++ telemetry 时：

- 使用 `qm_` / `Qm` 配置前缀。
- 保持 key=value 兼容；JSON Lines 可作为增强，不阻塞第一步。
- 每个事件必须有 `event`、`page` 或明确的系统标签。
- 耗时字段沿用对应事件的 `dur_ms` / `duration_ms` 毫秒合同；时间戳和其他单位字段不因本参考改名或换算。
- `page_switch` 是边界事件，不应混入耗时归因总量。
- `list_frame` 沿用当前事件字段：如 `items_total` / `rows_total`、`rows_visible`、`rows_processed` / `rows_iterated` / `rows_rendered`、`rows_skipped`；以发出端和解析器为准，控制采样开销，不统一改名。
- `section` 应记录 `dirty`、`text_new`、`text_reused`，缺失时用明确的 `unknown`。
- `work_drain` 必须记录 `kind`、`count`、`bytes`、`dur_ms`、`stop`。

## 报表标准

报表必须做到：

- 明确采样阈值和采样偏差。
- 将长帧归因到页面切换、列表处理、UI rebuild、work drain 或设备瓶颈。
- 显示同一操作路径对比，不把自动选择的上一份日志当严格回归判定。
- 不把 FBO hit rate 作为页面性能优化入口。
- 对缺失数据显示 unavailable 或 N/A，不伪造成优秀指标。

## 测试要求

验证命令和范围统一见 `qmclient-verification-gate`。新增或改变跨语言字段时，补充 C++/TS 合同验证，避免解析器与日志格式漂移；单侧改动只验证受影响的行为，纯文档不运行代码测试。
