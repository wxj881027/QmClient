# QmClient 阶段 0–5 验证清单

更新时间：2026-09-08

本清单区分“功能切片完成”和“架构/交互验证完成”。切片可以在纯逻辑、构建和静态门禁齐全时标记 DONE；真实 UI、联网和性能证据单独记录，不用未运行的场景冒充通过。

## 本轮增量证据（截至 2026-09-08）

本节按批次记录证据；最新工作树状态见“阶段 0–5 最终代码批次”。历史通过结果不能覆盖其后尚未验证的代码。

### 2026-09-08 全局卡片重构批次（(page,card) 放置模型 + 统一搜索 + 真实拖拽）

- 按目标模型 `ui-ux-refactor-plan.md` 重构全局卡片底层：descriptor 与页面声明分离、`(page, card)` 放置 order model（present 标记、跨页合并）、每页投影与新卡补位、schema v3 与 v2/v1 迁移、`card_search_logic` + `CCardSearchIndex` 统一搜索、`card_drag_logic` 拖拽事务/让位/自动滚动；设置视图接页面 tabs、真实拖拽、跨页投放与自动保存。明细与行为变更登记见 `feature-ledger.md` `qm.ui` 条目。
- 构建：`qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target testrunner` 与 `--target game-client` 均增量成功；期间停止了占用 `DDNet.exe` 的工作区客户端实例后完成链接。
- 测试：`cmake-build-release\testrunner.exe` 全量 **500 passed / 3 disabled**（含卡片 order/registry/projection/preferences/adapter/search/drag 与资源加载共 87 项专项）。
- 门禁：`py -3 qmclient_scripts/check_qmclient_boundary.py` 通过（51 source / 3 logic）；`py -3 qmclient_scripts/qmclient_i18n.py validate` 通过（44 messages / 41 source records，本批补登记 `Open page`、`Reset page`、`Toggle`）；`py -3 qmclient_scripts/check_qmclient_runtime_smoke.py` 通过（11 session lines，OpenGL，`write_failed=false`）。
- 本批测试侧修复（均非业务逻辑放宽）：projection 测试 feature 局部变量悬空改为 static；拖拽阈值零距离断言按实现语义修正；搜索优先级断言与实现/头文件文档对齐（DESCRIPTION > ALIAS）。
- gap：多页拖拽、跨页投放、搜索导航与列表/卡片双 presentation 的真实鼠标/触控/IME 交互未验证；性能 A/B 未运行。

### 2026-09-08 OpenGL 四边形绘制回归修复

- 原构建日志 `tmp/build-game-client.log` 记录 `GL_QUADS` 和 `GL_TEXTURE_2D_ARRAY_EXT` 重定义；条件编译边界使 GLES 三角形替代宏泄漏到桌面 GL。恢复 GLES 分支和文件级 guard，保留现有平台适配。
- `cmd /c qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target game-client --parallel 14`：通过，重新编译 OpenGL backend 并链接客户端，无上述宏重定义警告。
- `py -3 qmclient_scripts/check_qmclient_runtime_smoke.py`：通过；另以 `--base-config tmp/opengl-quad-regression.cfg` 请求 OpenGL 1.1 路径，通过。两次均为临时 storage，diagnostics 开关路径和正常退出通过；不以启动成功冒充截图视觉验收。
- 只读 review 核对 GL/GLES guard、QUADS draw 调用、backend callback 声明/定义/调用一致性。最终 Windows 增量构建及 OpenGL 1.1 fixture smoke 再次通过。WSL 首次构建发现 GLES 头顺序和 backend_threaded.cpp 异常编译选项问题，修复后两个翻译单元及桌面 GL 均编译通过；完整 game-client 被检查期间并行变化的卡片接口阻断（Find/Move/RegisterCard 等调用不匹配），日志 `tmp/opengl-regression-linux-build.log`。未覆盖这些并行改动；最终工作树全量构建不标通过。真实画面恢复、Android/iOS 运行尚未验证。

### 2026-09-08 i18n 工具重写证据

