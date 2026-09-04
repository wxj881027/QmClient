---
title: QmClient 官方上游补丁台账
date: 2026-09-02
status: active
baseline: ddnet-20.0
---

# QmClient 官方上游补丁台账

本文件只登记重构过程中必须修改官方 DDNet 文件的窄适配点。Qm 业务、provider、卡片布局和统计逻辑不得放入官方核心文件。当前触点保持默认行为不变，尚未引入协议、物理、预测或服务端玩法补丁。

| hook_id | official_file | reason | category | default_behavior | conflict_risk | removal_condition | tests | status |
|---|---|---|---|---|---|---|---|---|
| qm.runtime.registration | src/game/client/gameclient.cpp; src/game/client/gameclient.h | 注册 Qm composition root | Qm_ADAPTER | 官方行为不变；runtime 无业务时无可见差异 | 组件列表顺序冲突 | runtime 删除或官方提供等价扩展注册点 | baseline build; lifecycle contract | active |
| qm.graphics.telemetry | src/engine/client/client.h; src/engine/client/client.cpp | 在 graphics 初始化前后记录诊断会话，并保留失败上下文 | Qm_ADAPTER | telemetry 关闭或不可用时不改变渲染 | 图形初始化顺序、接口扩展和上游 renderer 同步 | 官方提供等价结构化诊断接口 | graphics init failure smoke; no-blocking check | active |
| qm.graphics.events | src/engine/graphics.h; src/engine/client/graphics_threaded.h; src/engine/client/graphics_threaded.cpp; src/engine/client/backend_sdl.h; src/engine/client/backend_sdl.cpp | 提供通用 graphics 生命周期、窗口配置和后端错误事件监听，交给 Qm diagnostics 自动落盘；后端 fatal 前回调，保留不可返回错误 | Qm_ADAPTER | 无监听器时不产生额外业务行为；回调运行在事件发出线程，listener 必须线程安全且不得阻塞 | IGraphics/backend factory 接口和 graphics threaded 主循环同步 | 官方提供等价结构化 graphics event listener | Release build; event callback smoke; fatal path review | active |
| qm.graphics.backend_snapshot | src/engine/client/backend/backend_base.h; src/engine/client/backend_sdl.h; src/engine/client/backend_sdl.cpp; src/engine/client/backend/vulkan/backend_vulkan.cpp | 通过已有可选 command 输出结构记录 Vulkan 初始化元数据，并由统一事件入口自动落盘 | Qm_ADAPTER | 默认不改变后端选择、设备创建、队列提交、present 或资源生命周期；采集失败只保留不可用值/原因 | backend command 结构、Vulkan 初始化顺序和后续上游字段变更 | 官方提供等价 backend diagnostics snapshot；Vulkan/Metal 成功路径验证完成 | Release clean build; C++ tests; OpenGL fallback smoke; Vulkan runtime pending | active |
| qm.graphics.opengl_debug_callback | src/engine/client/backend/backend_base.h; src/engine/client/backend/opengl/backend_opengl.h; src/engine/client/backend/opengl/backend_opengl.cpp; src/engine/client/backend/opengl/backend_opengl3.cpp; src/engine/client/backend/opengles/backend_opengles.cpp; src/engine/client/backend/opengles/backend_opengles3.cpp; src/engine/client/backend_sdl.h; src/engine/client/backend_sdl.cpp | 自动接入 OpenGL/GLES debug callback 的限流统计、固定容量消息上下文、错误路径强制 flush、独立 callback state 和 shutdown 注销 | Qm_ADAPTER | `dbg_gfx=none` 时不注册 callback；启用时只增加诊断日志/事件，不改变渲染命令和默认后端选择 | OpenGL/GLES backend 包装文件、派生 shutdown 覆盖、平台头文件、context teardown 顺序和 callback state 生命周期 | 官方提供等价 debug sink 且能保留自动诊断字段；不再需要 callback-only shutdown 和 Qm ring state 时移除对应适配 | Release OpenGL build; C++ tests; boundary check; GLES wrapper compile blocked by missing GLES3 headers; backend runtime pending | active |
| qm.graphics.vulkan_debug_callback | src/engine/client/backend/backend_base.h; src/engine/client/backend/vulkan/backend_vulkan.cpp; src/engine/client/backend_sdl.cpp | 自动接入 Vulkan `VK_EXT_debug_utils` validation/performance callback、instance 创建阶段 `pNext` callback、固定容量消息上下文、运行期限流 flush、初始化失败清理和 callback-only shutdown | Qm_ADAPTER | `dbg_gfx` 未启用时不注册 messenger；启用时只增加日志/诊断事件，不改变 Vulkan command、present、swapchain 或默认后端选择 | Vulkan command 结构、instance/device 生命周期、debug messenger 注销顺序、instance `pNext` 链和可选扩展 guard | 官方提供等价 debug sink；不再需要 Qm callback state 时移除适配 | Release Vulkan compile; C++ tests; boundary check; Vulkan success-path runtime pending | active |
| qm.particles.toggle | 待定 | 为粒子开关暴露最小控制能力 | Qm_ADAPTER | 默认保持官方粒子行为 | update/render 顺序和 1% low | 官方粒子组件已有公开开关 | particle regression; disabled overhead | INVENTORY |
| qm.ui.presentation | 待定 | 为 legacy/new UI 提供稳定菜单桥接 | Qm_ADAPTER | legacy 模式保持官方 UI | menus 生命周期和输入优先级 | 官方 UI 提供可插拔页面槽位 | keyboard/touch/IME smoke | INVENTORY |
| qm.frame.telemetry | src/engine/client/client.h; src/engine/client/client.cpp; src/game/client/gameclient.h; src/game/client/gameclient.cpp | 在 Render/Swap 外围提供完整 frame interval 计时边界 | Qm_ADAPTER | 默认 hook 为空；不改变官方渲染和同步语义 | engine/client 接口与主循环冲突 | 官方提供等价 frame telemetry hook | Release build; C++ tests; pacing smoke | active |
| qm.graphics.bootstrap | src/engine/client/client.h; src/engine/client/client.cpp; src/game/client/gameclient.h; src/game/client/gameclient.cpp | 在 graphics Init 前启动诊断，并在 Init 失败时自动记录上下文 | Qm_ADAPTER | 默认 hook 为空；图形初始化和错误对话框行为不变 | 启动顺序、失败路径、接口扩展 | 官方提供等价初始化诊断回调 | Release build; graphics failure smoke | active |
| qm.aio.close_error | src/base/aio.cpp | 将异步 writer 的最终 `io_close` 错误保留到 `aio_error`，使 diagnostics 能识别关闭阶段失败 | UPSTREAM_CHANGE | 成功关闭和已有写入错误行为不变；只补充错误状态 | base aio 生命周期和其他异步日志调用方 | 上游 `ASYNCIO` 提供等价 close/flush 错误状态 | Release C++ tests; diagnostics lifecycle smoke | active |

