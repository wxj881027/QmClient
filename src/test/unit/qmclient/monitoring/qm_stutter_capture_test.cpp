#include <game/client/components/qmclient/stutter_capture.h>

#include <gtest/gtest.h>

#include <new>
#include <string>
#include <vector>

namespace
{
	class CObservedCapture : public CQmStutterCapture::COperations
	{
	public:
		bool m_FailComponents = false;
		int m_FailSeries = -1;
		bool m_FailAfterPreparation = false;
		bool m_FailCapture = false;
		int m_SeriesCalls = 0;
		int m_CaptureCalls = 0;
		int m_FailEmit = -1;
		int m_EmitCalls = 0;
		std::vector<std::string> m_vPayloads;
		std::vector<bool> m_vForced;

		void PrepareComponents(std::vector<CQmStutterCapture::SComponent> &vComponents, size_t Count) override
		{
			COperations::PrepareComponents(vComponents, Count);
			if(m_FailComponents)
				throw std::bad_alloc();
		}
		void PrepareSeries(CQmStutterSampleSeries &Series, double Value) override
		{
			const bool Fail = ++m_SeriesCalls == m_FailSeries;
			if(Fail && !m_FailAfterPreparation)
				throw std::bad_alloc();
			COperations::PrepareSeries(Series, Value);
			if(Fail)
				throw std::bad_alloc();
		}
		CQmStutterCapture::SFeatures CaptureFeatures(const CQmStutterCapture::SFrame &Frame) override
		{
			++m_CaptureCalls;
			auto Features = COperations::CaptureFeatures(Frame);
			if(m_FailCapture)
				throw std::bad_alloc();
			return Features;
		}
		void Emit(const CQmStutterCapture::SFrame &, bool Force, const char *pPayload) override
		{
			++m_EmitCalls;
			m_vPayloads.emplace_back(pPayload);
			m_vForced.push_back(Force);
			// 模拟外部输出已发生后才报告失败，不能重放这一条。
			if(m_EmitCalls == m_FailEmit)
				throw std::bad_alloc();
		}
	};

	struct CState
	{
		CObservedCapture m_Operations;
		CQmStutterCapture m_Capture{&m_Operations};
		const char *m_apNames[2] = {"menus", "hud"};
		CQmStutterCapture::SFrame m_Frame;
		CState()
		{
			m_Frame.m_Enabled = true;
			m_Frame.m_ComponentCount = 2;
			m_Frame.m_ppComponentNames = m_apNames;
			m_Frame.m_Frame = 1;
			m_Frame.m_FrameMs = 6.0;
		}
		bool Record(uint64_t Frame, double FrameMs)
		{
			m_Frame.m_Frame = Frame;
			m_Frame.m_FrameMs = FrameMs;
			return m_Capture.ProcessFrame(m_Frame);
		}
	};
}

TEST(QmStutterCapture, DisabledFramesDoNotAllocateDiagnosticState)
{
	CState State;
	State.m_Operations.m_FailComponents = State.m_Operations.m_FailCapture = true;
	State.m_Operations.m_FailSeries = 1;
	State.m_Frame.m_Enabled = false;
	EXPECT_TRUE(State.Record(1, 20.0));
	EXPECT_FALSE(State.m_Capture.Active());
	EXPECT_EQ(State.m_Operations.m_SeriesCalls, 0);
	EXPECT_EQ(State.m_Operations.m_CaptureCalls, 0);
	EXPECT_TRUE(State.m_Operations.m_vPayloads.empty());
}

TEST(QmStutterCapture, ComponentPreparationFailureRejectsPartialFrameAndCanRetry)
{
	CState State;
	State.m_Operations.m_FailComponents = true;
	EXPECT_FALSE(State.m_Capture.PrepareComponents(2));
	State.m_Capture.RecordUpdate(0, 100.0);
	EXPECT_FALSE(State.Record(1, 6.0));
	EXPECT_EQ(State.m_Capture.StutterId(), 0u);
	State.m_Operations.m_FailComponents = false;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	State.m_Capture.RecordUpdate(0, 2.0);
	EXPECT_TRUE(State.Record(2, 6.0));
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
	ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
	EXPECT_NE(State.m_Operations.m_vPayloads[1].find("total_ms=2.000"), std::string::npos);
}

