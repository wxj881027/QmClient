#include <base/system.h>

#include <engine/shared/network.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <chrono>
#include <memory>

namespace
{
	class NetClientAllocation : public ::testing::Test
	{
	protected:
		CNetClient m_Client{};
		NETADDR m_BindAddr{};

		static void SetUpTestSuite()
		{
			net_init();
		}

		void SetUp() override
		{
			m_BindAddr.type = NETTYPE_IPV4;
			m_BindAddr.ip[0] = 127;
			m_BindAddr.ip[3] = 1;
		}

		void TearDown() override
		{
			m_Client.Close();
		}

		void ExpectStunAllocationFailure()
		{
			bool Opened;
			bool AllocationFailed;
			{
				CPredictionAllocationFailure Failure;
				Opened = m_Client.Open(m_BindAddr);
				AllocationFailed = Failure.Triggered();
			}
			EXPECT_TRUE(AllocationFailed);
			EXPECT_FALSE(Opened);
		}
	};
}

TEST_F(NetClientAllocation, StunAllocationFailureLeavesClientClosedAndAllowsRetry)
{
	ExpectStunAllocationFailure();
	EXPECT_EQ(m_Client.m_Socket, nullptr);

	ASSERT_TRUE(m_Client.Open(m_BindAddr));
	EXPECT_NE(m_Client.m_Socket, nullptr);
}

TEST_F(NetClientAllocation, StunAllocationFailurePreservesPreviouslyOpenedSocket)
{
	ASSERT_TRUE(m_Client.Open(m_BindAddr));
	const NETSOCKET PreviousSocket = m_Client.m_Socket;

	ExpectStunAllocationFailure();
	EXPECT_EQ(m_Client.m_Socket, PreviousSocket);
}

TEST_F(NetClientAllocation, ConnlessQueueAllocationFailureSkipsTokenRequestAndAllowsRetry)
{
	ASSERT_TRUE(m_Client.Open(m_BindAddr));

	NETADDR Target = m_BindAddr;
	std::unique_ptr<NETSOCKET_INTERNAL, decltype(&net_udp_close)> pReceiver(nullptr, net_udp_close);
	for(int Attempt = 0; Attempt < 25 && !pReceiver; ++Attempt)
	{
		Target.port = secure_rand_below(65535 - 1024) + 1024;
		pReceiver.reset(net_udp_create(Target));
	}
	ASSERT_NE(pReceiver, nullptr);

	const unsigned char aPayload[] = {'i', 'n', 'f', 'o'};
	CNetChunk Packet{};
	Packet.m_Address = Target;
	Packet.m_Address.type |= NETTYPE_TW7;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;
	Packet.m_DataSize = sizeof(aPayload);
	Packet.m_pData = aPayload;

	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure;
		m_Client.Send(&Packet);
		AllocationFailed = Failure.Triggered();
	}
	ASSERT_TRUE(AllocationFailed);
	EXPECT_EQ(net_socket_read_wait(pReceiver.get(), std::chrono::nanoseconds::zero()), 0);

	EXPECT_EQ(m_Client.Send(&Packet), 0);
	EXPECT_EQ(net_socket_read_wait(pReceiver.get(), std::chrono::seconds(1)), 1);
}

TEST_F(NetClientAllocation, TokenCacheAllocationFailureLeavesCacheReadyForRetry)
{
	CNetTokenCache Cache{};
	constexpr TOKEN Token = 1;

	bool AllocationFailed;
	{
		CPredictionAllocationFailure Failure;
		Cache.AddToken(&m_BindAddr, Token);
		AllocationFailed = Failure.Triggered();
	}
	ASSERT_TRUE(AllocationFailed);
	EXPECT_EQ(Cache.GetToken(&m_BindAddr), (TOKEN)NET_TOKEN_NONE);

	Cache.AddToken(&m_BindAddr, Token);
	EXPECT_EQ(Cache.GetToken(&m_BindAddr), Token);
}