- `py -3 qmclient_scripts/qmclient_i18n.py migrate --write`：完成旧 Qm key/fallback 到官方 source-key TOML 的迁移。
- `py -3 qmclient_scripts/qmclient_i18n.py validate`：通过，41 条英文 source key、39 条源码 source record。
- `scan`、`generate --languages simplified_chinese --output-dir tmp/qmclient-languages`：通过；保留现有 867 条官方条目并按官方格式加入 Qm source。
- DeepSeek 翻译未执行真实 API 请求，未写入新译文。
- 本轮未进行人工 UI、联网、demo/观战或性能验收；这些由用户手工执行。

### 2026-09-08 地基修复增量证据

- 修复卡片 presentation 缺口：`qm.player_indicator`、`qm.auto_team_lock`、`qm.speedrun_timer` 现在与 `qm.diagnostics` 一样注册为 `toggle` 并绑定各自 `qm_` 配置键；新增 adapter 回归测试。
- runtime 初始化改为 fail-closed：feature、card 或 dispatch 注册失败时不进入 `m_Initialized`，避免部分接线继续运行。
- `SQmFeatureModel` 移到 `core/qm_feature_model.h`；feature header 不再反向 include `game/client/ui/card_registry.h`。
- 规定入口构建：`cmd /c qmclient_scripts\\cmake-windows.cmd --build cmake-build-release --target testrunner --parallel 14`、`--target game-client --parallel 14` 均成功。
- 自动验证：Qm/UI/资源专项 `82 passed`；完整 Windows `testrunner` **469 passed / 3 disabled**；runtime smoke 通过（26 session events，OpenGL，`write_failed=false`）；boundary `50 source / 3 logic`、i18n `41 source key / 39 source record` 通过。
- 人工验收仍未完成：当前 CUA 环境未暴露本机原生窗口，无法可靠点击检查设置页；联网、demo/观战、触控/IME/DPI 和性能 A/B 继续保留为待验。

- 基础修复前新增 3 个测试真实失败：非法偏好部分导入、重复 ID、搜索忽略 availability/用户排序；修复后全量 420 tests passed / 3 disabled。
- 偏好 schema 与调度接入后增量构建成功，测试曾出现 432 passed / 1 failed（dirty 初值异常）；runtime smoke 曾 30 秒超时，去掉 `-s` 后观察到 sound SampleId 断言。未通过改配置禁音或跳过路径掩盖问题。
- 完整重建命令：`cmd /c qmclient_scripts\cmake-windows.cmd --build cmake-build-release --target game-client --clean-first --parallel 14`；成功完成 330 步。随后串行构建 `--target testrunner --parallel 14`，成功完成 130 步。
- 完整重建后 `cmake-build-release/testrunner.exe --gtest_brief=1`：433 tests passed / 3 disabled。没有修改业务逻辑或该失败断言，异常消失；证据支持增量产物布局不一致，Ninja 依赖遗漏的具体原因仍未定位。
- 完整重建后 `py -3 qmclient_scripts/check_qmclient_runtime_smoke.py`：通过，26 session events，OpenGL，`write_failed=false`、临时 report 为零、diagnostics 关闭不生成文件。受控 UI model 初始化/释放已通过实际启动退出验证。
- `py -3 qmclient_scripts/check_qmclient_boundary.py`：通过，44 source / 3 logic。`py -3 qmclient_scripts/qmclient_i18n.py validate`：通过，23 messages / 19 fallback。边界门禁不自动覆盖 core dispatcher 的纯 logic include，后续须显式扫描。
- smoke 脚本保留 `tmp/qmclient-runtime-smoke-*.log`；stdout 改文件避免未消费管道填满阻塞，去掉 `-s` 保留启动错误。独立 100000 字节子进程已验证修复；这不等于客户端根因就是管道。
- 官方 adapter / snapshot / 跨页布局批次：2026-09-07 19:24，串行 `game-client`、`testrunner` 增量构建成功；全量 441 tests passed / 3 disabled；boundary 47 source / 3 logic、i18n 23 messages / 19 fallback 通过；runtime smoke 再次通过（26 session events，OpenGL，关闭 diagnostics 无文件）。这批验证不代表真实 UI 或完整布局持久化完成。
- 历史资源 loader 基础批次：clean Release `game-client`、`testrunner` 编译通过，445 passed / 3 disabled；runtime smoke 启动退出通过。该 smoke 未断言 PNG 解码、上传或 UI 消费，不证明真实资源页完成。
- Linux 配置已完成，但编译未通过。曾缺 `GL/glu.h`；仓库已初始化 `ddnet-libs`，bundled GLEW 的默认 GLU include 与缺少系统 `libglew-dev` 路径有关，不能笼统归咎 WSL。当前工作树已调整 bundled GLEW 编译定义，后续还发现现有诊断代码的 GNU exceptions、Vulkan 字段及 GLES 条件编译问题；最后失败日志为 `tmp/linux-build.log`，不可标 Linux 通过。

