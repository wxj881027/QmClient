#include <base/detect.h>
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <memory>
#include <optional>
#include <string>

TEST(QmUpdateHttp, RetryAfterSecondsAcceptsWhitespaceAndRejectsMalformedNumbers)
{
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter("900\r\n", 0), 900);
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter(" 30 \t", 0), 30);
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter("0", 0), 0);
	for(const char *pValue : {"", " ", "-1", "12oops", "999999999999999999999999", "<html>"})
		EXPECT_FALSE(CHttpRequestCurl::ParseRetryAfter(pValue, 0).has_value()) << pValue;
}

TEST(QmUpdateHttp, RetryAfterHttpDateUsesRemainingTimeAndClampsPastDate)
{
	const char *pDate = "Wed, 21 Oct 2015 07:28:00 GMT";
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter(pDate, 1445412420), 60);
	EXPECT_EQ(CHttpRequestCurl::ParseRetryAfter(pDate, 1445412600), 0);
}

#include <test/support/http_curl_test_peer.h>

TEST(QmUpdateHttp, CancellationWinsOverSuccessfulTransportCompletion)
{
	CHttpRequestCurl Request("https://cancel.test/result");
	Request.Abort();
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
}

TEST(QmUpdateHttp, CancellationWinsOverTransportFailure)
{
	CHttpRequestCurl Request("https://cancel.test/result");
	Request.Abort();
	CHttpRequestCurlTestPeer::Complete(Request, nullptr, CURLE_COULDNT_CONNECT);
	EXPECT_EQ(Request.State(), EHttpState::ABORTED);
}

// 只安排 curl 句柄和额度，不执行网络；取消与完成均调用生产调度器。
class CHttpCurlTestPeer
{
	CHttpCurl m_Http;

public:
	CHttpCurlTestPeer() { m_Http.m_pMultiH = curl_multi_init(); }
	~CHttpCurlTestPeer()
	{
		while(!m_Http.m_RunningRequests.empty())
			m_Http.CompleteRunningRequest(m_Http.m_RunningRequests.begin()->first, CURLE_ABORTED_BY_CALLBACK);
		curl_multi_cleanup(m_Http.m_pMultiH);
		m_Http.m_pMultiH = nullptr;
	}
	bool Add(const std::shared_ptr<CHttpRequestCurl> &pRequest, const char *pHost)
	{
		if(!m_Http.m_pMultiH)
			return false;
		CURL *pHandle = curl_easy_init();
		if(!pHandle)
			return false;
		if(!CHttpRequestCurlTestPeer::Prepare(*pRequest, pHandle) || curl_multi_add_handle(m_Http.m_pMultiH, pHandle) != CURLM_OK)
		{
			curl_easy_cleanup(pHandle);
			return false;
		}
		m_Http.m_RunningRequests.emplace(pHandle, pRequest);
		pRequest->m_HostKey = pHost;
		return true;
	}
	void Cancel() { m_Http.CancelAbortedRequests(); }
	size_t Size() const { return m_Http.m_RunningRequests.size(); }
	size_t HostCount(const char *pHost) const { return m_Http.RunningForHost(pHost); }
};

TEST(QmUpdateHttp, CancellationWithoutProgressReleasesOnlyCancelledHostCapacity)
{
	CHttpCurlTestPeer Http;
	auto pCancelled = std::make_shared<CHttpRequestCurl>("https://same.test/first");
	auto pSibling = std::make_shared<CHttpRequestCurl>("https://same.test/second");
	auto pOther = std::make_shared<CHttpRequestCurl>("https://other.test/first");
	ASSERT_TRUE(Http.Add(pCancelled, "same.test"));
	ASSERT_TRUE(Http.Add(pSibling, "same.test"));
	ASSERT_TRUE(Http.Add(pOther, "other.test"));
	pCancelled->Abort();
	Http.Cancel();
	EXPECT_EQ(pCancelled->State(), EHttpState::ABORTED);
	EXPECT_FALSE(pSibling->Done());
	EXPECT_FALSE(pOther->Done());
	EXPECT_EQ(Http.Size(), 2u);
	EXPECT_EQ(Http.HostCount("same.test"), 1u);
	EXPECT_EQ(Http.HostCount("other.test"), 1u);
	Http.Cancel();
	EXPECT_EQ(Http.Size(), 2u);
	pSibling->Abort();
	Http.Cancel();
	EXPECT_EQ(Http.HostCount("same.test"), 0u);
	EXPECT_EQ(Http.HostCount("other.test"), 1u);
	EXPECT_EQ(Http.Size(), 1u);
}

