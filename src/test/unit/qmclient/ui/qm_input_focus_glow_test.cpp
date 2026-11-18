// 搜索框聚焦光晕的层表：只验证 ResolveInputFocusGlowLayers 这一份生产决策，
// 不复制像素公式——期望值一律从 ui_token::glow 令牌与「输入框高度」这两个事实推导。
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

#include <array>

namespace
{
	const ui_widget::SInputFocusGlowLayer &OuterLayer(const std::array<ui_widget::SInputFocusGlowLayer, 2> &aLayers) { return aLayers[0]; }
	const ui_widget::SInputFocusGlowLayer &InnerLayer(const std::array<ui_widget::SInputFocusGlowLayer, 2> &aLayers) { return aLayers[1]; }
}

TEST(InputFocusGlow, InactiveFocusProducesNoLayers)
{
	for(const float FocusAlpha : {0.0f, -0.5f, 0.0005f})
	{
		SCOPED_TRACE(FocusAlpha);
		const auto aLayers = ui_widget::ResolveInputFocusGlowLayers(CUIRect{10.0f, 20.0f, 240.0f, 28.0f}, FocusAlpha);
		for(const auto &Layer : aLayers)
		{
			EXPECT_FLOAT_EQ(Layer.m_Alpha, 0.0f);
			EXPECT_FLOAT_EQ(Layer.m_Spread, 0.0f);
		}
	}
}

TEST(InputFocusGlow, DegenerateShellProducesNoLayers)
{
	for(const CUIRect &Rect : {CUIRect{0.0f, 0.0f, 0.0f, 28.0f}, CUIRect{0.0f, 0.0f, 240.0f, 0.0f}, CUIRect{0.0f, 0.0f, -240.0f, -28.0f}})
	{
		const auto aLayers = ui_widget::ResolveInputFocusGlowLayers(Rect, 1.0f);
		EXPECT_FLOAT_EQ(OuterLayer(aLayers).m_Alpha, 0.0f);
		EXPECT_FLOAT_EQ(InnerLayer(aLayers).m_Alpha, 0.0f);
	}
}

TEST(InputFocusGlow, InnerLayerIsNarrowerAndStrongerThanTheOuterHalo)
{
	const auto aLayers = ui_widget::ResolveInputFocusGlowLayers(CUIRect{0.0f, 0.0f, 240.0f, 28.0f}, 1.0f);
	EXPECT_GT(OuterLayer(aLayers).m_Spread, InnerLayer(aLayers).m_Spread);
	EXPECT_GT(InnerLayer(aLayers).m_Alpha, OuterLayer(aLayers).m_Alpha);
	EXPECT_LT(InnerLayer(aLayers).m_Falloff, OuterLayer(aLayers).m_Falloff);
	EXPECT_FLOAT_EQ(OuterLayer(aLayers).m_Alpha, ui_token::glow::INPUT_FOCUS_OUTER_ALPHA);
	EXPECT_FLOAT_EQ(InnerLayer(aLayers).m_Alpha, ui_token::glow::INPUT_FOCUS_INNER_ALPHA);
}

TEST(InputFocusGlow, AlphaTracksAndClampsTheFocusProgress)
{
	const CUIRect Shell{0.0f, 0.0f, 240.0f, 28.0f};
	const auto Half = ui_widget::ResolveInputFocusGlowLayers(Shell, 0.5f);
	EXPECT_NEAR(OuterLayer(Half).m_Alpha, ui_token::glow::INPUT_FOCUS_OUTER_ALPHA * 0.5f, 0.0001f);
	EXPECT_NEAR(InnerLayer(Half).m_Alpha, ui_token::glow::INPUT_FOCUS_INNER_ALPHA * 0.5f, 0.0001f);
	// 过冲的进度不能把光晕叠到令牌强度以上（补间/弹簧的浮点残差不应放大亮度）。
	for(const float Overshoot : {1.5f, 4.0f})
	{
		const auto Full = ui_widget::ResolveInputFocusGlowLayers(Shell, Overshoot);
		EXPECT_FLOAT_EQ(OuterLayer(Full).m_Alpha, ui_token::glow::INPUT_FOCUS_OUTER_ALPHA);
		EXPECT_FLOAT_EQ(InnerLayer(Full).m_Alpha, ui_token::glow::INPUT_FOCUS_INNER_ALPHA);
	}
	// 两层共用同一份进度：聚焦淡入淡出的每一帧比例都一致，不会出现内层先亮外层后亮。
	EXPECT_NEAR(InnerLayer(Half).m_Alpha / ui_token::glow::INPUT_FOCUS_INNER_ALPHA,
		OuterLayer(Half).m_Alpha / ui_token::glow::INPUT_FOCUS_OUTER_ALPHA, 0.0001f);
}