### 阶段 0–5 代码批次记录

- 首轮 Windows clean Release `game-client` 336 步、`testrunner` 135 步链接成功。独立 `tmp/final-verification/` cwd 全量测试为 452 passed / 13 failed / 3 disabled；13 个失败均由新增 PNG fixture 在官方 `SavePng(IOHANDLE)` 已关闭文件后再关闭一次导致。已删重复 close，待代理重跑；不将首轮失败隐去或记为业务回归已排除。
- fixture 修复后首次重跑为 464 passed / 1 failed / 3 disabled；唯一失败是假定 temp storage 的 game 目录只有测试创建项，实际还包含打包资源 `game_06`。断言已改为验证 default/custom 唯一、隐藏目录过滤和重开 revision，不排除合法上游资源。
- 只读 terra/high 审查发现两个 P2：64 项预览淘汰未释放 manifest/metrics，长期浏览会耗尽 8192 上限；Windows 通用 `fs_rename` 在目标占用时可能先删旧偏好。资源 resident 现同时拥有并淘汰 preview/manifest/cache/metrics，排队满时不提前登记；偏好提交改为标准库单次 replace rename，不执行删除重试，失败保留旧文件和 dirty。新增 80 项 LRU、同名注销重开及 Windows 目标锁定回归测试，待重跑。
- runtime smoke 默认不再复制真实 `%APPDATA%/DDNet/settings_ddnet.cfg`，仅显式 `--base-config` 可输入 fixture；失败摘要只返回日志路径，不回显可能含 binds/连接信息的原日志。已清理本轮旧 `cmake-build-release/storage.cfg`，根目录未发现测试遗留文件夹。
- 已移除 runtime 无条件加载 `data/extras.png` 的无 UI 消费入口。设置图标 atlas 与官方六类 Assets 页面现均由实际消费者按需请求。
- loader 新增逐请求 ID、协作取消、路径/文件/像素/队列上限；主线程按每次 Poll 的纹理数量预算上传，全部成功后才公布 READY，失败/失效回收部分上传结果。
- 新增真实 job pool + 临时 storage + PNG fixture 测试代码：缓存命中、限额上传、缺失/损坏候选 fallback、缩略图、读取/上传失败、同 generation 重开、部分上传失效、目录 generation、失败不重试、shutdown、大尺寸 PNG header 和有界队列。未运行，不能沿用 445 作为本批通过数。
- auto lock / timer 的 enabled 现在同时要求 availability；timer 自动关闭后立即撤销当帧 render 资格。新增 model 门控回归测试，尚未运行。
- 最终自动验证按用户要求交 terra / high 子代理；真实人工验证允许列明待验后交付。必须先推送 `upstream` 同名分支，再给最终报告；未实现代码、失败检查和人工待验分开列出。
- Qm 设置页按钮消费 Phosphor Bold atlas；官方 Assets 六页后台扫描目录、只加载可见缩略图、候选解码失败继续 fallback、64 项 LRU、DPI/resize/map generation、切页取消和失败提示均已接入。生成器命令实际成功输出 17 个图标至 `tmp/qm_icon_manifest.h`；C++ 构建、实际画面、交互和 A/B 尚待本轮证据。
- 页面关闭输出 `qm/ui-resource` 指标行，字段为 scale、attempts、cache_hits、后台 load_ns、最近/峰值 decoded_bytes 和 failures；重开只累计一次 cache hit，不按帧计数。该数据可用于同一构建/后端下首开与重开 A/B，但 decoded_bytes 口径不含 decoder 临时内存、driver staging 或 GPU 显存。
- 相同指标通过 runtime 的窄回调写入现有 diagnostics/session 的 `ui.resource_page` 事件；页面关闭或客户端 shutdown 时记录一次，diagnostics 关闭时不写。当前尚未跑真实客户端生成该事件，不把 callback 已接入视为 A/B 证据已采集。