TEST(QmUpdateHttp, ProxyBypassMatchesDomainBoundaryCaseAndDots)
{
	for(const char *pHost : {"example.com", "WWW.Example.COM", "www.example.com."})
	{
		EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed(pHost, ".EXAMPLE.com.")) << pHost;
	}
	for(const char *pHost : {"notexample.com", "example.com.attacker", "other.test"})
		EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed(pHost, "example.com")) << pHost;
}

TEST(QmUpdateHttp, ProxyBypassHandlesCommaWhitespaceAndOnlyStandaloneWildcard)
{
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("target.test", " , other.test,\t target.test , "));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("target.test", "other.test target.test"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("target.test", "*"));
	for(const char *pList : {"", " , \t", "*,other.test", " * ", "*.test", "."})
		EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("target.test", pList)) << pList;
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("", "*"));
}

TEST(QmUpdateHttp, ProxyBypassMatchesStrictIpv4AndNetworkPrefixes)
{
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("192.0.2.129", "192.0.2.129"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("192.0.2.129", "192.0.2.128/25"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("192.0.2.129", "192.0.2.129/32"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("192.0.2.127", "192.0.2.128/25"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("192.0.2.128", "192.0.2.129/32"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("192.0.2.129", "2.129"));
	for(const char *pList : {"192.0.2.129oops", "192.0.2.128/25oops", "192.0.2.128/-1", "192.0.2.128/33", "192.0.2.128/", "192.0.2.128/+25"})
		EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("192.0.2.129", pList)) << pList;
}

TEST(QmUpdateHttp, ProxyBypassMatchesIpv6EquivalentAddressesAndNetworkPrefixes)
{
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("[2001:db8::1]", "2001:0db8:0:0:0:0:0:1"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("2001:db8::1", "2001:db8::/32"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("[2001:db8::1]", "2001:db8::1/128"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("[2001:db9::1]", "2001:db8::/32"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("[2001:db8::2]", "2001:db8::1/128"));
	for(const char *pList : {"2001:db8::1oops", "2001:db8::/129", "2001:db8::/-1", "[2001:db8::1]", "192.0.2.0/24"})
		EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("[2001:db8::1]", pList)) << pList;
}

TEST(QmUpdateHttp, ProxyBypassIpv6PartialBytePrefixUsesStandardNetworkBits)
{
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("[2001:db8::1]", "2001:db8::/33"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("[2001:db8:8000::1]", "2001:db8::/33"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("[2001:db9::1]", "2001:db8::/33"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("[2001:db8::2]", "2001:db8::2/127"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("[2001:db8::3]", "2001:db8::2/127"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("[2001:db8::4]", "2001:db8::2/127"));
}

TEST(QmUpdateHttp, ProxyBypassZeroPrefixMatchesAnyAddressInItsFamily)
{
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("192.0.2.2", "0.0.0.0/0"));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassed("[2001:db8::2]", "::/0"));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassed("192.0.2.2", "::/0"));
}

TEST(QmUpdateHttp, EarlyProxyEvidenceDowngradesOnlyUnreliablePrefixes)
{
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassEvidence("[2001:db8::1]", "2001:db8::/33", 0x080800));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassEvidence("[2001:db8::1]", "2001:db8::/32", 0x080800));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassEvidence("192.0.2.2", "0.0.0.0/0", 0x080800));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassEvidence("[2001:db8::1]", "2001:db8::/33,2001:db8::1", 0x080800));
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassEvidence("target.test", "*,other.test", 0x080800));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassEvidence("target.test", "*", 0x080800));
}

