// 真实控件入口的输入与关闭规则；fake 只替换输入设备、时钟及 GPU。
#include <engine/keys.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiDiscreteSlider.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_anim_test_helpers.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmSecondaryPanelActions : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CButtonContainer m_Left, m_Right;
		CUIRect m_Button{40, 40, 240, 24};
		SPopupMenuId m_Panel;
		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		int Draw(bool LeftDisabled = false)
		{
			return ui_widget::SplitActionButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Check for updates", &m_Right, "New features", m_Button, LeftDisabled);
		}

		void Key(int Code)
		{
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}

		void OpenPanel(bool Animated = false)
		{
			auto Props = ui_widget::SecondaryPanelProperties();
			Props.m_CenterInViewport = false;
			Props.m_AutoReposition = false;
			Props.m_Animate = Animated;
			m_Ui.DoPopupMenu(&m_Panel, 40, 40, 240, 140, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
		}
	};
}

TEST_F(QmSecondaryPanelActions, SplitSurfaceUsesSameFillAndBorderAsOrdinaryButton)
{
	Frame({0, 0}, false);
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::SecondaryButton(Ctx, &m_Left, "Check for updates", m_Button);
	ASSERT_EQ(Graphics.m_vRoundedSubmissions.size(), 1u);
	const auto Ordinary = Graphics.m_vRoundedSubmissions.front();
	Graphics.m_vRoundedSubmissions.clear();
	Draw();
	ASSERT_EQ(Graphics.m_vRoundedSubmissions.size(), 2u);
	EXPECT_FALSE(m_Ui.IsClipped());
	for(const auto &Half : Graphics.m_vRoundedSubmissions)
	{
		EXPECT_EQ(Half.m_Rect, Ordinary.m_Rect);
		EXPECT_EQ(Half.m_FillColor, Ordinary.m_FillColor);
		EXPECT_EQ(Half.m_BorderColor, Ordinary.m_BorderColor);
		EXPECT_EQ(Half.m_CornerRadii, Ordinary.m_CornerRadii);
	}
}

TEST_F(QmSecondaryPanelActions, AnchoredSecondaryPanelKeepsItsPositionAndClosesFromAnOutsideClick)
{
	Frame({80, 52}, true);
	auto Props = ui_widget::SecondaryPanelProperties(ui_widget::ESecondaryPanelPlacement::ANCHORED);
	Props.m_Animate = false;
	Props.m_AutoReposition = false;
	Props.m_ContentInset = 4.0f;
	CUIRect ObservedContent{};
	m_Ui.DoPopupMenu(&m_Panel, 40, 40, 240, 140, &ObservedContent, [](void *pContext, CUIRect View, bool) {
		*static_cast<CUIRect *>(pContext) = View;
		return CUi::POPUP_KEEP_OPEN; }, Props);
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	EXPECT_FLOAT_EQ(ObservedContent.x, 44.0f);
	EXPECT_FLOAT_EQ(ObservedContent.y, 44.0f);
	EXPECT_FLOAT_EQ(ObservedContent.w, 232.0f);
	EXPECT_FLOAT_EQ(ObservedContent.h, 132.0f);
	Frame({80, 52}, false);
	m_Ui.RenderPopupMenus();
	Frame({400, 200}, true);
	m_Ui.RenderPopupMenus();
	Frame({400, 200}, false);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
}