## 记录规则

- 每个实际修改官方文件的提交只包含一个清晰 hook，并标注 Qm_ADAPTER 或 UPSTREAM_CHANGE。
- 必须记录修改前后的默认行为、调用阶段、线程归属、性能预算、冲突重放方式和删除条件。
- 如果实现需要协议、snapshot、demo、物理、预测或服务端玩法变化，必须改标为 UPSTREAM_CHANGE，暂停普通迁移流程，单独评审。
- 暂定 hook 不能提前加入官方代码；先在 Qm 独立文件中证明需求确实存在。

## 2026-09-04 诊断范围冻结

现有图形诊断只作为 QmClient 阶段性 A/B 和基础故障定位基础，不再继续向官方 Vulkan、OpenGL 或 SDL 文件添加深层诊断字段。后续若要补充 Metal 成功路径、GPU query、device fault 深度信息或 renderer switch，必须先提交独立的需求证据、上游冲突评估和可删除条件；在此之前保持现状，不把 diagnostics 继续扩展成官方后端的平行实现。
## 当前重构分支新增触点

### `src/engine/shared/config_variables.h`

- 在文件末尾的 mod 配置区域增加 `qm_diagnostics` 和 `qm_player_indicator_*` 配置；diagnostics 默认开启以保证自动证据，显式关闭时不启动 writer/listener；player indicator 默认关闭，不改变官方默认行为。
- 触点范围：配置声明和自动生成的 `CConfig` 字段；没有协议、snapshot、预测或 demo 格式变化。
- 冲突风险：低。后续上游同步优先把配置继续放在 mod 区域；若官方提供等价配置，按功能台账做一次映射评估。
- 删除条件：Qm 功能删除或配置收口到独立扩展配置注册机制后，连同样板功能一起移除。

