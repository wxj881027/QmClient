#include <engine/client/video_export.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace
{
	// 构造带指纹的任务；段长 0 表示单段覆盖全范围。
	// pExtraParams 对应生产调用方传入的编码器/容器/输出尺寸等附加参数。
	std::unique_ptr<CVideoExportTask> MakeTask(const char *pName, int FirstTick, int LastTick, int SegmentSeconds, const char *pExtraParams = "")
	{
		auto pTask = std::make_unique<CVideoExportTask>(pName, "demos/test.demo", FirstTick, LastTick, SegmentSeconds, 60, 1280, 720, 18, 5, 100);
		pTask->ComputeFingerprint(12345, 1700000000, pExtraParams);
		return pTask;
	}

}

TEST(VideoExportTask, PlanSegmentsCoversWholeRangeWithoutGaps)
{
	const auto vSegments = CVideoExportTask::PlanSegments(100, 250, 10); // 10s = 500 ticks → 单段
	ASSERT_EQ(vSegments.size(), 1u);
	EXPECT_EQ(vSegments[0].m_StartTick, 100);
	EXPECT_EQ(vSegments[0].m_EndTick, 250);
}

TEST(VideoExportTask, PlanSegmentsSplitsIntoFixedSizedPieces)
{
	const auto vSegments = CVideoExportTask::PlanSegments(0, 13000, 50); // 50s = 2500 ticks → 6 段
	ASSERT_EQ(vSegments.size(), 6u);
	for(size_t i = 0; i < vSegments.size(); ++i)
	{
		EXPECT_EQ(vSegments[i].m_StartTick, (int)(i * 2500));
	}
	EXPECT_EQ(vSegments.front().m_StartTick, 0);
	EXPECT_EQ(vSegments.back().m_EndTick, 13000); // 末段收尾对齐 LastTick
	for(size_t i = 1; i < vSegments.size(); ++i)
	{
		EXPECT_EQ(vSegments[i].m_StartTick, vSegments[i - 1].m_EndTick); // 无缝衔接
	}
}

TEST(VideoExportTask, PlanSegmentsRejectsEmptyRange)
{
	EXPECT_TRUE(CVideoExportTask::PlanSegments(10, 10, 60).empty());
	EXPECT_TRUE(CVideoExportTask::PlanSegments(20, 10, 60).empty());
}

TEST(VideoExportTask, RecordingFailureFlagResetsPerSegment)
{
	// 录制失败标记必须按段重置：否则前一段的错误会让后续所有段都被判为失败而反复重渲。
	auto pTask = MakeTask("recfail", 0, 13000, 50);
	pTask->BeginSegment(0);
	EXPECT_FALSE(pTask->SegmentRecordingFailed());
	pTask->MarkSegmentRecordingFailed();
	EXPECT_TRUE(pTask->SegmentRecordingFailed());

	pTask->BeginSegment(1);
	EXPECT_FALSE(pTask->SegmentRecordingFailed());
}

TEST(VideoExportTask, StateMachineDrivesRecordWindowPerSegment)
{
	auto pTask = MakeTask("state", 0, 13000, 50);
	// 段 1 = [2500, 5000)：预热态在段起点前不触发
	pTask->BeginSegment(1);
	EXPECT_EQ(pTask->State(), CVideoExportTask::EState::WARMING_UP);
	EXPECT_EQ(pTask->UpdatePlayback(1000), CVideoExportTask::ACT_NONE); // 预热中
	EXPECT_EQ(pTask->UpdatePlayback(2499), CVideoExportTask::ACT_NONE); // 未到段起点
	EXPECT_EQ(pTask->UpdatePlayback(2500), CVideoExportTask::ACT_START_RECORD); // 段起点
	pTask->OnRecordingStarted();
	EXPECT_EQ(pTask->State(), CVideoExportTask::EState::RECORDING);
	EXPECT_EQ(pTask->UpdatePlayback(4999), CVideoExportTask::ACT_NONE);
	EXPECT_EQ(pTask->UpdatePlayback(5000), CVideoExportTask::ACT_STOP_RECORD); // 段末
	pTask->MarkSegmentDone(1);
	pTask->OnRecordingStopped();
	// 推进到最靠前的未完成段：段 0 仍未渲染，回到段 0（续渲语义）
	EXPECT_EQ(pTask->State(), CVideoExportTask::EState::WARMING_UP);
	EXPECT_EQ(pTask->CurrentSegment(), 0);
}

TEST(VideoExportTask, AllDoneWhenEverySegmentMarked)
{
	auto pTask = MakeTask("alldone", 0, 13000, 50);
	EXPECT_FALSE(pTask->AllDone());
	for(int i = 0; i < (int)pTask->Segments().size(); ++i)
		pTask->MarkSegmentDone(i);
	EXPECT_TRUE(pTask->AllDone());
	EXPECT_EQ(pTask->NextPendingSegment(), -1);
}

