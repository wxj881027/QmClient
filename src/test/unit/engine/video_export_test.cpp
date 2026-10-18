#include <engine/client/video_export.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

	std::string ProgressFilePath(const std::filesystem::path &Dir, const char *pName)
	{
		return (Dir / (std::string(pName) + ".progress")).string();
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

TEST(VideoExportTask, ProgressRoundTripPreservesDoneFlags)
{
	std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "qm_video_export_test";
	std::filesystem::create_directories(TempDir);
	const std::string Path = ProgressFilePath(TempDir, "roundtrip");

	auto pTask = MakeTask("roundtrip", 0, 13000, 50);
	ASSERT_EQ(pTask->Segments().size(), 6u);
	pTask->MarkSegmentDone(0);
	pTask->MarkSegmentDone(2);
	ASSERT_TRUE(pTask->SaveProgress(Path.c_str()));

	auto pLoaded = MakeTask("roundtrip", 0, 13000, 50);
	EXPECT_TRUE(pLoaded->LoadProgress(Path.c_str()));
	EXPECT_TRUE(pLoaded->Segments()[0].m_Done);
	EXPECT_FALSE(pLoaded->Segments()[1].m_Done);
	EXPECT_TRUE(pLoaded->Segments()[2].m_Done);
	EXPECT_EQ(pLoaded->NextPendingSegment(), 1);

	std::filesystem::remove(Path);
}

TEST(VideoExportTask, CorruptProgressResetsAllSegments)
{
	std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "qm_video_export_test";
	std::filesystem::create_directories(TempDir);
	const std::string Path = ProgressFilePath(TempDir, "corrupt");

	auto pTask = MakeTask("corrupt", 0, 13000, 50);
	{
		std::ofstream Out(Path, std::ios::binary);
		Out << "not a progress file\nrandom=binary\x01\x02\n";
	}
	EXPECT_FALSE(pTask->LoadProgress(Path.c_str()));
	for(const auto &Segment : pTask->Segments())
		EXPECT_FALSE(Segment.m_Done);
	EXPECT_EQ(pTask->NextPendingSegment(), 0);

	std::filesystem::remove(Path);
}

TEST(VideoExportTask, FingerprintMismatchIgnoresProgress)
{
	std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "qm_video_export_test";
	std::filesystem::create_directories(TempDir);
	const std::string Path = ProgressFilePath(TempDir, "fingerprint");

	auto pSaved = MakeTask("fingerprint", 0, 13000, 50);
	pSaved->MarkSegmentDone(0);
	pSaved->MarkSegmentDone(1);
	ASSERT_TRUE(pSaved->SaveProgress(Path.c_str()));

	// 不同参数（段长变化 → 指纹不同且段数不同）：进度必须整体失效
	auto pOther = std::make_unique<CVideoExportTask>("fingerprint", "demos/test.demo", 0, 13000, 60, 60, 1280, 720, 18, 5, 100);
	pOther->ComputeFingerprint(12345, 1700000000, "");
	EXPECT_FALSE(pOther->LoadProgress(Path.c_str()));
	EXPECT_EQ(pOther->NextPendingSegment(), 0);

	std::filesystem::remove(Path);
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

TEST(VideoExportTask, RangeChangeInvalidatesProgress)
{
	std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "qm_video_export_test";
	std::filesystem::create_directories(TempDir);
	const std::string Path = ProgressFilePath(TempDir, "rangechange");

	// 同一段长、同一 demo，仅选区的起止 tick 不同：旧段属于另一段内容，必须重渲。
	auto pSaved = MakeTask("rangechange", 0, 13000, 50);
	pSaved->MarkSegmentDone(0);
	ASSERT_TRUE(pSaved->SaveProgress(Path.c_str()));

	auto pOtherRange = MakeTask("rangechange", 3000, 13000, 50);
	EXPECT_FALSE(pOtherRange->LoadProgress(Path.c_str()));
	EXPECT_EQ(pOtherRange->NextPendingSegment(), 0);

	std::filesystem::remove(Path);
}

TEST(VideoExportTask, ExportSettingsChangeInvalidatesProgress)
{
	std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "qm_video_export_test";
	std::filesystem::create_directories(TempDir);
	const std::string Path = ProgressFilePath(TempDir, "settingchange");

	// 相同 demo/段长/选区，仅编码器与输出尺寸不同：旧段参数不兼容，不能复用。
	auto pSaved = MakeTask("settingchange", 0, 13000, 50, "|18|60|mp4|0x0|1");
	ASSERT_TRUE(pSaved->SaveProgress(Path.c_str()));

	auto pOtherEncoder = MakeTask("settingchange", 0, 13000, 50, "h264_nvenc|18|60|mp4|0x0|1");
	EXPECT_FALSE(pOtherEncoder->LoadProgress(Path.c_str()));

	auto pOtherSize = MakeTask("settingchange", 0, 13000, 50, "|18|60|mp4|1920x1080|1");
	EXPECT_FALSE(pOtherSize->LoadProgress(Path.c_str()));

	// 参数完全一致时进度仍可用，避免指纹收紧导致续渲失效。
	auto pSame = MakeTask("settingchange", 0, 13000, 50, "|18|60|mp4|0x0|1");
	pSaved->MarkSegmentDone(0);
	ASSERT_TRUE(pSaved->SaveProgress(Path.c_str()));
	EXPECT_TRUE(pSame->LoadProgress(Path.c_str()));

	std::filesystem::remove(Path);
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
