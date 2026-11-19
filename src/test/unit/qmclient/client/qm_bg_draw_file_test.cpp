#include <game/client/components/tclient/bg_draw_file.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	bool ReadLines(const std::vector<std::string> &vLines, CBgDrawItemData &Data)
	{
		size_t Index = 0;
		return BgDrawFile::Read([&](char *pBuf, int Length) {
			if(Index == vLines.size())
				return false;
			std::snprintf(pBuf, Length, "%s", vLines[Index++].c_str());
			return true;
		},
			Data);
	}
}

TEST(QmBgDrawFile, EofFinishesLastStrokeWithoutSeparator)
{
	CBgDrawItemData Data;
	ASSERT_TRUE(ReadLines({"1,2,3,1,0,0,1", "10,20,4,0,1,0,1"}, Data));
	ASSERT_EQ(Data.size(), 2u);
	EXPECT_FLOAT_EQ(Data.back().x, 10);
	EXPECT_FLOAT_EQ(Data.back().w, 4);
}

TEST(QmBgDrawFile, RejectsNonfiniteCoordinatesWidthsAndColors)
{
	CBgDrawItemData Data;
	ASSERT_TRUE(ReadLines({"nan,2", "1,inf", "1,2,inf", "1,2,3,nan", "4,5", ","}, Data));
	ASSERT_EQ(Data.size(), 1u);
	EXPECT_FLOAT_EQ(Data.front().x, 4);
	EXPECT_FLOAT_EQ(Data.front().y, 5);
	EXPECT_FLOAT_EQ(Data.front().w, 5);
}

TEST(QmBgDrawFile, EmptyFileDoesNotCreateAStroke)
{
	CBgDrawItemData Data;
	EXPECT_FALSE(ReadLines({}, Data));
	EXPECT_TRUE(Data.empty());
}

TEST(QmBgDrawFile, SavedStrokeRetainsEveryPointOnRead)
{
	const CBgDrawItemData Original{{1, 2, 3, 1, 0, 0, 1}, {10, 20, 4, 0, 1, 0, 1}};
	std::vector<std::string> vLines;
	ASSERT_TRUE(BgDrawFile::Write([&](const char *pLine) {
		vLines.emplace_back(pLine);
		return true;
	},
		Original));
	CBgDrawItemData Loaded;
	ASSERT_TRUE(ReadLines(vLines, Loaded));
	ASSERT_EQ(Loaded.size(), Original.size());
	EXPECT_FLOAT_EQ(Loaded.front().x, Original.front().x);
	EXPECT_FLOAT_EQ(Loaded.back().x, Original.back().x);
	EXPECT_FLOAT_EQ(Loaded.back().a, Original.back().a);
}

TEST(QmBgDrawFile, LongFinitePointKeepsEveryFieldWhenSavedAndRead)
{
	const CBgDrawItemData Original{{1.5e37f, -2.5e36f, 3.5e36f, 4.5e35f, -5.5e35f, 6.5e34f, 0.25f}};
	std::vector<std::string> vLines;
	ASSERT_TRUE(BgDrawFile::Write([&](const char *pLine) {
		vLines.emplace_back(pLine);
		return true;
	},
		Original));

	CBgDrawItemData Loaded;
	ASSERT_TRUE(ReadLines(vLines, Loaded));
	ASSERT_EQ(Loaded.size(), 1u);
	EXPECT_FLOAT_EQ(Loaded.front().x, Original.front().x);
	EXPECT_FLOAT_EQ(Loaded.front().y, Original.front().y);
	EXPECT_FLOAT_EQ(Loaded.front().w, Original.front().w);
	EXPECT_FLOAT_EQ(Loaded.front().r, Original.front().r);
	EXPECT_FLOAT_EQ(Loaded.front().g, Original.front().g);
	EXPECT_FLOAT_EQ(Loaded.front().b, Original.front().b);
	EXPECT_FLOAT_EQ(Loaded.front().a, Original.front().a);
}
