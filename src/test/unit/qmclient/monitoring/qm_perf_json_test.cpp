#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/stutter_diagnostics.h>

#include <gtest/gtest.h>

#include <algorithm>

TEST(QmMonitoringHelpers, PerfJsonFieldPreservesNumbersEscapesAndExistingPrefix)
{
	char aJson[512] = "{\"existing\":1";
	bool First = false;
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "duration_ms", "12.345");
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "text", "line\npath\\name\t");
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "empty", "");
	str_append(aJson, "}", sizeof(aJson));
	EXPECT_STREQ(aJson, "{\"existing\":1,\"duration_ms\":12.345,\"text\":\"line\\npath\\\\name\\t\",\"empty\":\"\"}");
}

TEST(QmMonitoringHelpers, PerfJsonFieldTruncatesAtEveryCapacityWithoutOverwritingTail)
{
	char aFull[256] = "{";
	bool First = true;
	QmPerfAppendJsonField(aFull, sizeof(aFull), First, "text", "中文内容");
	for(int Capacity = 2; Capacity <= (int)str_length(aFull) + 2; ++Capacity)
	{
		char aBuffer[256];
		std::fill(aBuffer, aBuffer + sizeof(aBuffer), '#');
		aBuffer[0] = '{';
		aBuffer[1] = 0;
		bool FirstField = true;
		QmPerfAppendJsonField(aBuffer, Capacity, FirstField, "text", "中文内容");
		EXPECT_LT(str_length(aBuffer), Capacity);
		EXPECT_TRUE(str_utf8_check(aBuffer));
		for(size_t i = Capacity; i < sizeof(aBuffer); ++i)
			EXPECT_EQ(aBuffer[i], '#');
	}
}

class CPerfLoggingPolicyTest : public ::testing::Test
{
	CConfig m_Previous = g_Config;

protected:
	void TearDown() override { g_Config = m_Previous; }
};

TEST_F(CPerfLoggingPolicyTest, DisabledLoggingAndConfiguredThresholdHaveIndependentPolicies)
{
	g_Config.m_QmPerfDebug = 0;
	EXPECT_FALSE(QmPerfEnabled());
	g_Config.m_QmPerfDebug = 1;
	EXPECT_TRUE(QmPerfEnabled());
	g_Config.m_QmPerfStutterDiagnostics = 0;
	g_Config.m_QmPerfDebugThresholdMs = 20;
	EXPECT_FALSE(QmPerfShouldLogDuration(19.999));
	EXPECT_TRUE(QmPerfShouldLogDuration(20.0));
	EXPECT_TRUE(QmPerfShouldLogDuration(0.0, true));
}

TEST_F(CPerfLoggingPolicyTest, StutterDiagnosticsCapThresholdButRespectLowerUserThreshold)
{
	g_Config.m_QmPerfStutterDiagnostics = 1;
	g_Config.m_QmPerfDebugThresholdMs = 20;
	EXPECT_DOUBLE_EQ(QmPerfThresholdMs(), QmStutterFrameBudgetMs());
	g_Config.m_QmPerfDebugThresholdMs = 1;
	EXPECT_DOUBLE_EQ(QmPerfThresholdMs(), 1.0);
}
