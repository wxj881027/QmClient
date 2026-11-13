#include <base/logger.h>
#include <base/str.h>

#include <game/client/components/qmclient/screenshot_clipboard.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	class CCaptureGraphics : public CIconBenchmarkGraphics
	{
	public:
		int m_Submissions = 0;
		bool m_NullFilename = false;
		FScreenshotCallback m_Callback;
		FScreenshotProcessor m_Processor;

		void TakeScreenshot(const char *pFilename, FScreenshotCallback Callback, FScreenshotProcessor Processor) override
		{
			++m_Submissions;
			m_NullFilename = pFilename == nullptr;
			m_Callback = std::move(Callback);
			m_Processor = std::move(Processor);
		}
	};

	// 只替换GameClient的处理器工厂边界，保留真实string/function堆申请。
	struct CCaptureSource
	{
		int m_FactoryCalls = 0;
		int m_ProcessorCalls = 0;
		int m_TakenCalls = 0;
		std::string m_Map = "capture-map-" + std::string(192, 'm');
		std::string m_DeliveredPath;
		CImageInfo m_Received;

		auto OnTaken()
		{
			return [this, Payload = std::array<unsigned char, 128>{}](CImageInfo &&Image, const char *pPath) {
				m_TakenCalls += 1 + Payload[0];
				m_Received = std::move(Image);
				m_DeliveredPath = pPath;
			};
		}

		auto ProcessorFactory()
		{
			return [this]() -> IGraphics::FScreenshotProcessor {
				++m_FactoryCalls;
				std::string Map = m_Map;
				return [this, Map = std::move(Map), Payload = std::array<unsigned char, 128>{}](CImageInfo &, std::string &Comment) {
					m_ProcessorCalls += 1 + Payload[0];
					Comment = Map;
					return true;
				};
			};
		}

		bool Request(IGraphics &Graphics, bool Clipboard = false)
		{
			return qm_screenshot_clipboard::RequestCapture(Graphics, Clipboard, OnTaken(), ProcessorFactory());
		}
	};

	class CCaptureLogger : public ILogger
	{
	public:
		int m_ClipboardWarnings = 0;
		void Log(const CLogMessage *pMessage) override
		{
			if(str_comp(pMessage->m_aSystem, "screenshot") == 0)
				++m_ClipboardWarnings;
		}
	};
}

TEST(QmScreenshotRequest, EveryNativePreparationFailureKeepsThePendingCallbackAndCanRetry)
{
	std::size_t Allocations;
	bool Prepared;
	{
		CCaptureGraphics Graphics;
		CCaptureSource Source;
		{
			CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
			Prepared = Source.Request(Graphics);
			Allocations = Trace.Attempts();
		}
		ASSERT_TRUE(Prepared);
		ASSERT_EQ(Graphics.m_Submissions, 1);
	}
	ASSERT_GT(Allocations, 0u);
	ASSERT_LE(Allocations, 256u);
	bool SawCallbackFailure = false, SawFactoryFailure = false;
	for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CCaptureGraphics Graphics;
		CCaptureSource Source;
		auto OldOwner = std::make_shared<int>(17);
		std::weak_ptr<int> OldLifetime = OldOwner;
		Graphics.m_Callback = [OldOwner](CImageInfo &&, const char *) {};
		OldOwner.reset();
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Prepared = Source.Request(Graphics);
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Prepared);
		EXPECT_EQ(Graphics.m_Submissions, 0);
		EXPECT_FALSE(OldLifetime.expired());
		EXPECT_FALSE(Graphics.m_Processor);
		EXPECT_EQ(Source.m_TakenCalls, 0);
		EXPECT_EQ(Source.m_ProcessorCalls, 0);
		SawCallbackFailure |= Source.m_FactoryCalls == 0;
		SawFactoryFailure |= Source.m_FactoryCalls != 0;
		const int FactoryCalls = Source.m_FactoryCalls;
		ASSERT_TRUE(Source.Request(Graphics));
		EXPECT_EQ(Graphics.m_Submissions, 1);
		EXPECT_EQ(Source.m_FactoryCalls, FactoryCalls + 1);
		EXPECT_TRUE(OldLifetime.expired());
	}
	EXPECT_TRUE(SawCallbackFailure);
	EXPECT_TRUE(SawFactoryFailure);
}

