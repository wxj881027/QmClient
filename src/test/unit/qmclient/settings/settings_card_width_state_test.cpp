#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsCardWidthState.h>

#include <gtest/gtest.h>

#include <string>

TEST(SettingsCardWidthState, ExplicitWidthsSurviveReloadAndDefaultChanges)
{
	qm_card_width::CState State;
	ASSERT_TRUE(State.SetFullWidth("deck:left", true));
	ASSERT_TRUE(State.SetFullWidth("deck:full", false));
	char aSerialized[64];
	ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
	EXPECT_STREQ(aSerialized, "!deck:full;deck:left");

	qm_card_width::CState Reloaded;
	ASSERT_TRUE(Reloaded.Load(aSerialized));
	EXPECT_TRUE(Reloaded.IsFullWidth("deck:left", false));
	EXPECT_FALSE(Reloaded.IsFullWidth("deck:full", true));
	EXPECT_FALSE(Reloaded.SetFullWidth("deck:left", true));
	EXPECT_FALSE(Reloaded.SetFullWidth("deck:full", false));
}

TEST(SettingsCardWidthState, MissingOverrideUsesCardDefaultAndReloadReplacesOverrides)
{
	qm_card_width::CState State;
	EXPECT_TRUE(State.IsFullWidth("deck:full", true));
	EXPECT_FALSE(State.IsFullWidth("deck:left", false));
	ASSERT_TRUE(State.SetFullWidth("deck:left", true));
	ASSERT_TRUE(State.Load("!deck:full"));
	EXPECT_FALSE(State.IsFullWidth("deck:left", false));
	EXPECT_FALSE(State.IsFullWidth("deck:full", true));
}

TEST(SettingsCardWidthState, InvalidIdsDoNotChangeStoredWidths)
{
	qm_card_width::CState State;
	ASSERT_TRUE(State.Load("deck:left"));
	const char *apInvalidIds[] = {nullptr, "", "!deck:right", "deck:right;deck:left", "deck:right\n"};
	for(const char *pId : apInvalidIds)
		EXPECT_FALSE(State.SetFullWidth(pId, false));
	char aSerialized[32];
	ASSERT_TRUE(State.Serialize(aSerialized, sizeof(aSerialized)));
	EXPECT_STREQ(aSerialized, "deck:left");
}

namespace
{
	class CSettingsCardWidthConfig : public testing::Test
	{
		std::string m_PreviousWidths = g_Config.m_QmSettingsCardWidths;

	protected:
		void SetUp() override
		{
			g_Config.m_QmSettingsCardWidths[0] = '\0';
			qm_card_width::SyncFromConfig();
		}

		void TearDown() override
		{
			str_copy(g_Config.m_QmSettingsCardWidths, m_PreviousWidths.c_str());
			qm_card_width::SyncFromConfig();
		}
	};
}

TEST_F(CSettingsCardWidthConfig, ReadersShareSavedWidthWithoutChangingOrderOrCollapse)
{
	const std::string Order = g_Config.m_QmGlobalCardOrder;
	const std::string Collapsed = g_Config.m_QmSettingsCardCollapsed;
	ASSERT_TRUE(qm_card_width::SetFullWidth("qm:translate", true));
	EXPECT_TRUE(qm_card_width::CurrentState().IsFullWidth("qm:translate", false));
	EXPECT_TRUE(qm_card_width::IsFullWidth("qm:translate"));
	EXPECT_STREQ(g_Config.m_QmSettingsCardWidths, "qm:translate");
	ASSERT_TRUE(qm_card_width::SetFullWidth("qm:translate", false));
	EXPECT_FALSE(qm_card_width::IsFullWidth("qm:translate", true));
	EXPECT_STREQ(g_Config.m_QmSettingsCardWidths, "!qm:translate");
	EXPECT_STREQ(g_Config.m_QmGlobalCardOrder, Order.c_str());
	EXPECT_STREQ(g_Config.m_QmSettingsCardCollapsed, Collapsed.c_str());
}

TEST_F(CSettingsCardWidthConfig, ExternalConfigEditReplacesCachedWidths)
{
	ASSERT_TRUE(qm_card_width::SetFullWidth("deck:left", true));
	str_copy(g_Config.m_QmSettingsCardWidths, "!deck:left;deck:right");
	EXPECT_FALSE(qm_card_width::IsFullWidth("deck:left", true));
	EXPECT_TRUE(qm_card_width::IsFullWidth("deck:right", false));
	g_Config.m_QmSettingsCardWidths[0] = '\0';
	EXPECT_FALSE(qm_card_width::IsFullWidth("deck:right", false));
}

TEST_F(CSettingsCardWidthConfig, FullConfigRejectsMutationWithoutChangingSavedOrCachedWidths)
{
	const std::string FullId(sizeof(g_Config.m_QmSettingsCardWidths) - 1, 'a');
	str_copy(g_Config.m_QmSettingsCardWidths, FullId.c_str());
	ASSERT_TRUE(qm_card_width::IsFullWidth(FullId.c_str()));
	EXPECT_FALSE(qm_card_width::SetFullWidth("deck:right", true));
	EXPECT_FALSE(qm_card_width::IsFullWidth("deck:right"));
	EXPECT_TRUE(qm_card_width::IsFullWidth(FullId.c_str()));
	EXPECT_STREQ(g_Config.m_QmSettingsCardWidths, FullId.c_str());
	// 同一个 ID 从全宽改为半宽也会多占一个字符，不能发布未保存的状态。
	EXPECT_FALSE(qm_card_width::SetFullWidth(FullId.c_str(), false));
	EXPECT_TRUE(qm_card_width::IsFullWidth(FullId.c_str()));
	EXPECT_STREQ(g_Config.m_QmSettingsCardWidths, FullId.c_str());
}
