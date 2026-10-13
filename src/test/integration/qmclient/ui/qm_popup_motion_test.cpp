// 真实弹层栈与动画后端协作；fake 只替换设备、时钟和 GPU 提交。
#include <engine/keys.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmPopupMotion : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUiV2AnimationRuntime m_Animation;
		SPopupMenuId m_Popup;
		CUIRect m_Target{40, 40, 240, 140};
		SPopupMenuProperties m_Props;
		struct SProbe
		{
			CUi *m_pUi;
			CUIRect m_Content{};
			float m_Alpha = 0.0f;
			int m_Renders = 0;
		} m_Probe{&m_Ui};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			g_Config.m_QmUiMotionLevel = 2;
			g_Config.m_QmUiPopupColor = CConfig::ms_QmUiPopupColor;
			g_Config.m_QmUiPopupOpacity = CConfig::ms_QmUiPopupOpacity;
			g_Config.m_QmUiCardBorderColor = CConfig::ms_QmUiCardBorderColor;
			m_Ui.SetQmAnimationRuntime(&m_Animation);
			m_Props = ui_widget::SecondaryPanelProperties(ui_widget::ESecondaryPanelPlacement::ANCHORED);
			m_Props.m_AutoReposition = false;
			m_Props.m_ContentInset = 0.0f;
			Frame();
		}

		void Frame(vec2 Position = {0, 0}, bool Pressed = false, float Dt = 1.0f / 60.0f)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Animation.Advance(Dt);
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		static CUi::EPopupMenuFunctionResult Probe(void *pContext, CUIRect View, bool)
		{
			auto &State = *static_cast<SProbe *>(pContext);
			State.m_Content = View;
			State.m_Alpha = State.m_pUi->ControlAlphaScale();
			++State.m_Renders;
			return CUi::POPUP_KEEP_OPEN;
		}

		void Open()
		{
			m_Ui.DoPopupMenu(&m_Popup, m_Target.x, m_Target.y, m_Target.w, m_Target.h, &m_Probe, Probe, m_Props);
		}

		qm_ui_test::CRealUiTestGraphics &Graphics()
		{
			return *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
		}

		void Draw()
		{
			Graphics().m_vRoundedSubmissions.clear();
			m_Ui.RenderPopupMenus();
		}

		void Settle()
		{
			for(int Index = 0; Index < 30; ++Index)
			{
				Frame();
				Draw();
			}
		}
	};

	void ExpectSameRect(const CUIRect &Actual, const CUIRect &Expected)
	{
		EXPECT_FLOAT_EQ(Actual.x, Expected.x);
		EXPECT_FLOAT_EQ(Actual.y, Expected.y);
		EXPECT_FLOAT_EQ(Actual.w, Expected.w);
		EXPECT_FLOAT_EQ(Actual.h, Expected.h);
	}

	void ExpectSamePresentation(const IGraphics::SRoundedRectSdfParams &Left, const IGraphics::SRoundedRectSdfParams &Right)
	{
		EXPECT_EQ(Left.m_Rect, Right.m_Rect);
		EXPECT_EQ(Left.m_FillColor, Right.m_FillColor);
		EXPECT_EQ(Left.m_BorderColor, Right.m_BorderColor);
	}

	class QmPopupMotionWithoutClient : public qm_ui_test::CRealUiFixture
	{
	};
}

TEST_F(QmPopupMotion, ClosingDuringOpeningKeepsTheVisibleGeometryAndAlpha)
{
	Open();
	Frame();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	const auto Before = Graphics().m_vRoundedSubmissions.front();
	ASSERT_GT(m_Probe.m_Alpha, 0.0f);
	ASSERT_LT(m_Probe.m_Alpha, 1.0f);
	m_Ui.ClosePopupMenu(&m_Popup);
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Popup));
	EXPECT_TRUE(m_Ui.IsPopupVisible(&m_Popup));
	Settle();
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_Popup));
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmPopupMotion, ReopeningDuringClosingKeepsTheVisibleGeometryAndAlpha)
{
	Open();
	Settle();
	m_Ui.ClosePopupMenu(&m_Popup);
	Frame();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	const auto Before = Graphics().m_vRoundedSubmissions.front();
	ASSERT_GT(Before.m_FillColor.a, 0.0f);
	Open();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
	Settle();
	ExpectSameRect(m_Probe.m_Content, m_Target);
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
}

