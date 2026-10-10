// 路径策略与真实存储、录像记录器及自动清理协作；所有文件位于测试隔离目录。
#include <engine/shared/demo.h>
#include <engine/shared/filecollection.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/media_paths.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace
{
	class CQmMediaStorageTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
		CConfig m_Config = {};

		std::string Absolute(const char *pName)
		{
			const auto Path = (std::filesystem::absolute(std::filesystem::u8path(m_Info.StoragePath())) / std::filesystem::u8path(pName)).generic_u8string();
			return std::string(reinterpret_cast<const char *>(Path.c_str()), Path.size());
		}

		void Write(const std::string &Path)
		{
			ASSERT_TRUE(qmclient::media_paths::PrepareWrite(m_pStorage.get(), Path));
			IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
			ASSERT_NE(File, nullptr);
			ASSERT_EQ(io_write(File, "capture", 7), 7U);
			ASSERT_EQ(io_close(File), 0);
		}
	};

	int CollectFiles(const char *pName, int IsDir, int, void *pUser)
	{
		if(!IsDir)
			static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName);
		return 0;
	}

	int CollectFileInfo(const CFsFileInfo *pInfo, int IsDir, int Type, void *pUser)
	{
		return CollectFiles(pInfo->m_pName, IsDir, Type, pUser);
	}
}

TEST_F(CQmMediaStorageTest, NewDemoPreservesSubdirectoriesAndLeavesOldFileInPlace)
{
	Write("demos/auto/race/old.demo");
	const std::string Root = Absolute("自定义 回放");
	str_copy(m_Config.m_QmDemoDirectory, Root.c_str());
	const std::string Path = qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "demos/auto/race/new.demo");
	Write(Path);
	EXPECT_EQ(Path, Root + "/auto/race/new.demo");
	EXPECT_TRUE(m_pStorage->FileExists("demos/auto/race/old.demo", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("demos/auto/race/new.demo", IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_pStorage->FileExists(Path.c_str(), IStorage::TYPE_ABSOLUTE));
}

TEST_F(CQmMediaStorageTest, RelativeDirectoryUsesSaveRootAndCreatesNestedParents)
{
	str_copy(m_Config.m_QmScreenshotDirectory, "captures/images");
	const std::string Path = qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "screenshots/auto/stats/score.png");
	Write(Path);
	EXPECT_EQ(Path, Absolute("captures/images/auto/stats/score.png"));
	EXPECT_TRUE(m_pStorage->FileExists("captures/images/auto/stats/score.png", IStorage::TYPE_SAVE));
}

TEST_F(CQmMediaStorageTest, FileBlockingDirectoryFailsAndKeepsConfiguration)
{
	Write("blocked");
	const std::string Root = Absolute("blocked/videos");
	str_copy(m_Config.m_QmVideoDirectory, Root.c_str());
	const std::string Path = qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "videos/run.mp4");
	EXPECT_FALSE(qmclient::media_paths::PrepareWrite(m_pStorage.get(), Path));
	EXPECT_STREQ(m_Config.m_QmVideoDirectory, Root.c_str());
	EXPECT_FALSE(m_pStorage->FileExists("videos/run.mp4", IStorage::TYPE_SAVE));
	EXPECT_EQ(qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "videos/retry.mp4"), Root + "/retry.mp4");
}

TEST_F(CQmMediaStorageTest, ChangingDirectoryDoesNotRedirectAlreadyResolvedFile)
{
	str_copy(m_Config.m_QmDemoDirectory, Absolute("first").c_str());
	const std::string PendingPath = qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "demos/replays/pending.demo");
	str_copy(m_Config.m_QmDemoDirectory, Absolute("second").c_str());
	Write(PendingPath);
	const std::string NextPath = qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "demos/replays/next.demo");
	Write(NextPath);
	EXPECT_TRUE(m_pStorage->FileExists("first/replays/pending.demo", IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists("second/replays/pending.demo", IStorage::TYPE_SAVE));
	EXPECT_TRUE(m_pStorage->FileExists("second/replays/next.demo", IStorage::TYPE_SAVE));
}

