#include <game/client/components/qmclient/media_paths.h>

#include <gtest/gtest.h>

namespace
{
#if defined(CONF_FAMILY_WINDOWS)
	constexpr const char *EXPORT_ROOT = "C:/captures";
#else
	constexpr const char *EXPORT_ROOT = "/captures";
#endif
}

TEST(QmMediaPaths, DefaultSettingsAreEmpty)
{
	EXPECT_STREQ(DefaultConfig::QmDemoDirectory, "");
	EXPECT_STREQ(DefaultConfig::QmVideoDirectory, "");
	EXPECT_STREQ(DefaultConfig::QmScreenshotDirectory, "");
}

TEST(QmMediaPaths, EmptySettingsKeepAllDefaultSubdirectories)
{
	CConfig Config = {};
	for(const char *pPath : {"demos/manual.demo", "demos/auto/run.demo", "demos/auto/race/run.demo", "demos/auto/server/run.demo", "demos/highlight/run.demo", "demos/replays/run.demo", "videos/run.mp4", "screenshots/manual.png", "screenshots/auto/stats/run.png"})
	{
		SCOPED_TRACE(pPath);
		EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, pPath), pPath);
	}
}

TEST(QmMediaPaths, CustomDemoRootKeepsEveryNestedSuffix)
{
	CConfig Config = {};
	str_copy(Config.m_QmDemoDirectory, EXPORT_ROOT);
	for(const char *pSuffix : {"manual.demo", "auto/run.demo", "auto/race/run.demo", "auto/server/run.demo", "highlight/run.demo", "replays/replay_tmp.demo", ".qm_rank_watchable.jsonl"})
	{
		SCOPED_TRACE(pSuffix);
		EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, (std::string("demos/") + pSuffix).c_str()), std::string(EXPORT_ROOT) + '/' + pSuffix);
	}
}

TEST(QmMediaPaths, ThreeDirectoriesAreIndependent)
{
	CConfig Config = {};
	str_copy(Config.m_QmDemoDirectory, (std::string(EXPORT_ROOT) + "/demo").c_str());
	str_copy(Config.m_QmVideoDirectory, (std::string(EXPORT_ROOT) + "/video").c_str());
	str_copy(Config.m_QmScreenshotDirectory, (std::string(EXPORT_ROOT) + "/图片").c_str());
	EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, "demos/auto/run.demo"), std::string(EXPORT_ROOT) + "/demo/auto/run.demo");
	EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, "videos/run.mp4"), std::string(EXPORT_ROOT) + "/video/run.mp4");
	EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, "screenshots/auto/stats/run.png"), std::string(EXPORT_ROOT) + "/图片/auto/stats/run.png");
}

TEST(QmMediaPaths, ClearingSettingRestoresDefaultForNextFile)
{
	CConfig Config = {};
	str_copy(Config.m_QmDemoDirectory, EXPORT_ROOT);
	const std::string PendingPath = qmclient::media_paths::Resolve(nullptr, Config, "demos/run.demo");
	Config.m_QmDemoDirectory[0] = '\0';
	EXPECT_EQ(PendingPath, std::string(EXPORT_ROOT) + "/run.demo");
	EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, "demos/next.demo"), "demos/next.demo");
}

TEST(QmMediaPaths, SimilarPrefixesAndOtherFilesKeepTheirOriginalPaths)
{
	CConfig Config = {};
	str_copy(Config.m_QmDemoDirectory, EXPORT_ROOT);
	for(const char *pPath : {"demos-old/run.demo", "qmclient/rank1/demos/run.demo", "record/csv/run.csv", "settings.cfg"})
		EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, pPath), pPath);
}

TEST(QmMediaPaths, TrailingSeparatorDoesNotAddAnotherSubdirectory)
{
	CConfig Config = {};
	str_copy(Config.m_QmVideoDirectory, (std::string(EXPORT_ROOT) + "///").c_str());
	EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, "videos/run.mp4"), std::string(EXPORT_ROOT) + "/run.mp4");
}

TEST(QmMediaPaths, ParentTraversalInFilenameIsRejected)
{
	CConfig Config = {};
	str_copy(Config.m_QmDemoDirectory, EXPORT_ROOT);
	for(const char *pPath : {"demos/../outside.demo", "demos/auto/../../outside.demo", "demos\\..\\outside.demo", "demos//outside.demo"})
		EXPECT_TRUE(qmclient::media_paths::Resolve(nullptr, Config, pPath).empty()) << pPath;
}

TEST(QmMediaPaths, OverlongDestinationIsRejectedWithoutTruncation)
{
	CConfig Config = {};
	const std::string Root = std::string(EXPORT_ROOT) + '/' + std::string(IO_MAX_PATH_LENGTH - std::string(EXPORT_ROOT).size() - 2, 'a');
	str_copy(Config.m_QmDemoDirectory, Root.c_str());
	EXPECT_TRUE(qmclient::media_paths::Resolve(nullptr, Config, "demos/file.demo").empty());
	EXPECT_STREQ(Config.m_QmDemoDirectory, Root.c_str());
}

#if defined(CONF_FAMILY_WINDOWS)
TEST(QmMediaPaths, WindowsBackslashesAndSpacesAreAccepted)
{
	CConfig Config = {};
	str_copy(Config.m_QmScreenshotDirectory, "D:\\我的截图\\Game captures\\");
	EXPECT_EQ(qmclient::media_paths::Resolve(nullptr, Config, "screenshots/auto/stats/run.png"), "D:/我的截图/Game captures/auto/stats/run.png");
}

TEST(QmMediaPaths, DriveRelativeDirectoryIsRejected)
{
	CConfig Config = {};
	str_copy(Config.m_QmDemoDirectory, "D:captures");
	EXPECT_TRUE(qmclient::media_paths::Resolve(nullptr, Config, "demos/run.demo").empty());
	EXPECT_STREQ(Config.m_QmDemoDirectory, "D:captures");
}

TEST(QmMediaPaths, RenameTargetKeepsSupportedExtension)
{
	using qmclient::media_paths::RenameTargetName;
	// 新名字以 "demo" 结尾时不能被判成「已有扩展名」，否则产物没有 .demo，
	// 会被演示浏览器的扩展名过滤挡掉（重命名后从列表消失）。
	EXPECT_EQ(RenameTargetName("q出场demo", "Lost Story 2_2026-10-08_demo.demo", false), "q出场demo.demo");
	// 补扩展名时点号不能丢。
	EXPECT_EQ(RenameTargetName("abc", "Foo.demo", false), "abc.demo");
	EXPECT_EQ(RenameTargetName("abc.DEMO", "Foo.demo", false), "abc.DEMO");
	EXPECT_EQ(RenameTargetName("abc.demo.demo", "Foo.demo", false), "abc.demo.demo");
	// 已经被旧逻辑改成无扩展名的文件，再改名时能补回来。
	EXPECT_EQ(RenameTargetName("q出场demo", "Lost Story 2_2026-10-08_demo", false), "q出场demo.demo");
	EXPECT_EQ(RenameTargetName("abc", "run.poses", false), "abc.poses");
	EXPECT_EQ(RenameTargetName("abc", "shot.png", true), "abc.png");
	EXPECT_EQ(RenameTargetName("abc", "no_ext", true), "abc.png");
	EXPECT_EQ(RenameTargetName("", "Foo.demo", false), "");
}
#endif