TEST_F(QmPopupMotion, InterruptedOpeningCarriesVelocityIntoTheNextFrame)
{
	SPopupMenuId ReferenceId;
	SProbe Reference{&m_Ui};
	m_Ui.DoPopupMenu(&ReferenceId, m_Target.x, m_Target.y, m_Target.w, m_Target.h, &Reference, Probe, m_Props);
	Open();
	Frame();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const float BeforeAlpha = Graphics().m_vRoundedSubmissions[1].m_FillColor.a;
	m_Ui.ClosePopupMenu(&m_Popup);
	// 用未中断的另一弹层比较同一短时间步，不复制后端的速度或弹簧算法。
	m_Animation.Advance(0.00005f);
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const auto &Interrupted = Graphics().m_vRoundedSubmissions[1];
	const auto &Uninterrupted = Graphics().m_vRoundedSubmissions[0];
	EXPECT_GT(Interrupted.m_FillColor.a, BeforeAlpha);
	EXPECT_NEAR(Interrupted.m_FillColor.a, Uninterrupted.m_FillColor.a, 0.0001f);
	EXPECT_NEAR(Interrupted.m_Rect.x, Uninterrupted.m_Rect.x, 0.001f);
	EXPECT_NEAR(Interrupted.m_Rect.w, Uninterrupted.m_Rect.w, 0.002f);
}

TEST_F(QmPopupMotion, CenteredPanelUsesItsVisibleBottomEdgeForOutsideClick)
{
	m_Props.m_CenterInViewport = true;
	Open();
	Frame();
	Draw();
	const CUIRect Visible = m_Probe.m_Content;
	const float TargetBottom = (m_Ui.Screen()->h + m_Target.h) * 0.5f;
	ASSERT_GT(Visible.y + Visible.h, TargetBottom);
	Frame({Visible.x + Visible.w * 0.5f, TargetBottom + 1.0f}, true, 0.0f);
	Draw();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
}

TEST_F(QmPopupMotion, DropdownUnionBorderAndFillContinueAcrossCloseAndReopen)
{
	CUi::SSelectionPopupContext Context;
	CScrollRegion Scroll;
	Context.m_pScrollRegion = &Scroll;
	Context.m_vEntries = {"First", "Second", "Third"};
	Context.m_Width = 240.0f;
	Context.m_AlignmentHeight = Context.m_EntryHeight = 24.0f;
	Context.m_Viewport = *m_Ui.Screen();
	m_Ui.ShowPopupSelection(40, 40, &Context);
	Frame();
	Draw();
	ASSERT_GE(Graphics().m_vRoundedSubmissions.size(), 2u);
	const auto Border = Graphics().m_vRoundedSubmissions[0];
	const auto Fill = Graphics().m_vRoundedSubmissions[1];
	ASSERT_GT(Fill.m_FillColor.a, 0.0f);
	m_Ui.ClosePopupMenu(&Context);
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	ExpectSamePresentation(Border, Graphics().m_vRoundedSubmissions[0]);
	ExpectSamePresentation(Fill, Graphics().m_vRoundedSubmissions[1]);
	m_Ui.ShowPopupSelection(40, 40, &Context);
	Draw();
	ASSERT_GE(Graphics().m_vRoundedSubmissions.size(), 2u);
	ExpectSamePresentation(Border, Graphics().m_vRoundedSubmissions[0]);
	ExpectSamePresentation(Fill, Graphics().m_vRoundedSubmissions[1]);
}

TEST_F(QmPopupMotion, LogicalCloseReleasesInputAndNotifiesOnlyOnceDuringExit)
{
	CButtonContainer Source;
	int Closed = 0;
	m_Props.m_OnClosed = [&]() { ++Closed; };
	m_Ui.SetKeyboardFocus(&Source);
	Open();
	Frame();
	Draw();
	const int Renders = m_Probe.m_Renders;
	m_Ui.ClosePopupMenu(&m_Popup);
	EXPECT_EQ(Closed, 1);
	EXPECT_TRUE(m_Ui.IsPopupVisible(&m_Popup));
	m_Ui.DoButtonLogic(&Source, 0, &m_Target, BUTTONFLAG_LEFT);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &Source);
	Draw();
	EXPECT_EQ(m_Probe.m_Renders, Renders);
	m_Ui.ClosePopupMenu(&m_Popup);
	Settle();
	EXPECT_EQ(Closed, 1);
}

