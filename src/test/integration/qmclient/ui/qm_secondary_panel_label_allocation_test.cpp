#include <engine/keys.h>

#include <game/client/QmUi/SecondaryPanel.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmSecondaryPanelLabelAllocation : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		ui_widget::SSecondaryPanelMetrics Metrics() const
		{
			return ui_widget::ResolveSecondaryPanelMetrics(1000);
		}
		ui_widget::CSecondaryPanel Panel()
		{
			return {ui_widget::ControlContext(&m_Ui), {40, 40, 240, 200}, true, Metrics(), {}};
		}
		void Frame(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}
	};
}

TEST_F(QmSecondaryPanelLabelAllocation, LabelAdmissionFailureDoesNotPreventTheToggleClick)
{
	ui_widget::SSecondaryPanelLabel Warm, State;
	int Value = 0;
	const CUIRect Content = Panel().ContentRect();
	const vec2 Position(Content.x + 5.0f, Content.y + Metrics().m_Spacing + Metrics().m_RowHeight * 0.5f);
	for(int Index = 0; Index < 2; ++Index)
	{
		Frame(Position, false);
		auto Row = Panel();
		Row.ToggleRow(Warm, "Enabled", Value);
	}
	for(const bool Pressed : {true, false})
	{
		Frame(Position, Pressed);
		auto Row = Panel();
		bool Triggered = false;
		{
			CPredictionAllocationFailure Fault;
			Row.ToggleRow(State, "Enabled", Value);
			Triggered = Fault.Triggered();
		}
		ASSERT_TRUE(Triggered);
		ASSERT_FALSE(State.m_Initialized);
		ASSERT_FALSE(State.m_Element.IsRegistered());
	}
	EXPECT_EQ(Value, 1);
	Frame(Position, false);
	auto Row = Panel();
	Row.ToggleRow(State, "Enabled", Value);
	EXPECT_TRUE(State.m_Initialized);
	EXPECT_TRUE(State.m_Element.IsRegistered());
	EXPECT_EQ(Value, 1);
}

TEST_F(QmSecondaryPanelLabelAllocation, LabelAdmissionFailurePreservesDropdownSelectionAndCanRetry)
{
	ui_widget::SSecondaryPanelLabel Label;
	CUi::SDropDownState Dropdown;
	const char *apNames[] = {"First", "Second"};
	Frame({0, 0}, false);
	auto Row = Panel();
	int Selected = -1;
	bool Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		Selected = Row.DropdownRow(Label, "Choice", 1, apNames, 2, Dropdown);
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(Selected, 1);
	ASSERT_FALSE(Label.m_Initialized);
	ASSERT_FALSE(Label.m_Element.IsRegistered());
	Frame({0, 0}, false);
	auto Retry = Panel();
	EXPECT_EQ(Retry.DropdownRow(Label, "Choice", Selected, apNames, 2, Dropdown), 1);
	ASSERT_TRUE(Label.m_Initialized);
	ASSERT_TRUE(Label.m_Element.IsRegistered());
	EXPECT_GT(Label.m_Element.Rect(0)->m_Cursor.m_GlyphCount, 0);
}
