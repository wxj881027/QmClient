# QmLive 验收

本文合并自 `qm_live_client_test_plan.md` 与 `qmlive-acceptance.md`。

---

## 一、验收测试清单

# QmLiveClient 验收测试清单

## 自动化测试

1. 构建 `testrunner` 并运行 `QmLiveDirector.*`。
   - 证明直播导播优先把活跃 DDRace 队伍加入右侧队伍条。
   - 证明没有有效 DDRace 队伍时会按全局玩家列表生成玩家行，而不是按当前画面内角色生成。
   - 证明存在有效 DDRace 队伍时只生成队伍行，玩家列表只作为无队伍 fallback。
   - 证明 team 0 / super 不会误作为战队行显示。
   - 证明同队人数统计、fallback 玩家、随机选队和随机选人稳定。
2. 构建 `testrunner` 并运行 `QmLiveReplayBuffer.*`。
   - 证明回放缓存只保留最新帧。
   - 证明快照数据会被拷贝，不依赖外部临时缓冲生命周期。
   - 证明空数据、空指针和禁用缓存不会写入无效帧。

推荐命令：

```powershell
cmake --build build-live-check-ninja --target testrunner
.\build-live-check-ninja\testrunner.exe --gtest_filter=QmLive*
```

## 正常功能测试

1. 启动 `QmLiveServer`，确认 `sv_qm_live_observer 1`。
2. 用 `QmLiveClient` 连接服务器。
   - 客户端应进入直播 observer 状态，不占用玩家列表名额。
   - 主画面应显示多人同框观战，不显示排名、武器、右侧原 HUD、右下角“正在旁观”。
   - 左上角显示自由镜头状态提示，不作为可点击切换按钮。
   - 右侧显示“直播导播”队伍条。
3. 让至少两个 DDRace 队伍分别进入 team 1 / team 2。
   - 右侧队伍条应显示“队伍 1”“队伍 2”及人数。
   - 点击“队伍 1”后主画面继续按 multiview 同框观战该队。
   - 再次点击“队伍 1”应展开该队成员列表，成员列表包含该队全局玩家列表中的全部成员。
   - 点击展开后的某个成员，应单独观战该成员，但仍保留当前队伍选择。
   - 收起“队伍 1”时，应自动回到“队伍 1”的多人同框队伍观战。
   - 成员较多时，鼠标停在右侧导播面板上滚轮应能滚动成员列表。
   - 被选中的队伍正常渲染，其他有效 DDRace 队伍仍然可见且约为 10% 透明度。
   - 点击“队伍 2”后应切换到队伍 2，并保持同样的透明度规则。
4. 按住鼠标左键进入临时自由镜头。
   - 按下左键时鼠标切到自由镜头模式，移动鼠标可移动镜头。
   - 松开左键后按镜头中心寻找最近有效 DDRace 队伍。
   - 找到队伍后随机吸附到该队一名全局玩家列表中的成员，并恢复多人同框队伍观战。
   - 最近队伍瞬间为空时，应回退到当前有效队伍或直播导播 fallback，不应崩溃或吸附到无效玩家。
5. 确认左键输入门控。
   - 点击右侧队伍条只切换队伍，不进入临时自由镜头。
   - 菜单、聊天、控制台、旁观者弹窗、表情弹窗打开时，左键不触发临时自由镜头。
   - 左上角状态提示区域点击不切换自由镜头，也不误触其他游戏输入。
6. 打开 Esc 游戏菜单。
   - 不应看到 Connect Dummy / Disconnect Dummy 按钮。
   - 控制台执行 `dummy_connect` 时不应连接 dummy，并应写出直播导播禁用 dummy 的日志。
7. 用 `QmLiveClient` 连接普通 DDNet 服务端。
   - 服务端未 accept live observer 时客户端不应因 `live observer denied` 或 `no accept` 断开。
   - 客户端应继续普通连接流程并进入“直播导播”UI。
   - 服务端允许观众时，客户端应自动请求并进入 spectator。
   - 服务端拒绝 spectator 或观众位满时，客户端应保持连接并持续节流请求 spectator。
   - 未进入 spectator 前，移动、跳跃、开火、hook、换武器、kill、chat、dummy 和加入游戏队伍都不应发出有效玩法行为。
8. 连接无 DDRace team 数据的普通服务端。
   - 右侧“直播导播”面板应显示全局玩家列表，不应只显示当前画面内角色。
   - 点击玩家行应发送普通 spectator 跟随消息，并在本地切到该玩家视角。

## 实际使用测试