TEST_F(QmSecondaryPanelActions, PreLayoutProcessesPressWithoutSubmittingControlBackgrounds)
{
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	Frame({80, 52}, true);
	Graphics.m_vRoundedSubmissions.clear();
	Graphics.m_vRectSubmissions.clear();
	m_Ui.BeginPreLayoutInput();
	ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Reset", m_Button);
	EXPECT_EQ(m_Ui.ActiveItem(), &m_Left);
	ui_widget::DrawToggle(ui_widget::ControlContext(&m_Ui), &m_Right, false, m_Button);
	m_Button.Draw(ColorRGBA(0, 0, 0, 0.5f), IGraphics::CORNER_ALL, 8);
	EXPECT_TRUE(Graphics.m_vRoundedSubmissions.empty());
	EXPECT_TRUE(Graphics.m_vRectSubmissions.empty());
	m_Ui.EndPreLayoutInput();
	ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Reset", m_Button);
	EXPECT_EQ(Graphics.m_vRoundedSubmissions.size(), 1u);
	Frame({80, 52}, false);
	m_Ui.BeginPreLayoutInput();
	EXPECT_TRUE(ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Reset", m_Button));
	m_Ui.EndPreLayoutInput();
	EXPECT_FALSE(ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Reset", m_Button));
}

TEST_F(QmSecondaryPanelActions, EachHalfTriggersOnlyItsOwnAction)
{
	for(const int Side : {0, 1})
	{
		const vec2 Position(Side == 0 ? 80.0f : 240.0f, 52.0f);
		Frame(Position, false);
		EXPECT_EQ(Draw(), -1);
		Frame(Position, false);
		EXPECT_EQ(Draw(), -1);
		Frame(Position, true);
		EXPECT_EQ(Draw(), -1);
		Frame(Position, false);
		EXPECT_EQ(Draw(), Side);
	}
}

TEST_F(QmSecondaryPanelActions, DisabledHalfDoesNotTriggerAndOtherHalfRemainsUsable)
{
	Frame({80, 52}, false);
	Draw(true);
	Frame({80, 52}, true);
	Draw(true);
	Frame({80, 52}, false);
	EXPECT_EQ(Draw(true), -1);
	Frame({240, 52}, false);
	Draw(true);
	Frame({240, 52}, true);
	Draw(true);
	Frame({240, 52}, false);
	EXPECT_EQ(Draw(true), 1);
}

TEST_F(QmSecondaryPanelActions, OutsideClickClosesAfterOpeningFrameButInsideClickDoesNot)
{
	Frame({10, 10}, false);
	Frame({10, 10}, true);
	OpenPanel();
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	Frame({80, 80}, false);
	m_Ui.RenderPopupMenus();
	Frame({80, 80}, true);
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	Frame({10, 10}, false);
	m_Ui.RenderPopupMenus();
	Frame({10, 10}, true);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
}

TEST_F(QmSecondaryPanelActions, EscapeClosesOnceAndRenderOnlyDoesNotConsumeIt)
{
	OpenPanel();
	Key(KEY_ESCAPE);
	m_Ui.BeginRenderOnly();
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	m_Ui.EndRenderOnly();
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE));
}

TEST_F(QmSecondaryPanelActions, EscapeBelongsToNestedPopupBeforeTheBusinessPanel)
{
	OpenPanel();
	SPopupMenuId Child;
	SPopupMenuProperties Props;
	Props.m_Animate = false;
	m_Ui.DoPopupMenu(&Child, 50, 50, 100, 100, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	IInput::CEvent Event{};
	Event.m_Key = KEY_ESCAPE;
	Event.m_Flags = IInput::FLAG_PRESS;
	ASSERT_TRUE(m_Ui.OnInput(Event));
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&Child));
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE));
	Key(KEY_ESCAPE);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
}

TEST_F(QmSecondaryPanelActions, SplitAndRegularButtonsSubmitTheSameOuterRadius)
{
	m_Button.h = 32;
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::SecondaryButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Regular", m_Button, false, EUiButtonGeometry::START_MENU);
	ASSERT_EQ(Graphics.m_vRoundedSubmissions.size(), 1u);
	const float Radius = Graphics.m_vRoundedSubmissions[0].m_CornerRadii.x;
	EXPECT_FLOAT_EQ(Radius, 10.0f);
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::SplitActionButton(ui_widget::ControlContext(&m_Ui), &m_Left, "Left", &m_Right, "Right", m_Button, false, false, EUiButtonGeometry::START_MENU);
	ASSERT_EQ(Graphics.m_vRoundedSubmissions.size(), 2u);
	for(const auto &Half : Graphics.m_vRoundedSubmissions)
	{
		EXPECT_FLOAT_EQ(Half.m_CornerRadii.x, Radius);
		EXPECT_FLOAT_EQ(Half.m_CornerRadii.y, Radius);
		EXPECT_GT(Half.m_Params.x, 0);
	}
}

