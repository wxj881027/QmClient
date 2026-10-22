#include <base/str.h>

#include <winsock2.h>

#include <windows.h>

#include <bcrypt.h>
#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_cdp_client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{
	bool WebSocketAccept(const std::string &Key, std::string &Accept)
	{
		struct CAlgorithm
		{
			BCRYPT_ALG_HANDLE m_Handle = nullptr;
			~CAlgorithm()
			{
				if(m_Handle)
					BCryptCloseAlgorithmProvider(m_Handle, 0);
			}
		} Algorithm;
		struct CHash
		{
			BCRYPT_HASH_HANDLE m_Handle = nullptr;
			~CHash()
			{
				if(m_Handle)
					BCryptDestroyHash(m_Handle);
			}
		};
		if(BCryptOpenAlgorithmProvider(&Algorithm.m_Handle, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0)
			return false;
		DWORD ObjectSize = 0, Written = 0;
		if(BCryptGetProperty(Algorithm.m_Handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&ObjectSize), sizeof(ObjectSize), &Written, 0) != 0)
			return false;
		std::vector<UCHAR> vObject(ObjectSize);
		CHash Hash;
		const std::string Input = Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		std::array<UCHAR, 20> aDigest{};
		if(BCryptCreateHash(Algorithm.m_Handle, &Hash.m_Handle, vObject.data(), ObjectSize, nullptr, 0, 0) != 0 ||
			BCryptHashData(Hash.m_Handle, reinterpret_cast<PUCHAR>(const_cast<char *>(Input.data())), static_cast<ULONG>(Input.size()), 0) != 0 ||
			BCryptFinishHash(Hash.m_Handle, aDigest.data(), static_cast<ULONG>(aDigest.size()), 0) != 0)
			return false;
		char aBase64[64];
		str_base64(aBase64, sizeof(aBase64), aDigest.data(), aDigest.size());
		Accept = aBase64;
		return true;
	}

	// 只模拟本机 WebSocket 对端；独立的五秒上限也约束旧实现回归时的等待。
	class CLoopbackPeer
	{
		SOCKET m_Listener = INVALID_SOCKET;
		bool m_Winsock = false;
		uint16_t m_Port = 0;
		std::atomic_bool m_Stop{false};
		std::thread m_Thread;
		std::chrono::steady_clock::time_point m_Deadline;

		bool Ready(SOCKET Socket, bool Write)
		{
			while(!m_Stop.load() && std::chrono::steady_clock::now() < m_Deadline)
			{
				fd_set Set;
				FD_ZERO(&Set);
				FD_SET(Socket, &Set);
				timeval Timeout{0, 20000};
				const int Result = select(0, Write ? nullptr : &Set, Write ? &Set : nullptr, nullptr, &Timeout);
				if(Result != 0)
					return Result > 0;
			}
			return false;
		}
		bool Read(SOCKET Socket, void *pData, size_t Size)
		{
			char *pBytes = static_cast<char *>(pData);
			while(Size != 0 && Ready(Socket, false))
			{
				const int Received = recv(Socket, pBytes, static_cast<int>(Size), 0);
				if(Received == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
					continue;
				if(Received <= 0)
					return false;
				pBytes += Received;
				Size -= Received;
			}
			return Size == 0;
		}
		bool Send(SOCKET Socket, const std::string &Bytes)
		{
			size_t Sent = 0;
			while(Sent < Bytes.size() && Ready(Socket, true))
			{
				const int Count = send(Socket, Bytes.data() + Sent, static_cast<int>(Bytes.size() - Sent), 0);
				if(Count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
					continue;
				if(Count <= 0)
					return false;
				Sent += Count;
			}
			return Sent == Bytes.size();
		}
		bool ReadClientFrame(SOCKET Socket)
		{
			std::array<unsigned char, 2> aHeader{};
			if(!Read(Socket, aHeader.data(), aHeader.size()) || (aHeader[1] & 0x80) == 0)
				return false;
			size_t Size = aHeader[1] & 0x7f;
			if(Size == 126)
			{
				std::array<unsigned char, 2> aLength{};
				if(!Read(Socket, aLength.data(), aLength.size()))
					return false;
				Size = static_cast<size_t>(aLength[0]) * 256 + aLength[1];
			}
			else if(Size == 127)
				return false;
			std::vector<char> vFrame(Size + 4);
			return Read(Socket, vFrame.data(), vFrame.size());
		}
		void Hold(SOCKET Socket)
		{
			std::array<char, 1024> aBuffer{};
			while(Ready(Socket, false))
			{
				if(recv(Socket, aBuffer.data(), static_cast<int>(aBuffer.size()), 0) <= 0)
					return;
			}
		}

	public:
		enum class EScenario
		{
			NOTIFICATION,
			IDLE,
			PARTIAL_FRAME,
			CONTROL_FLOOD,
			CANCEL_COMMAND
		};
		std::atomic_bool m_CommandSeen{false};
		CLoopbackPeer()
		{
			WSADATA Data{};
			m_Winsock = WSAStartup(MAKEWORD(2, 2), &Data) == 0;
			if(!m_Winsock)
				return;
			m_Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(m_Listener == INVALID_SOCKET)
				return;
			sockaddr_in Address{};
			Address.sin_family = AF_INET;
			Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if(bind(m_Listener, reinterpret_cast<sockaddr *>(&Address), sizeof(Address)) != 0 || listen(m_Listener, 1) != 0)
				return;
			int Length = sizeof(Address);
			u_long Nonblocking = 1;
			if(getsockname(m_Listener, reinterpret_cast<sockaddr *>(&Address), &Length) == 0 && ioctlsocket(m_Listener, FIONBIO, &Nonblocking) == 0)
				m_Port = ntohs(Address.sin_port);
		}
		~CLoopbackPeer()
		{
			Stop();
			if(m_Listener != INVALID_SOCKET)
				closesocket(m_Listener);
			if(m_Winsock)
				WSACleanup();
		}
		void Stop()
		{
			m_Stop.store(true);
			if(m_Thread.joinable())
				m_Thread.join();
		}
		bool IsValid() const { return m_Port != 0; }
		std::string Url() const { return "ws://127.0.0.1:" + std::to_string(m_Port) + "/devtools/page/test"; }
		void Start(EScenario Scenario, std::atomic_bool *pSessionStop)
		{
			m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			m_Thread = std::thread([this, Scenario, pSessionStop] {
				if(!Ready(m_Listener, false))
					return;
				struct CConnection
				{
					SOCKET m_Socket;
					~CConnection()
					{
						if(m_Socket != INVALID_SOCKET)
							closesocket(m_Socket);
					}
				} Connection{accept(m_Listener, nullptr, nullptr)};
				const SOCKET Socket = Connection.m_Socket;
				u_long Nonblocking = 1;
				if(Socket == INVALID_SOCKET || ioctlsocket(Socket, FIONBIO, &Nonblocking) != 0)
					return;
				std::string Request;
				while(Request.size() < 8192 && Request.find("\r\n\r\n") == std::string::npos)
				{
					char Byte;
					if(!Read(Socket, &Byte, 1))
						return;
					Request.push_back(Byte);
				}
				const std::string Prefix = "Sec-WebSocket-Key: ";
				const size_t Start = Request.find(Prefix);
				if(Start == std::string::npos)
					return;
				const size_t End = Request.find("\r\n", Start);
				std::string Accept;
				if(End == std::string::npos || !WebSocketAccept(Request.substr(Start + Prefix.size(), End - Start - Prefix.size()), Accept))
					return;
				std::string Response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + Accept + "\r\n\r\n";
				if(Scenario == EScenario::NOTIFICATION)
				{
					const std::string Notification = R"({"method":"Runtime.ready"})";
					Response.push_back(static_cast<char>(0x81));
					Response.push_back(static_cast<char>(Notification.size()));
					Response += Notification;
				}
				else if(Scenario == EScenario::PARTIAL_FRAME)
					Response.append("\x81\x7e\x00\x7eX", 5);
				if(!Send(Socket, Response))
					return;
				if(Scenario == EScenario::CONTROL_FLOOD)
				{
					std::string PongFrames;
					for(int i = 0; i < 1024; ++i)
						PongFrames.append("\x8a\x00", 2);
					while(Send(Socket, PongFrames)) {}
					return;
				}
				if(Scenario == EScenario::CANCEL_COMMAND && ReadClientFrame(Socket))
				{
					m_CommandSeen.store(true);
					pSessionStop->store(true);
				}
				Hold(Socket);
			});
		}
	};

	class QmNeteaseCdpSocket : public ::testing::Test
	{
	protected:
		CLoopbackPeer m_Peer;
		std::atomic_bool m_Stop{false};
		QmNeteaseCdp::CCdpSession m_Session;
		~QmNeteaseCdpSocket() override
		{
			m_Session.Close();
			m_Peer.Stop();
		}
		void SetUp() override
		{
			ASSERT_TRUE(m_Peer.IsValid());
			m_Session.SetStopFlag(&m_Stop);
		}
	};
}

TEST_F(QmNeteaseCdpSocket, HandshakeBufferedNotificationReachesCallback)
{
	m_Peer.Start(CLoopbackPeer::EScenario::NOTIFICATION, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));
	std::string Notification;
	m_Session.SetNotificationCallback([&](std::string_view Value) { Notification = Value; });
	EXPECT_TRUE(m_Session.Pump(100));
	EXPECT_EQ(Notification, R"({"method":"Runtime.ready"})");
}

