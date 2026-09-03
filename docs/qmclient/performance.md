---
title: QmClient 性能与图形诊断阶段记录
date: 2026-09-03
status: active
---

# 当前阶段

已建立自动落盘的诊断骨架：`CQmDiagnostics` 在用户存储目录写入
`qmclient/diagnostics/session-<timestamp>-<monotonic-suffix>.jsonl`，结束时写入带同一
会话后缀的 `report-*.json`。启动时会先创建 `qmclient` 再创建
`qmclient/diagnostics`，避免干净用户目录因非递归建目录失败。

当前记录内容包括：

- session 起止、客户端状态切换和窗口 resize 事件；
- update、完整 frame start-to-start 间隔和 game render CPU 采样窗口、平均值、p95、p99、最大帧耗时和 frame interval 1% low FPS；
- 纹理/Buffer 内存使用量；
- graphics 主线程事件：窗口属性/显示器切换请求、窗口重建、VSync/MSAA 请求结果和后端 fatal error；窗口尺寸同时记录逻辑尺寸与实际 canvas 尺寸；
- OpenGL/GLES context 事件：profile、GLSL 版本、extension 列表/数量/截断状态、最大纹理尺寸和 debug callback 可用性/启用状态；
- Vulkan 初始化快照：实例/设备 API、设备名/驱动、实例扩展和 layer 数量、验证层/debug messenger、队列族、内存 heap/type、timestamp query 能力、present mode、surface format 和 swapchain 尺寸/图像数；
- 图形后端配置值、实际 active API/vendor/renderer/version、active API 能力版本、通用 buffering 能力、纹理/Buffer/stream/staging 内存、窗口尺寸和 HiDPI。配置值与实际 active API 分开记录，能识别 fallback，避免误导性能分析。

计时 hook 位于 `CGameClient::OnUpdate`、`CGameClient::OnRender` 和
`CClient` 的 `Render()`/`Swap()` 外围；后两个通过 `IGameClient::OnFrameStart` /
`OnFrameEnd` 进入 Qm runtime。Qm feature 由 `CQmRuntime` 组合根承载，
`QmPlayerIndicator` 接收 runtime 生成的窄 frame，不直接依赖 `CGameClient`。
诊断失败只记录 warning，不阻止客户端启动。

`frame_interval_*` 是从一次 frame start 到下一次 frame start 的主循环间隔，
可以用于用户可感知的 pacing/1% low；它仍不是 GPU 完成时间。`render_cpu_*`
只表示 `CGameClient::OnRender` 的 CPU 区间，二者不能互相替代。

# 当前限制

这不是完整性能系统。尚未完成：

- Metal 的后端专属字段和跨后端字段收口；Vulkan 基础初始化快照、instance 创建阶段与运行期 validation 消息已接入，但成功路径仍需真实运行样本；OpenGL/GLES debug callback 已接入代码，但仍需各目标平台的 runtime smoke；
- 非阻塞 GPU query、shader/pipeline 失败、device loss、fallback 和资源重建事件；
- 每个 feature、UI 首次打开/搜索/滚动/布局阶段的独立计时；
- 工作集、Qm owned、纹理/字体 cache、后台 job 和 helper 的分项内存统计；
- 日志保留/轮转策略和同场景 A/B 采样工具。
- session JSONL 已改用 DDNet `ASYNCIO` writer；主线程只提交固定大小的短记录，退出时等待
  writer 完成并生成 report。non-blocking graphics observer 现在自动记录尝试、成功和丢弃原因
  （session lock、writer lock、buffer capacity、serialization、session inactive），并在 session
  末尾写入 `diagnostics_summary`、在 report 中写入同一组摘要；异常前后的事件 ring buffer
  已由 report 自动导出，但其容量固定且仍没有日志保留/轮转策略。
- graphics 对象创建后、backend `Init()` 前已启动最小 Qm session 并注册 listener；失败分支会自动记录 `graphics_init_failed` 并生成尽力而为的 report。诊断初始化阶段不访问未就绪 backend。OpenGL/GLES context 成功后的专属字段已经接入；初始化失败前的具体 Vulkan/device 字段仍未覆盖。
- resize 事件已接入 runtime，但 renderer switch、device loss、shader failure、fallback 和
  resource rebuild 尚未从各后端统一发出结构化事件。
