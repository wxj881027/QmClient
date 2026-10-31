#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <limits>

TEST(QmCameraViewBounds, ShortAxisUsesCenterForEveryRequestedPosition)
{
	for(float Position : {-1000.0f, 0.0f, 10000.0f})
	{
		EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(Position, 1), 16.0f);
		EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(Position, 12), 192.0f);
	}
}

TEST(QmCameraViewBounds, ThirteenTilesRetainTwoHundredUnitMargins)
{
	EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(0.0f, 13), 200.0f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(208.0f, 13), 208.0f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(1000.0f, 13), 216.0f);
}

TEST(QmCameraViewBounds, ShortAndNormalAxesAreHandledIndependently)
{
	EXPECT_EQ(QmCameraEffects::ViewTarget(ivec2(1000, 1000), vec2(), false, 12, 100), vec2(192.0f, 3000.0f));
	EXPECT_EQ(QmCameraEffects::ViewTarget(ivec2(-1000, -1000), vec2(), false, 100, 3), vec2(200.0f, 48.0f));
}

TEST(QmCameraViewBounds, RelativeCoordinatesKeepCurrentPositionBeforeClamping)
{
	EXPECT_EQ(QmCameraEffects::ViewTarget(ivec2(1, -2), vec2(500, 600), true, 100, 100), vec2(532, 536));
}

TEST(QmCameraViewBounds, NormalAbsoluteCoordinatesKeepTileConversion)
{
	EXPECT_EQ(QmCameraEffects::ViewTarget(ivec2(50, 60), vec2(999, 999), false, 100, 100), vec2(1600, 1920));
}

TEST(QmCameraViewBounds, WideAxisDoesNotMultiplyTileCountInInt)
{
	EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(std::numeric_limits<float>::max(), 100000000), 3199999744.0f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(std::numeric_limits<float>::max(), std::numeric_limits<int>::max()), 68719476736.0f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ClampViewAxis(-std::numeric_limits<float>::max(), std::numeric_limits<int>::max()), 200.0f);
}

TEST(QmCameraViewBounds, EmptyAxisHasZeroCenterWithoutReversedBounds)
{
	EXPECT_EQ(QmCameraEffects::ViewTarget(ivec2(1, 50), vec2(), false, 0, 100), vec2(0, 1600));
}

TEST(QmDemoZoomEncoding, NormalRangeKeepsFloatScalingAndTruncation)
{
	EXPECT_EQ(QmCameraEffects::EncodeZoom(1.0f), 1000);
	EXPECT_EQ(QmCameraEffects::EncodeZoom(1.234f), 1234);
	EXPECT_EQ(QmCameraEffects::EncodeZoom(1.2349f), 1234);
	EXPECT_EQ(QmCameraEffects::EncodeZoom(0.0005f), 0);
}

TEST(QmDemoZoomEncoding, LargestInRangeFloatKeepsOriginalQuantizedValue)
{
	EXPECT_EQ(QmCameraEffects::EncodeZoom(2147483.5f), 2147483520);
}

TEST(QmDemoZoomEncoding, NextOutOfRangeFloatUsesFieldMaximum)
{
	EXPECT_EQ(QmCameraEffects::EncodeZoom(2147483.75f), std::numeric_limits<int>::max());
}

TEST(QmDemoZoomEncoding, LargestFiniteZoomIsStillAcceptedAndRecordsMaximum)
{
	const float Zoom = std::numeric_limits<float>::max();
	EXPECT_TRUE(QmCameraEffects::IsValidZoom(Zoom, 0.01f, std::numeric_limits<float>::max()));
	EXPECT_EQ(QmCameraEffects::EncodeZoom(Zoom), std::numeric_limits<int>::max());
}

TEST(QmDemoZoomEncoding, PositiveInfinityUsesFieldMaximumWithoutConversion)
{
	EXPECT_EQ(QmCameraEffects::EncodeZoom(std::numeric_limits<float>::infinity()), std::numeric_limits<int>::max());
}

TEST(QmDemoZoomEncoding, NonPositiveAndNaNInputsUseFieldMinimum)
{
	for(float Zoom : {0.0f, -1.0f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		EXPECT_EQ(QmCameraEffects::EncodeZoom(Zoom), 0);
}
