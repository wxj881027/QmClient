#include <base/system.h>

#include <engine/shared/network.h>

#include <test/support/prediction_allocation_failure.h>

#include <gtest/gtest.h>

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
