#include <engine/client/screenshot_save.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
	CImageInfo MakeImage()
	{
		CImageInfo Image;
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.m_pData = static_cast<uint8_t *>(std::malloc(4));
		if(Image.m_pData)
		{
			Image.m_pData[0] = 42;
			Image.m_pData[1] = Image.m_pData[2] = 0;
			Image.m_pData[3] = 255;
		}
		return Image;
	}

	class ScreenshotSubmit : public ::testing::Test
	{
	protected:
		CImageInfo m_Image = MakeImage();
		IGraphics::FScreenshotCallback m_Preview;
		IGraphics::FScreenshotProcessor m_Processor;
		std::shared_ptr<screenshot_save::CSaveJob> m_pQueued;
		int m_Submits = 0, m_Reports = 0;
		~ScreenshotSubmit() override { m_Image.Free(); }
		void SetUp() override { ASSERT_NE(m_Image.m_pData, nullptr); }

		auto MakeJob(CImageInfo &&Image, IGraphics::FScreenshotProcessor &&Processor)
		{
			return std::make_shared<screenshot_save::CSaveJob>(nullptr, "unused.png", std::move(Image), std::move(Processor), nullptr);
		}
		bool Queue(const std::shared_ptr<screenshot_save::CSaveJob> &pJob)
		{
			++m_Submits;
			m_pQueued = pJob;
			return true;
		}
		bool Finish()
		{
			return screenshot_save::Finish(m_Image, m_Preview, m_Processor, [&](CImageInfo &&Image, IGraphics::FScreenshotProcessor &&Processor) { return MakeJob(std::move(Image), std::move(Processor)); }, [&](const auto &pJob) { return Queue(pJob); }, [&] { ++m_Reports; });
		}
	};
}

TEST_F(ScreenshotSubmit, AcceptanceTransfersProcessorLifetimeToTheActualJob)
{
	auto pToken = std::make_shared<int>(1);
	std::weak_ptr<int> Token = pToken;
	m_Processor = [pToken](CImageInfo &, std::string &) { return true; };
	pToken.reset();
	ASSERT_TRUE(Finish());
	EXPECT_EQ(m_Image.m_pData, nullptr);
	EXPECT_FALSE(m_Preview);
	EXPECT_FALSE(m_Processor);
	EXPECT_FALSE(Token.expired());
	EXPECT_EQ(m_Reports, 0);
	m_pQueued.reset();
	EXPECT_TRUE(Token.expired());
}

TEST_F(ScreenshotSubmit, PreviewReceivesACopyAndCannotModifySavedPixels)
{
	uint8_t *pOriginal = m_Image.m_pData;
	int Previews = 0;
	m_Preview = [&](CImageInfo &&Image) {
		++Previews;
		EXPECT_NE(Image.m_pData, pOriginal);
		Image.m_pData[0] = 9;
	};
	ASSERT_TRUE(screenshot_save::Finish(m_Image, m_Preview, m_Processor, [&](CImageInfo &&Image, IGraphics::FScreenshotProcessor &&Processor) {
			EXPECT_EQ(Image.m_pData, pOriginal);
			EXPECT_EQ(Image.m_pData[0], 42);
			return MakeJob(std::move(Image), std::move(Processor)); }, [&](const auto &pJob) { return Queue(pJob); }, [&] { ++m_Reports; }));
	EXPECT_EQ(Previews, 1);
	EXPECT_EQ(m_Reports, 0);
}

TEST_F(ScreenshotSubmit, PreviewExceptionDoesNotPreventSaving)
{
	m_Preview = [](CImageInfo &&) { throw std::bad_alloc(); };
	EXPECT_TRUE(Finish());
	EXPECT_EQ(m_Submits, 1);
	EXPECT_EQ(m_Reports, 0);
}

