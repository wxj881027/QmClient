# 预测 / 网络 / 手感（反 ping、输入、相机）上游同步对照表

- 生成时间：本会话（只读分析，未修改仓库任何受版本控制文件）
- HEAD：`78ffa71b7a`（QmClient 自有分支，2026-09-22）
- 上游引用：`ddnet/master` = `a9ddb8eda5`（2026-09-22）
- 上次同步点（`git merge-base HEAD ddnet/master`）：`de609e845e`（2025-11-27，Fix scoreboard not opening while menu is active）

## 0. 关键前提：本仓库的同步方式不是 merge，而是「改写式摘取上游提交」

- `git merge-base --is-ancestor <上游hash> HEAD` 对窗口内几乎所有上游提交都返回 **false**，但大量提交的**内容已在 HEAD 中**：
  例：上游 `e333184d88`（Predict all sounds and particles）→ 本地同主题提交 `21a804f285`；上游 `c9e92cbac7`（predict weapons with preinput）→ 本地 `7059c1e308`；上游 `ef8008be88` → 本地 `7bbba5c594`；上游 `e32a383ea1` → 本地 `0c44ffcd8c`；上游 `55c47ad1f0` → 本地 `fce7fd39de`；上游 `e7aecb02cd` → 本地 `f7af9f74e9`；上游 `e333184d88`/`f71afae23a`/`8e11338a26` 亦同。
- 因此本表**以内容/marker 比对为准**，不以 git 祖先关系为准。
- 全仓（不限本簇）用「提交标题交集」估算：`de609e845e..ddnet/master` 共 1258 条非 merge 提交，其中 **788 条标题已出现在本地提交中**；本地内容已覆盖到约 **2026-07-07** 的上游提交，之后（2026-07-08 → 2026-09-22）基本未同步。
- 本簇（`src/game/client/prediction`、`gameclient.{cpp,h}`、`components/{items,players,camera,controls}.cpp`、`engine/client/client.{cpp,h}`）在窗口内共 180 条上游提交：**83 条内容已在本地，97 条缺失**。明细见同目录 `_cluster_map.txt`（含每条上游 hash、日期、本地对应提交 hash）。

## 1. 对照表