TEST(QmUpdateHttp, EarlyProxyEvidenceRespectsRuntimeCidrSupport)
{
	EXPECT_FALSE(CHttpRequestCurl::ProxyBypassEvidence("192.0.2.2", "192.0.2.0/24", 0x075500));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassEvidence("192.0.2.2", "192.0.2.0/24", 0x075600));
	EXPECT_TRUE(CHttpRequestCurl::ProxyBypassEvidence("192.0.2.2", "192.0.2.0/24,192.0.2.2", 0x075500));
}

TEST(QmUpdateHttp, FinalProxyMeasurementOverridesMatcherEvidence)
{
	EXPECT_FALSE(CHttpRequestCurl::ProxyFallbackBypassed(503, true, true));
	EXPECT_TRUE(CHttpRequestCurl::ProxyFallbackBypassed(503, false, true));
	EXPECT_FALSE(CHttpRequestCurl::ProxyFallbackBypassed(503, false, false));
}

TEST(QmUpdateHttp, EarlyFailureUsesFinalHostEvidenceDespiteHistoricalProxy)
{
	EXPECT_TRUE(CHttpRequestCurl::ProxyFallbackBypassed(0, false, true));
	EXPECT_TRUE(CHttpRequestCurl::ProxyFallbackBypassed(302, true, true));
	EXPECT_TRUE(CHttpRequestCurl::ProxyFallbackBypassed(0, std::nullopt, true));
	EXPECT_FALSE(CHttpRequestCurl::ProxyFallbackBypassed(0, false, false));
	EXPECT_FALSE(CHttpRequestCurl::ProxyFallbackBypassed(302, true, false));
}

TEST(QmUpdateHttp, RangeRedirectBodyIsDiscardedBeforeExactFinalByte)
{
	CHttpRequestCurl Request("https://example.test/redirect");
	Request.ByteRange(0, 0);
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 302 Found\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Content-Length: 100\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Location: /final\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, std::string(100, 'r')), 100U);
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 206 Partial Content\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Content-Range: bytes 0-0/10\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "x"), 1U);
	CHttpRequestCurlTestPeer::Complete(Request);
	ASSERT_EQ(Request.State(), EHttpState::DONE);
	unsigned char *pResult;
	size_t Length;
	Request.Result(&pResult, &Length);
	ASSERT_EQ(Length, 1U);
	EXPECT_EQ(pResult[0], 'x');
}

TEST(QmUpdateHttp, RangeTerminalRedirectCannotCompleteSuccessfully)
{
	CHttpRequestCurl Request("https://example.test/no-location");
	Request.ByteRange(0, 0);
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 302 Found\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "redirect"), 8U);
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(QmUpdateHttp, RangeFinalBodyRejectsOversizeBeforeStoringBytes)
{
	CHttpRequestCurl Request("https://example.test/final");
	Request.ByteRange(0, 0);
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 206 Partial Content\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Content-Range: bytes 0-0/10\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "xx"), 0U);
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(QmUpdateHttp, RangeCompletionRejectsMissingFinalBytes)
{
	CHttpRequestCurl Request("https://example.test/final");
	Request.ByteRange(0, 0);
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 206 Partial Content\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Content-Range: bytes 0-0/10\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
}

TEST(QmUpdateHttp, RangeRejectsIgnoredOrMismatchedFinalRange)
{
	for(const char *pStatus : {"HTTP/1.1 200 OK\r\n", "HTTP/1.1 206 Partial Content\r\n"})
	{
		SCOPED_TRACE(pStatus);
		CHttpRequestCurl Request("https://example.test/final");
		Request.ByteRange(0, 0);
		CHttpRequestCurlTestPeer::Header(Request, pStatus);
		CHttpRequestCurlTestPeer::Header(Request, "Content-Range: bytes 1-1/10\r\n");
		CHttpRequestCurlTestPeer::Header(Request, "\r\n");
		EXPECT_EQ(CHttpRequestCurlTestPeer::Body(Request, "x"), 0U);
		CHttpRequestCurlTestPeer::Complete(Request);
		EXPECT_EQ(Request.State(), EHttpState::ERROR);
	}
}

