#include <engine/client/server_info_update.h>

#include <gtest/gtest.h>

#include <new>

namespace
{
	CServerInfo Info(const char *pName, const char *pPlayer)
	{
		CServerInfo Result{};
		str_copy(Result.m_aName, pName);
		str_copy(Result.m_aMap, "Previous map");
		Result.m_MapCrc = 123;
		Result.m_MapSize = 456;
		Result.m_ReceivedPackets = 5;
		Result.m_vClients.emplace_back();
		str_copy(Result.m_vClients.back().m_aName, pPlayer);
		return Result;
	}

	void ExpectPrevious(const CServerInfo &Target, const CServerInfo::CClient *pClients)
	{
		EXPECT_STREQ(Target.m_aName, "Previous");
		EXPECT_STREQ(Target.m_aMap, "Previous map");
		EXPECT_EQ(Target.m_MapCrc, 123);
		EXPECT_EQ(Target.m_MapSize, 456);
		EXPECT_EQ(Target.m_ReceivedPackets, 5U);
		ASSERT_EQ(Target.m_vClients.size(), 1U);
		EXPECT_EQ(Target.m_vClients.data(), pClients);
		EXPECT_STREQ(Target.m_vClients.front().m_aName, "Old player");
	}
}

TEST(ServerInfoUpdate, PublishesPreparedCandidateWithoutChangingSource)
{
	auto Target = Info("Previous", "Old player");
	const auto Source = Info("Fresh", "New player");
	const auto *pOldClients = Target.m_vClients.data();
	int Calls = 0;
	server_info::Update(Target, Source, [&](CServerInfo &Candidate) {
		++Calls;
		ExpectPrevious(Target, pOldClients);
		EXPECT_STREQ(Candidate.m_aName, "Fresh");
		str_copy(Candidate.m_aMap, "Actual map");
		Candidate.m_MapCrc = 987;
		Candidate.m_MapSize = 654;
	});
	EXPECT_EQ(Calls, 1);
	EXPECT_STREQ(Target.m_aName, "Fresh");
	EXPECT_STREQ(Target.m_aMap, "Actual map");
	EXPECT_EQ(Target.m_MapCrc, 987);
	EXPECT_EQ(Target.m_MapSize, 654);
	ASSERT_EQ(Target.m_vClients.size(), 1U);
	EXPECT_STREQ(Target.m_vClients.front().m_aName, "New player");
	EXPECT_STREQ(Source.m_aMap, "Previous map");
	EXPECT_EQ(Source.m_MapCrc, 123);
}

TEST(ServerInfoUpdate, AllocationExceptionDuringPreparationPreservesOldFieldsAndClientStorage)
{
	auto Target = Info("Previous", "Old player");
	const auto Source = Info("Fresh", "New player");
	const auto *pOldClients = Target.m_vClients.data();
	EXPECT_THROW(server_info::Update(Target, Source, [](CServerInfo &Candidate) {
		str_copy(Candidate.m_aMap, "Unpublished map");
		Candidate.m_MapCrc = 999;
		Candidate.m_ReceivedPackets = 64;
		Candidate.m_vClients.clear();
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	ExpectPrevious(Target, pOldClients);
}

TEST(ServerInfoUpdate, UnknownPreparationExceptionPropagatesWithoutPublishing)
{
	auto Target = Info("Previous", "Old player");
	const auto Source = Info("Fresh", "New player");
	const auto *pOldClients = Target.m_vClients.data();
	EXPECT_THROW(server_info::Update(Target, Source, [](CServerInfo &Candidate) {
		Candidate.m_MapSize = 1;
		str_copy(Candidate.m_vClients.front().m_aName, "Unpublished player");
		throw 7;
	}),
		int);
	ExpectPrevious(Target, pOldClients);
}

TEST(ServerInfoUpdate, SameSourceCanPublishAfterPreparationFailure)
{
	auto Target = Info("Previous", "Old player");
	const auto Source = Info("Fresh", "New player");
	EXPECT_THROW(server_info::Update(Target, Source, [](CServerInfo &) { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_NO_THROW(server_info::Update(Target, Source, [](CServerInfo &) {}));
	EXPECT_STREQ(Target.m_aName, "Fresh");
	ASSERT_EQ(Target.m_vClients.size(), 1U);
	EXPECT_STREQ(Target.m_vClients.front().m_aName, "New player");
}

TEST(ServerInfoUpdate, SelfSourceCanBePreparedWithoutMutatingPublishedStorage)
{
	auto Target = Info("Previous", "Old player");
	const auto *pOldClients = Target.m_vClients.data();
	server_info::Update(Target, Target, [&](CServerInfo &Candidate) {
		Candidate.m_vClients.clear();
		ExpectPrevious(Target, pOldClients);
		str_copy(Candidate.m_aName, "Self updated");
	});
	EXPECT_STREQ(Target.m_aName, "Self updated");
	EXPECT_TRUE(Target.m_vClients.empty());
}

TEST(ServerInfoUpdate, FailedSelfPreparationKeepsSourceAndTargetIntact)
{
	auto Target = Info("Previous", "Old player");
	const auto *pOldClients = Target.m_vClients.data();
	EXPECT_THROW(server_info::Update(Target, Target, [](CServerInfo &Candidate) {
		Candidate.m_vClients.clear();
		throw std::bad_alloc();
	}),
		std::bad_alloc);
	ExpectPrevious(Target, pOldClients);
}

TEST(ServerInfoUpdate, EmptySourceRemovesPreviousPlayersOnSuccessfulPublication)
{
	auto Target = Info("Previous", "Old player");
	const CServerInfo Empty{};
	server_info::Update(Target, Empty, [](CServerInfo &) {});
	EXPECT_TRUE(Target.m_vClients.empty());
	EXPECT_STREQ(Target.m_aName, "");
	EXPECT_EQ(Target.m_ReceivedPackets, 0U);
}