TEST(QmStutterCapture, SuccessfulRenderPreparationDoesNotHideEarlierUpdatePreparationFailure)
{
	CState State;
	State.m_Operations.m_FailComponents = true;
	EXPECT_FALSE(State.m_Capture.PrepareComponents(2));
	State.m_Operations.m_FailComponents = false;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	State.m_Capture.RecordRender(0, 3.0);
	EXPECT_FALSE(State.Record(1, 6.0));
	EXPECT_FALSE(State.m_Capture.Active());
	EXPECT_TRUE(State.Record(2, 6.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
}

TEST(QmStutterCapture, AnySeriesPreparationFailureLeavesTrackerAndAllSamplesUncommitted)
{
	for(int FailedSeries = 1; FailedSeries <= 5; ++FailedSeries)
	{
		for(bool AfterPreparation : {false, true})
		{
			SCOPED_TRACE(FailedSeries);
			SCOPED_TRACE(AfterPreparation);
			CState State;
			ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
			State.m_Operations.m_FailSeries = FailedSeries;
			State.m_Operations.m_FailAfterPreparation = AfterPreparation;
			State.m_Capture.RecordUpdate(0, 100.0);
			EXPECT_FALSE(State.Record(1, 6.0));
			EXPECT_EQ(State.m_Capture.SampleCount(), 0u);
			EXPECT_EQ(State.m_Capture.StutterId(), 0u);
			EXPECT_FALSE(State.m_Capture.Active());
			State.m_Operations.m_FailSeries = -1;
			State.m_Capture.RecordUpdate(0, 2.0);
			EXPECT_TRUE(State.Record(2, 6.0));
			EXPECT_EQ(State.m_Capture.StutterId(), 1u);
			EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
			ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
			EXPECT_NE(State.m_Operations.m_vPayloads[1].find("sample_count=1 total_ms=2.000"), std::string::npos);
		}
	}
}

TEST(QmStutterCapture, InitialSnapshotFailureDoesNotStartAnEmptyEpisode)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	State.m_Operations.m_FailCapture = true;
	EXPECT_FALSE(State.Record(1, 6.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 0u);
	EXPECT_EQ(State.m_Capture.StutterId(), 0u);
	State.m_Operations.m_FailCapture = false;
	EXPECT_TRUE(State.Record(2, 6.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
	EXPECT_EQ(State.m_Capture.WorstFrame(), 2u);
}

TEST(QmStutterCapture, LaterSnapshotFailurePreservesEarlierWorstFrameAndWindow)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	ASSERT_TRUE(State.Record(1, 10.0));
	State.m_Operations.m_FailCapture = true;
	EXPECT_FALSE(State.Record(2, 20.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
	EXPECT_EQ(State.m_Capture.WorstFrame(), 1u);
	EXPECT_TRUE(State.Record(3, 8.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 2u);
	EXPECT_EQ(State.m_Operations.m_CaptureCalls, 2);
}

TEST(QmStutterCapture, FailedPeriodicFrameDoesNotAdvanceTheSegmentOrLosePreviousSamples)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	ASSERT_TRUE(State.Record(1, 9999.0));
	State.m_Operations.m_SeriesCalls = 0;
	State.m_Operations.m_FailSeries = 3;
	EXPECT_FALSE(State.Record(2, 6.0));
	EXPECT_EQ(State.m_Capture.Segment(), 0u);
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
	EXPECT_TRUE(State.m_Operations.m_vPayloads.empty());
	State.m_Operations.m_FailSeries = -1;
	EXPECT_TRUE(State.Record(3, 6.0));
	EXPECT_EQ(State.m_Capture.Segment(), 1u);
	EXPECT_EQ(State.m_Capture.SampleCount(), 0u);
	ASSERT_FALSE(State.m_Operations.m_vPayloads.empty());
	EXPECT_NE(State.m_Operations.m_vPayloads[0].find("sample_frames=2"), std::string::npos);
	EXPECT_NE(State.m_Operations.m_vPayloads[0].find("end_reason=periodic"), std::string::npos);
}

TEST(QmStutterCapture, FastFramesClearPendingDurationsBeforeNextLowFrame)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	State.m_Capture.RecordUpdate(0, 100.0);
	EXPECT_TRUE(State.Record(1, 1.0));
	State.m_Capture.RecordUpdate(0, 2.0);
	EXPECT_TRUE(State.Record(2, 6.0));
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
	ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
	EXPECT_NE(State.m_Operations.m_vPayloads[1].find("total_ms=2.000"), std::string::npos);
}

TEST(QmStutterCapture, DisablingFlushesOnceAndReopeningStartsANewWindow)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	ASSERT_TRUE(State.Record(1, 6.0));
	State.m_Frame.m_Enabled = false;
	EXPECT_TRUE(State.Record(2, 6.0));
	EXPECT_FALSE(State.m_Capture.Active());
	ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
	EXPECT_TRUE(State.m_Operations.m_vForced[0]);
	EXPECT_NE(State.m_Operations.m_vPayloads[0].find("end_reason=diagnostics_disabled"), std::string::npos);
	EXPECT_TRUE(State.Record(3, 6.0));
	EXPECT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
	State.m_Frame.m_Enabled = true;
	EXPECT_TRUE(State.Record(4, 7.0));
	EXPECT_EQ(State.m_Capture.StutterId(), 1u);
	EXPECT_EQ(State.m_Capture.WorstFrame(), 4u);
}

TEST(QmStutterCapture, OutputFailureContinuesOtherRecordsAndDoesNotReplayClosedWindow)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	ASSERT_TRUE(State.Record(1, 6.0));
	State.m_Operations.m_FailEmit = 1;
	EXPECT_FALSE(State.m_Capture.Finish(State.m_Frame));
	EXPECT_FALSE(State.m_Capture.Active());
	EXPECT_EQ(State.m_Capture.SampleCount(), 0u);
	EXPECT_EQ(State.m_Operations.m_EmitCalls, 5);
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
	EXPECT_EQ(State.m_Operations.m_EmitCalls, 5);
}

TEST(QmStutterCapture, PeriodicOutputFailureStillAllowsTheNextSegment)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	State.m_Operations.m_FailEmit = 1;
	EXPECT_FALSE(State.Record(1, 10000.0));
	EXPECT_TRUE(State.m_Capture.Active());
	EXPECT_EQ(State.m_Capture.Segment(), 1u);
	EXPECT_EQ(State.m_Capture.SampleCount(), 0u);
	EXPECT_TRUE(State.Record(2, 6.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
	ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 10u);
	EXPECT_NE(State.m_Operations.m_vPayloads[5].find("segment=1"), std::string::npos);
}

TEST(QmStutterCapture, ShutdownWithMismatchedComponentNamesDropsWindowWithoutIndexingThem)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	ASSERT_TRUE(State.Record(1, 6.0));
	State.m_Frame.m_ComponentCount = 0;
	State.m_Frame.m_ppComponentNames = nullptr;
	EXPECT_FALSE(State.m_Capture.Finish(State.m_Frame));
	EXPECT_FALSE(State.m_Capture.Active());
	EXPECT_TRUE(State.m_Operations.m_vPayloads.empty());
}

TEST(QmStutterCapture, FailedFrameWithMismatchedNamesKeepsEarlierWindowForValidShutdown)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	ASSERT_TRUE(State.Record(1, 6.0));
	State.m_Frame.m_ppComponentNames = nullptr;
	EXPECT_FALSE(State.Record(2, 9.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
	State.m_Frame.m_ppComponentNames = State.m_apNames;
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
}

TEST(QmStutterCapture, InvalidComponentIndicesDoNotWriteOutsidePendingArray)
{
	CState State;
	ASSERT_TRUE(State.m_Capture.PrepareComponents(2));
	State.m_Capture.RecordUpdate(2, 100.0);
	State.m_Capture.RecordRender(1000, 100.0);
	EXPECT_TRUE(State.Record(1, 6.0));
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
	ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
	EXPECT_NE(State.m_Operations.m_vPayloads[1].find("total_ms=0.000"), std::string::npos);
}

TEST(QmStutterCapture, EnablingAtRenderTailPreparesTheOriginalZeroComponentSamples)
{
	CState State;
	EXPECT_TRUE(State.Record(1, 6.0));
	EXPECT_EQ(State.m_Capture.SampleCount(), 1u);
	EXPECT_TRUE(State.m_Capture.Finish(State.m_Frame));
	ASSERT_EQ(State.m_Operations.m_vPayloads.size(), 5u);
	EXPECT_NE(State.m_Operations.m_vPayloads[1].find("sample_count=1 total_ms=0.000"), std::string::npos);
}
