#include <engine/keys.h>

#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/components/qmclient/browser_map_preview.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmBrowserMapPreviewInteraction : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CQmBrowserMapPreviewView m_View;
		CUIRect m_Canvas{80, 110, 400, 220};
		SPopupMenuId m_Popup;
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
		void Open()
		{
			m_View.Configure({32000, 18000}, {1500, 844});
			m_View.ZoomAt(8, {0.5f, 0.5f});
			auto Props = ui_widget::SecondaryPanelProperties();
			Props.m_Animate = false;
			Props.m_AutoReposition = false;
			Props.m_OnClosed = [this]() { m_View.EndDrag(); };
			m_Ui.DoPopupMenu(&m_Popup, 50, 50, m_Ui.Screen()->w - 100, m_Ui.Screen()->h - 100, this, [](void *pContext, CUIRect, bool Active) {
				auto &Self = *static_cast<QmBrowserMapPreviewInteraction *>(pContext);
				QmBrowserMapPreviewInput(Self.m_Ui, Self.m_View, Self.m_Canvas, Active);
				return CUi::POPUP_KEEP_OPEN; }, Props);
		}
	};
}

TEST_F(QmBrowserMapPreviewInteraction, DragCapturesPointerUntilReleaseEvenOutsideCanvas)
{
	Frame({200, 180}, false);
	Open();
	m_Ui.RenderPopupMenus();
	const vec2 Before = m_View.Center();
	Frame({200, 180}, true);
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_View.Dragging());
	Frame({250, 200}, true);
	m_Ui.RenderPopupMenus();
	EXPECT_LT(m_View.Center().x, Before.x);
	Frame({490, 340}, true);
	m_Ui.RenderPopupMenus();
	EXPECT_TRUE(m_View.Dragging());
	Frame({490, 340}, false);
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_View.Dragging());
	EXPECT_EQ(m_Ui.ActiveItem(), nullptr);
}

TEST_F(QmBrowserMapPreviewInteraction, WheelZoomBelongsToCanvasAndDoesNotScrollUnderlyingPage)
{
	Frame({200, 180}, false);
	Open();
	ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(100, 120.0f, false));
	int PageOwner = 0;
	m_Ui.RegisterWheelOwner(&PageOwner, EUiWheelOwnerPriority::PAGE, *m_Ui.Screen(), true);
	float Delta = 0.0f;
	EXPECT_FALSE(m_Ui.TryConsumeWheel(&PageOwner, &Delta));
	const float Before = m_View.ZoomFactor();
	m_Ui.RenderPopupMenus();
	EXPECT_FLOAT_EQ(m_View.ZoomFactor(), Before * 1.25f);
	Frame({200, 70}, false);
	ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(101, 120.0f, false));
	m_Ui.RenderPopupMenus();
	EXPECT_FLOAT_EQ(m_View.ZoomFactor(), Before * 1.25f);
}

TEST_F(QmBrowserMapPreviewInteraction, EscapeCancelsDraggingAndReopeningDoesNotContinueOldGesture)
{
	Frame({200, 180}, false);
	Open();
	m_Ui.RenderPopupMenus();
	Frame({200, 180}, true);
	m_Ui.RenderPopupMenus();
	ASSERT_TRUE(m_View.Dragging());
	IInput::CEvent Event{};
	Event.m_Key = KEY_ESCAPE;
	Event.m_Flags = IInput::FLAG_PRESS;
	ASSERT_TRUE(m_Ui.OnInput(Event));
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Popup));
	EXPECT_FALSE(m_View.Dragging());
	Frame({250, 200}, false);
	Open();
	m_Ui.RenderPopupMenus();
	EXPECT_FALSE(m_View.Dragging());
}

TEST_F(QmBrowserMapPreviewInteraction, InactivePanelCannotStartADragOrConsumeWheel)
{
	Frame({200, 180}, false);
	m_View.Configure({32000, 18000}, {1500, 844});
	Frame({200, 180}, true);
	ASSERT_TRUE(m_Ui.BeginWheelOwnershipFrame(100, 120.0f, false));
	QmBrowserMapPreviewInput(m_Ui, m_View, m_Canvas, false);
	EXPECT_FALSE(m_View.Dragging());
	EXPECT_FLOAT_EQ(m_View.ZoomFactor(), 1.0f);
}
