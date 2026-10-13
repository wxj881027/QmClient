// 公共控件与真实 UI、动画后端协作；仅隔离设备输入、客户端时钟和 GPU 提交。
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmControlMotion : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUiV2AnimationRuntime m_Animation;
		CUiV2AnimationRuntime m_ReferenceAnimation;
		CButtonContainer m_Button;
		char m_ToggleId = 0;
		CLineInputBuffered<64> m_Text;
		CLineInputBuffered<64> m_OverlayText;
		const CUIRect m_Rect{40, 40, 120, 24};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			g_Config.m_QmUiMotionLevel = 2;
			g_Config.m_QmUiDropdownColor = CConfig::ms_QmUiDropdownColor;
			g_Config.m_QmUiDropdownOpacity = CConfig::ms_QmUiDropdownOpacity;
			g_Config.m_QmUiAccentColor = CConfig::ms_QmUiAccentColor;
			g_Config.m_QmUiAccentOpacity = CConfig::ms_QmUiAccentOpacity;
			Frame(0.0f);
		}

		void Frame(float Dt)
		{
			m_Animation.Advance(Dt);
			m_ReferenceAnimation.Advance(Dt);
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		qm_ui_test::CRealUiTestGraphics &Graphics()
		{
			return *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
		}

		IUiContext Context(CUiV2AnimationRuntime &Animation)
		{
			Graphics().m_vRoundedSubmissions.clear();
			m_Ui.SetQmAnimationRuntime(&Animation);
			auto Ctx = ui_widget::ControlContext(&m_Ui);
			// 设置页的后台上下文不携带可写动画入口，控件从 UI 读取原运行时。
			if(m_Ui.RenderOnly())
				Ctx.m_pAnim = nullptr;
			return Ctx;
		}

		void DrawButton(CUiV2AnimationRuntime &Animation, bool Selected)
		{
			ui_widget::SButtonSurfaceOptions Options;
			Options.m_Selected = Selected;
			ui_widget::DrawButtonSurface(Context(Animation), &m_Button, m_Rect, Options);
		}

		void DrawToggle(CUiV2AnimationRuntime &Animation, bool Value)
		{
			ui_widget::DrawToggle(Context(Animation), &m_ToggleId, Value, m_Rect);
		}

		void DrawInput(CUiV2AnimationRuntime &Animation)
		{
			ui_widget::InputField(Context(Animation), &m_Text, m_Rect, ui_widget::SInputFieldOptions{});
		}

		void ExpectSurface(const IGraphics::SRoundedRectSdfParams &Actual, const IGraphics::SRoundedRectSdfParams &Expected)
		{
			EXPECT_EQ(Actual.m_Rect, Expected.m_Rect);
			EXPECT_EQ(Actual.m_FillColor, Expected.m_FillColor);
			EXPECT_EQ(Actual.m_BorderColor, Expected.m_BorderColor);
			EXPECT_EQ(Actual.m_CornerRadii, Expected.m_CornerRadii);
			EXPECT_EQ(Actual.m_Params, Expected.m_Params);
		}
	};
}

TEST_F(QmControlMotion, BackgroundButtonColorMatchesTheForegroundAtTheSameAnimationTime)
{
	for(const int ColorMode : {0, 1})
	{
		SCOPED_TRACE(ColorMode);
		g_Config.m_QmUiColorInterpolation = ColorMode;
		m_Animation.Reset();
		m_ReferenceAnimation.Reset();
		DrawButton(m_Animation, false);
		DrawButton(m_ReferenceAnimation, false);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto Initial = Graphics().m_vRoundedSubmissions.front();
		DrawButton(m_Animation, true);
		DrawButton(m_ReferenceAnimation, true);
		Frame(0.04f);
		DrawButton(m_ReferenceAnimation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto Reference = Graphics().m_vRoundedSubmissions.front();
		ASSERT_NE(Reference.m_FillColor, Initial.m_FillColor);
		const int TracksBefore = m_Animation.ActiveTrackCount();
		const float TimeBefore = m_Animation.TimeSec();
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawButton(m_Animation, true);
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSurface(Graphics().m_vRoundedSubmissions.front(), Reference);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), TracksBefore);
		EXPECT_FLOAT_EQ(m_Animation.TimeSec(), TimeBefore);
	}
}

