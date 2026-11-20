// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <base/system.h>

#include <gtest/gtest.h>

#include <string>

TEST(NetAddr, FromUrlStringInvalid)
{
	NETADDR Addr;

	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+udp://127.0", nullptr, 0), -1); // invalid ip
	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+udp://ddnet.org", nullptr, 0), -1); // invalid ip
	EXPECT_EQ(net_addr_from_url(&Addr, "127.0.0.1", nullptr, 0), 1); // not a URL
	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.9+udp://127.0.0.1", nullptr, 0), 1); // invalid tw protocol
	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+tcp://127.0.0.1", nullptr, 0), 1); // invalid internet protocol
}

TEST(NetAddr, FromUrlStringValid)
{
	NETADDR Addr;
	char aBuf1[NETADDR_MAXSTRSIZE];
	char aBuf2[NETADDR_MAXSTRSIZE];

	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.7+udp://127.0.0.1", nullptr, 0), 0);
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "127.0.0.1:0");
	EXPECT_STREQ(aBuf2, "127.0.0.1");

	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+udp://127.0.0.1", nullptr, 0), 0);
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "127.0.0.1:0");
	EXPECT_STREQ(aBuf2, "127.0.0.1");

	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+udp://127.0.0.1", nullptr, 0), 0);
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "127.0.0.1:0");
	EXPECT_STREQ(aBuf2, "127.0.0.1");

	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+udp://[0123:4567:89ab:cdef:1:2:3:4]:5678", nullptr, 0), 0);
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[123:4567:89ab:cdef:1:2:3:4]:5678");
	EXPECT_STREQ(aBuf2, "[123:4567:89ab:cdef:1:2:3:4]");

	char aHost[128];
	EXPECT_EQ(net_addr_from_url(&Addr, "tw-0.6+udp://ger10.ddnet.org:5678", aHost, sizeof(aHost)), -1);
	EXPECT_STREQ(aHost, "ger10.ddnet.org:5678");
}

TEST(NetAddr, FromStr)
{
	NETADDR Addr;
	char aBuf1[NETADDR_MAXSTRSIZE];
	char aBuf2[NETADDR_MAXSTRSIZE];

	EXPECT_FALSE(net_addr_from_str(&Addr, "127.0.0.1"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "127.0.0.1:0");
	EXPECT_STREQ(aBuf2, "127.0.0.1");

	EXPECT_FALSE(net_addr_from_str(&Addr, "1.2.3.4:5678"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "1.2.3.4:5678");
	EXPECT_STREQ(aBuf2, "1.2.3.4");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[::1]"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[::1]:0");
	EXPECT_STREQ(aBuf2, "[::1]");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[0123:4567:89ab:cdef:1:2:3:4]:5678"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[123:4567:89ab:cdef:1:2:3:4]:5678");
	EXPECT_STREQ(aBuf2, "[123:4567:89ab:cdef:1:2:3:4]");
}

TEST(NetAddr, StrV6)
{
	NETADDR Addr;
	char aBuf1[NETADDR_MAXSTRSIZE];
	char aBuf2[NETADDR_MAXSTRSIZE];

	// Test vectors from RFC 5952 section 4:
	// https://tools.ietf.org/html/rfc5952#section-4
	// 4.1 Handling Leading Zeros in a 16-Bit Field
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:0db8::0001]:1"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[2001:db8::1]:1");
	EXPECT_STREQ(aBuf2, "[2001:db8::1]");

	// 4.2.1 Shorten as Much as Possible
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8:0:0:0:0:2:1]"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[2001:db8::2:1]:0");
	EXPECT_STREQ(aBuf2, "[2001:db8::2:1]");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8::0:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:db8::1]");

	// 4.2.2 Handling One 16-Bit 0 Field
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8::1:1:1:1:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:db8:0:1:1:1:1:1]");

	// 4.2.3 Choice in Placement of "::"
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:0:0:1:0:0:0:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:0:0:1::1]");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8:0:0:1:0:0:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:db8::1:0:0:1]");
}

TEST(NetAddr, FromStrInvalid)
{
	NETADDR Addr;
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0."));
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0.1a"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "1.1"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::1"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0.1:"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::]:"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0.1:1a"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::]:c"));
}

TEST(NetAddr, OversizedDecimalOctetsCannotWrapIntoValidIpv4)
{
	NETADDR Addr;
	for(const char *pAddress : {"4294967423.0.0.1", "127.4294967296.0.1", "127.0.4294967296.1", "127.0.0.4294967297"})
	{
		SCOPED_TRACE(pAddress);
		EXPECT_NE(net_addr_from_str(&Addr, pAddress), 0);
	}
	const std::string LongAddress = std::string(1024, '9') + ".0.0.1";
	EXPECT_NE(net_addr_from_str(&Addr, LongAddress.c_str()), 0);
}

