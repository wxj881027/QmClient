#include <engine/shared/demo_output.h>

#include <generated/protocol.h>

#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
	class CSliceOutputProbe : public demo_output::COperations
	{
	public:
		int m_Creates = 0;
		ECreate CreateDirectory(const char *pPath) override
		{
			++m_Creates;
			return COperations::CreateDirectory(pPath);
		}
	};

	class CSliceSnapshots : public CDemoPlayer::IListener
	{
	public:
		CDemoPlayer *m_pPlayer = nullptr;
		std::vector<int> m_vTicks;
		std::vector<int> m_vValues;
		void OnDemoPlayerSnapshot(void *pData, int) override
		{
			const auto *pFlag = static_cast<const CNetObj_Flag *>(static_cast<const CSnapshot *>(pData)->FindItem(NETOBJTYPE_FLAG, 0));
			ASSERT_NE(pFlag, nullptr);
			m_vTicks.push_back(m_pPlayer->Info()->m_Info.m_CurrentTick);
			m_vValues.push_back(pFlag->m_X);
		}
		void OnDemoPlayerMessage(void *, int) override {}
	};

	class DemoSliceSegmentsIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CSliceOutputProbe m_Output;
		CDemoEditor m_Editor;
		void SetUp() override
		{
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
			m_Editor.Init(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, nullptr, m_State.m_pStorage.get(), nullptr, &m_Output);
		}
		bool Slice(const std::vector<SDemoSliceSegment> &vSegments)
		{
			return m_Editor.Slice("source.demo", "target.demo", vSegments, nullptr, nullptr);
		}
		void CreateChangingTimeline(std::initializer_list<int> Ticks, std::initializer_list<int> Markers)
		{
			unsigned char aMap[] = {'m', 'a', 'p', 'b', 'y', 't', 'e', 's'};
			ASSERT_EQ(m_State.m_Recorder.Start(m_State.m_pStorage.get(), nullptr, "source.demo", "test", "map", SHA256_DIGEST{}, 0, "client", sizeof(aMap), aMap, nullptr, nullptr, nullptr), 0);
			auto pBuilder = CSnapshotBuilder::New();
			auto pBuffer = CSnapshotBuffer::New();
			for(int Tick : Ticks)
			{
				pBuilder->Init(false);
				CNetObj_Flag Flag{};
				Flag.m_X = Tick;
				ASSERT_TRUE(pBuilder->NewItem(NETOBJTYPE_FLAG, 0, Flag.AsSlice()));
				const int Size = pBuilder->Finish(*pBuffer);
				ASSERT_GT(Size, 0);
				m_State.m_Recorder.RecordSnapshot(Tick, pBuffer->AsSnapshot(), Size, true);
			}
			for(int Tick : Markers)
				ASSERT_TRUE(m_State.m_Recorder.AddDemoMarker(Tick));
			ASSERT_EQ(m_State.m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
		}
		void ExpectOutput(int First, int Last, const std::vector<int> &vMarkers = {}, const std::vector<int> &vSnapshots = {}, const std::vector<int> &vValues = {})
		{
			CSliceSnapshots Snapshots;
			CDemoPlayer Player(&*m_State.m_pDelta, &*m_State.m_pDeltaSixup, false, nullptr, CDemoPlayer::ETimeMode::UNCACHED);
			const int Loaded = Player.Load(m_State.m_pStorage.get(), nullptr, "target.demo", IStorage::TYPE_SAVE, false);
			EXPECT_EQ(Loaded, 0);
			if(Loaded == 0)
			{
				const auto &Info = Player.Info()->m_Info;
				EXPECT_EQ(Info.m_FirstTick, First);
				EXPECT_EQ(Info.m_LastTick, Last);
				EXPECT_EQ(Info.m_NumTimelineMarkers, static_cast<int>(vMarkers.size()));
				for(int i = 0; i < std::min(Info.m_NumTimelineMarkers, static_cast<int>(vMarkers.size())); ++i)
					EXPECT_EQ(Info.m_aTimelineMarkers[i], vMarkers[i]);
				if(!vSnapshots.empty())
				{
					Snapshots.m_pPlayer = &Player;
					Player.SetListener(&Snapshots);
				}
				const bool Playing = Player.Play();
				EXPECT_TRUE(Playing);
				if(Playing && !vSnapshots.empty())
				{
					Player.Update(false);
					EXPECT_EQ(Snapshots.m_vTicks, vSnapshots);
					EXPECT_EQ(Snapshots.m_vValues, vValues);
				}
			}
			Player.Stop();
		}
		void ExpectRejected(const std::vector<SDemoSliceSegment> &vSegments)
		{
			m_State.Write("target.demo", "previous");
			EXPECT_FALSE(Slice(vSegments));
			EXPECT_EQ(m_State.Read("target.demo"), "previous");
			EXPECT_EQ(m_Output.m_Creates, 0);
			EXPECT_TRUE(m_State.Exists("source.demo"));
		}
	};
}

