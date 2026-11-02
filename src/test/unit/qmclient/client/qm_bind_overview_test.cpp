#include <game/client/components/qmclient/bind_overview.h>

#include <gtest/gtest.h>

#include <iterator>
#include <vector>

using namespace qm_bind_overview;

TEST(QmBindOverview, KnownCommandsMapToTheirGroups)
{
	EXPECT_EQ(GroupForCommand("+left"), EBindOptionGroup::MOVEMENT);
	EXPECT_EQ(GroupForCommand("+right"), EBindOptionGroup::MOVEMENT);
	EXPECT_EQ(GroupForCommand("kill"), EBindOptionGroup::MOVEMENT);
	EXPECT_EQ(GroupForCommand("+weapon1"), EBindOptionGroup::WEAPON);
	EXPECT_EQ(GroupForCommand("+nextweapon"), EBindOptionGroup::WEAPON);
	EXPECT_EQ(GroupForCommand("vote yes"), EBindOptionGroup::VOTING);
	EXPECT_EQ(GroupForCommand("+show_chat; chat all"), EBindOptionGroup::CHAT);
	EXPECT_EQ(GroupForCommand("+qm_voice_ptt"), EBindOptionGroup::CHAT);
	EXPECT_EQ(GroupForCommand("toggle cl_dummy 0 1"), EBindOptionGroup::DUMMY);
	EXPECT_EQ(GroupForCommand("+toggle_restore cl_dummy_hook 1"), EBindOptionGroup::DUMMY);
	EXPECT_EQ(GroupForCommand("screenshot"), EBindOptionGroup::MISCELLANEOUS);
	EXPECT_EQ(GroupForCommand("qm_timeout_disconnect"), EBindOptionGroup::MISCELLANEOUS);
}

TEST(QmBindOverview, UnknownCommandsFallBackToCustom)
{
	// 默认键位与特殊命令里不在已知动作表中的条目归入“其他”（CUSTOM）。
	EXPECT_EQ(GroupForCommand("say /spec"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand("+scoreboard"), EBindOptionGroup::MISCELLANEOUS);
	EXPECT_EQ(GroupForCommand("bindfile"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand("toggle dbg_graphs 0 1"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand(""), EBindOptionGroup::CUSTOM);
}

TEST(QmBindOverview, CommandMatchIsExactNotPrefixOrCaseInsensitive)
{
	EXPECT_EQ(GroupForCommand("+lef"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand("+left2"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand("+left "), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand(" +left"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand("+LEFT"), EBindOptionGroup::CUSTOM);
	EXPECT_EQ(GroupForCommand("Kill"), EBindOptionGroup::CUSTOM);
}

TEST(QmBindOverview, EveryKnownActionResolvesToItsDeclaredGroup)
{
	for(const SKnownBindAction &Action : s_aKnownBindActions)
	{
		EXPECT_EQ(GroupForCommand(Action.m_pCommand), Action.m_Group) << Action.m_pCommand;
	}
}

TEST(QmBindOverview, KnownActionCommandsAreUnique)
{
	// 表内命令重复会让 GroupForCommand 命中错误分组，必须保持唯一。
	for(size_t Left = 0; Left < std::size(s_aKnownBindActions); ++Left)
	{
		for(size_t Right = Left + 1; Right < std::size(s_aKnownBindActions); ++Right)
		{
			EXPECT_NE(str_comp(s_aKnownBindActions[Left].m_pCommand, s_aKnownBindActions[Right].m_pCommand), 0) << s_aKnownBindActions[Left].m_pCommand;
		}
	}
}

TEST(QmBindOverview, SerializeWritesKeyEqualsCommandPerLine)
{
	const std::vector<SBindOverviewEntry> Entries = {
		{"A", "+left"},
		{"ctrl+shift+g", "toggle dbg_graphs 0 1"},
		{"mouse1", "+fire"},
	};
	EXPECT_EQ(Serialize(Entries),
		"A = +left\n"
		"ctrl+shift+g = toggle dbg_graphs 0 1\n"
		"mouse1 = +fire\n");
}

TEST(QmBindOverview, SerializeEmptyListProducesEmptyText)
{
	EXPECT_EQ(Serialize({}), "");
	EXPECT_TRUE(Serialize(std::vector<SBindOverviewEntry>{}).empty());
}
