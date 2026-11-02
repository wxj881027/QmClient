#include <game/client/components/qmclient/chat_bubble_geometry.h>

#include <gtest/gtest.h>

namespace
{
	constexpr float Max = std::numeric_limits<float>::max();
	constexpr float Inf = std::numeric_limits<float>::infinity();
	constexpr float Nan = std::numeric_limits<float>::quiet_NaN();
}

TEST(QmChatBubbleGeometry, ProjectsWorldAnchorIntoTheInterface)
{
	const auto Anchor = QmChatBubbleProjectAnchor({-100, -50, 100, 50}, {10, 20, 410, 220}, vec2(0, -25));
	ASSERT_TRUE(Anchor);
	EXPECT_EQ(*Anchor, vec2(210, 70));
}

TEST(QmChatBubbleGeometry, RejectsZeroAndReversedWorldExtents)
{
	for(const auto &World : {std::array<float, 4>{0, 0, 0, 100}, {100, 0, 0, 100}, {0, 100, 100, 0}})
		EXPECT_FALSE(QmChatBubbleProjectAnchor(World, {0, 0, 300, 200}, vec2(0, 0)));
}

TEST(QmChatBubbleGeometry, RejectsInvalidInterfaceExtents)
{
	for(const auto &Interface : {std::array<float, 4>{0, 0, 300, 0}, {0, 0, Inf, 100}, {-Max, 0, Max, 100}})
		EXPECT_FALSE(QmChatBubbleProjectAnchor({0, 0, 100, 100}, Interface, vec2(0, 0)));
}

TEST(QmChatBubbleGeometry, RejectsNonfiniteAnchorBeforeProjection)
{
	EXPECT_FALSE(QmChatBubbleProjectAnchor({0, 0, 100, 100}, {0, 0, 300, 200}, vec2(Nan, 0)));
	EXPECT_FALSE(QmChatBubbleProjectAnchor({0, 0, 100, 100}, {0, 0, 300, 200}, vec2(0, Inf)));
}

TEST(QmChatBubbleGeometry, RejectsOverflowDuringOtherwiseFiniteProjection)
{
	EXPECT_FALSE(QmChatBubbleProjectAnchor({-Max, 0, 0, 100}, {0, 0, 300, 200}, vec2(Max, 0)));
}

TEST(QmChatBubbleGeometry, VisibleCountPreservesHalfRoundingAndClamps)
{
	EXPECT_EQ(QmChatBubbleVisibleCharacters(2.49f, 5), 2);
	EXPECT_EQ(QmChatBubbleVisibleCharacters(2.5f, 5), 3);
	EXPECT_EQ(QmChatBubbleVisibleCharacters(-0.5f, 5), 0);
	EXPECT_EQ(QmChatBubbleVisibleCharacters(6.0f, 5), 5);
	EXPECT_EQ(QmChatBubbleVisibleCharacters(1.0f, 0), 0);
}

TEST(QmChatBubbleGeometry, VisibleCountClampsFiniteExtremeBeforeIntegerConversion)
{
	EXPECT_EQ(QmChatBubbleVisibleCharacters(Max, 5), 5);
	EXPECT_EQ(QmChatBubbleVisibleCharacters(-Max, 5), 0);
	EXPECT_EQ(QmChatBubbleVisibleCharacters(static_cast<float>(std::numeric_limits<int>::max()), std::numeric_limits<int>::max()), std::numeric_limits<int>::max());
}

TEST(QmChatBubbleGeometry, VisibleCountRejectsNonfiniteValuesAndNegativeCount)
{
	for(float Value : {Nan, Inf, -Inf})
		EXPECT_FALSE(QmChatBubbleVisibleCharacters(Value, 5));
	EXPECT_FALSE(QmChatBubbleVisibleCharacters(2, -1));
}

