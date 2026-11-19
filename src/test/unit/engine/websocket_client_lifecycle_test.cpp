#include <base/str.h>

#include <engine/shared/websocket_client_runtime.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using namespace std::chrono_literals;

	template<typename TCondition>
	bool WaitUntil(TCondition Condition)
	{
		const auto Deadline = std::chrono::steady_clock::now() + 2s;
		while(!Condition() && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::sleep_for(1ms);
		return Condition();
	}

	struct SFakeNetwork
	{
		enum class EStartFailure
		{
			NONE,
			SYSTEM,
			ALLOCATION,
			UNKNOWN,
		};
		struct SConnection
		{
			int m_Generation;
			bool m_Delivered = false;
		};

		std::atomic<EStartFailure> m_StartFailure{EStartFailure::NONE};
		std::atomic<int> m_Starts{0}, m_Connects{0}, m_Closes{0}, m_Live{0};
		std::atomic<bool> m_ThrowConnect{false}, m_ThrowRead{false}, m_ThrowUnknownRead{false}, m_ThrowSend{false};
		std::atomic<bool> m_RejectConnections{false}, m_EmitFirstMessage{false};
		std::atomic<bool> m_WaitForCancellation{false}, m_HandshakeEntered{false}, m_SawCancellation{false};
		std::atomic<bool> m_ClosedWhileConnected{false};
		IQmWebSocketClient *m_pClient = nullptr;
		std::mutex m_SentMutex;
		std::vector<std::string> m_vSent;

		SQmWebSocketRuntime Runtime()
		{
			return {
				this,
				[](void *pUser, const char *, const char *, const char *, uint32_t, size_t, char *pError, size_t ErrorSize, bool (*pCanceled)(void *), void *pCancelUser) -> void * {
					auto &Network = *static_cast<SFakeNetwork *>(pUser);
					const int Generation = ++Network.m_Connects;
					if(Network.m_ThrowConnect.exchange(false))
						throw std::bad_alloc();
					if(Network.m_RejectConnections)
					{
						str_copy(pError, "handshake rejected", ErrorSize);
						return nullptr;
					}
					if(Network.m_WaitForCancellation)
					{
						Network.m_HandshakeEntered = true;
						Network.m_SawCancellation = WaitUntil([&] { return pCanceled(pCancelUser); });
					}
					// 模拟在取消的同时完成握手，返回的资源仍须被关闭。
					auto *pConnection = new SConnection{Generation};
					++Network.m_Live;
					return pConnection;
				},
				[](void *pUser, void *, int, const unsigned char *pData, size_t Size, char *, size_t) {
					auto &Network = *static_cast<SFakeNetwork *>(pUser);
					if(Network.m_ThrowSend.exchange(false))
						throw std::runtime_error("send failure");
					std::lock_guard<std::mutex> Lock(Network.m_SentMutex);
					Network.m_vSent.emplace_back(pData ? reinterpret_cast<const char *>(pData) : "", Size);
					return true;
				},
				[](void *pUser, void *pHandle, const unsigned char **ppData, size_t *pSize, char *, size_t) {
					auto &Network = *static_cast<SFakeNetwork *>(pUser);
					if(Network.m_ThrowRead.exchange(false))
						throw std::bad_alloc();
					if(Network.m_ThrowUnknownRead.exchange(false))
						throw 7;
					auto &Connection = *static_cast<SConnection *>(pHandle);
					if(Network.m_EmitFirstMessage && Connection.m_Generation == 1 && !Connection.m_Delivered)
					{
						static const unsigned char s_aMessage[] = {'v', 'o', 'i', 'c', 'e'};
						Connection.m_Delivered = true;
						*ppData = s_aMessage;
						*pSize = sizeof(s_aMessage);
						return 2;
					}
					return 0;
				},
				[](void *pUser, void *pHandle) noexcept {
					auto &Network = *static_cast<SFakeNetwork *>(pUser);
					if(Network.m_pClient->State() == EQmWebSocketState::CONNECTED)
						Network.m_ClosedWhileConnected = true;
					delete static_cast<SConnection *>(pHandle);
					--Network.m_Live;
					++Network.m_Closes;
				},
				[](void *pUser, std::function<void()> Run) {
					auto &Network = *static_cast<SFakeNetwork *>(pUser);
					++Network.m_Starts;
					switch(Network.m_StartFailure.exchange(EStartFailure::NONE))
					{
					case EStartFailure::SYSTEM:
						throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
					case EStartFailure::ALLOCATION:
						throw std::bad_alloc();
					case EStartFailure::UNKNOWN:
						throw 7;
					case EStartFailure::NONE:
						break;
					}
					return std::thread(std::move(Run));
				},
			};
		}
	};

	class QmWebSocketLifecycle : public ::testing::Test
	{
	protected:
		SFakeNetwork m_Network;
		std::atomic<int> m_Opens{0}, m_Errors{0}, m_Disconnections{0}, m_Messages{0};
		std::atomic<bool> m_DisconnectedWithLiveHandle{false};
		std::unique_ptr<IQmWebSocketClient> m_pClient;
		std::string m_ConnectError;

		IQmWebSocketClient::SCallbacks Callbacks()
		{
			IQmWebSocketClient::SCallbacks Result;
			Result.m_Open = [this] { ++m_Opens; };
			Result.m_Error = [this](const std::string &) { ++m_Errors; };
			Result.m_Disconnected = [this](const std::string &, bool) {
				if(m_Network.m_Live != 0)
					m_DisconnectedWithLiveHandle = true;
				++m_Disconnections;
			};
			Result.m_Message = [this](EQmWebSocketMessageType, const char *, size_t) { ++m_Messages; };
			return Result;
		}

		void MakeClient(IQmWebSocketClient::SCallbacks Callbacks)
		{
			m_pClient = CreateQmWebSocketClient(std::move(Callbacks), m_Network.Runtime());
			m_Network.m_pClient = m_pClient.get();
			IQmWebSocketClient::STuning Tuning;
			Tuning.m_HeartbeatMs = 60000;
			Tuning.m_BackoffBaseMs = 1;
			Tuning.m_BackoffMaxMs = 1;
			m_pClient->SetTuning(Tuning);
		}

		bool Connect()
		{
			SQmWebSocketConnectConfig Config;
			Config.m_Host = "example.invalid";
			return m_pClient->Connect(Config, m_ConnectError);
		}

		bool WaitConnected(int Attempts = 1)
		{
			return WaitUntil([&] {
				return m_Network.m_Connects >= Attempts && m_pClient->State() == EQmWebSocketState::CONNECTED;
			});
		}

		void SetUp() override { MakeClient(Callbacks()); }
		void TearDown() override
		{
			// 回调只捕获 fixture，析构前先停止线程，避免访问已退出的测试局部变量。
			m_pClient.reset();
			EXPECT_EQ(m_Network.m_Live.load(), 0);
			EXPECT_FALSE(m_Network.m_ClosedWhileConnected.load());
			EXPECT_FALSE(m_DisconnectedWithLiveHandle.load());
		}
	};
}

