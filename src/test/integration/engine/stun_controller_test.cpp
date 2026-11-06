#include <base/system.h>

#include <engine/shared/network.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace
{
	class CStunOperations : public CStun::IOperations
	{
	public:
		struct CRequest
		{
			NETSOCKET m_Socket;
			NETADDR m_Address;
			std::vector<unsigned char> m_vData;
		};
		int64_t m_Now = 100000;
		unsigned m_RandomCalls = 0;
		bool m_FailSend = false;
		std::vector<CRequest> m_vRequests;

		int64_t Now() override { return m_Now; }
		int64_t Frequency() override { return 1000; }
		void RandomFill(void *pData, unsigned Size) override
		{
			++m_RandomCalls;
			for(unsigned i = 0; i < Size; ++i)
				((unsigned char *)pData)[i] = m_RandomCalls + i;
		}
		int Send(NETSOCKET Socket, const NETADDR *pAddr, const void *pData, int Size) override
		{
			const auto *pBytes = (const unsigned char *)pData;
			m_vRequests.push_back({Socket, *pAddr, {pBytes, pBytes + Size}});
			return m_FailSend ? -1 : Size;
		}
	};

	class CStunControllerTest : public testing::Test
	{
	protected:
		CStunOperations m_Operations;
		CStun m_Stun{nullptr, &m_Operations};
		NETADDR m_Server4{};
		NETADDR m_Server6{};

		void SetUp() override
		{
			ASSERT_EQ(net_addr_from_str(&m_Server4, "192.0.2.10:3478"), 0);
			ASSERT_EQ(net_addr_from_str(&m_Server6, "[2001:db8::10]:3478"), 0);
		}

		void Start()
		{
			m_Stun.FeedStunServer(m_Server4);
			m_Stun.Update();
			ASSERT_EQ(m_Operations.m_vRequests.size(), 1U);
		}

		void Advance(int64_t Milliseconds)
		{
			m_Operations.m_Now += Milliseconds;
			m_Stun.Update();
		}

		CONNECTIVITY Status(int NetType = NETTYPE_IPV4)
		{
			NETADDR Address{};
			return m_Stun.GetConnectivity(NetType, &Address);
		}

		std::vector<unsigned char> Response(size_t RequestIndex = 0, bool Error = false)
		{
			const auto &Request = m_Operations.m_vRequests.at(RequestIndex).m_vData;
			if(Request.size() != 20)
			{
				ADD_FAILURE() << "unexpected STUN request size: " << Request.size();
				return {};
			}
			std::vector<unsigned char> Result(Request.begin(), Request.begin() + 20);
			Result[0] = 1;
			Result[1] = Error ? 0x11 : 1;
			if(Error)
				Result.insert(Result.end(), {0, 9, 0, 4, 0, 0, 5, 0});
			else
			{
				// 包含需要填充的可选属性，联查真实解析器的属性遍历。
				Result.insert(Result.end(), {0x80, 0x22, 0, 3, 's', 'r', 'v', 0xfe});
				Result.insert(Result.end(), {0, 0x20, 0, 8, 0, 1, 0x01, 0x7d, 0xe1, 0x12, 0xa6, 0x43});
			}
			Result[2] = (Result.size() - 20) >> 8;
			Result[3] = (Result.size() - 20) & 0xff;
			return Result;
		}

		bool Feed(NETADDR Source, std::vector<unsigned char> &Message)
		{
			return m_Stun.OnPacket(Source, Message.data(), Message.size());
		}

		void ExpectAddress()
		{
			NETADDR Address{};
			ASSERT_EQ(m_Stun.GetConnectivity(NETTYPE_IPV4, &Address), CONNECTIVITY::ADDRESS_KNOWN);
			NETADDR Expected{};
			ASSERT_EQ(net_addr_from_str(&Expected, "192.0.2.1:8303"), 0);
			EXPECT_EQ(net_addr_comp(&Address, &Expected), 0);
		}
	};
}

TEST_F(CStunControllerTest, UnconfiguredControllerDoesNotSendOrPublishAddress)
{
	m_Stun.Refresh();
	m_Stun.Update();
	EXPECT_EQ(Status(), CONNECTIVITY::UNKNOWN);
	EXPECT_EQ(Status(NETTYPE_IPV6), CONNECTIVITY::UNKNOWN);
	EXPECT_TRUE(m_Operations.m_vRequests.empty());
}

