#include <game/client/components/qmclient/stutter_diagnostics.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmMonitoringHelpers, StutterPercentileQueriesAllowFurtherRecordingAndReset)
{
	CQmStutterSampleSeries Samples;
	EXPECT_DOUBLE_EQ(Samples.Percentile(95.0), 0.0);
	Samples.Record(10.0, 7);
	Samples.Record(1.0, 8);
	EXPECT_DOUBLE_EQ(Samples.Percentile(50.0), 1.0);
	Samples.Record(10.0, 9);
	Samples.Record(0.0, 10);
	Samples.Record(-1.0, 11);
	Samples.Record(std::numeric_limits<double>::infinity(), 12);
	Samples.Record(std::numeric_limits<double>::quiet_NaN(), 13);
	EXPECT_EQ(Samples.Count(), 4u);
	EXPECT_DOUBLE_EQ(Samples.Percentile(50.0), 1.0);
	EXPECT_DOUBLE_EQ(Samples.Percentile(95.0), 10.0);
	EXPECT_DOUBLE_EQ(Samples.Total(), 21.0);
	EXPECT_EQ(Samples.MaxFrame(), 7u);

	Samples.Reset();
	EXPECT_TRUE(Samples.Empty());
	EXPECT_DOUBLE_EQ(Samples.Total(), 0.0);
	EXPECT_DOUBLE_EQ(Samples.Average(), 0.0);
	EXPECT_DOUBLE_EQ(Samples.Max(), 0.0);
	EXPECT_EQ(Samples.MaxFrame(), 0u);
	EXPECT_DOUBLE_EQ(Samples.Percentile(99.0), 0.0);
	Samples.Record(2.0, 20);
	EXPECT_DOUBLE_EQ(Samples.Percentile(0.0), 2.0);
	EXPECT_DOUBLE_EQ(Samples.Percentile(100.0), 2.0);
	EXPECT_EQ(Samples.MaxFrame(), 20u);
}

TEST(QmStutterSampleSeries, RepeatedPercentilesKeepTheSameValuesAndFrameAttribution)
{
	CQmStutterSampleSeries Samples;
	Samples.Record(8.0, 11);
	Samples.Record(1.0, 12);
	Samples.Record(4.0, 13);
	Samples.Record(2.0, 14);
	for(int Repeat = 0; Repeat < 4; ++Repeat)
	{
		EXPECT_DOUBLE_EQ(Samples.Percentile(95.0), 8.0);
		EXPECT_DOUBLE_EQ(Samples.Percentile(50.0), 2.0);
		EXPECT_DOUBLE_EQ(Samples.Percentile(-1.0), 1.0);
		EXPECT_DOUBLE_EQ(Samples.Percentile(101.0), 8.0);
		EXPECT_DOUBLE_EQ(Samples.Total(), 15.0);
		EXPECT_EQ(Samples.MaxFrame(), 11u);
	}
}
