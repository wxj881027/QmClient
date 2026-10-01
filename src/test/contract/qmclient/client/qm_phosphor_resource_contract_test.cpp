// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <fstream>
#include <set>
#include <string>

namespace
{
	std::string ReadBinaryFile(const char *pPath)
	{
		std::ifstream File(std::string(DDNET_TEST_SOURCE_DIR) + "/" + pPath, std::ios::binary);
		if(!File)
			return {};
		std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		return Content;
	}
}

// 图标源已从「精选 SVG 子集」改为「随包 Phosphor TTF 全量 + 官方码点映射」。
// 本合同钉住资源侧的三个不变量：TTF 随包、码点映射随包、运行时图标名全部可解析。
TEST(QmPhosphorResourceContract, WeightFontsRemainBundled)
{
	const std::array<const char *, 5> apFonts = {
		"data/qmclient/fonts/Phosphor/Phosphor-Regular.ttf",
		"data/qmclient/fonts/Phosphor/Phosphor-Light.ttf",
		"data/qmclient/fonts/Phosphor/Phosphor-Bold.ttf",
		"data/qmclient/fonts/Phosphor/Phosphor-Fill.ttf",
		"data/qmclient/fonts/Phosphor/Phosphor-Duotone.ttf",
	};
	for(const char *pPath : apFonts)
	{
		const std::string Font = ReadBinaryFile(pPath);
		ASSERT_FALSE(Font.empty()) << pPath;
		// sfnt 魔数 0x00010000：必须是真实 TTF 而不是占位文件
		ASSERT_GE(Font.size(), 4u) << pPath;
		EXPECT_EQ(Font[0], '\x00') << pPath;
		EXPECT_EQ(Font[1], '\x01') << pPath;
		EXPECT_EQ(Font[2], '\x00') << pPath;
		EXPECT_EQ(Font[3], '\x00') << pPath;
		EXPECT_GE(Font.size(), 100000u) << "font looks truncated: " << pPath;
	}
}

TEST(QmPhosphorResourceContract, CodepointsCoverEveryRuntimeIconName)
{
	const std::string Codepoints = ReadTestSourceFile("datasrc/qm_icons/phosphor.codepoints");
	ASSERT_FALSE(Codepoints.empty());

	std::set<std::string> Names;
	for(size_t Start = 0; Start < Codepoints.size();)
	{
		size_t End = Codepoints.find('\n', Start);
		if(End == std::string::npos)
			End = Codepoints.size();
		const std::string Line = Codepoints.substr(Start, End - Start);
		Start = End + 1;
		if(Line.empty() || Line[0] == '#')
			continue;
		const size_t Space = Line.find(' ');
		ASSERT_NE(Space, std::string::npos) << Line;
		Names.insert(Line.substr(0, Space));
	}
	EXPECT_GE(Names.size(), 1400u) << "official Phosphor mapping looks truncated";

	// 运行时每个枚举图标的图集名都必须能在官方映射中找到码点，
	// 否则全量图集烘焙后该图标缺失，整个 MSDF 图集会被运行时拒载。
	for(int IconIndex = 0; IconIndex < static_cast<int>(EQmIcon::COUNT); ++IconIndex)
	{
		const char *pIconName = CQmIconManager::IconName(static_cast<EQmIcon>(IconIndex));
		ASSERT_NE(pIconName[0], '\0');
		EXPECT_NE(Names.find(pIconName), Names.end()) << pIconName;
	}
}
