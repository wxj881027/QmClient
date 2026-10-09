// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "websocket_client.h"

#include <base/system.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

extern "C" {
void *qm_ws_connect(const char *pUrl, const char *pProtocol, const char *pHeaders, uint32_t TimeoutMs, size_t Limit, char *pError, size_t ErrorSize, bool (*pCanceled)(void *), void *pUser);
bool qm_ws_send(void *pHandle, int Kind, const unsigned char *pData, size_t Size, char *pError, size_t ErrorSize);
int qm_ws_read(void *pHandle, const unsigned char **ppData, size_t *pSize, char *pError, size_t ErrorSize);
void qm_ws_close(void *pHandle);
}

namespace
{
	class CWebSocketClientRust final : public IQmWebSocketClient
	{
		SCallbacks m_Callbacks;
		SQmWebSocketConnectConfig m_Config;
		STuning m_Tuning{15000, 1000, 60000};
		std::thread m_Worker;
		std::atomic<bool> m_Desired{false};
		std::atomic<EQmWebSocketState> m_State{EQmWebSocketState::IDLE};
		std::atomic<int64_t> m_ConnectedTick{0}, m_MessageTick{0}, m_SendCount{0}, m_RecvCount{0};
		std::atomic<int64_t> m_DroppedIncoming{0}, m_DroppedOutgoing{0}, m_ReconnectCount{0};
		std::atomic<int> m_PingRtt{-1};
		mutable std::mutex m_Mutex;
		std::condition_variable m_Wakeup;
		std::deque<SQmWebSocketMessage> m_Incoming, m_Outgoing;
		size_t m_IncomingBytes = 0, m_OutgoingBytes = 0;
		std::string m_Error;

		void ClearQueues()
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_DroppedIncoming += m_Incoming.size();
			m_DroppedOutgoing += m_Outgoing.size();
			m_Incoming.clear();
			m_Outgoing.clear();
			m_IncomingBytes = m_OutgoingBytes = 0;
		}

