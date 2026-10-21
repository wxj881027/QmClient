#include <engine/client/game_ping.h>

#include <gtest/gtest.h>

TEST(QmMonitoringNetworkContract, GamePingProbeOnlyAcceptsCurrentUuid)
{
	const int64_t Frequency = 1000;
	const CUuid ProbeUuid = CalculateUuid("game-ping-current@test");
	const CUuid WrongUuid = CalculateUuid("game-ping-wrong@test");
	SGamePingProbe Probe;

	Probe.Begin(ProbeUuid, 10000, Frequency);
	EXPECT_FALSE(Probe.HandlePong(WrongUuid, 10025, Frequency));
	EXPECT_EQ(Probe.m_StartTime, 10000);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_Uuid, ProbeUuid);

	EXPECT_TRUE(Probe.HandlePong(ProbeUuid, 10025, Frequency));
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_EQ(Probe.m_NextTime, 11025);
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
}

TEST(QmMonitoringNetworkContract, GamePingProbeRejectsLatePongAfterTimeoutAndReplacement)
{
	const int64_t Frequency = 1000;
	const CUuid ProbeA = CalculateUuid("game-ping-a@test");
	const CUuid ProbeB = CalculateUuid("game-ping-b@test");
	SGamePingProbe Probe;

	Probe.Begin(ProbeA, 10000, Frequency);
	EXPECT_FALSE(Probe.HandleTimeout(11999, Frequency));
	EXPECT_TRUE(Probe.HandleTimeout(12000, Frequency));
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_NextTime, 13000);

	Probe.Begin(ProbeB, 13000, Frequency);
	EXPECT_FALSE(Probe.HandlePong(ProbeA, 13040, Frequency));
	EXPECT_EQ(Probe.m_StartTime, 13000);
	EXPECT_EQ(Probe.m_Uuid, ProbeB);
	EXPECT_TRUE(Probe.HandlePong(ProbeB, 13050, Frequency));
	EXPECT_EQ(Probe.m_RttMs, 50);

	Probe.Reset();
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_EQ(Probe.m_NextTime, -1);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
}

TEST(QmMonitoringNetworkContract, ManualPingProbeRejectsOverlappingRequests)
{
	SManualPingProbe Probe;
	EXPECT_TRUE(Probe.Begin(10000));
	EXPECT_FALSE(Probe.Begin(10010));
	EXPECT_EQ(Probe.m_StartTime, 10000);

	float RttMs = -1.0f;
	EXPECT_TRUE(Probe.HandlePong(10025, 1000, RttMs));
	EXPECT_FLOAT_EQ(RttMs, 25.0f);
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_FALSE(Probe.HandlePong(10030, 1000, RttMs));
}

TEST(QmMonitoringNetworkContract, ManualPingProbeRecoversAfterTimeout)
{
	SManualPingProbe Probe;
	EXPECT_TRUE(Probe.Begin(10000));
	EXPECT_FALSE(Probe.HandleTimeout(11999, 1000));
	EXPECT_TRUE(Probe.HandleTimeout(12000, 1000));
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_TRUE(Probe.Begin(13000));
}

TEST(QmMonitoringNetworkContract, LegacyGamePingProbeAcceptsLegacyReply)
{
	SGamePingProbe Probe;
	Probe.BeginLegacy(10000, 1000);
	EXPECT_TRUE(Probe.m_Legacy);
	EXPECT_TRUE(Probe.HandleLegacyPong(10025, 1000));
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_FALSE(Probe.m_Legacy);
}

TEST(QmMonitoringNetworkContract, LegacyManualPingSharesAutomaticProbeAndReply)
{
	constexpr int64_t Start = 10000;
	constexpr int64_t Frequency = 1000;
	SGamePingProbe Automatic;
	SManualPingProbe Manual;

	// Legacy 回复没有请求 ID，因此两个路径必须表示同一个线上请求。
	EXPECT_TRUE(Manual.Begin(Start));
	Automatic.BeginLegacy(Start, Frequency);
	EXPECT_TRUE(Automatic.HandleLegacyPong(Start + 25, Frequency));
	float RttMs = -1.0f;
	EXPECT_TRUE(Manual.HandlePong(Start + 25, Frequency, RttMs));
	EXPECT_FLOAT_EQ(RttMs, 25.0f);
	EXPECT_EQ(Automatic.m_StartTime, -1);
	EXPECT_EQ(Manual.m_StartTime, -1);
}

TEST(GamePingProbe, ExtendedPongCannotCompleteLegacyRequest)
{
	const CUuid Uuid = CalculateUuid("game-ping-replaced-extended@test");
	SGamePingProbe Probe;
	Probe.Begin(Uuid, 9000, 1000);
	Probe.BeginLegacy(10000, 1000);
	EXPECT_FALSE(Probe.HandlePong(Uuid, 10025, 1000));
	EXPECT_FALSE(Probe.HandlePong(UUID_ZEROED, 10025, 1000));
	EXPECT_EQ(Probe.m_StartTime, 10000);
	EXPECT_EQ(Probe.m_NextTime, 11000);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
	EXPECT_TRUE(Probe.m_Legacy);
	EXPECT_TRUE(Probe.HandleLegacyPong(10030, 1000));
	EXPECT_EQ(Probe.m_RttMs, 30);
}

