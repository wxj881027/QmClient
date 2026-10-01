#include <game/client/components/qmclient/music_lyrics/qm_spotify_token.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	// RFC 6238 附录 B 的 SHA1 种子。
	constexpr const char *RFC6238_SEED = "12345678901234567890";

	std::string TotpAt(const char *pSecret, int64_t ServerTimeSeconds)
	{
		return QmSpotifyToken::GenerateTotp(pSecret, ServerTimeSeconds);
	}
}

TEST(QmSpotifyToken, ParseSecretPayloadPicksMaxVersionAndTransforms)
{
	// 手工可验证的最小载荷:7 版本 [1,2]:
	//   i=0: 1 ^ 9 = 8  -> "8";i=1: 2 ^ 10 = 8 -> "8" => "88"
	QmSpotifyToken::STotpSecret Secret;
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload("{\"7\":[1,2]}", &Secret));
	EXPECT_EQ(Secret.m_Version, "7");
	EXPECT_EQ(Secret.m_Secret, "88");

	// 多版本取最大数字 key。
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload("{\"5\":[0],\"12\":[100]}", &Secret));
	EXPECT_EQ(Secret.m_Version, "12");
	EXPECT_EQ(Secret.m_Secret, "109"); // 100 ^ 9 = 109

	// 非法输入。
	EXPECT_FALSE(QmSpotifyToken::ParseSecretPayload("", &Secret));
	EXPECT_FALSE(QmSpotifyToken::ParseSecretPayload("not json", &Secret));
	EXPECT_FALSE(QmSpotifyToken::ParseSecretPayload("{\"x\":[1]}", &Secret));
	EXPECT_FALSE(QmSpotifyToken::ParseSecretPayload("{\"1\":[]}", &Secret));
}

TEST(QmSpotifyToken, ParseBundledSecretPayload)
{
	// 期望值由独立 Python 计算 + 线上端点实证(401 = TOTP 通过)确认。
	QmSpotifyToken::STotpSecret Secret;
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload(QmSpotifyToken::BundledSecretPayload(), &Secret));
	EXPECT_EQ(Secret.m_Version, "61");
	EXPECT_EQ(Secret.m_Secret, "376136387538459893883312310911992847112448894410210511297108");
}

TEST(QmSpotifyToken, ParseSecretPayloadVersion59Exact)
{
	const std::string Payload = "{\"59\":[123,105,79,70,110,59,52,125,60,49,80,70,89,75,80,86,63,53,123,37,117,49,52,93,77,62,47,86,48,104,68,72]}";
	QmSpotifyToken::STotpSecret Secret;
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload(Payload, &Secret));
	EXPECT_EQ(Secret.m_Version, "59");
	EXPECT_EQ(Secret.m_Secret, "1149968749953591094535678276937178384796571044743125108281211421789996");
}

// RFC 6238 附录 B(SHA1,8 位)的 6 位截断。
// 期望值经 pyotp(GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ)独立仲裁确认。
TEST(QmSpotifyToken, TotpRfc6238Sha1Vectors)
{
	EXPECT_EQ(TotpAt(RFC6238_SEED, 59), "287082"); // 8 位 94287082
	EXPECT_EQ(TotpAt(RFC6238_SEED, 1111111109), "081804"); // 8 位 07081804
	EXPECT_EQ(TotpAt(RFC6238_SEED, 1111111111), "050471"); // pyotp 仲裁值
}

TEST(QmSpotifyToken, NormalizeSpDc)
{
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc("abc123"), "abc123");
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc("sp_dc=abc123"), "abc123");
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc(" sp_dc=abc123; sp_landing=xyz "), "abc123");
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc("\"abc123\""), "abc123");
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc("sp_dc=abc;def"), "abc");
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc(""), "");
	EXPECT_EQ(QmSpotifyToken::NormalizeSpDc(";;;"), "");
}

TEST(QmSpotifyToken, BuildTokenUrlInitAndLegacy)
{
	// 直接构造 RFC6238 种子;serverTime=1770 -> counter=59 -> TOTP 083773(pyotp 仲裁值)。
	QmSpotifyToken::STotpSecret Secret;
	Secret.m_Secret = RFC6238_SEED;
	Secret.m_Version = "59";
	const int64_t ServerTime = 1770;
	const std::string InitUrl = QmSpotifyToken::BuildTokenUrl(Secret, ServerTime, false);
	EXPECT_EQ(InitUrl, "https://open.spotify.com/api/token?reason=init&productType=web-player&totp=083773&totpVer=59&totpServer=083773");
	const std::string LegacyUrl = QmSpotifyToken::BuildTokenUrl(Secret, ServerTime, true);
	EXPECT_EQ(LegacyUrl, "https://open.spotify.com/api/token?reason=transport&productType=web-player&totp=083773&totpVer=59&totpServer=083773&ts=1770");
}
