// 输入框聚焦光晕与外层的协作：真实 CUi + 文字引擎 + 动画运行时，只在 GPU 边界记录。
// 验证的是「聚焦的输入框向外提交两层光晕、任何模式都一样、同一张卡片里只有聚焦的那个框亮、
// 失焦后光晕自行淡出」，不复制任何宽度或 alpha 公式。
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <cmath>
#include <vector>

namespace
{
	// 光晕在设备边界上的唯一特征：只描边（填充丢弃）且边框有颜色。
	// 外壳自己是「有填充 + 有描边」，两者不会混淆。
	bool IsGlowSurface(const IGraphics::SRoundedRectSdfParams &Surface)
	{
		return Surface.m_GradientParams2.y > 0.5f && Surface.m_BorderColor.w > 0.0f;
	}

	std::vector<IGraphics::SRoundedRectSdfParams> CollectGlowSurfaces(const std::vector<IGraphics::SRoundedRectSdfParams> &vSurfaces)
	{
		std::vector<IGraphics::SRoundedRectSdfParams> vGlow;
		for(const auto &Surface : vSurfaces)
		{
			if(IsGlowSurface(Surface))
				vGlow.push_back(Surface);
		}
		return vGlow;
	}

	class CInputFocusGlow : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CLineInputBuffered<128> m_Search;
		CLineInputBuffered<128> m_Plain;
		// 服务器筛选卡里三个并排输入框的相对位置：搜索、排除、服务器地址。
		// 三者纵向间距 40、外壳高 28，光晕带宽（≤5）之间留有空白，便于按 y 归属。
		CLineInputBuffered<128> m_Search2;
		CLineInputBuffered<128> m_Address;
		const CUIRect m_Rect{40.0f, 40.0f, 260.0f, 28.0f};
		const CUIRect m_SecondRect{40.0f, 80.0f, 260.0f, 28.0f};
		const CUIRect m_ThirdRect{40.0f, 120.0f, 260.0f, 28.0f};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 光晕跟随聚焦淡入轨道，必须让动画资格门真正放行动画。
			g_Config.m_QmUiMotionLevel = 2;
			// 测试基座用 CConfig() 清零所有配置，光晕开关要显式打开；
			// 真实客户端由 CConfigManager 注册默认值 1。
			g_Config.m_QmUiFocusGlow = 1;
		}

		void AdvanceFrame()
		{
			m_Client.AdvanceFrame();
			AdvanceAnimFrame();
			m_Ui.Update();
		}

		void Focus(CLineInputBuffered<128> *pInput)
		{
			m_Ui.SetActiveItem(pInput);
			pInput->Activate(EInputPriority::UI);
			pInput->SelectAll();
		}

		void Unfocus(CLineInputBuffered<128> *pInput)
		{
			m_Ui.ReleaseActiveTextInput(pInput);
			pInput->Deactivate();
		}

		void DrawField(CLineInputBuffered<128> *pInput, const CUIRect &Rect, ui_widget::EInputFieldMode Mode)
		{
			ui_widget::SInputFieldOptions Options;
			Options.m_Mode = Mode;
			ui_widget::InputField(ui_widget::ControlContext(&m_Ui), pInput, Rect, Options);
		}

		// 三个框按生产顺序（搜索 → 排除 → 地址）在同一帧绘制，和服务器筛选卡一致。
		void DrawCard()
		{
			UiGraphics()->m_vRoundedSubmissions.clear();
			DrawField(&m_Search, m_Rect, ui_widget::EInputFieldMode::SEARCH);
			DrawField(&m_Search2, m_SecondRect, ui_widget::EInputFieldMode::SEARCH);
			DrawField(&m_Address, m_ThirdRect, ui_widget::EInputFieldMode::TEXT);
		}

		std::vector<IGraphics::SRoundedRectSdfParams> DrawOne(CLineInputBuffered<128> *pInput, ui_widget::EInputFieldMode Mode)
		{
			UiGraphics()->m_vRoundedSubmissions.clear();
			DrawField(pInput, m_Rect, Mode);
			return UiGraphics()->m_vRoundedSubmissions;
		}

		// 真实点击：光标移到外壳中心，按下并保持一帧，再松开一帧（与生产帧序一致）。
		void Click(const CUIRect &Rect)
		{
			const vec2 Position = Rect.Center();
			const vec2 Pixels = Position * vec2(UiGraphics()->WindowWidth(), UiGraphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = KEY_MOUSE_1;
			AdvanceFrame();
			DrawCard();
			m_Input.m_HeldKey = 0;
			AdvanceFrame();
			DrawCard();
		}

		// 光晕按「中心落在哪个输入框的纵向范围」归属：带宽 ≤ 5，三个框间距 12，不会串号。
		static bool GlowBelongsTo(const IGraphics::SRoundedRectSdfParams &Glow, const CUIRect &Field)
		{
			const float Center = Glow.m_Rect.y + Glow.m_Rect.w * 0.5f;
			return Center >= Field.y - ui_token::glow::INPUT_FOCUS_SPREAD_MAX - 1.0f && Center <= Field.y + Field.h + ui_token::glow::INPUT_FOCUS_SPREAD_MAX + 1.0f;
		}

		int CountGlowsFor(const std::vector<IGraphics::SRoundedRectSdfParams> &vSurfaces, const CUIRect &Field)
		{
			int Count = 0;
			for(const auto &Glow : CollectGlowSurfaces(vSurfaces))
			{
				if(GlowBelongsTo(Glow, Field))
					++Count;
			}
			return Count;
		}

		// 外壳描边的透明度：取非光晕、矩形与外壳同尺寸的那一条提交。
		// 焦点描边会把这个值从常态灰推高到强调色，因此它同时是「焦点提示还在不在」的观察点。
		float ShellBorderAlpha(const std::vector<IGraphics::SRoundedRectSdfParams> &vSurfaces) const
		{
			float Alpha = 0.0f;
			for(const auto &Surface : vSurfaces)
			{
				if(IsGlowSurface(Surface))
					continue;
				if(std::abs(Surface.m_Rect.z - m_Rect.w) > 1.5f || std::abs(Surface.m_Rect.w - m_Rect.h) > 1.5f)
					continue;
				Alpha = std::max(Alpha, Surface.m_BorderColor.a);
			}
			return Alpha;
		}
	};
} // namespace

