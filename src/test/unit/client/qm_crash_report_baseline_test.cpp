#include <engine/client/qm_crash_report_marker.h>

#include <gtest/gtest.h>

#include <limits>
#include <string_view>

TEST(QmCrashReportBaseline, ReadsExistingPositiveDecimalFormat)
{
	EXPECT_EQ(QmCrashReportMarker::ParseStartedAt("1700000000\n"), 1700000000LL);
	EXPECT_EQ(QmCrashReportMarker::ParseStartedAt("1"), 1);
}

TEST(QmCrashReportBaseline, KeepsDecimalWhitespacePlusAndLeadingZeroes)
{
	EXPECT_EQ(QmCrashReportMarker::ParseStartedAt("\t +123 \r\n"), 123);
	EXPECT_EQ(QmCrashReportMarker::ParseStartedAt("\v\f+123\f\v\n"), 123);
	EXPECT_EQ(QmCrashReportMarker::ParseStartedAt("000123"), 123);
}

TEST(QmCrashReportBaseline, AcceptsTheMaximumPositiveSignedTimestamp)
{
	EXPECT_EQ(QmCrashReportMarker::ParseStartedAt("9223372036854775807\n"), std::numeric_limits<int64_t>::max());
}

TEST(QmCrashReportBaseline, RejectsNumericOverflowWithoutPublishingSaturatedTimestamp)
{
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("9223372036854775808"));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("18446744073709551615"));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("-9223372036854775809"));
}

TEST(QmCrashReportBaseline, RejectsNonpositiveTimestamps)
{
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("0\n"));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("-1\n"));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("-9223372036854775808"));
}

TEST(QmCrashReportBaseline, RejectsEmptyOrWhitespaceOnlyText)
{
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt(""));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt(" \t\r\n"));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("+"));
}

TEST(QmCrashReportBaseline, RequiresTheWholeTrimmedValueToBeOneDecimalInteger)
{
	for(const auto *pText : {"123suffix", "123 456", "123\n456", "0x123", "123.0", "++123"})
	{
		SCOPED_TRACE(pText);
		EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt(pText));
	}
}

TEST(QmCrashReportBaseline, EmbeddedNullDoesNotHideTrailingBytes)
{
	const char aText[] = "123\0suffix";
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt(std::string_view(aText, sizeof(aText) - 1)));
}

TEST(QmCrashReportBaseline, InvalidBytesAreNotAcceptedAsWhitespaceOrDigits)
{
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("\xff"
							 "123"));
	EXPECT_FALSE(QmCrashReportMarker::ParseStartedAt("\xc2\xa0"
							 "123"));
}
