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
