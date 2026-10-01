# 功能场景检查参考

仅检查当前范围相关的条目；推荐入口需核对当前代码。授权、并行与验证遵循父 skill，不因命中标题扩展任务。

## Gores、自动切锤与快速输入

推荐入口：`CGameClient` 的 fast-input 路径、`controls.cpp`、TClient/Gores 状态、QmClient Gores 设置和相关测试。

```text
玩家场景：进入 Gores 模式，在 main/dummy 间切换，拾取武器、死亡、切图、断线重连，并切换自动武器与快速输入配置。

验证以下假设：
- 自动启用/关闭没有在离开 Gores、切图或断线时恢复原状态；
- main 与 dummy 的输入、weapon、fire counter 或 wanted weapon 被交叉污染；
- 自动切锤与快速输入同时生效时产生重复 fire/hook 边沿；
- 配置中途关闭后仍保留旧 action 或预测偏移；
- spectator/demo/offline 路径仍执行在线输入副作用；
- 高频 tick 路径重复查找配置、绑定或构造状态。

C++ 优先覆盖：模式状态机、输入合并、fire/hook 边沿、配置组合、reset/reconnect、main/dummy 参数矩阵。
游戏内验证：高延迟下手感、真实地图可达性和视觉引导线。
```

## 武器轨迹、激光、钩索与绘制顺序

推荐入口：`weapon_trajectory.*`、players/items/effects 渲染、激光与钩索预览、Appearance 设置和菜单分支测试。

```text
玩家场景：使用 grenade/laser/shotgun/hook，切换预测、观察者、dummy、透明度和轨迹显示模式。

验证以下假设：
- prev/current character、owner id 或预测 tick 使用错误；
- 轨迹计算与 DDNet 实际武器/调参语义不一致；
- invalid owner、spectator、demo 或非本地玩家错误显示；
- alpha、颜色、线宽、端点圆角和 weapon body 绘制顺序不一致；
- clip/map screen/texture state 未恢复，污染后续渲染；
- 极端 zoom、纵横比和边界坐标导致 NaN、越界或大循环。

C++ 优先覆盖：显示 gating、owner/scope、轨迹采样边界、绘制调用顺序、配置 clamp、无效 snapshot。
游戏内验证：真实轨迹吻合、端点遮挡、不同 renderer 的线宽与抗锯齿。
```

## 音乐歌词、媒体时钟与歌词源

推荐入口：`src/game/client/components/qmclient/music_lyrics/`、`music_lyrics_integration.*`、`qm_soda_lyric_file.*`、`src/test/music_lyrics_krc_test.cpp` 和 `src/test/music_lyrics_qrc_test.cpp`。

```text
玩家场景：播放、暂停、seek、切歌、无 metadata、离线和歌词源超时；按当前解析器覆盖 LRC、KRC、QRC 等已支持格式；其他格式和来源仅在实际实现时检查。

验证以下假设：
- media clock 在 pause/seek/rate change 后漂移；
- 同时间戳、多行、空行、负 offset 和超长歌词解析错误；
- 旧请求在切歌后覆盖新歌词；
- cache key 未包含歌曲身份、source、版本或解析选项；
- 多来源匹配排序不稳定或错误选择低置信度结果；
- 网络失败时覆盖已有可用歌词；
- karaoke 分段与普通行渲染在边界时间跳动。

C++ 优先覆盖：parser、clock、match/ranking、cache key、source fallback、请求 generation、渲染时间片选择。
人工验证：字体排版、卡拉 OK 动画节奏和真实媒体播放器集成。
```

## QmLive 历史场景（仅恢复或移植该功能时）

当前源码未检出 QmLive/LiveObserver 与 `qm_live_client_test.cpp`，以下仅为历史功能的候选场景，不代表现有实现。仅在用户要求恢复或移植且已定位真实代码时采用；普通观察者/Demo 审查使用多状态与回放参考。

