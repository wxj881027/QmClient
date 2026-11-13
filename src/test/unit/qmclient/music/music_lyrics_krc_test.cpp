#include <base/str.h>

#include <engine/external/zlib/zlib.h>

#include <game/client/components/qmclient/music_lyrics/music_lyrics_krc.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace QmMusicLyrics;

namespace
{
	// 构造加密 KRC 文件。
	// 标准格式: "krc1" + 4 字节长度 + 1 字节 flag + XOR64(压缩数据)。
	// 旧版格式: "krc1" + XOR16(压缩数据),无长度/flag 字段。
	std::string BuildEncryptedKrc(const std::string &PlainText, bool UseLegacyKey = false, bool UseNewFlag = false)
	{
		uLongf CompressedSize = compressBound((uLong)PlainText.size());
		std::vector<uint8_t> Compressed(CompressedSize);
		if(compress2(Compressed.data(), &CompressedSize, (const Bytef *)PlainText.data(), (uLong)PlainText.size(), Z_BEST_COMPRESSION) != Z_OK)
			return {};
		Compressed.resize(CompressedSize);

		std::string Result;
		Result.append("krc1");
		if(!UseLegacyKey)
		{
			Result.append(4, '\0');
			Result.push_back(UseNewFlag ? '\x01' : '\x00');
		}
		for(size_t Index = 0; Index < Compressed.size(); ++Index)
		{
			const uint8_t Key = UseLegacyKey ? KRC_KEY16[Index % sizeof(KRC_KEY16)] : KRC_KEY64[Index % sizeof(KRC_KEY64)];
			Result.push_back((char)(Compressed[Index] ^ Key));
		}
		return Result;
	}

	const char *SAMPLE_KRC =
		"[ti:测试歌曲]\n"
		"[ar:测试歌手]\n"
		"[00:00.00]<0,1200>第一句歌词\n"
		"[00:01.20]<0,100>逐<100,80>字<180,150>歌<330,120>词<450,200>!\n"
		"[00:03.00]第二句\n";

	std::string TranslationTag(std::string_view Json)
	{
		std::string Encoded((Json.size() + 2) / 3 * 4 + 1, '\0');
		str_base64(Encoded.data(), (int)Encoded.size(), Json.data(), (int)Json.size());
		Encoded.pop_back();
		return "[language:" + Encoded + "]\n";
	}
}

TEST(MusicLyricsKrc, DecryptsStandardKrcFile)
{
	const std::string Encrypted = BuildEncryptedKrc(SAMPLE_KRC);
	ASSERT_FALSE(Encrypted.empty());
	std::string Text;
	ASSERT_TRUE(DecryptKrc(Encrypted, &Text));
	EXPECT_NE(Text.find("[ti:测试歌曲]"), std::string::npos);
	EXPECT_NE(Text.find("第一句歌词"), std::string::npos);
}

TEST(MusicLyricsKrc, DecryptsLegacy16ByteKeyVariant)
{
	const std::string Encrypted = BuildEncryptedKrc(SAMPLE_KRC, true);
	ASSERT_FALSE(Encrypted.empty());
	std::string Text;
	ASSERT_TRUE(DecryptKrc(Encrypted, &Text));
	EXPECT_NE(Text.find("第一句歌词"), std::string::npos);
}

TEST(MusicLyricsKrc, RejectsBadMagic)
{
	std::string Error;
	std::string Text;
	EXPECT_FALSE(DecryptKrc("notkrc", &Text, &Error));
	EXPECT_EQ(Error, "bad krc magic");
}

TEST(MusicLyricsKrc, RejectsGarbagePayload)
{
	std::string Data = "krc1";
	Data.append(5, '\xAB');
	std::string Text;
	EXPECT_FALSE(DecryptKrc(Data, &Text));
}