TEST(QmChatBubbleGeometry, NormalLayoutUsesComicPaddingGapAndCentering)
{
	const auto Geometry = QmChatBubbleGeometry(vec2(100, 80), vec2(40, 10), 1, 0, 5, true);
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Position, vec2(68, 34));
	EXPECT_EQ(Geometry->m_Size, vec2(64, 30));
	EXPECT_EQ(Geometry->m_ContentPosition, vec2(80, 44));
	EXPECT_EQ(Geometry->m_ContentSize, vec2(40, 10));
}

TEST(QmChatBubbleGeometry, AnimatedLayoutScalesContentAndAppliesSlide)
{
	const auto Geometry = QmChatBubbleGeometry(vec2(100, 80), vec2(40, 10), 0.5f, 12, 5, false);
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_Position, vec2(84, 66.5f));
	EXPECT_EQ(Geometry->m_Size, vec2(32, 15));
	EXPECT_EQ(Geometry->m_ContentPosition, vec2(90, 71.5f));
	EXPECT_EQ(Geometry->m_ContentSize, vec2(20, 5));
}

TEST(QmChatBubbleGeometry, CachedContentKeepsItsOriginalSizeNearUnitScale)
{
	const auto Geometry = QmChatBubbleGeometry(vec2(100, 80), vec2(40, 10), 1.0005f, 0, 5, true);
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_ContentSize, vec2(40, 10));
	EXPECT_GT(Geometry->m_Size.x, 64);
}

TEST(QmChatBubbleGeometry, RejectsInvalidMetricsAndTransforms)
{
	for(vec2 Size : {vec2(-1, 10), vec2(10, -1), vec2(Nan, 10), vec2(10, Inf)})
		EXPECT_FALSE(QmChatBubbleGeometry(vec2(100, 80), Size, 1, 0, 5, false));
	for(float Scale : {0.0f, -1.0f, Inf, Nan})
		EXPECT_FALSE(QmChatBubbleGeometry(vec2(100, 80), vec2(40, 10), Scale, 0, 5, false));
	EXPECT_FALSE(QmChatBubbleGeometry(vec2(Inf, 80), vec2(40, 10), 1, 0, 5, false));
	EXPECT_FALSE(QmChatBubbleGeometry(vec2(100, 80), vec2(40, 10), 1, Inf, 5, false));
}

TEST(QmChatBubbleGeometry, RejectsOverflowOfFinalRectangleEdges)
{
	EXPECT_FALSE(QmChatBubbleGeometry(vec2(Max, 80), vec2(Max, 10), 1, 0, 5, false));
	EXPECT_FALSE(QmChatBubbleGeometry(vec2(100, 80), vec2(Max, 10), 2, 0, 5, false));
}

TEST(QmChatBubbleGeometry, RasterGuardMatchesNormalAndSubunitViewportRules)
{
	EXPECT_TRUE(QmChatBubbleTextSizeSafe(14, 200, 1080));
	EXPECT_TRUE(QmChatBubbleTextSizeSafe(1.0e9f, 0.5f, 1080));
	EXPECT_TRUE(QmChatBubbleTextSizeSafe(14, 200, 0));
}

TEST(QmChatBubbleGeometry, RasterGuardRejectsPixelIntegerOverflow)
{
	EXPECT_FALSE(QmChatBubbleTextSizeSafe(2147483648.0f, 1, 1));
	EXPECT_FALSE(QmChatBubbleTextSizeSafe(Max, 200, 1080));
	EXPECT_FALSE(QmChatBubbleTextSizeSafe(14, Max, 1));
}

TEST(QmChatBubbleGeometry, RasterGuardRejectsInvalidFontAndViewport)
{
	for(float Font : {-1.0f, Nan, Inf})
		EXPECT_FALSE(QmChatBubbleTextSizeSafe(Font, 200, 1080));
	for(float Height : {0.0f, -1.0f, Nan, Inf})
		EXPECT_FALSE(QmChatBubbleTextSizeSafe(14, Height, 1080));
}