TEST_F(DemoSliceSegmentsIntegration, EmptySelectionKeepsTheExistingWholeDemoFallback)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}, {170}));
	ASSERT_TRUE(Slice({}));
	ExpectOutput(100, 200, {170});
}

TEST_F(DemoSliceSegmentsIntegration, ReversedOnlySelectionKeepsTheExistingWholeDemoFallback)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}, {170}));
	ASSERT_TRUE(Slice({{200, 100}}));
	ExpectOutput(100, 200, {170});
}

TEST_F(DemoSliceSegmentsIntegration, ReversedEntryIsIgnoredWhenAnotherRangeIsUsable)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300}));
	ASSERT_TRUE(Slice({{300, 100}, {100, 200}}));
	ExpectOutput(100, 200);
}

TEST_F(DemoSliceSegmentsIntegration, UnsortedRangesKeepTheExistingChronologicalJoin)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400}, {150, 350}));
	const std::string Source = m_State.Read("source.demo");
	ASSERT_TRUE(Slice({{300, 400}, {100, 200}}));
	ExpectOutput(100, 301, {150, 251});
	EXPECT_EQ(m_State.Read("source.demo"), Source);
}

TEST_F(DemoSliceSegmentsIntegration, SortedRangesResumeAfterThePreviousReadReachedSourceEnd)
{
	ASSERT_NO_FATAL_FAILURE(CreateChangingTimeline({100, 200, 300, 400, 500}, {150, 350, 500}));
	ASSERT_TRUE(Slice({{100, 200}, {300, 400}}));
	ExpectOutput(100, 301, {150, 251}, {100, 200, 201, 301}, {100, 200, 300, 400});
}

TEST_F(DemoSliceSegmentsIntegration, ThreeSeparatedRangesEachResumeBeyondSeekWarmup)
{
	ASSERT_NO_FATAL_FAILURE(CreateChangingTimeline({100, 200, 300, 400, 500, 600}, {150, 350, 550}));
	ASSERT_TRUE(Slice({{100, 200}, {300, 400}, {500, 600}}));
	ExpectOutput(100, 402, {150, 251, 352}, {100, 200, 201, 301, 302, 402}, {100, 200, 300, 400, 500, 600});
}

TEST_F(DemoSliceSegmentsIntegration, AdjacentRangesKeepAllRecordedSnapshots)
{
	ASSERT_NO_FATAL_FAILURE(CreateChangingTimeline({100, 200, 300, 400}, {150, 350}));
	ASSERT_TRUE(Slice({{100, 200}, {201, 400}}));
	ExpectOutput(100, 400, {150, 350}, {100, 200, 300, 400}, {100, 200, 300, 400});
}

TEST_F(DemoSliceSegmentsIntegration, OverlappingRangesResumeWithTheirOriginalOutputOffsets)
{
	ASSERT_NO_FATAL_FAILURE(CreateChangingTimeline({100, 200, 300, 400}, {250, 350}));
	ASSERT_TRUE(Slice({{100, 300}, {200, 400}}));
	ExpectOutput(100, 501, {250, 351, 451}, {100, 200, 300, 301, 401, 501}, {100, 200, 300, 200, 300, 400});
}

TEST_F(DemoSliceSegmentsIntegration, RepeatedWideRangeKeepsSnapshotsBeyondSeekWarmup)
{
	ASSERT_NO_FATAL_FAILURE(CreateChangingTimeline({100, 200, 300, 400}, {150, 350}));
	ASSERT_TRUE(Slice({{100, 400}, {100, 400}}));
	ExpectOutput(100, 701, {150, 350, 451, 651}, {100, 200, 300, 400, 401, 501, 601, 701}, {100, 200, 300, 400, 100, 200, 300, 400});
}

TEST_F(DemoSliceSegmentsIntegration, RepeatedRangeRetainsBothCopiesAndRelocatesMarkers)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}, {170}));
	ASSERT_TRUE(Slice({{100, 200}, {100, 200}}));
	ExpectOutput(100, 301, {170, 271});
}

TEST_F(DemoSliceSegmentsIntegration, MarkerBetweenRecordedSnapshotsRemainsAtItsOriginalTick)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400}, {250}));
	ASSERT_TRUE(Slice({{175, 325}}));
	ExpectOutput(200, 300, {250});
}

TEST_F(DemoSliceSegmentsIntegration, MarkerBeforeTheFirstSparseOutputTickIsOmitted)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300}, {170}));
	ASSERT_TRUE(Slice({{150, 300}}));
	ExpectOutput(200, 300);
}

TEST_F(DemoSliceSegmentsIntegration, MarkerAfterTheLastSparseOutputTickIsOmitted)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300}, {250}));
	ASSERT_TRUE(Slice({{100, 275}}));
	ExpectOutput(100, 200);
}

