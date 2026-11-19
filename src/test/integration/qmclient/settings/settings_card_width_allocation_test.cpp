#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsCardWidthState.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>

namespace
{
	class CSettingsCardWidthPublication : public testing::Test
	{
		std::string m_PreviousWidths = g_Config.m_QmSettingsCardWidths;

	protected:
		void TearDown() override
		{
			str_copy(g_Config.m_QmSettingsCardWidths, m_PreviousWidths.c_str());
			qm_card_width::SyncFromConfig();
		}
	};
}

TEST_F(CSettingsCardWidthPublication, FailedReloadPreservesPublishedWidthsAndCanRetry)
{
	constexpr char PREVIOUS_ID[] = "deck:previous-card-width";
	constexpr char EXTERNAL_ID[] = "deck:external-card-width";
	constexpr char CLICKED_ID[] = "deck:clicked-card-width";
	str_copy(g_Config.m_QmSettingsCardWidths, PREVIOUS_ID);
	const auto &Published = qm_card_width::CurrentState();
	ASSERT_TRUE(Published.IsFullWidth(PREVIOUS_ID, false));
	str_copy(g_Config.m_QmSettingsCardWidths, EXTERNAL_ID);

	bool ReadAllocationFailed = false;
	{
		CPredictionAllocationFailure Failure;
		qm_card_width::CurrentState();
		ReadAllocationFailed = Failure.Triggered();
	}
	EXPECT_TRUE(ReadAllocationFailed);
	EXPECT_TRUE(Published.IsFullWidth(PREVIOUS_ID, false));
	EXPECT_FALSE(Published.IsFullWidth(EXTERNAL_ID, false));

	bool Changed = true;
	bool WriteAllocationFailed = false;
	{
		CPredictionAllocationFailure Failure;
		Changed = qm_card_width::SetFullWidth(CLICKED_ID, true);
		WriteAllocationFailed = Failure.Triggered();
	}
	EXPECT_TRUE(WriteAllocationFailed);
	EXPECT_FALSE(Changed);
	EXPECT_TRUE(Published.IsFullWidth(PREVIOUS_ID, false));
	EXPECT_STREQ(g_Config.m_QmSettingsCardWidths, EXTERNAL_ID);

	ASSERT_TRUE(qm_card_width::SetFullWidth(CLICKED_ID, true));
	EXPECT_FALSE(Published.IsFullWidth(PREVIOUS_ID, false));
	EXPECT_TRUE(Published.IsFullWidth(EXTERNAL_ID, false));
	EXPECT_TRUE(Published.IsFullWidth(CLICKED_ID, false));
	EXPECT_STREQ(g_Config.m_QmSettingsCardWidths, "deck:clicked-card-width;deck:external-card-width");
}