- 普通 JSONL 单行在一个 ASYNCIO 锁区间内提交，避免多线程事件把 JSON 内容和换行交错；graphics observer 事件使用固定栈缓冲区和 ASYNCIO try-lock，忙时允许丢弃，避免阻塞渲染/致命错误路径；
  但每 600 个样本的精确分位数排序仍在主线程执行，尚未迁移到后台统计 worker，不能把
  当前诊断开销视为已经量化为零。

因此当前日志只能证明“有自动诊断证据”和“基础帧统计可用”，不能据此宣称已经完成图形后端性能定位。
通用 graphics 事件入口已经建立；OpenGL/GLES 基础能力快照和 debug callback 统计已接入，Vulkan instance 创建阶段与运行期 validation 消息、关键 `VkResult` 结构化事件已接入，但 Metal 专属字段、完整 device-fault 事件、shader failure 和资源重建事件仍需分别接入。

## OpenGL/GLES debug callback 增量（2026-09-03）

- 桌面 OpenGL 的 `GL_KHR_debug` / `GL_ARB_debug_output` 与 GLES 的 `GL_KHR_debug` 均通过当前图形 context 注册 callback；GLES 使用 `SDL_GL_GetProcAddress`，扩展名按空格分隔 token 精确匹配。
- callback 只做有界消息复制、固定容量 ring 写入、普通日志输出和原子累计；事件 `graphics.opengl.debug_messages` 最多每秒一次，字段为 session 累计值，并包含 error/severity 分类、未知 severity 数量、最后观察到回调的 `last_seen_*` 元数据，以及 ring 中最近 8 条消息的有界快照。普通日志仍保留 512 字节上限；ring 总容量为 32，覆盖次数通过 `recent_dropped` 记录；锁竞争和文本截断分别通过 `recent_lock_dropped`、`recent_truncated` 记录。
- callback 的 user data 是由 `CGraphicsBackend_SDL_GL` 持有的独立状态，生命周期覆盖 processor 删除和 context teardown；GL context 仍有效时注销 callback，关闭闸门阻止新 callback 进入，并等待已进入 callback 结束后再 flush 最终统计，避免 backend 删除后的悬空 `this`。shutdown 不以固定超时换取退出，而是等待 callback 生命周期真正收口。
- GL command error/warning 的提前返回路径调用强制 `FlushCommands()`，普通帧路径仍由 `EndCommands()` 按 1 秒限流，避免错误发生前已经收到的驱动消息只存在于普通日志而不进入自动诊断事件。
- 具体驱动消息文本不在 callback 中写 JSONL，而是先进入固定容量 ring，再由图形线程按限流事件带入自动 JSONL；这样保留故障前后的上下文，同时避免高频 callback 的同步 I/O 影响 1% low。统计事件字段的 scope 是 `backend_attempt_cumulative`，因为图形初始化重试会重新建立 backend state。
- 当前 Windows 桌面 Release 已通过 OpenGL 构建；使用同一 MSVC 参数强制编译 GLES wrapper 时被环境缺少 `GLES3/gl3.h` 阻断，Android/Emscripten/GLES runtime 仍未验证，不能据此标记 GLES 完成。

## 验证记录（2026-09-03）

- Release `game-client`：通过，包含 runtime、i18n、UI model、异步诊断 writer 和 active API 字段变更。
- Release `run_cxx_tests`：373/373 通过，3 个既有 disabled；包含诊断指标/JSON 转义测试。
- Release `run_rust_tests`：`cargo test --locked` 通过，单元测试 0 个，doc-tests 18 个通过。
- `py -3 qmclient_scripts/check_qmclient_boundary.py`：通过，默认扫描 22 个 Qm/官方触点源文件；显式不存在路径返回失败。
- `codegraph sync`：最终一次同步 1 个变更文件；CodeGraph v1.6.0，状态为 `Index is up to date`。
- 当前工作区 Release 客户端正常退出 smoke：退出码 0，自动生成 session JSONL（5 行，全部可解析）和 report，`session_end` 存在且 `write_failed=false`；实际记录 `backend_config=Vulkan`、`active_api_name=OpenGL`，验证了 fallback 可见。
- 尚未完成真实联网游戏场景、Vulkan 实际成功路径、GLES 后端 smoke、后端 validation/debug callback、同场景 A/B 性能采样；这些不能由构建或单元测试替代。

