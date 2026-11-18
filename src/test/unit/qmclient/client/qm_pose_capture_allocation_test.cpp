#include <game/client/components/qmclient/pose_capture.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>

TEST(QmPoseCaptureAllocation, PreservesPixelsWhenSharpeningCannotAllocate)
{
	constexpr int WIDTH = 3;
	constexpr int HEIGHT = 3;
	constexpr int CENTER = (WIDTH + 1) * 4;
	std::array<uint8_t, WIDTH * HEIGHT * 4> aPixels{};
	aPixels[CENTER] = 64;
	aPixels[CENTER + 3] = 255;
	const auto aOriginal = aPixels;

	bool Failed = false;
	{
		CPredictionAllocationFailure Failure;
		qm_pose::ApplyUnsharpMask(aPixels.data(), WIDTH, HEIGHT, 100);
		Failed = Failure.Triggered();
	}
	ASSERT_TRUE(Failed);
	EXPECT_EQ(aPixels, aOriginal);

	qm_pose::ApplyUnsharpMask(aPixels.data(), WIDTH, HEIGHT, 100);
	EXPECT_GT(aPixels[CENTER], aOriginal[CENTER]);
	EXPECT_EQ(aPixels[CENTER + 3], aOriginal[CENTER + 3]);
}
