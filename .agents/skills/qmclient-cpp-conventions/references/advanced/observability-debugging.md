# 观测与调试工作流

本文件用于 debug 面板、debug bundle、日志分类、用户反馈格式和问题复现。

## 目标

把“卡、飘、不跟手、偶尔坏了”转成可调查证据。观测系统要帮助判断问题属于渲染、网络、输入、prediction、资源、设备还是配置。

## Debug 信息层级

| 层级 | 内容 | 用途 |
| --- | --- | --- |
| HUD/debug panel | 当前 FPS、p95/p99、ping、jitter、correction、renderer | 现场判断 |
| perf report | 时间序列、分布、尖峰、归因、采样偏差 | 性能决策 |
| debug bundle | 脱敏配置、日志、summary、环境 | 用户反馈 |
| fixed scenarios | maps/demos/configs/A-B 表 | 复现和回归 |

## Debug Bundle

建议包含：

```text
client_version.txt
system_info.txt
config_sanitized.cfg
last_log.txt
perf_summary.json
net_summary.json
prediction_summary.json
repro_steps.txt
```

必须脱敏，不能包含 token、密码、私密聊天和完整个人历史。

## 日志分类

warning/error 应能归类：

- rendering
- networking
- input
- prediction
- resource
- filesystem
- config
- telemetry

分类与严重度分别判断：无法归类时补充上下文或沿用模块类别；不能仅因缺少分类而降低真实 warning/error 的级别。

## 验收

新增观测项时必须说明：

- 它回答哪个问题。
- 是否有采样或隐私风险。
- 普通运行是否受影响。
- 如何在 perf report、debug panel 或 bundle 中消费。
