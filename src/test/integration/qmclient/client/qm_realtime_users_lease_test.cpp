#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qm_realtime_users.h>

#include <gtest/gtest.h>

#include <new>
#include <string>

namespace
{
	using EResult = CQmRealtimeUsersState::EApplyResult;

	class CQmRealtimeUsersLease : public ::testing::Test
	{
	protected:
		CQmRealtimeUsersState m_State;
		SQmClientDistributionSnapshot m_Snapshot;
		int64_t m_Now = 100;
		int m_PublishCalls = 0;
		bool m_Accept = true;
		bool m_Throw = false;

		EResult Apply(bool Full, int Revision, int Base, const char *pName = "Old", int Lease = 20, bool Heartbeat = false, const char *pAddress = "one:8303")
		{
			const std::string Rows = Heartbeat ? R"("servers":[],"players":[])" : std::string(R"("servers":[["one:8303",1,0]],"players":[["p1",")") + pName + R"(","qm","",true]])";
			const std::string Json = std::string(R"({"type":"users_sync","data":{"server_address":")") + pAddress + R"(","full":)" +
						 (Full ? "true" : "false") + R"(,"revision":)" + std::to_string(Revision) + R"(,"base_revision":)" + std::to_string(Base) +
						 R"(,"lease_seconds":)" + std::to_string(Lease) + ',' + Rows + R"(,"removed_servers":[],"removed_players":[]}})";
			SQmRealtimeMessage Message;
			EXPECT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Message));
			return m_State.ApplyAndPublish(Message.m_pPayload.get(), "one:8303", [this](auto Result, int LeaseSeconds) {
				++m_PublishCalls;
				if(m_Throw)
					throw std::bad_alloc();
				return m_Accept && m_Snapshot.Apply(Result, m_Now + LeaseSeconds);
			});
		}

		void Seed(int Revision = 1)
		{
			ASSERT_EQ(Apply(true, Revision, 0), EResult::APPLIED);
			ASSERT_EQ(m_Snapshot.m_ExpireTick, 120);
			ASSERT_EQ(m_PublishCalls, 1);
		}

		void Expire()
		{
			m_Now = 120;
			m_State.RequestExpiredLeaseRefresh(m_Snapshot, m_Now);
		}

		void ExpectLocalName(const char *pName)
		{
			const auto Result = m_State.Result();
			ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1u);
			EXPECT_EQ(Result.m_vLocalServerMarks.front().m_Name, pName);
		}
	};
}

TEST_F(CQmRealtimeUsersLease, FreshPublishedLeaseDoesNotRequestFull)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	m_State.RequestExpiredLeaseRefresh(m_Snapshot, 119);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_FALSE(m_Snapshot.IsStale(119));
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_PublishCalls, 1);
}

TEST_F(CQmRealtimeUsersLease, ExactExpiryRequestsRefreshWithoutDiscardingPublishedState)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	EXPECT_TRUE(m_State.NeedsFull());
	EXPECT_TRUE(m_Snapshot.IsStale(m_Now));
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_State.LeaseSeconds(), 20);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
	EXPECT_EQ(m_Snapshot.m_OnlineUserCount, 1);
	EXPECT_EQ(m_Snapshot.m_vServers.size(), 1u);
	ExpectLocalName("Old");
	EXPECT_EQ(m_PublishCalls, 1);
}

TEST_F(CQmRealtimeUsersLease, RepeatedExpiredChecksKeepOnePendingRefreshAndOldLease)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	for(const int64_t Now : {120, 121, 130})
	{
		SCOPED_TRACE(Now);
		m_State.RequestExpiredLeaseRefresh(m_Snapshot, Now);
		EXPECT_TRUE(m_State.NeedsFull());
		EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
		EXPECT_EQ(m_State.Revision(), 1);
		EXPECT_EQ(m_PublishCalls, 1);
	}
}

TEST_F(CQmRealtimeUsersLease, SameRevisionFullCanRenewExpiredPublishedLease)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	ASSERT_TRUE(m_State.NeedsFull());
	ASSERT_EQ(Apply(true, 1, 0), EResult::APPLIED);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 140);
	EXPECT_FALSE(m_Snapshot.IsStale(m_Now));
	EXPECT_EQ(m_PublishCalls, 2);
}

TEST_F(CQmRealtimeUsersLease, SameRevisionFullWithoutRefreshKeepsExistingIgnoreRule)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	m_Now = 110;
	m_State.RequestExpiredLeaseRefresh(m_Snapshot, m_Now);
	EXPECT_EQ(Apply(true, 1, 0), EResult::IGNORED);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
	EXPECT_EQ(m_PublishCalls, 1);
}