TEST_F(QmWebSocketLifecycle, NormalConnectionSendsAndPublishesReceivedMessages)
{
	m_Network.m_EmitFirstMessage = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	ASSERT_TRUE(m_pClient->SendText("hello", 5));
	ASSERT_TRUE(WaitUntil([&] { return m_pClient->SendCount() == 1 && m_pClient->PendingMessages() == 1; }));
	SQmWebSocketMessage Message;
	ASSERT_TRUE(m_pClient->PollMessage(Message));
	EXPECT_EQ(Message.m_Type, EQmWebSocketMessageType::BINARY);
	EXPECT_EQ(Message.m_Data, "voice");
	EXPECT_FALSE(m_pClient->PollMessage(Message));
	m_pClient->Disconnect();
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_EQ(m_Network.m_vSent, std::vector<std::string>{"hello"});
}

TEST_F(QmWebSocketLifecycle, ThreadCreationFailureClearsIntentAndAllowsAnotherConnect)
{
	m_Network.m_StartFailure = SFakeNetwork::EStartFailure::SYSTEM;
	EXPECT_FALSE(Connect());
	EXPECT_FALSE(m_pClient->Desired());
	EXPECT_EQ(m_pClient->State(), EQmWebSocketState::IDLE);
	EXPECT_EQ(m_Network.m_Connects.load(), 0);
	EXPECT_FALSE(m_ConnectError.empty());
	EXPECT_NE(m_pClient->LastError()[0], '\0');
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	EXPECT_EQ(m_Network.m_Starts.load(), 2);
}

