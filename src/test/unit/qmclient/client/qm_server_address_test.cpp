#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmServerAddress, NullEmptyAndWhitespaceReturnEmpty)
{
	EXPECT_TRUE(NormalizeQmServerAddress(nullptr).empty());
	for(const char *pText : {"", " ", "\t\r\n\v\f"})
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(NormalizeQmServerAddress(pText).empty());
	}
}

TEST(QmServerAddress, DnsAndIpv4KeepExistingCaseAndPortCanonicalization)
{
	EXPECT_EQ(NormalizeQmServerAddress("Example.COM:08303"), "example.com:8303");
	EXPECT_EQ(NormalizeQmServerAddress("127.0.0.1:00080"), "127.0.0.1:80");
	EXPECT_EQ(NormalizeQmServerAddress("Example.COM"), "example.com");
}

TEST(QmServerAddress, Ipv6BracketedAndBareFormsKeepExistingOutput)
{
	EXPECT_EQ(NormalizeQmServerAddress("[2001:DB8::1]:08303"), "[2001:db8::1]:8303");
	EXPECT_EQ(NormalizeQmServerAddress("2001:DB8::1"), "[2001:db8::1]");
	EXPECT_EQ(NormalizeQmServerAddress("[::1]"), "[::1]");
}

TEST(QmServerAddress, OuterWhitespaceSchemeDotsAndSlashesKeepExistingOrder)
{
	EXPECT_EQ(NormalizeQmServerAddress(" \tWS://EXAMPLE.COM.:08303///\r\n"), "example.com:8303");
	EXPECT_EQ(NormalizeQmServerAddress("HTTPS://Example.COM...///"), "example.com");
}

TEST(QmServerAddress, ExistingPortBoundariesArePreserved)
{
	EXPECT_EQ(NormalizeQmServerAddress("one:1"), "one:1");
	EXPECT_EQ(NormalizeQmServerAddress("one:65535"), "one:65535");
	for(const char *pText : {"one:0", "one:65536", "one:not-a-port", "one:-1", "one:999999999999999999999999999999999999"})
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(NormalizeQmServerAddress(pText).empty());
	}
}

TEST(QmServerAddress, ExistingMalformedHostCasesRemainEmpty)
{
	for(const char *pText : {"[::1", ":8303", ".", "///", "ws://"})
	{
		SCOPED_TRACE(pText);
		EXPECT_TRUE(NormalizeQmServerAddress(pText).empty());
	}
}

TEST(QmServerAddress, LongLeadingWhitespaceKeepsCanonicalAddress)
{
	const std::string Text = std::string(128 * 1024, ' ') + "EXAMPLE.COM:08303/";
	EXPECT_EQ(NormalizeQmServerAddress(Text.c_str()), "example.com:8303");
}

TEST(QmServerAddress, LongTrailingWhitespaceAndSlashesKeepCanonicalAddress)
{
	const std::string Text = "EXAMPLE.COM:08303" + std::string(64 * 1024, '/') + std::string(64 * 1024, '\t');
	EXPECT_EQ(NormalizeQmServerAddress(Text.c_str()), "example.com:8303");
}

TEST(QmServerAddress, LargeWhitespaceOnlyInputReturnsEmpty)
{
	const std::string Text(128 * 1024, ' ');
	EXPECT_TRUE(NormalizeQmServerAddress(Text.c_str()).empty());
}