TEST(VideoExportTask, EndOnlyRangeStartsAtDemoBeginning)
{
	int First = 100, Last = 1100;
	ASSERT_TRUE(CVideoExportTask::HasRange(-1.0f, 4.0f));
	ASSERT_TRUE(CVideoExportTask::ResolveRange(First, Last, -1.0f, 4.0f));
	EXPECT_EQ(First, 100);
	EXPECT_EQ(Last, 300);
}

TEST(VideoExportTask, StartOnlyRangeEndsAtDemoEnd)
{
	int First = 100, Last = 1100;
	ASSERT_TRUE(CVideoExportTask::ResolveRange(First, Last, 2.0f, -1.0f));
	EXPECT_EQ(First, 200);
	EXPECT_EQ(Last, 1100);
}

TEST(VideoExportTask, UnspecifiedRangePreservesWholeDemo)
{
	int First = 100, Last = 1100;
	EXPECT_FALSE(CVideoExportTask::HasRange(-1.0f, -1.0f));
	ASSERT_TRUE(CVideoExportTask::ResolveRange(First, Last, -1.0f, -1.0f));
	EXPECT_EQ(First, 100);
	EXPECT_EQ(Last, 1100);
}

TEST(VideoExportTask, LargeFiniteEndClampsBeforeIntegerConversion)
{
	int First = 100, Last = 1100;
	ASSERT_TRUE(CVideoExportTask::ResolveRange(First, Last, 2.0f, std::numeric_limits<float>::max()));
	EXPECT_EQ(First, 200);
	EXPECT_EQ(Last, 1100);
}

TEST(VideoExportTask, EmptyOrInvertedRangeDoesNotChangeTicks)
{
	const float aaRanges[][2] = {{4.0f, 2.0f}, {2.0f, 2.0f}, {-1.0f, 0.0f}, {100.0f, -1.0f}, {1.001f, 1.002f}};
	for(const auto &aRange : aaRanges)
	{
		SCOPED_TRACE(::testing::Message() << aRange[0] << ", " << aRange[1]);
		int First = 100, Last = 1100;
		EXPECT_FALSE(CVideoExportTask::ResolveRange(First, Last, aRange[0], aRange[1]));
		EXPECT_EQ(First, 100);
		EXPECT_EQ(Last, 1100);
	}
}

TEST(VideoExportTask, NonFiniteEndpointsAreRejectedAsRangeRequests)
{
	for(float Invalid : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		for(bool InvalidStart : {false, true})
		{
			const float Start = InvalidStart ? Invalid : -1.0f;
			const float End = InvalidStart ? -1.0f : Invalid;
			int First = 100, Last = 1100;
			EXPECT_TRUE(CVideoExportTask::HasRange(Start, End));
			EXPECT_FALSE(CVideoExportTask::ResolveRange(First, Last, Start, End));
			EXPECT_EQ(First, 100);
			EXPECT_EQ(Last, 1100);
		}
	}
}

TEST(VideoExportTask, RangeSupportsWideDemoTickSpan)
{
	int First = std::numeric_limits<int>::min(), Last = std::numeric_limits<int>::max();
	ASSERT_TRUE(CVideoExportTask::ResolveRange(First, Last, 2.0f, -1.0f));
	EXPECT_EQ(First, std::numeric_limits<int>::min() + 100);
	EXPECT_EQ(Last, std::numeric_limits<int>::max());
}

TEST(VideoExportTask, FractionalSecondsPreserveTickConversionPrecision)
{
	int First = 100, Last = 1100;
	ASSERT_TRUE(CVideoExportTask::ResolveRange(First, Last, 0.7f, 1.4f));
	EXPECT_EQ(First, 135);
	EXPECT_EQ(Last, 170);
}

TEST(VideoExportTask, EmptyOrInvertedDemoCannotResolveRange)
{
	for(int Last : {100, 99})
	{
		int First = 100;
		const int OriginalLast = Last;
		EXPECT_FALSE(CVideoExportTask::ResolveRange(First, Last, -1.0f, -1.0f));
		EXPECT_EQ(First, 100);
		EXPECT_EQ(Last, OriginalLast);
	}
}

TEST(VideoExportTask, SegmentPlanningAvoidsTickAndDurationOverflow)
{
	const int End = std::numeric_limits<int>::max();
	const auto vSegments = CVideoExportTask::PlanSegments(End - 125, End, 2);
	ASSERT_EQ(vSegments.size(), 2u);
	EXPECT_EQ(vSegments[0].m_StartTick, End - 125);
	EXPECT_EQ(vSegments[0].m_EndTick, End - 25);
	EXPECT_EQ(vSegments[1].m_StartTick, End - 25);
	EXPECT_EQ(vSegments[1].m_EndTick, End);
	const auto vLong = CVideoExportTask::PlanSegments(0, End, End);
	ASSERT_EQ(vLong.size(), 1u);
	EXPECT_EQ(vLong[0].m_EndTick, End);
}
