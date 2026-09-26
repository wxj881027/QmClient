#include <game/client/components/qmclient/local_saves.h>

#include <gtest/gtest.h>

TEST(QmLocalSaves, ParsesQuotedRowsAndPreservesOtherRecordsOnRemoval)
{
	const std::string Text = "Time,Players,Map,Code\n"
				 "2026,\"A, B\",Map,secret\n"
				 "2025,Other,OtherMap,secret\n"
				 "bad,row\n";
	const auto Entries = QmLocalSaves::ParseEntries(Text);
	ASSERT_EQ(Entries.size(), 2u);
	EXPECT_EQ(Entries[0].m_Players, "A, B");
	EXPECT_EQ(Entries[0].m_Code, "secret");
	std::string Updated;
	ASSERT_TRUE(QmLocalSaves::RemoveEntries(Text, "map", "secret", Updated));
	EXPECT_EQ(Updated, "Time,Players,Map,Code\n2025,Other,OtherMap,secret\nbad,row\n");
	std::string Unchanged;
	EXPECT_FALSE(QmLocalSaves::RemoveEntries(Updated, "Map", "secret", Unchanged));
}

TEST(QmLocalSaves, RejectsInvalidRepliesAndSelectsNewestDistinctCodes)
{
	EXPECT_EQ(QmLocalSaves::ParseReply("/qm Yes 2").m_Index, 2);
	EXPECT_EQ(QmLocalSaves::ParseReply("/qm No").m_Kind, QmLocalSaves::EReply::NO);
	EXPECT_EQ(QmLocalSaves::ParseReply("/qm Yes 0").m_Kind, QmLocalSaves::EReply::INVALID);
	EXPECT_EQ(QmLocalSaves::ParseReply("/qmore Yes").m_Kind, QmLocalSaves::EReply::NONE);
	const auto Entries = QmLocalSaves::ParseEntries(
		"Time,Players,Map,Code\n"
		"2024,\"Main, Dummy\",Map,old\n"
		"2025,\"Main, Dummy\",Map,new\n"
		"2023,\"Main, Dummy\",Map,new\n"
		"2026,\"Main, Dummy\",Other,other\n");
	const auto Candidates = QmLocalSaves::Candidates(Entries, "Map");
	ASSERT_EQ(Candidates.size(), 2u);
	EXPECT_EQ(Candidates[0].m_Code, "new");
	EXPECT_EQ(Candidates[1].m_Code, "old");
}

TEST(QmLocalSaves, KeepsRecordUntilConfirmedLoad)
{
	QmLocalSaves::CConfirmation Confirmation;
	QmLocalSaves::CConfirmation::SRequest Request;
	Request.m_Map = "Map";
	Request.m_Code = "secret";
	Request.m_Conn = 0;
	Request.m_Team = 1;
	Request.m_SentTick = 100;
	Request.m_Deadline = 500;
	Confirmation.Track(Request);
	EXPECT_EQ(Confirmation.Message(0, -1, "No such savegame for this map", "Map", 1, 200), QmLocalSaves::EResult::FAILED);
	EXPECT_EQ(Confirmation.Message(0, -1, "Loading successfully done", "Other", 1, 200), QmLocalSaves::EResult::NONE);
	EXPECT_EQ(Confirmation.Message(0, -1, "Loading successfully done", "Map", 1, 501), QmLocalSaves::EResult::NONE);
	EXPECT_EQ(Confirmation.Message(0, -1, "Loading successfully done", "Map", 1, 200), QmLocalSaves::EResult::SUCCESS);
	Confirmation.Reset();
	EXPECT_FALSE(Confirmation.Active());
}

TEST(QmLocalSaves, RestoreWaitsForBothPlayersAndRejectsForeignTeamMember)
{
	QmLocalSaves::CRestore Restore;
	QmLocalSaves::SEntry Entry{"2026", "Main, Dummy", "Map", "secret"};
	Restore.Begin(Entry, {"Main", "Dummy"}, 100, 10);
	QmLocalSaves::CRestore::SWorld World;
	World.m_Map = "Map";
	World.m_Online = true;
	EXPECT_EQ(Restore.Update(World, 100, 10), QmLocalSaves::EAction::CONNECT);
	World.m_DummyConnected = true;
	World.m_PlayersReady = true;
	World.m_CharactersReady = true;
	World.m_aNames = {"Main", "Dummy"};
	EXPECT_EQ(Restore.Update(World, 110, 10), QmLocalSaves::EAction::JOIN_MAIN);
	const int Team = Restore.Team();
	ASSERT_GT(Team, 0);
	World.m_aTeams[0] = Team;
	World.m_aTeamSizes[Team] = 2;
	EXPECT_EQ(Restore.Update(World, 120, 10), QmLocalSaves::EAction::FAILED);
	Restore.Reset();
	EXPECT_FALSE(Restore.Active());
}
