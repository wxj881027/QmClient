#include <engine/client/input_sdl3.h>

#include <gtest/gtest.h>

#include <limits>

TEST(SdlCompositionCursor, CharacterOffsetsUseUtf8ByteBoundaries)
{
	EXPECT_EQ(SdlCompositionCursor("abc", 2), 2);
	EXPECT_EQ(SdlCompositionCursor("中a🙂", 1), 3);
	EXPECT_EQ(SdlCompositionCursor("中a🙂", 2), 4);
	EXPECT_EQ(SdlCompositionCursor("中a🙂", 3), 8);
}

TEST(SdlCompositionCursor, OversizedOffsetsStopAtTheStringEnd)
{
	EXPECT_EQ(SdlCompositionCursor("中a🙂", 100), 8);
	EXPECT_EQ(SdlCompositionCursor("abc", std::numeric_limits<int>::max()), 3);
}

TEST(SdlCompositionCursor, NonPositiveOffsetsStayAtTheBeginning)
{
	EXPECT_EQ(SdlCompositionCursor("abc", 0), 0);
	EXPECT_EQ(SdlCompositionCursor("abc", -1), 0);
	EXPECT_EQ(SdlCompositionCursor("abc", std::numeric_limits<int>::min()), 0);
}

TEST(SdlCompositionCursor, EmptyAndMissingTextHaveNoCursorAdvance)
{
	EXPECT_EQ(SdlCompositionCursor("", std::numeric_limits<int>::max()), 0);
	EXPECT_EQ(SdlCompositionCursor(nullptr, std::numeric_limits<int>::max()), 0);
}

TEST(SdlMouseButtons, FirstAndLastMaskBitsUseTheirOneBasedButtonNumbers)
{
	EXPECT_TRUE(SdlMouseButtonPressed(1, 1));
	EXPECT_FALSE(SdlMouseButtonPressed(1, 2));
	EXPECT_TRUE(SdlMouseButtonPressed(0x80000000u, 32));
	EXPECT_FALSE(SdlMouseButtonPressed(0x80000000u, 31));
}

TEST(SdlMouseButtons, OutOfRangeButtonsAreRejectedBeforeShifting)
{
	for(int Index : {0, -1, 33, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
		EXPECT_FALSE(SdlMouseButtonPressed(0xffffffffu, Index)) << Index;
}

TEST(SdlMouseButtons, EmptyMaskDoesNotReportAnyValidButtonPressed)
{
	for(int Index = 1; Index <= 32; ++Index)
		EXPECT_FALSE(SdlMouseButtonPressed(0, Index)) << Index;
}