TEST_F(QmSecondaryPanelActions, BackgroundScopeDrawsControlsWithoutConsumingClicksOrEscape)
{
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	Frame({80, 52}, false);
	Draw();
	Frame({80, 52}, true);
	IInput::CEvent Event{};
	Event.m_Key = KEY_ESCAPE;
	Event.m_Flags = IInput::FLAG_PRESS;
	ASSERT_TRUE(m_Ui.OnInput(Event));
	Graphics.m_vRoundedSubmissions.clear();
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_TRUE(m_Ui.BackgroundRender());
		m_Ui.BeginRenderOnly();
		// 测量是嵌套状态，不能清掉外层后台身份。
		EXPECT_TRUE(m_Ui.BackgroundRender());
		EXPECT_TRUE(m_Ui.MeasurementOnly());
		EXPECT_TRUE(m_Ui.DrawingSuppressed());
		m_Ui.EndRenderOnly();
		EXPECT_FALSE(m_Ui.MeasurementOnly());
		EXPECT_TRUE(m_Ui.BackgroundRender());
		EXPECT_EQ(Draw(), -1);
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE));
		EXPECT_EQ(Graphics.m_vRoundedSubmissions.size(), 2u);
	}
	EXPECT_FALSE(m_Ui.RenderOnly());
	EXPECT_FALSE(m_Ui.BackgroundRender());
	EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ESCAPE));
}

TEST_F(QmSecondaryPanelActions, PanelBackdropCoversTheWholeViewportAndUpdateShellUsesAvailableHeight)
{
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	const CUIRect Viewport = *m_Ui.Screen();
	Graphics.m_vRectSubmissions.clear();
	const CUIRect Panel = ui_widget::DrawSecondaryPanelShell(ui_widget::ControlContext(&m_Ui), Viewport, ESettingsSecondaryPanelKind::UPDATE);
	ASSERT_FALSE(Graphics.m_vRectSubmissions.empty());
	const CUIRect Mask = Graphics.m_vRectSubmissions.front();
	EXPECT_FLOAT_EQ(Mask.x, Viewport.x);
	EXPECT_FLOAT_EQ(Mask.y, Viewport.y);
	EXPECT_FLOAT_EQ(Mask.w, Viewport.w);
	EXPECT_FLOAT_EQ(Mask.h, Viewport.h);
	EXPECT_LE(Panel.w, 600);
	EXPECT_GT(Panel.h, Viewport.h * 0.8f);
	EXPECT_GE(Panel.x, Viewport.x);
	EXPECT_GE(Panel.y, Viewport.y);
	EXPECT_LE(Panel.x + Panel.w, Viewport.x + Viewport.w);
	EXPECT_LE(Panel.y + Panel.h, Viewport.y + Viewport.h);
}

TEST_F(QmSecondaryPanelActions, SharedPanelMetricsPreserveTheExistingValues)
{
	const auto Metrics = ui_widget::ResolveSecondaryPanelMetrics(1000);
	EXPECT_FLOAT_EQ(Metrics.m_FontSize, 7.5f);
	EXPECT_FLOAT_EQ(Metrics.m_Margin, 3);
	EXPECT_FLOAT_EQ(Metrics.m_RowHeight, 16);
	EXPECT_FLOAT_EQ(Metrics.m_LabelHeight, 11);
	EXPECT_FLOAT_EQ(Metrics.m_DropdownHeight, 18);
	EXPECT_FLOAT_EQ(Metrics.m_Spacing, 4);
	const CUIRect Screen{0, 0, 1000, 600};
	const auto Library = ResolveSettingsSecondaryPanelRect(Screen);
	const auto News = ResolveSettingsSecondaryPanelRect(Screen, ESettingsSecondaryPanelKind::NEWS);
	const auto Update = ResolveSettingsSecondaryPanelRect(Screen, ESettingsSecondaryPanelKind::UPDATE);
	EXPECT_FLOAT_EQ(Library.w, 740);
	EXPECT_FLOAT_EQ(Library.h, 444);
	EXPECT_FLOAT_EQ(News.w, 820);
	EXPECT_FLOAT_EQ(News.h, 536);
	EXPECT_FLOAT_EQ(Update.w, 600);
	EXPECT_FLOAT_EQ(Update.h, 536);
}