// 只注入 curl 完成时提供的 URL 元数据，不发起网络请求；环境变量逐项恢复。
class QmUpdateHttpEnvironment : public ::testing::Test
{
protected:
	std::optional<std::string> m_NoProxy;
	std::optional<std::string> m_UpperNoProxy;
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> m_pHandle{nullptr, curl_easy_cleanup};

	static std::optional<std::string> ReadEnvironment(const char *pName)
	{
		const char *pValue = std::getenv(pName);
		return pValue ? std::optional<std::string>(pValue) : std::nullopt;
	}
	static int SetEnvironment(const char *pName, const char *pValue)
	{
#if defined(CONF_FAMILY_WINDOWS)
		return _putenv_s(pName, pValue ? pValue : "");
#else
		return pValue ? setenv(pName, pValue, 1) : unsetenv(pName);
#endif
	}
	void SetUp() override
	{
		m_NoProxy = ReadEnvironment("no_proxy");
		m_UpperNoProxy = ReadEnvironment("NO_PROXY");
		ASSERT_EQ(SetEnvironment("no_proxy", nullptr), 0);
		ASSERT_EQ(SetEnvironment("NO_PROXY", nullptr), 0);
		m_pHandle.reset(curl_easy_init());
		ASSERT_NE(m_pHandle, nullptr);
	}
	void TearDown() override
	{
		m_pHandle.reset();
		EXPECT_EQ(SetEnvironment("no_proxy", m_NoProxy ? m_NoProxy->c_str() : nullptr), 0);
		EXPECT_EQ(SetEnvironment("NO_PROXY", m_UpperNoProxy ? m_UpperNoProxy->c_str() : nullptr), 0);
	}
};

TEST_F(QmUpdateHttpEnvironment, CompletionUsesFinalHostRatherThanInitialHost)
{
	ASSERT_EQ(SetEnvironment("no_proxy", "final.test"), 0);
	ASSERT_EQ(curl_easy_setopt(m_pHandle.get(), CURLOPT_URL, "https://user:p%40ss@FINAL.test:8443?next=other.test"), CURLE_OK);
	CHttpRequestCurl Request("https://initial.test/redirect");
	Request.Proxy("http://proxy.test:8080");
	Request.LogProgress(HTTPLOG::NONE);
	CHttpRequestCurlTestPeer::Complete(Request, m_pHandle.get(), CURLE_COULDNT_CONNECT);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_STREQ(Request.ProxyUrl(), "");
	EXPECT_FALSE(Request.CompletedUsedProxy());
}

TEST_F(QmUpdateHttpEnvironment, InitialHostBypassCannotClearFinalHostProxy)
{
	ASSERT_EQ(SetEnvironment("no_proxy", "initial.test"), 0);
	ASSERT_EQ(curl_easy_setopt(m_pHandle.get(), CURLOPT_URL, "https://final.test:443/path"), CURLE_OK);
	CHttpRequestCurl Request("https://initial.test/redirect");
	Request.Proxy("http://proxy.test:8080");
	Request.LogProgress(HTTPLOG::NONE);
	CHttpRequestCurlTestPeer::Complete(Request, m_pHandle.get(), CURLE_COULDNT_CONNECT);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_STREQ(Request.ProxyUrl(), "http://proxy.test:8080");
}

TEST_F(QmUpdateHttpEnvironment, FinalIpv6HostWithUserinfoPortAndZoneCanBypass)
{
	ASSERT_EQ(SetEnvironment("NO_PROXY", "fe80::1"), 0);
	ASSERT_EQ(curl_easy_setopt(m_pHandle.get(), CURLOPT_URL, "http://user:pass@[fe80::1%25eth0]:8080/"), CURLE_OK);
	CHttpRequestCurl Request("https://initial.test/redirect");
	Request.Proxy("http://proxy.test:8080");
	Request.LogProgress(HTTPLOG::NONE);
	CHttpRequestCurlTestPeer::Complete(Request, m_pHandle.get(), CURLE_COULDNT_CONNECT);
	EXPECT_EQ(Request.State(), EHttpState::ERROR);
	EXPECT_STREQ(Request.ProxyUrl(), "");
}

