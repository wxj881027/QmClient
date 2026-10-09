# QmClient 进程级测试

Unix 打开链接的失败回归通过已构建的 `testrunner` 调用生产 `open_link/open_file`：

```text
python qmclient_scripts/integration/open_link_failure_smoke.py <build-dir>/testrunner
```

四个隔离场景检查启动器缺失、不可执行、恢复后原样传参，以及 Unicode 目录的 open_file 转发。PATH 只包含工作区临时启动器，不打开真实浏览器或访问网络。通过子进程退出状态检查 exec 失败后的退出路径；日志保留在 `tmp/tests/open-link-*`。测试代码已补充，是否执行须以当前任务的实际验证记录为准。

这里放 QmClient 专属的真实进程冒烟和端到端测试。根目录 `scripts/` 是 DDNet 上游同步区，不在其中增加 QmClient 场景。

默认配置与推荐外观的回归种子位于 `fixtures/default_profiles/`，固定尺寸和玩家路径见[默认配置发布回归](../../docs/规格/2026-10-08-默认配置发布回归.md)。这些种子不是执行结果；仅复制到独立测试目录使用。

Windows 的普通版和便携版均忽略 `storage.cfg`。通用进程测试需要独立的 `DEV=ON、QMCLIENT_TEST_STORAGE=ON` 构建，runner 通过 `QMCLIENT_TEST_STORAGE_ROOT` 指向各场景临时目录。误用普通发布构建时初始化会失败；测试构建禁止打包，不用于发布。Linux/macOS 沿用原隔离方式。

便携版专用端到端测试使用 `QMCLIENT_PORTABLE=ON` 的真实发布客户端，复制到 `tmp/` 后验证 `profile/` 随目录搬家、忽略目录外配置及不可写时不回退：

```text
python qmclient_scripts/integration/e2e_storage_modes.py cmake-build-portable
```

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

可用的 E2E 场景包括 `assert_dialog_no_false_hang`、`hang_watchdog_reports_stall`、`slow_asset_loading_no_false_hang`、`asset_loading_stall_reports_hang`、`demo_recording`、`qm_lifecycle_persistence`、`invalid_statistics_preserved`、`perf_log_persistence`、`connection_failure_recovery`、`recording_without_connection` 和 `startup_saved_favorites`。每个场景使用独立临时目录，并验证真实进程产生的日志、退出状态和文件产物。gate 中使用 `--run-qm-smoke` 时会先运行 smoke，再运行注册的 E2E 场景。

`assert_dialog_no_false_hang` 依赖客户端测试专用参数 `--qm-test-main-thread-assert`：它在主循环之前触发一次真实断言，等价于启动阶段的错误弹窗，用于验证进程内弹窗阻塞主线程期间 hang 看门狗不会误报、退出兜底看门狗不会强杀正在阅读的弹窗。场景同时通过环境变量 `QMCLIENT_TEST_HIDE_DIALOG` 隐藏弹窗窗口并抑制报告进程拉起，避免桌面点击提前关闭弹窗、测试遗留后台进程干扰断言；弹窗的创建和消息循环行为不变。

`hang_watchdog_reports_stall` 使用 `--qm-test-main-thread-stall` 在主循环内阻塞 12 秒，验证 hang 看门狗使用单调时钟后能在主线程阻塞期间真实写出卡死报告（正向回归，防止时钟退回 tick 缓存）。Windows 卡死场景会继续等待报告追加转储结果，并检查 `.dmp` 文件的 `MDMP` 签名；不能在文本报告刚出现时就结束进程。

资源加载场景通过测试专用环境变量 `QMCLIENT_TEST_ASSET_LOAD_DELAY_MS` 在每项启动素材加载后注入延迟，取值限制为 0 到 20000 毫秒。`slow_asset_loading_no_false_hang` 每项延迟 1 秒，验证累计加载超过 10 秒仍能启动并响应命令，且没有 hang 产物；`asset_loading_stall_reports_hang` 单次延迟 12 秒，验证启动期间真正停滞仍被看门狗发现。两者都使用有界日志等待，并通过 `QMCLIENT_TEST_HIDE_DIALOG` 抑制报告窗口。

注意：`crash_dialog_smoke.py` 是视觉测试，必须让窗口真实出现在屏幕上抓帧（烟花动画、正文、按钮渲染），运行时会在桌面弹出多个可见窗口约 20 秒，请在方便时运行。弹窗逻辑的静默回归由 E2E 场景 `assert_dialog_no_false_hang` 覆盖（窗口以 `QMCLIENT_TEST_HIDE_DIALOG` 隐藏，不会打扰桌面）。

进程测试必须从外部可观察结果断言启动、连接、日志、退出和失败回退。客户端或服务端崩溃必须失败，不得通过放宽超时或忽略退出码掩盖。

实时名单同步冒烟使用上述独立测试构建和 Python `websockets` 包，在随机本机端口提供模拟 WebSocket 服务：

```text
python qmclient_scripts/integration/realtime_users_smoke.py <test-build-dir>
```

两个场景验证真实客户端声明压缩能力、应用名单、增量断档后请求完整快照、恢复后正常续传、重连清除旧版本，以及旧服务端文本名单兼容。结果通过连接消息与客户端应用日志断言；不依赖公网服务，不代表游戏内头衔视觉验证。日志保留在 `tmp/realtime_*`。

字体资源真实进程回归使用专用便携客户端，不依赖已安装系统字体：

```text
python qmclient_scripts/integration/font_resources_smoke.py --client cmake-build-portable/DDNet.exe
```

脚本在 tmp 内生成合法 TTF/TTC，并通过 qm_graphics_trace 下的 qm_font_diagnostics 查询真实字体族、样式、FreeType glyph 索引及无缩放 advance。七个场景覆盖商店下载目录与用户目录的 Unicode 名称、仅 data 字体与 Book 样式、损坏用户同路径字体不遮住 data 字体、TTC 多族多面及跨目录重复加载、随包 Poppins／Source Han Sans 的家族样式分组，以及真实家族与旧样式别名冲突后的选择恢复。重复查询和动态添加集合副本后断言字体池大小不变；这是进程冒烟回归，不代表下拉菜单截图或完整玩家端到端流程通过。

退出生命周期的 Windows 进程回归使用专用便携构建，同时构建 `game-client`、`game-server` 和 `testrunner`：

```powershell
python qmclient_scripts/integration/exit_lifecycle_smoke.py --build-dir tmp/session-acceptance-build
```

覆盖正常最终清理、最终清理卡死后的看门狗强制退出并保留已保存配置，以及本地服务器在父进程正常退出、`_Exit`、外部强杀和 `CreateProcess` 尚未返回的创建窗口中被杀后的终止。Windows 本地服务器使用原子 Job-list 创建能力，需要 Windows 10 / Server 2016 或更新版本；能力不可用时拒绝启动。测试只运行工作区副本，服务器使用隔离的 `storage.cfg`，不会读取真实用户配置。