TEST_F(QmSecondaryPanelActions, FullscreenMaskDoesNotExpandTheExistingPanelLayoutViewport)
{
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	const CUIRect Root = *m_Ui.Screen();
	CUIRect Layout;
	Root.Margin(8, &Layout);
	Graphics.m_vRectSubmissions.clear();
	const auto Panel = ui_widget::DrawSecondaryPanelShell(ui_widget::ControlContext(&m_Ui), Layout, ESettingsSecondaryPanelKind::NEWS);
	const auto Expected = ResolveSettingsSecondaryPanelRect(Layout, ESettingsSecondaryPanelKind::NEWS);
	EXPECT_FLOAT_EQ(Panel.x, Expected.x);
	EXPECT_FLOAT_EQ(Panel.y, Expected.y);
	EXPECT_FLOAT_EQ(Panel.w, Expected.w);
	EXPECT_FLOAT_EQ(Panel.h, Expected.h);
	ASSERT_FALSE(Graphics.m_vRectSubmissions.empty());
	const auto Mask = Graphics.m_vRectSubmissions.front();
	EXPECT_FLOAT_EQ(Mask.x, Root.x);
	EXPECT_FLOAT_EQ(Mask.y, Root.y);
	EXPECT_FLOAT_EQ(Mask.w, Root.w);
	EXPECT_FLOAT_EQ(Mask.h, Root.h);
}

TEST_F(QmSecondaryPanelActions, BackgroundScopeCannotRegisterOrConsumePreviouslyRegisteredWheel)
{
	Frame({80, 80}, false);
	int PageOwner = 0, PanelOwner = 0;
	const CUIRect Area{40, 40, 240, 140};
	ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(100, -120.0f, false));
	m_Ui.RegisterWheelOwner(&PageOwner, EUiWheelOwnerPriority::PAGE, Area, true);
	float Delta = 0.0f;
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		EXPECT_FALSE(m_Ui.TryConsumeWheel(&PageOwner, &Delta));
		m_Ui.RegisterWheelOwner(&PageOwner, EUiWheelOwnerPriority::PAGE, Area, true);
	}
	EXPECT_FALSE(m_Ui.TryConsumeWheel(&PageOwner, &Delta));
	m_Ui.RegisterWheelOwner(&PanelOwner, EUiWheelOwnerPriority::POPUP, Area, true);
	ASSERT_TRUE(m_Ui.TryConsumeWheel(&PanelOwner, &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
}

TEST_F(QmSecondaryPanelActions, PreviouslyHotRealScrollRegionLeavesWheelForForegroundPanel)
{
	CScrollRegion Page;
	const auto DrawPage = [&]() {
		CUIRect Clip{40, 40, 240, 140};
		vec2 Offset;
		Page.Begin(&Clip, &Offset);
		Page.AddRect({Clip.x, Clip.y + Offset.y, Clip.w, 600});
		Page.End();
	};
	Frame({80, 80}, false);
	DrawPage();
	Frame({80, 80}, false);
	ASSERT_EQ(m_Ui.HotScrollRegion(), &Page);
	ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(100, -120.0f, false));
	{
		CUiScopedBackgroundRender Background(&m_Ui);
		DrawPage();
	}
	EXPECT_FALSE(Page.WheelConsumedThisFrame());
	int Foreground = 0;
	m_Ui.RegisterWheelOwner(&Foreground, EUiWheelOwnerPriority::POPUP, {40, 40, 240, 140}, true);
	float Delta = 0.0f;
	ASSERT_TRUE(m_Ui.TryConsumeWheel(&Foreground, &Delta));
	EXPECT_FLOAT_EQ(Delta, -120.0f);
}