TEST(NetAddr, OversizedDecimalPortsAreRejectedForBothAddressFamilies)
{
	NETADDR Addr;
	EXPECT_NE(net_addr_from_str(&Addr, "127.0.0.1:4294967296"), 0);
	EXPECT_NE(net_addr_from_str(&Addr, "[::1]:4294967296"), 0);
	const std::string LongPort = "127.0.0.1:" + std::string(1024, '9');
	EXPECT_NE(net_addr_from_str(&Addr, LongPort.c_str()), 0);
}

TEST(NetAddr, Ipv6RequiresTheWholeAddressToBeConsumed)
{
	NETADDR Addr;
	EXPECT_NE(net_addr_from_str(&Addr, "[::1]suffix"), 0);
	EXPECT_NE(net_addr_from_str(&Addr, "[::1]:8303suffix"), 0);
	EXPECT_NE(net_addr_from_str(&Addr, "[::1]:8303:80"), 0);
}

TEST(NetAddr, ValidMaximumPortAndLeadingZeroesKeepTheirMeaning)
{
	NETADDR Addr;
	ASSERT_EQ(net_addr_from_str(&Addr, "255.255.255.255:65535"), 0);
	EXPECT_EQ(Addr.port, 65535);
	ASSERT_EQ(net_addr_from_str(&Addr, "[::1]:65535"), 0);
	EXPECT_EQ(Addr.port, 65535);
	const std::string Address = std::string(64, '0') + "127.0.0.1:0008303";
	ASSERT_EQ(net_addr_from_str(&Addr, Address.c_str()), 0);
	EXPECT_EQ(Addr.ip[0], 127);
	EXPECT_EQ(Addr.port, 8303);
}

TEST(NetAddr, ValidParseAfterRejectedOverflowProducesFreshAddress)
{
	NETADDR Addr;
	EXPECT_NE(net_addr_from_str(&Addr, "127.0.0.1:99999999999999999999999"), 0);
	ASSERT_EQ(net_addr_from_str(&Addr, "192.0.2.1:8303"), 0);
	EXPECT_EQ(Addr.type, NETTYPE_IPV4);
	EXPECT_EQ(Addr.ip[0], 192);
	EXPECT_EQ(Addr.ip[1], 0);
	EXPECT_EQ(Addr.ip[2], 2);
	EXPECT_EQ(Addr.ip[3], 1);
	EXPECT_EQ(Addr.port, 8303);
}

TEST(NetAddrDeathTest, StrInvalid1)
{
	ASSERT_DEATH({
		NETADDR Addr = {0};
		char aBuf[NETADDR_MAXSTRSIZE];
		net_addr_str(&Addr, aBuf, sizeof(aBuf), true); }, "");
}

TEST(NetAddrDeathTest, StrInvalid2)
{
	ASSERT_DEATH({
		NETADDR Addr = {0};
		char aBuf[NETADDR_MAXSTRSIZE];
		net_addr_str(&Addr, aBuf, sizeof(aBuf), false); }, "");
}

TEST(NetAddr, IsLocal)
{
	NETADDR Addr;
	net_addr_from_str(&Addr, "127.0.0.1");
	EXPECT_TRUE(net_addr_is_local(&Addr));

	net_addr_from_str(&Addr, "[::1]");
	EXPECT_TRUE(net_addr_is_local(&Addr));

	net_addr_from_str(&Addr, "192.168.1.1");
	EXPECT_TRUE(net_addr_is_local(&Addr));

	net_addr_from_str(&Addr, "10.0.0.1");
	EXPECT_TRUE(net_addr_is_local(&Addr));

	net_addr_from_str(&Addr, "8.8.8.8");
	EXPECT_FALSE(net_addr_is_local(&Addr));

	net_addr_from_str(&Addr, "[2001:db8::1]");
	EXPECT_FALSE(net_addr_is_local(&Addr));

	net_addr_from_str(&Addr, "[fc00::1]");
	EXPECT_TRUE(net_addr_is_local(&Addr));
	net_addr_from_str(&Addr, "[fd12:3456::1]");
	EXPECT_TRUE(net_addr_is_local(&Addr));
	net_addr_from_str(&Addr, "[fe80::1]");
	EXPECT_TRUE(net_addr_is_local(&Addr));
	net_addr_from_str(&Addr, "[febf::1]");
	EXPECT_TRUE(net_addr_is_local(&Addr));
	net_addr_from_str(&Addr, "[fec0::1]");
	EXPECT_FALSE(net_addr_is_local(&Addr));
}