## 图形诊断增量（2026-09-03）

- 对公共图形头文件改动执行了 Release clean rebuild，`game-client` 319/319 通过；此前启动崩溃未再复现，说明旧增量产物是首要排查项，但不能据此证明所有后端路径正确。
- `RecordEvent` 增加 JSON 容量计算的整数溢出保护；边界门禁和 373 个 C++ 测试仍通过。
- Vulkan 后端现在通过现有可选诊断快照输出初始化元数据，并由 SDL 后端统一转成自动 JSONL 事件；未新增协议、渲染语义或 Qm 业务依赖。
- 当前机器本次仍实际运行 OpenGL fallback，因此 Vulkan 专属字段只完成编译和静态路径验证，不能标记为 Vulkan runtime verified。

## Vulkan validation debug callback 增量（2026-09-03）

- Vulkan validation/performance 消息通过 `VK_EXT_debug_utils` callback 进入与 OpenGL/GLES 相同的固定容量 ring 和自动 diagnostics 事件链；callback 的 user data 使用 backend 生命周期之外仍有效的共享状态。
- 事件使用 Vulkan 专属 `severity_errors`、`severity_warnings`、`severity_info`、`severity_verbose` 和 `severity_unknown` 字段，不把 Vulkan severity 映射为 OpenGL 的 high/medium/low/notification；`last_seen_type` 与 recent message 的 type 保存真实 `VkDebugUtilsMessageTypeFlagsEXT`。
- `EndCommands()` 按一秒窗口限流 flush，shutdown 和初始化失败回退强制 flush；callback 注销等待 in-flight callback 完成，Vulkan instance 销毁前一定清理 messenger。初始化失败不再清空 instance 句柄后跳过清理。
- 已通过 `VkInstanceCreateInfo.pNext` 捕获 `vkCreateInstance` 期间的 validation/performance 消息；当前仍未在实际 Vulkan 成功路径上运行，本机默认仍是 Vulkan 请求、OpenGL active 的 fallback。
- `CheckVulkanCriticalError` 对非成功结果自动记录 `graphics.vulkan.result`，包含数值 result、稳定分类和调用 stage；shader module、pipeline layout、graphics pipeline 创建失败也经过该路径，能区分 shader/pipeline 创建失败和 shader 文件加载失败；原有错误返回保持不变。

实际 JSON 字段名为 `active_api_name`（不是 `active_api`）；分析工具应同时读取
`backend_config` 与 `active_api_name`，并把 `active_api_available=false` 视为不可用状态。

## 只读 review 收口（2026-09-03）

三个子代理分别审查了架构/上游边界、性能/图形诊断、功能/UI/i18n。已处理的问题包括：

- 官方 `CGameClient` 状态读取集中到 `qm_game_state_adapter.*`；
- 关闭 player indicator 时跳过完整 frame 构造；
- i18n 缺失 key 回退、card canonical model 校验和空 ID 防护；
- JSONL 单行原子入队；Windows Ninja/MSVC 前缀修复的构建前置与错误码传播。

仍保持 `PARTIAL` 的项目：主线程分位数排序、Vulkan/OpenGL/GLES 专属事件、完整 report
摘要、Warlist/解冻中颜色/旧配置迁移、旧新 UI presentation、真实联网与后端 A/B 验证。

# 验证约定

性能比较必须固定构建类型、硬件、图形后端、场景、帧率上限/vsync 和采样窗口；至少同时比较平均值、p95、p99、最大帧和 1% low。

## 增量验证记录（2026-09-03）

