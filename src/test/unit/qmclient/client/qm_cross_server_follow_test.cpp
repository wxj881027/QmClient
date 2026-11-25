#include <base/system.h>

#include <game/client/components/qmclient/cross_server_follow_logic.h>

#include <gtest/gtest.h>

namespace
{
	using EAction = EFollowAction;

	class QmCrossServerFollow : public ::testing::Test
	{
	protected:
		CCrossServerFollowEngine m_Engine;
		NETADDR m_Current{};
		SFollowServerCandidate m_Old;
		SFollowServerCandidate m_New;
		SFollowServerCandidate m_Other;

		void SetUp() override
		{
			m_Engine.Start("Target", "Clan");
			m_Old = Server("127.0.0.1:8303");
			m_New = Server("127.0.0.1:8304");
			m_Other = Server("127.0.0.1:8305");
		}

		SFollowServerCandidate Server(const char *pAddress, const char *pClan = "Clan")
		{
			NETADDR Address{};
			EXPECT_EQ(net_addr_from_str(&Address, pAddress), 0);
			SFollowServerCandidate Result;
			Result.m_ConnectAddress = pAddress;
			Result.m_vAddresses = {Address};
			Result.m_Players.Add(m_Engine.Target(), "Target", pClan);
			return Result;
		}

		void Together(const SFollowServerCandidate &Server, double Now = 0.0)
		{
			m_Current = Server.m_vAddresses[0];
			EXPECT_EQ(Step(Now, {}, Server.m_Players).m_Action, EAction::NONE);
		}

		SFollowDecision Step(double Now, std::span<const SFollowServerCandidate> Servers, SFollowPlayerMatches Local = {},
			int Delay = 3, bool Connecting = false, bool IgnoreClan = false)
		{
			const std::span<const NETADDR> Current = m_Current.type == 0 ? std::span<const NETADDR>{} : std::span(&m_Current, 1);
			return m_Engine.Step(Now, Delay, Connecting, Servers, Current, Local, IgnoreClan);
		}
	};
}

