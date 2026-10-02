#include <game/client/components/qmclient/music_lyrics/qm_spotify_crypto.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
	std::string Hex(const std::string &Data)
	{
		constexpr const char HEX[] = "0123456789abcdef";
		std::string Result;
		Result.reserve(Data.size() * 2);
		for(unsigned char C : Data)
		{
			Result += HEX[C >> 4];
			Result += HEX[C & 0x0F];
		}
		return Result;
	}

	std::string Bytes(const std::vector<uint8_t> &Data)
	{
		return std::string((const char *)Data.data(), Data.size());
	}
}

// RFC 2202 的 HMAC-SHA1 测试向量。
TEST(QmSpotifyCrypto, HmacSha1Rfc2202Case1)
{
	const std::vector<uint8_t> Key(20, 0x0B);
	const std::string Data = "Hi There";
	const std::string Digest = QmSpotifyCrypto::HmacSha1(Key.data(), Key.size(), (const unsigned char *)Data.data(), Data.size());
	EXPECT_EQ(Hex(Digest), "b617318655057264e28bc0b6fb378c8ef146be00");
}

TEST(QmSpotifyCrypto, HmacSha1Rfc2202Case2)
{
	const std::string Key = "Jefe";
	const std::string Data = "what do ya want for nothing?";
	const std::string Digest = QmSpotifyCrypto::HmacSha1((const unsigned char *)Key.data(), Key.size(), (const unsigned char *)Data.data(), Data.size());
	EXPECT_EQ(Hex(Digest), "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
}

TEST(QmSpotifyCrypto, HmacSha1Rfc2202Case3)
{
	const std::vector<uint8_t> Key(20, 0xAA);
	const std::vector<uint8_t> Data(50, 0xDD);
	const std::string Digest = QmSpotifyCrypto::HmacSha1(Key.data(), Key.size(), Data.data(), Data.size());
	EXPECT_EQ(Hex(Digest), "125d7342b9ac11cd91a39af48aa17b4f63f175d3");
}
