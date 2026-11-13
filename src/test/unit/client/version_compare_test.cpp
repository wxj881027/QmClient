#include <game/client/components/tclient/version_compare.h>

#include <gtest/gtest.h>

TEST(ClientVersionCompare, NumericComponentsDetermineOrdering)
{
	EXPECT_TRUE(IsRemoteVersionNewer("2.0", "1.99"));
	EXPECT_TRUE(IsRemoteVersionNewer("1.10.0", "1.9.99"));
	EXPECT_FALSE(IsRemoteVersionNewer("1.9", "1.10"));
	EXPECT_FALSE(IsRemoteVersionNewer("1.2.3", "1.2.3"));
}

TEST(ClientVersionCompare, WhitespaceAndVersionPrefixAreNormalized)
{
	EXPECT_TRUE(IsRemoteVersionNewer(" \tv2.0 \r\n", " V1.0 "));
	EXPECT_FALSE(IsRemoteVersionNewer(" V1.02 ", "v1.2"));
}

TEST(ClientVersionCompare, MissingInputsDoNotOfferAnUpdate)
{
	EXPECT_FALSE(IsRemoteVersionNewer(nullptr, "1"));
	EXPECT_FALSE(IsRemoteVersionNewer("1", nullptr));
	EXPECT_FALSE(IsRemoteVersionNewer("", "1"));
	EXPECT_FALSE(IsRemoteVersionNewer("1", " \tv "));
}

TEST(ClientVersionCompare, MissingTrailingComponentsAndZerosRemainEquivalent)
{
	EXPECT_FALSE(IsRemoteVersionNewer("1.0.0.0", "1"));
	EXPECT_FALSE(IsRemoteVersionNewer("1.", "1.0"));
	EXPECT_TRUE(IsRemoteVersionNewer("1.0.0.1", "1"));
}

TEST(ClientVersionCompare, ComparisonStillStopsAfterFourComponents)
{
	EXPECT_FALSE(IsRemoteVersionNewer("1.2.3.4.9", "1.2.3.4.0"));
	EXPECT_TRUE(IsRemoteVersionNewer("1.2.3.5.0", "1.2.3.4.9"));
}

TEST(ClientVersionCompare, MalformedComponentKeepsItsExistingZeroFallback)
{
	EXPECT_FALSE(IsRemoteVersionNewer("1.2-preview", "1.1"));
	EXPECT_TRUE(IsRemoteVersionNewer("1.1", "1.2-preview"));
	EXPECT_FALSE(IsRemoteVersionNewer("1.x", "1.0"));
}

TEST(ClientVersionCompare, ComponentAboveIntMaximumIsComparedWithoutOverflow)
{
	EXPECT_TRUE(IsRemoteVersionNewer("2147483648", "2147483647"));
	EXPECT_FALSE(IsRemoteVersionNewer("2147483647", "2147483648"));
	EXPECT_TRUE(IsRemoteVersionNewer("1.4294967296", "1.4294967295"));
}

TEST(ClientVersionCompare, VeryLargeComponentsCompareByTheirNumericValue)
{
	EXPECT_TRUE(IsRemoteVersionNewer("1000000000000000000000000000000", "999999999999999999999999999999"));
	EXPECT_TRUE(IsRemoteVersionNewer("999999999999999999999999999998", "999999999999999999999999999997"));
	EXPECT_FALSE(IsRemoteVersionNewer("999999999999999999999999999997", "999999999999999999999999999998"));
}

TEST(ClientVersionCompare, LeadingZerosDoNotChangeLargeComponentValues)
{
	EXPECT_FALSE(IsRemoteVersionNewer("000000000000000000000000000000000000002147483648", "2147483648"));
	EXPECT_TRUE(IsRemoteVersionNewer("000000000000000000000000000000000000002147483649", "2147483648"));
	EXPECT_FALSE(IsRemoteVersionNewer("000000000000000000000000000000000000000000000000", "0"));
}

TEST(ClientVersionCompare, EqualLargeComponentsContinueToTheNextComponent)
{
	EXPECT_TRUE(IsRemoteVersionNewer("999999999999999999999999999.2", "999999999999999999999999999.1"));
	EXPECT_FALSE(IsRemoteVersionNewer("999999999999999999999999999.1", "999999999999999999999999999.2"));
}
