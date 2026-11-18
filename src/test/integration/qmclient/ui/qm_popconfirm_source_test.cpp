#include <game/client/QmUi/QmPopconfirm.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmPopconfirmSource : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		SQmPopconfirmContext m_Confirm;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void TearDown() override
		{
			m_Ui.ClosePopupMenus(false);
			CRealUiFixture::TearDown();
		}

		void Frame(bool SourceVisible, bool DrawPopups = true)
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
			if(SourceVisible)
				QmShowPopconfirm(&m_Ui, &m_Confirm, {80.0f, 60.0f, 24.0f, 16.0f});
			if(DrawPopups)
				m_Ui.RenderPopupMenus();
		}
	};
}

TEST_F(QmPopconfirmSource, LeavingTheAssetSourceCancelsAndReleasesPointerInput)
{
	str_copy(m_Confirm.m_aMessage, "Delete this asset?");
	m_Confirm.DefaultButtons();
	Frame(true);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Confirm));
	ASSERT_TRUE(m_Ui.PointerInputBlocked());

	Frame(true);
	Frame(true);
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Confirm));
	EXPECT_FALSE(m_Confirm.m_Canceled);

	Frame(false);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Confirm));
	EXPECT_FALSE(m_Ui.PointerInputBlocked());
	EXPECT_TRUE(m_Confirm.m_Canceled);
	EXPECT_FALSE(m_Confirm.m_Confirmed);
}

TEST_F(QmPopconfirmSource, UiUpdateCancelsEvenWhenPopupDrawingStops)
{
	str_copy(m_Confirm.m_aMessage, "Delete this asset?");
	m_Confirm.DefaultButtons();
	Frame(true);
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Confirm));

	Frame(false, false);
	Frame(false, false);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Confirm));
	EXPECT_FALSE(m_Ui.PointerInputBlocked());
	EXPECT_TRUE(m_Confirm.m_Canceled);
	EXPECT_FALSE(m_Confirm.m_Confirmed);
}
