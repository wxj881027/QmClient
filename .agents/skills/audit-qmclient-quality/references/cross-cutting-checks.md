# 横向风险检查参考

仅检查当前范围相关的条目；推荐入口需核对当前代码。授权、并行与验证遵循父 skill，不因命中标题扩展任务。

## UI、HUD 与文本布局

推荐入口：`src/game/client/components/menus*.cpp`、HUD 组件、`src/game/client/QmUi/`、文本渲染和 UI 测试。

```text
专项扫描 UI、HUD、菜单与文本布局：
- 4:3、16:9、21:9、超宽屏、720p、4K、高 DPI 和非整数 UI scale；
- 中英日韩、长翻译、超长昵称/Clan/服务器名下的截断、重叠和错误省略；
- TextWidth、换行、基线、图标字体与最终渲染是否一致；
- 卡片、列表、下拉框、弹窗和 tooltip 的裁剪、越界与滚动跟随；
- 嵌套滚动的 wheel owner、拖拽取消、焦点和命中区域；
- 单列/双列响应式顺序、卡片测量高度、搜索跳转和目标 reveal；
- 低帧率、首次 glyph、资源预热和窗口动态变化时的布局稳定性。
```

## 交互帧、预热与调度

推荐入口：菜单页面、`settings_*`、`CSectionLoader`、frame scheduler、Demo metadata 调度和性能测试。

```text
专项扫描打开页面、切 tab、滚动、筛选和首次显示时的交互帧：
- prewarm、render-only 和正常渲染是否错误共享业务写入；
- placeholder 与真实内容高度是否一致；
- 不可见 section/列表行是否仍处理，dirty 是否过宽或过窄；
- scheduler token、backlog、consumer 和 stop reason 是否真实生效；
- decode/upload/merge/publish 是否在切页帧集中 drain；
- 预热结果是否过期、污染状态或让首帧与稳定帧行为不同；
- 是否有同路径 p50/p95/p99、max、spike count 和 work_drain 证据。

不要把 FBO 当作默认路线；先归因，再建议不做、少做、分帧做或异步做。
```

## 缓存、资源与生命周期

推荐入口：`src/game/client/QmUi/`、QmClient 组件、Assets 设置、纹理/文本/预览缓存和测试。

```text
专项扫描缓存、纹理、文本容器、UI element、预览资源和跨帧状态：
- 裸指针、引用、string_view 和 vector 元素地址是否跨帧失效；
- cache key 或失效机制是否覆盖实际影响缓存结果的语言、UI scale、renderer、资源版本和配置，避免要求无关字段；
- hit/miss/disabled/corrupt/evicted/restore 是否都有安全 fallback；
- 切页、切服、切图、语言变化和图形设备重建后的失效与释放；
- 当前可见资源是否被错误驱逐，内存/显存是否 double count；
- 注册表、回调、纹理句柄和 text container 是否成对解除；
- 命中率是否真的代表减少工作，而不是统计口径自循环。
```

## Jobs、线程与主线程发布

推荐入口：资源 jobs、皮肤加载、异步设备采样、HTTP/翻译/语音模块和 jobs/thread 测试。

```text
画出 request -> worker -> result queue -> owner-thread publish -> consumer（图形命令继续追到后端执行线程） 的真实路径：
- 业务/UI 状态是否由所属线程修改，GPU 命令是否经合法提交路径在后端所属线程执行；不要将 Graphics thread 当普通资源 worker；
- request key、去重、优先级和取消条件是否完整；
- 旧 job 是否覆盖新请求，发布前是否检查 generation/version/owner；
- 锁是否进入渲染或音频热路径，是否有竞态、死锁或锁内重活；
- drain 是否受 count/bytes/duration 预算约束；
- 队列满、失败、取消和过期结果是否保留 live/source fallback。

不要把“加锁”当作默认修复；必须说明共享状态 owner 和触发时序。
```

## 性能量化与观测真实性

推荐入口：`src/game/client/components/qmclient/monitoring/`、`perf_logging.h`、`qmclient_scripts/perf/` 和监控合同测试。

```text
把性能量化系统作为生产诊断系统审查：
- event/page/tab/dur_ms/count/bytes/stop 的单位和缺省语义；
- page_switch 是否被错误计入耗时归因；
- 空数据、缺字段和畸形行是否被伪装成 0、100% 或优秀结论；
- p95/p99、样本标准差、阈值、样本量和采样偏差是否正确；
- KPI、文字结论、图表和 verdict 是否共享阈值；
- 未命中日志阈值时是否仍构造昂贵 payload；
- 自动基线是否被误写为严格 A/B 回归；
- C++ 与 TypeScript 字段是否有跨语言合同测试。
```