```text
玩家场景：在线 Live Observer、普通 observer、QmLive demo 和正常游戏之间切换；选择队伍、自由视角、手动跟随、seek 和回放结束。

验证以下假设：
- presentation mode 切换未完整保存/恢复 observer 状态；
- sidecar 缺失、损坏或不匹配时仍污染普通 demo；
- seek 回退后 team/finish event 只增量应用，没有重建；
- follow client 离线或换队后索引失效；
- manual follow、director 自动选择和 team filter 相互覆盖；
- overlay 抢占聊天、鼠标模式或普通 spectator 输入；
- 录制停止、异常退出或切图留下不完整 sidecar。

C++ 优先覆盖：presentation 状态机、save/restore/reset、sidecar 校验、seek 重建、team selection、输入 gating。
游戏内验证：完整比赛流程、复杂队伍变化、鼠标与聊天体验。
```

## 脚本系统与命令边界

推荐入口：`scripting.*`、`scripting/impl.*`、console/config/storage 接口和外部输入安全专项。

```text
玩家场景：加载、执行、重载、禁用和删除脚本；脚本报错、超时或访问已经销毁的客户端状态。

验证以下假设：
- 脚本路径或 include 可越过 storage root；
- 脚本在客户端未初始化、切服或销毁后持有失效接口；
- 单个脚本异常影响其他脚本或主循环；
- 回调未注销，重载后重复触发；
- 命令参数、配置或聊天数据未经边界处理；
- 每帧脚本无预算，造成长帧；
- 权限边界无法区分只读查询和有副作用操作。

C++ 优先覆盖：路径校验、注册/注销、生命周期、错误隔离、命令参数和预算决策。
运行时验证：真实解释器行为、恶意脚本、长时间运行与资源消耗。
```

## IME、文本输入与剪贴板

推荐入口：IME 平台实现、`lineinput.*`、UI 输入框、clipboard 调用和 `qm_ime_platform_test.cpp`。

```text
玩家场景：中文 composition、候选翻页、提交/取消、窗口失焦、切换输入框、复制粘贴长 UTF-8 文本。

验证以下假设：
- UTF-16/UTF-8 offset、candidate count 或 page size 越界；
- composition 在失焦、关闭菜单或切换输入框后残留；
- 系统候选窗与自绘候选窗同时显示；
- selection/cursor 使用字节索引和 codepoint 索引混算；
- clipboard 空值、超长文本、换行和非法 UTF-8 未处理；
- password/secret 输入被复制、记录或显示；
- 平台不支持路径错误退化为无输入。

C++ 优先覆盖：编码转换、offset clamp、candidate state、focus lifecycle、paste sanitization 和平台策略选择。
人工验证：Windows/macOS/Linux 原生 IME、系统候选窗位置和剪贴板集成。
```

## Android/iOS、触控与软键盘

推荐入口：`touch_controls.*`、`menus_ingame_touch_controls.*`、输入系统、`src/ios/ios_main.cpp` 和 `src/test/ios_runtime_test.cpp`。

```text
玩家场景：多指移动/瞄准/开火/钩索，打开编辑器移动按钮，旋转屏幕，呼出软键盘，应用或取消未保存设置。

验证以下假设：
- finger id 复用、抬起或系统取消后 action 卡住；
- 多指同时操作时同一 action owner 被覆盖；
- safe area、纵横比和旋转后按钮越界或重叠；
- editor 的 cache/save/reset 与真实按钮指针失效；
- 切菜单、失焦和暂停后输入状态未清理；
- 软键盘遮挡输入框或改变 viewport 后布局未重算；
- 不可见按钮仍接收触摸。

C++ 优先覆盖：finger/action 状态机、几何 clamp、重叠检测、缓存应用/取消、visibility 和 reset。
设备验证：真实多点触控、软键盘、安全区域、旋转和不同刷新率。
```

## 图形后端、Shader 与设备重建

推荐入口：`src/engine/client/backend/`、`graphics_threaded.*`、shader 资源、OpenGL/OpenGL ES/Vulkan/Metal 初始化与恢复，及 `src/test/metal_*test.cpp`。

