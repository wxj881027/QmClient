#include <game/client/components/qmclient/music_lyrics/qm_spotify_parser.h>
#include <game/client/components/qmclient/music_lyrics/qm_spotify_token.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace
{
	// 只观察真实接口的异常传播与重试；C解析树由malloc分配，释放计数不由new故障代表。
	template<typename TOutput, typename FParse, typename FCheck>
	void CheckNativeFailures(FParse Parse, FCheck Check)
	{
		TOutput Warmup;
		ASSERT_TRUE(Parse(Warmup));
		TOutput Baseline;
		bool Parsed;
		std::size_t Allocations;
		{
			CPredictionAllocationFailure Trace(std::numeric_limits<std::size_t>::max());
			Parsed = Parse(Baseline);
			Allocations = Trace.Attempts();
		}
		ASSERT_TRUE(Parsed);
		Check(Baseline);
		ASSERT_GT(Allocations, 0u);
		ASSERT_LE(Allocations, 256u);
		std::size_t Exceptions = 0;
		for(std::size_t Allocation = 0; Allocation < Allocations; ++Allocation)
		{
			SCOPED_TRACE(Allocation);
			TOutput Out;
			bool Threw = false, Triggered;
			Parsed = false;
			{
				CPredictionAllocationFailure Failure(Allocation);
				try
				{
					Parsed = Parse(Out);
				}
				catch(const std::bad_alloc &)
				{
					Threw = true;
				}
				Triggered = Failure.Triggered();
			}
			ASSERT_TRUE(Triggered);
			if(Threw)
				++Exceptions;
			else
			{
				// stable_sort的nothrow缓冲分配拒绝后可以回退并成功，不能强迫其抛异常。
				ASSERT_TRUE(Parsed);
				Check(Out);
			}
			TOutput Retry;
			ASSERT_TRUE(Parse(Retry));
			Check(Retry);
		}
		EXPECT_GT(Exceptions, 0u);
	}

	struct STokenOutput
	{
		std::string m_Token;
		int64_t m_Expiration = 0;
	};
}

TEST(QmSpotifyParserAllocation, SecretAssemblyAndVersionFailuresStillPropagateAndRetry)
{
	const std::string Json = "{\"1234567890123456789\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}";
	CheckNativeFailures<QmSpotifyToken::STotpSecret>(
		[&](auto &Out) { return QmSpotifyToken::ParseSecretPayload(Json, &Out); },
		[](const auto &Out) {
			EXPECT_EQ(Out.m_Version, "1234567890123456789");
			EXPECT_EQ(Out.m_Secret, "910111213141516171819202122232425262728");
		});
}

TEST(QmSpotifyParserAllocation, TokenStringPreparationFailuresStillPropagateAndRetry)
{
	const std::string Token(96, 't');
	const std::string Json = "{\"accessToken\":\"" + Token + "\",\"accessTokenExpirationTimestampMs\":1712345678000,\"isAnonymous\":false}";
	CheckNativeFailures<STokenOutput>(
		[&](auto &Out) { return QmSpotify::ParseTokenResponse(Json, &Out.m_Token, &Out.m_Expiration); },
		[&](const auto &Out) {
			EXPECT_EQ(Out.m_Token, Token);
			EXPECT_EQ(Out.m_Expiration, 1712345678000LL);
		});
}

TEST(QmSpotifyParserAllocation, ColorLyricsPreparationFailuresStillPropagateAndRetry)
{
	const std::string Words(96, 'w'), Translation(64, 't');
	const std::string Json = "{\"lyrics\":{\"syncType\":\"LINE_SYNCED\",\"lines\":["
				 "{\"startTimeMs\":\"3000\",\"words\":\"later-" +
				 Words + "\"},"
					 "{\"startTimeMs\":\"1000\",\"words\":\"earlier-" +
				 Words + "\"}],"
					 "\"alternatives\":[{\"lines\":[\"" +
				 Translation + "\",\"second\"]}]}}";
	CheckNativeFailures<QmMusicLyrics::SLyricsData>(
		[&](auto &Out) { return QmSpotify::ParseColorLyrics(Json, &Out); },
		[&](const auto &Out) {
			ASSERT_EQ(Out.m_Timeline.m_vLines.size(), 2u);
			EXPECT_EQ(Out.m_Timeline.m_vLines[0].m_StartMs, 1000);
			EXPECT_EQ(Out.m_Timeline.m_vLines[0].m_Text, "earlier-" + Words);
			ASSERT_EQ(Out.m_vTranslations.size(), 2u);
			EXPECT_EQ(Out.m_vTranslations[0], "second");
			EXPECT_EQ(Out.m_vTranslations[1], Translation);
		});
}

TEST(QmSpotifyParserAllocation, SearchCandidatePreparationFailuresStillPropagateAndRetry)
{
	const std::string Id(64, 'i'), Title(96, 'n'), Artist(64, 'a'), Album(64, 'b');
	const std::string Json = "{\"tracks\":{\"items\":[{\"id\":\"" + Id + "\",\"name\":\"" + Title +
				 "\",\"artists\":[{\"name\":\"" + Artist + "\"}],\"album\":{\"name\":\"" + Album + "\"}}]}}";
	CheckNativeFailures<std::vector<QmSpotify::STrackCandidate>>(
		[&](auto &Out) { return QmSpotify::ParseSearchResponse(Json, &Out); },
		[&](const auto &Out) {
			ASSERT_EQ(Out.size(), 1u);
			EXPECT_EQ(Out[0].m_Id, Id);
			EXPECT_EQ(Out[0].m_Title, Title);
			EXPECT_EQ(Out[0].m_Artist, Artist);
			EXPECT_EQ(Out[0].m_Album, Album);
		});
}

TEST(QmSpotifyParserAllocation, ServerTimeInputPreparationFailureStillPropagatesAndRetries)
{
	const std::string Json = "{\"serverTime\":1712345678,\"padding\":\"" + std::string(96, 'p') + "\"}";
	CheckNativeFailures<int64_t>(
		[&](auto &Out) { return QmSpotify::ParseServerTime(Json, &Out); },
		[](int64_t Out) { EXPECT_EQ(Out, 1712345678); });
}

TEST(QmSpotifyParserAllocation, LrclibSyncedAndPlainPreparationFailuresStillPropagateAndRetry)
{
	const std::string Title(64, 'n'), Words(96, 'w');
	for(const bool Synced : {false, true})
	{
		SCOPED_TRACE(Synced);
		const std::string Json = "{\"trackName\":\"" + Title + "\",\"artistName\":\"artist\",\"duration\":180," +
					 (Synced ? "\"syncedLyrics\":\"[00:01.00]" + Words + "\"}" : "\"plainLyrics\":\"" + Words + "\"}");
		CheckNativeFailures<QmMusicLyrics::SLyricsData>(
			[&](auto &Out) { return QmSpotify::ParseLrclib(Json, &Out); },
			[&](const auto &Out) {
				EXPECT_EQ(Out.m_Song.m_Title, Title);
				EXPECT_EQ(Out.m_Song.m_DurationMs, 180000);
				ASSERT_EQ(Out.m_Timeline.m_vLines.size(), 1u);
				EXPECT_EQ(Out.m_Timeline.m_vLines[0].m_Text, Words);
				EXPECT_EQ(Out.m_Timeline.m_HasTiming, Synced);
			});
	}
}