TEST_F(CStunControllerTest, RequestUsesProductionEncoderAndBorrowedSocket)
{
	Start();
	const auto &Request = m_Operations.m_vRequests[0];
	ASSERT_EQ(Request.m_vData.size(), 20U);
	const unsigned char aHeader[] = {0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42};
	EXPECT_EQ(mem_comp(Request.m_vData.data(), aHeader, sizeof(aHeader)), 0);
	EXPECT_EQ(Request.m_Socket, nullptr);
	EXPECT_EQ(net_addr_comp(&Request.m_Address, &m_Server4), 0);
	EXPECT_EQ(Status(), CONNECTIVITY::CHECKING);
}

TEST_F(CStunControllerTest, SuccessPublishesAddressAndStopsAutomaticRetries)
{
	Start();
	auto Message = Response();
	ASSERT_TRUE(Feed(m_Server4, Message));
	ExpectAddress();
	Advance(1000000);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 1U);
	EXPECT_FALSE(Feed(m_Server4, Message));
	ExpectAddress();
}

TEST_F(CStunControllerTest, DifferentSourceAddressOrPortCannotChangeState)
{
	Start();
	auto Message = Response();
	NETADDR Wrong = m_Server4;
	++Wrong.port;
	EXPECT_FALSE(Feed(Wrong, Message));
	Wrong = m_Server4;
	++Wrong.ip[3];
	EXPECT_FALSE(Feed(Wrong, Message));
	EXPECT_EQ(Status(), CONNECTIVITY::CHECKING);
	ASSERT_TRUE(Feed(m_Server4, Message));
	ExpectAddress();
}

TEST_F(CStunControllerTest, MalformedPacketDoesNotConsumeCurrentTransaction)
{
	Start();
	auto Message = Response();
	Message.pop_back();
	EXPECT_FALSE(Feed(m_Server4, Message));
	EXPECT_EQ(Status(), CONNECTIVITY::CHECKING);
	Message = Response();
	ASSERT_TRUE(Feed(m_Server4, Message));
	ExpectAddress();
}

TEST_F(CStunControllerTest, NegativeSizeIsRejectedBeforeParsing)
{
	Start();
	EXPECT_FALSE(m_Stun.OnPacket(m_Server4, nullptr, -1));
	EXPECT_EQ(Status(), CONNECTIVITY::CHECKING);
}

TEST_F(CStunControllerTest, StandardErrorMarksReachableAndKeepsRetrySchedule)
{
	Start();
	auto Error = Response(0, true);
	ASSERT_TRUE(Feed(m_Server4, Error));
	EXPECT_EQ(Status(), CONNECTIVITY::REACHABLE);
	Advance(999);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 1U);
	Advance(1);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 2U);
}

TEST_F(CStunControllerTest, RetryRotatesTransactionAndRejectsOldResponse)
{
	Start();
	auto Old = Response();
	Advance(1000);
	ASSERT_EQ(m_Operations.m_vRequests.size(), 2U);
	EXPECT_NE(mem_comp(m_Operations.m_vRequests[0].m_vData.data() + 8, m_Operations.m_vRequests[1].m_vData.data() + 8, 12), 0);
	EXPECT_FALSE(Feed(m_Server4, Old));
	EXPECT_EQ(Status(), CONNECTIVITY::CHECKING);
	auto Current = Response(1);
	ASSERT_TRUE(Feed(m_Server4, Current));
	ExpectAddress();
}

TEST_F(CStunControllerTest, RetryBackoffHonorsDeadlinesAndCapsAt512Seconds)
{
	Start();
	const std::array<int64_t, 11> Waits = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 512};
	size_t Requests = 1;
	for(int64_t Seconds : Waits)
	{
		SCOPED_TRACE(Seconds);
		Advance(Seconds * 1000 - 1);
		EXPECT_EQ(m_Operations.m_vRequests.size(), Requests);
		Advance(1);
		EXPECT_EQ(m_Operations.m_vRequests.size(), ++Requests);
	}
}

TEST_F(CStunControllerTest, ThreeConsecutiveSendFailuresStopUntilRefresh)
{
	m_Operations.m_FailSend = true;
	Start();
	Advance(1000);
	Advance(2000);
	ASSERT_EQ(m_Operations.m_vRequests.size(), 3U);
	EXPECT_EQ(Status(), CONNECTIVITY::UNREACHABLE);
	Advance(1000000);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 3U);
	m_Stun.Refresh();
	m_Stun.Update();
	EXPECT_EQ(m_Operations.m_vRequests.size(), 4U);
	Advance(32000);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 5U);
}