TEST(MusicLyricsKrc, ParsesWordTimingWithRelativeOffsets)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText(SAMPLE_KRC, &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 3u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 0);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 1200);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "第一句歌词");

	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 1200);
	ASSERT_EQ(Timeline.m_vLines[1].m_vWords.size(), 5u);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[0].m_StartMs, 1200);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[0].m_EndMs, 1300);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[0].m_Text, "逐");
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[1].m_StartMs, 1300);
	EXPECT_EQ(Timeline.m_vLines[1].m_vWords[4].m_StartMs, 1650);
	EXPECT_EQ(Timeline.m_vLines[1].m_Text, "逐字歌词!");

	EXPECT_EQ(Timeline.m_vLines[2].m_StartMs, 3000);
	EXPECT_EQ(Timeline.m_vLines[2].m_EndMs, -1);
}

TEST(MusicLyricsKrc, ParsesLegacyMillisecondPairHeader)
{
	const char *Legacy =
		"[0,1200]<0,400,0>旧<400,400,0>版<800,400,0>词\n"
		"[2000,500]第二行\n";
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText(Legacy, &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 0);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 1200);
	ASSERT_EQ(Timeline.m_vLines[0].m_vWords.size(), 3u);
	EXPECT_EQ(Timeline.m_vLines[0].m_vWords[1].m_StartMs, 400);
	EXPECT_EQ(Timeline.m_vLines[1].m_StartMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[1].m_EndMs, 2500);
}

TEST(MusicLyricsKrc, RejectsUntimedOrMalformedText)
{
	NeteaseLyrics::STimeline Timeline;
	std::string Error;
	EXPECT_FALSE(ParseKrcText("plain text\n", &Timeline, &Error));
	EXPECT_FALSE(ParseKrcText("[ti:只有元数据]\n", &Timeline, &Error));
}

TEST(MusicLyricsKrc, RoundTripsThroughDataEntry)
{
	const std::string Encrypted = BuildEncryptedKrc(SAMPLE_KRC);
	ASSERT_FALSE(Encrypted.empty());
	SLyricsData Data;
	ASSERT_TRUE(ParseKrcData(Encrypted, &Data));
	EXPECT_TRUE(Data.HasLyrics());
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 3u);
	EXPECT_TRUE(Data.m_vTranslations.empty());
}

TEST(MusicLyricsKrc, Utf8LineEndingsAndBomAreAccepted)
{
	const std::string WithBom = std::string("\xEF\xBB\xBF", 3) + "[00:00.00]<0,100>你好\n";
	const std::string Encrypted = BuildEncryptedKrc(WithBom);
	ASSERT_FALSE(Encrypted.empty());
	std::string Text;
	ASSERT_TRUE(DecryptKrc(Encrypted, &Text));
	EXPECT_EQ(Text.find('\xEF'), std::string::npos);
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText(Text, &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "你好");
}

namespace
{
	// 内嵌翻译轨 base64(紧凑 JSON {"content":[{"type":1,"lyricContent":[["第一句译"],["第二行译"]]}]})。
	const char *TRANSLATION_B64 =
		"eyJjb250ZW50IjpbeyJ0eXBlIjoxLCJseXJpY0NvbnRlbnQiOltbIuesrOS4gOWPpeivkSJdLFsi56ys5LqM6KGM6K+RIl1dfV19";
}

TEST(MusicLyricsKrc, ExtractsEmbeddedTranslationTrack)
{
	const char *KrcWithTranslation =
		"[ti:测试]\n"
		"[language:eyJjb250ZW50IjpbeyJ0eXBlIjoxLCJseXJpY0NvbnRlbnQiOltbIuesrOS4gOWPpeivkSJdLFsi56ys5LqM6KGM6K+RIl1dfV19]\n"
		"[0,4390]第一句歌词\n"
		"[4390,2000]第二行歌词\n";
	const std::vector<std::string> Translation = ExtractKrcTranslation(KrcWithTranslation);
	ASSERT_EQ(Translation.size(), 2u);
	EXPECT_EQ(Translation[0], "第一句译");
	EXPECT_EQ(Translation[1], "第二行译");
}

