# 平台功能对比：macOS / Linux / Android 相对 Windows 缺什么

本文回答「非 Windows 用户拿不到哪些功能」。取证方式：**静态代码审计**（三个只读审计各自
负责 Qm 自有功能 / 引擎与后端 / 构建与 CI 三条线），关键结论由维护侧抽检复核（表中标 ✓）。

口径与边界：

- **不做实机验证**，不给「能不能跑」下结论；只回答「代码里有没有这条路径」。
- 证据一律给 `文件:行号`；无证据的推测集中放 §6，不混进结论表。
- 平台列取值：有 / 部分（降级实现）/ 无 / 未知。
- 「缺功能」分两类：**平台机制差异**（如 Android 没有独立进程）与**尚未实现**（如非 Windows 更新器）。
  两者用户可见后果不同，表中分别标注。

结论摘要（先看这四条链）：

1. **媒体岛与歌词整体不可用**（macOS / Linux / Android）：SMTC（系统媒体控制）只在
   `CONF_FAMILY_WINDOWS && _MSC_VER` 下编译，歌词的 5 个来源全部依赖它或 Windows helper 进程。
2. **没有应用内自动更新**（macOS / Linux / Android）：更新检查/下载/退出安装五处入口都是
   `#if !defined(CONF_FAMILY_WINDOWS) return`，`qm-client-updater` 与签名发布流水线也只有 Windows。
3. **一批环境类开关在非 Windows 是死开关**：qWAVE QoS、进程高优先级、自绘 IME 候选条、
   GPU/显存采样、崩溃 dump 与「按崩溃报告自动切安全图形后端」。
4. **macOS 还有若干降级实现**：窗口化/独占全屏都退化成桌面全屏、OpenGL 封顶 4.1 且默认 OpenGL、
   每帧强制 `WaitForIdle`；**Linux** 额外要求系统提供 Vulkan 才能 configure；**Android** 最彻底：
   无进程 spawn / 打开本地文件 / 外部链接（改走 `SDL_OpenURL`）、强制 GLES、无视频录制与地图工具链。

> 反向缺口（Windows 相对缺的）：**桌面通知**（`notifications.cpp` 只有 macOS 与 libnotify 分支）、
> IPv6 DSCP 标记、OpenGL 4.4–4.0/3.3 回退候选（`backend_sdl.cpp` 的 Windows 分支被裁过）。

## 1. QmClient 自有功能

