# QmClient 进程级测试

这里放 QmClient 专属的真实进程冒烟和端到端测试。根目录 `scripts/` 是 DDNet 上游同步区，不在其中增加 QmClient 场景。

运行最小冒烟集：

```text
python qmclient_scripts/integration/qmclient_smoke.py <build-dir>
```

运行单个场景：

```text
python qmclient_scripts/integration/qmclient_smoke.py <build-dir> gores_configuration
```

运行端到端场景：

```text
python qmclient_scripts/integration/e2e_qmclient.py <build-dir>
python qmclient_scripts/integration/e2e_qmclient.py <build-dir> demo_recording
```

可用的 E2E 场景包括 `assert_dialog_no_false_hang`、`hang_watchdog_reports_stall`、`demo_recording`、`qm_lifecycle_persistence`、`invalid_statistics_preserved`、`perf_log_persistence`、`connection_failure_recovery`、`recording_without_connection` 和 `startup_saved_favorites`。每个场景使用独立临时目录，并验证真实进程产生的日志、退出状态和文件产物。gate 中使用 `--run-qm-smoke` 时会先运行 4 个 smoke，再运行 9 个 E2E。

`assert_dialog_no_false_hang` 依赖客户端测试专用参数 `--qm-test-main-thread-assert`：它在主循环之前触发一次真实断言，等价于启动阶段的错误弹窗，用于验证进程内弹窗阻塞主线程期间 hang 看门狗不会误报、退出兜底看门狗不会强杀正在阅读的弹窗。场景同时通过环境变量 `QMCLIENT_TEST_HIDE_DIALOG` 隐藏弹窗窗口并抑制报告进程拉起，避免桌面点击提前关闭弹窗、测试遗留后台进程干扰断言；弹窗的创建和消息循环行为不变。

`hang_watchdog_reports_stall` 使用 `--qm-test-main-thread-stall` 在主循环内阻塞 12 秒，验证 hang 看门狗使用单调时钟后能在主线程阻塞期间真实写出卡死报告（正向回归，防止时钟退回 tick 缓存）。

注意：`crash_dialog_smoke.py` 是视觉测试，必须让窗口真实出现在屏幕上抓帧（烟花动画、正文、按钮渲染），运行时会在桌面弹出多个可见窗口约 20 秒，请在方便时运行。弹窗逻辑的静默回归由 E2E 场景 `assert_dialog_no_false_hang` 覆盖（窗口以 `QMCLIENT_TEST_HIDE_DIALOG` 隐藏，不会打扰桌面）。

进程测试必须从外部可观察结果断言启动、连接、日志、退出和失败回退。客户端或服务端崩溃必须失败，不得通过放宽超时或忽略退出码掩盖。
