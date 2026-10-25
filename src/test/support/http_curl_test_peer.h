#ifndef TEST_SUPPORT_HTTP_CURL_TEST_PEER_H
#define TEST_SUPPORT_HTTP_CURL_TEST_PEER_H

#include <base/time.h>

#include <engine/shared/http_curl.h>

#include <string>

// 仅提供真实后端回调的输入入口，不复写响应处理或完成状态逻辑。
class CHttpRequestCurlTestPeer
{
public:
	static bool Prepare(CHttpRequestCurl &Request, CURL *pHandle)
	{
		Request.m_aErr[0] = '\0';
		if(!Request.ConfigureHandle(pHandle))
			return false;
		Request.m_State = EHttpState::RUNNING;
		return true;
	}
	static void HeadersReceivedAgo(CHttpRequestCurl &Request, int Seconds) { Request.m_ResultHeadersReceivedAt = time_get_nanoseconds() - std::chrono::seconds(Seconds); }
	static CURLSH *DnsShare(CHttpRequestCurl &Request) { return Request.m_pDnsShare; }
	static const curl_slist *DnsBootstrap(CHttpRequestCurl &Request) { return Request.m_pDnsBootstrap; }
	static size_t Header(CHttpRequestCurl &Request, std::string Text)
	{
		return Request.OnHeader(Text.data(), Text.size());
	}
	static size_t Body(CHttpRequestCurl &Request, std::string Text)
	{
		return CHttpRequestCurl::WriteCallback(Text.data(), 1, Text.size(), &Request);
	}
	static size_t BodySize(CHttpRequestCurl &Request, char *pData, size_t Size, size_t Number)
	{
		return CHttpRequestCurl::WriteCallback(pData, Size, Number, &Request);
	}
	static int Progress(CHttpRequestCurl &Request, double Total, double Current)
	{
		return CHttpRequestCurl::ProgressCallback(&Request, Total, Current, 0, 0);
	}
	static void Complete(CHttpRequestCurl &Request, CURL *pHandle = nullptr, CURLcode Code = CURLE_OK)
	{
		Request.m_aErr[0] = '\0';
		Request.OnCompletionInternal(pHandle, Code);
	}
};

#endif
