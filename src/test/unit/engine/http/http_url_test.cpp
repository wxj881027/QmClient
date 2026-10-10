#include <engine/shared/http_url.h>

#include <gtest/gtest.h>

TEST(HttpUrlHost, PathQueryAndFragmentCannotChangeHost)
{
	for(const char *pUrl : {"https://Example.TEST/path/other.test", "https://Example.TEST?next=https://other.test", "https://Example.TEST#user@other.test"})
	{
		SCOPED_TRACE(pUrl);
		EXPECT_EQ(HttpUrlHost(pUrl), "example.test");
	}
	EXPECT_EQ(HttpUrlHost("Example.TEST?next=https://other.test"), "example.test");
}

TEST(HttpUrlHost, UserinfoAndPortAreExcludedFromHost)
{
	EXPECT_EQ(HttpUrlHost("https://user:p%40ss:word@Example.TEST:443/path"), "example.test");
	EXPECT_EQ(HttpUrlHost("http://user@Example.TEST:00080?key=value"), "example.test");
	EXPECT_EQ(HttpUrlHost("https://Example.TEST:/path"), "example.test");
	EXPECT_EQ(HttpUrlHost("Example.TEST:443/path"), "example.test");
}

TEST(HttpUrlHost, Ipv6LiteralAndPortDoNotSplitAtAddressColon)
{
	EXPECT_EQ(HttpUrlHost("https://[2001:DB8::1]:443/path"), "2001:db8::1");
	EXPECT_EQ(HttpUrlHost("http://user:pass@[::ffff:192.0.2.1]:80?next=other.test"), "::ffff:192.0.2.1");
}

TEST(HttpUrlHost, Ipv6ZoneIsNotPartOfNoProxyAddress)
{
	EXPECT_EQ(HttpUrlHost("http://[fe80::1%25eth0]:8080/"), "fe80::1");
	EXPECT_EQ(HttpUrlHost("http://[fe80::1%eth0]/"), "fe80::1");
}

TEST(HttpUrlHost, EncodedHostIsDecodedBeforeNormalization)
{
	EXPECT_EQ(HttpUrlHost("https://%45xample%2eTEST:443/"), "example.test");
	EXPECT_EQ(HttpUrlHost("https://%31%39%32.0.2.1/"), "192.0.2.1");
}

TEST(HttpUrlHost, NumericIpv4FormsKeepCurlCanonicalHost)
{
	for(const char *pUrl : {"http://127.1/", "http://127.0.1/", "http://2130706433/", "http://0x7f.1/", "http://0177.0.0.01/"})
	{
		SCOPED_TRACE(pUrl);
		EXPECT_EQ(HttpUrlHost(pUrl), "127.0.0.1");
	}
	EXPECT_EQ(HttpUrlHost("http://4294967295/"), "255.255.255.255");
	for(const char *pHost : {"123.test", "1.2.3.256", "1.2.65536", "1.16777216", "4294967296", "1.2.3.4.5", "1.2.3.4.", "09.0.0.1"})
	{
		SCOPED_TRACE(pHost);
		EXPECT_EQ(HttpUrlHost(pHost), pHost);
	}
}

TEST(HttpUrlHost, MissingOrMalformedAuthorityCannotBecomeBypassEvidence)
{
	EXPECT_TRUE(HttpUrlHost(nullptr).empty());
	for(const char *pUrl : {"", "https://", "https:///path", "https://user@/", "https://user@other@safe.test/", "https://:443/", "https://[::1/", "https://[]/", "https://[not-ipv6]/", "https://[::1]other.test/", "https://[::1]:443junk/", "https://[fe80::1%]/", "https://2001:db8::1/", "https://host.test:-1/", "https://host.test:65536/", "https://host.test:99999999999999/", "https://host.test:443:80/"})
	{
		SCOPED_TRACE(pUrl);
		EXPECT_TRUE(HttpUrlHost(pUrl).empty());
	}
}

TEST(HttpUrlHost, EncodedDelimitersAndControlCharactersCannotCreateAnotherHost)
{
	for(const char *pUrl : {"https://safe.test%00.other.test/", "https://safe.test%2fother.test/", "https://safe.test%40other.test/", "https://safe.test%3a443/", "https://safe.test%5cother.test/", "https://safe.test%0a/", "https://safe.test%7f/", "https://safe.test%25/", "https://safe.test%21/", "https://*.test/", "https://safe.test%zz/", "https://safe.test%/", "https://safe.test%2/", "https://bad host/", "https://bad\tuser@safe.test/"})
	{
		SCOPED_TRACE(pUrl);
		EXPECT_TRUE(HttpUrlHost(pUrl).empty());
	}
	const char aUrl[] = "https://safe.test\0.other.test/";
	EXPECT_TRUE(HttpUrlHost(std::string_view(aUrl, sizeof(aUrl) - 1)).empty());
}
