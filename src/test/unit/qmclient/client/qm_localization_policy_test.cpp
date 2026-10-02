#include <game/localization.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

TEST(QmLocalization, ContextRequiresOpeningAndClosingBrackets)
{
	EXPECT_TRUE(LocalizationIsContextLine("[menu]"));
	EXPECT_FALSE(LocalizationIsContextLine("[%s] %s (Map: %s, Time: %s)"));
	EXPECT_FALSE(LocalizationIsContextLine("[broken"));
	EXPECT_FALSE(LocalizationIsContextLine("plain"));
}
