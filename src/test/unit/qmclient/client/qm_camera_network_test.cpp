#include <game/client/components/qmclient/camera_network.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(QmCameraNetworkEncoding, FinitePositiveDimensionsKeepTruncation)
{
	EXPECT_EQ(QmCameraEffects::EncodeNonNegativeInt(0.5f), 0);
	EXPECT_EQ(QmCameraEffects::EncodeNonNegativeInt(1050.75f), 1050);
	EXPECT_EQ(QmCameraEffects::EncodeNonNegativeInt(1500.0f), 1500);
}

TEST(QmCameraNetworkEncoding, LargestRepresentableDimensionBelowIntMaximumRemainsExact)
{
	EXPECT_EQ(QmCameraEffects::EncodeNonNegativeInt(2147483520.0f), 2147483520);
}

TEST(QmCameraNetworkEncoding, DimensionsAtOrAboveIntegerLimitUseTheFieldMaximum)
{
	for(float Value : {2147483648.0f, std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity()})
		EXPECT_EQ(QmCameraEffects::EncodeNonNegativeInt(Value), std::numeric_limits<int>::max());
}

TEST(QmCameraNetworkEncoding, NonPositiveAndNanDimensionsUseZero)
{
	for(float Value : {0.0f, -0.0f, -1.0f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		EXPECT_EQ(QmCameraEffects::EncodeNonNegativeInt(Value), 0);
}

TEST(QmCameraNetworkEncoding, CameraMessageUsesExistingZoomUnitsAndPreservesOptions)
{
	const auto Message = QmCameraNetwork::CameraInfo(1.2349f, 300, 60);
	EXPECT_EQ(Message.m_Zoom, 1234);
	EXPECT_EQ(Message.m_Deadzone, 300);
	EXPECT_EQ(Message.m_FollowFactor, 60);
}

TEST(QmCameraNetworkEncoding, UnboundedCameraMessageSaturatesOnlyItsZoomField)
{
	const auto Message = QmCameraNetwork::CameraInfo(std::numeric_limits<float>::max(), 3000, 200);
	EXPECT_EQ(Message.m_Zoom, std::numeric_limits<int>::max());
	EXPECT_EQ(Message.m_Deadzone, 3000);
	EXPECT_EQ(Message.m_FollowFactor, 200);
}

TEST(QmCameraNetworkEncoding, IntegerOptionsDoNotRoundThroughFloatStorage)
{
	const auto Message = QmCameraNetwork::CameraInfo(1.0f, std::numeric_limits<int>::max(), 16777217);
	EXPECT_EQ(Message.m_Deadzone, std::numeric_limits<int>::max());
	EXPECT_EQ(Message.m_FollowFactor, 16777217);
}

TEST(QmCameraNetworkEncoding, InvalidCameraZoomDoesNotPoisonOtherFields)
{
	const auto Message = QmCameraNetwork::CameraInfo(std::numeric_limits<float>::quiet_NaN(), 1300, 20);
	EXPECT_EQ(Message.m_Zoom, 0);
	EXPECT_EQ(Message.m_Deadzone, 1300);
	EXPECT_EQ(Message.m_FollowFactor, 20);
}

TEST(QmCameraNetworkEncoding, ShowDistanceMessageKeepsNormalAxesIndependent)
{
	const auto Message = QmCameraNetwork::ShowDistance(1875.0f, 937.5f);
	EXPECT_EQ(Message.m_X, 1875);
	EXPECT_EQ(Message.m_Y, 937);
}

TEST(QmCameraNetworkEncoding, OverflowedShowDistanceUsesRepresentableMaxima)
{
	const auto Message = QmCameraNetwork::ShowDistance(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max());
	EXPECT_EQ(Message.m_X, std::numeric_limits<int>::max());
	EXPECT_EQ(Message.m_Y, std::numeric_limits<int>::max());
}

TEST(QmCameraNetworkEncoding, InvalidShowDistanceAxisDoesNotDiscardAValidAxis)
{
	const auto First = QmCameraNetwork::ShowDistance(std::numeric_limits<float>::quiet_NaN(), 640.75f);
	EXPECT_EQ(First.m_X, 0);
	EXPECT_EQ(First.m_Y, 640);
	const auto Second = QmCameraNetwork::ShowDistance(800.25f, -std::numeric_limits<float>::infinity());
	EXPECT_EQ(Second.m_X, 800);
	EXPECT_EQ(Second.m_Y, 0);
}