### `src/game/client/gameclient.cpp` / `gameclient.h`

- `gameclient.h` 只增加 `CQmRuntime` 组合根成员和 include；`gameclient.cpp` 保留一个 Qm 组件集成点，并将其放在伤害指示器之后、官方 HUD 之前。
- `CQmRuntime` 在该触点内转发 `CComponent` 生命周期给 `CQmPlayerIndicator`，不让官方组件依赖 Qm 业务类型；runtime 内部将当前触点明确命名为 `ENTITY_OVERLAY`，并保留 `WORLD_BACKGROUND`、`HUD`、`MENU` 作为后续窄 adapter 的槽位契约。
- 默认行为：`qm_player_indicator=0` 时不执行渲染；诊断计时仍只记录，不改变游戏逻辑。
- 冲突风险：中低。组件列表排序是上游可能改动的位置；重放时只需重新选择 HUD 前的窄 hook。
- 删除条件：QmRuntime 改为官方 manifest/adapter 集成，或所有 Qm 功能不再需要该组合根。

### `src/engine/client/client.cpp` / `src/engine/client/client.h`

- `IGameClient::OnFrameStart` / `OnFrameEnd` 使用默认空实现，避免扩大其他实现的强制接口负担。
- `CClient` 只在主循环的 `Render()` 前和 `Graphics()->Swap()` 后调用 hook；不把 Qm 类型 include 到 engine client。
- 目的：让 `frame_interval_*` 观察主循环 pacing，而不是把 `CGameClient::OnRender` CPU 时间误称为完整帧。
- 冲突风险：中。官方主循环或 `IGameClient` 接口变更时需重新定位两个窄调用点。
- 删除条件：官方提供同等的完整 frame telemetry 边界，且 Qm diagnostics 不再需要该 hook。

- `CClient` 创建 graphics 实例后、调用其 `Init()` 前通过 `OnGraphicsInitBegin(IGraphics *)` 启动最小 Qm session 并注册 listener；`OnGraphicsInitFailed` 在失败分支记录 `graphics_init_failed` 并尽力关闭 session/report。
- 两个接口均为默认空实现，其他 `IGameClient` 实现不需要同步修改；Qm 类型不进入 engine client。
- 成功初始化后，`CQmRuntime::OnInit` 附加真实 graphics 接口并记录 `graphics_info`；初始化前不访问未就绪的 backend。

### `backend_base.h` / `backend_sdl.*` / `backend_opengl.cpp`