## i18n 与文本生成链

推荐入口：`qmclient_scripts/languages_qmclient/`、维护源 TOML、draft、`data/languages/` 和本轮本地化调用点。

```text
专项扫描源码 key -> active key -> 维护源 TOML -> 生成产物 -> 运行时加载；模型补译是 draft -> 审核 -> TOML 的可选分支：
- Localize、Localizable 和 Register help 文本是否正确提取；
- UI 文案、业务数据、服务器透传和玩家输入是否正确分类；
- 占位符、%%、换行、转义和 UTF-8 合同是否保持；
- active key、维护源、draft 和生成产物是否漂移；
- 生成顺序、重复项、unused 和历史译法是否可审计；
- 语言切换后文本缓存和布局是否失效；
- 英文 fallback 和缺翻译状态是否可见；
- 脚本是否重写、重排或污染未审核条目。
```

## 皮肤、Assets 与预览管线

推荐入口：skins、`menus_settings_assets.cpp`、`settings_resource_preview.*`、资源注册表、迁移和预览测试。

```text
专项扫描文件发现、解析、decode、GPU upload、注册、预览和游戏内应用：
- 排序/筛选是否每帧重建，是否只处理 visible range；
- preview scale、长宽比、裁剪、颜色和动画是否与实战一致；
- source/live/cache 路径是否产生不同结果；
- 同名资源、local-only、缺 metadata、删除和热重载；
- 页面切换或资源版本变化后旧 job 是否错误发布；
- 大图、批量资源和低显存环境的预算；
- 当前选择、默认资源和迁移是否丢失配置。
```

## HUD 通知、聊天与外部文本

推荐入口：`hud_notifications/`、聊天组件、好友通知、广播、规则目录和相关测试。

```text
专项扫描通知分类、规则优先级、触发、去重、布局和生命周期：
- must_i18n、business_data、服务器透传和玩家输入是否混淆；
- alias、白名单、黑名单和静态规则是否冲突；
- 同一 snapshot/event 是否重复触发，切服后是否残留；
- 超长 UTF-8、换行、控制字符和恶意文本；
- 首次出现新 glyph 是否同步建字形导致长帧；
- 通知堆叠、过期、动画、声音和聊天可见性是否一致；
- 上游静态规则与 QmClient 规则是否在同步后漂移。
```

## Server Browser 与网络状态

推荐入口：`src/engine/client/serverbrowser*`、菜单浏览器、ping cache、HTTP master、monitoring 和 TClient statusbar。

```text
专项扫描服务器列表、ping cache、HTTP 数据、筛选排序和网络状态展示：
- 刷新、取消、超时、重试和旧响应覆盖新请求；
- HTTP/LAN/ping cache 的合并与去重；
- 外部字段长度、UTF-8 和畸形响应；
- 大列表更新下的长帧、选择跳动和滚动稳定性；
- 断网、DNS 失败、空列表和部分成功状态；
- snapshot/prediction latency、jitter、packet loss 和速率的来源与单位；
- 本地化标签和极端数值下的状态栏宽度；
- 监控采样是否干扰网络或渲染线程。
```

## Demo、Ghost 与回放兼容

推荐入口：Demo、Ghost、`menus_demo.cpp`、`race_demo.*` 和 snapshot 测试。

```text
专项保护历史格式和确定性回放语义：
- 录制停止、切图、断线和异常退出时文件关闭；
- 旧版本与损坏 demo/ghost 的读取和安全失败；
- snapshot 翻译、tick、seek、pause、speed 和插值；
- 回放模式是否执行在线模式副作用；
- metadata/date 是否按预算读取；
- 重命名、删除、目录切换后选择索引是否失效；
- 路径、扩展名和超长文件名校验；
- 冲突解决是否削弱历史兼容测试。
```

## 语音与音频管线

推荐入口：`src/game/client/components/qmclient/voice/`、sound、地图声音、通知声音和 voice 测试。

```text
专项扫描语音捕获、缓冲、发送、播放和普通音频生命周期：
- callback、worker、网络和播放线程的 owner；
- 无设备、权限拒绝、热插拔和重初始化；
- ring buffer 溢出/欠载、格式转换和延迟；
- 静音、按键说话、失焦、切服和销毁后是否继续采集；
- 实时回调中是否有锁、分配、日志和格式化；
- 丢包、乱序、抖动和长时间无数据后的恢复；
- 音量与设备状态是否在模块间污染；
- 异常路径是否存在隐私风险。
```

## 更新、下载与外部输入安全

推荐入口：`update_version.*`、updater、HTTP、翻译/歌词源、脚本、文件导入导出和 storage。

