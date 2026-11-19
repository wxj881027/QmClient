#include <gtest/gtest.h>
#include <qm/hooks/soda/qm_soda_probe.h>

using namespace QmSodaProbe;

TEST(QmSodaProbe, InnerScriptIsAsciiAndIdempotent)
{
	const std::string Script = BuildInnerProbeScript();
	// atob 只认 Latin-1:脚本必须纯 ASCII。
	for(const unsigned char C : Script)
		EXPECT_LT(C, 0x80u);
	EXPECT_NE(Script.find("transportPort"), std::string::npos);
	EXPECT_NE(Script.find("method.invoke"), std::string::npos);
	EXPECT_NE(Script.find("sharedState"), std::string::npos);
	EXPECT_NE(Script.find("mediaDetail"), std::string::npos);
	EXPECT_NE(Script.find("lyricContent"), std::string::npos);
	EXPECT_NE(Script.find("translationLrc"), std::string::npos);
}

TEST(QmSodaProbe, BridgeExpressionEncodesInnerScript)
{
	const std::string Bridge = BuildBridgeExpression();
	EXPECT_NE(Bridge.find("executeJavaScript"), std::string::npos);
	EXPECT_NE(Bridge.find("main.html"), std::string::npos);
	EXPECT_NE(Bridge.find("setBackgroundThrottling(false)"), std::string::npos);
	EXPECT_NE(Bridge.find("atob(\""), std::string::npos);
	// 汽水 3.7.0 的 inspector 全局上下文没有 CJS require,必须走 mainModule。
	EXPECT_NE(Bridge.find("process.mainModule.require('electron')"), std::string::npos);
	EXPECT_EQ(Bridge.find("const {webContents}=require('electron')"), std::string::npos);
}

TEST(QmSodaProbe, ParsesFullPlaybackSnapshot)
{
	const char *Json = R"({"ok":true,"isPlaying":true,"isLoading":false,"progressSeconds":42.5,"durationSeconds":250,"mediaId":"6849382015611963393","name":"晴天","album":"叶惠美","artists":["周杰伦"],"coverUrl":"http://x/cover.jpg","lyricType":"krc","lyricContent":"[0,4390]<0,274,0>雨","translationLrc":"[00:00.00]雨","throttled":false})";
	SPlaybackSnapshot Snapshot;
	ASSERT_TRUE(ParseExtractionJson(Json, &Snapshot));
	EXPECT_TRUE(Snapshot.m_Ok);
	EXPECT_TRUE(Snapshot.m_IsPlaying);
	EXPECT_EQ(Snapshot.m_MediaId, "6849382015611963393");
	EXPECT_EQ(Snapshot.m_Name, "晴天");
	EXPECT_EQ(Snapshot.m_Artist, "周杰伦");
	EXPECT_EQ(Snapshot.m_Album, "叶惠美");
	EXPECT_DOUBLE_EQ(Snapshot.m_ProgressSeconds, 42.5);
	EXPECT_DOUBLE_EQ(Snapshot.m_DurationSeconds, 250);
	EXPECT_EQ(Snapshot.PositionMs(), 42500);
	EXPECT_EQ(Snapshot.DurationMs(), 250000);
	EXPECT_EQ(Snapshot.m_LyricType, "krc");
	EXPECT_EQ(Snapshot.m_LyricContent, "[0,4390]<0,274,0>雨");
	EXPECT_EQ(Snapshot.m_TranslationLrc, "[00:00.00]雨");
	EXPECT_FALSE(Snapshot.m_Throttled);
}

TEST(QmSodaProbe, ReportsErrorWhenNotOk)
{
	const char *Json = R"({"err":"no-port"})";
	SPlaybackSnapshot Snapshot;
	EXPECT_FALSE(ParseExtractionJson(Json, &Snapshot));
	EXPECT_EQ(Snapshot.m_Error, "no-port");
}

TEST(QmSodaProbe, RejectsEmptyAndNull)
{
	SPlaybackSnapshot Snapshot;
	EXPECT_FALSE(ParseExtractionJson("", &Snapshot));
	EXPECT_FALSE(ParseExtractionJson("null", &Snapshot));
	EXPECT_FALSE(ParseExtractionJson("not json", &Snapshot));
}

TEST(QmSodaProbe, RejectsSnapshotWithoutUsableFields)
{
	const char *Json = R"({"ok":true,"isPlaying":false,"progressSeconds":1})";
	SPlaybackSnapshot Snapshot;
	EXPECT_FALSE(ParseExtractionJson(Json, &Snapshot));
	EXPECT_EQ(Snapshot.m_Error, "extraction has no usable fields");
}

TEST(QmSodaProbe, UnavailableAndOutOfRangeTimesDoNotDiscardTheSong)
{
	SPlaybackSnapshot Snapshot;
	ASSERT_TRUE(ParseExtractionJson(R"({"ok":true,"mediaId":"42","progressSeconds":1e16,"durationSeconds":1e16})", &Snapshot));
	EXPECT_EQ(Snapshot.PositionMs(), -1);
	EXPECT_EQ(Snapshot.DurationMs(), 0);
	EXPECT_EQ(Snapshot.m_MediaId, "42");

	ASSERT_TRUE(ParseExtractionJson(R"({"ok":true,"mediaId":"42","durationSeconds":-1})", &Snapshot));
	EXPECT_EQ(Snapshot.PositionMs(), -1);
	EXPECT_EQ(Snapshot.DurationMs(), 0);

	ASSERT_TRUE(ParseExtractionJson(R"({"ok":true,"mediaId":"42","progressSeconds":0})", &Snapshot));
	EXPECT_EQ(Snapshot.PositionMs(), 0);
}

