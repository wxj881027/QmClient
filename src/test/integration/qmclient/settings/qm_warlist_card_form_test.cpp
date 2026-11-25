#include <game/client/QmUi/cards/QmWarListCardModel.h>

#include <gtest/gtest.h>

using namespace qm_warlist_card;

TEST(QmWarListCardForm, CandidateWithClanStillAddsOnlyThePlayerInPlayerMode)
{
	CQmWarListModel Model;
	const auto Player = CandidateIdentity(EMatch::PLAYER, "Alice", "Runners");
	ASSERT_TRUE(Player.Valid());
	ASSERT_TRUE(Model.AddWarEntry(Player.Name(), Player.Clan(), "", TargetGroup(Model, nullptr)->m_aWarName));

	ASSERT_EQ(Model.m_vWarEntries.size(), 1u);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aName, "Alice");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aClan, "");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_pWarType->m_aWarName, "enemy");
}

TEST(QmWarListCardForm, ClanModeRequiresAClanAndNeverFallsBackToPlayer)
{
	CQmWarListModel Model;
	CEntryForm Form;
	EXPECT_FALSE(Form.Prefill(Model, CandidateIdentity(EMatch::CLAN, "Alice", ""), TargetGroup(Model, nullptr)));
	EXPECT_FALSE(Form.Submit(Model));

	ASSERT_TRUE(Form.Prefill(Model, CandidateIdentity(EMatch::CLAN, "Alice", "Runners"), TargetGroup(Model, nullptr)));
	str_copy(Form.m_aReason, "Review");
	ASSERT_TRUE(Form.Submit(Model));
	ASSERT_EQ(Model.m_vWarEntries.size(), 1u);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aName, "");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aClan, "Runners");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aReason, "Review");
	EXPECT_FALSE(Form.Identity().Valid());
	EXPECT_STREQ(Form.m_aReason, "");
}

TEST(QmWarListCardForm, ExplicitNonePersistsAndReorderingKeepsTheEnemyDefault)
{
	CQmWarListModel Model;
	const CWarType *pEnemy = Model.FindActiveBuiltinWarType(WARLIST_BUILTIN_GROUP_ENEMY);
	ASSERT_NE(pEnemy, nullptr);
	Model.MoveWarType(1, 2);
	EXPECT_EQ(TargetGroup(Model, nullptr), pEnemy);
	EXPECT_EQ(TargetGroup(Model, Model.m_pWarTypeNone), Model.m_pWarTypeNone);
}

TEST(QmWarListCardForm, EditingUsesOneFieldEvenForLegacyEntriesWithBothFields)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "Original", "team"));
	Model.UpdateWarEntry(0, "Alice", "Old clan", "Original", Model.FindWarType("team"));
	CEntryForm Form;
	Form.ToggleSelection(Model, 0);
	EXPECT_EQ(Form.Match(), EMatch::PLAYER);
	EXPECT_STREQ(Form.Identity().m_pText, "Alice");
	EXPECT_STREQ(Form.m_aReason, "Original");
	EXPECT_EQ(Form.m_pGroup, Model.FindWarType("team"));

	Form.ChangeMatch(EMatch::CLAN);
	EXPECT_FALSE(Form.Submit(Model));
	str_copy(Form.m_aClan, "Runners");
	ASSERT_TRUE(Form.Submit(Model));
	EXPECT_EQ(Form.Selected(Model), 0);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aName, "");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aClan, "Runners");
}

TEST(QmWarListCardForm, CandidatePrefillDoesNotOverwriteAnActiveEdit)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "Original", "enemy"));
	CEntryForm Form;
	Form.ToggleSelection(Model, 0);

	EXPECT_FALSE(Form.Prefill(Model, CandidateIdentity(EMatch::PLAYER, "Bob", ""), Model.m_pWarTypeNone));
	EXPECT_STREQ(Form.Identity().m_pText, "Alice");
	Form.ToggleSelection(Model, 0);
	EXPECT_EQ(Form.Selected(Model), -1);
	EXPECT_FALSE(Form.Identity().Valid());
	EXPECT_STREQ(Form.m_aReason, "");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aReason, "Original");
}

TEST(QmWarListCardForm, StaleEditCannotWriteToAnEntryThatMovedIntoItsIndex)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("Alice", "", "", "enemy"));
	ASSERT_TRUE(Model.AddWarEntry("Bob", "", "", "enemy"));
	CEntryForm Form;
	Form.ToggleSelection(Model, 0);
	str_copy(Form.m_aReason, "Edited");
	Model.RemoveWarEntry("Alice", "", "enemy");

	EXPECT_FALSE(Form.Submit(Model));
	EXPECT_TRUE(Form.Synchronize(Model));
	ASSERT_EQ(Model.m_vWarEntries.size(), 1u);
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aName, "Bob");
	EXPECT_STREQ(Model.m_vWarEntries[0].m_aReason, "");
	EXPECT_FALSE(Form.Identity().Valid());
}

TEST(QmWarListCardForm, EntrySearchMatchesReasonAndGroupTogetherWithTheGroupFilter)
{
	CQmWarListModel Model;
	ASSERT_TRUE(Model.AddWarEntry("", "Runners", "Repeated griefing", "enemy"));
	const auto &Entry = Model.m_vWarEntries.front();

	EXPECT_TRUE(MatchesEntryFilter(Entry, "runners", nullptr));
	EXPECT_TRUE(MatchesEntryFilter(Entry, "GRIEF", Model.FindWarType("enemy")));
	EXPECT_TRUE(MatchesEntryFilter(Entry, "enemy", nullptr));
	EXPECT_FALSE(MatchesEntryFilter(Entry, "GRIEF", Model.FindWarType("team")));
}
