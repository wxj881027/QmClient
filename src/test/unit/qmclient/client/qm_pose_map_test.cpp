#include <game/client/components/qmclient/pose_map.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
	// 与真实索引同构的最小地图集合：maps/ 与 downloadedmaps/ 各有条目，
	// 其中一张带中文和空格，用来覆盖手工输入的最难输入形态。
	std::vector<std::string> KnownMaps()
	{
		return {
			"downloadedmaps/Other Map_1a2b3c4d.map",
			"maps/Tropical Etorm.map",
			"maps/合影 地图.map",
		};
	}

	const char *const POSES_PATH = "demos/group.poses";
}

TEST(QmPoseMap, OpensImmediatelyWhenAMapIsLoaded)
{
	// 已加载地图时不应该再打扰玩家：直接给出和以前一致的命令。
	const qm_pose_map::SPlan Plan = qm_pose_map::PlanStart(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr, "Tropical Etorm");

	EXPECT_FALSE(Plan.m_NeedsSelection);
	EXPECT_EQ(Plan.m_Command, "poses_open \"demos/group.poses\" \"Tropical Etorm\"");
}

TEST(QmPoseMap, AsksForSelectionWhenNoMapIsLoaded)
{
	// 没有地图时不能硬开（更不能退回 dm1）：必须先进入选择态。
	for(const char *pCurrentMap : {static_cast<const char *>(nullptr), ""})
	{
		const qm_pose_map::SPlan Plan = qm_pose_map::PlanStart(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr, pCurrentMap);
		EXPECT_TRUE(Plan.m_NeedsSelection);
		EXPECT_TRUE(Plan.m_Command.empty());
	}
}

TEST(QmPoseMap, OpensAfterUserSelectsMapFromList)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr);
	ASSERT_TRUE(Selection.Active());

	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	// 命中的是索引里的存储相对路径，引擎据此从 maps/ 取图。
	EXPECT_EQ(Command, "poses_open \"demos/group.poses\" \"maps/Tropical Etorm.map\"");
	EXPECT_FALSE(Selection.Active());
}

TEST(QmPoseMap, CancelDoesNotOpenAndClearsRequest)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr);
	Selection.Cancel();

	EXPECT_FALSE(Selection.Active());
	EXPECT_TRUE(Selection.PosesPath().empty());

	// 取消之后再确认必须无效：不允许用已放弃的请求生成命令。
	std::string Command = "unchanged";
	EXPECT_FALSE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	EXPECT_TRUE(Command.empty());
	EXPECT_EQ(Selection.Status(), qm_pose_map::EStatus::EMPTY);
}

TEST(QmPoseMap, UnknownMapReportsErrorAndKeepsSelection)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr);

	std::string Command;
	EXPECT_FALSE(Selection.Confirm("No Such Map", KnownMaps(), Command));
	EXPECT_TRUE(Command.empty());
	EXPECT_EQ(Selection.Status(), qm_pose_map::EStatus::NOT_FOUND);
	// 报错后仍停在选择态，玩家可以改输入重试，而不是整个流程被关掉。
	EXPECT_TRUE(Selection.Active());
}

TEST(QmPoseMap, AcceptsNameWithSpacesAndChinese)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr);

	std::string Command;
	ASSERT_TRUE(Selection.Confirm("合影 地图", KnownMaps(), Command));
	EXPECT_NE(Command.find("maps/合影 地图.map"), std::string::npos);
	// 参数仍用引号包住，空格不会把一条命令拆成两个参数。
	EXPECT_NE(Command.find("\"maps/合影 地图.map\""), std::string::npos);
}

TEST(QmPoseMap, RepeatedOpenDoesNotReusePreviousSelection)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, "demos/first.poses", nullptr);
	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));

	// 第二次打开是另一份合影：上次的地图选择不能残留。
	Selection.Begin(qm_pose_map::EAction::OPEN, "demos/second.poses", nullptr);
	EXPECT_EQ(Selection.Status(), qm_pose_map::EStatus::EMPTY);
	EXPECT_FALSE(Selection.Confirm("", KnownMaps(), Command));

	ASSERT_TRUE(Selection.Confirm("Other Map_1a2b3c4d", KnownMaps(), Command));
	EXPECT_EQ(Command, "poses_open \"demos/second.poses\" \"downloadedmaps/Other Map_1a2b3c4d.map\"");
}

TEST(QmPoseMap, RenderUsesTheSameChosenMap)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "group");
	ASSERT_EQ(Selection.Action(), qm_pose_map::EAction::RENDER);

	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	// 合影默认导出成一张照片：渲染与打开共用同一张地图，只是多带输出格式。
	EXPECT_EQ(Command, "poses_render \"demos/group.poses\" \"group.png\" \"maps/Tropical Etorm.map\" 1 0");
}