TEST(QmSodaProbe, SelectsOnlyTargetsOnTheDiscoveredLoopbackPort)
{
	const char *pTargets = R"([
		{"type":"node","webSocketDebuggerUrl":"ws://127.0.0.1:11200/other"},
		{"type":"node","webSocketDebuggerUrl":"ws://example.invalid:11201/remote"},
		{"type":"page","webSocketDebuggerUrl":"ws://127.0.0.1:11201/fallback"},
		{"type":"node","webSocketDebuggerUrl":"ws://127.0.0.1:11201/player"}])";
	EXPECT_EQ(PickNodeTarget(pTargets, 11201), "ws://127.0.0.1:11201/player");
	EXPECT_TRUE(PickNodeTarget(pTargets, 11202).empty());
}

TEST(QmSodaProbe, UsesFirstEligibleTargetWhenTypeIsUnavailable)
{
	const char *pTargets = R"([
		{"webSocketDebuggerUrl":"ws://127.0.0.1:11200/wrong"},
		{"webSocketDebuggerUrl":"ws://127.0.0.1:11201/first"},
		{"webSocketDebuggerUrl":"ws://127.0.0.1:11201/second"}])";
	EXPECT_EQ(PickNodeTarget(pTargets, 11201), "ws://127.0.0.1:11201/first");
}

TEST(QmSodaLyricPublication, LateLyricsPublishWithoutChangingTheSongGeneration)
{
	CLyricFilePublication Lyrics;
	SPlaybackSnapshot Snapshot;
	Snapshot.m_MediaId = "42";
	int Writes = 0;
	const auto Write = [&](const SPlaybackSnapshot &) {
		++Writes;
		return std::string("lyrics-42.json");
	};
	Lyrics.Update(Snapshot, 100, Write);
	const uint64_t Generation = Lyrics.Generation();
	EXPECT_GT(Generation, 0u);
	EXPECT_TRUE(Lyrics.Path().empty());
	EXPECT_EQ(Writes, 0);

	Snapshot.m_LyricContent = "[00:01.00]lyric";
	Lyrics.Update(Snapshot, 400, Write);
	EXPECT_EQ(Lyrics.Generation(), Generation);
	EXPECT_EQ(Lyrics.Path(), "lyrics-42.json");
	Lyrics.Update(Snapshot, 2000, Write);
	EXPECT_EQ(Writes, 1);
}

TEST(QmSodaLyricPublication, RetainsSameSongForRetryAfterFailedFileReplacement)
{
	CLyricFilePublication Lyrics;
	SPlaybackSnapshot Snapshot;
	Snapshot.m_MediaId = "42";
	Snapshot.m_LyricContent = "[00:01.00]lyric";
	int Writes = 0;
	const auto Write = [&](const SPlaybackSnapshot &) -> std::string {
		if(++Writes == 1)
			return {};
		return "lyrics-42.json";
	};
	Lyrics.Update(Snapshot, 100, Write);
	const uint64_t Generation = Lyrics.Generation();
	EXPECT_TRUE(Lyrics.Path().empty());
	Lyrics.Update(Snapshot, 1099, Write);
	EXPECT_EQ(Writes, 1);
	Lyrics.Update(Snapshot, 1100, Write);
	EXPECT_EQ(Writes, 2);
	EXPECT_EQ(Lyrics.Path(), "lyrics-42.json");
	EXPECT_EQ(Lyrics.Generation(), Generation);
}

TEST(QmSodaLyricPublication, SongChangeClearsPreviousPathAndRetryDelay)
{
	CLyricFilePublication Lyrics;
	SPlaybackSnapshot Snapshot;
	Snapshot.m_MediaId = "42";
	Snapshot.m_LyricContent = "[00:01.00]lyric";
	Lyrics.Update(Snapshot, 100, [](const SPlaybackSnapshot &) { return std::string("lyrics-42.json"); });
	ASSERT_EQ(Lyrics.Path(), "lyrics-42.json");
	const uint64_t Generation = Lyrics.Generation();

	Snapshot.m_MediaId = "73";
	Snapshot.m_LyricContent.clear();
	Lyrics.Update(Snapshot, 400, [](const SPlaybackSnapshot &) { return std::string("unreachable.json"); });
	EXPECT_GT(Lyrics.Generation(), Generation);
	EXPECT_TRUE(Lyrics.Path().empty());

	Snapshot.m_LyricContent = "[00:02.00]next";
	Lyrics.Update(Snapshot, 700, [](const SPlaybackSnapshot &) { return std::string("lyrics-73.json"); });
	EXPECT_EQ(Lyrics.Path(), "lyrics-73.json");
}

TEST(QmSodaLyricPublication, FilePreparationExceptionLeavesAnEmptyPathAndRetries)
{
	CLyricFilePublication Lyrics;
	SPlaybackSnapshot Snapshot;
	Snapshot.m_MediaId = "42";
	Snapshot.m_LyricContent = "[00:01.00]lyric";
	EXPECT_NO_THROW(Lyrics.Update(Snapshot, 100, [](const SPlaybackSnapshot &) -> std::string { throw std::bad_alloc(); }));
	EXPECT_TRUE(Lyrics.Path().empty());
	Lyrics.Update(Snapshot, 1100, [](const SPlaybackSnapshot &) { return std::string("lyrics-42.json"); });
	EXPECT_EQ(Lyrics.Path(), "lyrics-42.json");
}
