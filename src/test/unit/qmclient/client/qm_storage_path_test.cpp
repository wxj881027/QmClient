#include <engine/client/qm_storage_mode.h>
#include <engine/storage.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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
