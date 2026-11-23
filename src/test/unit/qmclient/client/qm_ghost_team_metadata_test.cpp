#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/ghost_team_metadata.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <vector>

TEST(QmGhostTeamMetadata, RoundTripsPerTrackTeams)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	const std::vector<SGhostTeamEntry> vExpected = {
		{"00.gho", 3},
		{"01.gho", 7},
	};
	ASSERT_TRUE(QmWriteGhostTeamMetadata(pStorage.get(), "teams.qmtm", vExpected));

	std::vector<SGhostTeamEntry> vActual;
	ASSERT_TRUE(QmReadGhostTeamMetadata(pStorage.get(), "teams.qmtm", vActual));
	ASSERT_EQ(vActual.size(), 2u);
	EXPECT_EQ(QmGhostTeamForFile(vActual, "00.gho"), 3);
	EXPECT_EQ(QmGhostTeamForFile(vActual, "01.gho"), 7);
	EXPECT_EQ(QmGhostTeamForFile(vActual, "missing.gho"), -1);
}

TEST(QmGhostTeamMetadata, RejectsCorruptOrUnversionedFiles)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	IOHANDLE File = pStorage->OpenFile("broken.qmtm", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const char aBroken[] = "00.gho\t4\n";
	ASSERT_EQ(io_write(File, aBroken, sizeof(aBroken) - 1), sizeof(aBroken) - 1);
	ASSERT_EQ(io_close(File), 0);

	std::vector<SGhostTeamEntry> vEntries;
	EXPECT_FALSE(QmReadGhostTeamMetadata(pStorage.get(), "broken.qmtm", vEntries));
	EXPECT_TRUE(vEntries.empty());
}

TEST(QmGhostTeamMetadata, UnknownTeamsAreNotPublished)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	const std::vector<SGhostTeamEntry> vExpected = {
		{"00.gho", -1},
		{"01.gho", 5},
	};
	ASSERT_TRUE(QmWriteGhostTeamMetadata(pStorage.get(), "teams.qmtm", vExpected));

	std::vector<SGhostTeamEntry> vActual;
	ASSERT_TRUE(QmReadGhostTeamMetadata(pStorage.get(), "teams.qmtm", vActual));
	ASSERT_EQ(vActual.size(), 1u);
	EXPECT_EQ(vActual[0].m_Filename, "01.gho");
	EXPECT_EQ(vActual[0].m_Team, 5);
}
