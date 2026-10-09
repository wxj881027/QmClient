#include "http_curl.h"
#if !defined(CONF_PLATFORM_EMSCRIPTEN)

#include <base/dbg.h>
#include <base/log.h>
#include <base/str.h>
#include <base/thread.h>
#include <base/time.h>

#include <engine/shared/config.h>
#include <engine/shared/http_url.h>
#include <engine/storage.h>

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <winsock2.h>

#include <ws2tcpip.h>
#else
#include <arpa/inet.h>

#include <csignal>
#endif

#include <curl/curl.h>

static constexpr size_t HTTP_MAX_CONCURRENT_REQUESTS = 16;
static constexpr size_t HTTP_MAX_CONCURRENT_REQUESTS_PER_HOST = 4;

static int CurlDebug(CURL *pHandle, curl_infotype Type, char *pData, size_t DataSize, void *pUser)
{
	char TypeChar;
	switch(Type)
	{
	case CURLINFO_TEXT:
		TypeChar = '*';
		break;
	case CURLINFO_HEADER_OUT:
		TypeChar = '<';
		break;
	case CURLINFO_HEADER_IN:
		TypeChar = '>';
		break;
	default:
		return 0;
	}
	while(const char *pLineEnd = (const char *)memchr(pData, '\n', DataSize))
	{
		int LineLength = pLineEnd - pData;
		log_debug("curl", "%c %.*s", TypeChar, LineLength, pData);
		pData += LineLength + 1;
		DataSize -= LineLength + 1;
	}
	return 0;
}

CHttpRequestCurl::CHttpRequestCurl(const char *pUrl) :
	IHttpRequest(pUrl)
{
}

CHttpRequestCurl::~CHttpRequestCurl()
{
	dbg_assert(m_File == nullptr, "HTTP request file was not closed");
	curl_slist_free_all(m_pRequestHeaders);
}

void CHttpRequestCurl::Header(const char *pNameColonValue)
{
	m_pRequestHeaders = curl_slist_append(m_pRequestHeaders, pNameColonValue);
}

