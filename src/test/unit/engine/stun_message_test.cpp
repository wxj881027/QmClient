#include <base/system.h>

#include <engine/shared/stun.h>

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <vector>

namespace
{
	class CStunMessageTest : public testing::Test
	{
	protected:
		CStunData m_Data{};
		std::vector<unsigned char> m_vMessage;
		bool m_Success = false;
		NETADDR m_Addr{};

		void SetUp() override
		{
			for(size_t i = 0; i < sizeof(m_Data.m_aSecret); ++i)
				m_Data.m_aSecret[i] = i + 1;
			Reset();
		}

		void Reset(bool Error = false)
		{
			m_vMessage = {0x01, (unsigned char)(Error ? 0x11 : 0x01), 0, 0, 0x21, 0x12, 0xa4, 0x42};
			m_vMessage.insert(m_vMessage.end(), std::begin(m_Data.m_aSecret), std::end(m_Data.m_aSecret));
		}

		void Append(const std::vector<unsigned char> &Bytes)
		{
			m_vMessage.insert(m_vMessage.end(), Bytes.begin(), Bytes.end());
			const size_t BodySize = m_vMessage.size() - 20;
			m_vMessage[2] = BodySize >> 8;
			m_vMessage[3] = BodySize & 0xff;
		}

		void AddV4()
		{
			Append({0x00, 0x20, 0, 8, 0, 1, 0x01, 0x7d, 0xe1, 0x12, 0xa6, 0x43});
		}

		bool Parse()
		{
			return StunMessageParse(m_vMessage.data(), m_vMessage.size(), &m_Data, &m_Success, &m_Addr);
		}

		void ExpectV4()
		{
			ASSERT_FALSE(Parse());
			EXPECT_TRUE(m_Success);
			NETADDR Expected{};
			ASSERT_EQ(net_addr_from_str(&Expected, "192.0.2.1:8303"), 0);
			EXPECT_EQ(net_addr_comp(&m_Addr, &Expected), 0);
		}

		void ExpectEmptyOutputs()
		{
			EXPECT_FALSE(m_Success);
			NETADDR Empty;
			mem_zero(&Empty, sizeof(Empty));
			EXPECT_EQ(mem_comp(&m_Addr, &Empty, sizeof(Empty)), 0);
		}

		void ExpectIgnored()
		{
			m_Success = true;
			m_Addr.type = NETTYPE_IPV6;
			m_Addr.port = 42;
			ASSERT_TRUE(Parse());
			ExpectEmptyOutputs();
		}
	};
}

TEST_F(CStunMessageTest, Ipv4MappedAddressDecodesCookieAndPort)
{
	AddV4();
	ExpectV4();
}

TEST_F(CStunMessageTest, KnownTransactionRequestPreservesHeaderSecretAndOuterGuard)
{
	std::array<unsigned char, 24> Buffer;
	Buffer.fill(0xa5);
	const CStunData Original = m_Data;
	ASSERT_EQ(StunMessagePrepare(Buffer.data(), 20, m_Data), 20U);
	const unsigned char aHeader[] = {0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42};
	EXPECT_EQ(mem_comp(Buffer.data(), aHeader, sizeof(aHeader)), 0);
	EXPECT_EQ(mem_comp(Buffer.data() + 8, Original.m_aSecret, sizeof(Original.m_aSecret)), 0);
	EXPECT_EQ(mem_comp(m_Data.m_aSecret, Original.m_aSecret, sizeof(Original.m_aSecret)), 0);
	for(size_t i = 20; i < Buffer.size(); ++i)
		EXPECT_EQ(Buffer[i], 0xa5);
}

TEST_F(CStunMessageTest, Ipv6MappedAddressDecodesCookieAndTransactionId)
{
	Append({0, 0x20, 0, 20, 0, 2, 0x01, 0x7d, 0x01, 0x13, 0xa9, 0xfa, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13});
	ASSERT_FALSE(Parse());
	ASSERT_TRUE(m_Success);
	NETADDR Expected{};
	ASSERT_EQ(net_addr_from_str(&Expected, "[2001:db8::1]:8303"), 0);
	EXPECT_EQ(net_addr_comp(&m_Addr, &Expected), 0);
}

TEST_F(CStunMessageTest, OptionalAttributeSkipsOneTwoOrThreePaddingBytes)
{
	const std::array<std::vector<unsigned char>, 3> Attributes = {{
		{0x80, 0x22, 0, 1, 'a', 0xfe, 0xfd, 0xfc},
		{0x80, 0x22, 0, 2, 'a', 'b', 0xfe, 0xfd},
		{0x80, 0x22, 0, 3, 'a', 'b', 'c', 0xfe},
	}};
	for(const auto &Attribute : Attributes)
	{
		SCOPED_TRACE(Attribute[3]);
		Reset();
		Append(Attribute);
		AddV4();
		ExpectV4();
	}
}

