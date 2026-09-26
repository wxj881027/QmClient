// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <gtest/gtest.h>

TEST(ColorContract, QmTeeColorCodeInputsBindClassicBodyAndFeetColors)
{
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string Settings7 = ReadTestSourceFile("src/game/client/components/menus_settings7.cpp");

	EXPECT_NE(Settings.find("static CLineInputBuffered<QM_TEE_COLOR_CODE_INPUT_SIZE> s_aaTeeColorCodeInputs[NUM_DUMMIES][2]"), std::string::npos);
	EXPECT_NE(Settings.find("CLineInput &ColorCodeInput = s_aaTeeColorCodeInputs[m_Dummy][Part]"), std::string::npos);
	EXPECT_NE(Settings.find("QmFormatTeeColorCode(*apColors[Part])"), std::string::npos);
	EXPECT_NE(Settings.find("if(!ColorCodeInput.IsActive())"), std::string::npos);
	EXPECT_EQ(Settings7.find("QmParseTeeColorCode"), std::string::npos);

	const size_t ParsePosition = Settings.find("QmParseTeeColorCode(ColorCodeInput.GetString())");
	ASSERT_NE(ParsePosition, std::string::npos);
	const size_t AssignmentPosition = Settings.find("*apColors[Part] = *Color", ParsePosition);
	const size_t SendPosition = Settings.find("SetNeedSendInfo()", ParsePosition);
	const size_t SlidersPosition = Settings.find("if(RenderHslaScrollbars", ParsePosition);
	ASSERT_NE(AssignmentPosition, std::string::npos);
	ASSERT_NE(SendPosition, std::string::npos);
	ASSERT_NE(SlidersPosition, std::string::npos);
	EXPECT_LT(ParsePosition, AssignmentPosition);
	EXPECT_LT(AssignmentPosition, SendPosition);
	EXPECT_LT(SendPosition, SlidersPosition);
}
