#include <game/editor/file_browser.h>

#include <gtest/gtest.h>

#include <string>

TEST(EditorFileBrowserPath, ExistingEntryKeepsBothExtensions)
{
	char aPath[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(CFileBrowser::FormatEntryPath(aPath, "maps/project", "draft.map.map"));
	EXPECT_STREQ(aPath, "maps/project/draft.map.map");
}

TEST(EditorFileBrowserPath, KeepsFullEntryAtBufferBoundary)
{
	const std::string Folder(IO_MAX_PATH_LENGTH - sizeof("/map.map"), 'a');
	char aPath[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(CFileBrowser::FormatEntryPath(aPath, Folder.c_str(), "map.map"));
	EXPECT_EQ(std::string(aPath), Folder + "/map.map");
	EXPECT_EQ(std::string(aPath).size(), IO_MAX_PATH_LENGTH - 1);
}

TEST(EditorFileBrowserPath, RejectsOverlongEntryWithoutProducingTruncatedTarget)
{
	const std::string Folder(IO_MAX_PATH_LENGTH - 1, 'a');
	char aPath[IO_MAX_PATH_LENGTH] = "";
	EXPECT_FALSE(CFileBrowser::FormatEntryPath(aPath, Folder.c_str(), "map.map"));
	EXPECT_STREQ(aPath, "");
}