TEST_F(QmNeteaseCdpSocket, IdleTimeoutKeepsConnectionAvailable)
{
	m_Peer.Start(CLoopbackPeer::EScenario::IDLE, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));
	EXPECT_TRUE(m_Session.Pump(80));
	EXPECT_TRUE(m_Session.IsConnected());
}

TEST_F(QmNeteaseCdpSocket, PartialFrameTimesOutAndClosesConnection)
{
	m_Peer.Start(CLoopbackPeer::EScenario::PARTIAL_FRAME, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));
	const auto Started = std::chrono::steady_clock::now();
	EXPECT_FALSE(m_Session.Pump(80));
	EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count(), 2.0);
	EXPECT_FALSE(m_Session.IsConnected());
}

TEST_F(QmNeteaseCdpSocket, ContinuousControlFramesCannotExtendPumpForever)
{
	m_Peer.Start(CLoopbackPeer::EScenario::CONTROL_FLOOD, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));
	const auto Started = std::chrono::steady_clock::now();
	m_Session.Pump(80);
	EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count(), 2.0);
}

TEST_F(QmNeteaseCdpSocket, StopDuringPendingCommandClosesBeforeCommandDeadline)
{
	m_Peer.Start(CLoopbackPeer::EScenario::CANCEL_COMMAND, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));
	const auto Started = std::chrono::steady_clock::now();
	EXPECT_FALSE(m_Session.Command("Runtime.enable", "{}", nullptr, 5000));
	EXPECT_TRUE(m_Peer.m_CommandSeen.load());
	EXPECT_FALSE(m_Session.IsConnected());
	EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count(), 2.0);
}
