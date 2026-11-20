#include <engine/storage.h>

#include <game/client/components/qmclient/rank_ghost.h>

#include <gtest/gtest.h>
#include <test/test.h>

TEST(QmRankGhostLegacyCleanupIntegration, RemovesOnlyLegacyGhostFiles)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("ghosts", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder(CRankGhost::LEGACY_GHOST_DIR, IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder("ghosts/rank_ghost/team", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->CreateFolder("ghosts/rank_ghost/team/archive", IStorage::TYPE_SAVE));

	constexpr const char *PLAYER_GHOST = "ghosts/player-best.gho";
	constexpr const char *LEGACY_GHOST = "ghosts/rank_ghost/old.gho";
	constexpr const char *TEAM_GHOST = "ghosts/rank_ghost/team/old.gho";
	constexpr const char *DEEP_GHOST = "ghosts/rank_ghost/team/archive/archive.gho";
	constexpr const char *OTHER_FILE = "ghosts/rank_ghost/team/notes.txt";
	for(const char *pPath : {PLAYER_GHOST, LEGACY_GHOST, TEAM_GHOST, DEEP_GHOST, OTHER_FILE})
	{
		const IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr) << pPath;
		ASSERT_EQ(io_close(File), 0) << pPath;
	}

	qmclient::rank_ghost::CleanupLegacyGhosts(*pStorage, CRankGhost::LEGACY_GHOST_DIR);

	EXPECT_TRUE(pStorage->FileExists(PLAYER_GHOST, IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists(LEGACY_GHOST, IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists(TEAM_GHOST, IStorage::TYPE_SAVE));
	EXPECT_TRUE(pStorage->FileExists(DEEP_GHOST, IStorage::TYPE_SAVE));
	EXPECT_TRUE(pStorage->FileExists(OTHER_FILE, IStorage::TYPE_SAVE));
}