TEST_F(QmSecondaryPanelActions, CenteredPanelBlocksPageWheelEvenOutsideItsRect)
{
	Frame({10, 10}, false);
	auto Props = ui_widget::SecondaryPanelProperties();
	Props.m_Animate = false;
	m_Ui.DoPopupMenu(&m_Panel, 0, 0, 240, 140, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(100, 120.0f, false));
	int Page = 0;
	m_Ui.RegisterWheelOwner(&Page, EUiWheelOwnerPriority::PAGE, *m_Ui.Screen(), true);
	float Delta = 0.0f;
	EXPECT_FALSE(m_Ui.TryConsumeWheel(&Page, &Delta));
	m_Ui.ClosePopupMenu(&m_Panel);
	m_Ui.RegisterWheelOwner(&Page, EUiWheelOwnerPriority::PAGE, *m_Ui.Screen(), true);
	EXPECT_TRUE(m_Ui.TryConsumeWheel(&Page, &Delta));
}

TEST_F(QmSecondaryPanelActions, TabAndArrowsFocusIndependentActionsAndEnterSubmitsOnce)
{
	Frame({10, 10}, false);
	Key(KEY_TAB);
	EXPECT_EQ(Draw(), -1);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Left);
	Key(KEY_RIGHT);
	EXPECT_EQ(Draw(), -1);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Right);
	Key(KEY_RETURN);
	EXPECT_EQ(Draw(), 1);
	EXPECT_EQ(Draw(), -1);
	Key(KEY_LEFT);
	EXPECT_EQ(Draw(), -1);
	Key(KEY_RETURN);
	EXPECT_EQ(Draw(), 0);
}

TEST_F(QmSecondaryPanelActions, BecomingDisabledMovesFocusToUsableHalf)
{
	Frame({10, 10}, false);
	m_Ui.SetKeyboardFocus(&m_Left);
	EXPECT_EQ(Draw(true), -1);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Right);
	Key(KEY_RETURN);
	EXPECT_EQ(Draw(true), 1);
}

TEST_F(QmSecondaryPanelActions, ModalEscapeRestoresSourceButtonOnItsNextRender)
{
	CScopedQmFullMotion FullMotion;
	Frame({10, 10}, false);
	m_Ui.SetKeyboardFocus(&m_Right);
	OpenPanel(true);
	// 入场动画推进到可见状态后再关闭（与真实操作节奏一致）。
	for(int Frame = 0; Frame < 12; ++Frame)
	{
		m_Client.AdvanceFrame();
		m_Ui.RenderPopupMenus();
	}
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
	Key(KEY_ESCAPE);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
	EXPECT_TRUE(m_Ui.IsPopupVisible(&m_Panel));
	EXPECT_EQ(Draw(), -1);
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Right);
	// 出场中重开仍有一次独立生命周期，恢复同一个来源。
	OpenPanel(true);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	m_Ui.ClosePopupMenu(&m_Panel);
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Right);
}

