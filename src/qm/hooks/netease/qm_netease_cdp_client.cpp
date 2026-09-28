#include "qm_netease_cdp_client.h"

#include <engine/external/json-parser/json.h>

#include <game/client/components/qmclient/netease/netease_lyric_parser.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <windows.h>

#include <bcrypt.h>
#include <iphlpapi.h>
#include <winhttp.h>
#include <ws2tcpip.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace QmNeteaseCdp
{
	namespace
	{
		const json_value *ObjectField(const json_value *pObject, const char *pName)
		{
			if(pObject == nullptr || pObject->type != json_object || pName == nullptr)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
					return Entry.value;
			}
			return nullptr;
		}

		std::string JsonEscape(std::string_view Value)
		{
			std::string Result;
			Result.reserve(Value.size() + 8);
			for(const unsigned char Byte : Value)
			{
				switch(Byte)
				{
				case '"': Result += "\\\""; break;
				case '\\': Result += "\\\\"; break;
				case '\b': Result += "\\b"; break;
				case '\f': Result += "\\f"; break;
				case '\n': Result += "\\n"; break;
				case '\r': Result += "\\r"; break;
				case '\t': Result += "\\t"; break;
				default:
					if(Byte < 0x20)
					{
						char aHex[7];
						std::snprintf(aHex, sizeof(aHex), "\\u%04x", Byte);
						Result += aHex;
					}
					else
						Result.push_back((char)Byte);
					break;
				}
			}
			return Result;
		}

#if defined(_WIN32)
		class CWinsockRuntime
		{
		public:
			CWinsockRuntime()
			{
				WSADATA Data{};
				m_Valid = WSAStartup(MAKEWORD(2, 2), &Data) == 0;
			}
			~CWinsockRuntime()
			{
				if(m_Valid)
					WSACleanup();
			}
			bool m_Valid = false;
		};

		bool SendAll(SOCKET Socket, const void *pData, size_t Size)
		{
			const char *pBytes = static_cast<const char *>(pData);
			while(Size > 0)
			{
				const int Sent = send(Socket, pBytes, (int)std::min<size_t>(Size, INT_MAX), 0);
				if(Sent <= 0)
					return false;
				pBytes += Sent;
				Size -= (size_t)Sent;
			}
			return true;
		}

		std::string Base64(const uint8_t *pData, size_t Size)
		{
			static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string Result;
			Result.reserve((Size + 2) / 3 * 4);
			for(size_t Index = 0; Index < Size; Index += 3)
			{
				const uint32_t A = pData[Index];
				const uint32_t B = Index + 1 < Size ? pData[Index + 1] : 0;
				const uint32_t C = Index + 2 < Size ? pData[Index + 2] : 0;
				const uint32_t Value = (A << 16) | (B << 8) | C;
				Result.push_back(Alphabet[(Value >> 18) & 63]);
				Result.push_back(Alphabet[(Value >> 12) & 63]);
				Result.push_back(Index + 1 < Size ? Alphabet[(Value >> 6) & 63] : '=');
				Result.push_back(Index + 2 < Size ? Alphabet[Value & 63] : '=');
			}
			return Result;
		}

		std::string Sha1Base64(std::string_view Text)
		{
			BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
			BCRYPT_HASH_HANDLE hHash = nullptr;
			std::array<uint8_t, 20> Digest{};
			DWORD HashLength = 0;
			DWORD ResultLength = 0;
			std::string Result;
			if(BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0 &&
				BCryptGetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&HashLength), sizeof(HashLength), &ResultLength, 0) == 0 &&
				HashLength == Digest.size() && BCryptCreateHash(hAlgorithm, &hHash, nullptr, 0, nullptr, 0, 0) == 0 &&
				BCryptHashData(hHash, (PUCHAR)Text.data(), (ULONG)Text.size(), 0) == 0 &&
				BCryptFinishHash(hHash, Digest.data(), (ULONG)Digest.size(), 0) == 0)
				Result = Base64(Digest.data(), Digest.size());
			if(hHash != nullptr)
				BCryptDestroyHash(hHash);
			if(hAlgorithm != nullptr)
				BCryptCloseAlgorithmProvider(hAlgorithm, 0);
			return Result;
		}

		bool HeaderValue(std::string_view Headers, std::string_view Name, std::string *pValue)
		{
			if(pValue == nullptr)
				return false;
			pValue->clear();
			for(size_t Offset = 0; Offset < Headers.size();)
			{
				const size_t End = Headers.find("\r\n", Offset);
				const std::string_view Line = Headers.substr(Offset, End == std::string_view::npos ? Headers.size() - Offset : End - Offset);
				const size_t Colon = Line.find(':');
				if(Colon != std::string_view::npos)
				{
					std::string Key(Line.substr(0, Colon));
					std::transform(Key.begin(), Key.end(), Key.begin(), [](char C) { return (char)std::tolower((unsigned char)C); });
					std::string Wanted(Name);
					std::transform(Wanted.begin(), Wanted.end(), Wanted.begin(), [](char C) { return (char)std::tolower((unsigned char)C); });
					if(Key == Wanted)
					{
						std::string Value(Line.substr(Colon + 1));
						while(!Value.empty() && std::isspace((unsigned char)Value.front()))
							Value.erase(Value.begin());
						while(!Value.empty() && std::isspace((unsigned char)Value.back()))
							Value.pop_back();
						*pValue = std::move(Value);
						return true;
					}
				}
				if(End == std::string_view::npos)
					break;
				Offset = End + 2;
			}
			return false;
		}

		class CWebSocket
		{
		public:
			~CWebSocket() { Close(); }
			bool Connect(std::string_view Url)
			{
				Close();
				if(!ParseLoopbackWebSocketUrl(Url, &m_Port))
					return false;
				const size_t HostStart = 5;
				const size_t Colon = Url.find(':', HostStart);
				const size_t PathStart = Url.find('/', Colon == std::string_view::npos ? HostStart : Colon);
				const std::string Path = PathStart == std::string_view::npos ? "/" : std::string(Url.substr(PathStart));
				static CWinsockRuntime Runtime;
				if(!Runtime.m_Valid)
					return false;
				addrinfo Hints{};
				Hints.ai_family = AF_INET;
				Hints.ai_socktype = SOCK_STREAM;
				Hints.ai_protocol = IPPROTO_TCP;
				addrinfo *pInfo = nullptr;
				const std::string PortText = std::to_string(m_Port);
				if(getaddrinfo("127.0.0.1", PortText.c_str(), &Hints, &pInfo) != 0 || pInfo == nullptr)
					return false;
				m_Socket = socket(pInfo->ai_family, pInfo->ai_socktype, pInfo->ai_protocol);
				if(m_Socket == INVALID_SOCKET || connect(m_Socket, pInfo->ai_addr, (int)pInfo->ai_addrlen) != 0)
				{
					if(m_Socket != INVALID_SOCKET)
						closesocket(m_Socket);
					m_Socket = INVALID_SOCKET;
					freeaddrinfo(pInfo);
					return false;
				}
				freeaddrinfo(pInfo);
				auto FailConnection = [this] {
					Close();
					return false;
				};
				const DWORD Timeout = 700;
				setsockopt(m_Socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&Timeout), sizeof(Timeout));
				setsockopt(m_Socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&Timeout), sizeof(Timeout));
				std::array<uint8_t, 16> Nonce{};
				if(BCryptGenRandom(nullptr, Nonce.data(), (ULONG)Nonce.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
					return FailConnection();
				const std::string Key = Base64(Nonce.data(), Nonce.size());
				const std::string Request = "GET " + Path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(m_Port) +
							    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + Key +
							    "\r\nSec-WebSocket-Version: 13\r\n\r\n";
				if(!SendAll(m_Socket, Request.data(), Request.size()))
					return FailConnection();
				std::string Response;
				std::array<char, 1024> Buffer{};
				while(Response.find("\r\n\r\n") == std::string::npos && Response.size() < 16 * 1024)
				{
					const int Received = recv(m_Socket, Buffer.data(), (int)Buffer.size(), 0);
					if(Received <= 0)
						return FailConnection();
					Response.append(Buffer.data(), (size_t)Received);
				}
				const size_t HeaderEnd = Response.find("\r\n\r\n");
				if(HeaderEnd == std::string::npos || Response.rfind("HTTP/1.1 101", 0) != 0)
					return FailConnection();
				std::string Accept;
				const std::string_view Headers(Response.data(), HeaderEnd + 4);
				if(!HeaderValue(Headers, "Sec-WebSocket-Accept", &Accept) || Accept != Sha1Base64(Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
					return FailConnection();
				m_ReceiveBuffer.assign(Response.data() + HeaderEnd + 4, Response.size() - HeaderEnd - 4);
				m_Connected = true;
				return true;
			}

			void Close()
			{
				if(m_Socket != INVALID_SOCKET)
				{
					shutdown(m_Socket, SD_BOTH);
					closesocket(m_Socket);
					m_Socket = INVALID_SOCKET;
				}
				m_Connected = false;
				m_FragmentOpcode = 0;
				m_FragmentPayload.clear();
				m_ReceiveBuffer.clear();
			}

			bool IsConnected() const { return m_Connected; }

			bool SendText(std::string_view Text)
			{
				if(!m_Connected || Text.size() > 16 * 1024 * 1024)
					return false;
				std::vector<uint8_t> Frame;
				Frame.reserve(Text.size() + 16);
				Frame.push_back(0x81);
				const uint64_t Length = Text.size();
				if(Length < 126)
					Frame.push_back((uint8_t)(0x80 | Length));
				else if(Length <= 0xFFFF)
				{
					Frame.push_back(0x80 | 126);
					Frame.push_back((uint8_t)(Length >> 8));
					Frame.push_back((uint8_t)Length);
				}
				else
				{
					Frame.push_back(0x80 | 127);
					for(int Shift = 56; Shift >= 0; Shift -= 8)
						Frame.push_back((uint8_t)(Length >> Shift));
				}
				std::array<uint8_t, 4> Mask{};
				if(BCryptGenRandom(nullptr, Mask.data(), (ULONG)Mask.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
					return false;
				Frame.insert(Frame.end(), Mask.begin(), Mask.end());
				for(size_t Index = 0; Index < Text.size(); ++Index)
					Frame.push_back((uint8_t)Text[Index] ^ Mask[Index % Mask.size()]);
				return SendAll(m_Socket, Frame.data(), Frame.size());
			}

			bool ReceiveText(std::string *pText, int TimeoutMs)
			{
				if(pText == nullptr || !m_Connected)
					return false;
				pText->clear();
				for(;;)
				{
					// 握手可能已经把完整首帧放进缓冲；有数据可消费时不能
					// 先 select，否则 socket 暂时无新数据会把首帧拖到命令超时。
					if(m_ReceiveBuffer.empty() && TimeoutMs >= 0)
					{
						fd_set ReadSet;
						FD_ZERO(&ReadSet);
						FD_SET(m_Socket, &ReadSet);
						timeval Wait{TimeoutMs / 1000, (TimeoutMs % 1000) * 1000};
						const int Ready = select(0, &ReadSet, nullptr, nullptr, &Wait);
						if(Ready == 0)
						{
							pText->clear();
							return true;
						}
						if(Ready < 0)
							return false;
					}
					uint8_t Header[2];
					if(!ReceiveBytes(Header, sizeof(Header)))
						return false;
					const bool Fin = (Header[0] & 0x80) != 0;
					// 未协商任何扩展；本地端点设置 RSV 位时直接视为协议错误。
					if((Header[0] & 0x70) != 0)
						return false;
					const uint8_t Opcode = Header[0] & 0x0F;
					if(Opcode != 0x0 && Opcode != 0x1 && Opcode != 0x2 && Opcode != 0x8 && Opcode != 0x9 && Opcode != 0xA)
						return false;
					const bool Control = (Opcode & 0x08) != 0;
					const bool Masked = (Header[1] & 0x80) != 0;
					// 服务端到客户端的帧不得设置 MASK 位（RFC 6455 5.1）。
					if(Masked)
						return false;
					const uint8_t InitialLength = Header[1] & 0x7F;
					uint64_t Length = InitialLength;
					if(InitialLength == 126)
					{
						uint8_t Bytes[2];
						if(!ReceiveBytes(Bytes, sizeof(Bytes)))
							return false;
						Length = ((uint64_t)Bytes[0] << 8) | Bytes[1];
						// 拒绝非最短长度编码。
						if(Length < 126)
							return false;
					}
					else if(InitialLength == 127)
					{
						uint8_t Bytes[8];
						if(!ReceiveBytes(Bytes, sizeof(Bytes)) || (Bytes[0] & 0x80) != 0)
							return false;
						Length = 0;
						for(const uint8_t Byte : Bytes)
							Length = (Length << 8) | Byte;
						if(Length < 65536)
							return false;
					}
					if(Length > 16 * 1024 * 1024 || (Control && (!Fin || Length > 125)))
						return false;
					std::string Payload((size_t)Length, '\0');
					if(Length > 0 && !ReceiveBytes(Payload.data(), (size_t)Length))
						return false;
					if(Opcode == 0x9)
					{
						if(!SendControl(0xA, Payload))
							return false;
						continue;
					}
					if(Opcode == 0x8)
					{
						// close 负载只有一个字节时格式非法；合法负载先回显，
						// 让 Chromium 完成关闭握手。
						if(Length == 1)
							return false;
						SendControl(0x8, Payload);
						Close();
						return false;
					}
					if(Opcode == 0xA)
						continue;
					if(Opcode == 0x2)
					{
						// CDP 只使用文本帧。消费二进制帧，并记住分片状态，
						// 防止后续 continuation 被误当作文本。
						if(m_FragmentOpcode != 0)
							return false;
						if(!Fin)
						{
							m_FragmentOpcode = 2;
							m_FragmentPayload.clear();
						}
						continue;
					}
					if(Opcode == 0x1)
					{
						if(m_FragmentOpcode != 0)
							return false;
						if(Fin)
						{
							*pText = std::move(Payload);
							return true;
						}
						m_FragmentOpcode = 1;
						m_FragmentPayload = std::move(Payload);
						continue;
					}
					// Continuation frame.
					if(m_FragmentOpcode == 0)
						return false;
					if(m_FragmentOpcode == 2)
					{
						if(Fin)
							m_FragmentOpcode = 0;
						continue;
					}
					if(m_FragmentPayload.size() + Payload.size() > 16 * 1024 * 1024)
						return false;
					m_FragmentPayload.append(Payload);
					if(Fin)
					{
						*pText = std::move(m_FragmentPayload);
						m_FragmentPayload.clear();
						m_FragmentOpcode = 0;
						return true;
					}
				}
			}

		private:
			bool ReceiveBytes(void *pData, size_t Size)
			{
				if(pData == nullptr)
					return false;
				char *pBytes = static_cast<char *>(pData);
				while(Size > 0)
				{
					if(!m_ReceiveBuffer.empty())
					{
						const size_t Count = std::min(Size, m_ReceiveBuffer.size());
						std::memcpy(pBytes, m_ReceiveBuffer.data(), Count);
						m_ReceiveBuffer.erase(0, Count);
						pBytes += Count;
						Size -= Count;
						continue;
					}
					const int Received = recv(m_Socket, pBytes, (int)std::min<size_t>(Size, INT_MAX), 0);
					if(Received <= 0)
						return false;
					pBytes += Received;
					Size -= (size_t)Received;
				}
				return true;
			}

			bool SendControl(uint8_t Opcode, std::string_view Payload)
			{
				if((Opcode & 0x08) == 0 || Opcode > 0x0A || Payload.size() > 125)
					return false;
				std::array<uint8_t, 4> Mask{};
				if(BCryptGenRandom(nullptr, Mask.data(), (ULONG)Mask.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
					return false;
				std::vector<uint8_t> Frame;
				Frame.reserve(2 + Mask.size() + Payload.size());
				Frame.push_back((uint8_t)(0x80 | Opcode));
				Frame.push_back((uint8_t)(0x80 | Payload.size()));
				Frame.insert(Frame.end(), Mask.begin(), Mask.end());
				for(size_t Index = 0; Index < Payload.size(); ++Index)
					Frame.push_back((uint8_t)Payload[Index] ^ Mask[Index % Mask.size()]);
				return SendAll(m_Socket, Frame.data(), Frame.size());
			}
			SOCKET m_Socket = INVALID_SOCKET;
			uint16_t m_Port = 0;
			bool m_Connected = false;
			uint8_t m_FragmentOpcode = 0;
			std::string m_FragmentPayload;
			std::string m_ReceiveBuffer;
		};

		bool HttpGetJson(uint16_t Port, std::string *pBody)
		{
			if(pBody == nullptr || Port == 0)
				return false;
			pBody->clear();
			HINTERNET hSession = WinHttpOpen(L"QmClient-Netease/5", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
			if(hSession == nullptr)
				return false;
			WinHttpSetTimeouts(hSession, 250, 250, 500, 500);
			HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", Port, 0);
			HINTERNET hRequest = hConnect != nullptr ? WinHttpOpenRequest(hConnect, L"GET", L"/json", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
			bool Success = false;
			DWORD RedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			const bool RedirectDisabled = hRequest != nullptr && WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &RedirectPolicy, sizeof(RedirectPolicy));
			if(RedirectDisabled && WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, nullptr))
			{
				DWORD Status = 0;
				DWORD StatusSize = sizeof(Status);
				if(WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &Status, &StatusSize, WINHTTP_NO_HEADER_INDEX) && Status == 200)
				{
					std::array<char, 8192> Buffer{};
					for(;;)
					{
						DWORD Available = 0;
						if(!WinHttpQueryDataAvailable(hRequest, &Available) || Available == 0)
							break;
						if(pBody->size() + Available > MAX_TARGET_JSON_BYTES)
							break;
						DWORD Read = 0;
						const DWORD Chunk = std::min<DWORD>(Available, (DWORD)Buffer.size());
						if(!WinHttpReadData(hRequest, Buffer.data(), Chunk, &Read) || Read == 0)
							break;
						pBody->append(Buffer.data(), Read);
					}
					Success = !pBody->empty();
				}
			}
			if(hRequest != nullptr)
				WinHttpCloseHandle(hRequest);
			if(hConnect != nullptr)
				WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return Success;
		}

		uint32_t LoopbackListenerProcessId(uint16_t Port)
		{
			if(Port == 0)
				return 0;
			DWORD BufferSize = 0;
			const DWORD ProbeResult = GetExtendedTcpTable(nullptr, &BufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
			if(ProbeResult != ERROR_INSUFFICIENT_BUFFER || BufferSize < sizeof(MIB_TCPTABLE_OWNER_PID))
				return 0;
			std::vector<uint8_t> Buffer(BufferSize);
			auto *pTable = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(Buffer.data());
			if(GetExtendedTcpTable(pTable, &BufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
				return 0;
			const DWORD LoopbackAddress = htonl(INADDR_LOOPBACK);
			for(DWORD Index = 0; Index < pTable->dwNumEntries; ++Index)
			{
				const MIB_TCPROW_OWNER_PID &Row = pTable->table[Index];
				if(Row.dwState == MIB_TCP_STATE_LISTEN && Row.dwLocalAddr == LoopbackAddress &&
					ntohs((u_short)Row.dwLocalPort) == Port)
					return Row.dwOwningPid;
			}
			return 0;
		}
#endif
	}

	struct CCdpSession::SImpl
	{
#if defined(_WIN32)
		CWebSocket m_WebSocket;
#endif
		uint64_t m_NextId = 1;
		TNotificationCallback m_NotificationCallback;
	};

	CCdpSession::CCdpSession() :
		m_pImpl(std::make_unique<SImpl>()) {}
	CCdpSession::~CCdpSession() { Close(); }

	bool CCdpSession::Connect(std::string_view WebSocketUrl)
	{
#if defined(_WIN32)
		return m_pImpl != nullptr && m_pImpl->m_WebSocket.Connect(WebSocketUrl);
#else
		(void)WebSocketUrl;
		return false;
#endif
	}

	void CCdpSession::Close()
	{
#if defined(_WIN32)
		if(m_pImpl)
			m_pImpl->m_WebSocket.Close();
#endif
	}

	bool CCdpSession::IsConnected() const
	{
#if defined(_WIN32)
		return m_pImpl != nullptr && m_pImpl->m_WebSocket.IsConnected();
#else
		return false;
#endif
	}

	void CCdpSession::SetNotificationCallback(TNotificationCallback Callback)
	{
		if(m_pImpl)
			m_pImpl->m_NotificationCallback = std::move(Callback);
	}

	bool CCdpSession::Command(std::string_view Method, std::string_view ParametersJson, std::string *pResultJson, int TimeoutMs)
	{
#if defined(_WIN32)
		if(!m_pImpl || !IsConnected() || Method.empty())
			return false;
		auto FailConnection = [this] {
			if(m_pImpl)
				m_pImpl->m_WebSocket.Close();
			return false;
		};
		const uint64_t Id = m_pImpl->m_NextId++;
		const std::string Parameters = ParametersJson.empty() ? "{}" : std::string(ParametersJson);
		const std::string Request = "{\"id\":" + std::to_string(Id) + ",\"method\":\"" + JsonEscape(Method) + "\",\"params\":" + Parameters + "}";
		if(!m_pImpl->m_WebSocket.SendText(Request))
			return FailConnection();
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, TimeoutMs));
		for(;;)
		{
			const auto Remaining = std::chrono::duration_cast<std::chrono::milliseconds>(Deadline - std::chrono::steady_clock::now()).count();
			if(Remaining <= 0)
				return FailConnection();
			std::string Message;
			if(!m_pImpl->m_WebSocket.ReceiveText(&Message, (int)std::min<int64_t>(Remaining, 500)))
				return FailConnection();
			json_value *pRoot = json_parse(Message.data(), Message.size());
			if(pRoot == nullptr)
				continue;
			const json_value *pMethod = ObjectField(pRoot, "method");
			if(pMethod != nullptr && pMethod->type == json_string)
			{
				if(m_pImpl->m_NotificationCallback)
					m_pImpl->m_NotificationCallback(Message);
				json_value_free(pRoot);
				continue;
			}
			const json_value *pId = ObjectField(pRoot, "id");
			const bool MatchingId = pId != nullptr && pId->type == json_integer && pId->u.integer == (json_int_t)Id;
			if(MatchingId)
			{
				const json_value *pError = ObjectField(pRoot, "error");
				const json_value *pResult = ObjectField(pRoot, "result");
				if(pResultJson != nullptr)
					*pResultJson = pError == nullptr && pResult != nullptr ? Message : "";
				const bool Success = pError == nullptr;
				json_value_free(pRoot);
				return Success;
			}
			json_value_free(pRoot);
		}
#else
		(void)Method;
		(void)ParametersJson;
		(void)pResultJson;
		(void)TimeoutMs;
		return false;
#endif
	}

	bool CCdpSession::Evaluate(std::string_view Expression, std::string *pValueJson, int TimeoutMs)
	{
		return EvaluateImpl(Expression, pValueJson, TimeoutMs, false);
	}

	bool CCdpSession::EvaluateAwaitPromise(std::string_view Expression, std::string *pValueJson, int TimeoutMs)
	{
		return EvaluateImpl(Expression, pValueJson, TimeoutMs, true);
	}

	bool CCdpSession::EvaluateImpl(std::string_view Expression, std::string *pValueJson, int TimeoutMs, bool AwaitPromise)
	{
		const std::string Params = "{\"expression\":\"" + JsonEscape(Expression) + "\",\"returnByValue\":true,\"awaitPromise\":" + (AwaitPromise ? "true" : "false") + "}";
		std::string Result;
		if(!Command("Runtime.evaluate", Params, &Result, TimeoutMs))
			return false;
		if(pValueJson == nullptr)
			return true;
		json_value *pRoot = json_parse(Result.data(), Result.size());
		if(pRoot == nullptr)
			return false;
		const json_value *pOuter = ObjectField(pRoot, "result");
		const json_value *pValue = ObjectField(pOuter, "result");
		if(pValue == nullptr || pValue->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}
		const json_value *pValueField = ObjectField(pValue, "value");
		if(pValueField != nullptr && pValueField->type == json_string)
			*pValueJson = std::string(pValueField->u.string.ptr, pValueField->u.string.length);
		else
			pValueJson->clear();
		json_value_free(pRoot);
		return pValueField != nullptr;
	}

	bool CCdpSession::Pump(int TimeoutMs)
	{
#if defined(_WIN32)
		if(!m_pImpl || !IsConnected())
			return false;
		std::string Message;
		if(!m_pImpl->m_WebSocket.ReceiveText(&Message, TimeoutMs))
		{
			m_pImpl->m_WebSocket.Close();
			return false;
		}
		json_value *pRoot = json_parse(Message.data(), Message.size());
		if(pRoot != nullptr)
		{
			if(ObjectField(pRoot, "method") != nullptr && m_pImpl->m_NotificationCallback)
				m_pImpl->m_NotificationCallback(Message);
			json_value_free(pRoot);
		}
		return true;
#else
		(void)TimeoutMs;
		return false;
#endif
	}

	struct CFrontendBridgeWorker::SImpl
	{
		std::atomic_bool m_Stop{false};
		std::atomic_bool m_Connected{false};
		uint32_t m_CloudMusicPid = 0;
		std::wstring m_CommandLine;
		TReportCallback m_Callback;
		std::thread m_Thread;
	};

	CFrontendBridgeWorker::CFrontendBridgeWorker() :
		m_pImpl(std::make_unique<SImpl>()) {}
	CFrontendBridgeWorker::~CFrontendBridgeWorker() { Stop(); }

	bool CFrontendBridgeWorker::Start(uint32_t CloudMusicPid, std::wstring CommandLine, TReportCallback Callback)
	{
		if(!m_pImpl || CloudMusicPid == 0)
			return false;
		Stop();
		m_pImpl->m_Stop = false;
		m_pImpl->m_Connected = false;
		m_pImpl->m_CloudMusicPid = CloudMusicPid;
		m_pImpl->m_CommandLine = std::move(CommandLine);
		m_pImpl->m_Callback = std::move(Callback);
		m_pImpl->m_Thread = std::thread(&CFrontendBridgeWorker::Run, this);
		return true;
	}

	void CFrontendBridgeWorker::Stop()
	{
		if(!m_pImpl)
			return;
		m_pImpl->m_Stop = true;
		if(m_pImpl->m_Thread.joinable())
			m_pImpl->m_Thread.join();
		m_pImpl->m_Connected = false;
	}

	bool CFrontendBridgeWorker::IsConnected() const
	{
		return m_pImpl != nullptr && m_pImpl->m_Connected.load(std::memory_order_acquire);
	}

	std::string BuildInstallHookScript()
	{
		return R"JS((() => {
const bridgeVersion = "13";
if (window.__QM_NCM_BRIDGE_VERSION__ !== bridgeVersion) {
  if (window.__QM_NCM_BRIDGE_TIMER__) {
    try { clearInterval(window.__QM_NCM_BRIDGE_TIMER__); } catch (_) {}
    window.__QM_NCM_BRIDGE_TIMER__ = null;
  }
  const previousSubscription = window.__QM_NCM_PROGRESS_SUBSCRIPTION__;
  if (previousSubscription && typeof previousSubscription.unsubscribe === "function") {
    try { previousSubscription.unsubscribe(); } catch (_) {}
  }
  window.__QM_NCM_PROGRESS_SUBSCRIPTION__ = null;
  window.__QM_NCM_PROGRESS_STREAM__ = null;
  window.__QM_NCM_NATIVE_PROGRESS__ = null;
  window.__QM_NCM_LAST_REPORT_AT__ = 0;
  window.__QM_NCM_LYRIC_STATE__ = null;
}
window.__QM_NCM_BRIDGE_VERSION__ = bridgeVersion;
const report = (value) => { try { if (typeof window.__qmReport === "function") window.__qmReport(JSON.stringify(value)); } catch (_) {} };
const toSongId = (value) => {
  try {
    const text = String(value == null ? "" : value);
    return /^[1-9][0-9]*$/.test(text) ? text : "";
  } catch (_) {}
  return "";
};
const getWebpackRequire = () => {
  try {
    if (window.__QM_NCM_WEBPACK_REQUIRE__) return window.__QM_NCM_WEBPACK_REQUIRE__;
    const queue = window.webpackJsonp;
    if (!queue || typeof queue.push !== "function") return null;
    let captured = null;
    const moduleId = "__qm_ncm_bridge_capture__";
    queue.push([[], {[moduleId]: function(module, exports, require) { captured = require; }}, [[moduleId]]]);
    if (captured && captured.c) {
      window.__QM_NCM_WEBPACK_REQUIRE__ = captured;
      return captured;
    }
  } catch (_) {}
  return null;
};
const findStore = () => {
  try {
    const cached = window.__QM_NCM_STORE_OWNER__;
    if (cached && typeof cached.getStore === "function") {
      const store = cached.getStore();
      if (store && store.playing) return store;
    }
    const require = getWebpackRequire();
    if (!require || !require.c) return null;
    for (const id of Object.keys(require.c)) {
      const exports = require.c[id] && require.c[id].exports;
      for (const candidate of [exports, exports && exports.default, exports && exports.a]) {
        if (!candidate || typeof candidate.getStore !== "function") continue;
        try {
          const store = candidate.getStore();
          if (store && store.playing) {
            window.__QM_NCM_STORE_OWNER__ = candidate;
            return store;
          }
        } catch (_) {}
      }
    }
  } catch (_) {}
  return null;
};
const findProgressStream = () => {
  try {
    const cached = window.__QM_NCM_CACHED_PROGRESS_STREAM__;
    if (cached && typeof cached.subscribe === "function") return cached;
    const require = getWebpackRequire();
    if (!require || !require.c) return null;
    for (const id of Object.keys(require.c)) {
      const stream = require.c[id] && require.c[id].exports && require.c[id].exports.audioPlayerPlayProgress$;
      if (stream && typeof stream.subscribe === "function") {
        window.__QM_NCM_CACHED_PROGRESS_STREAM__ = stream;
        return stream;
      }
    }
  } catch (_) {}
  return null;
};
const readPlaying = () => {
  try {
    const store = findStore();
    const playing = store && store.playing;
    if (!playing) return null;
    const songId = toSongId(playing.onlineResourceId) || toSongId(playing.resourceTrackId) || toSongId(playing.curTrack && playing.curTrack.id);
    let isPlaying = null;
    if (playing.playingState === 2 || playing.playingState === "playing" || playing.playingState === "play") isPlaying = true;
    else if (playing.playingState === -1 || playing.playingState === 0 || playing.playingState === 1 || playing.playingState === "paused" || playing.playingState === "stopped") isPlaying = false;
    return {songId, isPlaying};
  } catch (_) {}
  return null;
};
const lyricState = () => {
  let state = window.__QM_NCM_LYRIC_STATE__;
  if (!state) {
    state = {songId:"", songChangedAt:0, sawLoading:false, previousContentKey:"", reportedContentKey:"", reportedKey:"", candidateKey:"", candidateCount:0, fetchSongId:"", fetchRequestedAt:0, fetchPending:false, fetchCompleted:false, fetchGeneration:0};
    window.__QM_NCM_LYRIC_STATE__ = state;
  }
  return state;
};
const resetLyricCandidate = (state) => {
  state.candidateKey = "";
  state.candidateCount = 0;
};
const noteLyricSong = (songId, now) => {
  const state = lyricState();
  if (songId && state.songId !== songId) {
    state.songId = songId;
    state.songChangedAt = now;
    state.sawLoading = false;
    state.previousContentKey = state.reportedContentKey;
    state.reportedKey = "";
    state.fetchSongId = "";
    state.fetchRequestedAt = 0;
    state.fetchPending = false;
    state.fetchCompleted = false;
    resetLyricCandidate(state);
  }
  return state;
};
const serializeLyricLines = (lines) => {
  if (!Array.isArray(lines)) return "";
  const output = [];
  let hasText = false;
  for (const line of lines) {
    if (!line || typeof line !== "object" || typeof line.lyric !== "string") continue;
    const seconds = Number(line.time);
    if (!Number.isFinite(seconds) || seconds < 0 || seconds > Number.MAX_SAFE_INTEGER / 1000) continue;
    const totalMs = Math.round(seconds * 1000);
    const minutes = Math.floor(totalMs / 60000);
    const remainder = totalMs - minutes * 60000;
    const wholeSeconds = String(Math.floor(remainder / 1000)).padStart(2, "0");
    const milliseconds = String(remainder % 1000).padStart(3, "0");
    const text = line.lyric.replace(/[\r\n]+/g, " ");
    if (text.trim()) hasText = true;
    output.push(`[${minutes}:${wholeSeconds}.${milliseconds}]${text}`);
  }
  return hasText ? output.join("\n") : "";
};
const finishStoreLyricFetch = (songId, fetchGeneration, success) => {
  const state = lyricState();
  if (state.songId !== songId || state.fetchGeneration !== fetchGeneration) return;
  state.fetchPending = false;
  state.fetchCompleted = success;
  if (success) state.sawLoading = true;
  resetLyricCandidate(state);
};
const requestStoreLyrics = (songId, state, now) => {
  if (state.fetchSongId === songId && now - state.fetchRequestedAt < 30000) return false;
  try {
    const owner = window.__QM_NCM_STORE_OWNER__;
    const dispatch = owner && typeof owner.getDispatch === "function" ? owner.getDispatch() : null;
    if (typeof dispatch !== "function") return false;
    state.fetchSongId = songId;
    state.fetchRequestedAt = now;
    state.fetchPending = true;
    state.fetchCompleted = false;
    state.fetchGeneration += 1;
    const fetchGeneration = state.fetchGeneration;
    const result = dispatch({type:"async:lyric/fetchLyric", payload:{force:true}});
    if (result && typeof result.then === "function")
      result.then(() => finishStoreLyricFetch(songId, fetchGeneration, true), () => finishStoreLyricFetch(songId, fetchGeneration, false));
    else
      finishStoreLyricFetch(songId, fetchGeneration, true);
    return true;
  } catch (_) {
    state.fetchPending = false;
    state.fetchCompleted = false;
  }
  return false;
};
const reportProgress = (force) => {
  try {
    const now = Date.now();
    if (!force && now - (window.__QM_NCM_LAST_REPORT_AT__ || 0) < 750) return;
    const playing = readPlaying();
    const nativeProgress = window.__QM_NCM_NATIVE_PROGRESS__;
    const payload = {kind:"progress", songId:playing ? playing.songId : "", timestamp:now};
    if (nativeProgress && typeof nativeProgress.positionMs === "number" && Number.isFinite(nativeProgress.positionMs)) payload.positionMs = nativeProgress.positionMs;
    if (playing && typeof playing.isPlaying === "boolean") payload.playing = playing.isPlaying;
    if (!payload.songId && payload.positionMs == null && payload.playing == null) return;
    if (payload.songId) noteLyricSong(payload.songId, now);
    window.__QM_NCM_LAST_REPORT_AT__ = now;
    window.__QM_NCM_LAST_PROGRESS__ = payload;
    report(payload);
  } catch (_) {}
};
const reportStoreLyrics = () => {
  try {
    const store = findStore();
    const playing = readPlaying();
    const songId = playing ? playing.songId : "";
    if (!store || !songId) return;
    const now = Date.now();
    const state = noteLyricSong(songId, now);
    const lyric = store["async:lyric"];
    if (state.fetchSongId !== songId) {
      requestStoreLyrics(songId, state, now);
      resetLyricCandidate(state);
      return;
    }
    if (state.fetchPending || !state.fetchCompleted) {
      if (now - state.fetchRequestedAt >= 30000)
        requestStoreLyrics(songId, state, now);
      resetLyricCandidate(state);
      return;
    }
    if (!lyric || (!lyric.isLoading && Array.isArray(lyric.lyricLines) && lyric.lyricLines.length === 0 && lyric.currentUsedLyric === "none" && lyric.displayType === "default"))
      requestStoreLyrics(songId, state, now);
    if (!lyric || lyric.isLoading || !Array.isArray(lyric.lyricLines)) {
      if (lyric && lyric.isLoading) state.sawLoading = true;
      resetLyricCandidate(state);
      return;
    }
    const lrc = serializeLyricLines(lyric.lyricLines);
    if (!lrc) {
      resetLyricCandidate(state);
      return;
    }
    const contentKey = lrc;
    if (!state.sawLoading && state.previousContentKey && contentKey === state.previousContentKey && now - state.songChangedAt < 5000) {
      resetLyricCandidate(state);
      return;
    }
    const lyricVersion = `${String(lyric.currentUsedLyric || "")}:${String(lyric.currentUsedLyricVersion == null ? "" : lyric.currentUsedLyricVersion)}`;
    const key = `${songId}\n${lyricVersion}\n${contentKey}`;
    if (state.reportedKey === key) return;
    if (state.candidateKey !== key) {
      state.candidateKey = key;
      state.candidateCount = 1;
      return;
    }
    state.candidateCount += 1;
    if (state.candidateCount < 3) return;
    const payload = {kind:"lyrics", songId, rawLyrics:{lrc}, timestamp:now};
    state.reportedKey = key;
    state.reportedContentKey = contentKey;
    resetLyricCandidate(state);
    window.__QM_NCM_RAW_LYRICS__ = payload;
    report(payload);
  } catch (_) {}
};
const installProgress = () => {
  try {
    const stream = findProgressStream();
    if (!stream || stream === window.__QM_NCM_PROGRESS_STREAM__) return;
    const previous = window.__QM_NCM_PROGRESS_SUBSCRIPTION__;
    if (previous && typeof previous.unsubscribe === "function") previous.unsubscribe();
    window.__QM_NCM_PROGRESS_SUBSCRIPTION__ = stream.subscribe((value) => {
      try {
        const values = Array.isArray(value) ? value : [];
        const seconds = Number(values[1]);
        if (Number.isFinite(seconds) && seconds >= 0) {
          const firstProgress = !window.__QM_NCM_NATIVE_PROGRESS__;
          window.__QM_NCM_NATIVE_PROGRESS__ = {positionMs: Math.max(0, seconds * 1000), timestamp:Date.now()};
          reportProgress(firstProgress);
        }
      } catch (_) {}
    });
    window.__QM_NCM_PROGRESS_STREAM__ = stream;
  } catch (_) {}
};
const installLegacyLyrics = () => {
  try {
    if (typeof window.onProcessLyrics === "function" && !window.onProcessLyrics.__qmWrapped) {
      const original = window.onProcessLyrics;
      const wrapped = function(rawLyrics, songId) {
        try {
          const payload = {kind:"lyrics", songId:toSongId(songId), rawLyrics, timestamp:Date.now()};
          window.__QM_NCM_RAW_LYRICS__ = payload;
          report(payload);
        } catch (_) {}
        return original.apply(this, arguments);
      };
      Object.defineProperty(wrapped, "__qmWrapped", {value:true});
      Object.defineProperty(wrapped, "__qmOriginal", {value:original});
      window.onProcessLyrics = wrapped;
    }
  } catch (_) {}
};
const install = () => { installProgress(); installLegacyLyrics(); reportProgress(false); reportStoreLyrics(); };
install();
if (!window.__QM_NCM_BRIDGE_TIMER__) window.__QM_NCM_BRIDGE_TIMER__ = setInterval(install, 250);
return "installed";
})())JS";
	}

	void CFrontendBridgeWorker::Run()
	{
#if defined(_WIN32)
		std::vector<uint16_t> Ports;
		uint16_t ExplicitPort = 0;
		if(ParseDebuggingPort(m_pImpl->m_CommandLine, &ExplicitPort))
			Ports.push_back(ExplicitPort);
		wchar_t aEnv[16] = {};
		if(GetEnvironmentVariableW(L"QM_NCM_CDP_PORT", aEnv, (DWORD)std::size(aEnv)) > 0)
		{
			uint16_t EnvPort = 0;
			if(ParseDebuggingPort(std::wstring(L"--remote-debugging-port=") + aEnv, &EnvPort))
				Ports.push_back(EnvPort);
		}
		for(uint32_t Offset = 0; Offset < DYNAMIC_PORT_CANDIDATE_COUNT; ++Offset)
			Ports.push_back(DynamicPortCandidate(m_pImpl->m_CloudMusicPid, Offset));
		while(!m_pImpl->m_Stop)
		{
			bool Connected = false;
			for(const uint16_t Port : Ports)
			{
				const uint32_t ListenerProcessId = LoopbackListenerProcessId(Port);
				if(ListenerProcessId != m_pImpl->m_CloudMusicPid)
					continue;
				std::string Body;
				if(!HttpGetJson(Port, &Body))
					continue;
				std::vector<SCdpTarget> Targets;
				if(!ParseTargetList(Body, &Targets))
					continue;
				for(const SCdpTarget &Target : Targets)
				{
					// 在建立高权限 CDP 会话前再次核对 owner，缩小端口被替换的竞态窗口。
					const uint32_t CurrentListenerProcessId = LoopbackListenerProcessId(Port);
					if(!IsTrustedTargetForProcess(Target, Port, CurrentListenerProcessId, m_pImpl->m_CloudMusicPid))
						continue;
					CCdpSession Session;
					if(!Session.Connect(Target.m_WebSocketDebuggerUrl))
						continue;
					Session.SetNotificationCallback([this](std::string_view Message) {
						if(!m_pImpl->m_Callback)
							return;
						json_value *pRoot = json_parse(Message.data(), Message.size());
						if(pRoot == nullptr)
							return;
						const json_value *pMethod = ObjectField(pRoot, "method");
						const json_value *pParams = ObjectField(pRoot, "params");
						const json_value *pName = ObjectField(pParams, "name");
						const json_value *pPayload = ObjectField(pParams, "payload");
						if(pMethod != nullptr && pMethod->type == json_string &&
							std::string_view(pMethod->u.string.ptr, pMethod->u.string.length) == "Runtime.bindingCalled" &&
							pName != nullptr && pName->type == json_string && std::string_view(pName->u.string.ptr, pName->u.string.length) == "__qmReport" &&
							pPayload != nullptr && pPayload->type == json_string)
						{
							m_pImpl->m_Callback(std::string_view(pPayload->u.string.ptr, pPayload->u.string.length));
						}
						json_value_free(pRoot);
					});
					std::string Ignored;
					const std::string HookScript = BuildInstallHookScript();
					const std::string NewDocumentParams = "{\"source\":\"" + JsonEscape(HookScript) + "\"}";
					if(!Session.Command("Runtime.enable", "{}", &Ignored) ||
						!Session.Command("Page.enable", "{}", &Ignored))
					{
						Session.Close();
						continue;
					}
					bool BindingReady = Session.Command("Runtime.addBinding", "{\"name\":\"__qmReport\"}", &Ignored);
					if(!BindingReady)
					{
						std::string BindingCheck;
						BindingReady = Session.Evaluate("JSON.stringify(typeof globalThis.__qmReport === 'function')", &BindingCheck, 800) && BindingCheck == "true";
					}
					if(!BindingReady ||
						!Session.Command("Page.addScriptToEvaluateOnNewDocument", NewDocumentParams, &Ignored) ||
						!Session.Evaluate(HookScript, nullptr))
					{
						Session.Close();
						continue;
					}
					std::string InitialValue;
					if(Session.Evaluate("JSON.stringify(window.__QM_NCM_RAW_LYRICS__ || null)", &InitialValue, 800) && m_pImpl->m_Callback && !InitialValue.empty())
						m_pImpl->m_Callback(InitialValue);
					if(Session.Evaluate("JSON.stringify(window.__QM_NCM_LAST_PROGRESS__ || null)", &InitialValue, 800) && m_pImpl->m_Callback && !InitialValue.empty())
						m_pImpl->m_Callback(InitialValue);
					Connected = true;
					m_pImpl->m_Connected = true;
					uint64_t LastPoll = GetTickCount64();
					while(!m_pImpl->m_Stop && Session.IsConnected())
					{
						if(!Session.Pump(250))
							break;
						const uint64_t Now = GetTickCount64();
						if(Now - LastPoll >= 2000)
						{
							LastPoll = Now;
							std::string Value;
							if(Session.Evaluate("JSON.stringify(window.__QM_NCM_RAW_LYRICS__ || null)", &Value, 800) && m_pImpl->m_Callback && !Value.empty())
								m_pImpl->m_Callback(Value);
						}
					}
					Session.Close();
					break;
				}
				if(Connected)
					break;
			}
			m_pImpl->m_Connected = false;
			if(!m_pImpl->m_Stop)
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
#else
		// 非 Windows 构建没有网易云 frontend；保持 optional subsystem 语义。
		while(!m_pImpl->m_Stop)
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
#endif
	}
}
