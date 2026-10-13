// 公共导航与真实 UI、动画后端协作；fake 只替换输入设备、客户端时钟与 GPU。
#include <engine/keys.h>

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/UiNavigation.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <array>

namespace
{
	class QmNavigationSelection : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUiV2AnimationRuntime m_Animation;
		const std::array<CUIRect, 3> m_aSlots{{{20, 20, 80, 24}, {108, 20, 80, 24}, {196, 20, 80, 24}}};
		ui_widget::SCapsuleTabBarStyle m_Style;
		int m_Selected = 0;
		SPopupMenuId m_Popup;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			g_Config.m_QmUiMotionLevel = 2;
			Frame({0, 0}, false);
			Draw();
		}

		void Frame(vec2 Position, bool Pressed, float Dt = 1.0f / 60.0f)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Animation.Advance(Dt);
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		vec2 SlotCenter(int Index) const
		{
			const CUIRect &Slot = m_aSlots[Index];
			return {Slot.x + Slot.w * 0.5f, Slot.y + Slot.h * 0.5f};
		}

		qm_ui_test::CRealUiTestGraphics &Graphics()
		{
			return *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
		}

		IUiContext Context(CUiV2AnimationRuntime *pAnimation = nullptr)
		{
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pAnim = pAnimation != nullptr ? pAnimation : &m_Animation;
			return Ctx;
		}

		int Draw(CUiV2AnimationRuntime *pAnimation = nullptr)
		{
			Graphics().m_vRoundedSubmissions.clear();
			m_Selected = ui_widget::CapsuleTabBar(Context(pAnimation), 1234, m_aSlots.data(), static_cast<int>(m_aSlots.size()), m_Selected, m_Style);
			return m_Selected;
		}

		void DrawNested(const CUIRect &MainSlot, const CUIRect &SubSlot)
		{
			Graphics().m_vRoundedSubmissions.clear();
			ui_widget::SNestedSegmentStyle Style;
			ui_widget::NestedSegmentChrome(Context(), 5678, {20, 60, 300, 80}, &MainSlot, &SubSlot, Style);
		}
	};
}

TEST_F(QmNavigationSelection, PressReturnsNewSelectionAndStartsIndicatorOnTheSameFrame)
{
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const float InitialX = Graphics().m_vRoundedSubmissions.back().m_Rect.x;
	ASSERT_EQ(m_Animation.ActiveTrackCount(), 0);

	Frame(SlotCenter(1), true);
	EXPECT_EQ(Draw(), 1);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	EXPECT_FLOAT_EQ(Graphics().m_vRoundedSubmissions.back().m_Rect.x, InitialX);
	EXPECT_GT(m_Animation.ActiveTrackCount(), 0);

	Frame(SlotCenter(1), false);
	EXPECT_EQ(Draw(), 1);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const float MovingX = Graphics().m_vRoundedSubmissions.back().m_Rect.x;
	EXPECT_GT(MovingX, InitialX);
	EXPECT_LT(MovingX, m_aSlots[1].x + m_Style.m_IndicatorInset);
}

TEST_F(QmNavigationSelection, HoldingMouseWhileDraggingAcrossTabsKeepsPressedSelection)
{
	Frame(SlotCenter(1), true);
	ASSERT_EQ(Draw(), 1);
	Frame(SlotCenter(2), true);
	EXPECT_EQ(Draw(), 1);
}

TEST_F(QmNavigationSelection, ReleasingOverAnotherTabDoesNotSubmitAnotherSelection)
{
	Frame(SlotCenter(1), true);
	ASSERT_EQ(Draw(), 1);
	Frame(SlotCenter(2), false);
	EXPECT_EQ(Draw(), 1);
}

TEST_F(QmNavigationSelection, HoldingAnOutsidePressWhileEnteringTabsDoesNotSelect)
{
	Frame({400, 80}, true);
	ASSERT_EQ(Draw(), 0);
	Frame(SlotCenter(1), true);
	EXPECT_EQ(Draw(), 0);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmNavigationSelection, ClippedPressDoesNotSelectAndFreshVisiblePressRecovers)
{
	Frame(SlotCenter(1), true);
	m_Ui.ClipEnable(&m_aSlots[0]);
	EXPECT_EQ(Draw(), 0);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	m_Ui.ClipDisable();

	Frame(SlotCenter(1), false);
	EXPECT_EQ(Draw(), 0);
	Frame(SlotCenter(1), true);
	EXPECT_EQ(Draw(), 1);
}

