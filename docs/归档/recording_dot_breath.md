# 录制红点呼吸 + SDF 抗锯齿

- 已确认：灵动岛和独立计时胶囊中的录制红点共用 2.4 秒明暗周期，基础透明度为 65%～95%。
- 大小、颜色及显示条件保持现状；最终透明度继续叠加状态区和灵动岛入场透明度。
- 使用实际时间驱动余弦曲线，不受游戏计时暂停或帧率变化影响。
- 在 `src/test/qm_hud_media_island_logic_test.cpp` 补充周期、范围和循环边界平滑性测试代码。
- 验证：`python qmclient_scripts/gate/check_gate.py --mode quick` 通过（10 项通过，0 警告，0 失败）；`git diff --check` 通过。
- 只读审查：未发现本次改动的问题，两处调用保留原有布局、显隐判断与入场透明度叠加。
- 未执行：编译、测试运行、客户端视觉验证。
- 版本：在工作区已有 3.9.4 的基础上更新至 3.9.5。

## 红点轮廓改用灵动岛同款 SDF（2026-09-19）

问题：几何圆（`DrawSmoothCircle`，48 边形扇面）在红点这种小尺寸上边缘有硬折角，
和灵动岛逐像素抗锯齿的轮廓不是一个档次。

方案（未改引擎、未改着色器）：

- 新增 `DrawHudRecordingStatusDot`（`hud.cpp` 匿名命名空间）：把红点当成「一份 SDF 渲染状态」
  交给既有的 `Graphics()->RenderMediaIslandSdf`。
  - `m_MainRect` 宽高 = 直径、`m_MainRadius` = 半径 ⇒ 正方形 + 半径圆角 = 正圆；
  - 颜色与呼吸透明度写进 `m_BackgroundColor`（着色器里 `PanelAlpha` 就是整块板的不透明度）；
  - `m_ItemCount = 0`、无右胶囊、无轮廓环、无外阴影、`m_BackdropUv = 0`，
    所以 SDF 只画这个圆，不掺岛的其他层次，也不取模糊底图；
  - `m_Rect` 由 `QmHudMediaIslandSdfOuterRect` 得出，四边留出羽化与外阴影所需 padding。
- 羽化宽度沿用着色器里的 `Feather = max(ScreenPixelSize * 0.8, fwidth(Point) * 0.9)`，
  随屏幕像素密度与 HUD 编辑器缩放自动变化，所以轮廓更细腻且不需要任何额外参数。
- 不支持的 SDF 的后端（`HasMediaIslandSdf()` 为假）仍退回原来的 `DrawSmoothCircle` 几何圆。
- 屏幕像素比例抽成 `QmHudMediaIslandScreenPixelSize`（`hud_media_island_logic.h`），
  顺带替换掉开关环、钩子环、灵动岛里三处重复的同一段公式；红点用的比例在
  HUD 编辑器改写屏幕映射之前取（`CurrentScreenPixelSize`）。
- 两处调用（`CHud::RenderGameTimer` 的计时胶囊状态区、`CHud::RenderMediaIsland` 的岛内状态区）
  现在都走这一个入口，尺寸/位置/显隐条件逐字未变。

测试：`qm_hud_media_island_logic_test.cpp` 新增
`QmHudMediaIslandRecording.ScreenPixelSizeTakesTheLargerAxisScale`（纯函数，取两轴较大比例、
屏幕尺寸退化时按 1 像素处理）与
`QmHudMediaIslandSource.RecordingDotUsesTheIslandSdfWithGeometryFallback`
（源码契约：直径/半径构造正圆、无 item/胶囊/轮廓环/底图、兜底仍在入口内、
两条调用路径共用入口、比例在 BeginTransform 之前取）。

验证：

- `python qmclient_scripts/gate/check_gate.py --mode quick` —— 11 项通过、0 警告、0 失败。
- `qm_hud_media_island_logic_test.cpp` 编译通过（release 构建目录）。
- 未执行：完整 `testrunner` 链接与测试运行——工作区 `cmake-build-release` 里
  `src/test/qm_qqmusic_protocol_test.cpp`（HEAD `402a4b89e` 引入，与本次改动无关）
  第 46-49 行原始字符串里嵌了真实换行，编译即失败，整个测试目标链接不出来；
  另有 `steam_api.dll` 链接冲突。客户端视觉验证同样未执行。
- 顺带修复：`src/test/music_lyrics_qrc_test.cpp` 缺 `netease_lyric_timeline.h` 的 include
  （`SelectCurrentLine` / `SelectLatestStartedLine` / `SSelectedLine` 声明在那里），
  同一提交引入，否则该测试文件在 HEAD 上也编译不过。
