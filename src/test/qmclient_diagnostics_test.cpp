#include <game/client/components/qmclient/core/qm_diagnostics_json.h>
#include <game/client/components/qmclient/core/qm_diagnostics_metrics.h>
#include <game/client/components/qmclient/core/qm_diagnostics_retention.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

TEST(QmDiagnostics, CalculatesFrameMetrics)
{
	const std::vector<int64_t> Samples{1000000, 2000000, 3000000, 4000000, 5000000};
	EXPECT_DOUBLE_EQ(QmDiagnostics::Average(Samples), 3.0);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Percentile(Samples, 0.0), 1.0);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Percentile(Samples, 0.95), 5.0);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Percentile(Samples, 1.0), 5.0);
	EXPECT_NEAR(QmDiagnostics::OnePercentLow(Samples), 200.0, 0.0001);
	EXPECT_DOUBLE_EQ(QmDiagnostics::Average({}), 0.0);
}

TEST(QmDiagnostics, EscapesInvalidUtf8AndTruncatesSafely)
{
	const char InvalidUtf8[] = {'a', static_cast<char>(0xff), '"', '\n', 'b', '\0'};
	char aBuffer[64];
	QmDiagnostics::EscapeJson(aBuffer, sizeof(aBuffer), InvalidUtf8);
	EXPECT_STREQ(aBuffer, "a\\u00ff\\\"\\nb");

	char aSmallBuffer[4];
	EXPECT_FALSE(QmDiagnostics::EscapeJson(aSmallBuffer, sizeof(aSmallBuffer), "a\\\"b"));
	EXPECT_STREQ(aSmallBuffer, "a\\\\");

	char aZeroBuffer[1] = {'x'};
	EXPECT_TRUE(QmDiagnostics::EscapeJson(aZeroBuffer, sizeof(aZeroBuffer), ""));
	EXPECT_EQ(aZeroBuffer[0], '\0');

	char aFullBuffer[32];
	EXPECT_TRUE(QmDiagnostics::EscapeJson(aFullBuffer, sizeof(aFullBuffer), "a\\\"b"));
	EXPECT_STREQ(aFullBuffer, "a\\\\\\\"b");
}

TEST(QmDiagnostics, RetentionKeepsNewestFilesAndUsesStableTieBreak)
{
	std::vector<QmDiagnostics::SDiagnosticFileEntry> vEntries{
		{"report-old.tmp", 10},
		{"report-tie-a.tmp", 20},
		{"report-new.tmp", 30},
		{"report-tie-b.tmp", 20},
	};

	QmDiagnostics::SortDiagnosticFileEntries(vEntries);
	ASSERT_EQ(vEntries.size(), 4U);
	EXPECT_EQ(vEntries[0].m_Name, "report-new.tmp");
	EXPECT_EQ(vEntries[1].m_Name, "report-tie-b.tmp");
	EXPECT_EQ(vEntries[2].m_Name, "report-tie-a.tmp");
	EXPECT_EQ(vEntries[3].m_Name, "report-old.tmp");
	EXPECT_EQ(QmDiagnostics::FirstDiagnosticFileToRemove(vEntries.size(), 3), 3U);
	EXPECT_EQ(QmDiagnostics::FirstDiagnosticFileToRemove(vEntries.size(), 8), 4U);
	EXPECT_EQ(QmDiagnostics::FirstDiagnosticFileToRemove(0, 3), 0U);
}