| # | 功能名 | QmClient 位置(文件) | 官方对应能力(提交 hash / changelog) | 判定建议 | 依据(一句话) | 手感/玩法语义 |
|---|---|---|---|---|---|---|
| 1 | 自有快速输入提前量预测（Fast / Saiko+，`qm_fast_input_mode`、`qm_saiko_plus_amount`） | `src/game/client/components/tclient/fast_practice.cpp`；`src/game/client/gameclient.cpp`（`EffectiveFastInputOffsetTicks`/`QmApplyFastInputOffset`/`GetFastInputPos`/`GetFastInputRenderAmountMs`）；`src/engine/client/client.cpp`（`UpdatePredictionMargin`） | 未找到对应提交（上游只有 preinput 通道：`c9e92cbac7` predict weapons with preinput、`ef8008be88` Fix preinputs with dummy copy on high margin，两者本地均已具备） | KEEP-FORK | 上游没有「提前预测/提前渲染自身输入」的对等机制，改写等于直接改本地手感基线。 | **是** |
| 2 | 自适应预测边距 `qm_auto_margin` | `src/engine/client/client.cpp`（`ResetAutoPredictionMargin`/`UpdatePredictionMargin`/`QmComputeAutoPredictionMargin`） | `cl_prediction_margin`（基线已有）；`37b73680e9` Separate prediction margin graphs for main and dummy connection（未合入） | KEEP-FORK（可借鉴 37b73680e9 的按连接采样） | 本地是按实测 ping/抖动自适应算力度的自有算法，上游只有固定边距与图表拆分。 | **是** |
| 3 | 预测/网络诊断 HUD 全自有化（`dbg_graphs` 被接管） | `src/engine/client/client.cpp` `CClient::RenderGraphs()` → `CGameClient::RenderQmMonitoringHud()`；`src/game/client/components/qmclient/monitoring/monitoring.cpp`（预测超前、预测抖动、game time margin 历史曲线） | `37b73680e9`（上游把 `m_InputtimeMarginGraph` 拆为 `m_aInputtimeMarginGraphs[NUM_DUMMIES]`）；另有 `cl_showpred` | KEEP-FORK | 本地已整体替换上游图表栈，上游拆分对本仓库不适用；若要支持分身需在 Qm 监测里按连接采样。 | 否 |
| 4 | 预测事件（音效/粒子）基础设施 + 本地锤击去重 | `src/game/client/prediction/gameworld.{h,cpp}`（`m_PredictedEvents`、`CreatePredictedSound`、`QmCheckPredictedHammerHitHandled`）；`src/game/client/gameclient.cpp` | `e333184d88`（本地 `21a804f285` 已收）、`f71afae23a`（本地 `7f631d41a4`）、`8e11338a26`（本地 `44ee59d433`）、`6e2745d89b`/本地 `58cc8e792f`（predict envelope animations）均已具备 | KEEP-FORK | 上游版本已并入，本地只多一层预测事件去重扩展，无功能缺口。 | 否（音画） |
| 5 | 动态 antiping 玩家预测（`cl_antiping_players` 2/3） | 本地上限 `1`：`src/engine/shared/config_variables.h:21`；`src/game/client/gameclient.h:1030` `AntiPingPlayers()` | `4af8164f26` Add dynamic antiping player prediction (closes #1712) + `b2dc98ca29` Fix review for antiping players（**均未合入**） | TAKE-UPSTREAM | 本地仍是「始终激进预测他人」，上游新增「仅交互 / 含冻结」三态可显著减少假预测与误判。 | **是** |
| 6 | `AntiPing*` 的 FastPractice 强制预测覆盖 | `src/game/client/gameclient.h:1030-1033`（`m_FastPractice.ForcePredictPlayers/Grenade/Weapons/Gunfire`） | `4af8164f26` 把 `AntiPingPlayers()` 由 `bool` 改为 `int`（未合入） | MERGE-BOTH | 若吸收 4af8164f26，必须把 ForcePredict* 覆盖重排到新的三态语义上，否则练习模式会与动态 antiping 打架。 | **是** |
| 7 | 激光门（door）预测、门长度与碰撞 | `src/game/client/prediction/gameworld.cpp`（`NETOBJTYPE_LASER` 统一 gate 在 `m_PredictWeapons`）；`src/game/client/prediction/entities/door.cpp`（旧实现：switch 优先 + snapshot 端点） | `ac566d06f5` Predict laser doors without antiping（未合入）+ `9d8a20bd0d` Bound predicted door length to the map（未合入） | TAKE-UPSTREAM | 本地在 `cl_antiping_weapons 0` 时完全不预测门碰撞，会直接穿门；上游改为按 `m_PredictTiles` 且门长按地图推导。 | **是（碰撞/玩法）** |
| 8 | 预测 tune zone / 预测角色初始化 | `src/game/client/prediction/entities/character.cpp`（预测构造缺 `m_TuneZone = 0`） | `8edc5a3c21` prediction: Initialize tune zone settings + `7820407734` Revert "Revert ..."（**均未合入**） | TAKE-UPSTREAM | 上游修的是 tune zone 未初始化导致按错误调参预测；本地缺该初始化。 | **是（调参）** |
| 9 | 预测角色其余未初始化成员 | `src/game/client/prediction/entities/character.cpp` | `c2a0621bb1` client: More pred character initializations（未合入） | TAKE-UPSTREAM | 同族初始化补丁，本地只收到 `7820407734` 之前的其它初始化项。 | **是（潜在）** |
| 10 | `CLaser`/`CPlasma` 的 `m_Layer`/`m_Number` 未初始化 | `src/game/client/prediction/entities/laser.cpp`、`plasma.cpp`（继承自 `entity.h` 的 `m_Number`/`m_Layer` 未赋值） | `d62e5de553` Fix `CLaser`/`CPlasma` `m_Layer`/`m_Number` being uninitialized（未合入） | TAKE-UPSTREAM | switch 层归属字段未初始化会让激光/粒子的开关层预测错乱（上游已修）。 | 否（潜在错层） |
| 11 | 钩子提示线候选名单 / 空间索引 / 相交几何（Qm 自有优化） | `src/game/client/components/qmclient/qm_hook_coll_candidates.h`、`qm_hook_coll_spatial_index.h`、`qm_hook_coll_intersection.h`；`gameclient.cpp`（`UpdateHookCollTargets`/`IntersectCharacter`，`gameclient.h:1016-1017`） | `fabf09a3a3` clip hook collision line if there are no telehooks inside the map（未合入）、`061242fe47` improve hook collision line render check（未合入） | KEEP-FORK | 本地是纯性能重构，注释与实现都声明不改变队伍/solo/super 资格与命中者选择；上游改动的是「是否绘制」判定，两者可共存。 | 否（视觉） |
| 12 | 提示线视口裁剪与 telehook 探测 | `src/game/client/components/qmclient/qm_hook_coll_visibility.h`（`OnMapLoad` 扫 `CTeleTile` + `MayReachView`）；`players.cpp` `RenderHookCollLine` 用 `Graphics()->GetScreen` | `569edee60b` add CScreenRect（未合入）、`fabf09a3a3`、`061242fe47`（均未合入）；上游已收敛为 `Collision()->HasHookTeleIns()`（源头 `7e42356fe8` detect hook teleins、`1ed3568f5b` fix old telehooks） | MERGE-BOTH | 目标一致（远离视口的提示线不模拟），但机制不同：上游改用 `CScreenRect`+`MaxHookReach`，本地用自持 telehook 探测与带线宽 padding 的保守半径。 | 否（视觉） |
| 13 | 提示线 tip（末段几何 + 自定义颜色/透明度） | `src/engine/shared/config_variables.h:771` `cl_hook_coll_tip_color`；`players.cpp:432-468,584-601`（`HookTipLineSegment`、`m_TcRevertHookLine` 开关） | `d335860842` add hook line tip（本地 `80f60261cf` 已收）、`c50b26448c` hooklinetip custom color and alpha（按标题未合入，但配置与使用已在本地） | KEEP-FORK | 能力已具备，仅描述文案与上游不同（上游 "Specifies the color of the hookline tip"）。 | 否（视觉） |
| 14 | 快照实体扩展（EntityEx）关联与暂存复用 | `src/game/client/components/qmclient/snapshot_entities.h`（`QmAttachSnapshotEntityExtensions`）；`gameclient.cpp` 快照处理 | `8cd2412f75` Define `CSnapshotBuffer`（本地 `192c353910` 已收）；`be3e5e6a3c` Size the snapshot delta buffers correctly（未合入）；`51131ae86e` Revert "Rewrite snapshot building and delta in Rust"（未合入） | KEEP-FORK | 本地是渲染/关联侧复用缓存的优化，与上游快照缓冲重构不同层；不构成缺口。 | 否 |
| 15 | 网络缓冲正确性（快照 delta 容量） | `src/engine/client/client.cpp`/`client.h` 快照缓冲路径（本地沿基线实现） | `be3e5e6a3c` Size the snapshot delta buffers correctly（未合入） | TAKE-UPSTREAM | 上游是缓冲区尺寸正确性修复，属独立小补丁，与本地自有逻辑无冲突。 | 否 |
| 16 | 输入路径：分身输入数据保存 + 输入提前处理 | `src/game/client/components/controls.cpp` `SnapInput`（本地已写 `m_aInputData[!g_Config.m_ClDummy] = *pDummyInput`，且出现两处） | `5d05431b81` Fix input data not being stored with cl_dummy_control 1（**本地等效已具备**）；`178da1eade` client: Handle input earlier（**未合入**，把 `OnUpdate` 移到输入采样之前） | TAKE-UPSTREAM（仅 178da1eade） | 上游明言 60 刷新率下输入可提前约 16ms 发出，属直接的手感改善，本地尚未有等价实现。 | **是** |
| 17 | 分身锤输入重发（Qm 自有） | 本地 `3587aa2f7f`：`gameclient.cpp` `OnSnapInput` + `m_DummyHammerResends`、`gameclient.h` | 未找到对应提交（上游只有 `178da1eade` 改主循环顺序，与吞锤无关） | KEEP-FORK | 这是本地针对「非 vital 输入包丢失导致吞锤」的自有修复，上游无对等提交。 | **是（玩法）** |
| 18 | 相机：`qm_camera_drift*`、`qm_cinematic_camera`，以及 dyncam 修复 | `src/game/client/components/camera.cpp:371-390`（drift）、`:462`（cinematic） | 上游窗口内 camera.cpp 只有 `3541e1af8c`（花括号风格）、`b26985f15f`（`std::min/max` 替换）；另有 `c399e0f11b` Fixes for dyncam…（未合入） | KEEP-FORK（可单独吸收 c399e0f11b） | 上游无特效相机对等能力；c399e0f11b 的 dyncam 修复与本地 drift 分支无重叠。 | **是（视觉手感）** |
| 19 | 纯可视化预测：ninja 弹道预测、碰撞体胶囊绘制 | `src/game/client/components/qmclient/weapon_trajectory.{h,cpp}`（`QmWeaponTrajectoryNinjaEnabled`）、`players.cpp:930`；`collision_hitbox.{cpp,h}`、`collision_hitbox_logic.h` | 未找到对应提交（上游无 ninja 弹道预测与 hitbox 胶囊绘制） | KEEP-FORK | 均为纯绘制能力，不参与模拟与判定。 | 否 |
| 20 | Qm 配置覆盖状态机 / 模式联动；自有实时通道与卡顿诊断 | `src/game/client/components/qmclient/modes.{cpp,h}`（`ApplyQmConfigOverride`、`ShouldHideGoresGuide` 等）；`qm_realtime.{cpp,h}`；`perf_diagnostics.h`、`stutter_diagnostics.h` | 未找到对应提交 | KEEP-FORK | 上游无对等的配置覆盖/服务通道/诊断链路，且 `ShouldHideGoresGuide` 已被提示线渲染调用。 | 否 |

## 2. 手感 / 玩法语义敏感项（必须逐项人工确认）

共 **9 项**：第 1、2、5、6、7、8、9、16、17 行；其中第 18 行的特效相机属「视觉手感」，第 7、17 行触及碰撞与输入包语义，风险最高。

按风险从高到低：

1. 第 7 行 door 预测解耦（含 `ac566d06f5` + `9d8a20bd0d`）：会改变客户端的门碰撞预测，直接影响能否穿门判定。
2. 第 5/6 行 动态 antiping 玩家预测（`4af8164f26` + `b2dc98ca29`）：改变他人位置预测策略，且需要与 FastPractice 强制预测语义重新对齐。
3. 第 16 行 `178da1eade` 输入提前处理：改变输入发送时机（低刷新率下提前约 16ms），与本地 fast input 叠加后行为需实测。
4. 第 1/2 行 自有 fast input 与自适应边距：本地独有，是当前手感基线，不得被上游补丁顺手覆盖。
5. 第 8/9 行 预测初始化（tune zone 等）：改变预测所用调参，可能改变练习/实战的手感一致性。
6. 第 17 行 分身锤重发：本地自有的输入重发策略。
7. 第 18 行 相机 drift / cinematic / dyncam：视野跟随变化。

## 3. 不确定清单（判断不了，需人工或后续核验）

1. 第 11/12 行的钩子提示线裁剪：本地 `CQmHookCollVisibility::MayReachView`（Reach = HookLength+HookFireSpeed+1/128 修正+100+线宽 padding，且按 `sv_old_teleport_hook` 分 telehook 类型）与上游 `061242fe47` 的 `MaxHookReach` 判据是否在所有边界（贴屏边缘、极长钩、仅 TILE_TELEIN 的老图）等价——需要实测比对，不能仅凭阅读确认。
2. telehook 探测归属：本地自持一份扫描，上游已把探测搬进 `Collision::HasHookTeleIns()`；本仓库还有另一分支（`pr-153-head`，提交 `a7a46791ae`/`63644dbf90`，中文提交信息）实现了同名的 hook telein 识别但**不在 HEAD 上**，是否为并行在做的同名工作、是否应合并，无法从当前 HEAD 判断。
3. 「788 条标题已落入本地」意味着窗口内大部分上游提交是被改写摘取的，其**内容是否被本地二次改写**无法逐条核验；本表只对上述 20 行做了 marker 级核对。
4. `be3e5e6a3c`（快照 delta 缓冲容量）在上游依赖已被 revert 过的 Rust 快照重写（`a1e9d2b3c8` → `51131ae86e`）上下文，本地能否原样套用、是否已被本地自有实现覆盖，未验证。
5. `3665015161` Add 128 player support（2024-07-01，含 `protocol.h` 改动）在本地缺失，是否有意跳过、以及它对快照/输入路径是否存在隐含前提，未确认（本簇其它改动多依赖 `MAX_CLIENTS` 语义）。
6. `4af8164f26` 的三态语义与本地 `qm_fast_input_mode` / `cl_antiping_limit` / `cl_antiping_percent` 的叠加效果无法纸面判定，需要在实机上逐组合验证。
7. 上游 `1c312a8259`（服务器内存）、`a9c6d2efdf`/`2612f425f2`（gameinfo 队伍数）、`176b06eebf`（ddrace64 demo 兼容）、`c399e0f11b`（dyncam/`m_IsDDRace64`/KillTeam）等同时触及客户端与协议字段，是否属本仓库「协议默认不同步」范围，需按仓库范围边界单独决策。

## 4. 备注

- 本表所有 hash 均通过 `git log -1`/`git show` 实际解析，未编造；标「未合入」者指其内容在 HEAD 中不存在（已用 marker 比对，非仅凭祖先关系）。
- 明细映射文件：`tmp/upstream-inventory/_cluster_map.txt`（制表符分隔：上游 hash、日期、PRESENT/MISSING + 本地对应提交、上游标题）。
- 本次分析全程只读：未做 checkout/commit/merge/stash，未改动任何受版本控制文件；仅新建 `tmp/upstream-inventory/` 下的产物。