## 固定验收类别

| 类别 | 所需证据 | 当前缺口 |
| --- | --- | --- |
| 构建 | Windows Release client + tests；WSL Linux game-client | 两平台通过；macOS/Android 未运行 |
| 单元测试 | 全量结果与各功能边界测试，disabled 原因单列 | Windows 468、Linux native scratch 461 通过，各 3 disabled；挂载盘失败历史保留 |
| UI smoke | 官方与 Qm、新/旧 presentation；键盘/触控/IME/resize/DPI/主题/动画 | List/Cards 已接入，真实交互待验 |
| runtime smoke | 实际启动/退出、diagnostics 开关、生命周期 | 最终 Windows OpenGL 通过；其他后端 runtime 未运行 |
| 真实联网 smoke | race、auto lock 服务端响应及关闭不发送 | 未运行 |
| demo/观战 smoke | playback/seek、dummy、多人渲染及观战 | 未运行 |
| 性能 A/B | 同场景同后端同采样窗口加载耗时及内存 | loader metrics 已接 diagnostics，真实画面样本待人工采集 |
| 覆盖率 | 可重复 line/branch coverage 命令及报告 | 实际采集成功；只覆盖单测路径，最后 4 条 runtime 门控 delta 未重采 |

## 最终自动结果

- Windows：clean Release `game-client`、`testrunner` 构建完成；审查修复后串行增量重编译、全量 **468 passed / 3 disabled**。证据 `tmp/final-verification/windows-testrunner-final-20260907.xml`。
- disabled：`ReplaceWords.DISABLED_MissingBoundaries`、`ReplaceWords.DISABLED_Cyrillic`、`ReplaceWords.DISABLED_Spread`，是官方既有禁用项，本轮未启用或修改；不计入通过数。
- runtime：`py -3 qmclient_scripts/check_qmclient_runtime_smoke.py` 通过；`base_config=none`、26 session events、OpenGL、`write_failed=false`、临时 report=0、diagnostics 关闭无文件。该测试不打开资源页面，不证明 UI/预览或 A/B 通过。
- 静态/脚本：boundary 49 source / 3 logic、i18n 41 source key / 39 source record；Python 语法、图标 manifest 3 tests、coverage 汇总 2 tests、`git diff --cached --check` 均通过。
- 只读审查复核：terra / high 确认资源元数据 LRU 和 Windows 偏好替换两项 P2 已闭合，限定范围未发现具体残余；该复核没有代跑 UI、真实联网或性能采样。相关测试已包含在上述 468 passed 中。
- Linux：WSL 内 GCC/gcov 13.3 Debug coverage 配置成功，`game-client` 350/350、`testrunner` 136/136 编译链接成功，实际覆盖 GLES、Assets provider 和 loader 翻译单元。仓库挂载目录作为 test cwd 时为 459 passed / 2 failed / 3 disabled，失败均为官方 `Filesystem.RenameOpenFileSource*`；同一插桩二进制改用 `--test-work-root /tmp` 后为 **461 passed / 3 disabled**。这组 A/B 将差异定位到测试 scratch 文件系统语义，不是 Qm 测试或依赖缺失。标准 Release 已恢复配置并成功链接 `game-client`，最后的 runtime 配置门控也已在 Linux 增量编译。
- 最终 coverage：`test_exit_code=0`、`collection_errors=0`；Qm 扩展和 `src/game/client/ui/` 单元测试口径 line **60.23% (2055/3412)**、branch **25.75% (1792/6960)**。报告在 `tmp/final-verification/linux-native-coverage-final/`；这不是 UI/runtime/联网覆盖率，未插桩源码清单以报告为准。其后仅补 runtime 的当前配置关闭门控；该 4 条门控已两平台编译和 runtime smoke，但没有重配 coverage，因此报告不声称覆盖该最后 delta。
- 清理：根目录未发现测试遗留文件夹，旧构建目录 smoke `storage.cfg` 已删除。最终三份 Windows test cwd 已确认是本轮产物；递归清理被执行策略拒绝，仍保留在 `tmp/final-verification/windows-testrunner-work*`，不上传 Git。XML/log 历史证据另行保留。

