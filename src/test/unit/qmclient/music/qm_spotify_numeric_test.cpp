#include <game/client/components/qmclient/music_lyrics/qm_spotify_parser.h>
#include <game/client/components/qmclient/music_lyrics/qm_spotify_token.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

TEST(QmSpotifyNumeric, SecretVersionOverflowSkipsOnlyTheBadCandidate)
{
	QmSpotifyToken::STotpSecret Secret;
	const std::string Json = "{\"7\":[0],\"" + std::string(256, '9') + "\":[1],\"00012\":[2]}";
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload(Json, &Secret));
	EXPECT_EQ(Secret.m_Version, "12");
	EXPECT_EQ(Secret.m_Secret, "11");
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload("{\"9223372036854775808\":[2],\"9223372036854775807\":[1]}", &Secret));
	EXPECT_EQ(Secret.m_Version, "9223372036854775807");
	EXPECT_EQ(Secret.m_Secret, "8");
}

TEST(QmSpotifyNumeric, SecretVersionRequiresTheCompleteNonemptyDigitKey)
{
	for(const char *pJson : {"{\"\":[0]}", "{\"7x\":[0]}", "{\"7\\u0000x\":[0]}"})
	{
		SCOPED_TRACE(pJson);
		QmSpotifyToken::STotpSecret Secret;
		EXPECT_FALSE(QmSpotifyToken::ParseSecretPayload(pJson, &Secret));
	}
	QmSpotifyToken::STotpSecret Secret;
	ASSERT_TRUE(QmSpotifyToken::ParseSecretPayload("{\"7\\u0000x\":[0],\"5\":[1]}", &Secret));
	EXPECT_EQ(Secret.m_Version, "5");
	EXPECT_EQ(Secret.m_Secret, "8");
}

TEST(QmSpotifyNumeric, ServerTimeChecksTheRealInt64DoubleDomainBeforeConversion)
{
	struct SCase
	{
		const char *m_pNumber;
		bool m_Valid;
		int64_t m_Expected;
	};
	const SCase aCases[] = {
		{"9223372036854775807", true, std::numeric_limits<int64_t>::max()},
		{"-9223372036854775808", true, std::numeric_limits<int64_t>::min()},
		{"9223372036854774784.0", true, 9223372036854774784LL},
		{"-9223372036854775808.0", true, std::numeric_limits<int64_t>::min()},
		{"1.75", true, 1},
		{"-0.75", true, 0},
		{"9223372036854775808.0", false, 17},
		{"-9223372036854777856.0", false, 17},
		{"1e100", false, 17},
		{"-1e100", false, 17},
		{"1e400", false, 17},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pNumber);
		int64_t Out = 17;
		EXPECT_EQ(QmSpotify::ParseServerTime("{\"serverTime\":" + std::string(Case.m_pNumber) + "}", &Out), Case.m_Valid);
		EXPECT_EQ(Out, Case.m_Expected);
	}
}

TEST(QmSpotifyNumeric, TokenExpirationRejectsUnrepresentableNumbersWithoutPublishing)
{
	for(const char *pNumber : {"9223372036854775808.0", "1e100", "1e400"})
	{
		SCOPED_TRACE(pNumber);
		std::string Token = "old-token";
		int64_t Expiration = 17;
		const std::string Json = "{\"accessToken\":\"new-token\",\"accessTokenExpirationTimestampMs\":" + std::string(pNumber) + "}";
		EXPECT_FALSE(QmSpotify::ParseTokenResponse(Json, &Token, &Expiration));
		EXPECT_EQ(Token, "old-token");
		EXPECT_EQ(Expiration, 17);
	}
	std::string Token;
	int64_t Expiration = 0;
	ASSERT_TRUE(QmSpotify::ParseTokenResponse("{\"accessToken\":\"valid\",\"accessTokenExpirationTimestampMs\":9223372036854774784.0}", &Token, &Expiration));
	EXPECT_EQ(Token, "valid");
	EXPECT_EQ(Expiration, 9223372036854774784LL);
}

TEST(QmSpotifyNumeric, InvalidLyricsTimesUseTheOriginalFieldFallbacks)
{
	const std::string Overflow(128, '9');
	const std::string Json = "{\"lyrics\":{\"syncType\":\"LINE_SYNCED\",\"lines\":["
				 "{\"startTimeMs\":1e100,\"words\":\"invalid-double\"},"
				 "{\"startTimeMs\":\"" +
				 Overflow + "\",\"words\":\"invalid-string\"},"
					    "{\"startTimeMs\":\"18446744073709551616\",\"words\":\"must-not-wrap-to-zero\"},"
					    "{\"startTimeMs\":\"1000\",\"endTimeMs\":1e100,\"words\":\"kept\"},"
					    "{\"startTimeMs\":9223372036854774784.0,\"endTimeMs\":\"9223372036854775807\",\"words\":\"extreme\"}]}}";
	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmSpotify::ParseColorLyrics(Json, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "kept");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_EndMs, -1);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_StartMs, 9223372036854774784LL);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_EndMs, std::numeric_limits<int64_t>::max());
}

TEST(QmSpotifyNumeric, OutOfRangeLrclibDurationKeepsTheNoValueFallback)
{
	for(const char *pNumber : {"9223372036854775808.0", "-1e100", "1e400"})
	{
		SCOPED_TRACE(pNumber);
		const std::string Json = "{\"duration\":" + std::string(pNumber) + ",\"plainLyrics\":\"present\"}";
		QmMusicLyrics::SLyricsData Lyrics;
		ASSERT_TRUE(QmSpotify::ParseLrclib(Json, &Lyrics));
		EXPECT_EQ(Lyrics.m_Song.m_DurationMs, 0);
		ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
		EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "present");
	}
}

TEST(QmSpotifyNumeric, LrclibDurationConvertsBoundedSecondsToMilliseconds)
{
	struct SCase
	{
		const char *m_pSeconds;
		int64_t m_ExpectedMs;
	};
	const SCase aCases[] = {
		{"180", 180000},
		{"180.25", 180250},
		{"0.0005", 0},
		{nullptr, 0},
		{"null", 0},
		{"-1", 0},
		{"-0.25", 0},
		{"9223372036854775", 9223372036854775000LL},
		{"9223372036854776", 0},
		{"9007199254740992.0", 9007199254740992000LL},
		{"9223372036854776.0", 0},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pSeconds == nullptr ? "missing" : Case.m_pSeconds);
		const std::string Json = Case.m_pSeconds == nullptr ? "{\"plainLyrics\":\"present\"}" :
								      "{\"duration\":" + std::string(Case.m_pSeconds) + ",\"plainLyrics\":\"present\"}";
		QmMusicLyrics::SLyricsData Lyrics;
		ASSERT_TRUE(QmSpotify::ParseLrclib(Json, &Lyrics));
		EXPECT_EQ(Lyrics.m_Song.m_DurationMs, Case.m_ExpectedMs);
		ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 1u);
		EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "present");
	}
}
