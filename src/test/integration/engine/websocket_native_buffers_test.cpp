#include <engine/shared/websocket_native_state.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <string>

#if defined(CONF_WEBSOCKETS)
#include <base/system.h>
#include <libwebsockets.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#endif

namespace
{
	class CWebsocketNativeBuffersTest : public ::testing::Test
	{
	protected:
		int m_Token = 0;
		std::unique_ptr<CWebsocketNativeState> m_pState;
		CWebsocketNativeState::SSession *m_pSession = nullptr;
		NETADDR m_Addr = NETADDR_ZEROED;
		void SetUp() override
		{
			// 不使用LWS或真实连接；调用实际owner及实际回收环缓冲。
			m_pState = std::make_unique<CWebsocketNativeState>();
			m_Addr.type = NETTYPE_WEBSOCKET_IPV4;
			m_Addr.port = 8303;
			m_pSession = m_pState->TryRegister(reinterpret_cast<lws *>(&m_Token), m_Addr);
			ASSERT_NE(m_pSession, nullptr);
		}
	};
}

TEST_F(CWebsocketNativeBuffersTest, ConstructedSessionQueueKeepsBinaryPayloadAndPadding)
{
	const std::string Payload("a\0b", 3);
	ASSERT_TRUE(m_pState->QueueSend(*m_pSession, Payload.data(), Payload.size(), 16, 4));
	auto *pChunk = m_pSession->m_SendBuffer.First();
	ASSERT_NE(pChunk, nullptr);
	EXPECT_EQ(pChunk->m_Size, Payload.size());
	EXPECT_EQ(pChunk->m_Read, 0u);
	EXPECT_EQ(pChunk->m_Addr, m_Addr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pChunk->m_aData + 16), Payload.size()), Payload);
	for(size_t Index = 0; Index < 16; ++Index)
		EXPECT_EQ(pChunk->m_aData[Index], 0);
	for(size_t Index = 19; Index < 23; ++Index)
		EXPECT_EQ(pChunk->m_aData[Index], 0);
	EXPECT_EQ(m_pSession->m_SendBuffer.PopFirst(), 1);
	EXPECT_EQ(m_pSession->m_SendBuffer.First(), nullptr);
}

TEST_F(CWebsocketNativeBuffersTest, ReceiveQueueKeepsBinaryPayloadAndAddress)
{
	const std::string Payload("x\0y", 3);
	ASSERT_TRUE(m_pState->Receive(m_Addr, Payload.data(), Payload.size()));
	auto *pChunk = m_pState->RecvBuffer().First();
	ASSERT_NE(pChunk, nullptr);
	EXPECT_EQ(pChunk->m_Size, Payload.size());
	EXPECT_EQ(pChunk->m_Read, 0u);
	EXPECT_EQ(pChunk->m_Addr, m_Addr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pChunk->m_aData), Payload.size()), Payload);
}

TEST_F(CWebsocketNativeBuffersTest, ReceiveLengthOverflowKeepsAlreadyQueuedChunk)
{
	ASSERT_TRUE(m_pState->Receive(m_Addr, "old", 3));
	auto *pOld = m_pState->RecvBuffer().First();
	EXPECT_FALSE(m_pState->Receive(m_Addr, "x", std::numeric_limits<size_t>::max()));
	EXPECT_EQ(m_pState->RecvBuffer().First(), pOld);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pOld->m_aData), pOld->m_Size), "old");
}

TEST_F(CWebsocketNativeBuffersTest, SendLengthOrPaddingOverflowKeepsAlreadyQueuedChunk)
{
	ASSERT_TRUE(m_pState->QueueSend(*m_pSession, "old", 3, 16, 4));
	auto *pOld = m_pSession->m_SendBuffer.First();
	const auto Max = std::numeric_limits<size_t>::max();
	const std::array<std::array<size_t, 3>, 3> aaSizes{{{Max, 16, 4}, {1, Max, 4}, {1, 16, Max}}};
	for(const auto &Sizes : aaSizes)
	{
		EXPECT_FALSE(m_pState->QueueSend(*m_pSession, "x", Sizes[0], Sizes[1], Sizes[2]));
		EXPECT_EQ(m_pSession->m_SendBuffer.First(), pOld);
	}
}

TEST_F(CWebsocketNativeBuffersTest, SendChunkBeyondRingCapacityIsRejectedWithoutRecyclingOldData)
{
	ASSERT_TRUE(m_pState->QueueSend(*m_pSession, "old", 3, 16, 4));
	auto *pOld = m_pSession->m_SendBuffer.First();
	EXPECT_FALSE(m_pState->QueueSend(*m_pSession, "x", NET_CONN_BUFFERSIZE, 16, 4));
	EXPECT_EQ(m_pSession->m_SendBuffer.First(), pOld);
}

TEST_F(CWebsocketNativeBuffersTest, ReceiveChunkBeyondRingCapacityIsRejectedWithoutRecyclingOldData)
{
	ASSERT_TRUE(m_pState->Receive(m_Addr, "old", 3));
	auto *pOld = m_pState->RecvBuffer().First();
	EXPECT_FALSE(m_pState->Receive(m_Addr, "x", (MAX_CLIENTS * 2) * NET_CONN_BUFFERSIZE));
	EXPECT_EQ(m_pState->RecvBuffer().First(), pOld);
}

TEST_F(CWebsocketNativeBuffersTest, MissingSendPayloadIsRejectedBeforeQueueMutation)
{
	EXPECT_FALSE(m_pState->QueueSend(*m_pSession, nullptr, 1, 16, 4));
	EXPECT_EQ(m_pSession->m_SendBuffer.First(), nullptr);
}

