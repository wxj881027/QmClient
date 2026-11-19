#include <game/editor/mapitems/map.h>

#include <gtest/gtest.h>

#include <string>

TEST(EditorMapAppendImageName, ShortNameKeepsFullBaseAndSuffix)
{
	char aName[IO_MAX_PATH_LENGTH];
	CEditorMap::FormatAppendedImageName(aName, "grass (old)", 1);
	EXPECT_STREQ(aName, "grass (old) (1)");
}

TEST(EditorMapAppendImageName, FullNamesLeaveRoomForSuccessiveSuffixes)
{
	const std::string Original(IO_MAX_PATH_LENGTH - 1, 'a');
	char aFirst[IO_MAX_PATH_LENGTH];
	char aNext[IO_MAX_PATH_LENGTH];
	CEditorMap::FormatAppendedImageName(aFirst, Original.c_str(), 9);
	CEditorMap::FormatAppendedImageName(aNext, Original.c_str(), 10);
	EXPECT_EQ(std::string(aFirst), Original.substr(0, IO_MAX_PATH_LENGTH - 5) + " (9)");
	EXPECT_EQ(std::string(aNext), Original.substr(0, IO_MAX_PATH_LENGTH - 6) + " (10)");
}

TEST(EditorMapAppendImageName, SuffixNeverFollowsAPartialUtf8Character)
{
	const std::string Prefix(IO_MAX_PATH_LENGTH - 6, 'a');
	const std::string Original = Prefix + "\xE7\x93\xA6";
	char aName[IO_MAX_PATH_LENGTH];
	CEditorMap::FormatAppendedImageName(aName, Original.c_str(), 1);
	EXPECT_EQ(std::string(aName), Prefix + " (1)");
}