TEST_F(QmPopupMotion, DisabledMotionShowsTheTargetAndClosesWithoutAnExitTrack)
{
	g_Config.m_QmUiMotionLevel = 0;
	Open();
	Draw();
	ExpectSameRect(m_Probe.m_Content, m_Target);
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	m_Ui.ClosePopupMenu(&m_Popup);
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_Popup));
}

TEST_F(QmPopupMotion, DisablingMotionFinishesTheLiveTrackAndDoesNotReplayItOnRestore)
{
	Open();
	Frame();
	Draw();
	ASSERT_LT(m_Probe.m_Alpha, 1.0f);
	g_Config.m_QmUiMotionLevel = 0;
	Draw();
	ExpectSameRect(m_Probe.m_Content, m_Target);
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	g_Config.m_QmUiMotionLevel = 2;
	Draw();
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmPopupMotion, DisablingMotionRemovesAClosingPanelOnItsNextDraw)
{
	Open();
	Settle();
	m_Ui.ClosePopupMenu(&m_Popup);
	g_Config.m_QmUiMotionLevel = 0;
	Draw();
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_Popup));
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmPopupMotion, ExplicitAnimationOptOutStaysImmediateAtFullMotion)
{
	m_Props.m_Animate = false;
	Open();
	Draw();
	ExpectSameRect(m_Probe.m_Content, m_Target);
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	m_Ui.ClosePopupMenu(&m_Popup);
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_Popup));
}

TEST_F(QmPopupMotion, ReducedMotionUsesTheExistingShortenedDuration)
{
	g_Config.m_QmUiMotionLevel = 1;
	Open();
	m_Animation.Advance(0.06f);
	Draw();
	ExpectSameRect(m_Probe.m_Content, m_Target);
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmPopupMotion, BackgroundReadsTheCurrentPresentationWithoutRetargetingTheLiveTrack)
{
	Open();
	Frame();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	const auto Before = Graphics().m_vRoundedSubmissions.front();
	ASSERT_EQ(m_Animation.ActiveTrackCount(), 1);
	const float Time = m_Animation.TimeSec();
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Draw();
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
		g_Config.m_QmUiMotionLevel = 0;
		Draw();
		EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
		g_Config.m_QmUiMotionLevel = 2;
		Draw();
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
		m_Ui.ClosePopupMenu(&m_Popup);
	}
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
	EXPECT_FLOAT_EQ(m_Animation.TimeSec(), Time);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
}

TEST_F(QmPopupMotion, MeasurementDoesNotCreateRedirectOrAdvancePopupTracks)
{
	Open();
	Frame();
	Draw();
	ASSERT_EQ(m_Animation.ActiveTrackCount(), 1);
	const float Time = m_Animation.TimeSec();
	SPopupMenuId Other;
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		m_Ui.DoPopupMenu(&Other, 0, 0, 100, 100, &m_Probe, Probe, m_Props);
		g_Config.m_QmUiMotionLevel = 0;
		Draw();
		EXPECT_TRUE(Graphics().m_vRoundedSubmissions.empty());
		m_Ui.ClosePopupMenu(&m_Popup);
	}
	g_Config.m_QmUiMotionLevel = 2;
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Other));
	EXPECT_FLOAT_EQ(m_Animation.TimeSec(), Time);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 1);
}

TEST_F(QmPopupMotionWithoutClient, ImmediatePopupDoesNotRequireAClientClock)
{
	SPopupMenuId Popup;
	const CUIRect Target{40, 40, 240, 140};
	m_Ui.DoPopupMenu(&Popup, Target.x, Target.y, Target.w, Target.h, nullptr,
		[](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; });
	m_Ui.RenderPopupMenus();
	ASSERT_NE(m_Ui.GetPopupMenuRect(&Popup), nullptr);
	ExpectSameRect(*m_Ui.GetPopupMenuRect(&Popup), Target);
	m_Ui.ClosePopupMenu(&Popup);
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Popup));
}

