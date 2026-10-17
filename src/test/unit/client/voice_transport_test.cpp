#include <base/system.h>

#include <engine/shared/websocket_client.h>

#include <game/client/components/qmclient/voice/voice_utils.h>

#include <gtest/gtest.h>

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>

using namespace VoiceUtils;

namespace
{
	class CVoiceTransportTestClient final : public IQmWebSocketClient
	{
	public:
		STuning m_Tuning;
		EQmWebSocketState m_State = EQmWebSocketState::IDLE;
		bool m_Desired = false;
		bool m_SendAccepted = true;
		bool m_ConnectAccepted = true;
		int m_ConnectCalls = 0;
		int m_PollCalls = 0;
		std::string m_LastHost;
		std::function<void()> m_OnPoll;
		int64_t m_ConnectedTick = 0;
		std::deque<SQmWebSocketMessage> m_Incoming;

		bool Available() const override { return true; }
		const char *UnavailableReason() const override { return ""; }
		bool Connect(const SQmWebSocketConnectConfig &Config, std::string &Error) override
		{
			++m_ConnectCalls;
			m_LastHost = Config.m_Host;
			if(!m_ConnectAccepted)
			{
				m_Desired = false;
				m_State = EQmWebSocketState::IDLE;
				Error = "thread creation failed";
				return false;
			}
			m_Desired = true;
			m_State = EQmWebSocketState::CONNECTING;
			return true;
		}
		void Disconnect() override
		{
			m_Desired = false;
			m_State = EQmWebSocketState::IDLE;
			m_Incoming.clear();
		}
		bool Desired() const override { return m_Desired; }
		EQmWebSocketState State() const override { return m_State; }
		const char *StateName() const override { return "test"; }
		bool SendText(const char *, size_t) override { return false; }
		bool SendBinary(const char *, size_t) override { return m_SendAccepted; }
		void SetTuning(const STuning &Tuning) override { m_Tuning = Tuning; }
		bool PollMessage(SQmWebSocketMessage &Out) override
		{
			++m_PollCalls;
			if(m_OnPoll)
				m_OnPoll();
			if(m_Incoming.empty())
				return false;
			Out = std::move(m_Incoming.front());
			m_Incoming.pop_front();
			return true;
		}
		size_t PendingMessages() const override { return m_Incoming.size(); }
		int64_t LastConnectedTick() const override { return m_ConnectedTick; }
		int64_t LastMessageTick() const override { return 0; }
		int64_t SendCount() const override { return 0; }
		int64_t RecvCount() const override { return 0; }
		int64_t DroppedIncomingCount() const override { return 0; }
		int64_t DroppedOutgoingCount() const override { return 0; }
		int64_t ReconnectCount() const override { return 0; }
		int LastPingRttMs() const override { return -1; }
		const char *LastError() const override { return ""; }
	};
}

TEST(VoiceUtils, WebSocketVoiceBoundsOutgoingQueueAndResetsAfterReconnect)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	EXPECT_EQ(pMock->m_Tuning.m_OutgoingQueueCapacity, 8u);
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	EXPECT_TRUE(Transport.Connecting());
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	uint8_t aPacket[VOICE_PACKET_HEADER_SIZE] = {};
	EXPECT_TRUE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	pMock->m_SendAccepted = false;
	EXPECT_FALSE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	pMock->m_State = EQmWebSocketState::RECONNECTING;
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	EXPECT_FALSE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	EXPECT_FALSE(Transport.SendPacket(aPacket, sizeof(aPacket)));
	EXPECT_TRUE(Transport.Update("wss://voice.example/ws", true, 10, 20, VOICE_VERSION));
	pMock->m_SendAccepted = true;
	EXPECT_TRUE(Transport.SendPacket(aPacket, sizeof(aPacket)));
}

TEST(VoiceTransport, FailedConnectRetriesAtTheDeadlineWithUnchangedConfig)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	pMock->m_ConnectAccepted = false;
	CVoiceWebSocketTransport Transport(std::move(pClient));
	EXPECT_TRUE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0));
	EXPECT_EQ(pMock->m_ConnectCalls, 1);
	EXPECT_NE(Transport.LastError()[0], '\0');
	EXPECT_FALSE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, time_freq() - 1));
	EXPECT_EQ(pMock->m_ConnectCalls, 1);
	pMock->m_ConnectAccepted = true;
	EXPECT_FALSE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, time_freq()));
	EXPECT_EQ(pMock->m_ConnectCalls, 2);
	EXPECT_TRUE(Transport.Connecting());
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	EXPECT_TRUE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, time_freq() + 1));
	EXPECT_TRUE(Transport.Connected());
	EXPECT_STREQ(Transport.LastError(), "");
}

TEST(VoiceTransport, RepeatedCreationFailureWaitsBetweenAttempts)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	pMock->m_ConnectAccepted = false;
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, time_freq());
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, time_freq() + 1);
	EXPECT_EQ(pMock->m_ConnectCalls, 2);
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 2 * time_freq());
	EXPECT_EQ(pMock->m_ConnectCalls, 3);
}