bool CHttpRequestCurl::ConfigureHandle(CURL *pHandle)
{
	if(!BeforeInit())
	{
		return false;
	}

	if(g_Config.m_DbgHttp)
	{
		curl_easy_setopt(pHandle, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(pHandle, CURLOPT_DEBUGFUNCTION, CurlDebug);
	}
	long Protocols = CURLPROTO_HTTPS;
	if(m_AllowInsecureProtocol || g_Config.m_HttpAllowInsecure)
	{
		Protocols |= CURLPROTO_HTTP;
	}

	curl_easy_setopt(pHandle, CURLOPT_ERRORBUFFER, m_aErr);

	curl_easy_setopt(pHandle, CURLOPT_CONNECTTIMEOUT_MS, m_Timeout.m_ConnectTimeoutMs);
	curl_easy_setopt(pHandle, CURLOPT_TIMEOUT_MS, m_Timeout.m_TimeoutMs);
	curl_easy_setopt(pHandle, CURLOPT_LOW_SPEED_LIMIT, m_Timeout.m_LowSpeedLimit);
	curl_easy_setopt(pHandle, CURLOPT_LOW_SPEED_TIME, m_Timeout.m_LowSpeedTime);
	// 范围长度只约束最终正文，不能让中间重定向的 Content-Length 提前终止请求。
	if(m_MaxResponseSize >= 0 && !m_ByteRange)
	{
		curl_easy_setopt(pHandle, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)m_MaxResponseSize);
	}
	if(m_IfModifiedSince >= 0)
	{
		curl_easy_setopt(pHandle, CURLOPT_TIMEVALUE_LARGE, (curl_off_t)m_IfModifiedSince);
		curl_easy_setopt(pHandle, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
	}

	// ‘CURLOPT_PROTOCOLS’ is deprecated: since 7.85.0. Use CURLOPT_PROTOCOLS_STR
	// Wait until all platforms have 7.85.0
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	curl_easy_setopt(pHandle, CURLOPT_PROTOCOLS, Protocols);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
	curl_easy_setopt(pHandle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(pHandle, CURLOPT_MAXREDIRS, 4L);
	if(m_FailOnErrorStatus)
	{
		curl_easy_setopt(pHandle, CURLOPT_FAILONERROR, 1L);
	}
	curl_easy_setopt(pHandle, CURLOPT_URL, m_aUrl);
	if(m_ProxyConfigured)
		curl_easy_setopt(pHandle, CURLOPT_PROXY, m_aProxy);
	else
	{
		// 连接代理失败时 USED_PROXY 仍可能为零，保留所选环境路由用于官方直连回退。
		// 不设置 CURLOPT_PROXY，继续由 curl 原生处理 NO_PROXY、认证及代理优先级。
		const bool Https = str_startswith_nocase(m_aUrl, "https:");
		for(const char *pName : {Https ? "https_proxy" : "http_proxy", Https ? "HTTPS_PROXY" : "", "all_proxy", "ALL_PROXY"})
		{
			const char *pValue = *pName ? std::getenv(pName) : nullptr;
			if(pValue && *pValue)
			{
				str_copy(m_aProxy, pValue);
				break;
			}
		}
	}
	curl_easy_setopt(pHandle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pHandle, CURLOPT_USERAGENT, USER_AGENT_STRING);
	curl_easy_setopt(pHandle, CURLOPT_ACCEPT_ENCODING, ""); // Use any compression algorithm supported by libcurl.
	if(m_ByteRange)
		curl_easy_setopt(pHandle, CURLOPT_ACCEPT_ENCODING, "identity");

	curl_easy_setopt(pHandle, CURLOPT_HEADERDATA, this);
	curl_easy_setopt(pHandle, CURLOPT_HEADERFUNCTION, HeaderCallback);
	curl_easy_setopt(pHandle, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(pHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(pHandle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(pHandle, CURLOPT_PROGRESSDATA, this);
	// ‘CURLOPT_PROGRESSFUNCTION’ is deprecated: since 7.32.0. Use CURLOPT_XFERINFOFUNCTION
	// See problems with curl_off_t type in header file in https://github.com/ddnet/ddnet/pull/6185/
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	curl_easy_setopt(pHandle, CURLOPT_PROGRESSFUNCTION, ProgressCallback);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
	curl_easy_setopt(pHandle, CURLOPT_IPRESOLVE, m_IpResolve == IPRESOLVE::V4 ? CURL_IPRESOLVE_V4 : (m_IpResolve == IPRESOLVE::V6 ? CURL_IPRESOLVE_V6 : CURL_IPRESOLVE_WHATEVER));
	if(g_Config.m_Bindaddr[0] != '\0')
	{
		curl_easy_setopt(pHandle, CURLOPT_INTERFACE, g_Config.m_Bindaddr);
	}

	if(curl_version_info(CURLVERSION_NOW)->version_num < 0x074400)
	{
		// Causes crashes, see https://github.com/ddnet/ddnet/issues/4342.
		// No longer a problem in curl 7.68 and above, and 0x44 = 68.
		curl_easy_setopt(pHandle, CURLOPT_FORBID_REUSE, 1L);
	}

#ifdef CONF_PLATFORM_ANDROID
	curl_easy_setopt(pHandle, CURLOPT_CAPATH, "/system/etc/security/cacerts");
#endif

	switch(m_Type)
	{
	case REQUEST::GET:
		break;
	case REQUEST::HEAD:
		curl_easy_setopt(pHandle, CURLOPT_NOBODY, 1L);
		break;
	case REQUEST::POST:
	case REQUEST::POST_FORM:
	case REQUEST::POST_JSON:
		if(m_Type == REQUEST::POST_JSON)
		{
			Header("Content-Type: application/json");
		}
		else
		{
			Header("Content-Type:");
		}
		curl_easy_setopt(pHandle, CURLOPT_POSTFIELDS, m_pBody);
		curl_easy_setopt(pHandle, CURLOPT_POSTFIELDSIZE, m_BodyLength);
		break;
	}

	curl_easy_setopt(pHandle, CURLOPT_HTTPHEADER, m_pRequestHeaders);

	return true;
}

std::optional<int64_t> CHttpRequestCurl::ParseRetryAfter(std::string_view Value, int64_t Now)
{
	const auto First = Value.find_first_not_of(" \t\r\n");
	if(First == std::string_view::npos)
		return std::nullopt;
	Value = Value.substr(First, Value.find_last_not_of(" \t\r\n") - First + 1);
	if((Value.front() >= '0' && Value.front() <= '9') || Value.front() == '-')
	{
		int64_t Seconds;
		const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Seconds);
		if(Result.ec != std::errc() || Result.ptr != Value.data() + Value.size() || Seconds < 0)
			return std::nullopt;
		return Seconds;
	}
	const std::string Text(Value);
	const int64_t Date = curl_getdate(Text.c_str(), nullptr);
	return Date < 0 ? std::nullopt : std::optional<int64_t>(std::max<int64_t>(0, Date - Now));
}

bool CHttpRequestCurl::ProxyBypassed(std::string_view Host, std::string_view List)
{
	if(Host.empty() || List.empty())
		return false;
	// curl 仅把整个列表恰好为星号视为通配，不支持列表内的星号项。
	if(List == "*")
		return true;
	if(Host.front() == '[' && Host.back() == ']')
		Host = Host.substr(1, Host.size() - 2);
	if(Host.empty() || Host.find('\0') != std::string_view::npos)
		return false;
	unsigned char aHost[16] = {};
	const std::string HostText(Host);
	const int Family = inet_pton(AF_INET, HostText.c_str(), aHost) == 1  ? AF_INET :
			   inet_pton(AF_INET6, HostText.c_str(), aHost) == 1 ? AF_INET6 :
									       AF_UNSPEC;
	if(Family == AF_UNSPEC && Host.back() == '.')
		Host.remove_suffix(1);
	while(!List.empty())
	{
		const auto Start = List.find_first_not_of(", \t");
		if(Start == std::string_view::npos)
			break;
		List.remove_prefix(Start);
		const auto End = List.find_first_of(", \t");
		const auto Token = List.substr(0, End);
		List.remove_prefix(End == std::string_view::npos ? List.size() : End);
		if(Family != AF_UNSPEC)
		{
			const auto Slash = Token.find('/');
			int Bits = Family == AF_INET ? 32 : 128;
			if(Slash != std::string_view::npos)
			{
				const auto Prefix = Token.substr(Slash + 1);
				const auto Parsed = std::from_chars(Prefix.data(), Prefix.data() + Prefix.size(), Bits);
				if(Parsed.ec != std::errc() || Parsed.ptr != Prefix.data() + Prefix.size() || Bits < 0 || Bits > (Family == AF_INET ? 32 : 128))
					continue;
			}
			unsigned char aNetwork[16] = {};
			const std::string Network(Token.substr(0, Slash));
			if(Network.find('\0') != std::string::npos || inet_pton(Family, Network.c_str(), aNetwork) != 1)
				continue;
			bool Match = true;
			for(int Bit = 0; Bit < Bits; ++Bit)
			{
				if(((aHost[Bit / 8] ^ aNetwork[Bit / 8]) & (0x80 >> (Bit % 8))) != 0)
				{
					Match = false;
					break;
				}
			}
			if(Match)
				return true;
		}
		else
		{
			auto Domain = Token;
			if(!Domain.empty() && Domain.back() == '.')
				Domain.remove_suffix(1);
			if(!Domain.empty() && Domain.front() == '.')
				Domain.remove_prefix(1);
			if(Domain.empty() || Domain.size() > Host.size())
				continue;
			const auto Offset = Host.size() - Domain.size();
			if(Offset != 0 && Host[Offset - 1] != '.')
				continue;
			bool Match = true;
			for(size_t Index = 0; Index < Domain.size(); ++Index)
			{
				const auto Lower = [](char Character) { return Character >= 'A' && Character <= 'Z' ? Character + ('a' - 'A') : Character; };
				if(Lower(Host[Offset + Index]) != Lower(Domain[Index]))
				{
					Match = false;
					break;
				}
			}
			if(Match)
				return true;
		}
	}
	return false;
}

bool CHttpRequestCurl::ProxyBypassEvidence(std::string_view Host, std::string_view List, unsigned int RuntimeVersion)
{
	if(List == "*")
		return ProxyBypassed(Host, List);
	while(!List.empty())
	{
		const auto Start = List.find_first_not_of(", \t");
		if(Start == std::string_view::npos)
			break;
		List.remove_prefix(Start);
		const auto End = List.find_first_of(", \t");
		const auto Token = List.substr(0, End);
		List.remove_prefix(End == std::string_view::npos ? List.size() : End);
		if(Token == "*")
			continue;
		const auto Slash = Token.find('/');
		if(Slash != std::string_view::npos)
		{
			int Bits = -1;
			const auto Prefix = Token.substr(Slash + 1);
			const auto Parsed = std::from_chars(Prefix.data(), Prefix.data() + Prefix.size(), Bits);
			if(Parsed.ec != std::errc() || Parsed.ptr != Prefix.data() + Prefix.size())
				continue;
			// CIDR 在 curl 7.86 才支持；零位前缀和 8.8 的 IPv6 剩余位缺陷只降级证据。
			if(RuntimeVersion < 0x075600 || Bits == 0 || (RuntimeVersion == 0x080800 && Token.substr(0, Slash).find(':') != std::string_view::npos && Bits % 8 != 0))
				continue;
		}
		if(ProxyBypassed(Host, Token))
			return true;
	}
	return false;
}

bool CHttpRequestCurl::ProxyFallbackBypassed(int StatusCode, std::optional<bool> UsedProxy, bool BypassEvidence)
{
	// 最终 HTTP 响应的实测结果优先；中间 3xx 不能证明下一连接使用了代理。
	const bool FinalResponse = StatusCode >= 200 && (StatusCode < 300 || StatusCode >= 400);
	return BypassEvidence && (!FinalResponse || !UsedProxy.has_value() || !*UsedProxy);
}

size_t CHttpRequestCurl::OnHeader(char *pHeader, size_t HeaderSize)
{
	// `pHeader` is NOT null-terminated.
	// `pHeader` has a trailing newline.

	if(HeaderSize <= 1)
	{
		m_ResponseHeadersEnded = true;
		return HeaderSize;
	}
	if(m_ResponseHeadersEnded)
	{
		// redirect, clear old headers
		m_ResponseHeadersEnded = false;
		m_ResultDate = {};
		m_ResultLastModified = {};
		m_ResultRetryAfterSeconds = {};
		m_ResultContentRange = {};
	}
	// 重定向和 CONNECT 的响应头不能被当成最终分段响应。
	if(HeaderSize >= 5 && std::string_view(pHeader, HeaderSize).starts_with("HTTP/"))
	{
		const std::string_view Line(pHeader, HeaderSize);
		const auto Space = Line.find(' ');
		if(Space != std::string_view::npos)
			std::from_chars(Line.data() + Space + 1, Line.data() + Line.size(), m_StatusCode);
		m_ResultContentRange = {};
	}
	static const char CONTENT_RANGE[] = "Content-Range:";
	if(HeaderSize >= sizeof(CONTENT_RANGE) - 1 && str_startswith_nocase(pHeader, CONTENT_RANGE))
		m_ResultContentRange = ParseHttpContentRange(std::string_view(pHeader + sizeof(CONTENT_RANGE) - 1, HeaderSize - sizeof(CONTENT_RANGE) + 1));

	static const char DATE[] = "Date: ";
	static const char LAST_MODIFIED[] = "Last-Modified: ";

	// Trailing newline and null termination evens out.
	if(HeaderSize - 1 >= sizeof(DATE) - 1 && str_startswith_nocase(pHeader, DATE))
	{
		char aValue[128];
		str_truncate(aValue, sizeof(aValue), pHeader + (sizeof(DATE) - 1), HeaderSize - (sizeof(DATE) - 1) - 1);
		int64_t Value = curl_getdate(aValue, nullptr);
		if(Value != -1)
		{
			m_ResultDate = Value;
		}
	}
	if(HeaderSize - 1 >= sizeof(LAST_MODIFIED) - 1 && str_startswith_nocase(pHeader, LAST_MODIFIED))
	{
		char aValue[128];
		str_truncate(aValue, sizeof(aValue), pHeader + (sizeof(LAST_MODIFIED) - 1), HeaderSize - (sizeof(LAST_MODIFIED) - 1) - 1);
		int64_t Value = curl_getdate(aValue, nullptr);
		if(Value != -1)
		{
			m_ResultLastModified = Value;
		}
	}

	// 429 的等待窗口同时支持秒数和 HTTP 日期；只在请求完成后跨线程读取。
	static const char RETRY_AFTER[] = "Retry-After: ";
	if(HeaderSize > sizeof(RETRY_AFTER) && str_startswith_nocase(pHeader, RETRY_AFTER))
	{
		char aValue[128];
		str_truncate(aValue, sizeof(aValue), pHeader + sizeof(RETRY_AFTER) - 1, HeaderSize - sizeof(RETRY_AFTER));
		m_ResultRetryAfterSeconds = ParseRetryAfter(aValue, time_timestamp());
	}
	return HeaderSize;
}

void CHttpRequestCurl::OnCompletionInternal(CURL *pHandle, CURLcode Code)
{
	if(pHandle)
	{
		long StatusCode = 0;
		curl_easy_getinfo(pHandle, CURLINFO_RESPONSE_CODE, &StatusCode);
		m_StatusCode = StatusCode;
		std::optional<bool> UsedProxyResult;
#if LIBCURL_VERSION_NUM >= 0x080700
		long UsedProxy = 0;
		if(curl_easy_getinfo(pHandle, CURLINFO_USED_PROXY, &UsedProxy) == CURLE_OK)
		{
			UsedProxyResult = UsedProxy != 0;
			m_ResultUsedProxy = *UsedProxyResult;
		}
#endif
		// 没有 HTTP 状态也可能已按 NO_PROXY 直连，必须检查重定向后的最终主机。
		char *pEffectiveUrl = nullptr;
		if(curl_easy_getinfo(pHandle, CURLINFO_EFFECTIVE_URL, &pEffectiveUrl) == CURLE_OK && pEffectiveUrl)
		{
			const std::string Host = HttpUrlHost(pEffectiveUrl);
			if(!Host.empty())
			{
				const char *pNoProxy = std::getenv("no_proxy");
				if(!pNoProxy || !*pNoProxy)
					pNoProxy = std::getenv("NO_PROXY");
				// 实测最终连接优先，早期失败才使用与运行库兼容的旁路证据。
				const auto *pVersion = curl_version_info(CURLVERSION_NOW);
				const bool FinalProxyKnown = UsedProxyResult.has_value() && m_StatusCode >= 200 && (m_StatusCode < 300 || m_StatusCode >= 400);
				const bool BypassEvidence = pNoProxy && (FinalProxyKnown ? ProxyBypassed(Host, pNoProxy) :
											   pVersion && ProxyBypassEvidence(Host, pNoProxy, pVersion->version_num));
				if(ProxyFallbackBypassed(m_StatusCode, UsedProxyResult, BypassEvidence))
				{
					m_aProxy[0] = '\0';
					m_ResultUsedProxy = false;
				}
			}
		}
	}

	EHttpState State;
	// 前缀采样主动中止正文回调是成功；用户取消、状态错误与普通下载仍按原错误处理。
	const bool SampleComplete = Code == CURLE_WRITE_ERROR && m_ResponseSampleComplete && !IsAbortRequested() && m_StatusCode == 200;
	if(Code != CURLE_OK && !SampleComplete)
	{
		State = (Code == CURLE_ABORTED_BY_CALLBACK) ? EHttpState::ABORTED : EHttpState::ERROR;
		const bool IsShutdownAbort = State == EHttpState::ABORTED && str_comp(m_aErr, "Shutting down") == 0;
		if(!IsShutdownAbort && (g_Config.m_DbgHttp || (m_LogProgress >= HTTPLOG::FAILURE && HttpShouldLogFailure(State, m_AbortTriggeredByProgressCallback.load()))))
		{
			log_error("http", "%s failed. libcurl error (%u): %s", m_aUrl, Code, m_aErr[0] != '\0' ? m_aErr : curl_easy_strerror(Code));
		}
	}
	else
	{
		if(g_Config.m_DbgHttp || m_LogProgress >= HTTPLOG::ALL)
		{
			log_info("http", "task done: %s", m_aUrl);
		}
		State = EHttpState::DONE;
	}
	if(State == EHttpState::DONE && m_ByteRange &&
		(m_StatusCode != 206 || !m_ResultContentRange ||
			m_ResultContentRange->m_First != m_ByteRange->m_First || m_ResultContentRange->m_Last != m_ByteRange->m_Last ||
			m_ResponseLength != static_cast<uint64_t>(m_ByteRange->Length())))
		State = EHttpState::ERROR;

	IHttpRequest::OnCompletionInternal(State);
}

size_t CHttpRequestCurl::HeaderCallback(char *pData, size_t Size, size_t Number, void *pUser)
{
	dbg_assert(Size == 1, "invalid size parameter passed to header callback");
	return ((CHttpRequestCurl *)pUser)->OnHeader(pData, Number);
}

size_t CHttpRequestCurl::WriteCallback(char *pData, size_t Size, size_t Number, void *pUser)
{
	auto *pRequest = static_cast<CHttpRequestCurl *>(pUser);
	// 中间重定向正文不属于分段；终态 3xx 仍由完成检查拒绝。
	if(pRequest->m_ByteRange && pRequest->m_StatusCode >= 300 && pRequest->m_StatusCode < 400)
		return Size * Number;
	// 服务器忽略 Range 时不接受整包正文，更不能把 200 响应追加到分段文件。
	if(pRequest->m_ByteRange && (pRequest->m_StatusCode != 206 || !pRequest->m_ResultContentRange ||
					    pRequest->m_ResultContentRange->m_First != pRequest->m_ByteRange->m_First ||
					    pRequest->m_ResultContentRange->m_Last != pRequest->m_ByteRange->m_Last))
		return 0;
	return pRequest->OnData(pData, Size * Number);
}

int CHttpRequestCurl::ProgressCallback(void *pUser, double DlTotal, double DlCurr, double UlTotal, double UlCurr)
{
	CHttpRequestCurl *pTask = (CHttpRequestCurl *)pUser;
	pTask->m_Current.store(DlCurr, std::memory_order_relaxed);
	pTask->m_Size.store(DlTotal, std::memory_order_relaxed);
	pTask->m_Progress.store(DlTotal == 0.0 ? 0 : (100 * DlCurr) / DlTotal, std::memory_order_relaxed);
	if(pTask->m_pProgressCallback != nullptr)
	{
		pTask->m_pProgressCallback->OnProgress();
	}
	if(pTask->m_Abort)
	{
		pTask->m_AbortTriggeredByProgressCallback = true;
		return -1;
	}
	return 0;
}

std::unique_ptr<IHttpRequest> CreateHttpRequest(const char *pUrl)
{
	return std::make_unique<CHttpRequestCurl>(pUrl);
}

void EscapeUrl(char *pBuf, size_t Size, const char *pStr)
{
	char *pEsc = curl_easy_escape(nullptr, pStr, 0);
	str_copy(pBuf, pEsc, Size);
	curl_free(pEsc);
}

bool CHttpCurl::Init(std::chrono::milliseconds ShutdownDelay)
{
	m_ShutdownDelay = ShutdownDelay;

#if !defined(CONF_FAMILY_WINDOWS)
	// As a multithreaded application we have to tell curl to not install signal
	// handlers and instead ignore SIGPIPE from OpenSSL ourselves.
	signal(SIGPIPE, SIG_IGN);
#endif
	m_pThread = thread_init(CHttpCurl::ThreadMain, this, "http");

	std::unique_lock Lock(m_Lock);
	m_ConditionVariableInit.wait(Lock, [this]() { return m_State != CHttpCurl::UNINITIALIZED; });
	if(m_State != CHttpCurl::RUNNING)
	{
		return false;
	}

	return true;
}

void CHttpCurl::Shutdown()
{
	std::unique_lock Lock(m_Lock);
	if(m_Shutdown || m_State != CHttpCurl::RUNNING)
		return;

	m_Shutdown = true;
	curl_multi_wakeup(m_pMultiH);
}

CHttpCurl::~CHttpCurl()
{
	if(!m_pThread)
		return;

	Shutdown();
	thread_wait(m_pThread);
}

void CHttpCurl::Run(std::shared_ptr<IHttpRequest> pRequest)
{
	std::shared_ptr<CHttpRequestCurl> pRequestImpl = std::static_pointer_cast<CHttpRequestCurl>(pRequest);
	std::unique_lock Lock(m_Lock);
	if(m_Shutdown || m_State == CHttpCurl::ERROR)
	{
		str_copy(pRequestImpl->m_aErr, "Shutting down");
		pRequestImpl->OnCompletionInternal(nullptr, CURLE_ABORTED_BY_CALLBACK);
		return;
	}
	m_ConditionVariableInit.wait(Lock, [this]() { return m_State != CHttpCurl::UNINITIALIZED; });
	m_PendingRequests.emplace_back(pRequestImpl);
	curl_multi_wakeup(m_pMultiH);
}

bool CHttpCurl::HasIpresolveBug() const
{
	// curl < 7.77.0 doesn't use CURLOPT_IPRESOLVE correctly wrt.
	// connection caches.
	return curl_version_info(CURLVERSION_NOW)->version_num < 0x074d00;
}

void CHttpCurl::ThreadMain(void *pUser)
{
	static_cast<CHttpCurl *>(pUser)->RunLoop();
}

void CHttpCurl::RunLoop()
{
	std::unique_lock Lock(m_Lock);
	if(curl_global_init(CURL_GLOBAL_DEFAULT))
	{
		log_error("http", "curl_global_init failed");
		m_State = CHttpCurl::ERROR;
		m_ConditionVariableInit.notify_all();
		return;
	}

	m_pMultiH = curl_multi_init();
	if(!m_pMultiH)
	{
		log_error("http", "curl_multi_init failed");
		m_State = CHttpCurl::ERROR;
		m_ConditionVariableInit.notify_all();
		return;
	}

	// print curl version
	{
		curl_version_info_data *pVersion = curl_version_info(CURLVERSION_NOW);
		log_info("http", "libcurl version %s (compiled = " LIBCURL_VERSION ")", pVersion->version);
	}

	m_State = CHttpCurl::RUNNING;
	m_ConditionVariableInit.notify_all();
	Lock.unlock();
	m_NextTimeout = std::numeric_limits<int>::max();
	std::unordered_map<std::string, size_t> RunningRequestsPerHost;
	RunningRequestsPerHost.reserve(HTTP_MAX_CONCURRENT_REQUESTS);

	while(m_State == CHttpCurl::RUNNING)
	{
		int Events = 0;
		const CURLMcode PollCode = curl_multi_poll(m_pMultiH, nullptr, 0, m_NextTimeout, &Events);

		// We may have been woken up for a shutdown
		if(m_Shutdown)
		{
			if(m_RunningRequests.empty() && m_PendingRequests.empty())
				break;

			auto Now = std::chrono::steady_clock::now();
			if(!m_ShutdownTime.has_value())
			{
				m_ShutdownTime = Now + m_ShutdownDelay;
				m_NextTimeout = m_ShutdownDelay.count();
			}
			else if(m_ShutdownTime < Now)
			{
				break;
			}
		}

		if(PollCode != CURLM_OK)
		{
			Lock.lock();
			log_error("http", "curl_multi_poll failed: %s", curl_multi_strerror(PollCode));
			m_State = CHttpCurl::ERROR;
			break;
		}

		const CURLMcode PerformCode = curl_multi_perform(m_pMultiH, &Events);
		if(PerformCode != CURLM_OK)
		{
			Lock.lock();
			log_error("http", "curl_multi_perform failed: %s", curl_multi_strerror(PerformCode));
			m_State = CHttpCurl::ERROR;
			break;
		}

		struct CURLMsg *pMsg;
		while((pMsg = curl_multi_info_read(m_pMultiH, &Events)))
		{
			if(pMsg->msg == CURLMSG_DONE)
			{
				auto RequestIt = m_RunningRequests.find(pMsg->easy_handle);
				dbg_assert(RequestIt != m_RunningRequests.end(), "Running handle not added to map");
				const std::string HostKey = HttpUrlHost(RequestIt->second->m_aUrl);
				if(!HostKey.empty())
				{
					auto HostIt = RunningRequestsPerHost.find(HostKey);
					if(HostIt != RunningRequestsPerHost.end())
					{
						if(HostIt->second > 1)
							--HostIt->second;
						else
							RunningRequestsPerHost.erase(HostIt);
					}
				}
				auto pRequest = std::move(RequestIt->second);
				m_RunningRequests.erase(RequestIt);
				pRequest->OnCompletionInternal(pMsg->easy_handle, pMsg->data.result);
				curl_multi_remove_handle(m_pMultiH, pMsg->easy_handle);
				curl_easy_cleanup(pMsg->easy_handle);
			}
		}

		decltype(m_PendingRequests) NewRequests = {};
		Lock.lock();
		std::swap(m_PendingRequests, NewRequests);
		Lock.unlock();

		decltype(m_PendingRequests) DeferredRequests = {};
		while(!NewRequests.empty())
		{
			auto &pRequest = NewRequests.front();
			if(pRequest->IsAbortRequested())
			{
				pRequest->m_AbortTriggeredByProgressCallback = true;
				pRequest->OnCompletionInternal(nullptr, CURLE_ABORTED_BY_CALLBACK);
				NewRequests.pop_front();
				continue;
			}
			if(g_Config.m_DbgHttp)
				log_debug("http", "task: %s %s", CHttpRequestCurl::GetRequestType(pRequest->m_Type), pRequest->m_aUrl);

			if(pRequest->ShouldSkipRequest())
			{
				if(pRequest->m_pProgressCallback != nullptr)
				{
					pRequest->m_pProgressCallback->OnCompletion(EHttpState::DONE);
				}
				{
					std::unique_lock WaitLock(pRequest->m_WaitMutex);
					pRequest->m_State = EHttpState::DONE;
				}
				pRequest->m_WaitCondition.notify_all();
				NewRequests.pop_front();
				continue;
			}

			const std::string HostKey = HttpUrlHost(pRequest->m_aUrl);
			const size_t RunningForHost = HostKey.empty() ? 0 : RunningRequestsPerHost[HostKey];
			if(m_RunningRequests.size() >= HTTP_MAX_CONCURRENT_REQUESTS ||
				(!HostKey.empty() && RunningForHost >= HTTP_MAX_CONCURRENT_REQUESTS_PER_HOST))
			{
				DeferredRequests.push_back(std::move(pRequest));
				NewRequests.pop_front();
				continue;
			}

			CURL *pEH = curl_easy_init();
			if(!pEH)
			{
				log_error("http", "curl_easy_init failed");
				goto error_init;
			}

			if(!pRequest->ConfigureHandle(pEH))
			{
				curl_easy_cleanup(pEH);
				str_copy(pRequest->m_aErr, "Failed to initialize request");
				pRequest->OnCompletionInternal(nullptr, CURLE_ABORTED_BY_CALLBACK);
				NewRequests.pop_front();
				continue;
			}

			if(curl_multi_add_handle(m_pMultiH, pEH) != CURLM_OK)
			{
				log_error("http", "curl_multi_add_handle failed");
				goto error_configure;
			}

			{
				std::unique_lock WaitLock(pRequest->m_WaitMutex);
				pRequest->m_State = EHttpState::RUNNING;
			}
			m_RunningRequests.emplace(pEH, std::move(pRequest));
			if(!HostKey.empty())
				++RunningRequestsPerHost[HostKey];
			NewRequests.pop_front();
			continue;

		error_configure:
			curl_easy_cleanup(pEH);
		error_init:
			Lock.lock();
			m_State = CHttpCurl::ERROR;
			break;
		}

		if(m_State == CHttpCurl::ERROR)
		{
			if(!NewRequests.empty())
				m_PendingRequests.insert(m_PendingRequests.end(), std::make_move_iterator(NewRequests.begin()), std::make_move_iterator(NewRequests.end()));
			if(!DeferredRequests.empty())
				m_PendingRequests.insert(m_PendingRequests.end(), std::make_move_iterator(DeferredRequests.begin()), std::make_move_iterator(DeferredRequests.end()));
			break;
		}

		if(!DeferredRequests.empty())
		{
			Lock.lock();
			m_PendingRequests.insert(m_PendingRequests.begin(), std::make_move_iterator(DeferredRequests.begin()), std::make_move_iterator(DeferredRequests.end()));
			Lock.unlock();
		}
	}

	if(!Lock.owns_lock())
		Lock.lock();

	bool Cleanup = m_State != CHttpCurl::ERROR;
	for(auto &pRequest : m_PendingRequests)
	{
		str_copy(pRequest->m_aErr, "Shutting down");
		pRequest->OnCompletionInternal(nullptr, CURLE_ABORTED_BY_CALLBACK);
	}
	m_PendingRequests.clear();

	for(auto &ReqPair : m_RunningRequests)
	{
		auto &[pHandle, pRequest] = ReqPair;

		str_copy(pRequest->m_aErr, "Shutting down");
		pRequest->OnCompletionInternal(pHandle, CURLE_ABORTED_BY_CALLBACK);

		if(Cleanup)
		{
			curl_multi_remove_handle(m_pMultiH, pHandle);
			curl_easy_cleanup(pHandle);
		}
	}
	m_RunningRequests.clear();

	if(Cleanup)
	{
		curl_multi_cleanup(m_pMultiH);
		curl_global_cleanup();
	}
}

IEngineHttp *CreateEngineHttp()
{
	return new CHttpCurl;
}

#endif // !CONF_PLATFORM_EMSCRIPTEN