TEST_F(QmPopupMotion, StandaloneUiUsesTheSameInterruptibleBackendWithoutDoubleAdvancing)
{
	m_Ui.SetQmAnimationRuntime(nullptr);
	Open();
	Frame();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	const auto Before = Graphics().m_vRoundedSubmissions.front();
	m_Ui.ClosePopupMenu(&m_Popup);
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
	Open();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
	Settle();
	EXPECT_FLOAT_EQ(m_Probe.m_Alpha, 1.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmPopupMotion, AnimatedChildRestoresLiveParentTextBeforeItsExitFinishes)
{
	struct SParent
	{
		CUi *m_pUi;
		CLineInputBuffered<64> m_Input;
		SPopupMenuId m_Child;
		bool m_OpenChild = false;
	} Parent{&m_Ui};
	Parent.m_Input.Set("parent draft");
	m_Ui.DoPopupMenu(&m_Popup, m_Target.x, m_Target.y, m_Target.w, m_Target.h, &Parent, [](void *pContext, CUIRect View, bool Active) {
		auto &State = *static_cast<SParent *>(pContext);
		CUIRect Edit;
		View.HSplitTop(24.0f, &Edit, nullptr);
		if(Active)
			State.m_pUi->DoEditBox(&State.m_Input, &Edit, 12);
		else
		{
			CUiScopedBackgroundRender Background(State.m_pUi);
			State.m_pUi->DoEditBox(&State.m_Input, &Edit, 12);
		}
		if(Active && State.m_OpenChild)
		{
			State.m_OpenChild = false;
			auto Props = ui_widget::SecondaryPanelProperties();
			State.m_pUi->DoPopupMenu(&State.m_Child, 0, 0, 100, 80, nullptr,
				[](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
		}
		return CUi::POPUP_KEEP_OPEN;
	}, m_Props);
	m_Ui.SetActiveItem(&Parent.m_Input);
	Draw();
	ASSERT_TRUE(Parent.m_Input.IsActive());
	Parent.m_OpenChild = true;
	Frame();
	Draw();
	ASSERT_FALSE(Parent.m_Input.IsActive());
	Frame();
	Draw();
	ASSERT_TRUE(m_Ui.CloseTopPopupMenu());
	ASSERT_TRUE(m_Ui.IsPopupVisible(&Parent.m_Child));
	Draw();
	EXPECT_TRUE(Parent.m_Input.IsActive());
	EXPECT_STREQ(Parent.m_Input.GetString(), "parent draft");
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Popup));
	EXPECT_TRUE(m_Ui.IsPopupVisible(&Parent.m_Child));
	m_Ui.ClosePopupMenus(false);
	EXPECT_FALSE(Parent.m_Input.IsActive());
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmPopupMotionWithoutClient, RequestedAnimationFallsBackToImmediateWithoutAnyClock)
{
	g_Config.m_QmUiMotionLevel = 2;
	SPopupMenuId Popup;
	const CUIRect Target{40, 40, 240, 140};
	auto Props = ui_widget::SecondaryPanelProperties(ui_widget::ESecondaryPanelPlacement::ANCHORED);
	Props.m_ContentInset = 0.0f;
	float Alpha = -1.0f;
	struct SProbe
	{
		CUi *m_pUi;
		float *m_pAlpha;
	} Probe{&m_Ui, &Alpha};
	m_Ui.DoPopupMenu(&Popup, Target.x, Target.y, Target.w, Target.h, &Probe,
		[](void *pContext, CUIRect, bool) {
			auto &State = *static_cast<SProbe *>(pContext);
			*State.m_pAlpha = State.m_pUi->ControlAlphaScale();
			return CUi::POPUP_KEEP_OPEN;
		}, Props);
	m_Ui.RenderPopupMenus();
	ASSERT_NE(m_Ui.GetPopupMenuRect(&Popup), nullptr);
	ExpectSameRect(*m_Ui.GetPopupMenuRect(&Popup), Target);
	EXPECT_FLOAT_EQ(Alpha, 1.0f);
	m_Ui.ClosePopupMenu(&Popup);
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Popup));
}

TEST_F(QmPopupMotion, StandaloneBackgroundTimeDoesNotBurstForwardOnRestore)
{
	m_Ui.SetQmAnimationRuntime(nullptr);
	Open();
	Frame();
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	const auto Before = Graphics().m_vRoundedSubmissions.front();
	ASSERT_GT(m_Probe.m_Alpha, 0.0f);
	ASSERT_LT(m_Probe.m_Alpha, 1.0f);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		for(int Index = 0; Index < 60; ++Index)
		{
			Frame();
			Draw();
		}
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
		ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
	}
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);
	ExpectSamePresentation(Before, Graphics().m_vRoundedSubmissions.front());
	Frame();
	Draw();
	EXPECT_GT(Graphics().m_vRoundedSubmissions.front().m_FillColor.a, Before.m_FillColor.a);
	EXPECT_LT(m_Probe.m_Alpha, 1.0f);
}