TEST_F(CQmMediaStorageTest, AbsoluteDirectoryListsRenamesAndRemovesFiles)
{
	const std::string Root = Absolute("browser");
	Write(Root + "/capture.png");
	std::vector<std::string> vNames;
	m_pStorage->ListDirectory(IStorage::TYPE_ABSOLUTE, Root.c_str(), CollectFiles, &vNames);
	ASSERT_EQ(vNames, std::vector<std::string>{"capture.png"});
	std::vector<std::string> vInfoNames;
	m_pStorage->ListDirectoryInfo(IStorage::TYPE_ABSOLUTE, Root.c_str(), CollectFileInfo, &vInfoNames);
	EXPECT_EQ(vInfoNames, vNames);
	ASSERT_TRUE(m_pStorage->RenameFile((Root + "/capture.png").c_str(), (Root + "/renamed.png").c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_FALSE(m_pStorage->FileExists((Root + "/capture.png").c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_TRUE(m_pStorage->RemoveFile((Root + "/renamed.png").c_str(), IStorage::TYPE_ABSOLUTE));
}

TEST_F(CQmMediaStorageTest, AutoCleanupOnlyDeletesMatchingFilesInCustomDirectory)
{
	Write("screenshots/auto/autoscreen_2026-01-01_00-00-00.png");
	const std::string Root = Absolute("new-screenshots/auto");
	Write(Root + "/autoscreen_2026-01-01_00-00-00.png");
	Write(Root + "/autoscreen_2026-01-02_00-00-00.png");
	Write(Root + "/keep.png");
	CFileCollection Collection;
	Collection.Init(m_pStorage.get(), Root.c_str(), "autoscreen", ".png", 1);
	EXPECT_FALSE(m_pStorage->FileExists((Root + "/autoscreen_2026-01-01_00-00-00.png").c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_TRUE(m_pStorage->FileExists((Root + "/autoscreen_2026-01-02_00-00-00.png").c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_TRUE(m_pStorage->FileExists((Root + "/keep.png").c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_TRUE(m_pStorage->FileExists("screenshots/auto/autoscreen_2026-01-01_00-00-00.png", IStorage::TYPE_SAVE));
}

TEST_F(CQmMediaStorageTest, RecorderKeepsAndRemovesAbsoluteDemoFiles)
{
	str_copy(m_Config.m_QmDemoDirectory, Absolute("recordings").c_str());
	const std::string Path = qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "demos/auto/race/tmp.demo");
	ASSERT_TRUE(qmclient::media_paths::PrepareWrite(m_pStorage.get(), Path));
	CDemoRecorder Recorder(nullptr, true);
	unsigned char MapData = 0;
	const auto Start = [&]() {
		return Recorder.Start(m_pStorage.get(), nullptr, Path.c_str(), "test", "test_map", SHA256_DIGEST{}, 0, "client", 0, &MapData, nullptr, nullptr, nullptr);
	};
	ASSERT_EQ(Start(), 0);
	Recorder.RecordTickMarker(100, true);
	Recorder.RecordTickMarker(101);
	const std::string Renamed = Absolute("recordings/auto/race/finished.demo");
	ASSERT_EQ(Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE, Renamed.c_str()), 0);
	EXPECT_FALSE(m_pStorage->FileExists(Path.c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_TRUE(m_pStorage->FileExists(Renamed.c_str(), IStorage::TYPE_ABSOLUTE));
	ASSERT_EQ(Start(), 0);
	ASSERT_EQ(Recorder.Stop(IDemoRecorder::EStopMode::REMOVE_FILE), 0);
	EXPECT_FALSE(m_pStorage->FileExists(Path.c_str(), IStorage::TYPE_ABSOLUTE));
}

TEST_F(CQmMediaStorageTest, RelativeDirectoryCannotEscapeSaveRoot)
{
	str_copy(m_Config.m_QmDemoDirectory, "../outside");
	EXPECT_TRUE(qmclient::media_paths::Resolve(m_pStorage.get(), m_Config, "demos/run.demo").empty());
	EXPECT_STREQ(m_Config.m_QmDemoDirectory, "../outside");
}
