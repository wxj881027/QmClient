#include <engine/message.h>
#include <engine/shared/packer.h>

#include <game/client/components/qmclient/camera_network.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <cmath>
#include <limits>

namespace
{
	template<typename TMessage, typename TCheck>
	void CheckPacket(const TMessage &Message, TCheck &&Check)
	{
		CMsgPacker Packet(TMessage::ms_MsgId);
		ASSERT_FALSE(Message.Pack(&Packet));
		CUnpacker Unpacker;
		Unpacker.Reset(Packet.Data(), Packet.Size());
		CNetObjHandler Handler;
		const auto *pDecoded = static_cast<const TMessage *>(Handler.SecureUnpackMsg(Packet.m_MsgId, &Unpacker));
		ASSERT_NE(pDecoded, nullptr);
		ASSERT_FALSE(Unpacker.Error());
		Check(*pDecoded);
	}
}

TEST(CameraNetworkMessages, NormalCameraInfoSurvivesRealMessagePacking)
{
	CheckPacket(QmCameraNetwork::CameraInfo(1.2349f, 300, 60), [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_Zoom, 1234);
		EXPECT_EQ(Decoded.m_Deadzone, 300);
		EXPECT_EQ(Decoded.m_FollowFactor, 60);
	});
}

TEST(CameraNetworkMessages, IntegerBoundaryAndUnboundedZoomRemainValidMessageFields)
{
	struct SCase
	{
		float m_Zoom;
		int m_Expected;
	};
	for(const SCase Case : {SCase{2147483.5f, 2147483520}, SCase{2147483.75f, std::numeric_limits<int>::max()}, SCase{std::numeric_limits<float>::max(), std::numeric_limits<int>::max()}})
	{
		SCOPED_TRACE(Case.m_Zoom);
		CheckPacket(QmCameraNetwork::CameraInfo(Case.m_Zoom, 3000, 200), [&](const auto &Decoded) {
			EXPECT_EQ(Decoded.m_Zoom, Case.m_Expected);
			EXPECT_EQ(Decoded.m_Deadzone, 3000);
			EXPECT_EQ(Decoded.m_FollowFactor, 200);
		});
	}
}

TEST(CameraNetworkMessages, InvalidCameraZoomEncodesZeroWithoutChangingOptions)
{
	CheckPacket(QmCameraNetwork::CameraInfo(std::numeric_limits<float>::quiet_NaN(), 1300, 0), [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_Zoom, 0);
		EXPECT_EQ(Decoded.m_Deadzone, 1300);
		EXPECT_EQ(Decoded.m_FollowFactor, 0);
	});
}

TEST(CameraNetworkMessages, RealScreenParametersKeepTheirNormalIntegerDimensions)
{
	CIconBenchmarkGraphics Graphics;
	float Width, Height;
	Graphics.CalcScreenParams(2.0f, 1.25f, &Width, &Height);
	EXPECT_FLOAT_EQ(Width, 1875.0f);
	EXPECT_FLOAT_EQ(Height, 937.5f);
	CheckPacket(QmCameraNetwork::ShowDistance(Width, Height), [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_X, 1875);
		EXPECT_EQ(Decoded.m_Y, 937);
	});
}

TEST(CameraNetworkMessages, OverflowedRealScreenParametersProduceMaximumValidDimensions)
{
	CIconBenchmarkGraphics Graphics;
	float Width, Height;
	Graphics.CalcScreenParams(2.0f, std::numeric_limits<float>::max(), &Width, &Height);
	ASSERT_TRUE(std::isinf(Width));
	ASSERT_TRUE(std::isinf(Height));
	CheckPacket(QmCameraNetwork::ShowDistance(Width, Height), [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_X, std::numeric_limits<int>::max());
		EXPECT_EQ(Decoded.m_Y, std::numeric_limits<int>::max());
	});
}

TEST(CameraNetworkMessages, FiniteScreenDimensionsSaturateOnlyTheOutOfIntegerRangeAxis)
{
	CIconBenchmarkGraphics Graphics;
	float Width, Height;
	Graphics.CalcScreenParams(2.0f, 2000000.0f, &Width, &Height);
	ASSERT_TRUE(std::isfinite(Width));
	ASSERT_TRUE(std::isfinite(Height));
	EXPECT_FLOAT_EQ(Width, 3000000000.0f);
	EXPECT_FLOAT_EQ(Height, 1500000000.0f);
	CheckPacket(QmCameraNetwork::ShowDistance(Width, Height), [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_X, std::numeric_limits<int>::max());
		EXPECT_EQ(Decoded.m_Y, 1500000000);
	});
}

TEST(CameraNetworkMessages, InvalidShowDistanceAxisIsIsolatedThroughPacking)
{
	CheckPacket(QmCameraNetwork::ShowDistance(std::numeric_limits<float>::quiet_NaN(), 1000.75f), [](const auto &Decoded) {
		EXPECT_EQ(Decoded.m_X, 0);
		EXPECT_EQ(Decoded.m_Y, 1000);
	});
}
