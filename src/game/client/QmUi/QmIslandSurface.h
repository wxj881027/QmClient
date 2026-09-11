// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMISLANDSURFACE_H
#define GAME_CLIENT_QMUI_QMISLANDSURFACE_H

#include <engine/graphics.h>

#include <game/client/components/hud_media_island_logic.h>

// 灵动岛表面：把「一份 SDF 渲染状态 → 屏幕上的胶囊岛」这条路径收成一个公共组件。
//
// 与 HUD 的关系：HUD 动态岛当前仍使用自己的私有绘制路径（hud.cpp 内），
// 本组件只做新增调用方，不反向替换 HUD，避免动到守护 HUD 渲染结构的既有测试。
// 两边共用同一份状态结构（SHudMediaIslandSdfRenderState）与 GPU 参数构建
// （QmHudMediaIslandBuildGpuSdfParams），因此观感同源。
namespace qm_island
{
	// 外阴影常量与 HUD 保持一致：状态里的 m_OuterShadowSize / m_OuterShadowOpacity
	// 需要按同一比例推导，否则菜单岛与 HUD 岛的外沿观感会不一致。
	inline constexpr float OUTER_SHADOW_PIXELS = 5.0f;
	inline constexpr float OUTER_SHADOW_OPACITY = 0.35f;

	// 环绕倒计时条：外框描边走向的进度环。整圈底色为 RingColor 的 18% 透明度，
	// 进度弧按 CountdownProgress 顺时针覆盖，与 HUD 卫星倒计时环同一套参数语义。
	struct SIslandRing
	{
		float m_Radius = 0.0f;
		float m_Thickness = 0.0f;
		float m_Progress = 1.0f;
		ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	};

	// 让 State 的外阴影按 State.m_ScreenPixelSize 推导，与 HUD 的取值方式一致。
	inline void ApplyOuterShadow(SHudMediaIslandSdfRenderState &State)
	{
		State.m_OuterShadowSize = State.m_ScreenPixelSize * OUTER_SHADOW_PIXELS;
		State.m_OuterShadowOpacity = OUTER_SHADOW_OPACITY * State.m_BackgroundColor.a;
	}

	// 绘制一块灵动岛表面（调用方负责填好 SdfState，含 m_Rect）。
	// 优先走实时 SDF（含外阴影、液体融合、环绕环）；不支持时退回圆角矩形 + 圆环几何。
	// Backdrop 为 HUD 侧的背景模糊目标，菜单调用方传默认值即可（不做背景模糊）。
	bool Render(IGraphics *pGraphics, const SHudMediaIslandSdfRenderState &SdfState, IGraphics::CRenderTargetHandle Backdrop = IGraphics::CRenderTargetHandle());
} // namespace qm_island

#endif