## 已执行

- [x] Release `game-client` 与 `testrunner` 构建。
- [x] 历史基线：`cmake-build-release\testrunner.exe`：415 tests passed，3 disabled。
- [x] `py -3 qmclient_scripts/check_qmclient_boundary.py`。
- [x] `py -3 qmclient_scripts/qmclient_i18n.py validate`。
- [x] `git diff --check`（仅换行符提示，无 whitespace error）。
- [x] runtime smoke：离线启动/退出、diagnostics session/report、关闭 diagnostics 不生成文件。
- [x] 全局卡片 registry：注册冻结、owner、页面、排序、搜索、非法引用测试。
- [x] 全局 order model：Full/Left/Right、默认 merge、旧格式解析、序列化、revision 和非法项过滤测试。
- [x] deck projection：可见性、不可用 feature、列投影、新卡默认补位和 order revision 测试。
- [x] presentation contract：状态优先级、折叠、不可用卡片、布局边界和动作优先级测试。
- [x] UI foundation：隐藏、默认折叠/排序、不可用 feature 过滤、偏好导入导出测试。
- [x] 资源页 cache：同 generation 去重、过期结果拒绝、失败状态和 invalidate 测试。

## 人工验收待执行

- [ ] 官方 HUD 卡片与 Qm 诊断卡片在 List/Cards 两种 presentation 中打开、搜索、隐藏、恢复、跨页排序、保存并重启恢复。
- [ ] 键盘 Tab/方向键、触控、IME、DPI/resize、主题和动画开关 smoke。
- [ ] Assets 六页首开/重开/损坏 PNG/切页/resize/DPI 肉眼检查，并从 diagnostics 提取同场景首开与 cache 命中样本。
- [ ] 真实联网 race：speedrun timer kill、auto team lock `/lock 1` 服务端响应。
- [ ] demo 播放/seek、观战、dummy 双本地角色、多人 player indicator 截图回归。
- [ ] 关闭路径每帧采样：确认不构造输入、不执行 logic、不进入对应 render。

## 生命周期矩阵

以下是当前已迁切片的代码契约，不是尚未运行的真实服务器/画面通过记录。`core/qm_runtime.cpp` 的 `UpdateFeatureModels` 在官方更新分发前刷新 enabled/availability；render/update 注册表初始化后冻结，仅遍历目标槽位，同时检查当前配置以覆盖同帧输入关闭，门控通过后才打包输入和计时。