TEST_F(QmSecondaryPanelActions, ClosingParentClearsChildAndNotifiesEachOwnerOnce)
{
	int ParentClosed = 0, ChildClosed = 0;
	auto Props = ui_widget::SecondaryPanelProperties();
	Props.m_Animate = true;
	Props.m_OnClosed = [&]() { ++ParentClosed; };
	m_Ui.DoPopupMenu(&m_Panel, 40, 40, 240, 140, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	SPopupMenuId Child;
	Props.m_OnClosed = [&]() { ++ChildClosed; };
	m_Ui.DoPopupMenu(&Child, 80, 80, 100, 80, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	m_Ui.ClosePopupMenu(&m_Panel);
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_FALSE(m_Ui.IsPopupVisible(&Child));
	EXPECT_EQ(ParentClosed, 1);
	EXPECT_EQ(ChildClosed, 1);
	m_Ui.ClosePopupMenu(&m_Panel);
	EXPECT_EQ(ParentClosed, 1);
	EXPECT_EQ(ChildClosed, 1);
}

TEST_F(QmSecondaryPanelActions, ModalRestoresTextOnlyWhenTheSameSourceInputRendersAgain)
{
	CLineInputBuffered<64> Input;
	Input.Set("source");
	const CUIRect Rect{40, 40, 240, 24};
	Frame({10, 10}, false);
	m_Ui.SetActiveItem(&Input);
	m_Ui.DoEditBox(&Input, &Rect, 12);
	ASSERT_TRUE(Input.IsActive());
	OpenPanel();
	EXPECT_FALSE(Input.IsActive());
	m_Ui.ClosePopupMenu(&m_Panel);
	EXPECT_FALSE(Input.IsActive());
	Frame({10, 10}, false);
	m_Ui.DoEditBox(&Input, &Rect, 12);
	EXPECT_TRUE(Input.IsActive());
	EXPECT_STREQ(Input.GetString(), "source");
	m_Ui.ReleaseActiveTextInput(&Input);
}

TEST_F(QmSecondaryPanelActions, LeavingSourceClosesPanelWithoutRestoringItsOldFocus)
{
	int Source = 0;
	Frame({10, 10}, false);
	{
		CUiScopedInteractionOwner Owner(&m_Ui, &Source, "source-card");
		m_Ui.SetKeyboardFocus(&m_Left);
		OpenPanel();
	}
	m_Ui.CloseInteractionSource(&Source, "source-card");
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
}

TEST_F(QmSecondaryPanelActions, MouseTriggerReplacesOlderKeyboardFocusBeforeOpeningPanel)
{
	Frame({80, 52}, false);
	m_Ui.SetKeyboardFocus(&m_Right);
	m_Ui.DoButtonLogic(&m_Left, 0, &m_Button, BUTTONFLAG_LEFT | BUTTONFLAG_CURRENT_HIT);
	Frame({80, 52}, true);
	m_Ui.DoButtonLogic(&m_Left, 0, &m_Button, BUTTONFLAG_LEFT | BUTTONFLAG_CURRENT_HIT);
	Frame({80, 52}, false);
	ASSERT_EQ(m_Ui.DoButtonLogic(&m_Left, 0, &m_Button, BUTTONFLAG_LEFT | BUTTONFLAG_CURRENT_HIT), 1);
	OpenPanel();
	m_Ui.ClosePopupMenu(&m_Panel);
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Left);
}

TEST_F(QmSecondaryPanelActions, ButtonKeyboardNavigationLeavesActiveTextKeysUntouched)
{
	CLineInputBuffered<64> Input;
	const CUIRect Rect{40, 80, 240, 24};
	Frame({10, 10}, false);
	m_Ui.SetKeyboardFocus(&m_Left);
	Key(KEY_RIGHT);
	m_Ui.SetActiveItem(&Input);
	m_Ui.DoEditBox(&Input, &Rect, 12);
	ASSERT_TRUE(Input.IsActive());
	EXPECT_EQ(Draw(), -1);
	EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_RIGHT));
	m_Ui.ReleaseActiveTextInput(&Input);
}

TEST_F(QmSecondaryPanelActions, BackgroundMultilineInputDoesNotDeactivateForegroundText)
{
	CLineInputBuffered<64> Foreground, Background;
	const CUIRect Rect{40, 80, 240, 60};
	Frame({10, 10}, false);
	m_Ui.SetActiveItem(&Foreground);
	m_Ui.DoEditBox(&Foreground, &Rect, 12);
	ASSERT_TRUE(Foreground.IsActive());
	{
		CUiScopedBackgroundRender Scope(&m_Ui);
		EXPECT_FALSE(m_Ui.DoEditBoxMultiLine(&Background, &Rect, 12, 0.0f));
	}
	EXPECT_TRUE(Foreground.IsActive());
	EXPECT_FALSE(Background.IsActive());
	m_Ui.ReleaseActiveTextInput(&Foreground);
}

