#include <game/client/qm_icon_prewarm.h>

#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <utility>

TEST(QmIconPrewarm, ProducesRegisteredGlyphsAtActualPixelSizesAndFinishes)
{
	CQmIconPrewarmPlan Plan;
	ASSERT_TRUE(Plan.Configure(2.0f, 1));
	std::set<std::pair<int, int>> Jobs;
	int Codepoint, Size;
	while(Plan.Next(Codepoint, Size))
	{
		EXPECT_GT(Codepoint, 0);
		EXPECT_GE(Size, 6);
		EXPECT_LE(Size, 128);
		Jobs.emplace(Codepoint, Size);
	}
	EXPECT_TRUE(Plan.Complete());
	EXPECT_NE(Jobs.find({CQmIconRegistry::Codepoint(EQmIcon::TRASH), 48}), Jobs.end());
	EXPECT_FALSE(Plan.Configure(2.0f, 1));
	EXPECT_FALSE(Plan.Next(Codepoint, Size));
}

TEST(QmIconPrewarm, PartialWorkResumesAndWeightResizeOrDeviceChangesRestart)
{
	CQmIconPrewarmPlan Plan;
	ASSERT_TRUE(Plan.Configure(1.0f, 1));
	int FirstCodepoint, FirstSize, Codepoint, Size;
	ASSERT_TRUE(Plan.Next(FirstCodepoint, FirstSize));
	EXPECT_FALSE(Plan.Configure(1.0f, 1));
	ASSERT_TRUE(Plan.Next(Codepoint, Size));
	EXPECT_NE(Codepoint, FirstCodepoint);
	ASSERT_TRUE(Plan.Configure(1.0f, 3));
	ASSERT_TRUE(Plan.Next(Codepoint, Size));
	EXPECT_EQ(Codepoint, FirstCodepoint);
	ASSERT_TRUE(Plan.Configure(2.0f, 3));
	ASSERT_TRUE(Plan.Next(Codepoint, Size));
	EXPECT_EQ(Size, 6);
	// 极小字号在两种比例均会落到缓存最小像素档。
	Plan.Invalidate();
	EXPECT_TRUE(Plan.Configure(2.0f, 3));
	EXPECT_TRUE(Plan.Next(Codepoint, Size));
	EXPECT_EQ(Codepoint, FirstCodepoint);
}

TEST(QmIconPrewarm, RejectsInvalidScaleAndDeduplicatesClampedSizes)
{
	CQmIconPrewarmPlan Plan;
	EXPECT_FALSE(Plan.Configure(0, 1));
	EXPECT_FALSE(Plan.Configure(std::numeric_limits<float>::quiet_NaN(), 1));
	EXPECT_FALSE(Plan.Configure(std::numeric_limits<float>::infinity(), 1));
	ASSERT_TRUE(Plan.Configure(1000, 1));
	int Codepoint, Size, Count = 0;
	while(Plan.Next(Codepoint, Size))
	{
		EXPECT_EQ(Size, 128);
		++Count;
	}
	EXPECT_EQ(Count, static_cast<int>(EQmIcon::COUNT));
}

TEST(QmIconPrewarm, FractionalButtonAndFormSizesUseTextEngineTruncation)
{
	CQmIconPrewarmPlan Plan;
	ASSERT_TRUE(Plan.Configure(1.5f, 1));
	std::set<int> Sizes;
	int Codepoint, Size;
	while(Plan.Next(Codepoint, Size))
		Sizes.insert(Size);
	// 14px 方框 -> 11.2 逻辑字号 -> 16.8 实际像素，文字引擎使用 16 而非 17。
	EXPECT_EQ(QmIconRasterPixelSize(QmIconFontSize({0, 0, 14, 14}), 1.5f), 16);
	EXPECT_NE(Sizes.find(16), Sizes.end());
	// 20px 输入按钮 -> 9.28 逻辑字号 -> 13.92 实际像素，使用 13。
	EXPECT_EQ(QmIconRasterPixelSize((20 * 0.58f) * 0.8f, 1.5f), 13);
	EXPECT_NE(Sizes.find(13), Sizes.end());
}
