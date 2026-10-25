// 公共下拉入口与弹层栈、滚轮归属的跨帧协作；不复制控件状态机或接线逻辑。
// 此文件需要独立链接真实 UI，不能与 ui_rect_test_helpers.cpp 的 UI 桩共存。
#include <engine/keys.h>

#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <array>

namespace
{
	class QmDropdownPopupWheelEntry : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUi::SDropDownState m_State;
		CScrollRegion m_ScrollRegion;
		SPopupMenuId m_ParentId;
		int m_PageOwner = 0;
		CUIRect m_Anchor{-10.0f, -24.0f, 100.0f, 24.0f};
		CUIRect m_Viewport{-100.0f, -100.0f, 400.0f, 400.0f};
		std::array<const char *, 10> m_aEntries{"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};

		static CUi::EPopupMenuFunctionResult KeepOpen(void *, CUIRect, bool)
		{
			return CUi::POPUP_KEEP_OPEN;
		}

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(1, -120.0f, false));
		}

		void OpenChild(int Count = 10)
		{
			SPopupMenuProperties ParentProps;
			ParentProps.m_AutoReposition = false;
			m_Ui.DoPopupMenu(&m_ParentId, m_Viewport.x, m_Viewport.y, m_Viewport.w, m_Viewport.h, nullptr, KeepOpen, ParentProps);
			auto &Context = m_State.m_SelectionPopupContext;
			Context.Reset();
			Context.m_pScrollRegion = &m_ScrollRegion;
			Context.m_Width = m_Anchor.w;
			Context.m_AlignmentHeight = m_Anchor.h;
			Context.m_EntryHeight = 20.0f;
			Context.m_EntrySpacing = 0.0f;
			Context.m_Viewport = m_Viewport;
			Context.m_ActiveIndex = Count - 1;
			Context.m_Props.m_RequireSourceRefresh = true;
			Context.m_Props.m_SourceFrame = m_Ui.PopupSourceFrame();
			Context.m_vEntries.assign(m_aEntries.begin(), m_aEntries.begin() + Count);
			SQmDropdownInput Open;
			Open.m_TogglePressed = true;
			Open.m_InitialIndex = Count - 1;
			ASSERT_TRUE(m_State.m_DropDownState.Update(Open, Count).m_Opened);
			m_Ui.ShowPopupSelection(m_Anchor.x, m_Anchor.y, &Context);
			ASSERT_TRUE(m_Ui.IsPopupOpen(&Context));
			ASSERT_NE(m_Ui.GetPopupMenuRect(&Context), nullptr);
		}

		int DrawInactiveParent(int Count = 10)
		{
			CUi::SDropDownProperties Props;
			Props.m_Enabled = false;
			Props.m_ClosePopupWhenDisabled = false;
			Props.m_pAnchorViewport = &m_Viewport;
			Props.m_pPopupViewport = &m_Viewport;
			return m_Ui.DoDropDown(&m_Anchor, Count - 1, m_aEntries.data(), Count, m_State, Props);
		}

		void RegisterPageOwner()
		{
			m_Ui.RegisterWheelOwner(&m_PageOwner, EUiWheelOwnerPriority::PAGE, m_Viewport, true);
		}
	};

	TEST_F(QmDropdownPopupWheelEntry, DisabledParentRenewsChildOwnerOnEveryFrame)
	{
		OpenChild();
		ASSERT_TRUE(m_State.m_SelectionPopupContext.m_Scrollable);
		const CUIRect Original = *m_Ui.GetPopupMenuRect(&m_State.m_SelectionPopupContext);
		for(uint64_t Frame = 2; Frame <= 4; ++Frame)
		{
			SCOPED_TRACE(Frame);
			ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(Frame, -120.0f, false));
			float Delta = 0.0f;
			ASSERT_FALSE(m_Ui.TryConsumeWheel(&m_State.m_SelectionPopupContext, &Delta));
			RegisterPageOwner();
			EXPECT_EQ(DrawInactiveParent(), 9);
			EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
			ASSERT_TRUE(m_Ui.TryConsumeWheel(&m_State.m_SelectionPopupContext, &Delta));
			EXPECT_FLOAT_EQ(Delta, -120.0f);
			EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_State.m_SelectionPopupContext, &Delta));
			EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
			ASSERT_TRUE(m_Ui.IsPopupOpen(&m_ParentId));
			ASSERT_TRUE(m_Ui.IsPopupOpen(&m_State.m_SelectionPopupContext));
			const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_State.m_SelectionPopupContext);
			EXPECT_FLOAT_EQ(pPopup->x, Original.x);
			EXPECT_FLOAT_EQ(pPopup->y, Original.y);
			EXPECT_FLOAT_EQ(pPopup->w, Original.w);
			EXPECT_FLOAT_EQ(pPopup->h, Original.h);
			EXPECT_EQ(m_State.m_SelectionPopupContext.m_ActiveIndex, 9);
			EXPECT_EQ(m_State.m_SelectionPopupContext.m_SelectionIndex, -1);
			EXPECT_EQ(m_State.m_SelectionPopupContext.m_pScrollRegion, &m_ScrollRegion);
		}
	}

	TEST_F(QmDropdownPopupWheelEntry, ShortChildStillBlocksPageWheel)
	{
		OpenChild(3);
		ASSERT_FALSE(m_State.m_SelectionPopupContext.m_Scrollable);
		ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(2, 120.0f, true));
		RegisterPageOwner();
		EXPECT_EQ(DrawInactiveParent(3), 2);
		float Delta = 0.0f;
		EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
		ASSERT_TRUE(m_Ui.TryConsumeWheel(&m_State.m_SelectionPopupContext, &Delta));
		EXPECT_FLOAT_EQ(Delta, 360.0f);
		EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
	}

	TEST_F(QmDropdownPopupWheelEntry, UsesLivePopupRectInsteadOfOldSourceGeometry)
	{
		OpenChild();
		auto &Context = m_State.m_SelectionPopupContext;
		m_Ui.DoPopupMenu(&Context, 100.0f, 100.0f, 100.0f, 170.0f, &Context, KeepOpen, Context.m_Props);
		ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(2, -120.0f, false));
		RegisterPageOwner();
		EXPECT_EQ(DrawInactiveParent(), 9);
		float Delta = 0.0f;
		EXPECT_FALSE(m_Ui.TryConsumeWheel(&Context, &Delta));
		EXPECT_TRUE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
	}

	TEST_F(QmDropdownPopupWheelEntry, CancellationReleasesOwnerAndReopeningRenewsIt)
	{
		OpenChild();
		m_Ui.ClosePopupMenus();
		ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(2, -120.0f, false));
		RegisterPageOwner();
		EXPECT_EQ(DrawInactiveParent(), 9);
		EXPECT_FALSE(m_State.m_DropDownState.IsOpen());
		float Delta = 0.0f;
		EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_State.m_SelectionPopupContext, &Delta));
		ASSERT_TRUE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
		ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(3, -120.0f, false));
		OpenChild();
		ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(4, -120.0f, false));
		RegisterPageOwner();
		EXPECT_EQ(DrawInactiveParent(), 9);
		EXPECT_FALSE(m_Ui.TryConsumeWheel(&m_PageOwner, &Delta));
		EXPECT_TRUE(m_Ui.TryConsumeWheel(&m_State.m_SelectionPopupContext, &Delta));
		EXPECT_EQ(m_State.m_SelectionPopupContext.m_SelectionIndex, -1);
	}

	// 弹层内的下拉：父层（二级菜单）失焦后公共入口只走禁用分支续注册，滚轮仍必须
	// 由列表消费。走真实 OnInput → Update → RenderPopupMenus 帧路径，而不是只调
	// DoDropDown，才能覆盖父弹层与子列表两层同时存在的渲染上下文。
	class QmNestedDropdownWheel : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestUiClient m_Client;
		qm_ui_test::CTestInput m_Input;
		CUi::SDropDownState m_State;
		CScrollRegion m_ScrollRegion;
		SPopupMenuId m_ParentId;
		CUIRect m_Viewport{};
		CUIRect m_ParentRect{};
		CUIRect m_Anchor{};
		bool m_ParentActive = false;
		std::array<const char *, 11> m_aEntries{"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten"};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			// 关闭惯性：偏移变化只能来自本帧真实消费的滚轮增量。
			g_Config.m_UiSmoothScrollTime = 0;
			m_Viewport = *m_Ui.Screen();
			m_ParentRect = {m_Viewport.x + 40.0f, m_Viewport.y + 40.0f, 420.0f, 260.0f};
			m_Anchor = {m_ParentRect.x + 180.0f, m_ParentRect.y + 40.0f, 180.0f, 24.0f};
			MoveMouse({m_Anchor.x + m_Anchor.w * 0.5f, m_Anchor.y + m_Anchor.h * 0.5f});
		}

		void MoveMouse(vec2 Position)
		{
			const auto *pScreen = m_Ui.Screen();
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / pScreen->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
		}

		static CUi::EPopupMenuFunctionResult ParentBody(void *pContext, CUIRect View, bool Active)
		{
			auto *pThis = static_cast<QmNestedDropdownWheel *>(pContext);
			pThis->m_ParentActive = Active;
			CUi::SDropDownProperties Props;
			// 子列表打开后父层失焦：与二级菜单里的下拉一致，只禁用触发器、保活子层。
			Props.m_Enabled = Active;
			Props.m_ClosePopupWhenDisabled = false;
			Props.m_pAnchorViewport = &pThis->m_Viewport;
			Props.m_pPopupViewport = &pThis->m_Viewport;
			pThis->m_Ui.DoDropDown(&pThis->m_Anchor, 0, pThis->m_aEntries.data(), (int)pThis->m_aEntries.size(), pThis->m_State, Props);
			return CUi::POPUP_KEEP_OPEN;
		}

		void OpenParent()
		{
			SPopupMenuProperties Props;
			Props.m_AutoReposition = false;
			Props.m_Animate = false;
			Props.m_BlockUnderlyingPointerInput = true;
			Props.m_BlockUnderlyingScroll = true;
			m_Ui.DoPopupMenu(&m_ParentId, m_ParentRect.x, m_ParentRect.y, m_ParentRect.w, m_ParentRect.h, this, ParentBody, Props);
		}

		void OpenList()
		{
			auto &Context = m_State.m_SelectionPopupContext;
			Context.Reset();
			Context.m_pScrollRegion = &m_ScrollRegion;
			Context.m_Width = m_Anchor.w;
			Context.m_AlignmentHeight = m_Anchor.h;
			Context.m_EntryHeight = m_Anchor.h;
			Context.m_EntrySpacing = 0.0f;
			Context.m_Viewport = m_Viewport;
			Context.m_ActiveIndex = 0;
			Context.m_vEntries.assign(m_aEntries.begin(), m_aEntries.end());
			SQmDropdownInput Open;
			Open.m_TogglePressed = true;
			Open.m_InitialIndex = 0;
			ASSERT_TRUE(m_State.m_DropDownState.Update(Open, (int)m_aEntries.size()).m_Opened);
			m_Ui.ShowPopupSelection(m_Anchor.x, m_Anchor.y, &Context);
			ASSERT_TRUE(m_Ui.IsPopupOpen(&Context));
		}

		void Frame(int WheelKey = 0)
		{
			m_Client.AdvanceFrame();
			m_Ui.SetEnabled(true);
			if(WheelKey != 0)
			{
				IInput::CEvent Event{};
				Event.m_Key = WheelKey;
				Event.m_Flags = IInput::FLAG_PRESS;
				ASSERT_TRUE(m_Ui.OnInput(Event));
			}
			m_Ui.Update();
			m_Ui.RenderPopupMenus();
		}
	};

	TEST_F(QmNestedDropdownWheel, PopupHostedListScrollsWithWheelAfterParentLosesFocus)
	{
		OpenParent();
		Frame();
		OpenList();
		Frame();
		ASSERT_FALSE(m_ParentActive);
		const CUIRect *pList = m_Ui.GetPopupMenuRect(&m_State.m_SelectionPopupContext);
		ASSERT_NE(pList, nullptr);
		ASSERT_TRUE(m_ScrollRegion.ContentOverflows());
		MoveMouse({pList->x + pList->w * 0.5f, pList->y + pList->h * 0.5f});
		Frame();
		Frame(KEY_MOUSE_WHEEL_DOWN);
		EXPECT_TRUE(m_ScrollRegion.WheelConsumedThisFrame());
		EXPECT_GT(m_ScrollRegion.State().Offset(), 0.0f);
	}

	TEST_F(QmDropdownPopupWheelEntry, InputStageCloseDismissesOnlyTheTopPopupThenTheParent)
	{
		OpenChild();
		EXPECT_TRUE(m_Ui.CloseTopPopupMenu());
		EXPECT_TRUE(m_Ui.IsPopupOpen(&m_ParentId));
		EXPECT_FALSE(m_Ui.IsPopupOpen(&m_State.m_SelectionPopupContext));
		// 控件在下一次绘制同步外部关闭，再走正常重开事件。
		EXPECT_EQ(DrawInactiveParent(), 9);
		EXPECT_FALSE(m_State.m_DropDownState.IsOpen());
		EXPECT_TRUE(m_Ui.CloseTopPopupMenu());
		EXPECT_FALSE(m_Ui.IsPopupOpen());
		EXPECT_FALSE(m_Ui.CloseTopPopupMenu());
		OpenChild();
		EXPECT_TRUE(m_Ui.IsPopupOpen(&m_ParentId));
	}
}
