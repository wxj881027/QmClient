#include <game/client/components/qmclient/perf_logging.h>

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

TEST(QmMonitoringHelpers, PerfPayloadKeepsNumericLookingSkinNamesAsStrings)
{
	for(const char *pSkin : {"2.3.1", "007", "+12", ".5", "5."})
	{
		char aPayload[128];
		str_format(aPayload, sizeof(aPayload), "first_visible_skin=%s first_visible_index=0 duration_ms=-0.5", pSkin);
		char aJson[256] = "{";
		bool First = true;
		QmPerfAppendPayloadJsonFields(aJson, sizeof(aJson), First, aPayload);
		str_append(aJson, "}", sizeof(aJson));
		char aExpected[256];
		str_format(aExpected, sizeof(aExpected), "{\"first_visible_skin\":\"%s\",\"first_visible_index\":0,\"duration_ms\":-0.5}", pSkin);
		EXPECT_STREQ(aJson, aExpected);
	}
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

TEST_F(CPerfLoggingPolicyTest, ConfiguredThresholdAppliesWhenLoggingIsEnabled)
{
	ASSERT_TRUE(QmApplyPerfPreferences(0, CConfig::ms_QmPerfDebugThresholdMs));
	EXPECT_FALSE(QmPerfEnabled());
	EXPECT_DOUBLE_EQ(QmPerfThresholdMs(), 50.0);

	ASSERT_TRUE(QmApplyPerfPreferences(1, 20));
	EXPECT_TRUE(QmPerfEnabled());
	EXPECT_FALSE(QmPerfShouldLogDuration(19.999));
	EXPECT_TRUE(QmPerfShouldLogDuration(20.0));
	EXPECT_TRUE(QmPerfShouldLogDuration(0.0, true));
}

TEST_F(CPerfLoggingPolicyTest, InvalidArgumentsLeaveBothPreferencesUnchanged)
{
	ASSERT_TRUE(QmApplyPerfPreferences(1, 400));
	EXPECT_FALSE(QmApplyPerfPreferences(0, 19));
	EXPECT_FALSE(QmApplyPerfPreferences(0, 401));
	EXPECT_FALSE(QmApplyPerfPreferences(2, 50));
	EXPECT_TRUE(QmPerfEnabled());
	EXPECT_DOUBLE_EQ(QmPerfThresholdMs(), 400.0);

	EXPECT_TRUE(QmApplyPerfPreferences(0, 50));
	EXPECT_FALSE(QmPerfEnabled());
}
