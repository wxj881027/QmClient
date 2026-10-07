#include <game/client/components/qmclient/update_proxy.h>

#include <gtest/gtest.h>

TEST(QmUpdateProxy, SelectsHttpsProxyFromWindowsProtocolMap)
{
	EXPECT_EQ(qm_update::SelectSystemProxy("http=127.0.0.1:8080; https=127.0.0.1:7890; socks=127.0.0.1:1080"), "http://127.0.0.1:7890");
	EXPECT_EQ(qm_update::SelectSystemProxy("127.0.0.1:7890"), "http://127.0.0.1:7890");
	EXPECT_EQ(qm_update::SelectSystemProxy("socks=127.0.0.1:1080"), "socks5h://127.0.0.1:1080");
	EXPECT_EQ(qm_update::SelectSystemProxy("https=[::1]:7890"), "http://[::1]:7890");
	EXPECT_EQ(qm_update::SelectSystemProxy("socks5h://127.0.0.1:1080"), "socks5h://127.0.0.1:1080");
	EXPECT_EQ(qm_update::SelectSystemProxy("socks5://127.0.0.1:1080"), "socks5://127.0.0.1:1080");
}

TEST(QmUpdateProxy, RejectsCredentialsMalformedAndUnsupportedSystemProxyValues)
{
	for(const char *pValue : {"", "https=user:password@127.0.0.1:7890", "https=proxy\nBad", "ftp://proxy:21", "http=proxy:8080"})
		EXPECT_TRUE(qm_update::SelectSystemProxy(pValue).empty()) << pValue;
}

TEST(QmUpdateProxy, BypassMatchingRespectsCaseWildcardAndLocalScope)
{
	EXPECT_TRUE(qm_update::ProxyBypassed("api.github.com", "*.github.com;localhost;<local>"));
	EXPECT_TRUE(qm_update::ProxyBypassed("GITHUB.COM", "github.com"));
	EXPECT_TRUE(qm_update::ProxyBypassed("localhost", "<local>"));
	EXPECT_FALSE(qm_update::ProxyBypassed("api.github.com", "github.com"));
	EXPECT_FALSE(qm_update::ProxyBypassed("github.com", "<local>"));
	EXPECT_FALSE(qm_update::ProxyBypassed("github.com.evil", "*.github.com;github.com"));
}