TEST_F(QmSecondaryPanelActions, ImmediateReopenBeforeSourceRenderPreservesItsReturnFocus)
{
	CScopedQmFullMotion FullMotion;
	Frame({10, 10}, false);
	m_Ui.SetKeyboardFocus(&m_Right);
	OpenPanel(true);
	// 入场动画推进到可见状态（真实操作节奏），再走关闭路径。
	for(int Frame = 0; Frame < 12; ++Frame)
	{
		m_Client.AdvanceFrame();
		m_Ui.RenderPopupMenus();
	}
	// 关闭按钮会留下 LastActiveItem，但它属于正在退场的面板。
	m_Ui.SetActiveItem(&m_Left);
	m_Ui.ClosePopupMenu(&m_Panel);
	ASSERT_TRUE(m_Ui.IsPopupVisible(&m_Panel));
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
	OpenPanel(true);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	m_Ui.ClosePopupMenu(&m_Panel);
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Right);
}

TEST_F(QmSecondaryPanelActions, ReturnFocusRejectsTheSameControlUnderAnotherSource)
{
	int Source = 0, OtherSource = 0;
	Frame({10, 10}, false);
	{
		CUiScopedInteractionOwner Owner(&m_Ui, &Source, "source-card");
		m_Ui.SetKeyboardFocus(&m_Left);
		OpenPanel();
	}
	m_Ui.ClosePopupMenu(&m_Panel);
	{
		CUiScopedInteractionOwner Owner(&m_Ui, &OtherSource, "source-card");
		Draw();
		EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
	}
	{
		CUiScopedInteractionOwner Owner(&m_Ui, &Source, "source-card");
		Draw();
		EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Left);
	}
}

TEST_F(QmSecondaryPanelActions, MissingSourceExpiresPendingFocusBeforeARevisitedPage)
{
	Frame({10, 10}, false);
	m_Ui.SetKeyboardFocus(&m_Right);
	OpenPanel();
	m_Ui.ClosePopupMenu(&m_Panel);
	Frame({10, 10}, false);
	Frame({10, 10}, false);
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
}

TEST_F(QmSecondaryPanelActions, ContextWithoutCardIdRestoresOnlyToThatContext)
{
	int Source = 0;
	Frame({10, 10}, false);
	{
		CUiScopedInteractionOwner Owner(&m_Ui, &Source, nullptr);
		m_Ui.SetKeyboardFocus(&m_Right);
		OpenPanel();
	}
	m_Ui.ClosePopupMenu(&m_Panel);
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
	{
		CUiScopedInteractionOwner Owner(&m_Ui, &Source, nullptr);
		Draw();
		EXPECT_EQ(m_Ui.KeyboardFocusItem(), &m_Right);
	}
}

TEST_F(QmSecondaryPanelActions, ClosingNestedModalRestoresTheLiveParentTextInput)
{
	struct SContext
	{
		CUi *m_pUi;
		CLineInputBuffered<64> m_Input;
		SPopupMenuId m_Child;
		bool m_OpenChild = false;
	} Context{&m_Ui};
	Context.m_Input.Set("parent text");
	Frame({10, 10}, false);
	auto Props = ui_widget::SecondaryPanelProperties();
	Props.m_Animate = false;
	Props.m_CenterInViewport = false;
	Props.m_AutoReposition = false;
	m_Ui.DoPopupMenu(&m_Panel, 40, 40, 240, 140, &Context, [](void *pContext, CUIRect View, bool Active) {
			auto *pState = static_cast<SContext *>(pContext);
			CUIRect Edit;
			View.HSplitTop(24.0f, &Edit, nullptr);
			if(Active)
				pState->m_pUi->DoEditBox(&pState->m_Input, &Edit, 12);
			else
			{
				CUiScopedBackgroundRender Background(pState->m_pUi);
				pState->m_pUi->DoEditBox(&pState->m_Input, &Edit, 12);
			}
			if(Active && pState->m_OpenChild)
			{
				pState->m_OpenChild = false;
				auto ChildProps = ui_widget::SecondaryPanelProperties();
				ChildProps.m_Animate = false;
				pState->m_pUi->DoPopupMenu(&pState->m_Child, 0, 0, 100, 80, nullptr,
					[](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, ChildProps);
			}
			return CUi::POPUP_KEEP_OPEN; }, Props);
	m_Ui.SetActiveItem(&Context.m_Input);
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(Context.m_Input.IsActive());
	Context.m_OpenChild = true;
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(Context.m_Input.IsActive());
	ASSERT_TRUE(m_Ui.CloseTopPopupMenu());
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Panel));
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(Context.m_Input.IsActive());
	EXPECT_STREQ(Context.m_Input.GetString(), "parent text");
	m_Ui.ClosePopupMenus(false);
	EXPECT_FALSE(Context.m_Input.IsActive());
}