TEST(InputFocusGlow, SpreadScalesWithShellHeightWithinTokenLimits)
{
	float PreviousSpread = 0.0f;
	for(const float Height : {8.0f, 12.0f, 16.0f, 20.0f, 24.0f, 32.0f, 48.0f})
	{
		SCOPED_TRACE(Height);
		const auto aLayers = ui_widget::ResolveInputFocusGlowLayers(CUIRect{0.0f, 0.0f, 240.0f, Height}, 1.0f);
		const float Spread = OuterLayer(aLayers).m_Spread;
		EXPECT_GE(Spread, ui_token::glow::INPUT_FOCUS_SPREAD_MIN);
		EXPECT_LE(Spread, ui_token::glow::INPUT_FOCUS_SPREAD_MAX);
		EXPECT_GE(Spread, PreviousSpread);
		EXPECT_NEAR(InnerLayer(aLayers).m_Spread, Spread * ui_token::glow::INPUT_FOCUS_INNER_RATIO, 0.0001f);
		PreviousSpread = Spread;
	}

	// 矮控件被下限兜住，高控件停在上限：光晕不会随控件一路变宽。
	EXPECT_FLOAT_EQ(OuterLayer(ui_widget::ResolveInputFocusGlowLayers(CUIRect{0.0f, 0.0f, 240.0f, 4.0f}, 1.0f)).m_Spread, ui_token::glow::INPUT_FOCUS_SPREAD_MIN);
	EXPECT_FLOAT_EQ(OuterLayer(ui_widget::ResolveInputFocusGlowLayers(CUIRect{0.0f, 0.0f, 240.0f, 200.0f}, 1.0f)).m_Spread, ui_token::glow::INPUT_FOCUS_SPREAD_MAX);
}

TEST(InputFocusGlow, DisabledSwitchProducesNoLayersEvenWhenFocused)
{
	const CUIRect Shell{0.0f, 0.0f, 240.0f, 28.0f};
	for(const auto &Layer : ui_widget::ResolveInputFocusGlowLayers(Shell, 1.0f, false))
	{
		EXPECT_FLOAT_EQ(Layer.m_Alpha, 0.0f);
		EXPECT_FLOAT_EQ(Layer.m_Spread, 0.0f);
	}
	// 开关只关光晕，不动层表本身：重新打开后立刻回到同一组宽度与强度。
	const auto aEnabled = ui_widget::ResolveInputFocusGlowLayers(Shell, 1.0f, true);
	EXPECT_GT(aEnabled[1].m_Alpha, 0.0f);
	EXPECT_GT(aEnabled[0].m_Spread, 0.0f);
}

TEST(InputFocusGlow, LightSurfacesDimTheHaloWithoutTouchingDarkThemes)
{
	// 白色卡片：亮度 1，压到只剩 55%。
	EXPECT_FLOAT_EQ(ResolveUiSurfaceGlowScale(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)), 1.0f - ui_token::glow::LIGHT_SURFACE_DIM);
	// 深色卡片：几乎不压，令牌原强度就是观感上限。
	EXPECT_GE(ResolveUiSurfaceGlowScale(ColorRGBA(0.06f, 0.07f, 0.09f, 1.0f)), 0.95f);
	// 越亮越压：中间灰必须落在两端之间，且随亮度单调不增。
	float PreviousScale = 2.0f;
	for(const float Value : {0.0f, 0.15f, 0.35f, 0.55f, 0.75f, 1.0f})
	{
		SCOPED_TRACE(Value);
		const float Scale = ResolveUiSurfaceGlowScale(ColorRGBA(Value, Value, Value, 1.0f));
		EXPECT_GE(Scale, 1.0f - ui_token::glow::LIGHT_SURFACE_DIM - 1e-4f);
		EXPECT_LE(Scale, 1.0f + 1e-4f);
		EXPECT_LE(Scale, PreviousScale + 1e-4f);
		PreviousScale = Scale;
	}
	// 透明度先与背板合成再判亮度：完全透明的表面不能拿它自己的 RGB 冒充"白底"。
	EXPECT_FLOAT_EQ(ResolveUiSurfaceGlowScale(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f)),
		ResolveUiSurfaceGlowScale(ui_token::color::SURFACE_BACKDROP));
}
