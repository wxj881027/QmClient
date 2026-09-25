#include "test.h"

#include <game/client/QmUi/cards/QmMapUploadSearch.h>

#include <gtest/gtest.h>

TEST(QmMapUploadSearch, FindsNestedMapsInBothRootsAndExcludesOtherFiles)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	for(const char *pFolder : {"maps", "maps/nested", "downloadedmaps", "downloadedmaps/nested"})
		ASSERT_TRUE(pStorage->CreateFolder(pFolder, IStorage::TYPE_SAVE));
	for(const char *pPath : {"maps/nested/Test.map", "downloadedmaps/nested/TEST.MAP", "maps/nested/Test.txt", "Outside.map"})
	{
		IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		io_close(File);
	}

	qm_map_upload::CSearchIndex Index;
	Index.Reset(1);
	ASSERT_TRUE(Index.Busy());
	for(int Step = 0; Step < 100 && Index.Busy(); ++Step)
		Index.ScanNext(pStorage.get());
	ASSERT_FALSE(Index.Busy());
	const auto vMatches = Index.Find("test");
	ASSERT_EQ(vMatches.size(), 2u);
	EXPECT_STREQ(vMatches[0].m_aPath, "downloadedmaps/nested/TEST.MAP");
	EXPECT_STREQ(vMatches[1].m_aPath, "maps/nested/Test.map");
	EXPECT_EQ(vMatches[0].m_StorageType, IStorage::TYPE_SAVE);
	EXPECT_EQ(vMatches[1].m_StorageType, IStorage::TYPE_SAVE);
	EXPECT_TRUE(Index.Find("nested").empty());
	EXPECT_TRUE(Index.Find("missing").empty());
	EXPECT_EQ(Index.Find("").size(), 2u);
	Index.Reset(0);
	EXPECT_FALSE(Index.Busy());
	EXPECT_TRUE(Index.Find("test").empty());
}

TEST(QmMapUploadSearch, MatchesChineseAndPartialNamesWithoutMatchingFolderNames)
{
	qm_map_upload::SMapFile File;
	str_copy(File.m_aFilename, "测试地图ABC.map");
	str_copy(File.m_aPath, "maps/other/测试地图ABC.map");
	EXPECT_TRUE(qm_map_upload::MatchesMapName(File, "地图ab"));
	EXPECT_TRUE(qm_map_upload::MatchesMapName(File, "测试"));
	EXPECT_TRUE(qm_map_upload::MatchesMapName(File, ""));
	EXPECT_FALSE(qm_map_upload::MatchesMapName(File, "other"));
	File.m_IsDirectory = true;
	EXPECT_FALSE(qm_map_upload::MatchesMapName(File, "测试"));
}
