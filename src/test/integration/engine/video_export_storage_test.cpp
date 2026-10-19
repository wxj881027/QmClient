#include <engine/client/video_export.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
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

	class CVideoExportStorage : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::filesystem::path m_Directory;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Directory = std::filesystem::absolute(m_Info.StoragePath());
		}

		std::string Path(const char *pName) const { return (m_Directory / pName).string(); }
	};
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
		pEnc->max_b_frames = 0;
		// 与生产 video.cpp 同构：mkv/封装器需要全局头（extradata 在 header 前可用）
		pEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		av_opt_set(pEnc->priv_data, "preset", "ultrafast", 0);
		av_opt_set_int(pEnc->priv_data, "crf", 51, 0);
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
		pStream->avg_frame_rate = {60, 1};

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

TEST(VideoExportTask, ProgressRoundTripPreservesDoneFlags)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::filesystem::path TempDir = std::filesystem::absolute(Info.StoragePath());
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
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::filesystem::path TempDir = std::filesystem::absolute(Info.StoragePath());
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
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::filesystem::path TempDir = std::filesystem::absolute(Info.StoragePath());
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

TEST(VideoExportTask, RangeChangeInvalidatesProgress)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::filesystem::path TempDir = std::filesystem::absolute(Info.StoragePath());
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
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::filesystem::path TempDir = std::filesystem::absolute(Info.StoragePath());
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

TEST(VideoExportTask, ResumeRestoresMissingDoneSegmentToPending)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const std::filesystem::path TempDir = std::filesystem::absolute(Info.StoragePath());
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

TEST_F(CVideoExportStorage, BlockedTemporaryFilePreservesPreviousCheckpoint)
{
	const std::string Progress = Path("checkpoint.progress");
	auto pTask = MakeTask("checkpoint", 0, 13000, 50);
	pTask->MarkSegmentDone(0);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	ASSERT_TRUE(std::filesystem::create_directory(Progress + ".tmp"));

	pTask->MarkSegmentDone(1);
	EXPECT_FALSE(pTask->SaveProgress(Progress.c_str()));
	auto pReloaded = MakeTask("checkpoint", 0, 13000, 50);
	ASSERT_TRUE(pReloaded->LoadProgress(Progress.c_str()));
	EXPECT_TRUE(pReloaded->Segments()[0].m_Done);
	EXPECT_FALSE(pReloaded->Segments()[1].m_Done);
}

TEST_F(CVideoExportStorage, ResumeRejectsChangedSizeEvenWhenVideoHeaderRemainsReadable)
{
	const std::string Segment = WriteMinimalVideoFile(m_Directory, "receipt_p001.mkv");
	ASSERT_FALSE(Segment.empty());
	const auto OriginalSize = std::filesystem::file_size(Segment);
	ASSERT_GT(OriginalSize, 1u);
	const std::string Progress = Path("receipt.progress");
	auto pTask = MakeTask("receipt", 0, 13000, 50);
	pTask->MarkSegmentDone(0);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	{
		std::ofstream Append(Progress, std::ios::app | std::ios::binary);
		Append << "segment_0_size=" << OriginalSize << '\n';
	}
	std::filesystem::resize_file(Segment, OriginalSize - 1);
	ASSERT_TRUE(CVideoExportTask::VerifySegmentFile(Segment.c_str()));

	auto pReloaded = MakeTask("receipt", 0, 13000, 50);
	ASSERT_TRUE(pReloaded->LoadProgress(Progress.c_str()));
	pReloaded->RestoreMissingDoneSegments([&](const char *) { return Segment.c_str(); });
	EXPECT_FALSE(pReloaded->Segments()[0].m_Done);
	EXPECT_EQ(pReloaded->NextPendingSegment(), 0);
}

namespace
{
	unsigned ShortProgressWrite(IOHANDLE File, const void *pData, unsigned Size)
	{
		return io_write(File, pData, Size / 2);
	}

	int FailProgressSync(IOHANDLE)
	{
		return 1;
	}

	int FailProgressClose(IOHANDLE File)
	{
		io_close(File);
		return 1;
	}

	int FailVideoClose(AVIOContext **ppContext)
	{
		avio_closep(ppContext);
		return AVERROR(EIO);
	}

	std::string ReadVideoTestFile(const std::string &Path)
	{
		std::ifstream Input(Path, std::ios::binary);
		return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
	}

