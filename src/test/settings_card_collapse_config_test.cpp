#include <game/client/QmUi/SettingsCardCollapseState.h>

#include <base/str.h>
#include <engine/shared/config.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	class CSettingsCardCollapseConfig : public testing::Test
	{
		std::string m_States = g_Config.m_QmSettingsCardCollapsed;
		std::string m_Legacy = g_Config.m_QmSidebarCardCollapsed;
		int m_Migrated = g_Config.m_QmSettingsCardCollapseMigrated;

	protected:
		void SetUp() override
		{
			g_Config.m_QmSettingsCardCollapsed[0] = '\0';
			g_Config.m_QmSidebarCardCollapsed[0] = '\0';
			g_Config.m_QmSettingsCardCollapseMigrated = 0;
		}

		void TearDown() override
		{
			str_copy(g_Config.m_QmSettingsCardCollapsed, m_States.c_str());
			str_copy(g_Config.m_QmSidebarCardCollapsed, m_Legacy.c_str());
			g_Config.m_QmSettingsCardCollapseMigrated = 1;
			qm_card_collapse::SyncFromConfig();
			g_Config.m_QmSettingsCardCollapseMigrated = m_Migrated;
		}
	};

	TEST_F(CSettingsCardCollapseConfig, LegacyStateIsImportedOnlyOnce)
	{
		str_copy(g_Config.m_QmSidebarCardCollapsed, "translate;voice");
		EXPECT_TRUE(qm_card_collapse::IsCollapsed("qm:translate"));
		EXPECT_TRUE(qm_card_collapse::IsCollapsed("qm:voice"));
		EXPECT_EQ(g_Config.m_QmSettingsCardCollapseMigrated, 1);
		ASSERT_TRUE(qm_card_collapse::SetCollapsed("qm:translate", false));
		g_Config.m_QmSettingsCardCollapsed[0] = '\0';
		qm_card_collapse::SyncFromConfig();
		EXPECT_FALSE(qm_card_collapse::IsCollapsed("qm:translate"));
		EXPECT_FALSE(qm_card_collapse::IsCollapsed("qm:voice"));
	}

	TEST_F(CSettingsCardCollapseConfig, QmPageAndSearchUseTheSameSavedState)
	{
		std::array<bool, qm_module::QmModuleCount> Page{};
		std::array<bool, qm_module::QmModuleCount> Search{};
		ASSERT_TRUE(qm_card_collapse::SetQmModuleCollapsed(qm_module::EQmModuleId::Translate, true));
		qm_card_collapse::SyncQmModules(Page);
		qm_card_collapse::SyncQmModules(Search);
		EXPECT_TRUE(Page[(int)qm_module::EQmModuleId::Translate]);
		EXPECT_TRUE(Search[(int)qm_module::EQmModuleId::Translate]);
		ASSERT_TRUE(qm_card_collapse::SetCollapsed("qm:translate", false));
		qm_card_collapse::SyncQmModules(Page);
		EXPECT_FALSE(Page[(int)qm_module::EQmModuleId::Translate]);
	}

	TEST_F(CSettingsCardCollapseConfig, ExternalConfigEditReplacesCachedState)
	{
		ASSERT_TRUE(qm_card_collapse::SetCollapsed("deck:controls-movement", true));
		str_copy(g_Config.m_QmSettingsCardCollapsed, "!deck:controls-movement;deck:graphics-icons");
		EXPECT_FALSE(qm_card_collapse::IsCollapsed("deck:controls-movement"));
		EXPECT_TRUE(qm_card_collapse::IsCollapsed("deck:graphics-icons"));
	}

	TEST_F(CSettingsCardCollapseConfig, FullConfigRejectsMutationWithoutLosingState)
	{
		const std::string FullId(sizeof(g_Config.m_QmSettingsCardCollapsed) - 1, 'a');
		str_copy(g_Config.m_QmSettingsCardCollapsed, FullId.c_str());
		g_Config.m_QmSettingsCardCollapseMigrated = 1;
		EXPECT_FALSE(qm_card_collapse::SetCollapsed("qm:voice", true));
		EXPECT_STREQ(g_Config.m_QmSettingsCardCollapsed, FullId.c_str());
		EXPECT_TRUE(qm_card_collapse::IsCollapsed(FullId.c_str()));
		EXPECT_FALSE(qm_card_collapse::IsCollapsed("qm:voice"));
	}
}