| owner | 关闭 / 不可用 | 断线、重连、切图 | demo / dummy / 观战 | resize / shutdown |
| --- | --- | --- | --- | --- |
| `qm.player_indicator` | 不构造固定玩家 frame、不调用 Render；model 无跨帧玩家缓存 | 当前官方 snapshot 决定 availability，不持有旧地图玩家指针 | 非 ONLINE 或观战禁用；跳过两端本地 ID；仅 race + 有角色 + 允许 zoom | 下一次绘制读取新 screen；无任务/纹理 owner |
| `qm.auto_team_lock` | enabled 下降沿 Disable 清 pending/deadline，保留最近队伍，防关闭重开重发；不构造输入/执行 Update/发聊天 | 非 ONLINE、reset、map load 清两个 dummy 槽位；重连重新按当前队伍调度 | demo 无更新；dummy 固定两槽，当前连接 ID 必须匹配本地信息；不可锁队伍取消 pending，观战以官方 team/snapshot 为准 | 无渲染或外部任务；shutdown 不发命令 |
| `qm.speedrun_timer` | 关闭或 availability 下降沿 Reset，自动关闭在同帧撤销 render；不构造输入/执行 Update/发 kill | 非 ONLINE、reset、map load 清 race/过期状态；新起跑 tick 重置倒计时 | demo 禁用；当前 dummy 的 tick，缺本地角色/比赛未开始不 kill；seek 不继承 live race | HUD 每次读取当前尺寸；无纹理/任务 owner，shutdown 无 kill |
| `qm.ui` | 页面关闭停输入、IME，取消未完成 atlas；官方/Qm 配置仍是唯一业务 owner | 偏好不随地图/连接重置；仅资源失效，model 指针在 registry 冻结后创建 | 官方菜单决定可见/输入；搜索框优先于卡片键盘，官方 popup 先行；不接管官方游戏输入 | resize 重置滚动并失效 atlas；先停 view/IME，再保存偏好并释放 view/model |
| `qm.assets` | 页未打开不扫描/解码；离开页取消临时结果，READY 预览保留于 64 项 LRU | generation + request ID 拒绝旧结果；取消 scan 仍保留到 DONE，避免反复切页堆积任务 | 菜单资源与 live/demo 状态解耦，不写游戏 snapshot，选择仍走官方接口 | 缩略图随实际像素尺度重建；shutdown 取消工作并释放 GPU，官方 job pool 先于 storage 销毁排空 |
| `qm.diagnostics` | 设置注明 restart required；启动时关闭不创建 session/writer/query | listener weak owner + session generation，不让旧 callback 写新 session | 仅诊断，不修改预测、网络、demo；所有场景失败不阻断游戏 | resize 清帧节奏窗口，shutdown 先撤销 listener 再关 writer；实时切换开关并非本批能力 |
| `qm.config_migration` | 仅配置加载阶段消费白名单，不在帧循环运行 | 不随地图或连接重跑 | 不参与游戏输入/网络/demo | 无 worker/graphics；配置命令由官方 manager 所有 |

每个 owner 的验收：

- [ ] 指示器：关闭、观战、demo 都零新增绘制；双端本地角色不互相指示；切图无旧玩家。
- [ ] 自动锁：离队、关闭、断线在 delay 内取消；重连能重新安排；两 dummy 槽位互不污染。
- [ ] 计时器：默认关闭不 kill；超时只触发一次，auto-disable 同帧不画；死亡/重开 race/切图不继承旧计时。
- [ ] UI：两种展示共享配置/排序；输入框 IME 活动时方向/删除键不作用于卡片；关页不留下 active input。
- [ ] Assets：切页/切图/DPI 时旧任务不公布纹理；损坏文件有占位，游戏仍运行；快速切页不增长无限任务。
- [ ] 诊断：启动关闭无文件，开启的 session/report 正常结束，失败路径不阻断；GPU 后端各自验证。
- [ ] 配置迁移：新键优先、旧键仅白名单；真实旧配置 fixture 和未迁键保持单独 gap。

## 人工操作步骤

只运行 `cmake-build-release/` 内的当前工作区客户端，不替换正式客户端配置。用单独 storage，保留该目录下 diagnostics 证据；触控设备、真实服务器、demo 文件由验收人选择。