TEST_F(QmCrossServerFollow, WaitsForOfflineTargetAndConnectsImmediatelyOnAppearance)
{
	EXPECT_EQ(Step(0.0, {}).m_Action, EAction::NONE);
	EXPECT_TRUE(m_Engine.Target().m_Active);
	EXPECT_EQ(Step(5.0, std::span(&m_New, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, StartingOnAnotherServerDoesNotStartCountdown)
{
	m_Current = m_Old.m_vAddresses[0];
	m_Engine.Start("Target", "Clan", &m_Current, true);
	EXPECT_EQ(Step(0.0, std::span(&m_New, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, AppliesDelayOnlyAfterBeingTogetherWithoutLimitingJumps)
{
	auto Source = m_Old;
	auto Destination = m_New;
	for(int Jump = 0; Jump < 4; ++Jump)
	{
		const double Now = Jump * 10.0;
		Together(Source, Now);
		const auto Servers = std::span(&Destination, 1);
		const auto Pending = Step(Now + 1.0, Servers);
		EXPECT_EQ(Pending.m_Action, EAction::NOTIFY_PENDING_SWITCH);
		EXPECT_EQ(Pending.m_SecondsRemaining, 3);
		EXPECT_EQ(Step(Now + 3.9, Servers).m_Action, EAction::NONE);
		EXPECT_EQ(Step(Now + 4.0, Servers).m_Action, EAction::CONNECT);
		m_Engine.OnConnectionEstablished();
		std::swap(Source, Destination);
	}
}

TEST_F(QmCrossServerFollow, ReturningToCurrentServerCancelsAndRestartsCountdown)
{
	Together(m_Old);
	const auto Servers = std::span(&m_New, 1);
	EXPECT_EQ(Step(1.0, Servers).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	Together(m_Old, 2.0);
	EXPECT_EQ(Step(4.0, Servers).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	EXPECT_EQ(Step(6.0, Servers).m_Action, EAction::NONE);
	EXPECT_EQ(Step(7.0, Servers).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, TargetDisappearingClearsCountdownButKeepsSession)
{
	Together(m_Old);
	const auto Servers = std::span(&m_New, 1);
	EXPECT_EQ(Step(1.0, Servers).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	EXPECT_EQ(Step(2.0, {}).m_Action, EAction::NONE);
	EXPECT_TRUE(m_Engine.Target().m_Active);
	EXPECT_EQ(Step(5.0, Servers).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	EXPECT_EQ(Step(8.0, Servers).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, ChangingDestinationsStartsANewCountdown)
{
	Together(m_Old);
	EXPECT_EQ(Step(1.0, std::span(&m_New, 1)).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	EXPECT_EQ(Step(3.0, std::span(&m_Other, 1)).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	EXPECT_EQ(Step(4.0, std::span(&m_Other, 1)).m_Action, EAction::NONE);
	EXPECT_EQ(Step(6.0, std::span(&m_Other, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, CurrentSnapshotDisprovesAnAdvertisedGhostOnTheCurrentServer)
{
	Together(m_Old);
	const SFollowServerCandidate aServers[] = {m_Old, m_New};
	const auto Decision = Step(1.0, aServers, {}, 0);
	ASSERT_EQ(Decision.m_Action, EAction::CONNECT);
	EXPECT_EQ(Decision.m_pServer, &aServers[1]);
}

TEST_F(QmCrossServerFollow, NewServerWinsOverRememberedGhostWhenOffline)
{
	Together(m_Old);
	m_Current = {};
	const SFollowServerCandidate aServers[] = {m_Old, m_New};
	const auto Decision = Step(1.0, aServers);
	ASSERT_EQ(Decision.m_Action, EAction::CONNECT);
	EXPECT_EQ(Decision.m_pServer, &aServers[1]);
}

TEST_F(QmCrossServerFollow, OldGhostDoesNotPullTheClientBackWhileAwaitingTargetOnNewServer)
{
	Together(m_Old);
	const SFollowServerCandidate aServers[] = {m_Old, m_New};
	ASSERT_EQ(Step(1.0, aServers, {}, 0).m_Action, EAction::CONNECT);
	m_Engine.OnConnectionEstablished();
	m_Current = m_New.m_vAddresses[0];
	EXPECT_EQ(Step(2.0, aServers).m_Action, EAction::NONE);
	Together(m_New, 3.0);
	EXPECT_EQ(Step(4.0, std::span(&m_Old, 1), {}, 0).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, ASecondQuickSwitchIgnoresGhostsFromBothDepartedServers)
{
	Together(m_Old);
	const SFollowServerCandidate aFirstMove[] = {m_Old, m_New};
	ASSERT_EQ(Step(1.0, aFirstMove, {}, 0).m_Action, EAction::CONNECT);
	m_Engine.OnConnectionEstablished();
	m_Current = m_New.m_vAddresses[0];
	ASSERT_EQ(Step(2.0, aFirstMove, m_New.m_Players, 0).m_Action, EAction::NONE);

	const SFollowServerCandidate aSecondMove[] = {m_Old, m_New, m_Other};
	const auto Decision = Step(3.0, aSecondMove, {}, 0);
	ASSERT_EQ(Decision.m_Action, EAction::CONNECT);
	EXPECT_EQ(Decision.m_pServer, &aSecondMove[2]);
	m_Engine.OnConnectionEstablished();
	m_Current = m_Other.m_vAddresses[0];
	EXPECT_EQ(Step(4.0, aSecondMove, {}, 0).m_Action, EAction::NONE);
}

TEST_F(QmCrossServerFollow, TwoUnknownMatchingServersRemainAmbiguous)
{
	const SFollowServerCandidate aServers[] = {m_New, m_Other};
	EXPECT_EQ(Step(0.0, aServers).m_Action, EAction::NONE);
	EXPECT_EQ(Step(5.0, std::span(&m_New, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, ClanMatchBeatsAnotherServerWithTheSameName)
{
	const SFollowServerCandidate aServers[] = {Server("127.0.0.1:8305", "Different Clan"), m_New};
	const auto Decision = Step(0.0, aServers);
	ASSERT_EQ(Decision.m_Action, EAction::CONNECT);
	EXPECT_EQ(Decision.m_pServer, &aServers[1]);
}

TEST_F(QmCrossServerFollow, ChangedClanIsAcceptedWhenTheNameIsUnique)
{
	const auto Changed = Server("127.0.0.1:8305", "Changed Clan");
	EXPECT_EQ(Step(0.0, std::span(&Changed, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, ClanFallbackCountsLocalAndRemoteMatches)
{
	m_Current = m_Old.m_vAddresses[0];
	const auto Changed = Server("127.0.0.1:8305", "Changed Clan");
	EXPECT_EQ(Step(0.0, std::span(&Changed, 1), {1, 0}).m_Action, EAction::NONE);
	EXPECT_EQ(Step(1.0, {}, {1, 0}).m_Action, EAction::NONE);
	EXPECT_EQ(Step(2.0, std::span(&Changed, 1)).m_Action, EAction::NOTIFY_PENDING_SWITCH);
}

TEST_F(QmCrossServerFollow, IgnoringClanLeavesTwoNewSameNameServersAmbiguous)
{
	const SFollowServerCandidate aServers[] = {Server("127.0.0.1:8305", "Different Clan"), m_New};
	EXPECT_EQ(Step(0.0, aServers, {}, 3, false, true).m_Action, EAction::NONE);
}

TEST_F(QmCrossServerFollow, FullDestinationNotifiesOnceAndConnectsWhenThereIsRoom)
{
	m_New.m_PlayersFull = true;
	const auto Servers = std::span(&m_New, 1);
	EXPECT_EQ(Step(0.0, Servers).m_Action, EAction::NOTIFY_SERVER_FULL);
	EXPECT_EQ(Step(4.0, Servers).m_Action, EAction::NONE);
	m_New.m_PlayersFull = false;
	EXPECT_EQ(Step(5.0, Servers).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, PasswordedDestinationWaitsForManualConnection)
{
	m_New.m_Passworded = true;
	const auto Servers = std::span(&m_New, 1);
	EXPECT_EQ(Step(0.0, Servers).m_Action, EAction::NOTIFY_PASSWORD_REQUIRED);
	EXPECT_TRUE(m_Engine.IsFollowingServer(m_New.m_vAddresses));
	EXPECT_EQ(Step(20.0, Servers).m_Action, EAction::NONE);
	m_New.m_Passworded = false;
	EXPECT_EQ(Step(21.0, Servers).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, ManualConnectionToThePendingDestinationDoesNotFollowTheOldGhost)
{
	Together(m_Old);
	m_New.m_Passworded = true;
	const SFollowServerCandidate aServers[] = {m_Old, m_New};
	ASSERT_EQ(Step(1.0, aServers).m_Action, EAction::NOTIFY_PASSWORD_REQUIRED);
	EXPECT_FALSE(m_Engine.TryManualConnectToTarget(m_Other.m_vAddresses, 1.0));
	ASSERT_TRUE(m_Engine.TryManualConnectToTarget(m_New.m_vAddresses, 1.0));
	m_Current = m_New.m_vAddresses[0];
	m_Engine.OnConnectionEstablished(&m_Current);
	EXPECT_EQ(Step(2.0, aServers).m_Action, EAction::NONE);
	EXPECT_TRUE(m_Engine.Target().m_Active);
}

TEST_F(QmCrossServerFollow, PasswordRejectionStaysBlockedEvenWhenUnadvertised)
{
	const auto Servers = std::span(&m_New, 1);
	ASSERT_EQ(Step(0.0, Servers).m_Action, EAction::CONNECT);
	m_Engine.OnConnectionFailed(1.0, true);
	EXPECT_EQ(Step(2.0, Servers).m_Action, EAction::NOTIFY_PASSWORD_REQUIRED);
	EXPECT_EQ(Step(20.0, Servers).m_Action, EAction::NONE);
	EXPECT_EQ(Step(21.0, std::span(&m_Other, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, RetryWaitsSixSecondsAfterFailureAndNeverInterruptsAConnection)
{
	const auto Servers = std::span(&m_New, 1);
	ASSERT_EQ(Step(0.0, Servers).m_Action, EAction::CONNECT);
	EXPECT_EQ(Step(15.0, Servers, {}, 3, true).m_Action, EAction::NONE);
	m_Engine.OnConnectionFailed(16.0, false);
	EXPECT_EQ(Step(21.9, Servers).m_Action, EAction::NONE);
	EXPECT_EQ(Step(22.0, Servers).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, ManualOverrideClearsAPendingSwitchAndTarget)
{
	Together(m_Old);
	const auto Servers = std::span(&m_New, 1);
	ASSERT_EQ(Step(1.0, Servers).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	m_Engine.OnManualConnectOrDisconnect();
	EXPECT_EQ(Step(10.0, Servers).m_Action, EAction::NONE);
	EXPECT_FALSE(m_Engine.Target().m_Active);
	EXPECT_TRUE(m_Engine.Target().m_Name.empty());
}

TEST_F(QmCrossServerFollow, RetargetingClearsThePreviousCountdown)
{
	Together(m_Old);
	ASSERT_EQ(Step(1.0, std::span(&m_New, 1)).m_Action, EAction::NOTIFY_PENDING_SWITCH);
	m_Engine.Start("Another", "");
	m_New.m_Players = {};
	m_New.m_Players.Add(m_Engine.Target(), "Another", "");
	EXPECT_EQ(Step(2.0, std::span(&m_New, 1)).m_Action, EAction::CONNECT);
}

TEST_F(QmCrossServerFollow, EmptyNameDoesNotStartFollowingAClan)
{
	m_Engine.Start("", "Clan");
	EXPECT_FALSE(m_Engine.Target().m_Active);
	EXPECT_EQ(Step(0.0, std::span(&m_New, 1)).m_Action, EAction::NONE);
}
