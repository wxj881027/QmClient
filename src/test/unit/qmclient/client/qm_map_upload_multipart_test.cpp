#include <game/client/components/qmclient/qm_map_upload.h>

#include <gtest/gtest.h>

TEST(QmMapUpload, AcceptsMapExtensionAndServerCompatibleNames)
{
	for(const char *pFilename : {"test.map", "Test.MAP", "测试 地图-1_2.map"})
	{
		EXPECT_TRUE(QmMapUpload::IsMapFilename(pFilename)) << pFilename;
		EXPECT_TRUE(QmMapUpload::ValidateFilename(pFilename)) << pFilename;
	}
	for(const char *pFilename : {"", "map", "test.map.bak", "test.txt", "test.map "})
		EXPECT_FALSE(QmMapUpload::IsMapFilename(pFilename)) << pFilename;
}

TEST(QmMapUpload, RejectsPathsEmptyStemsMultipleDotsAndForbiddenCharacters)
{
	for(const char *pFilename : {".map", "a.b.map", "../test.map", "maps/test.map", "maps\\test.map", "test .map", "test.map "})
		EXPECT_FALSE(QmMapUpload::ValidateFilename(pFilename)) << pFilename;
	for(const char Character : std::string("!@#$%^&*()+|\\/[]{};:'\",<>=\t\r\n\x01"))
	{
		const std::string Filename = std::string("test") + Character + ".map";
		EXPECT_FALSE(QmMapUpload::ValidateFilename(Filename.c_str())) << Filename;
	}
}

TEST(QmMapUpload, MultipartPreservesBinaryMapAndUtf8PlayerName)
{
	const unsigned char aMap[] = {'D', 'A', 'T', 'A', 0, '\r', '\n', 0xff};
	const char *pBoundary = "QmUploadBoundary123";
	std::string Body;
	ASSERT_TRUE(QmMapUpload::BuildMultipart("测试地图.map", "玩家 甲", aMap, sizeof(aMap), pBoundary, Body));
	EXPECT_EQ(Body.find("--QmUploadBoundary123\r\n"), size_t(0));
	EXPECT_NE(Body.find("Content-Disposition: form-data; name=\"file\"; filename=\"测试地图.map\"\r\n"), std::string::npos);
	EXPECT_NE(Body.find(std::string(reinterpret_cast<const char *>(aMap), sizeof(aMap))), std::string::npos);
	EXPECT_NE(Body.find("Content-Disposition: form-data; name=\"player_id\"\r\n\r\n玩家 甲\r\n"), std::string::npos);
	const std::string Closing = "--QmUploadBoundary123--\r\n";
	ASSERT_GE(Body.size(), Closing.size());
	EXPECT_EQ(Body.compare(Body.size() - Closing.size(), Closing.size(), Closing), 0);
}

TEST(QmMapUpload, MultipartRejectsMissingFieldsAndOversizedFilesBeforeReadingData)
{
	const unsigned char aMap[] = {'D'};
	std::string Body;
	EXPECT_FALSE(QmMapUpload::BuildMultipart("../test.map", "Player", aMap, sizeof(aMap), "Boundary", Body));
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "", aMap, sizeof(aMap), "Boundary", Body));
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "Player", aMap, 0, "Boundary", Body));
	// 大小检查必须先于读取文件内容，避免为超限文件分配请求正文。
	EXPECT_EQ(QmMapUpload::MAX_MAP_SIZE, size_t(64) * 1024 * 1024);
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "Player", aMap, QmMapUpload::MAX_MAP_SIZE + 1, "Boundary", Body));
}

TEST(QmMapUpload, MultipartRejectsInvalidOrConflictingBoundaries)
{
	const unsigned char aMap[] = {'D'};
	std::string Body;
	for(const char *pBoundary : {"", "bad\r\nboundary", "bad\"boundary"})
		EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "Player", aMap, sizeof(aMap), pBoundary, Body));
	const unsigned char aCollision[] = {'\r', '\n', '-', '-', 'b', 'o', 'u', 'n', 'd', 'a', 'r', 'y', '\r', '\n'};
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "Player", aCollision, sizeof(aCollision), "boundary", Body));
	EXPECT_FALSE(QmMapUpload::BuildMultipart("test.map", "Player\r\n--boundary", aMap, sizeof(aMap), "boundary", Body));
}