TEST_F(QmControlMotion, BackgroundToggleKeepsTheForegroundTrackColorAndKnobPosition)
{
	for(const int ColorMode : {0, 1})
	{
		SCOPED_TRACE(ColorMode);
		g_Config.m_QmUiColorInterpolation = ColorMode;
		m_Animation.Reset();
		m_ReferenceAnimation.Reset();
		DrawToggle(m_Animation, false);
		DrawToggle(m_ReferenceAnimation, false);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
		const float InitialX = Graphics().m_vRoundedSubmissions.back().m_Rect.x;
		DrawToggle(m_Animation, true);
		DrawToggle(m_ReferenceAnimation, true);
		Frame(0.04f);
		DrawToggle(m_ReferenceAnimation, true);
		const auto Reference = Graphics().m_vRoundedSubmissions;
		ASSERT_EQ(Reference.size(), 2u);
		ASSERT_GT(Reference.back().m_Rect.x, InitialX);
		const int TracksBefore = m_Animation.ActiveTrackCount();
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawToggle(m_Animation, true);
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), Reference.size());
		for(size_t Index = 0; Index < Reference.size(); ++Index)
			ExpectSurface(Graphics().m_vRoundedSubmissions[Index], Reference[Index]);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), TracksBefore);
	}
}

TEST_F(QmControlMotion, BackgroundTargetChangesDoNotRedirectTheVisibleButtonAnimation)
{
	for(const int ColorMode : {0, 1})
	{
		SCOPED_TRACE(ColorMode);
		g_Config.m_QmUiColorInterpolation = ColorMode;
		m_Animation.Reset();
		m_ReferenceAnimation.Reset();
		DrawButton(m_Animation, false);
		DrawButton(m_ReferenceAnimation, false);
		DrawButton(m_Animation, true);
		DrawButton(m_ReferenceAnimation, true);
		Frame(0.04f);
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawButton(m_Animation, false);
		}
		Frame(0.02f);
		DrawButton(m_ReferenceAnimation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto Reference = Graphics().m_vRoundedSubmissions.front();
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawButton(m_Animation, false);
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		EXPECT_EQ(Graphics().m_vRoundedSubmissions.front().m_FillColor, Reference.m_FillColor);
		EXPECT_EQ(m_Animation.QueuedTrackCount(), 0);
	}
}

TEST_F(QmControlMotion, ForegroundRetargetAfterOverlayStartsFromTheLastDisplayedColor)
{
	for(const int ColorMode : {0, 1})
	{
		SCOPED_TRACE(ColorMode);
		g_Config.m_QmUiColorInterpolation = ColorMode;
		m_Animation.Reset();
		m_ReferenceAnimation.Reset();
		DrawButton(m_Animation, false);
		DrawButton(m_ReferenceAnimation, false);
		DrawButton(m_Animation, true);
		DrawButton(m_ReferenceAnimation, true);
		Frame(0.04f);
		DrawButton(m_ReferenceAnimation, true);
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawButton(m_Animation, true);
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto LastBackgroundColor = Graphics().m_vRoundedSubmissions.front().m_FillColor;
		DrawButton(m_ReferenceAnimation, false);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto ReferenceColor = Graphics().m_vRoundedSubmissions.front().m_FillColor;
		DrawButton(m_Animation, false);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto ActualColor = Graphics().m_vRoundedSubmissions.front().m_FillColor;
		EXPECT_NEAR(ActualColor.x, LastBackgroundColor.x, 0.000001f);
		EXPECT_NEAR(ActualColor.y, LastBackgroundColor.y, 0.000001f);
		EXPECT_NEAR(ActualColor.z, LastBackgroundColor.z, 0.000001f);
		EXPECT_NEAR(ActualColor.w, LastBackgroundColor.w, 0.000001f);
		EXPECT_EQ(Graphics().m_vRoundedSubmissions.front().m_FillColor, ReferenceColor);
	}
}

TEST_F(QmControlMotion, FirstBackgroundDrawDoesNotSeedControlsWithTheOldTarget)
{
	for(const int ColorMode : {0, 1})
	{
		SCOPED_TRACE(ColorMode);
		g_Config.m_QmUiColorInterpolation = ColorMode;
		m_Animation.Reset();
		m_ReferenceAnimation.Reset();
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawButton(m_Animation, false);
			DrawToggle(m_Animation, false);
		}
		EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
		DrawButton(m_ReferenceAnimation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto ReferenceButton = Graphics().m_vRoundedSubmissions.front();
		DrawButton(m_Animation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSurface(Graphics().m_vRoundedSubmissions.front(), ReferenceButton);
		DrawToggle(m_ReferenceAnimation, true);
		const auto ReferenceToggle = Graphics().m_vRoundedSubmissions;
		DrawToggle(m_Animation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), ReferenceToggle.size());
		for(size_t Index = 0; Index < ReferenceToggle.size(); ++Index)
			ExpectSurface(Graphics().m_vRoundedSubmissions[Index], ReferenceToggle[Index]);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), m_ReferenceAnimation.ActiveTrackCount());
		Frame(0.04f);
		DrawButton(m_ReferenceAnimation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto MovingReferenceButton = Graphics().m_vRoundedSubmissions.front();
		DrawButton(m_Animation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSurface(Graphics().m_vRoundedSubmissions.front(), MovingReferenceButton);
		DrawToggle(m_ReferenceAnimation, true);
		const auto MovingReferenceToggle = Graphics().m_vRoundedSubmissions;
		DrawToggle(m_Animation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), MovingReferenceToggle.size());
		for(size_t Index = 0; Index < MovingReferenceToggle.size(); ++Index)
			ExpectSurface(Graphics().m_vRoundedSubmissions[Index], MovingReferenceToggle[Index]);
	}
}