- `SCommand_Init` 增加可选的 `SGraphicsBackendDiagnostics` 输出指针；OpenGL/GLES 在拥有当前 context 的图形线程采集 GLSL、extension、context profile、最大纹理尺寸和 debug callback 状态，主线程等待命令完成后通过统一 graphics event 入口自动落盘。
- `CGraphics_Threaded` 在 listener 注册前暂存最多 64 个启动事件，Qm 诊断初始化后注册 listener 并按序回放；超过有界队列的事件记录丢弃计数，不能宣称启动期事件绝对不丢失。
- 默认渲染、debug 配置和 API 选择不变；采集失败只写 `available=false` 或明确的不可用原因，不阻断启动。
- 现有 Vulkan 初始化失败后的 OpenGL 重试增加 `graphics.backend_fallback_attempt` 旁路事件，记录实际 `from`/`to`、请求的 `requested_to`、是否真的应用了回退的 `applied`、选择来源 `selection_source` 和固定 `reason`；这是回退尝试事件，不把它当作目标 backend 已初始化成功的结果，不改变原有配置回退、版本重试或初始化控制流。环境变量强制覆盖配置时允许记录 `applied=false`，避免把重复 Vulkan 重试误报为已切换到 OpenGL。
- backend 选择完成后增加 `graphics.backend_selection` 旁路事件，记录选择出的 backend 和 `selection_source=config|environment_valid|environment_empty|environment_invalid|default`；Qm report 同时保留初始化开始时的配置请求、最近一次 graphics info 的 backend 配置、选择来源和实际 active backend。`requested_backend_config` 暂作为与 `requested_backend` 同值的兼容字段保留；空值/无效值只细化来源诊断，不改变 DDNet 20.0 当前保持初始 OpenGL 选择的行为，环境变量覆盖时不把配置差异误判为 fallback。
- fallback attempt 之后增加 `graphics.backend_fallback_result` 结果事件，覆盖所有后续 Init 返回路径，记录 `success`、`failed` 或最终选择仍未改变时的 `not_applied`，并携带 `init_result` 与 DDNet graphics backend wrapper 的 `error_code`；该错误码不是原始 Vulkan `VkResult`。Qm report 保留 `backend_fallback_attempted`、`backend_fallback_applied`、`backend_fallback_result`：前两个是当前 session 内的累计事实，后者是当前 session 最后一次已观察结果；其摘要通过单个原子状态更新，不依赖非阻塞 observer 成功拿到 session 锁。事件行仍可能因 listener、队列、锁或有界 buffer 的 best-effort 策略丢失；事件进入 observer 后，生命周期关闭或 generation 不匹配的迟到事件会被拒绝，不进入事件 ring 或 JSONL。将 fallback 尝试、结果与最终 active backend 判定分离，不改变后端错误和重试控制流。
- 监听器回放在 Qm session 建立后进行；注册、pending 队列和 listener 快照读取使用窄 mutex，回调在锁外执行并对单个 listener 异常隔离，快照失败只丢弃当前事件，回放期间的新事件继续进入同一队列，避免乱序；这仍不是每帧路径的同步保证。窗口属性/显示器切换记录为 request，VSync/MSAA 记录为 request result，不把后端接受请求等同于 swapchain 或 context 已完成重建。
- fatal graphics error 的 observer 使用有界 non-blocking 写入；在格式化、分配、session 关闭、writer 忙、writer 容量不足或 observer 异常时仍回到原有 `dbg_assert_failed()`，诊断失败不能替代原始 fatal 行为。
- `ProcessError()` 在保留 `graphics.backend_error` 通用事件的同时，按统一错误类型和可用的原始后端错误文本发出稳定分类事件：
  `graphics.device_lost`、`graphics.shader_failure`、`graphics.pipeline_failure`、
  `graphics.out_of_memory`、`graphics.swapchain_failure`、命令记录/提交失败和初始化失败。
  其中 OpenGL/GLES 的 shader 加载/编译失败已有精确的 backend 事件，但 link/pipeline 失败仍按通用初始化失败记录，后续再补充对应触点。这一步只增加 observer 旁路，不改变原有错误翻译、日志和 `dbg_assert_failed()` 控制流；
  Vulkan 私有 swapchain 重建的 begin/end 事件另作为独立 hook 评估。
- Vulkan 关键结果现在通过同一事件 sink 记录 `graphics.vulkan.result`，包含数值 result、稳定分类和固定调用 stage；instance 创建、物理设备枚举、swapchain 创建、关键帧提交、memory command 提交、present-image helper 提交、acquire、present、memory recovery/command wait、swapchain recreate wait-idle、shutdown 和 window destroy wait-idle 均覆盖，acquire/present 的 out-of-date/suboptimal 特殊分支也会记录通用结果；shader module、pipeline layout、graphics pipeline 的创建失败也会带上明确 stage；同时记录 `graphics.swapchain_out_of_date`、
  `graphics.swapchain_suboptimal` 以及 `graphics.swapchain_recreate_begin/end`；
  acquire/present 的特殊分支会记录统一的 `stage` 字段，重建事件携带旧/新 image count、失败阶段和返回值；依赖资源初始化失败会并入重建结果。
  事件保持 Vulkan 的递归重试和资源清理顺序；交换链重建失败现在由 `PrepareFrame()` 传播为当前命令失败，避免在 `vkDeviceWaitIdle` 或依赖资源初始化失败后继续 acquire。