#if !defined(CONF_FAMILY_WINDOWS)
// Windows 环境变量名不区分大小写，不能在那里构造两份独立的旁路列表。
TEST_F(QmUpdateHttpEnvironment, EmptyLowercaseFallsBackToUppercase)
{
	ASSERT_EQ(SetEnvironment("no_proxy", ""), 0);
	ASSERT_EQ(SetEnvironment("NO_PROXY", "final.test"), 0);
	ASSERT_EQ(curl_easy_setopt(m_pHandle.get(), CURLOPT_URL, "https://final.test/"), CURLE_OK);
	CHttpRequestCurl Request("https://initial.test/");
	Request.Proxy("http://proxy.test:8080");
	Request.LogProgress(HTTPLOG::NONE);
	CHttpRequestCurlTestPeer::Complete(Request, m_pHandle.get(), CURLE_COULDNT_CONNECT);
	EXPECT_STREQ(Request.ProxyUrl(), "");
}

TEST_F(QmUpdateHttpEnvironment, NonemptyLowercaseTakesPriorityOverUppercase)
{
	ASSERT_EQ(SetEnvironment("no_proxy", "other.test"), 0);
	ASSERT_EQ(SetEnvironment("NO_PROXY", "final.test"), 0);
	ASSERT_EQ(curl_easy_setopt(m_pHandle.get(), CURLOPT_URL, "https://final.test/"), CURLE_OK);
	CHttpRequestCurl Request("https://initial.test/");
	Request.Proxy("http://proxy.test:8080");
	Request.LogProgress(HTTPLOG::NONE);
	CHttpRequestCurlTestPeer::Complete(Request, m_pHandle.get(), CURLE_COULDNT_CONNECT);
	EXPECT_STREQ(Request.ProxyUrl(), "http://proxy.test:8080");
}
#endif
TEST(QmServerListHttp, EncryptedDnsConfigurationUsesSeparateCachePerRequestAndBootstrapOnlyForResolver)
{
	CHttpRequestCurl First("https://master1.ddnet.org/ddnet/15/servers.json");
	CHttpRequestCurl Second("https://master2.ddnet.org/ddnet/15/servers.json");
	CHttpRequestCurl Custom("https://master1.ddnet.org.evil.test/ddnet/15/servers.json");
	ASSERT_TRUE(First.ServerListDnsFallback());
	ASSERT_TRUE(Second.ServerListDnsFallback());
	EXPECT_FALSE(Custom.ServerListDnsFallback());
	CURL *pFirst = curl_easy_init();
	CURL *pSecond = curl_easy_init();
	ASSERT_NE(pFirst, nullptr);
	ASSERT_NE(pSecond, nullptr);
	EXPECT_TRUE(CHttpRequestCurlTestPeer::Prepare(First, pFirst));
	EXPECT_TRUE(CHttpRequestCurlTestPeer::Prepare(Second, pSecond));
	EXPECT_NE(CHttpRequestCurlTestPeer::DnsShare(First), nullptr);
	EXPECT_NE(CHttpRequestCurlTestPeer::DnsShare(First), CHttpRequestCurlTestPeer::DnsShare(Second));
	ASSERT_NE(CHttpRequestCurlTestPeer::DnsBootstrap(First), nullptr);
	EXPECT_STREQ(CHttpRequestCurlTestPeer::DnsBootstrap(First)->data, "dns.alidns.com:443:223.5.5.5,223.6.6.6");
	EXPECT_EQ(CHttpRequestCurlTestPeer::DnsBootstrap(First)->next, nullptr);
	curl_easy_cleanup(pFirst);
	curl_easy_cleanup(pSecond);
	CHttpRequestCurlTestPeer::Complete(First, nullptr, CURLE_ABORTED_BY_CALLBACK);
	CHttpRequestCurlTestPeer::Complete(Second, nullptr, CURLE_ABORTED_BY_CALLBACK);
}

