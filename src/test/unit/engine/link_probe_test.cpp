#include <engine/client/link_probe.h>

#include <gtest/gtest.h>

namespace
{
	constexpr int64_t FREQ = 1000;

	CUuid MakeId(unsigned char Value)
	{
		CUuid Id = {};
		Id.m_aData[0] = Value;
		return Id;
	}

	// 在 Now 时刻发出 Count 个探针，前 Confirmed 个收到回复，随后推进到 End 时刻结算。
	void RunSamples(CLinkProbeTracker &Tracker, int Count, int Confirmed, int64_t Now, int64_t End)
	{
		for(int i = 0; i < Count; i++)
		{
			ASSERT_TRUE(Tracker.RecordSent(MakeId(static_cast<unsigned char>(i + 1)), Now));
			if(i < Confirmed)
				ASSERT_TRUE(Tracker.Confirm(MakeId(static_cast<unsigned char>(i + 1))));
		}
		Tracker.Update(End, FREQ);
	}
}

TEST(LinkProbe, NoEstimateMeansSingleCopy)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	EXPECT_FALSE(Tracker.HasEstimate());
	EXPECT_EQ(Tracker.Copies(3), 1);
}

TEST(LinkProbe, ConfirmOfUnknownIdIsRejected)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	ASSERT_TRUE(Tracker.RecordSent(MakeId(1), 0));
	EXPECT_FALSE(Tracker.Confirm(MakeId(2)));
	EXPECT_TRUE(Tracker.Confirm(MakeId(1)));
}

TEST(LinkProbe, SampleIsNotSettledBeforeTwoSeconds)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	ASSERT_TRUE(Tracker.RecordSent(MakeId(1), 0));
	Tracker.Update(2 * FREQ - 1, FREQ);
	EXPECT_EQ(Tracker.TotalSamples(), 0u);
	Tracker.Update(2 * FREQ, FREQ);
	EXPECT_EQ(Tracker.TotalSamples(), 1u);
	EXPECT_EQ(Tracker.TotalUnconfirmed(), 1u);
}

TEST(LinkProbe, ConfirmAfterSettlementIsRejected)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	ASSERT_TRUE(Tracker.RecordSent(MakeId(1), 0));
	Tracker.Update(2 * FREQ, FREQ);
	EXPECT_FALSE(Tracker.Confirm(MakeId(1)));
}

TEST(LinkProbe, EstimateNeedsTenSamplesAndTenSeconds)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	RunSamples(Tracker, 9, 0, 0, 10 * FREQ);
	EXPECT_FALSE(Tracker.HasEstimate());
	EXPECT_EQ(Tracker.WindowSamples(), 9u);

	// 样本够了但窗口不满十秒也不出估计。
	CLinkProbeTracker Short;
	Short.Reset(0);
	RunSamples(Short, 10, 0, 0, 9 * FREQ);
	EXPECT_FALSE(Short.HasEstimate());
}

TEST(LinkProbe, HighUnconfirmedRateUsesLimitCopies)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	RunSamples(Tracker, 10, 8, 0, 10 * FREQ);
	ASSERT_TRUE(Tracker.HasEstimate());
	EXPECT_FLOAT_EQ(Tracker.UnconfirmedPercent(), 20.0f);
	EXPECT_EQ(Tracker.Copies(3), 3);
	EXPECT_EQ(Tracker.Copies(2), 2);
	EXPECT_EQ(Tracker.Copies(1), 1);
	EXPECT_EQ(Tracker.Copies(0), 0);
}

TEST(LinkProbe, MediumUnconfirmedRateUsesTwoCopies)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	RunSamples(Tracker, 20, 19, 0, 10 * FREQ);
	ASSERT_TRUE(Tracker.HasEstimate());
	EXPECT_FLOAT_EQ(Tracker.UnconfirmedPercent(), 5.0f);
	EXPECT_EQ(Tracker.Copies(3), 2);
}

TEST(LinkProbe, CleanLinkKeepsSingleCopy)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	RunSamples(Tracker, 20, 20, 0, 10 * FREQ);
	ASSERT_TRUE(Tracker.HasEstimate());
	EXPECT_FLOAT_EQ(Tracker.UnconfirmedPercent(), 0.0f);
	EXPECT_EQ(Tracker.Copies(3), 1);
}

TEST(LinkProbe, NewWindowIsSmoothedWithPreviousEstimate)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	RunSamples(Tracker, 10, 0, 0, 10 * FREQ);
	ASSERT_TRUE(Tracker.HasEstimate());
	EXPECT_FLOAT_EQ(Tracker.UnconfirmedPercent(), 100.0f);

	// 下一个窗口全部确认，估计值向 0 移动一半而不是直接归零。
	RunSamples(Tracker, 10, 10, 10 * FREQ, 20 * FREQ);
	EXPECT_FLOAT_EQ(Tracker.UnconfirmedPercent(), 50.0f);
}

TEST(LinkProbe, MainThreadStallDiscardsInFlightSamples)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	ASSERT_TRUE(Tracker.RecordSent(MakeId(1), 0));
	// 两帧间隔超过 1/4 秒，无法判断回复何时到达，在途样本作废而不计未确认。
	Tracker.OnFrame(FREQ, FREQ);
	EXPECT_EQ(Tracker.TotalIgnored(), 1u);
	Tracker.Update(10 * FREQ, FREQ);
	EXPECT_EQ(Tracker.TotalSamples(), 0u);
	EXPECT_EQ(Tracker.TotalUnconfirmed(), 0u);
	EXPECT_FALSE(Tracker.Confirm(MakeId(1)));
}

TEST(LinkProbe, NormalFrameGapKeepsInFlightSamples)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	ASSERT_TRUE(Tracker.RecordSent(MakeId(1), 0));
	Tracker.OnFrame(FREQ / 4, FREQ);
	EXPECT_EQ(Tracker.TotalIgnored(), 0u);
	EXPECT_TRUE(Tracker.Confirm(MakeId(1)));
}

TEST(LinkProbe, RecordSentFailsWhenAllSlotsAreInFlight)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	for(int i = 0; i < 64; i++)
		ASSERT_TRUE(Tracker.RecordSent(MakeId(static_cast<unsigned char>(i + 1)), 0));
	EXPECT_FALSE(Tracker.RecordSent(MakeId(200), 0));
	// 结算后槽位被回收。
	Tracker.Update(2 * FREQ, FREQ);
	EXPECT_TRUE(Tracker.RecordSent(MakeId(200), 2 * FREQ));
}

TEST(LinkProbe, ResetClearsEstimateAndCounters)
{
	CLinkProbeTracker Tracker;
	Tracker.Reset(0);
	RunSamples(Tracker, 10, 0, 0, 10 * FREQ);
	ASSERT_TRUE(Tracker.HasEstimate());
	Tracker.Reset(20 * FREQ);
	EXPECT_FALSE(Tracker.HasEstimate());
	EXPECT_EQ(Tracker.TotalSamples(), 0u);
	EXPECT_EQ(Tracker.Copies(3), 1);
}