- Vulkan device fault 诊断统一为 backend-owned 的 KHR/EXT 适配：运行时优先启用 `VK_KHR_device_fault`；两者同时存在且 KHR feature 不支持时，仅在 EXT feature 可用时切换到 `VK_EXT_device_fault`，最终只启用一个扩展和对应 feature；EXT-only 时直接使用 EXT；分别加载对应函数并输出 `api=khr|ext|none`。KHR 使用零 timeout 的 report 查询，EXT 保留 counts/info 查询；扩展不可用、feature 不支持、feature 查询入口不可用、设备级故障查询函数指针缺失和查询 timeout 都形成明确事件。不得读取 vendor binary，且不改变 device-lost 控制流。
- presented-image helper 的 `vkEndCommandBuffer`、fence reset/submit/wait 和 mapped-memory invalidate 失败均记录固定 stage；command buffer 只有 fence wait 成功后才标记为可复用，invalidate 失败归类为 render command failure。成功路径和默认渲染行为不变；该 helper 错误路径采用 fail-fast 返回，不能表述为“原有错误返回保持不变”。
- graphics 事件注册、启动队列入队和 pending 回放增加异常恢复；监听器分配、队列分配或同步异常只丢弃诊断事件并恢复 replay 状态，不传播到原始图形路径。
- fatal graphics 事件分类先收集所有错误文本再按固定优先级选择根因，避免错误容器顺序变化造成 device lost、pipeline 或 shader 分类漂移。
- OpenGL/GLES debug callback 的具体消息只写入 backend-owned 固定容量 ring，事件 flush 时复制最近 8 条消息和 ring 覆盖、锁竞争、截断计数；初始化失败回退使用 callback-only shutdown 收口，不引入动态内存或同步 I/O，默认渲染行为不变。
- OpenGL/GLES shader 编译器持有可选的 graphics event sink；shader 文件加载失败和编译失败通过 `graphics.opengl.shader_failure` 记录 `api`、`stage`、`phase` 和 `file`，program link 失败通过 `graphics.opengl.program_link_failure` 记录 `api`、`phase`、`program_id` 和 linker log 是否可用；不改变原有 `LoadShader`/`LinkProgram` 返回值、初始化控制流或 shader 日志。shader 与 program 的源文件对应关系仍未进入事件，后续再单独设计。
- 统一 GPU 时间可用性字段由 OpenGL/GLES/Vulkan backend snapshot 自动输出：当前 `gpu_time_available=false`，原因是 `measurement_not_implemented`；Vulkan 的 `timestamp_query_supported` 只表示设备 capability，不等于已经完成 GPU 时间测量。未实现异步 query 前不得输出 `gpu_ms`。
- `CGraphics_Threaded::GotResized` 在画布尺寸实际变化时，对官方 resize listener 资源重建窗口发出 `graphics.resource_rebuild_begin/end`；事件包含旧/新画布尺寸、listener 数和结束结果（`completed` 只表示回调序列返回，不伪称资源内部成功），位于 listener 调用前后，不改变 listener 顺序、viewport、等待或默认渲染行为。该触点只覆盖 DDNet 20.0 当前的 resize-driven rebuild；初始化/后端 fallback 和未来显式 renderer switch 仍由各自事件记录。
- OpenGL2 初始化失败回退现在统一销毁已创建的 tile/border/3D shader program；正常 shutdown 也覆盖 border program。`CGLSLProgram` 对已创建但 link 失败的 program 同样执行 `glDeleteProgram`，避免 callback-only 路径或 shader 初始化失败留下 GPU 对象；未改变 shader 选择、渲染结果和 fallback 语义。
- 上游冲突风险：中。device-fault 诊断直接修改 `backend_vulkan.cpp` 的成员/函数指针、device-fault 查询摘要、device-lost 触发路径、设备扩展候选、逻辑设备 feature 查询、device `pNext` 和最终扩展过滤；同步时必须按上游 Vulkan backend 初始化流程重新定位，不能只重放 listener 调用。其他图形诊断仍集中在可选 command 输出结构和 listener 入口，优先保留 upstream 的 command 字段顺序与 backend init 流程。

## Review 修复（2026-09-04）