	int VideoPacketCount(const std::string &Path)
	{
		AVFormatContext *pInput = nullptr;
		if(avformat_open_input(&pInput, Path.c_str(), nullptr, nullptr) < 0)
			return -1;
		AVPacket *pPacket = av_packet_alloc();
		if(!pPacket)
		{
			avformat_close_input(&pInput);
			return -1;
		}
		int Count = 0;
		int Result;
		while((Result = av_read_frame(pInput, pPacket)) >= 0)
		{
			if(pInput->streams[pPacket->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				++Count;
			av_packet_unref(pPacket);
		}
		av_packet_free(&pPacket);
		avformat_close_input(&pInput);
		return Result == AVERROR_EOF ? Count : -1;
	}
}

TEST_F(CVideoExportStorage, ShortWriteKeepsOldCheckpointAndAllowsRetry)
{
	const std::string Progress = Path("short.progress");
	auto pTask = MakeTask("short", 0, 13000, 50);
	pTask->MarkSegmentDone(0);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	const std::string Before = ReadVideoTestFile(Progress);
	pTask->MarkSegmentDone(1);
	SVideoExportIo Io;
	Io.m_pWrite = ShortProgressWrite;
	EXPECT_FALSE(pTask->SaveProgress(Progress.c_str(), &Io));
	EXPECT_EQ(ReadVideoTestFile(Progress), Before);
	EXPECT_FALSE(std::filesystem::exists(Progress + ".tmp"));
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	auto pReloaded = MakeTask("short", 0, 13000, 50);
	ASSERT_TRUE(pReloaded->LoadProgress(Progress.c_str()));
	EXPECT_TRUE(pReloaded->Segments()[1].m_Done);
}

TEST_F(CVideoExportStorage, SyncFailureKeepsOldCheckpoint)
{
	const std::string Progress = Path("sync.progress");
	auto pTask = MakeTask("sync", 0, 13000, 50);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	const std::string Before = ReadVideoTestFile(Progress);
	pTask->MarkSegmentDone(0);
	SVideoExportIo Io;
	Io.m_pSync = FailProgressSync;
	EXPECT_FALSE(pTask->SaveProgress(Progress.c_str(), &Io));
	EXPECT_EQ(ReadVideoTestFile(Progress), Before);
	EXPECT_FALSE(std::filesystem::exists(Progress + ".tmp"));
}

TEST_F(CVideoExportStorage, CloseFailureKeepsOldCheckpoint)
{
	const std::string Progress = Path("close.progress");
	auto pTask = MakeTask("close", 0, 13000, 50);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	const std::string Before = ReadVideoTestFile(Progress);
	pTask->MarkSegmentDone(0);
	SVideoExportIo Io;
	Io.m_pClose = FailProgressClose;
	EXPECT_FALSE(pTask->SaveProgress(Progress.c_str(), &Io));
	EXPECT_EQ(ReadVideoTestFile(Progress), Before);
	EXPECT_FALSE(std::filesystem::exists(Progress + ".tmp"));
}

TEST_F(CVideoExportStorage, FailedCheckpointCommitKeepsDestinationDirectory)
{
	const std::string Progress = Path("directory.progress");
	ASSERT_TRUE(std::filesystem::create_directory(Progress));
	auto pTask = MakeTask("directory", 0, 13000, 50);
	EXPECT_FALSE(pTask->SaveProgress(Progress.c_str()));
	EXPECT_TRUE(std::filesystem::is_directory(Progress));
	EXPECT_FALSE(std::filesystem::exists(Progress + ".tmp"));
}

TEST_F(CVideoExportStorage, CompletedFileSizeSurvivesCheckpointAndAcceptsUnchangedSegment)
{
	const std::string Segment = WriteMinimalVideoFile(m_Directory, "size_p001.mkv");
	ASSERT_FALSE(Segment.empty());
	const std::string Progress = Path("size.progress");
	auto pTask = MakeTask("size", 0, 13000, 50);
	ASSERT_TRUE(pTask->CompleteSegmentFile(0, Segment.c_str()));
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	auto pReloaded = MakeTask("size", 0, 13000, 50);
	ASSERT_TRUE(pReloaded->LoadProgress(Progress.c_str()));
	EXPECT_EQ(pReloaded->Segments()[0].m_FileSize, (int64_t)std::filesystem::file_size(Segment));
	pReloaded->RestoreMissingDoneSegments([&](const char *) { return Segment.c_str(); });
	EXPECT_TRUE(pReloaded->Segments()[0].m_Done);
	EXPECT_EQ(pReloaded->NextPendingSegment(), 1);
}

TEST_F(CVideoExportStorage, MissingFileCannotBeCompleted)
{
	auto pTask = MakeTask("missing", 0, 13000, 50);
	EXPECT_FALSE(pTask->CompleteSegmentFile(0, Path("missing.mkv").c_str()));
	EXPECT_FALSE(pTask->Segments()[0].m_Done);
	EXPECT_EQ(pTask->Segments()[0].m_FileSize, -1);
}

TEST_F(CVideoExportStorage, LegacyCheckpointAcquiresSizeWhenExistingSegmentIsRestored)
{
	const std::string Segment = WriteMinimalVideoFile(m_Directory, "legacy_p001.mkv");
	ASSERT_FALSE(Segment.empty());
	const std::string Progress = Path("legacy.progress");
	auto pTask = MakeTask("legacy", 0, 13000, 50);
	pTask->MarkSegmentDone(0);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	auto pReloaded = MakeTask("legacy", 0, 13000, 50);
	ASSERT_TRUE(pReloaded->LoadProgress(Progress.c_str()));
	EXPECT_EQ(pReloaded->Segments()[0].m_FileSize, -1);
	pReloaded->RestoreMissingDoneSegments([&](const char *) { return Segment.c_str(); });
	EXPECT_TRUE(pReloaded->Segments()[0].m_Done);
	EXPECT_EQ(pReloaded->Segments()[0].m_FileSize, (int64_t)std::filesystem::file_size(Segment));
	ASSERT_TRUE(pReloaded->SaveProgress(Progress.c_str()));
	auto pAgain = MakeTask("legacy", 0, 13000, 50);
	ASSERT_TRUE(pAgain->LoadProgress(Progress.c_str()));
	EXPECT_EQ(pAgain->Segments()[0].m_FileSize, pReloaded->Segments()[0].m_FileSize);
}

TEST_F(CVideoExportStorage, FailedReloadClearsPreviouslyLoadedCompletion)
{
	const std::string Progress = Path("reload.progress");
	auto pTask = MakeTask("reload", 0, 13000, 50);
	pTask->MarkSegmentDone(0);
	ASSERT_TRUE(pTask->SaveProgress(Progress.c_str()));
	ASSERT_TRUE(pTask->LoadProgress(Progress.c_str()));
	ASSERT_TRUE(pTask->Segments()[0].m_Done);
	EXPECT_FALSE(pTask->LoadProgress(Path("not-found.progress").c_str()));
	EXPECT_EQ(pTask->NextPendingSegment(), 0);
}

TEST_F(CVideoExportStorage, ConcatCloseFailureKeepsOldOutputAndAllowsRetry)
{
	const std::string Segment = WriteMinimalVideoFile(m_Directory, "input.mkv");
	const std::string Output = WriteMinimalVideoFile(m_Directory, "output.mkv");
	ASSERT_FALSE(Segment.empty());
	ASSERT_FALSE(Output.empty());
	const std::string Before = ReadVideoTestFile(Output);
	SVideoExportIo Io;
	Io.m_pCloseVideo = FailVideoClose;
	EXPECT_FALSE(ConcatVideoFiles({Segment, Segment}, Output.c_str(), "matroska", &Io));
	EXPECT_EQ(ReadVideoTestFile(Output), Before);
	EXPECT_FALSE(std::filesystem::exists(Output + ".part"));
	ASSERT_TRUE(ConcatVideoFiles({Segment, Segment}, Output.c_str(), "matroska"));
	EXPECT_EQ(VideoPacketCount(Output), 4);
}

TEST_F(CVideoExportStorage, ConcatMissingLaterSegmentKeepsOldOutput)
{
	const std::string Segment = WriteMinimalVideoFile(m_Directory, "input.mkv");
	const std::string Output = WriteMinimalVideoFile(m_Directory, "output.mkv");
	ASSERT_FALSE(Segment.empty());
	ASSERT_FALSE(Output.empty());
	const std::string Before = ReadVideoTestFile(Output);
	EXPECT_FALSE(ConcatVideoFiles({Segment, Path("not-found.mkv")}, Output.c_str(), "matroska"));
	EXPECT_EQ(ReadVideoTestFile(Output), Before);
	EXPECT_FALSE(std::filesystem::exists(Output + ".part"));
}

TEST_F(CVideoExportStorage, FailedConcatCommitKeepsDestinationDirectory)
{
	const std::string Segment = WriteMinimalVideoFile(m_Directory, "input.mkv");
	ASSERT_FALSE(Segment.empty());
	const std::string Output = Path("directory.mkv");
	ASSERT_TRUE(std::filesystem::create_directory(Output));
	EXPECT_FALSE(ConcatVideoFiles({Segment}, Output.c_str(), "matroska"));
	EXPECT_TRUE(std::filesystem::is_directory(Output));
	EXPECT_FALSE(std::filesystem::exists(Output + ".part"));
}

TEST_F(CVideoExportStorage, UncommittedRecordingPreservesPreviousOutput)
{
	const std::string Target = Path("recording.mp4");
	std::ofstream(Target) << "previous-video";
	{
		CVideoExportOutput Output(Target.c_str(), ".recording");
		std::ofstream(Output.TemporaryPath()) << "incomplete-video";
		Output.Opened();
	}
	std::ifstream Previous(Target);
	EXPECT_EQ(std::string(std::istreambuf_iterator<char>(Previous), {}), "previous-video");
	EXPECT_FALSE(std::filesystem::exists(Target + ".recording"));
}

TEST_F(CVideoExportStorage, SuccessfulRecordingReplacesPreviousOutput)
{
	const std::string Target = Path("recording.mp4");
	std::ofstream(Target) << "previous-video";
	CVideoExportOutput Output(Target.c_str(), ".recording");
	std::ofstream(Output.TemporaryPath()) << "complete-video";
	Output.Opened();
	ASSERT_TRUE(Output.Commit());
	std::ifstream Current(Target);
	EXPECT_EQ(std::string(std::istreambuf_iterator<char>(Current), {}), "complete-video");
	EXPECT_FALSE(std::filesystem::exists(Target + ".recording"));
}

TEST_F(CVideoExportStorage, UnopenedTransactionDoesNotRemoveUnownedTemporaryFile)
{
	const std::string Target = Path("recording.mp4");
	std::ofstream(Target + ".recording") << "unowned";
	{
		CVideoExportOutput Output(Target.c_str(), ".recording");
		EXPECT_FALSE(Output.Commit());
	}
	EXPECT_TRUE(std::filesystem::exists(Target + ".recording"));
}

TEST_F(CVideoExportStorage, DemoDisplayChangesInvalidateCompletedSegments)
{
	CConfig Config{};
	SVideoDisplaySnapshot Display{};
	const std::string Progress = Path("display.progress");
	auto pOriginal = MakeTask("display", 0, 13000, 50, VideoExportConfigParams(Config, Display).c_str());
	pOriginal->MarkSegmentDone(0);
	ASSERT_TRUE(pOriginal->SaveProgress(Progress.c_str()));
	auto pSame = MakeTask("display", 0, 13000, 50, VideoExportConfigParams(Config, Display).c_str());
	ASSERT_TRUE(pSame->LoadProgress(Progress.c_str()));
	EXPECT_TRUE(pSame->Segments()[0].m_Done);
	int SVideoDisplaySnapshot::*apMembers[] = {
		&SVideoDisplaySnapshot::m_Direction, &SVideoDisplaySnapshot::m_StrongWeak,
		&SVideoDisplaySnapshot::m_StrongWeakScope, &SVideoDisplaySnapshot::m_StrongWeakSize};
	for(size_t i = 0; i < std::size(apMembers); ++i)
	{
		SCOPED_TRACE(i);
		SVideoDisplaySnapshot Changed = Display;
		// 每次只改一个实际画面参数，验证旧的完成标记不会漏过指纹校验。
		const auto pMember = apMembers[i];
		Changed.*pMember = 1;
		auto pResume = MakeTask("display", 0, 13000, 50, VideoExportConfigParams(Config, Changed).c_str());
		EXPECT_FALSE(pResume->LoadProgress(Progress.c_str()));
		EXPECT_FALSE(pResume->Segments()[0].m_Done);
	}
	{
		SVideoDisplaySnapshot Changed = Display;
		Changed.m_Hud = true;
		auto pResume = MakeTask("display", 0, 13000, 50, VideoExportConfigParams(Config, Changed).c_str());
		EXPECT_FALSE(pResume->LoadProgress(Progress.c_str()));
		EXPECT_FALSE(pResume->Segments()[0].m_Done);
	}
	{
		SVideoDisplaySnapshot Changed = Display;
		Changed.m_Chat = true;
		auto pResume = MakeTask("display", 0, 13000, 50, VideoExportConfigParams(Config, Changed).c_str());
		EXPECT_FALSE(pResume->LoadProgress(Progress.c_str()));
		EXPECT_FALSE(pResume->Segments()[0].m_Done);
	}
	{
		SVideoDisplaySnapshot Changed = Display;
		Changed.m_RenderDemoOnly = true;
		auto pResume = MakeTask("display", 0, 13000, 50, VideoExportConfigParams(Config, Changed).c_str());
		EXPECT_FALSE(pResume->LoadProgress(Progress.c_str()));
		EXPECT_FALSE(pResume->Segments()[0].m_Done);
	}
}

TEST_F(CVideoExportStorage, WarmupChangeInvalidatesCompletedSegments)
{
	const std::string Progress = Path("warmup.progress");
	auto pOriginal = MakeTask("warmup", 0, 13000, 50);
	pOriginal->MarkSegmentDone(0);
	ASSERT_TRUE(pOriginal->SaveProgress(Progress.c_str()));
	CVideoExportTask Changed("warmup", "demos/test.demo", 0, 13000, 50, 60, 1280, 720, 18, 5, 200);
	Changed.ComputeFingerprint(12345, 1700000000, "");
	EXPECT_FALSE(Changed.LoadProgress(Progress.c_str()));
	EXPECT_FALSE(Changed.Segments()[0].m_Done);
}