TEST(MusicLyricsKrc, TranslationTrackAlignsWithTimelineInDataEntry)
{
	const std::string KrcWithTranslation =
		"[ti:测试]\n"
		"[language:eyJjb250ZW50IjpbeyJ0eXBlIjoxLCJseXJpY0NvbnRlbnQiOltbIuesrOS4gOWPpeivkSJdLFsi56ys5LqM6KGM6K+RIl1dfV19]\n"
		"[0,4390]第一句歌词\n"
		"[4390,2000]第二行歌词\n";
	const std::string Encrypted = BuildEncryptedKrc(KrcWithTranslation);
	ASSERT_FALSE(Encrypted.empty());
	SLyricsData Data;
	ASSERT_TRUE(ParseKrcData(Encrypted, &Data));
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
	ASSERT_EQ(Data.m_vTranslations.size(), 2u);
	EXPECT_EQ(Data.m_vTranslations[0], "第一句译");
	EXPECT_TRUE(Data.HasTranslation());
}

TEST(MusicLyricsKrc, MissingTranslationTrackYieldsEmpty)
{
	const std::vector<std::string> Translation = ExtractKrcTranslation("[ti:测试]\n[0,4390]第一句\n");
	EXPECT_TRUE(Translation.empty());
}

TEST(MusicLyricsKrc, RejectsMalformedTranslationBase64)
{
	const std::vector<std::string> Translation = ExtractKrcTranslation("[language:!!!not-base64!!!]\n");
	EXPECT_TRUE(Translation.empty());
}

TEST(MusicLyricsKrc, AcceptsInflatedTextAtExactBudget)
{
	const std::string Plain(KRC_MAX_TEXT_BYTES, 'a');
	std::string Text;
	ASSERT_TRUE(DecryptKrc(BuildEncryptedKrc(Plain), &Text));
	EXPECT_EQ(Text, Plain);
}

TEST(MusicLyricsKrc, RejectsHighlyCompressedTextOverBudgetWithoutPartialOutput)
{
	std::string Text = "previous";
	EXPECT_FALSE(DecryptKrc(BuildEncryptedKrc(std::string(KRC_MAX_TEXT_BYTES + 1, 'a')), &Text));
	EXPECT_TRUE(Text.empty());
}

TEST(MusicLyricsKrc, RejectsTruncatedCompressedStream)
{
	std::string Encrypted = BuildEncryptedKrc(SAMPLE_KRC);
	ASSERT_GT(Encrypted.size(), 10u);
	Encrypted.resize(Encrypted.size() - 4);
	std::string Text;
	EXPECT_FALSE(DecryptKrc(Encrypted, &Text));
	EXPECT_TRUE(Text.empty());
}

TEST(MusicLyricsKrc, SkipsInvalidStartTimeAndKeepsFollowingValidLine)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText("[invalid,100]bad\n[2000,500]valid\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Timeline.m_vLines[0].m_StartMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "valid");
}

TEST(MusicLyricsKrc, SkipsInvalidDurationInsteadOfCreatingZeroTimeLine)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText("[0,invalid]bad\n[2000,500]valid\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "valid");
}

TEST(MusicLyricsKrc, SkipsOverflowingEndTime)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText("[9223372036854775807,1]bad\n[2000,500]valid\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 1u);
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 2500);
}

TEST(MusicLyricsKrc, UnknownEndsUseTheNextDistinctStartAndKeepStableDuplicateOrder)
{
	NeteaseLyrics::STimeline Timeline;
	ASSERT_TRUE(ParseKrcText("[2000,0]last-unknown\n[1000,0]first-unknown\n[0,25]explicit\n[1000,50]second-explicit\n[1000,0]third-unknown\n[2000,10]last-explicit\n", &Timeline));
	ASSERT_EQ(Timeline.m_vLines.size(), 6u);
	EXPECT_EQ(Timeline.m_vLines[0].m_Text, "explicit");
	EXPECT_EQ(Timeline.m_vLines[0].m_EndMs, 25);
	EXPECT_EQ(Timeline.m_vLines[1].m_Text, "first-unknown");
	EXPECT_EQ(Timeline.m_vLines[1].m_EndMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[2].m_Text, "second-explicit");
	EXPECT_EQ(Timeline.m_vLines[2].m_EndMs, 1050);
	EXPECT_EQ(Timeline.m_vLines[3].m_Text, "third-unknown");
	EXPECT_EQ(Timeline.m_vLines[3].m_EndMs, 2000);
	EXPECT_EQ(Timeline.m_vLines[4].m_Text, "last-unknown");
	EXPECT_EQ(Timeline.m_vLines[4].m_EndMs, -1);
	EXPECT_EQ(Timeline.m_vLines[5].m_Text, "last-explicit");
	EXPECT_EQ(Timeline.m_vLines[5].m_EndMs, 2010);
}