TEST(VoiceTransport, ChangedEndpointCanConnectBeforeTheOldRetryDeadline)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	pMock->m_ConnectAccepted = false;
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	pMock->m_ConnectAccepted = true;
	EXPECT_TRUE(Transport.UpdateAt("wss://new.example/ws", true, 10, 20, VOICE_VERSION, 1));
	EXPECT_EQ(pMock->m_ConnectCalls, 2);
	EXPECT_EQ(pMock->m_LastHost, "new.example");
	EXPECT_TRUE(Transport.Connecting());
}

TEST(VoiceTransport, DisabledTransportStopsRetriesAndReenableStartsImmediately)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	pMock->m_ConnectAccepted = false;
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	EXPECT_TRUE(Transport.UpdateAt("wss://voice.example/ws", false, 10, 20, VOICE_VERSION, 1));
	Transport.UpdateAt("wss://voice.example/ws", false, 10, 20, VOICE_VERSION, 3 * time_freq());
	EXPECT_EQ(pMock->m_ConnectCalls, 1);
	EXPECT_FALSE(Transport.Connecting());
	pMock->m_ConnectAccepted = true;
	EXPECT_TRUE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 3 * time_freq()));
	EXPECT_EQ(pMock->m_ConnectCalls, 2);
}

TEST(VoiceTransport, InvalidUrlDoesNotKeepRetryingUntilItChanges)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("http://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	EXPECT_FALSE(Transport.UrlValid());
	Transport.UpdateAt("http://voice.example/ws", true, 10, 20, VOICE_VERSION, 2 * time_freq());
	EXPECT_EQ(pMock->m_ConnectCalls, 0);
	EXPECT_TRUE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 2 * time_freq() + 1));
	EXPECT_EQ(pMock->m_ConnectCalls, 1);
}

TEST(VoiceTransport, ActiveAutomaticReconnectKeepsItsExistingWorker)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	pMock->m_State = EQmWebSocketState::RECONNECTING;
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 100 * time_freq());
	EXPECT_EQ(pMock->m_ConnectCalls, 1);
	EXPECT_TRUE(Transport.Connecting());
}

TEST(VoiceTransport, EndedWorkerCanReconnectWithoutChangingConfig)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 1);
	pMock->m_Desired = false;
	pMock->m_State = EQmWebSocketState::IDLE;
	EXPECT_TRUE(Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, time_freq()));
	EXPECT_EQ(pMock->m_ConnectCalls, 2);
	EXPECT_TRUE(Transport.Connecting());
}

TEST(VoiceTransport, CancellationBeforePollingPreservesTheNextPacket)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 1);
	pMock->m_Incoming.push_back({EQmWebSocketMessageType::BINARY, std::string(VOICE_PACKET_HEADER_SIZE, '\0')});
	bool Canceled = true;
	const auto IsCanceled = [](const void *pValue) { return *static_cast<const bool *>(pValue); };
	SQmWebSocketMessage Message;
	EXPECT_FALSE(Transport.PollPacket(Message, IsCanceled, &Canceled));
	EXPECT_EQ(pMock->m_PollCalls, 0);
	EXPECT_EQ(pMock->m_Incoming.size(), 1U);
	Canceled = false;
	EXPECT_TRUE(Transport.PollPacket(Message, IsCanceled, &Canceled));
	EXPECT_EQ(Message.m_Data.size(), VOICE_PACKET_HEADER_SIZE);
}

TEST(VoiceTransport, InvalidMessageFloodObservesCancellationBeforeDrainingTheQueue)
{
	auto pClient = std::make_unique<CVoiceTransportTestClient>();
	auto *pMock = pClient.get();
	CVoiceWebSocketTransport Transport(std::move(pClient));
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 0);
	pMock->m_State = EQmWebSocketState::CONNECTED;
	++pMock->m_ConnectedTick;
	Transport.UpdateAt("wss://voice.example/ws", true, 10, 20, VOICE_VERSION, 1);
	for(int i = 0; i < 100; ++i)
		pMock->m_Incoming.push_back({EQmWebSocketMessageType::TEXT, "ignored"});
	pMock->m_Incoming.push_back({EQmWebSocketMessageType::BINARY, std::string(VOICE_PACKET_HEADER_SIZE, '\0')});
	bool Canceled = false;
	pMock->m_OnPoll = [&] {
		if(pMock->m_PollCalls == 3)
			Canceled = true;
	};
	const auto IsCanceled = [](const void *pValue) { return *static_cast<const bool *>(pValue); };
	SQmWebSocketMessage Message;
	EXPECT_FALSE(Transport.PollPacket(Message, IsCanceled, &Canceled));
	EXPECT_EQ(pMock->m_PollCalls, 3);
	EXPECT_EQ(pMock->m_Incoming.size(), 98U);
	Canceled = false;
	EXPECT_TRUE(Transport.PollPacket(Message, IsCanceled, &Canceled));
	EXPECT_EQ(Message.m_Data.size(), VOICE_PACKET_HEADER_SIZE);
}
