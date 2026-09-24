# 上游同步对照表：渲染 / 视觉 / 性能

- 工作目录：`D:\QmClient_code\QmClient`（分析过程严格只读，未修改任何仓库文件）
- HEAD（QmClient 自有分支）：`78ffa71b7a` — `fix(qmclient): 修复皮肤 PNG 泄漏与 Axiom 缓存/状态语义`（2026-09-22）
- 上游引用：`ddnet/master` = `a9ddb8eda5` — `UI: Rework scoreboard cursor hint (#12903)`（2026-09-22）
- 上次同步点（merge-base）：`de609e845e` — `Fix scoreboard not opening while menu is active (#11324)`（2025-11-27）
- 该区间内 `src/game/client`、`src/engine/client/backend`、`src/engine/gfx` 共 **360** 个非合并提交；`de609e845e..ddnet/master` 全量 **2117** 个提交
- 上游 changelog 参考：[DDNet 20.0（2026-08-27）](https://ddnet.org/downloads/) 明确列出 "More FPS: Stop rendering off-screen entities, players, hooks, ghosts and spectator chars"、"More FPS: Cache scoreboard text containers"、"Reuse the text container buffer in CCachedText"、"Clip hook collision line on maps without telehooks"、"Fix projectile clipping being applied to start position instead of actual position" 等条目

判定口径：`KEEP-FORK` = 自有实现明显更丰富/更贴合需求，不引入上游；`TAKE-UPSTREAM` = 上游实现更好且不冲突，直接采纳；`MERGE-BOTH` = 两侧各有价值，需要手工融合；`DEFER` = 有价值但依赖大改动或收益不明，延后。

---

## 对照表

| # | 功能名 | QmClient 位置 | 官方对应能力（提交 / changelog） | 判定 | 依据 |
|---|---|---|---|---|---|
| 1 | 屏幕外实体剔除（玩家 / 钩子 / 观战角色） | `src/game/client/components/players.cpp:680`（仅 Hook 的 AABB 预筛 + `BorderBuffer=100.0f`）、`players.cpp:1971`（`OnRender` 里扩大后的屏幕边界） | `14fc1e9d1e` clip rendering of entitties to view screen；changelog 20.0 "More FPS: Stop rendering off-screen entities, players, hooks, ghosts and spectator chars" | MERGE-BOTH | 自有版本只覆盖钩子，上游把玩家本体、观战角色也纳入同一套裁剪；保留自有注释/缓冲语义，补上玩家与观战角色分支 |
| 2 | 投影物 / 激光 / 拾取物屏幕裁剪 | `src/game/client/components/items.cpp`（无 `GetScreen` 调用，无裁剪） | `14fc1e9d1e`（含 `items.cpp`、`laser_data.*`、`projectile_data.*`）、`c4cb37a356` fix projectile clip being applied to start position instead of actual position | MERGE-BOTH | 自有 `items.cpp` 已重写为增强激光（13 层辉光、脉冲），`git diff` 偏离上游 228/107 行；需在上游 `CScreenRect` 裁剪框架内保留自有激光绘制 |
| 3 | 钩子碰撞线裁剪与渲染前置检查 | `src/game/client/components/players.cpp`（`RenderHookCollLine`，无屏幕矩形早退） | `fabf09a3a3` clip hook collision line if there are no telehooks inside the map、`061242fe47` improve hook collision line render check | TAKE-UPSTREAM | 纯性能优化 + 无 telehook 地图的正确性修复，与自有实现无功能重叠；`Collision()->HasHookTeleIns()` 是新增依赖 |
| 4 | `CScreenRect` 统一屏幕边界抽象 | 无（仍用 `Graphics()->GetScreen(&x0,&y0,&x1,&y1)` 六处以上） | `569edee60b` add CScreenRect in order to simplify screen border handling；changelog 20.0 | DEFER | 是第 1/2/3 项的前置基础设施；改到自有 `players.cpp`/`items.cpp`/`nameplates.cpp`/`hud.cpp` 会与 1000+ 行自有改动正面冲突，建议随大版本融合而非单独摘 |
| 5 | 计分板文字容器跨帧缓存 | `src/game/client/components/scoreboard.cpp` / `scoreboard.h`（`git grep STextContainerIndex` 无命中，未使用） | `5b10b49e16` Keep scoreboard text in text containers across frames（`CCachedText` 落在 `src/game/client/ui.h:311`）；changelog 20.0 "Cache scoreboard text containers" / "Reuse the text container buffer in CCachedText" | MERGE-BOTH | 上游把复用能力下沉到 `CCachedText` 公共设施，自有侧已有同类思路（`nameplate_text_cache.h` 的 `QmUpdateNameplateTextContainer`）但互相独立；建议引入 `CCachedText` 后让计分板与名牌共用 |
| 6 | 表情抖动动画收尾 | `src/game/client/components/players.cpp:1374`（仍为 `std::sin(5 * Wiggle)`） | `1cfa7f927a` Fix emoticon wiggle animation not ending smoothly（改为 `std::sin(2 * pi * Wiggle)`） | TAKE-UPSTREAM | 逐字命中：merge-base 与 HEAD 都是旧公式，一行修复且零冲突 |
| 7 | 名牌 / 铭牌渲染与文字缓存 | `src/game/client/components/nameplates.cpp`（偏离上游 2296/195 行）、`src/game/client/components/qmclient/nameplate_layout.h`、`nameplate_text_cache.h` | `abfe01656c` client: Fix "Show own name plate"、`7177a219ac` Inline s_OutlineColor、`569edee60b`（`CScreenRect`）；自有测试 `src/test/qm_new_ui_menu_branch_test.cpp:2749` 明确拒绝上游写法 | KEEP-FORK | 自有实现有独立布局、LOD、缓存与测试契约；仅可按需吸收上游的小修复（`cl_nameplates_own` 与 `ShowDirection` 的联合早退条件），不能整体替换 |
| 8 | 皮肤描边（Tee 轮廓） | `src/game/client/components/qmclient/qm_skin_outline.h`、`skin_prepared_visuals.h:16`（圆盘膨胀 + 滑动最大值，纹理按宽度惰性重建） | 未找到对应提交（上游无 Tee 皮肤描边功能；`756dcc04f9` 是"隐形贴图替换为默认 sprite"，不等价） | KEEP-FORK | 自有独有视觉功能，上游无同类实现可比 |
| 9 | 轨迹带几何（Tee 拖尾） | `src/game/client/components/qmclient/trail_band_geometry.h`、`trail_band_section.h`、`src/game/client/components/tclient/qm_tee_trail.cpp`（511 行，6 种样式） | 未找到对应提交 | KEEP-FORK | 上游无拖尾带渲染，自有实现含斜接限宽与自交防护 |
| 10 | 轨迹颜色模式（彩虹 / 速度 / 随机 / Tee 本体色） | `src/game/client/components/tclient/trails.cpp:124-146` | 未找到对应提交（上游只有队伍/自定义色） | KEEP-FORK | 自有配置族（`tc_tee_trail_color_mode` 等）无上游对应 |
| 11 | 彩虹 / 脉冲 / 随机 Tee 颜色 | `src/game/client/components/tclient/rainbow.cpp` | 未找到对应提交 | KEEP-FORK | 上游 `CTeeRenderInfo::m_CustomColoredSkin` 只是承载机制，无动画逻辑 |
| 12 | Tee 循环色调（Hue Cycle，含 0.7 sixup 通道） | `src/game/client/components/qmclient/tee_hue_cycle.cpp:65`、调用点 `players.cpp:1262` | 未找到对应提交 | KEEP-FORK | 自有实现同时改写 `m_ColorBody/m_ColorFeet` 与 `m_aSixup` 配色，上游无等同能力 |
| 13 | 果冻 Tee（受击/落地形变） | `src/game/client/components/qmclient/jelly_tee.cpp`（弹簧 + 阻尼 + 夹取） | 未找到对应提交 | KEEP-FORK | 上游 Tee 渲染无几何形变能力 |
| 14 | 武器换弹 / 开火旋转动画 | `src/game/client/components/qmclient/weapon_animation.h`（7 关键帧），调用点 `players.cpp:1006` | 未找到对应提交（上游 `players.cpp` 只有 `ANIM_HAMMER_SWING` / `ANIM_NINJA_SWING` 状态机） | KEEP-FORK | 自有动画由 `m_PlayReloadAnimation` + 调参驱动，与上游状态机并存不冲突 |
| 15 | 贴图不可见资源的兜底替换 | 自有皮肤管线：`src/game/client/components/qmclient/skin_prepared_textures.h`、`skin_load_budget.h`（解码任务内准备、按 1 ms 预算分步上传） | `756dcc04f9` detect invisible assets on sprite texture loading and replace them with default sprite | DEFER | 两者目标不同（上游补"全透明贴图"兜底，自有做异步上传节流）；上游思路可作为自有皮肤解码的补充校验，但需先确认自有侧是否已覆盖 |
| 16 | 皮肤加载顺序与刷新崩溃 | `src/game/client/components/qmclient/skin_prepared_visuals.h`、`players.cpp` 中的 `IsTextureHandleAllocated` 每帧自愈 | `364c03c797` client: Fix crash when refreshing skins while skins are still loading、`03407db9e6` Fix skin metrics potentially not being reset before loading、`3e7e479de1`、`47007311e2` Validate 0.7 skin names, fix stack overflow | TAKE-UPSTREAM | 上游是崩溃/栈溢出类修复；自有侧已有句柄自愈，但 `m_SkinMetrics` 未重置这类问题仍需上游补丁 |
| 17 | 粒子系统的暂停与倍速 | `src/game/client/components/particles.cpp`（自有分支已在缓冲分支补上 `dbg_assert(LastQuadOffset >= FirstParticleOffset)`，等价 `b55657a02c`） | `b55657a02c` Fix OOB access to particle texture when there are no particles、`e333184d88` Predict all sounds and particles、`83e771720a` Add `CGameClient::GetAnimationPlaybackSpeed`、`2efd8cf5c4` Add `IsWorldPaused`/`IsDemoPlaybackPaused` | MERGE-BOTH | OOB 修复自有已达成；但 `GetAnimationPlaybackSpeed` / `IsWorldPaused` 抽象未落地（自有 `IsRenderingDummyMiniMap()`、`effects.cpp` 用自有时钟），统一到上游抽象可减少分叉 |
| 18 | 未预测阴影 Tee（自定义范围 + 透明度） | `src/engine/shared/config_variables.h:758-759`（`cl_unpredicted_shadow` 0-3、`cl_unpredicted_shadow_alpha`） | `fc54d62ed0` Update unpredicted shadow, add alpha and customization、`96d539cb41` Remove unpredicted shadow from debug mode, fix descriptions | TAKE-UPSTREAM | 自有已带同名同范围配置，但 `players.cpp` 的渲染条件仍是旧写法（`Local && ((m_Debug && ...) || == 1)`），未含上游的 `== 3 / 非本机 == 2` 语义与 `Debug` 解耦 |
| 19 | 钩子提示线（Hook Line Tip）配色与 alpha | `src/engine/shared/config_variables.h:771`（`cl_hook_coll_tip_color`，默认值 `2150367104` 与上游一致） | `d335860842` add hook line tip、`c50b26448c` hooklinetip custom color and alpha | MERGE-BOTH | 配置项已合并，但线路分段绘制（`HookTipLineSegment` 独立着色、`vLineSegments.clear()`）需与第 3 项的裁剪改动一并对齐 |
| 20 | 观战者 / 幽灵 / 自由视角绘制 | `src/game/client/components/players.cpp:2026` 附近（自有 `IsOtherTeam`、`m_SpecChar` 分支） | `ada53c8cb3` clip player, hook and ghost on screen、`14fc1e9d1e` | MERGE-BOTH | 自有 `players.cpp` 偏离上游 1268/153 行，需手工把 `ScreenRect.Inside(Client.m_SpecChar)` 早退移植进自有分支 |
| 21 | 地图加载进度条 | `src/game/client/components/maplayers.cpp:36`（`FRenderUploadCallback` → `CMenus::RenderLoading(pTitle,pMessage,IncreaseCounter)`，`menus.h:2176`） | `e8c2df1d58` add loading bar to map loading（改用 `FCallbackMapRendererInit` + `CMenus::RenderLoadingDirect(caption, content, std::optional<float> progress)`） | MERGE-BOTH | 上游引入按 group/layer 的连续进度，自有仍是计步式 `IncreaseCounter`；需把上游进度回调映射到自有 UI，属于真实集成工作 |
| 22 | 独立地图渲染器 / headless 后端 | 无（自有地图渲染在 `CGraphicsThreaded` + `MapRenderer` 内，`graphics_threaded.cpp` 偏离上游 1961/248 行，新增 `CMapLayers::RenderCustom`） | `8ebc6713bc` add standalone map renderer with headless backend（新增 `src/engine/client/backend_egl.cpp`、`backend_threaded.cpp`、`src/tools/map_render.cpp`，从 `backend_sdl.cpp` 拆出 200 行） | DEFER | 对客户端运行时无直接收益，且与自有后端分叉严重；仅在需要离屏/工具渲染时再评估 |
| 23 | 后端纹理采样与着色器正确性 | `src/engine/client/backend/opengl/backend_opengl.cpp`（偏离 401/102）、`backend_opengl3.cpp`（433/9）、`backend/vulkan/backend_vulkan.cpp`（2966/357） | `7c366145d1` Fix incorrect texture resampling with Vulkan and OpenGL 3.3、`6f764e4f11` Fix OpenGL shader location not bound for textured tile program、`def350c3c0` Fix heap overflow in OpenGL backends on resizing array textures、`98f0064af0` Fix OpenGL shader programs leaked on shutdown | MERGE-BOTH | 上游多为崩溃/越界/采样正确性修复，优先级高；但自有后端已被大量改写（QmVulkan 增强、低帧优化），无法直接 cherry-pick，需逐条比对 |
| 24 | 帧呈现与低刷新率性能 | `src/engine/client/backend_sdl.cpp`（偏离 171/123）、`src/game/client/components/qmclient/perf_logging.h`、`stutter_diagnostics.h`、`settings_perf_windows.h`（自有诊断 + 呈现对齐） | `dc92ab5434` client: Improve performance for low refresh/update rates、`ff0205d314` client: Don't sleep with cl_refresh_rate 0 | DEFER | 自有已撤回过一版自动限帧（见 `d297a1eb08` 提交说明），说明这一带曾引发延迟/拖影；再引入上游帧节奏改动前需实测对比 |
| 25 | 自有视觉/性能配置的卡片化 UI | `src/game/client/QmUi/cards/`（`QmCardCatalog{Visual,Function,Hud}.cpp`）+ `src/game/client/components/qmclient/menus_qmclient.cpp` | `cfb9c8226a` Rename `CMenus::RenderSettingsCustom` to `RenderSettingsAssets`、`menus_settings_appearance.cpp`（新增 978 行）等设置菜单拆分 | KEEP-FORK | 自有卡片目录是硬约束（AGENTS.md），上游按文件拆分的设置页与本架构不可混用；仅可借鉴具体设置项的呈现 |

---

## 不确定清单（判断不了 / 需要人工确认）

1. **`CScreenRect`（#4）的引入代价未估**：需要确认自有 `Graphics()->GetScreen` 的所有调用点（`players.cpp`、`items.cpp`、`nameplates.cpp`、`hud.cpp`、`particles.cpp`、`statusbar.cpp`、`bg_draw.cpp`、`moving_tiles.cpp`）能否一次性迁移，还是只能随上游大版本一起做。
2. **#2 与自有增强激光的交互未验证**：`CItems::RenderLaser` 是自有重写（`m_RiBetterLasers` / `m_QmLaserGlowIntensity`），上游裁剪使用 `pCurrent->m_From/m_To` 判定；需要确认在激光反射（`LASERTYPE_RIFLE` 等）情形下裁剪不会误裁半可见激光。
3. **#15 上游贴图兜底是否已被自有覆盖**：`skin_prepared_textures.h` / `skin_load_budget.h` 的实际行为需读实现确认，当前只从文件名与提交说明推断。
4. **#19 线路分段重构范围不明**：自有 `RenderHookCollLine` 是否包含 `HookTipLineSegment` 的等价实现（只确认了配置项存在），需读 `players.cpp` 中该函数全文。
5. **上游 `e333184d88`（Predict all sounds and particles）与自有 `cl_predict_events` 的差异未核实**：自有 `particles.cpp`/`effects.h` 中未命中 `Predict`/`m_IsPredicted`，推断预测发生在更上层，但未定位。
6. **`8ebc6713bc` headless 后端是否与自有 CI/工具链有交集未核实**：`src/tools/` 与 `src/game/client/maprenderer.cpp` 在自有侧的分叉量未逐一统计。
7. **上游 20.0 后 master 的新增提交（2026-08-27 之后）未逐条归类**：本次仅按路径 + 关键词筛查，`ui_popups.cpp`、`menus_settings_*.cpp` 等拆分文件的视觉行为变化未纳入。

---

## 最重要的 5 条判定（速览）

1. **#1 + #2 + #3 + #20（屏幕外裁剪族）→ MERGE-BOTH / TAKE-UPSTREAM**：这是 20.0 changelog 明写的高价值性能项，自有只做了钩子一半；`14fc1e9d1e`、`fabf09a3a3`、`061242fe47`、`ada53c8cb3` 应作为一次提交手工融合。
2. **#6（表情抖动收尾）→ TAKE-UPSTREAM**：`1cfa7f927a` 一行修复，`players.cpp:1374` 逐字命中旧公式，零冲突零风险，可立即落地。
3. **#5（计分板文字容器缓存）→ MERGE-BOTH**：自有 `scoreboard.cpp` 完全没用 `CCachedText`（`git grep` 无命中），上游 `5b10b49e16` 的公共设施可同时服务自有名牌缓存（`nameplate_text_cache.h`）。
4. **#18 + #19（未预测阴影 / 钩子提示线）→ TAKE-UPSTREAM + MERGE-BOTH**：自有已带同名配置项与默认值，只差 `players.cpp` 的渲染条件与分段着色对齐，属于"配置已合、代码未合"的低成本补齐。
5. **#7 / #8 / #9 / #11 / #12 / #13 / #14 / #25（视觉自有族）→ KEEP-FORK**：描边、拖尾带、彩虹/循环色、果冻、换弹动画、卡片化设置页均无上游对应实现，不应为"跟上游"而替换。

**风险提示**：QmClient 在共享渲染文件上的偏离量极大（`hud.cpp` 6455/767、`scoreboard.cpp` 1576/368、`skins.cpp` 3182/392、`players.cpp` 1268/153、`vulkan/backend_vulkan.cpp` 2966/357），绝大部分上游渲染/性能提交无法直接 cherry-pick；本次同步应按"逐条手工融合 + 每次单独验证"推进，任何整体 merge 上游 `ddnet/master` 都会在渲染层产生大规模冲突与回归风险。