TEST(MusicLyricsKrc, TranslationTrimsJoinedFragmentsAndPreservesOtherControlBytes)
{
	const std::string Text = TranslationTag(R"({"content":[{"type":0,"lyricContent":[["ignored"]]},{"type":1,"lyricContent":[[" \t","translated",5,"\r\n "],[],null,[" \t"],["\u0000\u0001\u001f"]]},{"type":1,"lyricContent":[["later track"]]}]})");
	const std::vector<std::string> Translation = ExtractKrcTranslation(Text);
	ASSERT_EQ(Translation.size(), 5u);
	EXPECT_EQ(Translation[0], "translated");
	EXPECT_TRUE(Translation[1].empty());
	EXPECT_TRUE(Translation[2].empty());
	EXPECT_TRUE(Translation[3].empty());
	EXPECT_EQ(Translation[4], std::string("\0\x01\x1F", 3));
}

TEST(MusicLyricsKrc, LongTranslationWhitespaceWithinTheTextBudgetIsTrimmed)
{
	const std::string Json = "{\"content\":[{\"type\":1,\"lyricContent\":[[\"" + std::string(1024 * 1024, ' ') + "translated \"]]}]}";
	const std::string Text = TranslationTag(Json);
	ASSERT_LT(Text.size(), KRC_MAX_TEXT_BYTES);
	const std::vector<std::string> Translation = ExtractKrcTranslation(Text);
	ASSERT_EQ(Translation.size(), 1u);
	EXPECT_EQ(Translation[0], "translated");
}

TEST(MusicLyricsKrc, EmbeddedTranslationsFollowSourceRowsThroughFilteringAndStableSorting)
{
	const std::string Text = TranslationTag(R"({"content":[{"type":1,"lyricContent":[["late-t"],["empty-t"],["broken-t"],["early-first-t"],["early-second-t"]]}]})") +
				 "[ti:title]\n[ar:artist, another]\n\nplain noise\n[00:00,10]unsupported header\n"
				 "[2000,0]late\n[1500,0]\n[1200,100]<broken>discarded\n[1000,0]<0,100>early-first\n[1000,50]early-second\n";
	SLyricsData Data;
	ASSERT_TRUE(ParseKrcTextData(Text, &Data));
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 3u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "early-first");
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_EndMs, 2000);
	ASSERT_EQ(Data.m_Timeline.m_vLines[0].m_vWords.size(), 1u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_vWords[0].m_StartMs, 1000);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_vWords[0].m_Text, "early-first");
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "early-second");
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_EndMs, 1050);
	EXPECT_EQ(Data.m_Timeline.m_vLines[2].m_Text, "late");
	EXPECT_EQ(Data.m_Timeline.m_vLines[2].m_EndMs, -1);
	const std::vector<std::string> Expected{"early-first-t", "early-second-t", "late-t"};
	EXPECT_EQ(Data.m_vTranslations, Expected);
	EXPECT_TRUE(Data.HasTranslation());
}