TEST_F(QmWebSocketLifecycle, ThreadCreationAllocationFailureReturnsAnError)
{
	m_Network.m_StartFailure = SFakeNetwork::EStartFailure::ALLOCATION;
	EXPECT_FALSE(Connect());
	EXPECT_FALSE(m_pClient->Desired());
	EXPECT_EQ(m_pClient->State(), EQmWebSocketState::IDLE);
	EXPECT_NE(m_pClient->LastError()[0], '\0');
	EXPECT_EQ(m_Network.m_Connects.load(), 0);
}

TEST_F(QmWebSocketLifecycle, UnknownThreadCreationFailureReturnsAnError)
{
	m_Network.m_StartFailure = SFakeNetwork::EStartFailure::UNKNOWN;
	EXPECT_FALSE(Connect());
	EXPECT_FALSE(m_pClient->Desired());
	EXPECT_EQ(m_pClient->State(), EQmWebSocketState::IDLE);
	EXPECT_FALSE(m_ConnectError.empty());
	EXPECT_EQ(m_Network.m_Connects.load(), 0);
}

TEST_F(QmWebSocketLifecycle, ExceptionBeforeAHandleIsCreatedRetriesWithoutLosingIntent)
{
	m_Network.m_ThrowConnect = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_TRUE(m_pClient->Desired());
	EXPECT_EQ(m_Network.m_Live.load(), 1);
	EXPECT_EQ(m_Network.m_Closes.load(), 0);
	EXPECT_EQ(m_Errors.load(), 1);
}

TEST_F(QmWebSocketLifecycle, OpenCallbackExceptionClosesTheHandleBeforeRetrying)
{
	auto Hooks = Callbacks();
	Hooks.m_Open = [this] {
		if(++m_Opens == 1)
			throw std::runtime_error("open failure");
	};
	MakeClient(std::move(Hooks));
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitUntil([&] { return m_Opens >= 2; }));
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_EQ(m_Network.m_Live.load(), 1);
	EXPECT_EQ(m_Disconnections.load(), 1);
	EXPECT_NE(str_find(m_pClient->LastError(), "open failure"), nullptr);
}

TEST_F(QmWebSocketLifecycle, ReceiveAllocationExceptionClosesTheHandleAndReconnects)
{
	m_Network.m_ThrowRead = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_EQ(m_Network.m_Live.load(), 1);
	EXPECT_EQ(m_Errors.load(), 1);
	EXPECT_TRUE(m_pClient->Desired());
}

TEST_F(QmWebSocketLifecycle, UnknownReceiveExceptionStillClosesTheHandleAndReconnects)
{
	m_Network.m_ThrowUnknownRead = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_EQ(m_Disconnections.load(), 1);
	EXPECT_NE(m_pClient->LastError()[0], '\0');
}

TEST_F(QmWebSocketLifecycle, MessageCallbackExceptionDoesNotPublishTheFailedMessage)
{
	m_Network.m_EmitFirstMessage = true;
	auto Hooks = Callbacks();
	Hooks.m_Message = [this](EQmWebSocketMessageType, const char *, size_t) {
		++m_Messages;
		throw std::runtime_error("message failure");
	};
	MakeClient(std::move(Hooks));
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Messages.load(), 1);
	EXPECT_EQ(m_pClient->PendingMessages(), 0U);
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_NE(str_find(m_pClient->LastError(), "message failure"), nullptr);
}

TEST_F(QmWebSocketLifecycle, SendExceptionClosesTheHandleBeforeReconnect)
{
	m_Network.m_ThrowSend = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	ASSERT_TRUE(m_pClient->SendText("hello", 5));
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_EQ(m_pClient->SendCount(), 0);
	EXPECT_NE(str_find(m_pClient->LastError(), "send failure"), nullptr);
}

TEST_F(QmWebSocketLifecycle, ErrorCallbackExceptionStillNotifiesDisconnectionAndRetries)
{
	m_Network.m_ThrowRead = true;
	auto Hooks = Callbacks();
	Hooks.m_Error = [this](const std::string &) {
		++m_Errors;
		throw std::bad_alloc();
	};
	MakeClient(std::move(Hooks));
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Errors.load(), 1);
	EXPECT_EQ(m_Disconnections.load(), 1);
	EXPECT_NE(str_find(m_pClient->LastError(), "error callback"), nullptr);
}