1. 直播端长时间挂在真实比赛服务器，至少 30 分钟。
   - 队伍加入、死亡、重生、换队后右侧队伍条持续更新。
   - 主视角不会跳到无关队伍，除非当前队伍消失并触发 fallback。
   - 选队观战时其他有效 DDRace 队伍始终保留低透明度显示。
2. 在比赛中发起投票换图并通过。
   - 客户端应自动下载/载入新地图。
   - 不需要退出到地图浏览器重新进入。
   - 换图后 native observer 或兼容导播模式都应保持，右侧队伍条或玩家列表能重新显示。
3. OBS/直播姬捕获 `QmLiveClient` 窗口。
   - 画面只包含游戏主画面、左上自由镜头状态提示、右侧直播导播条。
   - 无排名 HUD、武器 HUD、右下旁观文字和不必要英文文案。
4. 同时运行普通 `DDNet/QmClient` 与 `QmLiveClient`。
   - 普通客户端 HUD、聊天、dummy、队伍、投票行为不应被直播端改动影响。
   - 普通客户端仍可按原逻辑连接 dummy。
   - `QmLiveServer` 上普通玩家仍可正常投票换图、进入游戏、换队和连接 dummy。

---

## 二、验收标准


# QmLiveClient 验收手册

本文只记录自动化测试无法替代的人工验收。实现行为与回归断言以当前源码和 `src/test/qm_live_client_test.cpp` 为准。

## 自动化基线

Windows 最终验收运行完整 C++ 测试入口：

```powershell
qmclient_scripts/cmake-windows.cmd --build cmake-build-release --target run_cxx_tests -j 14
```

Linux/macOS：

```sh
cmake --build cmake-build-release --target run_cxx_tests -j 14
```

仅定位 QmLive 问题时可以运行过滤测试；过滤结果不能代替最终全量测试：

```powershell
cmake-build-release/testrunner.exe --gtest_filter=QmLive*
```

## 导播与兼容模式

1. 启动启用了 `sv_qm_live_observer 1` 的 `QmLiveServer`，再用 `QmLiveClient` 连接。
2. 确认客户端以只读 observer 进入，不占玩家名额；直播画面不显示普通排名、武器 HUD、右下旁观文字或 dummy 操作入口。
3. 打开控制台执行 `dummy_connect`，确认不会建立 dummy 连接，并出现 `Dummy connection is disabled for QmLive director.` 拒绝日志。
4. 准备至少两个有效 DDRace 队伍，检查队伍条人数、展开成员、单人跟随、多人同框、滚动和切队；未选队伍保持低透明度可见。
5. 按住左键移动临时自由镜头，松开后应吸附到镜头附近的有效队伍。队伍瞬间失效时应稳定回退，不选择无效玩家。
6. 在菜单、聊天、控制台、旁观者弹窗和表情弹窗打开时点击，确认不会误触自由镜头；点击导播面板只执行面板操作。
7. 连接普通 DDNet 服务端，确认服务端不支持 live observer 时仍保持连接并进入兼容导播；进入 spectator 前不得发出移动、武器、聊天、kill、dummy 或加入队伍等玩法输入。
8. 连接没有 DDRace team 数据的服务端，确认导播回退到全局玩家列表，点击玩家可按普通 spectator 流程跟随。

## 录制与回放

1. 使用 `qm_live_match_record_start` / `qm_live_match_record_stop` 完成一段比赛录制，确认 `.demo` 写入 `demos/qm_live/matches/` 且可由标准 demo 播放器打开。
2. 播放录制内容并执行 seek/restart，确认完成排名和队伍上下文按当前时间重建，不残留跳转前状态。
3. 使用 `qm_live_team_filter <team>` 和 `qm_live_team_filter_off` 切换单队预览，确认画面、音效和完成提示遵循当前过滤配置；关闭过滤后恢复完整比赛。
4. 缺失、损坏或与 demo 不匹配的 `.qmlive.json` sidecar 不应阻止标准 demo 播放。

## 长时间与直播输出

1. 在真实比赛服务器连续运行至少 30 分钟，覆盖加入、死亡、重生、换队和当前队伍消失；队伍条持续更新，主视角不无故跳队。
2. 比赛中通过换图投票，确认新地图自动下载/载入，换图后 observer/兼容导播状态和队伍或玩家列表恢复。
3. 用 OBS/直播姬捕获窗口，确认输出只包含预期的比赛画面、自由镜头状态和导播 UI，无普通客户端 HUD 或无关文案。
4. 同时运行普通 QmClient 与 QmLiveClient，确认普通客户端和 `QmLiveServer` 上的普通玩家仍可正常使用 HUD、聊天、dummy、投票、换队和玩法输入。

人工验收结果应记录环境、服务端类型、持续时间和失败截图/日志；未执行的项目必须作为 gap 汇报。
