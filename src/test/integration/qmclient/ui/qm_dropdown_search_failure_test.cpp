#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>
#include <test/support/tracked_allocation_resource.h>

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
