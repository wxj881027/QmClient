// 公共导航与真实 UI、动画后端协作；fake 只替换输入设备、客户端时钟与 GPU。
#include <engine/keys.h>

#include <game/client/QmUi/UiNavigation.h>
#include <game/client/ui_listbox.h>

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

		int Draw()
		{
			Graphics().m_vRoundedSubmissions.clear();
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pAnim = &m_Animation;
			m_Selected = ui_widget::CapsuleTabBar(Ctx, 1234, m_aSlots.data(), static_cast<int>(m_aSlots.size()), m_Selected, m_Style);
			return m_Selected;
		}

		// 没有选中槽位时的绘制入口：整页切到不属于这组页签的页面就是这种状态。
		void DrawWithoutActiveSlot()
		{
			Graphics().m_vRoundedSubmissions.clear();
			IUiContext Ctx;
			Ctx.m_pUi = &m_Ui;
			Ctx.m_pAnim = &m_Animation;
			ui_widget::CapsuleTabBarChrome(Ctx, 1234, m_aSlots.data(), static_cast<int>(m_aSlots.size()), -1, m_Style);
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

// 列表策略通过真实列表框与滚动区验证，不以函数名或源码分支证明初始位置。
TEST_F(QmNavigationSelection, SavedListSelectionCanStayAtTopUntilExplicitlyRevealed)
{
	CListBox List;
	std::array<int, 60> Ids{};
	CUIElementBase::Init(&m_Ui);
	g_Config.m_UiSmoothScrollTime = 0;
	g_Config.m_QmUiListEntryAnimations = 0;
	const auto DrawList = [&](int Selected) {
		Frame({400, 400}, false);
		CUIRect View{20, 20, 200, 100};
		List.SetActive(false);
		List.DoStart(20, static_cast<int>(Ids.size()), 1, 3, Selected, &View, false);
		for(auto &Id : Ids)
			List.DoNextItem(&Id);
		List.DoEnd();
	};
	List.SuppressInitialSelectionScroll();
	DrawList(-1);
	// 异步列表稍后出现保存的地址，仍然不能自动跳到靠后的行。
	DrawList(45);
	DrawList(45);
	EXPECT_FLOAT_EQ(List.ScrollOffsetY(), 0.0f);
	List.ScrollToSelected();
	DrawList(45);
	DrawList(45);
	EXPECT_GT(List.ScrollOffsetY(), 0.0f);
}

TEST_F(QmNavigationSelection, HidingActiveSlotFadesIndicatorInPlaceAndBack)
{
	// 进到不属于这组页签的页面时滑块不该瞬间消失，而是停在上一次的位置淡出；
	// 回到这组页签时再从透明淡回来。
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const IGraphics::SRoundedRectSdfParams Initial = Graphics().m_vRoundedSubmissions.back();
	ASSERT_GT(Initial.m_FillColor.a, 0.9f);

	// 起的是补间轨道：这一帧先提交目标值，下一帧才开始看到变化。
	Frame({0, 0}, false);
	DrawWithoutActiveSlot();
	Frame({0, 0}, false);
	DrawWithoutActiveSlot();
	// 与原位一致，只是变淡。
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const IGraphics::SRoundedRectSdfParams Fading = Graphics().m_vRoundedSubmissions.back();
	EXPECT_LT(Fading.m_FillColor.a, Initial.m_FillColor.a);
	EXPECT_FLOAT_EQ(Fading.m_Rect.x, Initial.m_Rect.x);
	EXPECT_FLOAT_EQ(Fading.m_Rect.z, Initial.m_Rect.z);

	// 淡出结束后不再提交滑块，只留容器。
	for(int Step = 0; Step < 15; ++Step)
	{
		Frame({0, 0}, false);
		DrawWithoutActiveSlot();
	}
	EXPECT_EQ(Graphics().m_vRoundedSubmissions.size(), 1u);

	// 重新选中后从透明淡入，落点就是目标槽位。
	Frame({0, 0}, false);
	EXPECT_EQ(Draw(), 0);
	for(int Step = 0; Step < 15; ++Step)
	{
		Frame({0, 0}, false);
		Draw();
	}
	ASSERT_EQ(Graphics().m_vRoundedSubmissions.size(), 2u);
	const IGraphics::SRoundedRectSdfParams Restored = Graphics().m_vRoundedSubmissions.back();
	EXPECT_GT(Restored.m_FillColor.a, 0.9f);
	EXPECT_NEAR(Restored.m_Rect.x, m_aSlots[0].x + m_Style.m_IndicatorInset, 0.5f);
}