- Qm 测试边界脚本现在递归扫描根层 `qmclient_*.cpp/.h` 和路径中包含 `qmclient` 的嵌套测试目录，避免新增测试文件通过目录层级绕过 `TClient` 字段门禁；这是 Qm 工具脚本改动，不改变客户端运行时。
- OpenGL frame-boundary debug ring flush 改为 try-lock。图形 callback 持锁时本次 flush 记录 `recent_lock_dropped` 并在后续 command boundary 重试，不在渲染线程无限自旋；初始化阶段的锁收口仍保留等待语义。
- GLES backend 显式包含项目随 SDL 提供的 `SDL_opengles2_gl2ext.h`，使 `GL_KHR_debug` 类型和常量不依赖间接 include。
- Vulkan 快照区分 SDL 提供的必需 instance extension 数 `instance_extension_count` 与最终传给 `vkCreateInstance` 的启用数 `enabled_instance_extension_count`，后者包含可选 debug utils 扩展；默认扩展选择和初始化控制流不变。
- graphics listener 异常现在使 dispatch 返回失败，同时继续遍历其他 listener；调用方会准确计入该事件未完整 dispatch，且不会让诊断异常传播到原始图形路径。
- 删除条件：上游提供等价的 backend diagnostics sink、统一生命周期事件和自动落盘能力后，移除该输出指针、pending event 缓冲和 Qm 事件映射。

### `CMakeLists.txt`

- 注册 Qm core、player indicator、诊断保留策略头文件和 C++ 测试源文件；不修改依赖版本和协议生成流程。
- 冲突风险：中。上游源文件清单可能新增条目；同步时按路径插入，不做全文件重排。
- 删除条件：对应 Qm 模块和测试删除，或改为独立目标。

### `src/engine/client.h` / `src/engine/client/client.cpp` / `src/game/client/gameclient.*`

- 增加两个默认空的配置生命周期触点：主配置加载期间转发未识别命令，主配置执行完成后通知 game client。Qm 迁移器只消费白名单中的旧 `tc_*` 指示器键，其他未知命令仍由官方配置管理器原样保存。
- 新键通过 console chain 记录是否在主配置中明确出现；只有对应新键未出现时才迁移旧键。迁移值复用官方 console 的类型、范围和颜色解析，不把旧配置头重新注册，也不让 engine client 依赖 Qm 业务类型。
- 默认行为：无旧键时新配置保持默认；新键优先；命令按官方 console 的引号、分号和注释规则截取为单条命令；可解析但越界的整数交给官方配置命令继续 clamp；无法解析、超限或捕获失败的旧命令由配置管理器按单条命令保留，不阻断启动且不推进迁移版本。成功迁移后由 `qm_config_migration_version` 标记完成。
- 冲突风险：中低。上游同步时只需重放两个 IGameClient 默认 hook 和配置加载后通知点；若官方提供配置迁移生命周期，应改为接入官方能力。
- 删除条件：所有已迁移功能完成旧配置淘汰、用户配置迁移窗口结束，或官方提供等价的兼容机制。

### `src/engine/shared/config_variables.h` / `src/game/client/components/menus.h` / `menus_settings.cpp`

- 在官方设置页列表末尾追加 `QmClient` 页，保留原有设置页枚举值和 `ui_settings_page` 持久化编号不变，并把该配置的合法上限从 `10` 扩展到 `11`；页面实现位于独立的 `qmclient/presentation/qm_legacy_settings.cpp`。
- 触点范围：旧 UI 导航、页面分发和页面索引范围，不改变官方已有页面内容、输入优先级或配置字段；Qm 页面直接绑定现有 `qm_` 配置，暂不复制 feature 业务状态。
- 冲突风险：中。上游新增设置页时需保持 Qm 页追加在末尾，并重新核对枚举与 tab 文本数量。
- 删除条件：Qm legacy presentation 被新旧 UI 统一桥接替代，或对应 Qm 设置全部移除。

### `src/game/client/components/qmclient/presentation/qm_legacy_settings.cpp`

- 提供当前已迁移的 diagnostics 开关和 player indicator 配置的旧 UI 入口；diagnostics 明确标注重启生效，player indicator 参数实时写入同一 `CConfig` 状态。
- 页面只负责 presentation，不读取 `CTClient`、不拥有重复 feature 状态，也不进入游戏热路径；颜色使用官方 `CMenus::DoLine_ColorPicker`。
- 删除条件：新旧 UI presentation 共用正式 card provider 且旧页面完成行为对照后移除。