TEST_F(QmSecondaryPanelActions, LostRefreshSourceClosesWithoutRestoringFocus)
{
	Frame({10, 10}, false);
	m_Ui.SetKeyboardFocus(&m_Right);
	auto Props = ui_widget::SecondaryPanelProperties();
	Props.m_Animate = false;
	Props.m_RequireSourceRefresh = true;
	Props.m_SourceFrame = m_Ui.PopupSourceFrame();
	m_Ui.DoPopupMenu(&m_Panel, 0, 0, 240, 140, nullptr, [](void *, CUIRect, bool) { return CUi::POPUP_KEEP_OPEN; }, Props);
	Frame({10, 10}, false);
	Frame({10, 10}, false);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Panel));
	Draw();
	EXPECT_EQ(m_Ui.KeyboardFocusItem(), nullptr);
}

TEST_F(QmSecondaryPanelActions, ServerFilterAnimatesKnobBetweenStopsWhenMotionIsEnabled)
{
	CScopedQmFullMotion FullMotion;
	CUiV2AnimationRuntime Animation;
	m_Ui.SetQmAnimationRuntime(&Animation);
	const auto Ctx = ui_widget::ControlContext(&m_Ui);
	const auto Geometry = ui_widget::ResolveDiscreteSliderGeometry(m_Button, Ctx.m_UiScale);
	auto &Graphics = *static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	ui_widget::SDiscreteSliderState State;
	int Selected = 0;
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::DiscreteSlider(Ctx, &m_Left, State, &Selected, 0, 6, m_Button);
	ASSERT_FALSE(Graphics.m_vRoundedSubmissions.empty());
	const float StartCenter = Graphics.m_vRoundedSubmissions.back().m_Rect.x + Graphics.m_vRoundedSubmissions.back().m_Rect.z * 0.5f;

	Selected = 4;
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::DiscreteSlider(Ctx, &m_Left, State, &Selected, 0, 6, m_Button);
	Animation.Advance(1.0f / 60.0f);
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::DiscreteSlider(Ctx, &m_Left, State, &Selected, 0, 6, m_Button);
	ASSERT_FALSE(Graphics.m_vRoundedSubmissions.empty());
	const float MidCenter = Graphics.m_vRoundedSubmissions.back().m_Rect.x + Graphics.m_vRoundedSubmissions.back().m_Rect.z * 0.5f;
	EXPECT_GT(MidCenter, StartCenter + 1.0f);
	EXPECT_LT(MidCenter, Geometry.Position(4.0f / 6.0f) - 1.0f);

	AdvanceQmAnimFor(Animation, 1.0f);
	Graphics.m_vRoundedSubmissions.clear();
	ui_widget::DiscreteSlider(Ctx, &m_Left, State, &Selected, 0, 6, m_Button);
	ASSERT_FALSE(Graphics.m_vRoundedSubmissions.empty());
	const float PixelSize = m_Ui.Screen()->w / Graphics.ScreenWidth();
	EXPECT_NEAR(Graphics.m_vRoundedSubmissions.back().m_Rect.x + Graphics.m_vRoundedSubmissions.back().m_Rect.z * 0.5f, Geometry.Position(4.0f / 6.0f), PixelSize);
	m_Ui.SetQmAnimationRuntime(nullptr);
}