TEST_F(DemoSliceSegmentsIntegration, OnlyMarkersInsideTheActualOutputBoundsRemain)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400}, {180, 250, 320}));
	ASSERT_TRUE(Slice({{175, 325}}));
	ExpectOutput(200, 300, {250});
}

TEST_F(DemoSliceSegmentsIntegration, MarkersAtTheActualOutputEndpointsAreKept)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400}, {200, 300}));
	ASSERT_TRUE(Slice({{175, 325}}));
	ExpectOutput(200, 300, {200, 300});
}

TEST_F(DemoSliceSegmentsIntegration, EmptyPrefixRetainsItsReservedDurationBeforeTheNextRange)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400}, {150, 350}));
	ASSERT_TRUE(Slice({{140, 160}, {300, 400}}));
	ExpectOutput(161, 261, {211});
}

TEST_F(DemoSliceSegmentsIntegration, ARangeWithoutAnySnapshotDoesNotReplaceTheTarget)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}));
	m_State.Write("target.demo", "previous");
	EXPECT_FALSE(Slice({{140, 160}}));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(DemoSliceSegmentsIntegration, ARangeWithOnlyOneRecordedTickDoesNotReplaceTheTarget)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}, {170}));
	m_State.Write("target.demo", "previous");
	EXPECT_FALSE(Slice({{150, 200}}));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_TRUE(m_State.Exists("source.demo"));
}

TEST_F(DemoSliceSegmentsIntegration, TwoRangesWithOneTickEachCanCompletePlaybackWarmup)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300, 400}, {170, 370}));
	ASSERT_TRUE(Slice({{150, 250}, {350, 450}}));
	ExpectOutput(200, 301, {271});
}

TEST_F(DemoSliceSegmentsIntegration, RejectedSingleTickCutCanBeRetriedWithAWiderRange)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300}, {170}));
	m_State.Write("target.demo", "previous");
	EXPECT_FALSE(Slice({{150, 200}}));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	ASSERT_TRUE(Slice({{150, 300}}));
	ExpectOutput(200, 300);
}

TEST_F(DemoSliceSegmentsIntegration, AWholeLegalSpanCanEndAtTheLastRepresentableTick)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({MIN_TICK, MAX_TICK - 1}));
	ASSERT_TRUE(Slice({{MIN_TICK, MAX_TICK - 1}}));
	ExpectOutput(MIN_TICK, MAX_TICK - 1);
}

TEST_F(DemoSliceSegmentsIntegration, TwoWholeLegalSpansAreRejectedBeforeCreatingAnOutput)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({MIN_TICK, MAX_TICK - 1}));
	ExpectRejected({{MIN_TICK, MAX_TICK - 1}, {MIN_TICK, MAX_TICK - 1}});
}

TEST_F(DemoSliceSegmentsIntegration, AJoinCrossingTheFormatLimitIsRejectedBeforeWriting)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({MAX_TICK - 2, MAX_TICK - 1}));
	ExpectRejected({{MAX_TICK - 2, MAX_TICK - 1}, {MAX_TICK - 2, MAX_TICK - 1}});
}

TEST_F(DemoSliceSegmentsIntegration, EndTickOutsideTheFormatIsRejectedBeforeWriting)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}));
	ExpectRejected({{100, MAX_TICK}});
}

TEST_F(DemoSliceSegmentsIntegration, MaximumIntegerStartCannotOverflowTheSeekIncrement)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}));
	ExpectRejected({{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}});
}

TEST_F(DemoSliceSegmentsIntegration, NegativeNonSentinelStartIsRejectedBeforeArithmetic)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}));
	ExpectRejected({{std::numeric_limits<int>::min(), 200}});
}

TEST_F(DemoSliceSegmentsIntegration, RejectedRequestCanBeRetriedThroughTheSameEditor)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}, {170}));
	const auto Source = m_State.Read("source.demo");
	ExpectRejected({{100, MAX_TICK}});
	ASSERT_TRUE(Slice({{100, 200}}));
	ExpectOutput(100, 200, {170});
	EXPECT_EQ(m_State.Read("source.demo"), Source);
}

TEST_F(DemoSliceSegmentsIntegration, SentinelEndpointsStillUseTheSourceBounds)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200, 300}, {150, 300}));
	ASSERT_TRUE(Slice({{-1, 200}, {300, -1}}));
	ExpectOutput(100, 201, {150, 201});
}

TEST_F(DemoSliceSegmentsIntegration, ALookbackBeforeTheSourceDoesNotShiftItsRecordedTicks)
{
	ASSERT_NO_FATAL_FAILURE(m_State.CreateTimeline({100, 200}, {170}));
	ASSERT_TRUE(Slice({{MIN_TICK, 200}}));
	ExpectOutput(100, 200, {170});
}