TEST_F(QmWebSocketLifecycle, UnknownDisconnectedCallbackExceptionDoesNotStopRetrying)
{
	m_Network.m_ThrowRead = true;
	auto Hooks = Callbacks();
	Hooks.m_Disconnected = [this](const std::string &, bool) {
		++m_Disconnections;
		throw 7;
	};
	MakeClient(std::move(Hooks));
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Disconnections.load(), 1);
	EXPECT_TRUE(m_pClient->Desired());
	EXPECT_NE(str_find(m_pClient->LastError(), "disconnected callback"), nullptr);
}

TEST_F(QmWebSocketLifecycle, FailedSessionMessagesAreDiscardedBeforeReconnect)
{
	m_Network.m_EmitFirstMessage = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitUntil([&] { return m_pClient->PendingMessages() == 1; }));
	m_Network.m_ThrowRead = true;
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_pClient->PendingMessages(), 0U);
	EXPECT_EQ(m_pClient->DroppedIncomingCount(), 1);
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
}

TEST_F(QmWebSocketLifecycle, CancelDuringHandshakeClosesAConcurrentSuccessfulHandle)
{
	m_Network.m_WaitForCancellation = true;
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitUntil([&] { return m_Network.m_HandshakeEntered.load(); }));
	m_pClient->Disconnect();
	EXPECT_TRUE(m_Network.m_SawCancellation.load());
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	EXPECT_EQ(m_Network.m_Live.load(), 0);
	EXPECT_EQ(m_Opens.load(), 0);
	EXPECT_EQ(m_pClient->State(), EQmWebSocketState::IDLE);
}

TEST_F(QmWebSocketLifecycle, RepeatedDisconnectAllowsAReplacementConnection)
{
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	m_pClient->Disconnect();
	m_pClient->Disconnect();
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected(2));
	EXPECT_EQ(m_Network.m_Starts.load(), 2);
	EXPECT_EQ(m_Network.m_Live.load(), 1);
}

TEST_F(QmWebSocketLifecycle, DestructionJoinsAndClosesTheActiveConnection)
{
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	m_pClient.reset();
	EXPECT_EQ(m_Network.m_Live.load(), 0);
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
}

TEST_F(QmWebSocketLifecycle, InvalidReplacementConfigPreservesTheActiveConnection)
{
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	SQmWebSocketConnectConfig InvalidConfig;
	EXPECT_FALSE(m_pClient->Connect(InvalidConfig, m_ConnectError));
	EXPECT_FALSE(m_ConnectError.empty());
	EXPECT_TRUE(m_pClient->Desired());
	EXPECT_EQ(m_pClient->State(), EQmWebSocketState::CONNECTED);
	EXPECT_EQ(m_Network.m_Closes.load(), 0);
}

TEST_F(QmWebSocketLifecycle, DisconnectSendsAlreadyQueuedLeaveBeforeClosing)
{
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitConnected());
	ASSERT_TRUE(m_pClient->SendText("leave", 5));
	m_pClient->Disconnect();
	EXPECT_EQ(m_pClient->SendCount(), 1);
	EXPECT_EQ(m_Network.m_vSent, std::vector<std::string>{"leave"});
	EXPECT_EQ(m_Network.m_Closes.load(), 1);
}

TEST_F(QmWebSocketLifecycle, DisconnectWakesTheReconnectDelay)
{
	m_Network.m_RejectConnections = true;
	IQmWebSocketClient::STuning Tuning;
	Tuning.m_BackoffBaseMs = 5000;
	Tuning.m_BackoffMaxMs = 5000;
	m_pClient->SetTuning(Tuning);
	ASSERT_TRUE(Connect());
	ASSERT_TRUE(WaitUntil([&] { return m_Errors > 0; }));
	EXPECT_TRUE(m_pClient->Desired());
	const auto Start = std::chrono::steady_clock::now();
	m_pClient->Disconnect();
	EXPECT_LT(std::chrono::steady_clock::now() - Start, 2s);
	EXPECT_EQ(m_Network.m_Connects.load(), 1);
	EXPECT_EQ(m_pClient->State(), EQmWebSocketState::IDLE);
}
