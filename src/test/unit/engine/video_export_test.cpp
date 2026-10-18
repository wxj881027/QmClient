#include <engine/client/video_export.h>

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

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

namespace
{
	// 合成一段最小但真实的 mkv：用裁剪版 FFmpeg 自带的 libx264 编码 2 帧 64x64 黑帧，
	// 写入 matroska。这是生产链路（编码器 → 封装器）的真实最小重放，产物天然满足
	// VerifySegmentFile 的"可打开且含视频流"判定；不复制生产校验逻辑。
	std::string WriteMinimalVideoFile(const std::filesystem::path &Dir, const char *pName)
	{
		const std::string Path = (Dir / pName).string();
		const AVCodec *pCodec = avcodec_find_encoder_by_name("libx264");
		if(!pCodec)
		{
			EXPECT_FALSE(true) << "libx264 missing from bundled FFmpeg";
			return {};
		}
		AVCodecContext *pEnc = avcodec_alloc_context3(pCodec);
		if(!pEnc)
			return {};
		pEnc->width = 64;
		pEnc->height = 64;
		pEnc->time_base = {1, 60};
		pEnc->framerate = {60, 1};
		pEnc->pix_fmt = AV_PIX_FMT_YUV420P;
		pEnc->gop_size = 60;
		// 与生产 video.cpp 同构：mkv/封装器需要全局头（extradata 在 header 前可用）
		pEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		char aX264Params[] = "preset=ultrafast:crf=51";
		av_opt_set(pEnc->priv_data, "x264-params", aX264Params, 0);
		if(avcodec_open2(pEnc, pCodec, nullptr) < 0)
		{
			EXPECT_FALSE(true) << "avcodec_open2(libx264) failed";
			avcodec_free_context(&pEnc);
			return {};
		}

		AVFormatContext *pContext = nullptr;
		if(avformat_alloc_output_context2(&pContext, nullptr, "matroska", Path.c_str()) < 0)
		{
			EXPECT_FALSE(true) << "alloc output context failed: " << Path;
			avcodec_free_context(&pEnc);
			return {};
		}
		AVStream *pStream = avformat_new_stream(pContext, nullptr);
		if(!pStream || avcodec_parameters_from_context(pStream->codecpar, pEnc) < 0)
		{
			EXPECT_FALSE(true) << "stream setup failed";
			avformat_free_context(pContext);
			avcodec_free_context(&pEnc);
			return {};
		}
		pStream->time_base = {1, 60};

		if(!(pContext->oformat->flags & AVFMT_NOFILE) && avio_open(&pContext->pb, Path.c_str(), AVIO_FLAG_WRITE) < 0)
		{
			EXPECT_FALSE(true) << "avio_open failed: " << Path;
			avformat_free_context(pContext);
			avcodec_free_context(&pEnc);
			return {};
		}
		if(avformat_write_header(pContext, nullptr) < 0)
		{
			EXPECT_FALSE(true) << "write_header failed";
			avio_closep(&pContext->pb);
			avformat_free_context(pContext);
			avcodec_free_context(&pEnc);
			return {};
		}

		bool Encoded = true;
		for(int i = 0; Encoded && i < 2; ++i)
		{
			AVFrame *pFrame = av_frame_alloc();
			pFrame->format = AV_PIX_FMT_YUV420P;
			pFrame->width = 64;
			pFrame->height = 64;
			if(av_frame_get_buffer(pFrame, 0) < 0 || av_frame_make_writable(pFrame) < 0)
			{
				Encoded = false;
			}
			else
			{
				memset(pFrame->data[0], 16, pFrame->linesize[0] * 64);
				memset(pFrame->data[1], 128, pFrame->linesize[1] * 32);
				memset(pFrame->data[2], 128, pFrame->linesize[2] * 32);
				pFrame->pts = i;
				if(avcodec_send_frame(pEnc, pFrame) < 0)
					Encoded = false;
			}
			av_frame_free(&pFrame);
			if(Encoded)
			{
				AVPacket *pPacket = av_packet_alloc();
				while(Encoded && avcodec_receive_packet(pEnc, pPacket) >= 0)
				{
					av_packet_rescale_ts(pPacket, pEnc->time_base, pStream->time_base);
					pPacket->stream_index = pStream->index;
					if(av_interleaved_write_frame(pContext, pPacket) < 0)
						Encoded = false;
				}
				av_packet_free(&pPacket);
			}
		}
		// 排空编码器收尾，与生产 FinishFrames 同构
		if(Encoded && avcodec_send_frame(pEnc, nullptr) >= 0)
		{
			AVPacket *pPacket = av_packet_alloc();
			while(avcodec_receive_packet(pEnc, pPacket) >= 0)
			{
				av_packet_rescale_ts(pPacket, pEnc->time_base, pStream->time_base);
				pPacket->stream_index = pStream->index;
				if(av_interleaved_write_frame(pContext, pPacket) < 0)
					Encoded = false;
			}
			av_packet_free(&pPacket);
		}
		Encoded = Encoded && av_write_trailer(pContext) >= 0;

		if(!(pContext->oformat->flags & AVFMT_NOFILE))
			avio_closep(&pContext->pb);
		avformat_free_context(pContext);
		avcodec_free_context(&pEnc);
		return Encoded ? Path : std::string{};
	}
} // namespace

TEST(VideoExportTask, ResumeRestoresMissingDoneSegmentToPending)
{
	std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "qm_video_export_test";
	std::filesystem::create_directories(TempDir);
	const std::string Path = ProgressFilePath(TempDir, "resume_missing");

	auto pSaved = MakeTask("resume_missing", 0, 13000, 50);
	pSaved->MarkSegmentDone(0);
	pSaved->MarkSegmentDone(1);
	ASSERT_TRUE(pSaved->SaveProgress(Path.c_str()));

	// 段 0 对应文件 resume_missing_p001.mkv（段名 1 起）真实存在且可被生产校验打开；
	// 段 1 对应文件缺失：续渲必须只把段 1 置回待渲染。
	const std::string Segment0 = WriteMinimalVideoFile(TempDir, "resume_missing_p001.mkv");
	ASSERT_FALSE(Segment0.empty());
	ASSERT_TRUE(CVideoExportTask::VerifySegmentFile(Segment0.c_str())) << "fixture must satisfy the production completeness check";

	// 生产回调语义：段逻辑路径（videos/<name>_pNNN，无扩展名）→ 定位为绝对路径；
	// 文件不存在返回空指针。容器与扩展名映射由调用方决定，测试映射到 .mkv fixture。
	const auto Resolve = [&](const char *pLogical) -> const char * {
		std::string Logical = pLogical;
		const auto Slash = Logical.rfind('/');
		const std::string Base = Slash == std::string::npos ? Logical : Logical.substr(Slash + 1);
		static std::string s_Whole;
		s_Whole = (TempDir / (Base + ".mkv")).string();
		if(!std::filesystem::exists(s_Whole))
			return nullptr;
		return s_Whole.c_str();
	};

	auto pTask = MakeTask("resume_missing", 0, 13000, 50);
	ASSERT_TRUE(pTask->LoadProgress(Path.c_str()));
	pTask->RestoreMissingDoneSegments(Resolve);
	EXPECT_TRUE(pTask->Segments()[0].m_Done); // 文件完好的段保持完成
	EXPECT_FALSE(pTask->Segments()[1].m_Done); // 缺失文件段被置回待渲染
	EXPECT_EQ(pTask->NextPendingSegment(), 1);

	std::filesystem::remove(Path);
	std::filesystem::remove(Segment0);
}