| 场景 | 步骤与预期 |
| --- | --- |
| 双 presentation | 设置 → QmClient，在 List/Cards 搜索 `HUD`/`Diagnostics`；隐藏后显示隐藏项并恢复；上下移动、折叠、保存、重启。两个视图顺序一致，原官方 HUD checkbox 与卡片值同步，默认配置不被复制修改 |
| 键盘 / IME | 搜索框输入中英文并移动光标、删除组合文本；Tab/Shift+Tab 转焦点，方向键导航，Enter 切换，Ctrl+上下排序；输入框活动时除 Tab 外不触发卡片动作，弹窗优先 |
| 触控 / 窗口 | 真触控点击/滚动列表，窗口从窄到宽，切全屏及 100/150/200% DPI，开关主题/动画。没有重叠、文字截断、误触或资源消失；缩放后图标/预览清晰 |
| Assets | 六个 tab 各首开→离开→重开；加入有效 PNG 与损坏 PNG，刷新、快速切页、resize。仅可见缩略图异步加载，失败项显示 Error，其他项与游戏仍可用；实际选择资源外观与官方一致 |
| 联网 / demo | 自有或获准服务器验证 race、DDRace team lock、dummy、观战、重连/切图；播放 demo 并 seek。核对上表每个 owner 的零副作用/复位条件，保存服务端响应与截图，不能用离线启动 smoke 替代 |
| 性能 A/B | 同一 Release、硬件、后端、窗口尺寸和资源目录，A 首开、B 已完成后关页重开，至少 10 对；保存 `ui.resource_page` 事件和同窗口 frame pacing。比较 worker load_ns、cache_hits、上传数、缩略图保留字节及帧时间。Assets 的 scale=0 表示目录聚合，各图保留字节之和不等于同时常驻内存或进程/GPU 峰值；后者需要独立 profiler |

## 覆盖度

已有 `qmclient_scripts/collect_qm_coverage.py` 和统计逻辑测试 `test_qm_coverage.py`，实际报告及边界见上方最终结果。WSL 使用 GCC/gcov 13.3，不需要 lcov/gcovr；脚本仅依赖 Python 标准库和匹配编译器的 gcov。

可重复采集顺序（所有构建目标串行）：

```sh
cd /mnt/e/Coding/DDNet/QmClient
cmake -S . -B cmake-build-linux -DCMAKE_BUILD_TYPE=Debug -DDOWNLOAD_GTEST=ON \
  -DCMAKE_C_FLAGS=--coverage -DCMAKE_CXX_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage
cmake --build cmake-build-linux --target game-client --clean-first -j 14
cmake --build cmake-build-linux --target testrunner -j 14
python3 qmclient_scripts/test_qm_coverage.py
python3 qmclient_scripts/collect_qm_coverage.py --test-work-root /tmp
```

采集前只删除 `cmake-build-linux/` 内的 `.gcda` 计数器；默认测试工作目录和 gcov 临时目录在输出目录下自动清理。WSL 仓库位于 `/mnt/e` 时，可显式 `--test-work-root /tmp` 将测试创建的文件放到原生 Linux 文件系统；只清理该参数下新建的随机子目录，不删除 root。所有报告仍输出到仓库 `tmp/qm-coverage/`：`coverage.json`、`tests.xml`、`tests.log`、`gcov.log`。报告记录 scratch root、GTest 原始退出码、disabled 名称和 gcov 错误，失败不伪装为通过；同一二进制的 DrvFS/native A/B 用于确认差异，而非屏蔽测试。

口径：范围仅为 Qm 扩展目录和 `src/game/client/ui/`；line 按源文件/行取执行并集，branch 按编译实例（translation unit / function / line / branch）计数，不把模板实例混为同一分支。未生成 gcov 行信息的源文件显式列入 `uninstrumented_sources`，不能声称已经测得整个仓库覆盖率。此脚本只运行单元测试，不代表 UI、真实联网或 demo 的运行覆盖率；测试数量不等于覆盖率。

完成后恢复标准 Linux Release 配置再做发布构建验证，不将 Debug 插桩构建用作性能 A/B 样本。

## 通过标准

未执行项必须保持未勾选。自动验证通过后可按用户授权先提交推送并交付人工验收清单；这表示本轮交付结束，不代表人工 UI、联网、demo、性能 A/B 或所有平台已通过。阶段 6 仍须等基础验收满足后再启动。
