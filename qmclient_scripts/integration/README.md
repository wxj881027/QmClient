# QmClient 进程级测试

Windows 独立截图查看器冒烟使用已有构建，不自动编译：

```text
python qmclient_scripts/integration/screenshot_viewer_smoke.py <build-dir>
```

检查原图打开、缩放与适应窗口、两个窗口独立关闭、ESC 退出和坏图错误提示。测试图片与日志保留在 `<build-dir>/tmp/tests/screenshot-viewer-*`；不读取玩家配置。测试代码已补充，未执行。

Unix 打开链接的失败回归通过已构建的 `testrunner` 调用生产 `open_link/open_file`：

```text
python qmclient_scripts/integration/open_link_failure_smoke.py <build-dir>/testrunner
```

四个隔离场景检查启动器缺失、不可执行、恢复后原样传参，以及 Unicode 目录的 open_file 转发。PATH 只包含工作区临时启动器，不打开真实浏览器或访问网络。通过子进程退出状态检查 exec 失败后的退出路径；日志保留在 `tmp/tests/open-link-*`。测试代码已补充，是否执行须以当前任务的实际验证记录为准。

这里放 QmClient 专属的真实进程冒烟和端到端测试。根目录 `scripts/` 是 DDNet 上游同步区，不在其中增加 QmClient 场景。

默认配置与推荐外观的回归种子位于 `fixtures/default_profiles/`，固定尺寸和玩家路径见[默认配置发布回归](../../docs/规格/2026-10-08-默认配置发布回归.md)。这些种子不是执行结果；仅复制到独立测试目录使用。

Windows 的普通版和便携版优先读取程序旁的 `storage.cfg`，未找到时再查工作目录；没有配置路径时分别使用用户目录和程序旁的 `profile/`。专用隔离测试构建继续忽略该文件。通用进程环境、启动呈现、皮肤加载和实时名单冒烟共用 `process_build.py`：传入普通构建目录时，启动前选择同一源码、配置和生成器的已有隔离缓存；没有匹配缓存时使用 `<build-dir>-process-tests`。独立缓存启用 `DEV=ON、QMCLIENT_TEST_STORAGE=ON、QMCLIENT_PORTABLE=OFF`，沿用功能与优化开关，并通过 Windows 封装入口增量构建当前源码。原始普通／便携缓存的存储模式保持不变，构建失败或来源不匹配时不启动旧程序。

runner 通过 `QMCLIENT_TEST_STORAGE_ROOT` 指向场景临时目录。测试构建禁止打包，不用于发布；客户端仍拒绝用普通发布二进制运行隔离测试。已有专用测试目录也会先增量构建。Linux/macOS 沿用原隔离方式。

服务器列表恢复使用真实客户端与随机 loopback 端口的 HTTP 服务，不依赖公网：

```text
python qmclient_scripts/integration/e2e_serverlist.py cmake-build-release
```

通过隔离构建中的测试命令查询 `CServerBrowser` 实际消费的列表数量和名称，并从外部驱动刷新、响应失败与恢复、退出及重启。HTTP 层下载成功日志不能单独作为列表恢复的断言。默认三层场景保留官方 master、镜像、GitHub 的生产来源身份和调度，仅在测试构建中把 HTTP 传输映射到本机服务；自定义来源场景另用隔离目录中的配置文件。此入口纳入 gate 的 `--run-qm-smoke`，独立执行时也会先增量构建；运行结果以实际日志为准。本机 HTTP 场景不证明用户线路上的 DNS、TLS 或官方多层来源可达性。

实际公网 HTTPS 与客户端原生 DoH 的手动探测入口：

```text
python qmclient_scripts/integration/public_serverlist_probe.py cmake-build-release --live
```

可用 `--source official mirror github doh` 选择来源。每项启动独立存储的真实客户端，使用公开 HTTPS 地址、正常证书校验和直连环境；DoH 项只对精确官方 URL 启用生产加密解析接口。结果记录消费者数量、名称、revision、过期状态、curl 日志与正常退出码，保存到 `tmp/public-serverlist-*`。GitHub 可达但快照过期会单独记录 `stale`，不等同于新鲜列表恢复。入口必须显式传入 `--live`，不纳入常规离线 gate；返回 0 表示全部获得有效列表，2 表示存在公网无列表或观察超时，1 表示客户端或测试编排错误。成功只证明本次所在网络，不能替代故障用户线路验收。

配置读写保护的 Windows 端到端入口：

```text
python qmclient_scripts/integration/e2e_config_persistence.py <test-build-dir>
```

三个场景验证双客户端按正序和反序退出均能保存、最后退出实例的设置生效，以及错误编码导致加载失败后保留原文件。启动前检查构建开启 `DEV=ON` 和 `QMCLIENT_TEST_STORAGE=ON`，否则拒绝运行，避免访问真实用户存档。产物保留在 `tmp/config-persistence-*`；坏配置场景记录日志与原文件状态后仅结束本场景的实例。场景已更新，尚未在符合这些开关的客户端上运行；普通 Release 构建不作为此 runner 的运行证据。

