#include <engine/client/qm_font_file_probe.h>
#include <engine/client/qm_font_restore.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>

TEST(QmSystemFontRestore, FindsFamilyStyleAndPostscriptNamesBeforeReadingFullFiles)
{
	CQmFontFileProbe Probe;
	const std::string Path = TestSourcePath("data/fonts/DejaVuSans.ttf");
	EXPECT_TRUE(Probe.Matches(Path.c_str(), {"DejaVu Sans"}));
	EXPECT_TRUE(Probe.Matches(Path.c_str(), {"DejaVu Sans Book"}));
	EXPECT_TRUE(Probe.Matches(Path.c_str(), {"DejaVuSans"}));
	EXPECT_FALSE(Probe.Matches(Path.c_str(), {"unavailable family"}));
}

TEST(QmSystemFontRestore, CollectionLookupPreservesChineseLegacySelection)
{
	CQmFontFileProbe Probe;
	const std::string Path = TestSourcePath("data/fonts/SourceHanSans.ttc");
	EXPECT_TRUE(Probe.Matches(Path.c_str(), {"Source Han Sans SC"}));
	EXPECT_TRUE(Probe.Matches(Path.c_str(), {"Source Han Sans SC Regular"}));
	EXPECT_FALSE(Probe.Matches(Path.c_str(), {"DejaVu Sans"}));
}

TEST(QmSystemFontRestore, UnrelatedAndInvalidFontsNeverReachFullReadOrHash)
{
	const std::string Selected = TestSourcePath("data/fonts/DejaVuSans.ttf");
	const std::vector<std::string> vPaths{
		TestSourcePath("data/fonts/Cabin-Regular.ttf"),
		TestSourcePath("data/fonts/index.json"),
		TestSourcePath("data/fonts/missing-restore-test.ttf"),
		Selected};
	std::vector<std::string> vRead;
	CQmFontRestore Restore;
	const auto pJob = Restore.Request({"DejaVu Sans", "", ""}, [](const char *) { return false; }, [&](const auto &vNames) { return QmFindSelectedFontFiles(vPaths, vNames); }, [&](const char *pPath, void **ppData, unsigned *pSize) {
			vRead.emplace_back(pPath);
			return CQmFontScanJob::ReadAbsolute(pPath, ppData, pSize); });
	ASSERT_NE(pJob, nullptr);
	EXPECT_TRUE(vRead.empty());
	CJobPool Pool;
	Pool.Init(1);
	Pool.Add(pJob);
	Pool.Shutdown();
	EXPECT_EQ(vRead, (std::vector<std::string>{Selected}));
	CQmFontScanJob::SFile File;
	ASSERT_TRUE(Restore.TakeNext(File));
	EXPECT_EQ(File.m_Path, Selected);
	EXPECT_GT(File.m_Size, 0u);
	EXPECT_FALSE(File.m_Hash.empty());
	EXPECT_FALSE(Restore.TakeNext(File));
}

TEST(QmSystemFontRestore, ProbeSupportsUnicodeInstallationPaths)
{
	CTestInfo Info;
	const std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto Path = std::filesystem::absolute(Info.StoragePath()) / std::filesystem::u8path("测试字体.ttf");
	std::filesystem::copy_file(std::filesystem::u8path(TestSourcePath("data/fonts/DejaVuSans.ttf")), Path);
	CQmFontFileProbe Probe;
	const auto Utf8Path = Path.u8string();
	const std::string PathString(Utf8Path.begin(), Utf8Path.end());
	EXPECT_TRUE(Probe.Matches(PathString.c_str(), {"DejaVu Sans"}));
}