		bool Queue(EQmWebSocketMessageType Type, const char *pData, size_t Size)
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			const size_t QueueCapacity = std::clamp(m_Tuning.m_OutgoingQueueCapacity, (size_t)1, (size_t)32);
			if(!m_Desired || m_State != EQmWebSocketState::CONNECTED || (!pData && Size) || Size > m_Config.m_MaxMessageSize || m_Outgoing.size() >= QueueCapacity || m_OutgoingBytes + Size > m_Config.m_MaxMessageSize * 2)
			{
				++m_DroppedOutgoing;
				return false;
			}
			m_Outgoing.push_back({Type, std::string(pData ? pData : "", Size)});
			m_OutgoingBytes += Size;
			return true;
		}

		void Run()
		{
			const std::string Host = m_Config.m_Host.find(':') == std::string::npos ? m_Config.m_Host : "[" + m_Config.m_Host + "]";
			const std::string Url = std::string(m_Config.m_UseTls ? "wss://" : "ws://") + Host + ":" + std::to_string(m_Config.m_Port) + m_Config.m_Path;
			std::string Headers;
			for(const auto &Header : m_Config.m_vHeaders)
				Headers += Header + "\n";
			int Attempt = 0;
			bool EverConnected = false;
			while(m_Desired)
			{
				m_State = EverConnected ? EQmWebSocketState::RECONNECTING : EQmWebSocketState::CONNECTING;
				char aError[512] = {};
				void *pHandle = qm_ws_connect(Url.c_str(), m_Config.m_Protocol.c_str(), Headers.c_str(), m_Config.m_HandshakeTimeoutMs > 0 ? m_Config.m_HandshakeTimeoutMs : 10000, m_Config.m_MaxMessageSize, aError, sizeof(aError), [](void *pUser) { return !static_cast<CWebSocketClientRust *>(pUser)->m_Desired.load(); }, this);
				bool Clean = false;
				int64_t ConnectedAt = 0;
				if(pHandle && m_Desired)
				{
					// 每次握手都开始新会话，旧连接的上下行消息不能跨重连应用。
					ClearQueues();
					ConnectedAt = time_get_impl();
					m_ConnectedTick = ConnectedAt;
					m_State = EQmWebSocketState::CONNECTED;
					m_PingRtt = -1;
					EverConnected = true;
					if(m_Callbacks.m_Open)
						m_Callbacks.m_Open();
					int64_t LastIncoming = ConnectedAt, LastPing = ConnectedAt;
					bool PingPending = false;
					while(true)
					{
						SQmWebSocketMessage Out;
						bool HasOutgoing = false;
						{
							std::lock_guard<std::mutex> Lock(m_Mutex);
							if(!m_Outgoing.empty())
							{
								Out = std::move(m_Outgoing.front());
								m_Outgoing.pop_front();
								m_OutgoingBytes -= Out.m_Data.size();
								HasOutgoing = true;
							}
						}
						if(HasOutgoing)
						{
							if(!qm_ws_send(pHandle, Out.m_Type == EQmWebSocketMessageType::TEXT ? 0 : 1, reinterpret_cast<const unsigned char *>(Out.m_Data.data()), Out.m_Data.size(), aError, sizeof(aError)))
								break;
							++m_SendCount;
						}
						// 主动退出先发送已入队的 stop/leave，再关闭通道。
						if(!m_Desired)
						{
							if(HasOutgoing)
								continue;
							Clean = true;
							break;
						}
						const unsigned char *pData = nullptr;
						size_t Size = 0;
						const int Kind = qm_ws_read(pHandle, &pData, &Size, aError, sizeof(aError));
						const int64_t Now = time_get_impl();
						if(Kind < 0)
						{
							Clean = Kind == -2;
							break;
						}
						if(Kind == 0)
							std::this_thread::sleep_for(std::chrono::milliseconds(10));
						if(Kind > 0)
							LastIncoming = Now;
						if(Kind == 3 && PingPending)
						{
							m_PingRtt = (int)((Now - LastPing) * 1000 / time_freq());
							PingPending = false;
						}
						else if(Kind == 1 || Kind == 2)
						{
							SQmWebSocketMessage Message{Kind == 1 ? EQmWebSocketMessageType::TEXT : EQmWebSocketMessageType::BINARY, std::string(reinterpret_cast<const char *>(pData), Size)};
							m_MessageTick = Now;
							++m_RecvCount;
							if(m_Callbacks.m_Message)
								m_Callbacks.m_Message(Message.m_Type, Message.m_Data.data(), Message.m_Data.size());
							std::lock_guard<std::mutex> Lock(m_Mutex);
							if(m_Incoming.size() >= 64 || m_IncomingBytes + Size > m_Config.m_MaxMessageSize * 2)
							{
								// 丢弃中间状态可能改变协作语义，断线后重新获取权威快照。
								++m_DroppedIncoming;
								str_copy(aError, "接收队列已满");
								break;
							}
							m_IncomingBytes += Size;
							m_Incoming.push_back(std::move(Message));
						}
						if(Now - LastIncoming > (int64_t)std::max(45000, m_Tuning.m_HeartbeatMs * 3) * time_freq() / 1000)
						{
							str_copy(aError, "WebSocket 心跳超时");
							break;
						}
						if(!PingPending && Now - LastPing >= (int64_t)m_Tuning.m_HeartbeatMs * time_freq() / 1000)
						{
							if(!qm_ws_send(pHandle, 2, nullptr, 0, aError, sizeof(aError)))
								break;
							LastPing = Now;
							PingPending = true;
						}
					}
				}
				m_State = m_Desired ? EQmWebSocketState::RECONNECTING : EQmWebSocketState::IDLE;
				qm_ws_close(pHandle);
				ClearQueues();
				if(aError[0])
				{
					{
						std::lock_guard<std::mutex> Lock(m_Mutex);
						m_Error = aError;
					}
					if(m_Callbacks.m_Error)
						m_Callbacks.m_Error(aError);
				}
				if(ConnectedAt && m_Callbacks.m_Disconnected)
					m_Callbacks.m_Disconnected(aError, Clean);
				if(!m_Desired)
					break;
				if(ConnectedAt && time_get_impl() - ConnectedAt >= 30 * time_freq())
					Attempt = 0;
				++m_ReconnectCount;
				const int Delay = QmWebSocketBackoffDelayMs(Attempt++, m_Tuning.m_BackoffBaseMs, m_Tuning.m_BackoffMaxMs);
				std::unique_lock<std::mutex> Lock(m_Mutex);
				m_Wakeup.wait_for(Lock, std::chrono::milliseconds(Delay), [this] { return !m_Desired; });
			}
			m_State = EQmWebSocketState::IDLE;
		}

	public:
		explicit CWebSocketClientRust(SCallbacks Callbacks) :
			m_Callbacks(std::move(Callbacks)) {}
		~CWebSocketClientRust() override { Disconnect(); }
		bool Available() const override { return true; }
		const char *UnavailableReason() const override { return ""; }
		bool Connect(const SQmWebSocketConnectConfig &Config, std::string &Error) override
		{
			if(Config.m_Host.empty() || Config.m_Port < 1 || Config.m_Port > 65535 || Config.m_Path.empty() || Config.m_Path[0] != '/' || Config.m_MaxMessageSize < 128 * 1024 || Config.m_MaxMessageSize > 32 * 1024 * 1024)
			{
				Error = "WebSocket 连接配置无效";
				return false;
			}
			if(Config.m_UseTls && Config.m_AllowInsecureTls)
			{
				Error = "WSS 必须校验服务器证书";
				return false;
			}
			Disconnect();
			m_Config = Config;
			m_Desired = true;
			m_Worker = std::thread([this] { Run(); });
			return true;
		}
		void Disconnect() override
		{
			m_Desired = false;
			m_Wakeup.notify_all();
			if(m_Worker.joinable())
				m_Worker.join();
			m_State = EQmWebSocketState::IDLE;
		}
		void SetTuning(const STuning &Tuning) override
		{
			if(Tuning.m_HeartbeatMs > 0)
				m_Tuning.m_HeartbeatMs = Tuning.m_HeartbeatMs;
			if(Tuning.m_BackoffBaseMs > 0)
				m_Tuning.m_BackoffBaseMs = Tuning.m_BackoffBaseMs;
			if(Tuning.m_BackoffMaxMs > 0)
				m_Tuning.m_BackoffMaxMs = Tuning.m_BackoffMaxMs;
			if(Tuning.m_OutgoingQueueCapacity > 0)
				m_Tuning.m_OutgoingQueueCapacity = std::clamp(Tuning.m_OutgoingQueueCapacity, (size_t)1, (size_t)32);
		}
		bool Desired() const override { return m_Desired; }
		EQmWebSocketState State() const override { return m_State; }
		const char *StateName() const override
		{
			switch(State())
			{
			case EQmWebSocketState::CONNECTING: return "connecting";
			case EQmWebSocketState::CONNECTED: return "connected";
			case EQmWebSocketState::RECONNECTING: return "reconnecting";
			default: return "idle";
			}
		}
		bool SendText(const char *pData, size_t Size) override { return Queue(EQmWebSocketMessageType::TEXT, pData, Size); }
		bool SendBinary(const char *pData, size_t Size) override { return Queue(EQmWebSocketMessageType::BINARY, pData, Size); }
		bool PollMessage(SQmWebSocketMessage &Out) override
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			if(m_Incoming.empty())
				return false;
			Out = std::move(m_Incoming.front());
			m_Incoming.pop_front();
			m_IncomingBytes -= Out.m_Data.size();
			return true;
		}
		size_t PendingMessages() const override
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			return m_Incoming.size();
		}
		int64_t LastConnectedTick() const override { return m_ConnectedTick; }
		int64_t LastMessageTick() const override { return m_MessageTick; }
		int64_t SendCount() const override { return m_SendCount; }
		int64_t RecvCount() const override { return m_RecvCount; }
		int64_t DroppedIncomingCount() const override { return m_DroppedIncoming; }
		int64_t DroppedOutgoingCount() const override { return m_DroppedOutgoing; }
		int64_t ReconnectCount() const override { return m_ReconnectCount; }
		int LastPingRttMs() const override { return m_PingRtt; }
		const char *LastError() const override
		{
			thread_local std::string Error;
			std::lock_guard<std::mutex> Lock(m_Mutex);
			Error = m_Error;
			return Error.c_str();
		}
	};
}

std::unique_ptr<IQmWebSocketClient> CreateQmWebSocketClient(IQmWebSocketClient::SCallbacks Callbacks)
{
	return std::make_unique<CWebSocketClientRust>(std::move(Callbacks));
}