| 功能 | 证据（`file:line`） | Win | macOS | Linux | Android | 用户可见后果 | 来源 |
|------|--------------------|-----|-------|-------|---------|--------------|------|
| 系统媒体控制 SMTC（媒体岛曲目/封面/播放控制） | `system_media_controls.h:90-93`（`CONF_FAMILY_WINDOWS && _MSC_VER` 才置 1）✓ | 有 | 无 | 无 | 无 | 媒体岛不显示歌曲信息与封面，设置页 SMTC 卡的上一曲/播放/下一曲点了没反应 | 审计+抽检 ✓ |
| 歌词岛（网易云/汽水/酷狗/QQ/Spotify） | `hud.cpp:3801-3817` 三级来源都要 Hook 或 SMTC；`netease_integration.cpp:155-162`、`qm_spotify_integration.cpp:336-343` 以 `GetStateSnapshot` 为前置 | 有 | 无 | 无 | 无 | 歌词功能整体不可用，界面停在「等待歌词采集器」 | 审计 |
| 音乐 App 自动跟随（同时只认一个 Hook） | `music_app_watcher.cpp:20-42`（`BuildRunningMask()` 非 Windows 恒 0） | 有 | 无 | 无 | 无 | 先开哪个音乐 App 就永远只认它，不会跟着实际播放切换 | 审计 |
| 网易云/汽水/酷狗/QQ 的 helper 注入进程 | `CMakeLists.txt:162-281`（`windows && 64bit` 才建 4 个 helper + minhook） | 有 | 无 | 无 | 无 | 非 Windows 完全没有这些 helper | 审计 |
| 客户端自动更新（检查/下载/退出安装） | `tclient.cpp:2799,2884,2933,2943` ✓（五处 `CONF_FAMILY_WINDOWS` 门控）；`CMakeLists.txt:133-135` ✓（`AUTOUPDATE` 非 WIN32 默认 OFF） | 有 | 无 | 无 | 无 | 无更新检查/下载/安装，只能手动替换；`qm_auto_update` 是死开关 | 审计+抽检 ✓ |
| 更新清单与签名资产 | `update_manifest.cpp:111-116` 只认 `QmClient-windows.*`；`sign_update_release.py:172-173` 拒绝非 Windows 包名 | 有 | 无 | 无 | 无 | 只有 Windows 走 Ed25519 签名的更新链路 | 审计 |
| Qm 自绘 IME 候选窗（分页/滚动/sticky） | `qm_ime_policy.h:11-18,20-25`（非 Windows 恒「用系统候选」）；`input.cpp:1075-1231` 用 `ImmGetCandidateListW` 填候选表 | 有 | 无 | 无 | 无 | 非 Windows 只能用系统候选框，`qm_new_ime` 相关 UI 无效 | 审计 |
| 低延迟网络 QoS（qWAVE 流标记） | `system.cpp:144`（`LoadLibraryExW(L"qwave.dll")`）✓；非 Windows 返回 `UNAVAILABLE` | 有 | 无 | 无 | 无 | `qm_net_qos` 恒 failed、无任何效果（可用 `qm_net_qos_status` 验证） | 审计+抽检 ✓ |
| 进程高优先级 | `client.cpp:104-122`（非 Windows 只打一行「not supported」） | 有 | 无 | 无 | 无 | `qm_process_high_priority` 勾了没用 | 审计 |
| GPU 占用 / 显存（独占+共享）/ 磁盘读速率 | `monitoring_device_perf.cpp:218-366`（PDH + DXGI 全在 Windows 宏内） | 有 | 无 | 无 | 无 | 性能日志 `perf/device` 的 gpu/vram/disk 字段恒 -1，提交问题时缺 GPU 证据 | 审计 |
| 「DDNet / 总 CPU」比例 | `monitoring_device_perf.cpp:114-187`（总 CPU 只实现 Linux `/proc/stat` 与 Windows PDH） | 有 | 无 | 有 | 无 | macOS/Android 只显示自身 CPU，没有分母 | 审计 |
| 崩溃转储（`.dmp`/`.RTP`）与挂起 dump | `base/crashdump.cpp:7-19`（非 Windows 空实现）；`client.cpp:5729-5738`（hang dump 在 Windows 宏内） | 有 | 无 | 无 | 无 | 非 Windows 崩溃只有文本日志 | 审计 |
| 按崩溃报告自动切安全图形后端 | `client.cpp:145-152`（只认 `_fatal_report.txt`/`.RTP`）、`546-581` | 有 | 无 | 无 | 无 | 非 Windows 的驱动崩溃回退流程基本触发不了 | 审计 |
| 机器标识（HWID，实时通道/语音绑定基础） | `qmclient.cpp:537-572`（Windows 读注册表 `MachineGuid`、Linux 读 `/etc/machine-id`，其余 `return false`） | 有 | 部分 | 有 | 部分 | macOS/Android 退化成「安装级随机值」，换机或删配置即换身份 | 审计 |
| 协议/扩展名注册（`ddnet://`、`.map`/`.demo` 关联） | `client.cpp:6784-6787`、`7200+`、`menus_settings.cpp:8680-8688` 全在 Windows 宏内 | 有 | 无 | 无 | 无 | 非 Windows 无注册/反注册入口（平台机制差异，影响小） | 审计 |
| 打开本地文件/目录（截图、日志目录） | `system.cpp:2263,2397-2418` 与 `system.h:934-1020` 排除 Android | 有 | 有 | 有 | 无 | Android 不能从客户端打开文件管理器定位截图 | 审计 |
| 桌面通知 | `notifications.cpp:27-39`（macOS 原生 + Unix libnotify，无 Windows 分支） | **无** | 有 | 部分 | 无 | **反向缺口**：Windows 收不到桌面通知 | 审计 |

## 2. 引擎 / 后端 / 平台层