- 增加通用 graphics event listener；无监听器时保持默认空实现，Qm runtime 注册后自动记录窗口属性、显示器切换、VSync、MSAA、窗口创建/销毁和后端 fatal 事件。
- 增加启动期 pending event 回放和 OpenGL/GLES context 能力快照；请求类事件使用 `request` / `request_result` 命名，resize 同时记录 logical/canvas 尺寸。
- Release `game-client`：通过。
- Release `run_cxx_tests`：373/373 通过，3 个既有 disabled。
- Release `run_rust_tests`：0 个单元测试、18 个 doc-tests 通过。
- `py -3 qmclient_scripts/check_qmclient_boundary.py`：通过，扫描 22 个源文件。
- `codegraph sync` / `codegraph status`：同步 5 个变更文件，索引最新。
- `git diff --check`：通过；仍有 Git 的 LF/CRLF 转换提示。
- 本增量未完成 Vulkan/OpenGL/GLES/Metal 专属成功路径和运行时事件 smoke，因此仍不能标记完整图形诊断为 `DONE`。

## P1 稳定性增量（2026-09-03）

- graphics listener 改为在 `CClient` 创建 graphics 对象后、调用 `Init()` 前注册，覆盖 backend 初始化期间产生的事件；注册/快照读取使用窄 mutex，回调在锁外执行，单个 observer 异常不会中断其他 observer。
- `RecordGraphicsInfo` 移除 1536 字节固定 JSON 缓冲区，改为按转义字符串动态构造，避免长驱动/renderer 字段造成截断或无效 JSON。
- fatal graphics error 的格式化、诊断回调和异常分配失败均隔离；graphics observer 使用有界 non-blocking 写入，原有 `dbg_assert_failed()` 仍是最终控制流。忙时事件可能丢失，专用 fatal ring buffer 仍是后续工作。
- diagnostics session 的关闭与 graphics observer 通过可尝试获取的 session 锁协调；non-blocking 写入在 async buffer 容量不足时直接丢弃，不触发 writer 扩容。初始化 pending 事件在统一 replay 队列中按序派发，并复用普通 listener 的异常隔离。
- Release `game-client`：通过；Release `run_cxx_tests`：373/373 通过；boundary check：通过。
- 当前工作区 Release smoke：进程正常退出（`CloseMainWindow`，退出码 0），最新 session 21 行全部可解析，含 `session_end`，report 已生成且 `write_failed=false`；本机仍是 `backend_config=Vulkan`、`active_api_name=OpenGL` 的 fallback，未验证 Vulkan 成功路径。

## 诊断丢弃统计增量（2026-09-03）

- non-blocking observer 事件使用原子计数记录 attempts/enqueued/dropped；`enqueued` 只表示
  成功提交到 `ASYNCIO` 环形缓冲区，不等于已经落盘。丢弃原因分为
  `session_lock_busy`、`session_inactive`、`writer_lock_busy`、`buffer_capacity` 和
  `serialization_failure`。
- `session_start` 增加 schema version 2 和 non-blocking 策略标识；session 结束前写入
  `diagnostics_summary`，自动 report 同时包含事件计数和 graphics fallback/availability 摘要。
- 计数读取只发生在 session 收尾和 report 生成路径；事件生产路径不等待、不扩容。
- non-blocking observer 先通过可尝试获取的共享统计闸门进入；shutdown 取得独占闸门后，
  不再接受新的 observer 统计，并等待已进入的回调结束，再写出 summary/report，避免收尾期间
  的计数漂移。`active_api_available` 表示当前 backend 名称已识别，另有
  `active_api_version_available` 表示 driver-age 版本查询可用；不能用后者替代前者，尤其是 GLES。
- report 同时保留 `requested_backend` 和当前 `backend_config`，fallback 判断以初始化开始时的
  requested 值为准；`graphics_info_recorded` 仅在 graphics_info 成功提交到 session writer 后置为 true。
- report 现在自动导出固定容量的最近事件 ring buffer（按时间顺序，包含截断标记、容量和是否回绕）；
  它覆盖异常事件前后的上下文，不依赖手动复制日志，也不在事件生产路径分配内存。
- 非阻塞图形事件在 shutdown 统计闸门争用时也会计入 attempts/dropped，并单独记录
  `drop_stats_gate_busy`，保持丢弃原因不被伪装成 session 或 writer 锁争用。
- 图形事件注册、启动 pending 队列和回放路径对分配/同步异常做隔离；异常时记录 warning、丢弃受影响事件并清除 replay 状态，避免诊断系统卡住后续事件或改变图形错误控制流。
- fatal graphics 事件分类改为与错误文本顺序无关的固定优先级，组合错误不会因附加错误字符串排列变化而产生不同诊断事件。