TEST(MusicLyricsKrc, RejectedNumericTimeHeadersStillConsumeTheirSourceTranslationSlot)
{
	const char *apHeaders[] = {
		"[ 9223372036854775808 , 1 ]",
		"[9223372036854775807,1]",
		"[0,9223372036854775808]",
		"[153722867280913:00.000]",
		"[00:60.000]",
	};
	for(const char *pHeader : apHeaders)
	{
		SCOPED_TRACE(pHeader);
		const std::string Text = TranslationTag(R"({"content":[{"type":1,"lyricContent":[["rejected-t"],["kept-t"]]}]})") +
					 pHeader + "discarded\n[invalid,100]noise\n[ti:metadata, comma]\n[00:01.00]kept\n";
		SLyricsData Data;
		ASSERT_TRUE(ParseKrcTextData(Text, &Data));
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_StartMs, 1000);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "kept");
		ASSERT_EQ(Data.m_vTranslations.size(), 1u);
		EXPECT_EQ(Data.m_vTranslations[0], "kept-t");
	}
}

TEST(MusicLyricsKrc, MissingShortAndLongEmbeddedTracksKeepOnlyTheirOwnMainRows)
{
	struct SCase
	{
		const char *m_pJson;
		std::vector<std::string> m_vExpected;
	};
	const SCase aCases[] = {
		{nullptr, {}},
		{R"({"content":[{"type":1,"lyricContent":[]}]})", {}},
		{R"({"content":[{"type":1,"lyricContent":[["last-t"]]}]})", {"", "", "last-t"}},
		{R"({"content":[{"type":1,"lyricContent":[["last-t"],["first-t"],["middle-t"],["extra-t"]]}]})", {"first-t", "middle-t", "last-t"}},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_pJson == nullptr ? "missing" : Case.m_pJson);
		const std::string Text = (Case.m_pJson == nullptr ? std::string() : TranslationTag(Case.m_pJson)) +
					 "[3000,100]last\n[1000,100]first\n[2000,100]middle\n";
		SLyricsData Data;
		ASSERT_TRUE(ParseKrcTextData(Text, &Data));
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 3u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "first");
		EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "middle");
		EXPECT_EQ(Data.m_Timeline.m_vLines[2].m_Text, "last");
		EXPECT_EQ(Data.m_vTranslations, Case.m_vExpected);
	}
}

TEST(MusicLyricsKrc, CombinedEntriesKeepOldDataOnFailureAndEncryptedInputUsesTheSameAlignment)
{
	SLyricsData Data;
	const std::string OldText = TranslationTag(R"({"content":[{"type":1,"lyricContent":[["old-t"]]}]})") + "[1000,100]old\n";
	ASSERT_TRUE(ParseKrcTextData(OldText, &Data));
	Data.m_Song.m_Title = "old metadata";
	for(const char *pBadText : {"plain", "\xff"})
	{
		SCOPED_TRACE(pBadText);
		EXPECT_FALSE(ParseKrcTextData(pBadText, &Data));
		EXPECT_EQ(Data.m_Song.m_Title, "old metadata");
		ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 1u);
		EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "old");
		ASSERT_EQ(Data.m_vTranslations.size(), 1u);
		EXPECT_EQ(Data.m_vTranslations[0], "old-t");
	}
	EXPECT_FALSE(ParseKrcData("bad magic", &Data));
	EXPECT_EQ(Data.m_Song.m_Title, "old metadata");
	ASSERT_EQ(Data.m_vTranslations.size(), 1u);
	EXPECT_EQ(Data.m_vTranslations[0], "old-t");
	const std::string Text = TranslationTag(R"({"content":[{"type":1,"lyricContent":[["late-t"],["early-t"]]}]})") +
				 "[2000,100]late\n[1000,100]early\n";
	ASSERT_TRUE(ParseKrcData(BuildEncryptedKrc(Text), &Data));
	EXPECT_TRUE(Data.m_Song.m_Title.empty());
	ASSERT_EQ(Data.m_Timeline.m_vLines.size(), 2u);
	EXPECT_EQ(Data.m_Timeline.m_vLines[0].m_Text, "early");
	EXPECT_EQ(Data.m_Timeline.m_vLines[1].m_Text, "late");
	const std::vector<std::string> Expected{"early-t", "late-t"};
	EXPECT_EQ(Data.m_vTranslations, Expected);
}
