#include <engine/keys.h>

#include <game/client/ui_scrollregion.h>

#include <test/support/prediction_allocation_failure.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>
#include <test/support/tracked_allocation_resource.h>

#include <iterator>

class QmDropdownSearchFailure : public qm_ui_test::CRealUiFixture
{
protected:
	qm_ui_test::CTestInput m_Input;
	qm_ui_test::CTestUiClient m_Client;
	CTrackedAllocationResource m_Memory;
	CUi::SSelectionPopupContext m_Context{&m_Memory};
	CScrollRegion m_Scroll;

	IInput *UiInput() override { return &m_Input; }
	IClient *UiClient() override { return &m_Client; }
	void TearDown() override
	{
		m_Ui.ClosePopupMenu(&m_Context);
		CRealUiFixture::TearDown();
	}
	void Frame()
	{
		m_Client.AdvanceFrame();
		m_Ui.Update();
		m_Ui.RenderPopupMenus();
	}
	void Open()
	{
		m_Context.m_pScrollRegion = &m_Scroll;
		m_Context.m_Searchable = true;
		m_Context.m_vEntries = {"Default", "Arial", "Verdana"};
		m_Context.m_SearchInput.Set("Arial");
		m_Ui.ShowPopupSelection(40.0f, 40.0f, &m_Context);
		ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Context));
	}
};

TEST_F(QmDropdownSearchFailure, FirstSearchFailureClosesPopupWithoutSelectingAndReopeningCanRetry)
{
	m_Memory.m_FailAt = 1;
	ASSERT_NO_FATAL_FAILURE(Open());
	EXPECT_NO_THROW(Frame());
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Context));
	EXPECT_EQ(m_Context.m_SelectionIndex, -1);
	EXPECT_EQ(m_Context.m_pSelection, nullptr);
	EXPECT_EQ(m_Memory.m_Live, 0);
	m_Memory.m_FailAt = -1;
	ASSERT_NO_FATAL_FAILURE(Open());
	Frame();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Context));
	EXPECT_EQ(m_Context.m_Search.Indices(), (std::vector<int>{1}));
}

TEST_F(QmDropdownSearchFailure, SourceShrinkFailureCannotUseOldFontIndices)
{
	ASSERT_NO_FATAL_FAILURE(Open());
	Frame();
	ASSERT_EQ(m_Context.m_Search.Indices(), (std::vector<int>{1}));
	m_Context.m_vEntries = {"Mono"};
	m_Memory.m_FailAt = m_Memory.m_Allocations + 1;
	EXPECT_NO_THROW(Frame());
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Context));
	EXPECT_EQ(m_Context.m_SelectionIndex, -1);
	EXPECT_EQ(m_Context.m_pSelection, nullptr);
}

TEST_F(QmDropdownSearchFailure, UnknownSnapshotExceptionClosesPopupWithoutUnwindingUi)
{
	m_Memory.m_FailAt = 1;
	m_Memory.m_ThrowUnknown = true;
	ASSERT_NO_FATAL_FAILURE(Open());
	EXPECT_NO_THROW(Frame());
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Context));
	EXPECT_EQ(m_Context.m_SelectionIndex, -1);
	EXPECT_EQ(m_Context.m_pSelection, nullptr);
	EXPECT_EQ(m_Memory.m_Live, 0);
}

TEST_F(QmDropdownSearchFailure, ClickingClearRestoresAllEntriesWithoutSelecting)
{
	ASSERT_NO_FATAL_FAILURE(Open());
	Frame();
	ASSERT_EQ(m_Context.m_Search.Indices(), (std::vector<int>{1}));
	const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Context);
	ASSERT_NE(pPopup, nullptr);
	CUIRect Search, ClearButton;
	pPopup->Margin(CUi::PopupMenuContentInset() * 0.5f, &Search);
	Search.HSplitTop(m_Context.m_EntryHeight, &Search, nullptr);
	Search.VSplitRight(Search.h, nullptr, &ClearButton);
	const vec2 WindowSize(Graphics().WindowWidth(), Graphics().WindowHeight());
	const vec2 Position = ClearButton.Center() * WindowSize / m_Ui.Screen()->Size();
	const vec2 Delta = Position - m_Ui.UpdatedMousePos();
	m_Ui.OnCursorMove(Delta.x, Delta.y);
	Frame();

	m_Input.m_HeldKey = KEY_MOUSE_1;
	Frame();
	m_Input.m_HeldKey = 0;
	Frame();

	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Context));
	EXPECT_STREQ(m_Context.m_SearchInput.GetString(), "");
	EXPECT_EQ(m_Context.m_Search.Indices(), (std::vector<int>{0, 1, 2}));
	EXPECT_EQ(m_Context.m_SelectionIndex, -1);
}

TEST_F(QmDropdownSearchFailure, ScrollRegionAllocationFailureKeepsRegistrationForRetry)
{
	m_Ui.SetEnabled(true);
	{
		// 预留一个登记位置，让故障落在滚动区分配。
		CUIElement Element;
		Element.Init(&m_Ui, -1);
	}

	const char *apEntries[] = {"Default", "Arial"};
	for(CScrollRegion *pBorrowedScroll : {static_cast<CScrollRegion *>(nullptr), &m_Scroll})
	{
		SCOPED_TRACE(pBorrowedScroll != nullptr ? "borrowed scroll region" : "owned scroll region");
		CUi::SDropDownState State;
		State.m_SelectionPopupContext.m_pScrollRegion = pBorrowedScroll;
		CUIRect Anchor{40.0f, 40.0f, 200.0f, 24.0f};
		const auto DrawDropDown = [&] { return m_Ui.DoDropDown(&Anchor, 1, apEntries, static_cast<int>(std::size(apEntries)), State); };

		Frame();
		int Selection = -1;
		bool Triggered = false;
		m_Ui.BeginPreLayoutInput();
		{
			CPredictionAllocationFailure Fault;
			EXPECT_NO_THROW(Selection = DrawDropDown());
			Triggered = Fault.Triggered();
		}
		m_Ui.EndPreLayoutInput();
		ASSERT_TRUE(Triggered);
		ASSERT_TRUE(State.m_Init);
		EXPECT_EQ(Selection, 1);
		EXPECT_EQ(State.m_pOwnedScrollRegion, nullptr);
		EXPECT_EQ(State.m_SelectionPopupContext.m_pScrollRegion, pBorrowedScroll);
		EXPECT_FALSE(m_Ui.IsPopupOpen(&State.m_SelectionPopupContext));

		Frame();
		Selection = -1;
		m_Ui.BeginPreLayoutInput();
		EXPECT_NO_THROW(Selection = DrawDropDown());
		m_Ui.EndPreLayoutInput();
		EXPECT_EQ(Selection, 1);
		ASSERT_NE(State.m_pOwnedScrollRegion, nullptr);
		const CScrollRegion *pExpected = pBorrowedScroll != nullptr ? pBorrowedScroll : State.m_pOwnedScrollRegion.get();
		EXPECT_EQ(State.m_pScrollRegion, pExpected);
		EXPECT_EQ(State.m_SelectionPopupContext.m_pScrollRegion, pExpected);
	}
}
