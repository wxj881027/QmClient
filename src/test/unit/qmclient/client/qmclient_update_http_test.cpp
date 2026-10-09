#include <base/detect.h>
#if !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <engine/shared/http_curl.h>

#include <gtest/gtest.h>

#include <cstdlib>
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

// 仅提供真实后端回调的输入入口，不复写响应处理或完成状态逻辑。
class CHttpRequestCurlTestPeer
{
public:
	static size_t Header(CHttpRequestCurl &Request, std::string Text)
	{
		return Request.OnHeader(Text.data(), Text.size());
	}
	static size_t Body(CHttpRequestCurl &Request, std::string Text)
	{
		return CHttpRequestCurl::WriteCallback(Text.data(), 1, Text.size(), &Request);
	}
	static void Complete(CHttpRequestCurl &Request, CURL *pHandle = nullptr, CURLcode Code = CURLE_OK)
	{
		Request.m_aErr[0] = '\0';
		Request.OnCompletionInternal(pHandle, Code);
	}
};

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
#endif