TEST_F(CInputFocusGlow, FocusedSearchFieldSubmitsInnerAndOuterHaloAroundTheShell)
{
	Focus(&m_Search);
	std::vector<IGraphics::SRoundedRectSdfParams> vGlow;
	for(int i = 0; i < 12; ++i)
	{
		AdvanceFrame();
		vGlow = CollectGlowSurfaces(DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH));
	}
	ASSERT_EQ(vGlow.size(), 2u);
	const auto &Outer = vGlow[0];
	const auto &Inner = vGlow[1];

	// 外层先画、内层后画：外层更宽更淡，内层更窄更亮，叠起来才是「贴着边框的晕」。
	// SDF 矩形是 vec4(x, y, 宽, 高)：宽度取 .z，别用 .h/.w 这类名字别名。
	EXPECT_GT(Outer.m_Rect.z, Inner.m_Rect.z);
	EXPECT_GT(Inner.m_Rect.z, m_Rect.w);
	EXPECT_LT(Outer.m_BorderColor.a, Inner.m_BorderColor.a);
	EXPECT_GT(Outer.m_BorderColor.a, 0.0f);
	// 聚焦进度到顶后强度到顶；浅色表面还会再压一档（ResolveUiSurfaceGlowScale），
	// 所以只断言上下界，以及两层共用同一个表面压暗系数。
	EXPECT_LE(Inner.m_BorderColor.a, ui_token::glow::INPUT_FOCUS_INNER_ALPHA + 1e-3f);
	EXPECT_GE(Inner.m_BorderColor.a, ui_token::glow::INPUT_FOCUS_INNER_ALPHA * (1.0f - ui_token::glow::LIGHT_SURFACE_DIM) - 1e-3f);
	EXPECT_LE(Outer.m_BorderColor.a, ui_token::glow::INPUT_FOCUS_OUTER_ALPHA + 1e-3f);
	EXPECT_NEAR(Outer.m_BorderColor.a / Inner.m_BorderColor.a,
		ui_token::glow::INPUT_FOCUS_OUTER_ALPHA / ui_token::glow::INPUT_FOCUS_INNER_ALPHA, 0.01f);

	// 两层光晕与外壳同心：左右两侧各自向外扩出一段，不能只往一边长。
	const float OuterSpread = (Outer.m_Rect.z - m_Rect.w) * 0.5f;
	const float InnerSpread = (Inner.m_Rect.z - m_Rect.w) * 0.5f;
	EXPECT_NEAR(Outer.m_Rect.x, m_Rect.x - OuterSpread, 0.01f);
	EXPECT_NEAR(Inner.m_Rect.x, m_Rect.x - InnerSpread, 0.01f);
	// 上下同样对称：高度方向也要按同一 Spread 外扩，不能只加在宽度上。
	// 设备像素对齐会让两轴的扩出量相差不到一个逻辑像素，因此按一像素容差比较。
	EXPECT_NEAR((Outer.m_Rect.w - m_Rect.h) * 0.5f, OuterSpread, 1.0f);
	EXPECT_NEAR((Inner.m_Rect.w - m_Rect.h) * 0.5f, InnerSpread, 1.0f);
	// 外层宽度由令牌上限与外壳高度共同决定，内层按比例收窄。
	EXPECT_NEAR(OuterSpread, ui_token::glow::InputFocusSpread(m_Rect.h), 0.5f);

	// 外壳本体仍然照常提交（填充 + 描边），光晕没有把它顶掉。
	UiGraphics()->m_vRoundedSubmissions.clear();
	DrawField(&m_Search, m_Rect, ui_widget::EInputFieldMode::SEARCH);
	bool SawShell = false;
	for(const auto &Surface : UiGraphics()->m_vRoundedSubmissions)
	{
		// 外壳仍按原始矩形提交（像素对齐误差一像素以内），且不是光晕那条只描边路径。
		if(!IsGlowSurface(Surface) && Surface.m_BorderColor.a > 0.0f && std::abs(Surface.m_Rect.z - m_Rect.w) <= 1.5f && std::abs(Surface.m_Rect.w - m_Rect.h) <= 1.5f)
			SawShell = true;
	}
	EXPECT_TRUE(SawShell);
}

