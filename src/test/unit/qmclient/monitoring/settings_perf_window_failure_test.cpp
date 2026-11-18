#include <game/client/components/qmclient/settings_perf_windows.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(SettingsPerfWindowFailure, FirstSampleFailureStopsTheWindowAndAllowsRestart)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("tab_switch", "settings", "qmclient", "visual", 10, false);
	SQmSettingsPerfWindowFrameResult Failed;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Failed = Tracker.RecordFrame(0.010f, 2.0, false, 100);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Failed.m_ShouldFlush);
	EXPECT_EQ(Failed.m_Summary.m_SampleFrames, 0);
	EXPECT_FALSE(Tracker.HasActiveWindow());

	EXPECT_FALSE(Tracker.StartFixedFrameWindow("tab_switch", "settings", "qmclient", "visual", 1, false).m_ShouldFlush);
	const auto Restarted = Tracker.RecordFrame(0.020f, 3.0, false, 101);
	ASSERT_TRUE(Restarted.m_ShouldFlush);
	EXPECT_EQ(Restarted.m_Summary.m_SampleFrames, 1);
	EXPECT_EQ(Restarted.m_Summary.m_WindowStartFrame, 101u);
	EXPECT_EQ(Restarted.m_Summary.m_WindowEndFrame, 101u);
}

TEST(SettingsPerfWindowFailure, GrowthFailureFlushesOnlyRecordedFrames)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("tab_switch", "settings", "qmclient", "visual", 4096, false);
	ASSERT_FALSE(Tracker.RecordFrame(0.010f, 2.0, false, 100).m_ShouldFlush);

	SQmSettingsPerfWindowFrameResult Failed;
	bool Triggered = false;
	uint64_t LastRecorded = 100;
	int RecordedFrames = 1;
	for(uint64_t Frame = 101; Frame < 1024; ++Frame)
	{
		{
			CPredictionAllocationFailure Fault;
			Failed = Tracker.RecordFrame(0.010f, 2.0, false, Frame);
			Triggered = Fault.Triggered();
		}
		if(Triggered)
			break;
		ASSERT_FALSE(Failed.m_ShouldFlush);
		LastRecorded = Frame;
		++RecordedFrames;
	}
	ASSERT_TRUE(Triggered);
	ASSERT_TRUE(Failed.m_ShouldFlush);
	EXPECT_EQ(Failed.m_Summary.m_SampleFrames, RecordedFrames);
	EXPECT_EQ(Failed.m_Summary.m_WindowStartFrame, 100u);
	EXPECT_EQ(Failed.m_Summary.m_WindowEndFrame, LastRecorded);
	EXPECT_FLOAT_EQ(Failed.m_Summary.m_FrameMsP99, 10.0f);
	EXPECT_FALSE(Tracker.HasActiveWindow());
}

TEST(SettingsPerfWindowFailure, FinishingComputesPercentilesWithoutAllocating)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("tab_switch", "settings", "qmclient", "visual", 200, false);
	Tracker.RecordFrame(0.080f, 2.0, false);
	for(int i = 0; i < 94; ++i)
		Tracker.RecordFrame(0.010f, 2.0, false);
	Tracker.RecordFrame(0.040f, 2.0, false);
	for(int i = 0; i < 4; ++i)
		Tracker.RecordFrame(0.020f, 2.0, false);

	SQmSettingsPerfWindowSummary Summary;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Summary = Tracker.FinishActiveWindow();
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Summary.m_SampleFrames, 100);
	EXPECT_FLOAT_EQ(Summary.m_FrameMsP95, 20.0f);
	EXPECT_FLOAT_EQ(Summary.m_FrameMsP99, 40.0f);
	EXPECT_FLOAT_EQ(Summary.m_FpsOnePctLow, 12.5f);
	EXPECT_FALSE(Tracker.HasActiveWindow());
}