```text
玩家场景：首次启动、切 renderer、切全屏/窗口、调整分辨率、设备丢失、驱动不支持和安全启动回退。

验证以下假设：
- shader 缺失、损坏或版本不匹配时错误继续初始化；
- pipeline/shader cache key 与设备、驱动或渲染状态不匹配；
- swapchain/窗口重建遗漏纹理、buffer、clip 或 screen state；
- backend fallback 保存了不可再次启动的配置；
- size/format 计算溢出导致分配错误；
- render thread 与主线程之间资源销毁顺序错误；
- 当前受影响的 OpenGL/OpenGL ES/Vulkan/Metal 路径产生不同玩家可见语义。

C++ 优先覆盖：格式/尺寸计算、配置 fallback、能力选择、错误分类和纯 cache-key 逻辑。
运行时验证：真实 GPU/驱动、设备重建、shader/pipeline 创建和画面一致性。
```

## 崩溃、卡死与 Debug Bundle

推荐入口：client hang report、日志、monitoring snapshot、debug bundle、storage 和 observability 文档。

```text
玩家场景：主线程长时间无响应、后台线程死锁、崩溃、磁盘满、报告写入失败和用户提交诊断包。

验证以下假设：
- watchdog 把正常长加载误判为 hang；
- dump/report 路径递归触发分配、锁或崩溃；
- 多线程同时写报告导致损坏；
- API key、服务器密码、聊天、玩家路径或个人信息未脱敏；
- 磁盘满/无权限时覆盖原有日志或再次卡死；
- 报告缺少版本、commit、平台、renderer、操作和最近事件；
- debug bundle 收集无边界文件或体积无限增长。

C++ 优先覆盖：路径、字段合同、脱敏、大小限制、fallback 和纯 watchdog 判定。
运行时验证：真实 hang/dump、崩溃处理器、磁盘/权限错误和操作系统限制。
```

## 配置持久化、默认值与迁移

推荐入口：QmClient 配置头、console chains、`settings_runtime_cache.*`、版本迁移、设置 UI 和配置测试。

```text
玩家场景：旧版本升级、非法手改配置、运行时切换、重启、恢复默认和配置文件只读。

验证以下假设：
- 默认值变化无迁移，意外启用新功能；
- UI clamp 只修显示，没有修运行时消费；
- config chain 在接口未初始化时执行副作用；
- QmClient 配置错误使用 cl_ 前缀或与上游变量冲突；
- runtime cache 与持久配置版本/renderer/UI scale 不匹配；
- 多处 owner 写同一配置，保存顺序导致回退；
- 非法枚举、负值、极端数值进入索引、分配或渲染。

C++ 优先覆盖：默认值、迁移表、clamp、枚举 canonicalize、chain 初始化阶段、runtime cache key 和序列化往返。
人工验证：真实旧配置升级、只读文件和跨版本回退。
```

## Main、Dummy、Spectator 与 Demo 多状态

推荐入口：`gameclient.cpp`、controls、spectator、HUD/QmClient 功能 gating、Demo playback 和 `qm_modes_test.cpp`。

```text
对同一功能建立状态矩阵：offline、online main、online dummy、spectator、demo playback；QmLive demo 仅在对应实现存在时纳入。

验证以下假设：
- 读取 local character 时未区分 main/dummy 或不存在的 local id；
- spectator/demo 路径错误发送网络、输入或配置副作用；
- main/dummy 切换后缓存、HUD、skin、weapon 或 follow target 仍引用旧对象；
- scope gating 只隐藏 UI，没有阻止业务行为；
- reset/reconnect 没有清空模式特有状态；
- preview 使用真实游戏状态并污染运行时；
- 同一测试只覆盖 online main，遗漏其他模式。

C++ 优先覆盖：状态矩阵、scope predicate、无 local player、切换/reset、side-effect gating 和 fallback。
游戏内验证：真实 dummy 操作、观察者切换和 demo seek。
```