TEST_F(CStunMessageTest, OptionalPaddedAttributeAfterAddressIsAccepted)
{
	AddV4();
	Append({0x80, 0x22, 0, 3, 'a', 'b', 'c', 0xfe});
	ExpectV4();
}

TEST_F(CStunMessageTest, DamagedAttributeAfterAddressDoesNotPublishPartialAddress)
{
	AddV4();
	Append({0x80, 0x22, 0, 8});
	ExpectIgnored();
}

TEST_F(CStunMessageTest, SuccessWithoutMappedAddressIsIgnored)
{
	ExpectIgnored();
}

TEST_F(CStunMessageTest, UnsupportedAddressFamilyCanPrecedeSupportedAddress)
{
	Append({0, 0x20, 0, 4, 0, 3, 0x01, 0x7d});
	AddV4();
	ExpectV4();
}

TEST_F(CStunMessageTest, WrongMappedAddressLengthIsIgnored)
{
	const std::array<std::vector<unsigned char>, 2> Attributes = {{
		{0, 0x20, 0, 4, 0, 1, 0x01, 0x7d},
		{0, 0x20, 0, 8, 0, 2, 0x01, 0x7d, 1, 2, 3, 4},
	}};
	for(const auto &Attribute : Attributes)
	{
		Reset();
		Append(Attribute);
		ExpectIgnored();
	}
}

TEST_F(CStunMessageTest, FirstMappedAddressRetainsExistingDuplicatePolicy)
{
	AddV4();
	Append({0, 0x20, 0, 8, 0, 1, 0x01, 0x7d, 0xe1, 0x12, 0xa6, 0x40});
	ExpectV4();
}

TEST_F(CStunMessageTest, UnknownRequiredAttributeRejectsWholeResponse)
{
	AddV4();
	Append({0, 0x0f, 0, 0});
	ExpectIgnored();
}

TEST_F(CStunMessageTest, StandardErrorCodeProducesHandledErrorResponse)
{
	Reset(true);
	Append({0, 9, 0, 8, 0, 0, 5, 0, 'O', 'o', 'p', 's'});
	ASSERT_FALSE(Parse());
	ExpectEmptyOutputs();
}

TEST_F(CStunMessageTest, ErrorCodeIgnoresReservedBitsAndReasonPadding)
{
	Reset(true);
	Append({0, 9, 0, 7, 0xff, 0xff, 0xe4, 1, 'b', 'a', 'd', 0xfe});
	ASSERT_FALSE(Parse());
	ExpectEmptyOutputs();
}

TEST_F(CStunMessageTest, ErrorWithoutErrorCodeIsIgnored)
{
	Reset(true);
	ExpectIgnored();
}

TEST_F(CStunMessageTest, ShortErrorCodeIsIgnored)
{
	Reset(true);
	Append({0, 9, 0, 3, 0, 0, 5, 0xfe});
	ExpectIgnored();
}

TEST_F(CStunMessageTest, InvalidErrorCodeClassOrNumberIsIgnored)
{
	const std::array<std::array<unsigned char, 2>, 4> Codes = {{{2, 0}, {7, 0}, {5, 100}, {5, 255}}};
	for(const auto &Code : Codes)
	{
		Reset(true);
		Append({0, 9, 0, 4, 0, 0, Code[0], Code[1]});
		ExpectIgnored();
	}
}

TEST_F(CStunMessageTest, AllTruncatedResponsePrefixesAreIgnored)
{
	AddV4();
	for(size_t Size = 0; Size < m_vMessage.size(); ++Size)
	{
		SCOPED_TRACE(Size);
		m_Success = true;
		m_Addr.type = NETTYPE_IPV6;
		ASSERT_TRUE(StunMessageParse(m_vMessage.data(), Size, &m_Data, &m_Success, &m_Addr));
		ExpectEmptyOutputs();
	}
}

TEST_F(CStunMessageTest, WrongHeaderOrTransactionIdIsIgnored)
{
	for(size_t Offset : {size_t(0), size_t(1), size_t(4), size_t(8)})
	{
		SCOPED_TRACE(Offset);
		Reset();
		AddV4();
		m_vMessage[Offset] ^= 2;
		ExpectIgnored();
	}
}

TEST_F(CStunMessageTest, InvalidDeclaredMessageLengthIsIgnored)
{
	AddV4();
	m_vMessage[3] = 13;
	ExpectIgnored();
	m_vMessage[3] = 16;
	ExpectIgnored();
}

TEST_F(CStunMessageTest, BytesAfterDeclaredMessageRetainExistingPolicy)
{
	AddV4();
	m_vMessage.insert(m_vMessage.end(), {0, 0x0f, 0, 0});
	ExpectV4();
}

TEST_F(CStunMessageTest, DamagedResponseDoesNotPreventNextValidResponse)
{
	AddV4();
	m_vMessage[3] = 16;
	ExpectIgnored();
	Reset();
	AddV4();
	ExpectV4();
}