TEST_F(CInputFocusGlow, EveryFieldModeGlowsWhenFocused)
{
	// 服务器地址框是 TEXT 模式；它和搜索框并排放着，聚焦时必须同样发光，
	// 否则玩家会以为地址框「点了没反应」。多行编辑器共用同一份层表。
	for(const auto Mode : {ui_widget::EInputFieldMode::TEXT, ui_widget::EInputFieldMode::SEARCH, ui_widget::EInputFieldMode::MULTILINE})
	{
		SCOPED_TRACE(static_cast<int>(Mode));
		Focus(&m_Plain);
		std::vector<IGraphics::SRoundedRectSdfParams> vGlow;
		for(int i = 0; i < 12; ++i)
		{
			AdvanceFrame();
			vGlow = CollectGlowSurfaces(DrawOne(&m_Plain, Mode));
		}
		ASSERT_EQ(vGlow.size(), 2u);
		EXPECT_GT(vGlow[0].m_BorderColor.a, 0.0f);
		Unfocus(&m_Plain);
		// 每个模式之间清干净：失焦的框不能再给下一个模式留下亮着的轨道。
		for(int i = 0; i < 20; ++i)
		{
			AdvanceFrame();
			DrawOne(&m_Plain, Mode);
		}
		EXPECT_TRUE(CollectGlowSurfaces(DrawOne(&m_Plain, Mode)).empty());
	}
}