TEST(GamePingProbe, LegacyPongCannotCompleteExtendedRequest)
{
	const CUuid Uuid = CalculateUuid("game-ping-extended@test");
	SGamePingProbe Probe;
	Probe.BeginLegacy(9000, 1000);
	Probe.Begin(Uuid, 10000, 1000);
	EXPECT_FALSE(Probe.HandleLegacyPong(10025, 1000));
	EXPECT_EQ(Probe.m_StartTime, 10000);
	EXPECT_EQ(Probe.m_NextTime, 11000);
	EXPECT_EQ(Probe.m_Uuid, Uuid);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_FALSE(Probe.m_Legacy);
	EXPECT_TRUE(Probe.HandlePong(Uuid, 10030, 1000));
	EXPECT_EQ(Probe.m_RttMs, 30);
}

TEST(GamePingProbe, CompletedExtendedRequestRejectsDuplicatePong)
{
	const CUuid Uuid = CalculateUuid("game-ping-duplicate@test");
	SGamePingProbe Probe;
	Probe.Begin(Uuid, 10000, 1000);
	ASSERT_TRUE(Probe.HandlePong(Uuid, 10025, 1000));
	EXPECT_FALSE(Probe.HandlePong(Uuid, 10125, 1000));
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_EQ(Probe.m_NextTime, 11025);
}

TEST(GamePingProbe, LegacyTimeoutInvalidatesReplyAndCanRetry)
{
	SGamePingProbe Probe;
	Probe.BeginLegacy(10000, 1000);
	EXPECT_FALSE(Probe.HandleTimeout(11999, 1000));
	ASSERT_TRUE(Probe.HandleTimeout(12000, 1000));
	EXPECT_FALSE(Probe.m_Legacy);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_EQ(Probe.m_NextTime, 13000);
	EXPECT_FALSE(Probe.HandleLegacyPong(12001, 1000));
	EXPECT_FALSE(Probe.HandleTimeout(14000, 1000));
	EXPECT_EQ(Probe.m_NextTime, 13000);

	Probe.BeginLegacy(14000, 1000);
	EXPECT_TRUE(Probe.HandleLegacyPong(14020, 1000));
	EXPECT_EQ(Probe.m_RttMs, 20);
}

TEST(GamePingProbe, ResetClearsLastMeasurementAndPendingIdentity)
{
	const CUuid Uuid = CalculateUuid("game-ping-reset@test");
	SGamePingProbe Probe;
	Probe.Begin(Uuid, 10000, 1000);
	ASSERT_TRUE(Probe.HandlePong(Uuid, 10025, 1000));
	Probe.Begin(Uuid, 12000, 1000);
	Probe.Reset();
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_EQ(Probe.m_NextTime, -1);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
	EXPECT_FALSE(Probe.m_Legacy);
	EXPECT_FALSE(Probe.HandleLegacyPong(12050, 1000));
	EXPECT_FALSE(Probe.HandlePong(Uuid, 12050, 1000));

	Probe.Begin(Uuid, 13000, 1000);
	EXPECT_TRUE(Probe.HandlePong(Uuid, 13010, 1000));
	EXPECT_EQ(Probe.m_RttMs, 10);
}

TEST(ManualPingProbe, ResetCancelsPendingRequestAndCanRetry)
{
	SManualPingProbe Probe;
	ASSERT_TRUE(Probe.Begin(10000));
	Probe.Reset();
	float RttMs = -7.0f;
	EXPECT_FALSE(Probe.HandlePong(10025, 1000, RttMs));
	EXPECT_FLOAT_EQ(RttMs, -7.0f);
	EXPECT_FALSE(Probe.HandleTimeout(12000, 1000));
	EXPECT_TRUE(Probe.Begin(13000));
	EXPECT_TRUE(Probe.HandlePong(13040, 1000, RttMs));
	EXPECT_FLOAT_EQ(RttMs, 40.0f);
}

TEST(ManualPingProbe, TimeoutRejectsLateReplyWithoutChangingOutput)
{
	SManualPingProbe Probe;
	ASSERT_TRUE(Probe.Begin(10000));
	ASSERT_TRUE(Probe.HandleTimeout(12000, 1000));
	float RttMs = -7.0f;
	EXPECT_FALSE(Probe.HandlePong(12001, 1000, RttMs));
	EXPECT_FLOAT_EQ(RttMs, -7.0f);
}

TEST(GamePingProbe, RttTruncatesFractionalMilliseconds)
{
	const CUuid Uuid = CalculateUuid("game-ping-fractional@test");
	SGamePingProbe Probe;
	Probe.Begin(Uuid, 10000, 10000);
	ASSERT_TRUE(Probe.HandlePong(Uuid, 10255, 10000));
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_EQ(Probe.m_NextTime, 20255);
}

TEST(ManualPingProbe, RttRetainsFractionalMilliseconds)
{
	SManualPingProbe Probe;
	ASSERT_TRUE(Probe.Begin(10000));
	float RttMs = -1.0f;
	ASSERT_TRUE(Probe.HandlePong(10255, 10000, RttMs));
	EXPECT_FLOAT_EQ(RttMs, 25.5f);
}
