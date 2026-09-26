#include <engine/client/font_size_cache.h>

#include <gtest/gtest.h>

TEST(QmFontSizeCache, SkipsRepeatedSetForSameFaceAndSize)
{
	CQmFontSizeCache Cache;
	int Face = 0;
	int Calls = 0;

	for(int i = 0; i < 5; ++i)
		Cache.Ensure(&Face, 12, [&]() { ++Calls; return 0; });

	// 同一 face 同一字号只需要真正设置一次。
	EXPECT_EQ(Calls, 1);
}

TEST(QmFontSizeCache, SetsAgainWhenFaceOrSizeChanges)
{
	CQmFontSizeCache Cache;
	int FaceA = 0;
	int FaceB = 0;
	int Calls = 0;
	const auto SetSize = [&]() { ++Calls; return 0; };

	Cache.Ensure(&FaceA, 12, SetSize);
	Cache.Ensure(&FaceA, 12, SetSize);
	EXPECT_EQ(Calls, 1);

	Cache.Ensure(&FaceA, 24, SetSize);
	EXPECT_EQ(Calls, 2);

	Cache.Ensure(&FaceB, 24, SetSize);
	EXPECT_EQ(Calls, 3);

	// 切回旧组合仍要重新设置：只记住最近一次成功项。
	Cache.Ensure(&FaceA, 12, SetSize);
	EXPECT_EQ(Calls, 4);
}

TEST(QmFontSizeCache, FailedSetIsNotRemembered)
{
	CQmFontSizeCache Cache;
	int Face = 0;
	int Calls = 0;

	// 失败可能已经改变底层状态，因此不能记为命中，下次必须重试。
	Cache.Ensure(&Face, 12, [&]() { ++Calls; return 1; });
	Cache.Ensure(&Face, 12, [&]() { ++Calls; return 1; });
	EXPECT_EQ(Calls, 2);

	// 失败后转为成功：成功那次开始才被记住。
	Cache.Ensure(&Face, 12, [&]() { ++Calls; return 0; });
	Cache.Ensure(&Face, 12, [&]() { ++Calls; return 0; });
	EXPECT_EQ(Calls, 3);
}

TEST(QmFontSizeCache, NullFaceIsSkippedAndResetForcesNextSet)
{
	CQmFontSizeCache Cache;
	int Calls = 0;
	const auto SetSize = [&]() { ++Calls; return 0; };

	// 空 face 没有可设置的对象：直接跳过，不调用设置函数，也不占用缓存键。
	Cache.Ensure(nullptr, 12, SetSize);
	Cache.Ensure(nullptr, 12, SetSize);
	EXPECT_EQ(Calls, 0);

	int Face = 0;
	Cache.Ensure(&Face, 12, SetSize);
	EXPECT_EQ(Calls, 1);
	Cache.Ensure(&Face, 12, SetSize);
	EXPECT_EQ(Calls, 1);

	// Reset 用于字形表清空或字体链变化后强制下一次重新设置。
	Cache.Reset();
	Cache.Ensure(&Face, 12, SetSize);
	EXPECT_EQ(Calls, 2);
}
