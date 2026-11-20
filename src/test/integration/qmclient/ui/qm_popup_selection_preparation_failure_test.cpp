#include <engine/keys.h>

#include <game/client/ui_scrollregion.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <array>

namespace
{
	class QmPopupSelectionPreparationFailure : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUi::SDropDownState m_DropDown;
		CUi::SSelectionPopupContext m_Selection;
		CScrollRegion m_Scroll;
		SPopupMenuId m_Parent;
		CLineInputBuffered<64> m_ParentInput;
		CUIRect m_Anchor{40.0f, 40.0f, 200.0f, 24.0f};
		const std::array<const char *, 2> m_apEntries{"First option", "Second option"};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Update(bool Pressed = false)
		{
			m_Client.AdvanceFrame();
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			m_Ui.SetEnabled(true);
			m_Ui.Update();
		}

		void HoverTrigger()
		{
			const vec2 WindowSize = vec2(Graphics().WindowWidth(), Graphics().WindowHeight());
			const vec2 Delta = m_Anchor.Center() * WindowSize / m_Ui.Screen()->Size() - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
		}

		int DrawDropDown()
		{
			return m_Ui.DoDropDown(&m_Anchor, 1, m_apEntries.data(), (int)m_apEntries.size(), m_DropDown);
		}

		void PressTrigger()
		{
			Update(true);
			m_Ui.BeginPreLayoutInput();
			EXPECT_EQ(DrawDropDown(), 1);
			m_Ui.EndPreLayoutInput();
			EXPECT_TRUE(m_Ui.CheckActiveItem(&m_DropDown.m_ButtonContainer));
			Update();
		}

		static CUi::EPopupMenuFunctionResult KeepOpen(void *, CUIRect, bool)
		{
			return CUi::POPUP_KEEP_OPEN;
		}

		void PrepareSelection()
		{
			Update();
			SPopupMenuProperties Properties;
			Properties.m_BlockUnderlyingPointerInput = true;
			m_Ui.DoPopupMenu(&m_Parent, 20.0f, 20.0f, 300.0f, 250.0f, nullptr, KeepOpen, Properties);
			ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Parent));
			m_Selection.m_pScrollRegion = &m_Scroll;
			m_Selection.m_vEntries.assign(m_apEntries.begin(), m_apEntries.end());
		}
	};
}

TEST_F(QmPopupSelectionPreparationFailure, OptionCopyFailureKeepsSelectionAndCanRetryTheClick)
{
	m_DropDown.m_SelectionPopupContext.m_pScrollRegion = &m_Scroll;
	HoverTrigger();
	ASSERT_NO_FATAL_FAILURE(PressTrigger());

	int Selection = -1;
	bool Triggered = false;
	// 预布局走真实按钮输入，跳过标签绘制和字形准备。
	m_Ui.BeginPreLayoutInput();
	{
		CPredictionAllocationFailure Fault;
		EXPECT_NO_THROW(Selection = DrawDropDown());
		Triggered = Fault.Triggered();
	}
	m_Ui.EndPreLayoutInput();
	ASSERT_TRUE(Triggered);
	EXPECT_EQ(Selection, 1);
	EXPECT_FALSE(m_DropDown.m_DropDownState.IsOpen());
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_DropDown.m_SelectionPopupContext));
	EXPECT_TRUE(m_DropDown.m_SelectionPopupContext.m_vEntries.empty());

	ASSERT_NO_FATAL_FAILURE(PressTrigger());
	m_Ui.BeginPreLayoutInput();
	EXPECT_EQ(DrawDropDown(), 1);
	m_Ui.EndPreLayoutInput();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_DropDown.m_SelectionPopupContext));
	EXPECT_EQ(m_DropDown.m_SelectionPopupContext.m_pScrollRegion, &m_Scroll);
	EXPECT_EQ(m_DropDown.m_SelectionPopupContext.m_ActiveIndex, 1);
}

TEST_F(QmPopupSelectionPreparationFailure, ButtonAllocationFailureKeepsTheParentAndItsTextInput)
{
	ASSERT_NO_FATAL_FAILURE(PrepareSelection());
	m_ParentInput.Activate(EInputPriority::UI);
	ASSERT_EQ(CLineInput::GetActiveInput(), &m_ParentInput);

	bool Triggered = false;
	{
		CPredictionAllocationFailure Fault;
		EXPECT_NO_THROW(m_Ui.ShowPopupSelection(40.0f, 40.0f, &m_Selection));
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Parent));
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Selection));
	EXPECT_EQ(CLineInput::GetActiveInput(), &m_ParentInput);
	EXPECT_EQ(m_Selection.m_SelectionIndex, -1);

	m_Ui.ShowPopupSelection(40.0f, 40.0f, &m_Selection);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Selection));
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Parent));
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmPopupSelectionPreparationFailure, LiveListGrowthFailureClosesOnlyTheListAndRestoresClipping)
{
	ASSERT_NO_FATAL_FAILURE(PrepareSelection());
	m_Ui.ShowPopupSelection(40.0f, 40.0f, &m_Selection);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Selection));
	m_Ui.RenderPopupMenus();
	ASSERT_FALSE(m_Ui.IsClipped());
	// 搜索和 Graphics 替身的记录均已准备，故障留给条目按钮数组增长。
	m_Selection.m_vEntries.resize(m_Selection.m_vButtonContainers.capacity() + 1, "Another option");
	ASSERT_EQ(m_Selection.m_Search.TryUpdate(m_Selection.m_vEntries, ""), CQmDropdownSearch::EUpdateResult::UPDATED);
	Graphics().m_vClipStack.reserve(Graphics().m_vClipStack.size() + 8);
	Update();

	bool Triggered = false;
	m_Ui.BeginPreLayoutInput();
	{
		CPredictionAllocationFailure Fault;
		EXPECT_NO_THROW(m_Ui.RenderPopupMenus());
		Triggered = Fault.Triggered();
	}
	m_Ui.EndPreLayoutInput();
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(m_Ui.IsClipped());
	EXPECT_FALSE(m_Ui.RenderingPopupMenus());
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Parent));
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Selection));
	EXPECT_EQ(m_Selection.m_SelectionIndex, -1);
}