TEST_F(QmControlMotion, DisabledMotionDrawsCurrentTargetsBehindAnOverlay)
{
	for(const int ColorMode : {0, 1})
	{
		SCOPED_TRACE(ColorMode);
		g_Config.m_QmUiMotionLevel = 2;
		g_Config.m_QmUiColorInterpolation = ColorMode;
		m_Animation.Reset();
		m_ReferenceAnimation.Reset();
		DrawButton(m_Animation, false);
		DrawButton(m_ReferenceAnimation, false);
		DrawToggle(m_Animation, false);
		DrawToggle(m_ReferenceAnimation, false);
		DrawButton(m_Animation, true);
		DrawButton(m_ReferenceAnimation, true);
		DrawToggle(m_Animation, true);
		DrawToggle(m_ReferenceAnimation, true);
		Frame(0.04f);
		const int TracksBefore = m_Animation.ActiveTrackCount();
		g_Config.m_QmUiMotionLevel = 0;
		DrawButton(m_ReferenceAnimation, true);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		const auto ReferenceButton = Graphics().m_vRoundedSubmissions.front();
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawButton(m_Animation, true);
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSurface(Graphics().m_vRoundedSubmissions.front(), ReferenceButton);
		DrawToggle(m_ReferenceAnimation, true);
		const auto ReferenceToggle = Graphics().m_vRoundedSubmissions;
		{
			CUiScopedBackgroundRender Background(&m_Ui);
			DrawToggle(m_Animation, true);
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), ReferenceToggle.size());
		for(size_t Index = 0; Index < ReferenceToggle.size(); ++Index)
			ExpectSurface(Graphics().m_vRoundedSubmissions[Index], ReferenceToggle[Index]);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), TracksBefore);
	}
}

TEST_F(QmControlMotion, MeasurementSubmitsNoControlsAndLeavesAnimationTimeUnchanged)
{
	DrawButton(m_Animation, false);
	DrawToggle(m_Animation, false);
	DrawButton(m_Animation, true);
	DrawToggle(m_Animation, true);
	const int TracksBefore = m_Animation.ActiveTrackCount();
	const float TimeBefore = m_Animation.TimeSec();
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		DrawButton(m_Animation, false);
		EXPECT_TRUE(Graphics().m_vRoundedSubmissions.empty());
		DrawToggle(m_Animation, false);
		EXPECT_TRUE(Graphics().m_vRoundedSubmissions.empty());
		DrawInput(m_Animation);
		EXPECT_TRUE(Graphics().m_vRoundedSubmissions.empty());
	}
	EXPECT_EQ(m_Animation.ActiveTrackCount(), TracksBefore);
	EXPECT_FLOAT_EQ(m_Animation.TimeSec(), TimeBefore);
}

TEST_F(QmControlMotion, BackgroundInputKeepsItsFocusFadeWithoutTakingOverlayFocus)
{
	m_Text.Set("unfinished draft");
	DrawInput(m_Animation);
	DrawInput(m_ReferenceAnimation);
	m_Ui.SetActiveItem(&m_Text);
	m_Text.Activate(EInputPriority::UI);
	DrawInput(m_Animation);
	DrawInput(m_ReferenceAnimation);
	Frame(0.04f);
	DrawInput(m_ReferenceAnimation);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	const auto Reference = Graphics().m_vRoundedSubmissions.front();
	ASSERT_GT(Reference.m_Params.x, 1.0f);
	const int TracksBefore = m_Animation.ActiveTrackCount();
	m_OverlayText.Activate(EInputPriority::CONSOLE);
	m_Ui.SetActiveItem(&m_OverlayText);
	m_Ui.SetKeyboardFocus(&m_OverlayText);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		DrawInput(m_Animation);
	}
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	ExpectSurface(Graphics().m_vRoundedSubmissions.front(), Reference);
	EXPECT_EQ(CLineInput::GetActiveInput(), &m_OverlayText);
	EXPECT_EQ(m_Ui.ActiveItem(), &m_OverlayText);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_OverlayText);
	EXPECT_FALSE(m_Text.IsActive());
	EXPECT_STREQ(m_Text.GetString(), "unfinished draft");
	EXPECT_EQ(m_Animation.ActiveTrackCount(), TracksBefore);
}