TEST_F(CQmRealtimeUsersLease, ValidDeltaCanRenewLeaseWhileRefreshIsPending)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	ASSERT_TRUE(m_State.NeedsFull());
	ASSERT_EQ(Apply(false, 2, 1, "Next"), EResult::APPLIED);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 2);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 140);
	ExpectLocalName("Next");
	EXPECT_EQ(m_PublishCalls, 2);
}

TEST_F(CQmRealtimeUsersLease, SameRevisionHeartbeatClearsRefreshOnlyAfterPublishing)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	ASSERT_EQ(Apply(false, 1, 1, "", 10, true), EResult::APPLIED);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_State.LeaseSeconds(), 10);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 130);
	ExpectLocalName("Old");
	EXPECT_EQ(m_PublishCalls, 2);
}

TEST_F(CQmRealtimeUsersLease, OlderFullCannotRollBackPendingRefresh)
{
	ASSERT_NO_FATAL_FAILURE(Seed(3));
	Expire();
	EXPECT_EQ(Apply(true, 2, 0), EResult::IGNORED);
	EXPECT_TRUE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 3);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
	EXPECT_EQ(m_PublishCalls, 1);
}

TEST_F(CQmRealtimeUsersLease, WrongContextCannotSatisfyPendingRefresh)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	EXPECT_EQ(Apply(true, 1, 0, "Other", 20, false, "other:8303"), EResult::IGNORED);
	EXPECT_TRUE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
	ExpectLocalName("Old");
	EXPECT_EQ(m_PublishCalls, 1);
}

TEST_F(CQmRealtimeUsersLease, FailedFullPublicationPreservesOldSnapshotAndRequiresFullRecovery)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	m_Accept = false;
	EXPECT_EQ(Apply(true, 1, 0), EResult::RESYNC);
	EXPECT_TRUE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_State.LeaseSeconds(), 20);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
	EXPECT_EQ(m_PublishCalls, 2);
	m_Accept = true;
	EXPECT_EQ(Apply(false, 2, 1, "Next"), EResult::RESYNC);
	EXPECT_EQ(m_PublishCalls, 2);
	ASSERT_EQ(Apply(true, 1, 0), EResult::APPLIED);
	EXPECT_FALSE(m_State.NeedsFull());
	ASSERT_EQ(Apply(false, 2, 1, "Next"), EResult::APPLIED);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 140);
	ExpectLocalName("Next");
	EXPECT_EQ(m_PublishCalls, 4);
}

TEST_F(CQmRealtimeUsersLease, ThrowingHeartbeatPublicationKeepsSnapshotAndCanRecoverWithFull)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	m_Throw = true;
	EXPECT_EQ(Apply(false, 1, 1, "", 10, true), EResult::RESYNC);
	EXPECT_TRUE(m_State.NeedsFull());
	EXPECT_EQ(m_State.Revision(), 1);
	EXPECT_EQ(m_State.LeaseSeconds(), 20);
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 120);
	EXPECT_EQ(m_PublishCalls, 2);
	m_Throw = false;
	ASSERT_EQ(Apply(true, 1, 0), EResult::APPLIED);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_EQ(m_Snapshot.m_ExpireTick, 140);
}

TEST_F(CQmRealtimeUsersLease, ResetDoesNotRequestRefreshFromPreviousConnectionSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(Seed());
	Expire();
	ASSERT_TRUE(m_State.NeedsFull());
	m_State.Reset();
	m_State.RequestExpiredLeaseRefresh(m_Snapshot, 121);
	EXPECT_EQ(m_State.Revision(), 0);
	EXPECT_FALSE(m_State.NeedsFull());
	EXPECT_FALSE(m_State.Result().m_Parsed);
	EXPECT_TRUE(m_Snapshot.IsStale(121));
}

TEST_F(CQmRealtimeUsersLease, ExpiredLegacySnapshotDoesNotCreateModernFullSubscriptionRequest)
{
	const std::string Json = R"({"type":"users","data":{"server_address":"users","users":[{"server_address":"one:8303","player_name":"Legacy"}]}})";
	SQmRealtimeMessage Message;
	ASSERT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Message));
	SQmClientUsersParseResult Result;
	ASSERT_TRUE(ParseQmClientUsersJson(Message.m_pPayload.get(), "one:8303", Result));
	ASSERT_TRUE(m_Snapshot.Apply(Result, 120));
	m_State.RequestExpiredLeaseRefresh(m_Snapshot, 120);
	EXPECT_TRUE(m_Snapshot.IsStale(120));
	EXPECT_EQ(m_Snapshot.m_OnlineUserCount, 1);
	EXPECT_EQ(m_State.Revision(), 0);
	EXPECT_FALSE(m_State.NeedsFull());
}
