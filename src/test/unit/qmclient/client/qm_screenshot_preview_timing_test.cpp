#include <game/client/components/qmclient/screenshot_preview_timing.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(QmScreenshotPreviewTiming, FadesAndExpiresOneSecondAfterCaptureWithoutInput)
{
	CQmScreenshotPreviewTiming Timing;
	Timing.Start(5s);
	EXPECT_FLOAT_EQ(Timing.Alpha(5500ms), 1.0f);
	EXPECT_GT(Timing.Alpha(5900ms), 0.0f);
	EXPECT_LT(Timing.Alpha(5900ms), 1.0f);
	EXPECT_FLOAT_EQ(Timing.Alpha(6s), 0.0f);
}

TEST(QmScreenshotPreviewTiming, FirstFrameAfterLongPauseSkipsExpiredPreview)
{
	CQmScreenshotPreviewTiming Timing;
	Timing.Start(5s);
	EXPECT_FLOAT_EQ(Timing.Alpha(8s), 0.0f);
}

TEST(QmScreenshotPreviewTiming, ManualDismissCanOnlyShortenLifetime)
{
	CQmScreenshotPreviewTiming Timing;
	Timing.Start(5s);
	Timing.Dismiss(5400ms);
	Timing.Dismiss(5500ms);
	EXPECT_FLOAT_EQ(Timing.Alpha(5620ms), 0.0f);

	Timing.Start(8s);
	Timing.Dismiss(8900ms);
	EXPECT_FLOAT_EQ(Timing.Alpha(9s), 0.0f);
}

TEST(QmScreenshotPreviewTiming, ReplacementGetsItsOwnFullLifetime)
{
	CQmScreenshotPreviewTiming Timing;
	Timing.Start(5s);
	Timing.Dismiss(5500ms);
	Timing.Start(5600ms);
	EXPECT_FLOAT_EQ(Timing.ElapsedSeconds(5600ms), 0.0f);
	EXPECT_FLOAT_EQ(Timing.Alpha(6s), 1.0f);
	EXPECT_FLOAT_EQ(Timing.Alpha(6600ms), 0.0f);
}
