#include <engine/shared/config.h>

#include <game/client/QmUi/cards/QmCardCatalog.h>

#include <gtest/gtest.h>

class CQmGeneralCardLayoutTest : public ::testing::Test
{
	CConfig m_OriginalConfig;

protected:
	void SetUp() override
	{
		m_OriginalConfig = g_Config;
		g_Config.m_ClAutoDemoRecord = 0;
		g_Config.m_ClAutoScreenshot = 0;
		g_Config.m_ClAutoStatboardScreenshot = 0;
		g_Config.m_ClAutoCSV = 0;
		g_Config.m_ClDyncam = 0;
		g_Config.m_ClMouseFollowfactor = 0;
	}

	void TearDown() override
	{
		g_Config = m_OriginalConfig;
	}
};

TEST_F(CQmGeneralCardLayoutTest, RevealingASettingsRowInvalidatesSharedCardDefinitions)
{
	struct SCase
	{
		const char *m_pName;
		int *m_pSetting;
	};
	const SCase aCases[] = {
		{"cl_auto_demo_record", &g_Config.m_ClAutoDemoRecord},
		{"cl_auto_screenshot", &g_Config.m_ClAutoScreenshot},
		{"cl_auto_statboard_screenshot", &g_Config.m_ClAutoStatboardScreenshot},
		{"cl_auto_csv", &g_Config.m_ClAutoCSV},
		{"cl_dyncam", &g_Config.m_ClDyncam},
		{"cl_mouse_followfactor", &g_Config.m_ClMouseFollowfactor},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pName);
		const uint64_t Before = qm_card_catalog::MeasureContentRevision(8, 8);
		*Case.m_pSetting = 1;
		EXPECT_NE(qm_card_catalog::MeasureContentRevision(8, 8), Before);
		*Case.m_pSetting = 0;
	}
}

TEST_F(CQmGeneralCardLayoutTest, EditingRefreshRateDoesNotRecreateCardDefinitions)
{
	g_Config.m_ClRefreshRate = 60;
	const uint64_t Before = qm_card_catalog::MeasureContentRevision(8, 8);
	g_Config.m_ClRefreshRate = 480;
	EXPECT_EQ(qm_card_catalog::MeasureContentRevision(8, 8), Before);
}

TEST_F(CQmGeneralCardLayoutTest, LoadingMoreThemesInvalidatesSharedCardDefinitions)
{
	const uint64_t Before = qm_card_catalog::MeasureContentRevision(8, 3);
	EXPECT_NE(qm_card_catalog::MeasureContentRevision(8, 7), Before);
}

TEST_F(CQmGeneralCardLayoutTest, UpdatingTheLanguageListInvalidatesSharedCardDefinitions)
{
	const uint64_t Before = qm_card_catalog::MeasureContentRevision(3, 8);
	EXPECT_NE(qm_card_catalog::MeasureContentRevision(7, 8), Before);
}