TEST(QmPoseMap, RenderOutputFormatChoosesEncoderAndSuffix)
{
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "clip");
	Selection.SetOutput(qm_pose_map::EOutput::WEBP);
	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	// 扩展名同时决定编码器（截图管线按后缀走 WebP）和落盘文件名。
	EXPECT_EQ(Command, "poses_render \"demos/group.poses\" \"clip.webp\" \"maps/Tropical Etorm.map\" 1 0");
}

TEST(QmPoseMap, RenderOutputFallsBackToPngAfterReopen)
{
	// 上一次选了 WebP 不该影响下一次打开：输出格式是单次请求的状态。
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "clip");
	Selection.SetOutput(qm_pose_map::EOutput::WEBP);
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "next");
	EXPECT_EQ(Selection.Output(), qm_pose_map::EOutput::PNG);

	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	EXPECT_EQ(Command, "poses_render \"demos/group.poses\" \"next.png\" \"maps/Tropical Etorm.map\" 1 0");
}

TEST(QmPoseMap, RenderSupersamplingIsOnlyWrittenAboveOne)
{
	// 2 倍以上才把倍数写进命令：1x 与旧调用保持逐字节一致，4x 明确写在最后。
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "group");
	Selection.SetCaptureScale(2);
	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	EXPECT_EQ(Command, "poses_render \"demos/group.poses\" \"group.png\" \"maps/Tropical Etorm.map\" 2 0");

	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "group4");
	Selection.SetCaptureScale(4);
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	EXPECT_EQ(Command, "poses_render \"demos/group.poses\" \"group4.png\" \"maps/Tropical Etorm.map\" 4 0");
}

TEST(QmPoseMap, RenderSupersamplingFallsBackToOneAfterReopen)
{
	// 倍数是单次请求的状态：上一次选了 4x，下一次打开必须回到 1x（命令里不出现倍数）。
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "clip");
	Selection.SetCaptureScale(4);
	EXPECT_EQ(Selection.CaptureScale(), 4);

	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "next");
	EXPECT_EQ(Selection.CaptureScale(), 1);
	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", KnownMaps(), Command));
	EXPECT_EQ(Command, "poses_render \"demos/group.poses\" \"next.png\" \"maps/Tropical Etorm.map\" 1 0");
}

TEST(QmPoseMap, RenderSupersamplingRejectsUnsupportedMultipliers)
{
	// 只有 1/2/3/4 有意义：越界值归一成 1，避免把坏参数交给渲染路径。
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::RENDER, POSES_PATH, "clip");

	for(const int Scale : {1, 2, 3, 4})
	{
		Selection.SetCaptureScale(Scale);
		EXPECT_EQ(Selection.CaptureScale(), Scale);
	}
	for(const int Scale : {0, -1, 5, 64})
	{
		Selection.SetCaptureScale(Scale);
		EXPECT_EQ(Selection.CaptureScale(), 1);
	}

	// 取消同样清掉倍数，下一次请求不会沿用。
	Selection.SetCaptureScale(4);
	Selection.Cancel();
	EXPECT_EQ(Selection.CaptureScale(), 1);
}

TEST(QmPoseMap, TypedNameFallsBackToMapSuffix)
{
	// 玩家常直接打地图名；补上 .map 后仍要能命中同一份文件。
	std::string MapPath;
	EXPECT_EQ(qm_pose_map::Resolve("Tropical Etorm", KnownMaps(), MapPath), qm_pose_map::EStatus::OK);
	EXPECT_EQ(MapPath, "maps/Tropical Etorm.map");

	EXPECT_EQ(qm_pose_map::Resolve("Tropical Etorm.map", KnownMaps(), MapPath), qm_pose_map::EStatus::OK);
	EXPECT_EQ(MapPath, "maps/Tropical Etorm.map");

	// 首尾空格来自复制路径，不该影响解析。
	EXPECT_EQ(qm_pose_map::Resolve("  maps/Tropical Etorm.map  ", KnownMaps(), MapPath), qm_pose_map::EStatus::OK);
	EXPECT_EQ(MapPath, "maps/Tropical Etorm.map");
}

TEST(QmPoseMap, AbsolutePathIsUsedAsIs)
{
	std::string MapPath;
	EXPECT_EQ(qm_pose_map::Resolve("C:/maps/Custom Map.map", KnownMaps(), MapPath), qm_pose_map::EStatus::OK);
	// 绝对路径不进索引，原样交给引擎：能否加载由 LoadMap 报错。
	EXPECT_EQ(MapPath, "C:/maps/Custom Map.map");
}