TEST_F(QmNavigationSelection, BackgroundRenderLeavesPressForTheInteractivePass)
{
	Frame(SlotCenter(1), true);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_EQ(Draw(), 0);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	}
	EXPECT_EQ(Draw(), 1);
	EXPECT_GT(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmNavigationSelection, MeasurementDoesNotSelectOrStartIndicatorBeforeTheInteractivePass)
{
	Frame(SlotCenter(1), true);
	{
		CUiScopedMeasurement Measurement(&m_Ui);
		EXPECT_EQ(Draw(), 0);
		EXPECT_TRUE(Graphics().m_vRoundedSubmissions.empty());
		EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	}
	EXPECT_EQ(Draw(), 1);
	EXPECT_GT(m_Animation.ActiveTrackCount(), 0);
}

TEST_F(QmNavigationSelection, BlockingPopupPreventsUnderlyingSelectionUntilClosed)
{
	SPopupMenuProperties Props;
	Props.m_Animate = false;
	Props.m_CenterInViewport = false;
	Props.m_AutoReposition = false;
	Props.m_BlockUnderlyingPointerInput = true;
	m_Ui.DoPopupMenu(&m_Popup, 400, 120, 100, 80, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Popup));

	Frame(SlotCenter(1), true);
	EXPECT_EQ(Draw(), 0);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	m_Ui.ClosePopupMenus();
	Frame(SlotCenter(1), false);
	EXPECT_EQ(Draw(), 0);
	Frame(SlotCenter(1), true);
	EXPECT_EQ(Draw(), 1);
}

TEST_F(QmNavigationSelection, ReversePressRetargetsFromTheCurrentIndicatorPosition)
{
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const float InitialX = Graphics().m_vRoundedSubmissions.back().m_Rect.x;
	Frame(SlotCenter(1), true);
	ASSERT_EQ(Draw(), 1);
	Frame(SlotCenter(1), false);
	ASSERT_EQ(Draw(), 1);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const float MovingX = Graphics().m_vRoundedSubmissions.back().m_Rect.x;
	ASSERT_GT(MovingX, InitialX);

	// 同一动画时刻反向按下，选择立即变化，滑块不能跳回旧槽或目标槽。
	Frame(SlotCenter(0), true, 0.0f);
	EXPECT_EQ(Draw(), 0);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	EXPECT_FLOAT_EQ(Graphics().m_vRoundedSubmissions.back().m_Rect.x, MovingX);
	EXPECT_GT(m_Animation.ActiveTrackCount(), 0);
	for(int FrameIndex = 0; FrameIndex < 30; ++FrameIndex)
	{
		Frame(SlotCenter(0), true);
		EXPECT_EQ(Draw(), 0);
	}
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	EXPECT_LT(Graphics().m_vRoundedSubmissions.back().m_Rect.x, MovingX);
}

TEST_F(QmNavigationSelection, ReducedMotionBackgroundKeepsMovingIndicatorAndForegroundRestoresContinuously)
{
	g_Config.m_QmUiMotionLevel = 1;
	Frame(SlotCenter(1), true);
	ASSERT_EQ(Draw(), 1);
	Frame(SlotCenter(1), false);
	ASSERT_EQ(Draw(), 1);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const float MovingX = Graphics().m_vRoundedSubmissions.back().m_Rect.x;
	ASSERT_GT(MovingX, m_aSlots[0].x + m_Style.m_IndicatorInset);
	ASSERT_LT(MovingX, m_aSlots[1].x + m_Style.m_IndicatorInset);
	const int TrackCount = m_Animation.ActiveTrackCount();

	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_EQ(Draw(), 1);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
		EXPECT_FLOAT_EQ(Graphics().m_vRoundedSubmissions.back().m_Rect.x, MovingX);
		EXPECT_EQ(m_Animation.ActiveTrackCount(), TrackCount);
	}
	EXPECT_EQ(Draw(), 1);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	EXPECT_FLOAT_EQ(Graphics().m_vRoundedSubmissions.back().m_Rect.x, MovingX);
	Frame(SlotCenter(1), false);
	Draw();
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	EXPECT_GT(Graphics().m_vRoundedSubmissions.back().m_Rect.x, MovingX);
}