TEST(QmScreenshotRequest, ProcessorLengthErrorDoesNotSubmitAndTheNextRequestCanSucceed)
{
	CCaptureGraphics Graphics;
	CCaptureSource Source;
	EXPECT_FALSE(qm_screenshot_clipboard::RequestCapture(Graphics, false, Source.OnTaken(), []() -> IGraphics::FScreenshotProcessor {
		throw std::length_error("processor preparation failed");
	}));
	EXPECT_EQ(Graphics.m_Submissions, 0);
	ASSERT_TRUE(Source.Request(Graphics));
	EXPECT_EQ(Graphics.m_Submissions, 1);
}

TEST(QmScreenshotRequest, UnknownProcessorExceptionPropagatesBeforeGraphicsSubmission)
{
	CCaptureGraphics Graphics;
	CCaptureSource Source;
	EXPECT_THROW(qm_screenshot_clipboard::RequestCapture(Graphics, false, Source.OnTaken(), []() -> IGraphics::FScreenshotProcessor {
		throw 7;
	}),
		int);
	EXPECT_EQ(Graphics.m_Submissions, 0);
	EXPECT_FALSE(Graphics.m_Callback);
	EXPECT_FALSE(Graphics.m_Processor);
}

TEST(QmScreenshotRequest, CaptureKeepsTheRequestedClipboardValueAfterTheSettingChanges)
{
	for(const bool Requested : {false, true})
	{
		SCOPED_TRACE(Requested);
		CCaptureGraphics Graphics;
		CCaptureSource Source;
		bool Enabled = Requested;
		ASSERT_TRUE(Source.Request(Graphics, Enabled));
		Enabled = !Enabled;
		CCaptureLogger Logger;
		{
			CLogScope LogScope(&Logger);
			// 空图像让真实Copy在打开或修改系统剪贴板前返回，以警告观察捕获的开关。
			CImageInfo Empty;
			auto Callback = std::exchange(Graphics.m_Callback, IGraphics::FScreenshotCallback{});
			Callback(std::move(Empty), "screenshots/manual.png");
		}
		EXPECT_EQ(Enabled, !Requested);
		EXPECT_EQ(Logger.m_ClipboardWarnings, Requested ? 1 : 0);
		EXPECT_EQ(Source.m_TakenCalls, 1);
		EXPECT_EQ(Source.m_DeliveredPath, "screenshots/manual.png");
	}
}

TEST(QmScreenshotRequest, SubmissionMovesThePreparedProcessorAndImageCallbackOnce)
{
	CCaptureGraphics Graphics;
	CCaptureSource Source;
	const std::string OriginalMap = Source.m_Map;
	ASSERT_TRUE(Source.Request(Graphics));
	EXPECT_EQ(Graphics.m_Submissions, 1);
	EXPECT_TRUE(Graphics.m_NullFilename);
	EXPECT_EQ(Source.m_FactoryCalls, 1);
	EXPECT_EQ(Source.m_ProcessorCalls, 0);
	EXPECT_EQ(Source.m_TakenCalls, 0);
	Source.m_Map = "changed-after-request";
	std::array<uint8_t, 3> aPixels{12, 34, 56};
	CImageInfo Image;
	Image.m_Width = Image.m_Height = 1;
	Image.m_Format = CImageInfo::FORMAT_RGB;
	Image.m_pData = aPixels.data();
	std::string Comment;
	auto Processor = std::exchange(Graphics.m_Processor, IGraphics::FScreenshotProcessor{});
	auto Callback = std::exchange(Graphics.m_Callback, IGraphics::FScreenshotCallback{});
	ASSERT_TRUE(Processor(Image, Comment));
	Callback(std::move(Image), "screenshots/manual.png");
	EXPECT_EQ(Comment, OriginalMap);
	EXPECT_EQ(Source.m_ProcessorCalls, 1);
	EXPECT_EQ(Source.m_TakenCalls, 1);
	EXPECT_EQ(Source.m_Received.m_pData, aPixels.data());
	EXPECT_EQ(Image.m_pData, nullptr);
	EXPECT_EQ(Source.m_DeliveredPath, "screenshots/manual.png");
	EXPECT_FALSE(Graphics.m_Callback);
	EXPECT_FALSE(Graphics.m_Processor);
}