TEST_F(CStunControllerTest, SuccessfulSendResetsConsecutiveFailureCount)
{
	m_Operations.m_FailSend = true;
	Start();
	Advance(1000);
	m_Operations.m_FailSend = false;
	Advance(2000);
	m_Operations.m_FailSend = true;
	Advance(4000);
	Advance(8000);
	ASSERT_EQ(m_Operations.m_vRequests.size(), 5U);
	Advance(16000);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 6U);
	Advance(1000000);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 6U);
}

TEST_F(CStunControllerTest, RefreshAfterSuccessSendsAgainAndErrorClearsKnownAddress)
{
	Start();
	auto Success = Response();
	ASSERT_TRUE(Feed(m_Server4, Success));
	m_Stun.Refresh();
	ExpectAddress();
	m_Stun.Update();
	ASSERT_EQ(m_Operations.m_vRequests.size(), 2U);
	EXPECT_FALSE(Feed(m_Server4, Success));
	auto Error = Response(1, true);
	ASSERT_TRUE(Feed(m_Server4, Error));
	EXPECT_EQ(Status(), CONNECTIVITY::REACHABLE);
}

TEST_F(CStunControllerTest, RefeedingSameServerDoesNotResetPendingDeadline)
{
	Start();
	Advance(500);
	m_Stun.FeedStunServer(m_Server4);
	m_Stun.Update();
	EXPECT_EQ(m_Operations.m_vRequests.size(), 1U);
	Advance(500);
	EXPECT_EQ(m_Operations.m_vRequests.size(), 2U);
}

TEST_F(CStunControllerTest, ServerChangeRetainsAddressWhileRejectingPreviousServer)
{
	Start();
	auto Previous = Response();
	ASSERT_TRUE(Feed(m_Server4, Previous));
	NETADDR Next = m_Server4;
	++Next.port;
	m_Stun.FeedStunServer(Next);
	ExpectAddress();
	EXPECT_FALSE(Feed(m_Server4, Previous));
	m_Stun.Update();
	ASSERT_EQ(m_Operations.m_vRequests.size(), 2U);
	EXPECT_EQ(net_addr_comp(&m_Operations.m_vRequests[1].m_Address, &Next), 0);
	auto Current = Response(1);
	ASSERT_TRUE(Feed(Next, Current));
	ExpectAddress();
}

TEST_F(CStunControllerTest, AddressFamiliesKeepTransactionsAndStatusIndependent)
{
	m_Stun.FeedStunServer(m_Server4);
	m_Stun.FeedStunServer(m_Server6);
	m_Stun.Update();
	ASSERT_EQ(m_Operations.m_vRequests.size(), 2U);
	EXPECT_EQ(net_addr_comp(&m_Operations.m_vRequests[0].m_Address, &m_Server6), 0);
	auto V6Transaction = Response(0);
	EXPECT_FALSE(Feed(m_Server4, V6Transaction));
	ASSERT_TRUE(Feed(m_Server6, V6Transaction));
	EXPECT_EQ(Status(NETTYPE_IPV6), CONNECTIVITY::ADDRESS_KNOWN);
	EXPECT_EQ(Status(), CONNECTIVITY::CHECKING);
	auto V4Transaction = Response(1);
	ASSERT_TRUE(Feed(m_Server4, V4Transaction));
	ExpectAddress();
}

TEST_F(CStunControllerTest, ErrorResponseGracePeriodEndsAtThirtySeconds)
{
	Start();
	auto Error = Response(0, true);
	ASSERT_TRUE(Feed(m_Server4, Error));
	for(int64_t Wait : {1000, 2000, 4000, 8000})
		Advance(Wait);
	ASSERT_EQ(m_Operations.m_vRequests.size(), 5U);
	m_Operations.m_Now = 129999;
	EXPECT_EQ(Status(), CONNECTIVITY::REACHABLE);
	++m_Operations.m_Now;
	EXPECT_EQ(Status(), CONNECTIVITY::UNREACHABLE);
}

TEST_F(CStunControllerTest, UnsupportedNetworkTypeIsIgnored)
{
	NETADDR Unsupported{};
	Unsupported.type = NETTYPE_ALL;
	m_Stun.FeedStunServer(Unsupported);
	m_Stun.Update();
	std::vector<unsigned char> Empty;
	EXPECT_FALSE(Feed(Unsupported, Empty));
	EXPECT_TRUE(m_Operations.m_vRequests.empty());
	EXPECT_EQ(Status(), CONNECTIVITY::UNKNOWN);
}