便携版专用端到端测试使用 `QMCLIENT_PORTABLE=ON` 的真实发布客户端，复制到 `tmp/` 后验证缺省 `profile/` 随目录搬家、显式存储配置的保存位置与搜索顺序、工作目录配置回退，以及首条保存路径不可用时终止初始化。新增场景已补充，未执行：

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

可用的 E2E 场景包括 `assert_dialog_no_false_hang`、`hang_watchdog_reports_stall`、`slow_asset_loading_no_false_hang`、`asset_loading_stall_reports_hang`、`system_font_restore_no_false_hang`、`demo_recording`、`qm_lifecycle_persistence`、`invalid_statistics_preserved`、`perf_log_persistence`、`connection_failure_recovery`、`recording_without_connection` 和 `startup_saved_favorites`。每个场景使用独立临时目录，并验证真实进程产生的日志、退出状态和文件产物。gate 中使用 `--run-qm-smoke` 时会先运行 smoke，再运行注册的 E2E 场景。

`assert_dialog_no_false_hang` 依赖客户端测试专用参数 `--qm-test-main-thread-assert`：它在主循环之前触发一次真实断言，等价于启动阶段的错误弹窗，用于验证进程内弹窗阻塞主线程期间 hang 看门狗不会误报、退出兜底看门狗不会强杀正在阅读的弹窗。场景同时通过环境变量 `QMCLIENT_TEST_HIDE_DIALOG` 隐藏弹窗窗口并抑制报告进程拉起，避免桌面点击提前关闭弹窗、测试遗留后台进程干扰断言；弹窗的创建和消息循环行为不变。

`hang_watchdog_reports_stall` 使用 `--qm-test-main-thread-stall` 在主循环内阻塞 12 秒，验证 hang 看门狗使用单调时钟后能在主线程阻塞期间真实写出卡死报告（正向回归，防止时钟退回 tick 缓存）。Windows 卡死场景会继续等待报告追加转储结果，并检查 `.dmp` 文件的 `MDMP` 签名；不能在文本报告刚出现时就结束进程。

`settings_language_round_trip` 在隔离目录创建页签译文，先绘制 TClient、外观和素材页，再离开这些页面切到英文并重载原路径的更新译文。每次用真实截图完成作为绘制观察点，检查两次往返后客户端正常退出且没有崩溃报告；它不比较截图文字或布局。

`fatal_exception_exits_without_hang` 仅用于 Windows，通过 `--qm-test-main-thread-exception` 注入不可继续的访问违例，检查原始异常、本地报告、已确认标记、有效 minidump 与非零退出结果，且没有额外 hang 产物。两个新场景已补充，未执行。

资源加载场景通过测试专用环境变量 `QMCLIENT_TEST_ASSET_LOAD_DELAY_MS` 在每项启动素材加载后注入延迟，取值限制为 0 到 20000 毫秒。`slow_asset_loading_no_false_hang` 每项延迟 1 秒，验证累计加载超过 10 秒仍能启动并响应命令，且没有 hang 产物；`asset_loading_stall_reports_hang` 单次延迟 12 秒，验证启动期间真正停滞仍被看门狗发现。两者都使用有界日志等待，并通过 `QMCLIENT_TEST_HIDE_DIALOG` 抑制报告窗口。

`system_font_restore_no_false_hang` 复用字体资源冒烟的生成器，在隔离目录创建测试字体。该场景仅运行于 Windows，其他平台明确报告跳过。仅测试存储构建启用 `QMCLIENT_TEST_SYSTEM_FONT_FILE` 和 `QMCLIENT_TEST_FONT_LOOKUP_DELAY_MS`：查找固定延迟 12 秒，不枚举真实系统字体。场景检查查找期间客户端已启动且能处理输入，关闭候选时仍恢复保存的中文字体，并且没有 hang 产物。已补充场景，未执行。

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

## Vulkan FSAA 背景模糊回归

```text
python qmclient_scripts/integration/vulkan_fsaa_blur_smoke.py cmake-build-release --validation-layer <Vulkan-SDK-Bin>
```

入口使用隔离存储构建，需要可见桌面、Vulkan GPU 和验证层。默认在 1/3 个渲染线程下分别串行检查 0×、4×、8×、再次 4×（可用 `--samples`、`--render-threads` 缩小范围），每次执行模糊开启、关闭和重新开启；要求对应采样模式下背板捕获激活、每一步生成新截图、调整窗口高度并用截图尺寸确认交换链重建、正常退出，且没有命令缓冲复用、设备丢失或退出资源泄漏错误。不会把旧截图日志当作新事件，退出码为零也不能绕过验证层错误。上游 device layer 注册提示单独记录，不属于本场景 GPU 命令验证。聚焦只选择已显示的客户端窗口，避免显示 SDL 隐藏探测窗口。截图仅作为视觉验收产物，自动检查不声称图片内容已证明模糊效果；日志与图片保留在 `tmp/startup_capture_font_fix/`。此设备场景单独运行，不加入无 GPU 的常规 gate。