TEST(QmHttpFreshness, AgeHeaderWithoutLastModifiedSurvivesRealHeaderParsing)
{
	CHttpRequestCurl Request("https://mirror.test/list");
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "aGe:\t42 \r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::Complete(Request);
	ASSERT_TRUE(Request.ResultAgeSeconds().has_value());
	EXPECT_GE(*Request.ResultAgeSeconds(), 42);
	EXPECT_LE(*Request.ResultAgeSeconds(), 43);
}

TEST(QmHttpFreshness, InvalidAgeValuesAreUnknownAndLargeKnownAgeRemainsLarge)
{
	for(const char *pAge : {"-1", "+10", "garbage", "10oops", ""})
	{
		SCOPED_TRACE(pAge);
		CHttpRequestCurl Request("https://mirror.test/list");
		CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
		CHttpRequestCurlTestPeer::Header(Request, std::string("Age: ") + pAge + "\r\n");
		CHttpRequestCurlTestPeer::Complete(Request);
		EXPECT_FALSE(Request.ResultAgeSeconds().has_value());
	}
	CHttpRequestCurl Request("https://mirror.test/list");
	CHttpRequestCurlTestPeer::Header(Request, "Age: 9223372036854775808\r\n");
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_EQ(Request.ResultAgeSeconds(), INT64_MAX);
}

TEST(QmHttpFreshness, RedirectAgeAndLastModifiedCannotLeakIntoFinalResponse)
{
	CHttpRequestCurl Request("https://mirror.test/redirect");
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 302 Found\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Age: 90000\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Date: Wed, 21 Oct 2015 07:28:00 GMT\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Last-Modified: Wed, 21 Oct 2015 07:27:00 GMT\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Age: 5\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::Complete(Request);
	EXPECT_FALSE(Request.ResultLastModified().has_value());
	ASSERT_TRUE(Request.ResultAgeSeconds().has_value());
	EXPECT_GE(*Request.ResultAgeSeconds(), 5);
	EXPECT_LE(*Request.ResultAgeSeconds(), 6);
}

TEST(QmHttpFreshness, OlderDateAndLastModifiedTakePrecedenceOverSmallerAge)
{
	CHttpRequestCurl Request("https://mirror.test/list");
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Age: 2\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Date: Wed, 21 Oct 2015 07:28:00 GMT\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Last-Modified: Wed, 21 Oct 2015 07:27:00 GMT\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::Complete(Request);
	ASSERT_TRUE(Request.ResultAgeSeconds().has_value());
	EXPECT_GT(*Request.ResultAgeSeconds(), 86400);
}

TEST(QmHttpFreshness, AgeIncludesResidentTimeAfterHeaderReception)
{
	CHttpRequestCurl Request("https://mirror.test/list");
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Age: 40\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::HeadersReceivedAgo(Request, 20);
	CHttpRequestCurlTestPeer::Complete(Request);
	ASSERT_TRUE(Request.ResultAgeSeconds().has_value());
	EXPECT_GE(*Request.ResultAgeSeconds(), 60);
	EXPECT_LE(*Request.ResultAgeSeconds(), 61);
}

TEST(QmHttpFreshness, AgeAndModificationAgeAreNotDoubleCounted)
{
	const auto HttpDate = [](int64_t Timestamp) {
		const time_t Value = Timestamp;
		char aDate[64];
		std::strftime(aDate, sizeof(aDate), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&Value));
		return std::string(aDate);
	};
	CHttpRequestCurl Request("https://mirror.test/list");
	const int64_t Now = time_timestamp();
	CHttpRequestCurlTestPeer::Header(Request, "HTTP/1.1 200 OK\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Age: 120\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Date: " + HttpDate(Now) + "\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "Last-Modified: " + HttpDate(Now - 60) + "\r\n");
	CHttpRequestCurlTestPeer::Header(Request, "\r\n");
	CHttpRequestCurlTestPeer::Complete(Request);
	ASSERT_TRUE(Request.ResultAgeSeconds().has_value());
	EXPECT_GE(*Request.ResultAgeSeconds(), 120);
	EXPECT_LE(*Request.ResultAgeSeconds(), 121);
}

#endif