| 能力 | 证据（`file:line`） | Win | macOS | Linux | Android | 后果 | 来源 |
|------|--------------------|-----|-------|-------|---------|------|------|
| 窗口化全屏（`gfx_fullscreen=3`） | `graphics_threaded.cpp:3636-3642` ✓、`backend_sdl.cpp:1646-1649`：非 Windows 把模式 3 并入桌面全屏 | 有 | 无 | 无 | 无 | 非 Windows 选「窗口化全屏」实际得到桌面全屏 | 审计+抽检 ✓ |
| 独占全屏（`gfx_fullscreen=1`） | `backend_sdl.cpp:1650-1658`（macOS/Haiku 用 `SDL_WINDOW_FULLSCREEN_DESKTOP`，注释 `Todo SDL`） | 有 | 部分 | 部分 | 部分 | macOS 没有真独占全屏 | 审计 |
| Vulkan 后端 | `CMakeLists.txt:119-126,151,316-319`；`cmake/FindVulkan.cmake:17-83`；`ddnet-libs/vulkan` 无 linux/android 目录 | 有 | 有（MoltenVK） | 有（须系统 libvulkan） | 有（NDK） | Linux 未装 Vulkan 开发包会直接 configure 失败 | 审计 |
| 默认图形后端 | `config_variables.h:811-819`（Android/Emscripten 默认 GLES；非 32 位且非 macOS 默认 Vulkan；其余 OpenGL） | Vulkan | OpenGL | Vulkan | GLES | macOS 即使打包了 MoltenVK 也默认跑 OpenGL | 审计 |
| OpenGL 版本上限 | `src/engine/graphics.h:166-176`（macOS 探到 4.1，其它 4.6） | 4.6 | 4.1 | 4.6 | GLES 3.0 | macOS 用不了 4.3+ 特性 | 审计 |
| 视频录制（FFmpeg）与 GIF/JPEG/视频背景 | `CMakeLists.txt:5042-5043`；`cmake_android.sh:170 -DVIDEORECORDER=OFF`；`background.cpp:578-588,717-734` | 有 | 有 | 有 | 无 | Android 无录制、背景只支持 PNG/WebP | 审计 |
| 启动外部进程 / 本地服务器 | `system.cpp:2264-2322`（Android 无该函数）；`local_server.cpp:19-30`（Android 走 JNI 前台 Service） | 有（独立 exe） | 有 | 有 | 部分 | Android 服务器**存在**，但必须是带通知权限的前台 Service | 审计 |
| 地图工具链（`dilate`/`map_optimize` 等） | `CMakeLists.txt:3843-3912`；`cmake_android.sh:167 -DTOOLS=OFF` | 有 | 有 | 有 | 无 | Android 不产出任何工具 | 审计 |
| Steam 集成 | `CMakeLists.txt:2804-2818`、`steam_api_stub.cpp:9` | 有 | stub | stub | 强制 OFF | 非 Windows Steam 功能恒不可用 | 审计 |
| Discord Rich Presence | `CMakeLists.txt:283-286`（Android 强制 OFF） | 有 | 有 | 有 | 无 | Android 无 Discord | 审计 |
| macOS 文本编辑快捷键（Option 按词移动） | `lineinput.cpp:224-228` 用了从未定义的 `CONF_PLATFORM_MACOSX`（`detect.h:81` 定义的是 `CONF_PLATFORM_MACOS`） | — | 无（死代码） | — | — | macOS 上只能用 Ctrl 而非 Option | 审计 |

## 3. 构建、打包与发布

| 项 | 证据 | 结论 |
|----|------|------|
| 仅 Windows 的构建目标 | `CMakeLists.txt:162-281`（minhook 与 4 个 helper）、`3623-3632`（`qm-client-updater`）、`1182-1184`（`qm_update`） | 自更新器与音乐 helper 在非 Windows 构建里**根本不存在** |
| 仅 macOS 的目标 | `CMakeLists.txt:3815-3824`（`game-server-launcher`） | 只有 macOS 有独立服务器 launcher |
| Android 缺工具 | `scripts/android/cmake_android.sh:167` | `TOOLS=OFF` |
| 打包形态 | `CMakeLists.txt:4853-4866`（Win zip/7z、macOS dmg、Linux tar.xz）；Android 走 Gradle（`cmake_android.sh:201-289`） | 四平台各有产物，但只有 Windows 包带更新签名资产 |
| 发布签名 | `build.yml:717-732`、`sign_update_release.py:26,172-173` | Ed25519 更新签名链路仅 Windows |
| CI 覆盖面 | `.github/workflows/style.yml:38` 只跑 `--mode quick`；`strict_build`（`/WX` + `/analyze` + 严格 clang-tidy）、`dilate`、`identifiers`、`clang_tidy_warn` 只在本地 `--mode full` | CI 覆盖「能构建 + 能跑单元测试 + 能打包」，不覆盖本地严格门禁 |
| 本地 `--mode full` 的平台限制 | `gate/checks/strict_build.py` 硬要求 `qmclient_scripts/cmake-windows.cmd` | 非 Windows 主机上 full 门禁结构性无法通过 |