TEST_F(ScreenshotSubmit, FactoryFailureConsumesRequestAndNextCaptureCanSave)
{
	int Previews = 0;
	m_Preview = [&](CImageInfo &&) { ++Previews; };
	EXPECT_FALSE(screenshot_save::Finish(m_Image, m_Preview, m_Processor, [](CImageInfo &&, IGraphics::FScreenshotProcessor &&) -> std::shared_ptr<screenshot_save::CSaveJob> { throw std::bad_alloc(); }, [&](const auto &pJob) { return Queue(pJob); }, [&] { ++m_Reports; }));
	EXPECT_EQ(m_Image.m_pData, nullptr);
	EXPECT_FALSE(m_Preview);
	EXPECT_FALSE(m_Processor);
	EXPECT_EQ(m_Submits, 0);
	EXPECT_EQ(m_Reports, 1);
	m_Image = MakeImage();
	ASSERT_NE(m_Image.m_pData, nullptr);
	EXPECT_TRUE(Finish());
	EXPECT_EQ(Previews, 1);
}

TEST_F(ScreenshotSubmit, EmptyFactoryResultDoesNotReachQueue)
{
	EXPECT_FALSE(screenshot_save::Finish(m_Image, m_Preview, m_Processor, [](CImageInfo &&, IGraphics::FScreenshotProcessor &&) { return std::shared_ptr<screenshot_save::CSaveJob>(); }, [&](const auto &pJob) { return Queue(pJob); }, [&] { ++m_Reports; }));
	EXPECT_EQ(m_Submits, 0);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_Image.m_pData, nullptr);
}

TEST_F(ScreenshotSubmit, RejectedSubmissionReleasesConstructedJob)
{
	std::weak_ptr<screenshot_save::CSaveJob> Job;
	EXPECT_FALSE(screenshot_save::Finish(m_Image, m_Preview, m_Processor, [&](CImageInfo &&Image, IGraphics::FScreenshotProcessor &&Processor) {
			auto pJob = MakeJob(std::move(Image), std::move(Processor));
			Job = pJob;
			return pJob; }, [](const auto &) { return false; }, [&] { ++m_Reports; }));
	EXPECT_TRUE(Job.expired());
	EXPECT_EQ(m_Reports, 1);
}

TEST_F(ScreenshotSubmit, QueueExceptionIsReportedWithoutEscaping)
{
	EXPECT_FALSE(screenshot_save::Finish(m_Image, m_Preview, m_Processor, [&](CImageInfo &&Image, IGraphics::FScreenshotProcessor &&Processor) { return MakeJob(std::move(Image), std::move(Processor)); }, [](const auto &) -> bool { throw std::bad_alloc(); }, [&] { ++m_Reports; }));
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_Image.m_pData, nullptr);
}

TEST_F(ScreenshotSubmit, ReportingExceptionDoesNotReplayFailure)
{
	EXPECT_FALSE(screenshot_save::Finish(m_Image, m_Preview, m_Processor, [&](CImageInfo &&Image, IGraphics::FScreenshotProcessor &&Processor) { return MakeJob(std::move(Image), std::move(Processor)); }, [](const auto &) { return false; }, [&] { ++m_Reports; throw std::runtime_error("warning unavailable"); }));
	EXPECT_EQ(m_Reports, 1);
}

TEST_F(ScreenshotSubmit, MissingPreviewAndProcessorStillAllowSaving)
{
	EXPECT_TRUE(Finish());
	EXPECT_EQ(m_Submits, 1);
	EXPECT_EQ(m_Reports, 0);
}

TEST_F(ScreenshotSubmit, ConsumedPreviewIsNotReplayedOnAnotherCapture)
{
	int Previews = 0;
	m_Preview = [&](CImageInfo &&) { ++Previews; };
	ASSERT_TRUE(Finish());
	m_Image = MakeImage();
	ASSERT_NE(m_Image.m_pData, nullptr);
	ASSERT_TRUE(Finish());
	EXPECT_EQ(Previews, 1);
	EXPECT_EQ(m_Submits, 2);
}
