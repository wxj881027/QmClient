#include <base/str.h>

#include <engine/external/json-parser/json.h>

#include <winsock2.h>

#include <windows.h>

#include <bcrypt.h>
#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_cdp_client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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

	// 只模拟本机 HTTP 发现和 WebSocket 对端。对端的等待上限为五秒。
	class CLoopbackPeer
	{
		struct CConnection
		{
			SOCKET m_Socket;
			~CConnection()
			{
				if(m_Socket != INVALID_SOCKET)
					closesocket(m_Socket);
			}
		};

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
		bool ReadClientFrame(SOCKET Socket, std::string *pPayload = nullptr)
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
			std::vector<unsigned char> vFrame(Size + 4);
			if(!Read(Socket, vFrame.data(), vFrame.size()))
				return false;
			if(pPayload != nullptr)
			{
				pPayload->resize(Size);
				for(size_t Index = 0; Index < Size; ++Index)
					(*pPayload)[Index] = static_cast<char>(vFrame[Index + 4] ^ vFrame[Index % 4]);
			}
			return true;
		}
		json_int_t ReadCommandId(SOCKET Socket, std::string_view Method)
		{
			std::string Payload;
			if(!ReadClientFrame(Socket, &Payload))
				return 0;
			const std::unique_ptr<json_value, decltype(&json_value_free)> Root(json_parse(Payload.data(), Payload.size()), json_value_free);
			if(!Root)
				return 0;
			const auto &CdpMethod = (*Root)["method"];
			const auto &Id = (*Root)["id"];
			if(CdpMethod.type != json_string || std::string_view(CdpMethod.u.string.ptr, CdpMethod.u.string.length) != Method || Id.type != json_integer)
				return 0;
			return Id.u.integer;
		}
		bool ReplyToCommand(SOCKET Socket, std::string_view Method, std::string_view Fields)
		{
			const json_int_t Id = ReadCommandId(Socket, Method);
			if(Id <= 0)
				return false;
			std::string Message = "{\"id\":" + std::to_string(Id);
			Message.append(Fields);
			Message.push_back('}');
			std::string Frame;
			return AppendTextFrame(Frame, Message) && Send(Socket, Frame);
		}
		static bool AppendTextFrame(std::string &Frame, std::string_view Message)
		{
			if(Message.size() > 0xffff)
				return false;
			Frame.push_back('\x81');
			if(Message.size() < 126)
				Frame.push_back(static_cast<char>(Message.size()));
			else
			{
				Frame.push_back('\x7e');
				Frame.push_back(static_cast<char>(Message.size() >> 8));
				Frame.push_back(static_cast<char>(Message.size() & 0xff));
			}
			Frame.append(Message);
			return true;
		}
		SOCKET AcceptConnection()
		{
			if(!Ready(m_Listener, false))
				return INVALID_SOCKET;
			const SOCKET Socket = accept(m_Listener, nullptr, nullptr);
			if(Socket == INVALID_SOCKET)
				return INVALID_SOCKET;
			u_long Nonblocking = 1;
			if(ioctlsocket(Socket, FIONBIO, &Nonblocking) == 0)
				return Socket;
			closesocket(Socket);
			return INVALID_SOCKET;
		}
		std::string ReadRequest(SOCKET Socket)
		{
			std::string Request;
			while(Request.size() < 8192)
			{
				char Byte;
				if(!Read(Socket, &Byte, 1))
					break;
				Request.push_back(Byte);
				if(Request.ends_with("\r\n\r\n"))
					return Request;
			}
			return {};
		}
		static std::string UpgradeResponse(const std::string &Request)
		{
			const std::string Prefix = "Sec-WebSocket-Key: ";
			const size_t Start = Request.find(Prefix);
			if(Start == std::string::npos)
				return {};
			const size_t End = Request.find("\r\n", Start);
			std::string Accept;
			if(End == std::string::npos || !WebSocketAccept(Request.substr(Start + Prefix.size(), End - Start - Prefix.size()), Accept))
				return {};
			return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + Accept + "\r\n\r\n";
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
			MALFORMED_NOTIFICATION,
			IDLE,
			PARTIAL_FRAME,
			CONTROL_FLOOD,
			CANCEL_COMMAND,
			MISSING_COMMAND_RESULT,
			EVALUATION_EXCEPTION
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
		uint16_t Port() const { return m_Port; }
		std::string Url() const { return "ws://127.0.0.1:" + std::to_string(m_Port) + "/devtools/page/test"; }
		void Start(EScenario Scenario, std::atomic_bool *pSessionStop)
		{
			m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			m_Thread = std::thread([this, Scenario, pSessionStop] {
				const CConnection Connection{AcceptConnection()};
				const SOCKET Socket = Connection.m_Socket;
				if(Socket == INVALID_SOCKET)
					return;
				std::string Response = UpgradeResponse(ReadRequest(Socket));
				if(Response.empty())
					return;
				if(Scenario == EScenario::MALFORMED_NOTIFICATION)
				{
					std::string Malformed = R"({"method":"Runtime.incomplete"})";
					Malformed.push_back('\0');
					Malformed += "invalid";
					if(!AppendTextFrame(Response, Malformed))
						return;
				}
				if(Scenario == EScenario::NOTIFICATION || Scenario == EScenario::MALFORMED_NOTIFICATION)
				{
					if(!AppendTextFrame(Response, R"({"method":"Runtime.ready"})"))
						return;
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
				if(Scenario == EScenario::MISSING_COMMAND_RESULT)
				{
					if(!ReplyToCommand(Socket, "Runtime.enable", "") ||
						!ReplyToCommand(Socket, "Runtime.enable", R"(,"result":{})"))
						return;
				}
				else if(Scenario == EScenario::EVALUATION_EXCEPTION)
				{
					constexpr std::string_view ExceptionReply =
						R"(,"result":{"result":{"type":"object","subtype":"error","className":"Error","description":"Error: test"},)"
						R"("exceptionDetails":{"exceptionId":1,"text":"Uncaught","lineNumber":0,"columnNumber":0}})";
					if(!ReplyToCommand(Socket, "Runtime.evaluate", ExceptionReply) ||
						!ReplyToCommand(Socket, "Runtime.evaluate", R"(,"result":{"result":{"type":"string","value":"ready"}})"))
						return;
				}
				Hold(Socket);
			});
		}
		std::future<bool> StartFrontendFailover()
		{
			m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			std::packaged_task<bool()> Serve([this] {
				{
					const CConnection Discovery{AcceptConnection()};
					const SOCKET Socket = Discovery.m_Socket;
					if(Socket == INVALID_SOCKET || !ReadRequest(Socket).starts_with("GET /json "))
						return false;
					const std::string Targets = "[{\"url\":\"orpheus://first\",\"webSocketDebuggerUrl\":\"" + Url() +
								    "/first\"},{\"url\":\"orpheus://next\",\"webSocketDebuggerUrl\":\"" + Url() + "/next\"}]";
					if(!Send(Socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
								 std::to_string(Targets.size()) + "\r\n\r\n" + Targets))
						return false;
				}
				{
					const CConnection First{AcceptConnection()};
					const SOCKET Socket = First.m_Socket;
					if(Socket == INVALID_SOCKET)
						return false;
					const std::string Request = ReadRequest(Socket);
					const std::string Upgrade = UpgradeResponse(Request);
					if(!Request.starts_with("GET /devtools/page/test/first ") || Upgrade.empty() || !Send(Socket, Upgrade) ||
						!ReplyToCommand(Socket, "Runtime.enable", R"(,"result":{})") ||
						!ReplyToCommand(Socket, "Page.enable", R"(,"result":{})") ||
						!ReplyToCommand(Socket, "Runtime.addBinding", R"(,"result":{})") ||
						!ReplyToCommand(Socket, "Page.addScriptToEvaluateOnNewDocument", R"(,"result":{"identifier":"test"})") ||
						!ReplyToCommand(Socket, "Runtime.evaluate", R"(,"result":{"result":{"type":"string","value":"installed"}})") ||
						ReadCommandId(Socket, "Runtime.evaluate") <= 0)
						return false;
					// 安装完成。收到首个快照请求后关闭连接。
				}
				const CConnection Next{AcceptConnection()};
				return Next.m_Socket != INVALID_SOCKET && ReadRequest(Next.m_Socket).starts_with("GET /devtools/page/test/next ");
			});
			auto Result = Serve.get_future();
			m_Thread = std::thread(std::move(Serve));
			return Result;
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

TEST_F(QmNeteaseCdpSocket, NotificationWithEmbeddedNullIsIgnored)
{
	m_Peer.Start(CLoopbackPeer::EScenario::MALFORMED_NOTIFICATION, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));
	std::string Notification;
	m_Session.SetNotificationCallback([&](std::string_view Value) { Notification = Value; });

	ASSERT_TRUE(m_Session.Pump(250));
	EXPECT_TRUE(Notification.empty());
	ASSERT_TRUE(m_Session.Pump(250));
	EXPECT_EQ(Notification, R"({"method":"Runtime.ready"})");
}

TEST_F(QmNeteaseCdpSocket, MatchingReplyWithoutResultFailsCommand)
{
	m_Peer.Start(CLoopbackPeer::EScenario::MISSING_COMMAND_RESULT, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));

	std::string Result = "previous";
	EXPECT_FALSE(m_Session.Command("Runtime.enable", "{}", &Result));
	EXPECT_TRUE(Result.empty());
	EXPECT_TRUE(m_Session.Command("Runtime.enable", "{}", &Result));
}

TEST_F(QmNeteaseCdpSocket, JavaScriptExceptionFailsEvaluationWithoutOutput)
{
	m_Peer.Start(CLoopbackPeer::EScenario::EVALUATION_EXCEPTION, &m_Stop);
	ASSERT_TRUE(m_Session.Connect(m_Peer.Url()));

	EXPECT_FALSE(m_Session.Evaluate("throw new Error('test')", nullptr));
	std::string Value;
	ASSERT_TRUE(m_Session.Evaluate("'ready'", &Value));
	EXPECT_EQ(Value, "ready");
}

TEST_F(QmNeteaseCdpSocket, FrontendTriesNextTargetAfterInitialSnapshotDisconnects)
{
	auto NextTarget = m_Peer.StartFrontendFailover();
	QmNeteaseCdp::CFrontendBridgeWorker Worker;
	ASSERT_TRUE(Worker.Start(GetCurrentProcessId(), L"--remote-debugging-port=" + std::to_wstring(m_Peer.Port()), {}));

	ASSERT_EQ(NextTarget.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_TRUE(NextTarget.get());
}
