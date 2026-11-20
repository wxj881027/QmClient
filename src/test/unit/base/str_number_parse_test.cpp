#include <base/str.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>

TEST(StrNumberParse, DecimalOutsideIntRangeLeavesOutputUnchanged)
{
	const std::string AboveMax = std::to_string(static_cast<int64_t>(std::numeric_limits<int>::max()) + 1);
	const std::string BelowMin = std::to_string(static_cast<int64_t>(std::numeric_limits<int>::min()) - 1);
	for(const char *pInput : {AboveMax.c_str(), BelowMin.c_str(), "999999999999999999999999999999"})
	{
		SCOPED_TRACE(pInput);
		int Value = 17;
		EXPECT_FALSE(str_toint(pInput, &Value));
		EXPECT_EQ(Value, 17);
	}
}

TEST(StrNumberParse, DecimalRequiresDigitsAndConsumesTheWholeInput)
{
	for(const char *pInput : {"", " \t", "-", "12x"})
	{
		SCOPED_TRACE(pInput);
		EXPECT_FALSE(str_toint(pInput, nullptr));
	}
}

TEST(StrNumberParse, DecimalAcceptsIntBoundariesAndSignedInput)
{
	for(const int Expected : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		const std::string Input = std::to_string(Expected);
		int Value = 0;
		ASSERT_TRUE(str_toint(Input.c_str(), &Value));
		EXPECT_EQ(Value, Expected);
	}
	int Value = 0;
	ASSERT_TRUE(str_toint(" \t+42", &Value));
	EXPECT_EQ(Value, 42);
	EXPECT_TRUE(str_toint("42", nullptr));
}
