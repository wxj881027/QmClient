#include "test.h"

#include <game/client/components/qmclient/qm_map_upload.h>

#include <gtest/gtest.h>
TEST(QmMapUpload, ValidatesSafeMapNames)
{
	EXPECT_TRUE(QmMapUpload::IsMapFilename("test.map"));
	EXPECT_FALSE(QmMapUpload::IsMapFilename("test.txt"));
	EXPECT_TRUE(QmMapUpload::ValidateFilename("test.map"));
	EXPECT_FALSE(QmMapUpload::ValidateFilename("../test.map"));
	EXPECT_FALSE(QmMapUpload::ValidateFilename("test .map"));
}
TEST(QmMapUpload, BuildsAndParses)
{
	const unsigned char Data[] = {'m', 'a', 'p'};
	std::string Body;
	ASSERT_TRUE(QmMapUpload::BuildMultipart("test.map", "player", Data, sizeof(Data), "Boundary-1", Body));
	EXPECT_NE(Body.find("filename=\"test.map\""), std::string::npos);
	const char *pResponse = "{\"success\":true,\"message\":\"ok\"}";
	auto R = QmMapUpload::ParseResponse(200, pResponse, str_length(pResponse));
	EXPECT_TRUE(R.m_Success);
}

TEST(QmMapUpload, RejectsMultipartBoundaryInFields)
{
	const unsigned char Data[] = {'m', 'a', 'p'};
	std::string Body;
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "player", Data, sizeof(Data), "test", Body));
	EXPECT_TRUE(Body.empty());
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "player", Data, sizeof(Data), "player", Body));
	EXPECT_TRUE(Body.empty());
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "player", Data, sizeof(Data), "map", Body));
	EXPECT_TRUE(Body.empty());
}

TEST(QmMapUpload, UploadLifecycleRejectsUnconfiguredEndpoints)
{
	QmMapUpload::CUpload Upload;
	Upload.Start(nullptr, nullptr, nullptr, "ftp://invalid", "maps/test.map", 0, "player");
	EXPECT_EQ(Upload.Status(), QmMapUpload::EStatus::INVALID_ENDPOINT);
	EXPECT_FALSE(Upload.Busy());
}

TEST(QmMapUpload, SearchIndexMatchesNestedMapsWithoutMatchingFolders)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	for(const char *pFolder : {"maps", "maps/nested", "downloadedmaps", "downloadedmaps/other"})
		ASSERT_TRUE(pStorage->CreateFolder(pFolder, IStorage::TYPE_SAVE));
	for(const char *pPath : {"maps/nested/Test.map", "downloadedmaps/other/TEST.MAP", "maps/nested/Test.txt", "Outside.map"})
	{
		IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		io_close(File);
	}
	QmMapUpload::CSearchIndex Index;
	Index.Reset(1);
	ASSERT_TRUE(Index.Busy());
	for(int Step = 0; Step < 100 && Index.Busy(); ++Step)
		Index.ScanNext(pStorage.get());
	ASSERT_FALSE(Index.Busy());
	const auto Matches = Index.Find("test");
	ASSERT_EQ(Matches.size(), 2u);
	EXPECT_STREQ(Matches[0].m_aPath, "downloadedmaps/other/TEST.MAP");
	EXPECT_STREQ(Matches[1].m_aPath, "maps/nested/Test.map");
	EXPECT_TRUE(Index.Find("nested").empty());
	EXPECT_TRUE(Index.Find("missing").empty());
	EXPECT_EQ(Index.Find("").size(), 2u);
	Index.Reset(0);
	EXPECT_FALSE(Index.Busy());
	EXPECT_TRUE(Index.Find("test").empty());
}

TEST(QmMapUpload, SearchIndexMatchesChinesePartialNames)
{
	QmMapUpload::SMapFile File;
	str_copy(File.m_aFilename, "测试地图ABC.map");
	str_copy(File.m_aPath, "maps/other/测试地图ABC.map");
	EXPECT_TRUE(QmMapUpload::MatchesMapName(File, "地图ab"));
	EXPECT_TRUE(QmMapUpload::MatchesMapName(File, "测试"));
	EXPECT_TRUE(QmMapUpload::MatchesMapName(File, ""));
	EXPECT_FALSE(QmMapUpload::MatchesMapName(File, "other"));
	File.m_IsDirectory = true;
	EXPECT_FALSE(QmMapUpload::MatchesMapName(File, "测试"));
}
