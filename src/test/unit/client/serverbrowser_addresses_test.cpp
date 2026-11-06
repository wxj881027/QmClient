#include <engine/client/serverbrowser_addresses.h>

#include <gtest/gtest.h>

#include <array>

TEST(ServerBrowserAddresses, MultipleAddressesPreserveOrderAndProtocol)
{
	NETADDR aAddresses[2];
	ASSERT_EQ(net_addr_from_str(&aAddresses[0], "127.0.0.1:8303"), 0);
	ASSERT_EQ(net_addr_from_str(&aAddresses[1], "[2001:db8::1]:8304"), 0);
	aAddresses[1].type |= NETTYPE_TW7;
	char aBuffer[160];
	ServerBrowserFormatAddresses(aBuffer, sizeof(aBuffer), aAddresses, 2);
	EXPECT_STREQ(aBuffer, "127.0.0.1:8303,tw-0.7+udp://[2001:db8::1]:8304");
}

TEST(ServerBrowserAddresses, EmptyListClearsThePreviousAddress)
{
	char aBuffer[32] = "previous";
	ServerBrowserFormatAddresses(aBuffer, sizeof(aBuffer), nullptr, 0);
	EXPECT_STREQ(aBuffer, "");
}

TEST(ServerBrowserAddresses, ExactCapacityKeepsTheWholeAddressAndTerminator)
{
	NETADDR Address;
	ASSERT_EQ(net_addr_from_str(&Address, "127.0.0.1:8303"), 0);
	std::array<char, sizeof("127.0.0.1:8303") + 1> aBuffer;
	aBuffer.fill('#');
	ServerBrowserFormatAddresses(aBuffer.data(), aBuffer.size() - 1, &Address, 1);
	EXPECT_STREQ(aBuffer.data(), "127.0.0.1:8303");
	EXPECT_EQ(aBuffer.back(), '#');
}

TEST(ServerBrowserAddresses, InsufficientSpaceOmitsWholeTrailingAddress)
{
	NETADDR aAddresses[2];
	ASSERT_EQ(net_addr_from_str(&aAddresses[0], "127.0.0.1:8303"), 0);
	ASSERT_EQ(net_addr_from_str(&aAddresses[1], "[2001:db8::1]:8304"), 0);
	aAddresses[1].type |= NETTYPE_TW7;
	char aBuffer[32];
	ServerBrowserFormatAddresses(aBuffer, sizeof(aBuffer), aAddresses, 2);
	EXPECT_STREQ(aBuffer, "127.0.0.1:8303");
}

TEST(ServerBrowserAddresses, ShortFirstAddressDoesNotPublishPartialTarget)
{
	NETADDR Address;
	ASSERT_EQ(net_addr_from_str(&Address, "127.0.0.1:8303"), 0);
	char aBuffer[4] = "old";
	ServerBrowserFormatAddresses(aBuffer, sizeof(aBuffer), &Address, 1);
	EXPECT_STREQ(aBuffer, "");
}

TEST(ServerBrowserAddresses, ZeroCapacityLeavesOutputUntouched)
{
	NETADDR Address;
	ASSERT_EQ(net_addr_from_str(&Address, "127.0.0.1:8303"), 0);
	char aBuffer[] = "previous";
	ServerBrowserFormatAddresses(aBuffer, 0, &Address, 1);
	EXPECT_STREQ(aBuffer, "previous");
}