TEST_F(CWebsocketNativeBuffersTest, MissingReceivePayloadIsRejectedBeforeQueueMutation)
{
	EXPECT_FALSE(m_pState->Receive(m_Addr, nullptr, 1));
	EXPECT_EQ(m_pState->RecvBuffer().First(), nullptr);
}

TEST_F(CWebsocketNativeBuffersTest, EmptySendKeepsExistingZeroLengthQueuePolicy)
{
	ASSERT_TRUE(m_pState->QueueSend(*m_pSession, nullptr, 0, 16, 4));
	auto *pChunk = m_pSession->m_SendBuffer.First();
	ASSERT_NE(pChunk, nullptr);
	EXPECT_EQ(pChunk->m_Size, 0u);
}

TEST_F(CWebsocketNativeBuffersTest, EmptyReceiveKeepsExistingZeroLengthQueuePolicy)
{
	ASSERT_TRUE(m_pState->Receive(m_Addr, nullptr, 0));
	auto *pChunk = m_pState->RecvBuffer().First();
	ASSERT_NE(pChunk, nullptr);
	EXPECT_EQ(pChunk->m_Size, 0u);
}

TEST_F(CWebsocketNativeBuffersTest, ContextResetDiscardsReceiveDataAndOldSessionQueues)
{
	ASSERT_TRUE(m_pState->Receive(m_Addr, "old", 3));
	ASSERT_TRUE(m_pState->QueueSend(*m_pSession, "old", 3, 16, 4));
	m_pState->Reset();
	m_pSession = nullptr;
	EXPECT_EQ(m_pState->RecvBuffer().First(), nullptr);
	EXPECT_TRUE(m_pState->Ports().empty());
	ASSERT_TRUE(m_pState->Receive(m_Addr, "new", 3));
	auto *pNew = m_pState->RecvBuffer().First();
	ASSERT_NE(pNew, nullptr);
	EXPECT_EQ(std::string(reinterpret_cast<const char *>(pNew->m_aData), pNew->m_Size), "new");
}

#if defined(CONF_WEBSOCKETS) && defined(LWS_WITH_IPV6)
namespace
{
	struct SWebsocketHandshake
	{
		char m_aHost[NETADDR_MAXSTRSIZE] = {};
		bool m_Established = false;

		static int Callback(lws *pWsi, lws_callback_reasons Reason, void *, void *, size_t)
		{
			if(Reason != LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION && Reason != LWS_CALLBACK_ESTABLISHED)
				return 0;
			auto *pHandshake = static_cast<SWebsocketHandshake *>(lws_context_user(lws_get_context(pWsi)));
			if(Reason == LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION)
				return lws_hdr_copy(pWsi, pHandshake->m_aHost, sizeof(pHandshake->m_aHost), WSI_TOKEN_HOST) < 0 ? 1 : 0;
			pHandshake->m_Established = true;
			return 0;
		}
	};
}

TEST(WebsocketNativeHandshake, Ipv6LoopbackUsesBracketedHostWithPort)
{
	if(std::getenv("http_proxy") || std::getenv("socks_proxy"))
		GTEST_SKIP() << "libwebsockets would pick up a proxy from the environment";

	SWebsocketHandshake Handshake;
	static const lws_protocols s_aProtocols[] = {{"binary", SWebsocketHandshake::Callback, 0}, {}};
	lws_context_creation_info Info{};
	Info.iface = "::1";
	Info.port = 0;
	Info.options = LWS_SERVER_OPTION_FAIL_UPON_UNABLE_TO_BIND | LWS_SERVER_OPTION_IPV6_V6ONLY_VALUE | LWS_SERVER_OPTION_IPV6_V6ONLY_MODIFY;
	Info.vhost_name = "qm-native-ws-test";
	Info.gid = -1;
	Info.uid = -1;
	Info.protocols = s_aProtocols;
	Info.user = &Handshake;
	const std::unique_ptr<lws_context, decltype(&lws_context_destroy)> pServer(lws_create_context(&Info), lws_context_destroy);
	if(!pServer)
		GTEST_SKIP() << "could not open an IPv6 loopback listener";
	auto *pVhost = lws_get_vhost_by_name(pServer.get(), Info.vhost_name);
	ASSERT_NE(pVhost, nullptr);
	const int Port = lws_get_vhost_listen_port(pVhost);
	ASSERT_GT(Port, 0);
	ASSERT_LE(Port, 65535);

	NETADDR BindAddr;
	ASSERT_EQ(net_addr_from_str(&BindAddr, "[::1]"), 0);
	BindAddr.type = NETTYPE_WEBSOCKET_IPV6;
	const std::unique_ptr<NETSOCKET_INTERNAL, decltype(&net_udp_close)> pClient(net_udp_create(BindAddr), net_udp_close);
	ASSERT_NE(pClient, nullptr);
	NETADDR Peer = BindAddr;
	Peer.port = static_cast<unsigned short>(Port);
	const unsigned char Payload = 1;
	// 首次发送发起连接，握手完成前可以返回失败。
	net_udp_send(pClient.get(), &Peer, &Payload, sizeof(Payload));

	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!Handshake.m_Established && std::chrono::steady_clock::now() < Deadline)
	{
		ASSERT_GE(lws_service(pServer.get(), -1), 0);
		NETADDR Sender;
		unsigned char *pData = nullptr;
		net_udp_recv(pClient.get(), &Sender, &pData);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	ASSERT_TRUE(Handshake.m_Established);
	EXPECT_EQ(std::string(Handshake.m_aHost), "[::1]:" + std::to_string(Port));
}
#endif