TEST_F(QmNavigationSelection, BackgroundLayoutChangesDoNotRedirectTheInteractiveTrajectory)
{
	CUiV2AnimationRuntime Reference;
	Draw(&Reference);
	Frame(SlotCenter(1), true);
	Reference.Advance(1.0f / 60.0f);
	ASSERT_EQ(Draw(), 1);
	Draw(&Reference);
	Frame(SlotCenter(1), false);
	Reference.Advance(1.0f / 60.0f);
	Draw();
	Draw(&Reference);
	auto ShiftedSlots = m_aSlots;
	for(CUIRect &Slot : ShiftedSlots)
	{
		Slot.x += 40.0f;
		Slot.w += 10.0f;
	}

	{
		CUiScopedBackgroundRender Background(&m_Ui);
		ui_widget::CapsuleTabBar(Context(), 1234, ShiftedSlots.data(), static_cast<int>(ShiftedSlots.size()), m_Selected, m_Style);
		// 时钟仍由后端推进，背景布局不能成为新的动画目标。
		m_Animation.Advance(1.0f / 60.0f);
		Reference.Advance(1.0f / 60.0f);
	}
	for(int FrameIndex = 0; FrameIndex < 4; ++FrameIndex)
	{
		SCOPED_TRACE(FrameIndex);
		Draw();
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
		const vec4 Actual = Graphics().m_vRoundedSubmissions.back().m_Rect;
		Draw(&Reference);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
		const vec4 Expected = Graphics().m_vRoundedSubmissions.back().m_Rect;
		EXPECT_FLOAT_EQ(Actual.x, Expected.x);
		EXPECT_FLOAT_EQ(Actual.z, Expected.z);
		m_Animation.Advance(1.0f / 60.0f);
		Reference.Advance(1.0f / 60.0f);
	}
}

TEST_F(QmNavigationSelection, FirstBackgroundAppearanceDrawsSelectionWithoutInitializingTracks)
{
	constexpr uint64_t GroupId = 4321;
	const uint64_t NodeKey = BuildUiAnimNodeKey(GroupId, 0);
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		Graphics().m_vRoundedSubmissions.clear();
		ui_widget::CapsuleTabBar(Context(), GroupId, m_aSlots.data(), static_cast<int>(m_aSlots.size()), 1, m_Style);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
		EXPECT_FLOAT_EQ(Graphics().m_vRoundedSubmissions.back().m_Rect.x, m_aSlots[1].x + m_Style.m_IndicatorInset);
	}
	EXPECT_FLOAT_EQ(m_Animation.GetValue(NodeKey, EUiAnimProperty::POS_X, -1000.0f), -1000.0f);
	EXPECT_FLOAT_EQ(m_Animation.GetValue(NodeKey, EUiAnimProperty::WIDTH, -1000.0f), -1000.0f);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	EXPECT_EQ(m_Animation.QueuedTrackCount(), 0);
}

TEST_F(QmNavigationSelection, DisabledMotionDrawsTargetInBackgroundAndCancelsOnInteractiveRender)
{
	Frame(SlotCenter(1), true);
	ASSERT_EQ(Draw(), 1);
	Frame(SlotCenter(1), false);
	Draw();
	ASSERT_GT(m_Animation.ActiveTrackCount(), 0);
	g_Config.m_QmUiMotionLevel = 0;
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_EQ(Draw(), 1);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
		EXPECT_FLOAT_EQ(Graphics().m_vRoundedSubmissions.back().m_Rect.x, m_aSlots[1].x + m_Style.m_IndicatorInset);
	}
	EXPECT_EQ(Draw(), 1);
	EXPECT_EQ(m_Animation.ActiveTrackCount(), 0);
	EXPECT_EQ(m_Animation.QueuedTrackCount(), 0);
}

TEST_F(QmNavigationSelection, NestedIndicatorsKeepEveryMovingChannelInBackground)
{
	DrawNested({20, 60, 120, 36}, {24, 64, 48, 28});
	const CUIRect MainTarget{180, 68, 140, 44};
	const CUIRect SubTarget{240, 74, 72, 32};
	DrawNested(MainTarget, SubTarget);
	Frame({0, 0}, false);
	DrawNested(MainTarget, SubTarget);
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 3u);
	const auto Before = Graphics().m_vRoundedSubmissions;
	ASSERT_LT(Before[1].m_Rect.x, MainTarget.x + 2.0f);
	ASSERT_LT(Before[2].m_Rect.x, SubTarget.x + 3.0f);
	const int TrackCount = m_Animation.ActiveTrackCount();

	{
		CUiScopedBackgroundRender Background(&m_Ui);
		DrawNested(MainTarget, SubTarget);
		ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), Before.size());
		for(size_t Index = 1; Index < Before.size(); ++Index)
		{
			SCOPED_TRACE(Index);
			const vec4 &Expected = Before[Index].m_Rect;
			const vec4 &Actual = Graphics().m_vRoundedSubmissions[Index].m_Rect;
			EXPECT_FLOAT_EQ(Actual.x, Expected.x);
			EXPECT_FLOAT_EQ(Actual.y, Expected.y);
			EXPECT_FLOAT_EQ(Actual.z, Expected.z);
			EXPECT_FLOAT_EQ(Actual.w, Expected.w);
		}
		EXPECT_EQ(m_Animation.ActiveTrackCount(), TrackCount);
	}
}
