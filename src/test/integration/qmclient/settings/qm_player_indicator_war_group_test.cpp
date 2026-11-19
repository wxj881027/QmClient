#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>

#include <gtest/gtest.h>

namespace
{
	constexpr char ENEMY_ROW[] = "warlist-enemy";
	constexpr char TEAM_ROW[] = "warlist-team";

	void DeclareIndicatorGroupRows(const CQmWarListModel &WarList, std::vector<SSettingsContentRowDeclaration> &vRows)
	{
		CSettingsContentLayoutScope Scope(vRows);
		CUIRect Content{0.0f, 0.0f, 300.0f, 0.0f};
		CSettingsContentLayoutFlow Flow(Content);
		CUIRect Row;
		qm_tclient_cards::PlacePlayerIndicatorWarGroupRow(Flow, Row, WarList, WARLIST_BUILTIN_GROUP_ENEMY, ENEMY_ROW, 20.0f, 4.0f);
		qm_tclient_cards::PlacePlayerIndicatorWarGroupRow(Flow, Row, WarList, WARLIST_BUILTIN_GROUP_TEAM, TEAM_ROW, 20.0f, 4.0f);
	}
}

TEST(QmPlayerIndicatorWarGroup, OmitsOnlyTheDeletedBuiltinRows)
{
	CQmWarListModel Model;
	const ColorRGBA CustomColor(0.2f, 0.3f, 0.4f, 1.0f);
	Model.RemoveWarType("enemy");
	ASSERT_TRUE(Model.AddWarType("enemy", CustomColor));
	ASSERT_NE(Model.FindWarType("enemy"), Model.m_pWarTypeNone);

	std::vector<SSettingsContentRowDeclaration> vRows;
	DeclareIndicatorGroupRows(Model, vRows);
	ASSERT_EQ(vRows.size(), 1u);
	EXPECT_EQ(vRows.front().m_Id, TEAM_ROW);

	Model.RemoveWarType("team");
	vRows.clear();
	DeclareIndicatorGroupRows(Model, vRows);
	EXPECT_TRUE(vRows.empty());
	EXPECT_NE(Model.FindWarType("enemy"), Model.m_pWarTypeNone);
}

TEST(QmPlayerIndicatorWarGroup, ReplayUsesTheRenamedBuiltinGroupsAfterReordering)
{
	CQmWarListModel Model;
	const ColorRGBA Color(0.2f, 0.3f, 0.4f, 1.0f);
	const CWarType *pEnemy = Model.FindWarType("enemy");
	const CWarType *pTeam = Model.FindWarType("team");
	ASSERT_TRUE(Model.UpsertWarType(WARLIST_BUILTIN_GROUP_ENEMY, "rivals", Color));
	ASSERT_TRUE(Model.UpsertWarType(WARLIST_BUILTIN_GROUP_TEAM, "allies", Color));
	ASSERT_TRUE(Model.AddWarType("enemy", Color));
	Model.MoveWarType(WARLIST_BUILTIN_GROUP_ENEMY, 3);

	CSettingsContentLayoutCache Cache;
	auto &vRows = Cache.ScratchDeclarations();
	DeclareIndicatorGroupRows(Model, vRows);
	const auto pLayout = Cache.Resolve(vRows, 300.0f, 0, {});
	ASSERT_NE(pLayout->Find(ENEMY_ROW), nullptr);
	ASSERT_NE(pLayout->Find(TEAM_ROW), nullptr);

	CSettingsContentLayoutScope Scope(*pLayout, ESettingsContentPass::DRAW);
	CUIRect Content{0.0f, 0.0f, 300.0f, pLayout->Height()};
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row;
	const CWarType *pEnemyRow = qm_tclient_cards::PlacePlayerIndicatorWarGroupRow(Flow, Row, Model, WARLIST_BUILTIN_GROUP_ENEMY, ENEMY_ROW, 20.0f, 4.0f);
	const CWarType *pTeamRow = qm_tclient_cards::PlacePlayerIndicatorWarGroupRow(Flow, Row, Model, WARLIST_BUILTIN_GROUP_TEAM, TEAM_ROW, 20.0f, 4.0f);
	ASSERT_NE(pEnemyRow, nullptr);
	ASSERT_NE(pTeamRow, nullptr);
	EXPECT_EQ(pEnemyRow, pEnemy);
	EXPECT_EQ(pTeamRow, pTeam);
	EXPECT_STREQ(pEnemyRow->m_aWarName, "rivals");
	EXPECT_STREQ(pTeamRow->m_aWarName, "allies");
}

TEST(QmPlayerIndicatorWarGroup, CustomMatchInTheOldTeamSlotDoesNotRestoreTheDeletedGroup)
{
	CQmWarListModel Model;
	Model.RemoveWarType("team");
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "reason", "custom"));
	Model.RefreshWarTypeIndices();

	CWarDataCache Cache;
	ASSERT_TRUE(Cache.Reset(Model.m_WarTypes.size()));
	Model.VisitMatches("Alice", "", [&](const CWarEntry &Entry, bool ByName) {
		Cache.ApplyMatch(Entry, ByName);
		return true;
	});
	ASSERT_EQ(Model.FindWarType("custom")->m_Index, WARLIST_BUILTIN_GROUP_TEAM);
	EXPECT_TRUE(Cache.m_WarGroupMatches[WARLIST_BUILTIN_GROUP_TEAM]);
	EXPECT_EQ(Model.FindActiveBuiltinWarType(WARLIST_BUILTIN_GROUP_TEAM), nullptr);
}