TEST_F(CInputFocusGlow, HaloFadesOutAfterTheFieldLosesFocus)
{
	Focus(&m_Search);
	std::vector<IGraphics::SRoundedRectSdfParams> vGlow;
	for(int i = 0; i < 6; ++i)
	{
		AdvanceFrame();
		vGlow = CollectGlowSurfaces(DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH));
	}
	ASSERT_EQ(vGlow.size(), 2u);
	const float FocusedAlpha = vGlow[1].m_BorderColor.a;

	Unfocus(&m_Search);
	bool SawFading = false;
	float PreviousAlpha = FocusedAlpha;
	for(int i = 0; i < 30; ++i)
	{
		AdvanceFrame();
		const auto vFrameGlow = CollectGlowSurfaces(DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH));
		if(vFrameGlow.size() < 2u)
			continue;
		SawFading = true;
		// 失焦的时间轴只能单向变暗：光晕跟着聚焦轨道一起淡出，不会半路回亮。
		EXPECT_LE(vFrameGlow[1].m_BorderColor.a, PreviousAlpha + 1e-3f);
		PreviousAlpha = vFrameGlow[1].m_BorderColor.a;
	}
	EXPECT_TRUE(SawFading);
	EXPECT_LT(PreviousAlpha, FocusedAlpha);
	// 淡出结束后不再提交任何光晕层。
	EXPECT_TRUE(CollectGlowSurfaces(DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH)).empty());
}

TEST_F(CInputFocusGlow, ToggleOffSuppressesTheHaloAndKeepsFocusFeedback)
{
	// 「图像 → 视觉效果 → 界面主题 → 启用聚焦光晕」对应 qm_ui_focus_glow。
	// 关掉后光晕整体消失；焦点描边与文本焦点保留，玩家不会因此失去焦点提示。
	// 先用未聚焦的描边透明度做基线：开关不能把「描边变亮」这件事一起关掉。
	float RestingBorderAlpha = 0.0f;
	for(int i = 0; i < 4; ++i)
	{
		AdvanceFrame();
		DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH);
	}
	RestingBorderAlpha = ShellBorderAlpha(UiGraphics()->m_vRoundedSubmissions);
	ASSERT_GT(RestingBorderAlpha, 0.0f);

	Focus(&m_Search);
	g_Config.m_QmUiFocusGlow = 0;
	float FocusedBorderAlpha = 0.0f;
	for(int i = 0; i < 12; ++i)
	{
		AdvanceFrame();
		EXPECT_TRUE(CollectGlowSurfaces(DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH)).empty());
		FocusedBorderAlpha = ShellBorderAlpha(UiGraphics()->m_vRoundedSubmissions);
	}
	// 文本焦点与描边都在：关掉的是效果，不是焦点提示。
	EXPECT_TRUE(m_Search.IsActive());
	EXPECT_GT(FocusedBorderAlpha, RestingBorderAlpha);

	// 开关是实时生效的：重新打开后下一次绘制就要恢复两层光晕。
	g_Config.m_QmUiFocusGlow = 1;
	std::vector<IGraphics::SRoundedRectSdfParams> vGlow;
	for(int i = 0; i < 12; ++i)
	{
		AdvanceFrame();
		vGlow = CollectGlowSurfaces(DrawOne(&m_Search, ui_widget::EInputFieldMode::SEARCH));
	}
	EXPECT_EQ(vGlow.size(), 2u);
}

TEST_F(CInputFocusGlow, OnlyTheClickedFieldGlowsInsideOneCard)
{
	// 复现服务器筛选卡：搜索、排除、服务器地址三个框同帧绘制。
	// 点第一个框：只有它发光；再点地址框（TEXT 模式）：光必须整体搬到地址框上，
	// 前两个框不能残留——「两个框同时亮」正是玩家看到的错乱。
	AdvanceFrame();
	DrawCard();
	ASSERT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_Rect), 0);

	Click(m_Rect);
	for(int i = 0; i < 20; ++i)
	{
		AdvanceFrame();
		DrawCard();
	}
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_Rect), 2);
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_SecondRect), 0);
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_ThirdRect), 0);

	Click(m_ThirdRect);
	for(int i = 0; i < 20; ++i)
	{
		AdvanceFrame();
		DrawCard();
	}
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_ThirdRect), 2);
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_Rect), 0);
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_SecondRect), 0);

	// 换回来：焦点回搜索框，地址框的 TEXT 光晕同样淡出。
	Click(m_Rect);
	for(int i = 0; i < 20; ++i)
	{
		AdvanceFrame();
		DrawCard();
	}
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_Rect), 2);
	EXPECT_EQ(CountGlowsFor(UiGraphics()->m_vRoundedSubmissions, m_ThirdRect), 0);
}