## 4. 「设置项照常出现但无效」清单（UX 缺陷，可单独修）

非 Windows 上这些开关**照常显示、点了没用**（配置项与卡片都没有平台判断）：

| 设置项 | 注册处 | 实际效果 |
|--------|--------|----------|
| `qm_net_qos` | `config_variables_qmclient.h:19` | 恒 failed（`qm_net_qos_status` 可自查） |
| `qm_process_high_priority` | `config_variables_qmclient.h:18` | 只打一行日志 |
| `qm_auto_update` | `config_variables_qmclient.h:55` | 无更新器可调用 |
| `qm_new_ime` 系列 | `menus_qmclient.cpp` 的 IME 卡片 | 自绘候选逻辑不执行 |
| 音乐 Hook 开关（5 个来源） | `menus_qmclient.cpp:3870-3902` | 无 helper 进程可连 |
| 媒体岛 / 歌词卡 | `QmCardCatalogHud.cpp:368-369` 无条件挂载 | 恒无媒体状态 |

**建议**（未实施，等拍板）：按平台把这些卡片/开关隐藏或标注「仅 Windows 可用」，
而不是让用户自己试错。

## 5. 不确定项（不下结论）

1. macOS `gfx_fullscreen=1` 的真实行为（代码映射为桌面全屏，但 SDL 在 macOS 的 Space 语义未验证）。
2. macOS Vulkan resize 不重建 swapchain 后画面是否正确（`backend_vulkan.cpp:9454-9456`）。
3. macOS 打包产物名与代码里硬编码的 `DDNet.app`/`DDNet-Server.app` 是否一致（`src/macos/client.mm:13,20`、`storage.cpp:408`、`local_server.cpp:37-39`）——需比对实际产物。
4. `system_media_controls.h:90` 的 `_MSC_VER` 条件意味着 **MinGW/Clang 构建的 Windows 也没有媒体岛**；本仓库 CI 用 MSVC，实际影响面未验证。
5. Intel macOS 的内置 WebP（`ddnet-libs/webp/mac` 只有 `libarm64`）是否真缺——取决于是否用系统 Homebrew WebP。
6. macOS 自绘 IME 是「有意让位给系统候选」还是「尚未实现」——代码看不出意图，需作者确认。
7. Steam 是否由仓库外流程替换成真 SDK（仓库内只有 stub）。
8. `CONF_INFORM_UPDATE` 定义但全仓库零引用（被取代还是漏接）。
9. Android 是否有 Java 侧文件选择器补偿 `open_file` 缺失。
10. Android 上 `gfx_fullscreen` 0–3 四档的实际差异（代码里未见区分）。

## 6. 怎么自己复核

| 想确认的事 | 命令/位置 |
|-----------|-----------|
| QoS 在本机是否生效 | 控制台 `qm_net_qos_status` |
| GPU/显存/磁盘采样是否有值 | 性能日志 `perf/device` 的 `gpu_util_percent` / `gpu_dedicated_vram_mb` / `disk_read_mb_s` |
| 本机平台宏 | `src/base/detect.h`（`CONF_FAMILY_*` / `CONF_PLATFORM_*`）与 `CMakeLists.txt` 的 `TARGET_OS` |
| 某功能是否编进本机二进制 | `qmclient_scripts/gate/check_gate.py --mode quick` + CMake 缓存里的 `CONF_*` 定义 |
| 媒体岛/歌词缺失原因 | 先看 `system_media_controls.h` 的 `SYSTEM_MEDIA_CONTROLS_WINRT_ENABLED`，再看 `music_app_watcher.cpp` |
