#include <engine/client/qm_storage_mode.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

TEST(QmStoragePath, BuildsCandidatesRelativeToExecutable)
{
	char aPath[IO_MAX_PATH_LENGTH];
	EXPECT_TRUE(StoragePathFromExecutable("C:\\QmClient\\DDNet.exe", "data/mapres", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "C:\\QmClient/data/mapres");
	EXPECT_TRUE(StoragePathFromExecutable("/opt/qmclient/DDNet", "storage.cfg", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "/opt/qmclient/storage.cfg");
	EXPECT_FALSE(StoragePathFromExecutable("DDNet.exe", "data", aPath, sizeof(aPath)));
}

TEST(QmStoragePath, RejectsTruncatedExecutableRelativeProfile)
{
	char aPath[8];
	EXPECT_FALSE(StoragePathFromExecutable("E:/Games/QmClient/DDNet.exe", "profile", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "");
	EXPECT_FALSE(StoragePathFromExecutable(nullptr, "profile", aPath, sizeof(aPath)));
}

TEST(QmStoragePath, BuildModeSelectsExclusivePortableOrUserStorage)
{
	EXPECT_EQ(QmClientStorageModeForBuild(true), IStorage::EInitializationType::CLIENT_PORTABLE);
	EXPECT_EQ(QmClientStorageModeForBuild(false), IStorage::EInitializationType::CLIENT);
	EXPECT_EQ(QmClientStorageModeForBuild(false, true), IStorage::EInitializationType::CLIENT_TEST);
}

TEST(QmStoragePath, BundledResourcePathIgnoresUserShadowAndRejectsEscape)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto Root = std::filesystem::absolute(Info.StoragePath());
	std::filesystem::create_directories(Root / "data/mapres");
	std::filesystem::create_directories(Root / "data/fonts");
	std::filesystem::create_directories(Root / "fonts");
	std::ofstream(Root / "data/fonts/index.json") << "bundled";
	std::ofstream(Root / "fonts/index.json") << "stale-user";
	const std::string Executable = (Root / "client.exe").string();
	const char *apArgs[] = {Executable.c_str()};
	pStorage = CreateTempStorage(Info.StoragePath(), 1, apArgs);
	ASSERT_NE(pStorage, nullptr);
	char aPath[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(pStorage->GetDataPath("fonts/index.json", aPath, sizeof(aPath)));
	char *pText = pStorage->ReadFileStr(aPath, IStorage::TYPE_ABSOLUTE);
	ASSERT_NE(pText, nullptr);
	EXPECT_STREQ(pText, "bundled");
	free(pText);
	for(const char *pInvalid : {"../fonts/index.json", "..\\fonts/index.json", "..", "/fonts/index.json"})
	{
		SCOPED_TRACE(pInvalid);
		EXPECT_FALSE(pStorage->GetDataPath(pInvalid, aPath, sizeof(aPath)));
		EXPECT_STREQ(aPath, "");
	}
	char aSmall[4];
	EXPECT_FALSE(pStorage->GetDataPath("fonts/index.json", aSmall, sizeof(aSmall)));
	EXPECT_STREQ(aSmall, "");
	EXPECT_FALSE(pStorage->GetDataPath(nullptr, aPath, sizeof(aPath)));
}