```text
画出不可信输入从 source 到 parser、storage 和 runtime consumer 的路径：
- URL、重定向、TLS、版本、哈希/签名和来源；
- 临时文件、原子替换、失败回滚和文件占用；
- 文件名、相对路径、归档路径、符号链接和 storage root；
- 响应大小、超时、重试、压缩炸弹和畸形格式；
- API key、token、用户路径和聊天内容是否进入日志；
- 外部命令参数和 shell 转义；
- 取消、关机和断网后是否留下半成品；
- 离线和失败状态是否有可恢复 fallback。
```

## 上游同步与 QmClient 边界

推荐入口：目标 commit/diff、QmClient/QmUi/TClient 组件、配置、翻译、测试和同步文档。

```text
专项检查上游修复是否完整落地，同时保留 QmClient/TClient 定制：
- commit 父关系、merge commit 内实际补丁和依赖顺序；
- 上游调用链、测试意图及格式/协议边界；
- 冲突解决是否只做到编译通过；
- 本地配置、菜单、翻译、监控和测试是否被绕过；
- 是否重复移植本地已覆盖的修改；
- Cargo.lock、生成文件和子模块是否由正确工具更新；
- 官方 tag/版本是否真实存在；
- 测试是否被削弱以适应冲突结果。

逐项分类：直接移植、需适配、已覆盖、应跳过、需人工决定。
```

## 预测、快照与玩法兼容

推荐入口：`src/game/client/prediction/`、snapshot 翻译、实体、输入和对应测试。

```text
默认只报告风险，不授权修改核心玩法语义：
- client/predicted/snapshot tick 与 render time 是否混用；
- entity id、owner、copy/destroy 和 world iteration；
- projectile、laser、hook、dragger、door、tele、speedup、switch 和 tune；
- dummy、spectator、demo playback、六版协议和高延迟路径；
- 浮点/整数转换、迭代顺序和平台确定性；
- 输入计数、prediction reset 和 reconnect；
- 防御性校验是否拒绝合法旧地图/demo/snapshot；
- 测试是否表达对地图完成和玩家操作的真实影响。

分类：确定行为变化、潜在兼容风险、仅健壮性改进。
```

## 测试有效性与回归防护

推荐入口：当前 diff、`src/test/`、gate 和有效 spec/plan 的验收合同。

```text
专项判断测试是否保护玩家可见意图：
- 目标行为回归时测试是否真的失败；
- 结构测试与运行时测试的职责；
- 精确源码字符串断言是否过度脆弱；
- 修改测试后是否重建 testrunner；
- 测试集合是否与变更风险匹配，已有 gate 证据是否复用；
- merge 是否削弱父分支断言；
- 空数据、最大数据、非法索引、取消、重连和平台差异；
- 性能测试是否有固定场景、基线和样本可信度；
- build/test/quick/default/full gate 是否被正确表述。

分类：缺失测试、脆弱测试、错误测试、验证证据缺口。
```

## 文档、规格与实现漂移

推荐入口：当前有效 specs/plans、`.agents/skills/`（含 `audit-qmclient-quality` 与相关 skill）、实际代码和测试。

```text
先按日期、status、过时 banner 和 supersedes 关系确定权威文档：
- 文档声称未实现但代码已完成，或声称完成但仅有局部实现；
- 文件、函数、配置、命令和构建目录是否漂移；
- 多份有效文档是否决策冲突；
- build、focused test、gate 和全量回归是否混写；
- 视觉、跨平台、性能和已知 gap 是否明确；
- 实施步骤是否依赖未记录历史；
- 文档入口变化是否同步治理检查；
- 旧文档是否双向标记替代关系。

分类：当前有效、部分过时、已实现待回填、缺失实现、互相冲突。优先维护一个权威文档。
```

## 跨平台构建与发布

推荐入口：CMake、Rust bridge、平台代码、workflows、打包脚本、版本文件和 release note 工具。

```text
专项扫描 Windows、Linux、macOS、Android、iOS 和本次涉及的 renderer 的合并/发布风险：
- MSVC、Clang、GCC 的类型、warning、include 和链接差异；
- 32/64 位、结构布局、路径、文件锁和大小写敏感；
- OpenGL/OpenGL ES/Vulkan/Metal、HiDPI、窗口和图形设备重建；
- iOS 前后台切换、drawable 可用性、安全区域和软键盘；
- Rust/C++ bridge、Cargo.lock、绑定和 features；
- 子模块初始化和共享 build 目录串行约束；
- package_default、运行时资源、语言文件和 metadata；
- 版本、tag、docs/info.json、release note 和 workflow；
- 已验证平台与仅由静态检查推断的平台。

结论说明实际已验证平台和剩余发布风险，不把未验证平台视为通过。
```