TEST(QmPoseMap, RejectsParentDirectorySegments)
{
	std::string MapPath;
	EXPECT_EQ(qm_pose_map::Resolve("../secret.map", KnownMaps(), MapPath), qm_pose_map::EStatus::INVALID);
	EXPECT_EQ(qm_pose_map::Resolve("maps/../../secret.map", KnownMaps(), MapPath), qm_pose_map::EStatus::INVALID);
	EXPECT_TRUE(MapPath.empty());

	// 名字里恰好带两个点的合法地图不能被误判成越级路径。
	EXPECT_EQ(qm_pose_map::Resolve("maps/a..b.map", KnownMaps(), MapPath), qm_pose_map::EStatus::NOT_FOUND);
}

TEST(QmPoseMap, SelectionOverridesAnAlreadyDifferentLoadedMap)
{
	// 已加载地图也可能是错的那张：选择态里明确选中的地图必须直接进入命令。
	const std::vector<std::string> vMaps = {"maps/Already Loaded.map", "maps/Tropical Etorm.map"};
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr);

	std::string Command;
	ASSERT_TRUE(Selection.Confirm("Tropical Etorm", vMaps, Command));
	EXPECT_EQ(Command, "poses_open \"demos/group.poses\" \"maps/Tropical Etorm.map\"");
}

TEST(QmPoseMap, EmptyMapListReportsNoMatch)
{
	// maps/ 与 downloadedmaps/ 都没有地图时只能报未找到，不能编出一个路径。
	qm_pose_map::CSelection Selection;
	Selection.Begin(qm_pose_map::EAction::OPEN, POSES_PATH, nullptr);

	std::string Command;
	const std::vector<std::string> vEmpty;
	EXPECT_FALSE(Selection.Confirm("Tropical Etorm", vEmpty, Command));
	EXPECT_EQ(Selection.Status(), qm_pose_map::EStatus::NOT_FOUND);
	EXPECT_TRUE(Command.empty());
}

TEST(QmPoseMap, EmptyInputReportsEmptyStatus)
{
	std::string MapPath;
	EXPECT_EQ(qm_pose_map::Resolve("", KnownMaps(), MapPath), qm_pose_map::EStatus::EMPTY);
	EXPECT_EQ(qm_pose_map::Resolve("   ", KnownMaps(), MapPath), qm_pose_map::EStatus::EMPTY);
	EXPECT_EQ(qm_pose_map::Resolve(nullptr, KnownMaps(), MapPath), qm_pose_map::EStatus::EMPTY);
}

TEST(QmPoseMap, DisplayNameHidesDownloadedHashSuffix)
{
	// downloadedmaps 落盘名形如「名字_sha1.map」：列表只显示名字。
	EXPECT_EQ(qm_pose_map::DisplayName("Absurd 5_1a3267b62c9d62fa0cf26c488a2ed4de0cc614deb4a4bbdbd.map"), std::string("Absurd 5"));
	EXPECT_EQ(qm_pose_map::DisplayName("Tropical Etorm.map"), std::string("Tropical Etorm"));
	EXPECT_EQ(qm_pose_map::DisplayName("合影 地图.map"), std::string("合影 地图"));
	// 下划线后不是指纹就不动它，避免把正常名字截断。
	EXPECT_EQ(qm_pose_map::DisplayName("a_b.map"), std::string("a_b"));
	EXPECT_EQ(qm_pose_map::DisplayName("名称_v2.map"), std::string("名称_v2"));
	// 目录名原样保留（调用方只在文件上使用该函数）。
	EXPECT_EQ(qm_pose_map::DisplayName("maps"), std::string("maps"));
}

TEST(QmPoseMap, BrowseParentReturnsToTheRootList)
{
	// 根列表就是 maps/ 与 downloadedmaps/ 两个入口，它们的上一层还是根列表。
	EXPECT_EQ(qm_pose_map::BrowseParent(""), std::string());
	EXPECT_EQ(qm_pose_map::BrowseParent("maps"), std::string());
	EXPECT_EQ(qm_pose_map::BrowseParent("downloadedmaps"), std::string());
	// 子目录逐级回退：先回到所属根目录，再回到根列表。
	EXPECT_EQ(qm_pose_map::BrowseParent("maps/sub"), std::string("maps"));
	EXPECT_EQ(qm_pose_map::BrowseParent("maps/sub/deeper"), std::string("maps/sub"));
}

TEST(QmPoseMap, BrowseChildJoinsFolderAndEntry)
{
	EXPECT_EQ(qm_pose_map::BrowseChild("", "maps"), std::string("maps"));
	EXPECT_EQ(qm_pose_map::BrowseChild("maps", "sub"), std::string("maps/sub"));
	EXPECT_EQ(qm_pose_map::BrowseChild("maps", "Tropical Etorm.map"), std::string("maps/Tropical Etorm.map"));
	// 点进子目录再点「..」必须回到上一级，而不是卡在原地或跳到根。
	EXPECT_EQ(qm_pose_map::BrowseParent(qm_pose_map::BrowseChild("maps", "sub").c_str()), std::string("maps"));
}
