#include <game/client/components/qmclient/music_lyrics/qm_lxmusic_config.h>
#include <game/client/components/qmclient/music_lyrics/qm_lxmusic_parser.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// LX Music 开放 API 的响应样例取自上游 src/main/modules/openApi/index.ts:
// /status 返回 player_status 的字段子集,progress 与 duration 是浮点秒;
// /lyric-all 返回 { lyric, tlyric, rlyric, lxlyric },lyric 为整首 LRC。
// 本文件只测纯逻辑(解析、合成、配置改写、重试节奏),不触碰网络与文件系统。
namespace
{
	const char *STATUS_JSON = R"json({"status":"playing","name":"晴天","singer":"周杰伦",
		"albumName":"叶惠美","picUrl":"https://img.example/cover.jpg",
		"progress":12.75,"duration":269,"playbackRate":1})json";

	const char *LYRIC_JSON = R"json({"lyric":"[00:01.00]Hello world\n[00:03.50]Second line\n",
		"tlyric":"[00:01.00]你好世界\n[00:03.50]第二行\n","rlyric":"","lxlyric":""})json";

	// LX 数据同步服务在同一路径上返回的 401 文本,用来验证"非 JSON 不当成状态"。
	const char *FORBIDDEN_BODY = "Forbidden";

	// 本机 config_v2.json 的真实结构:同步服务与开放 API 都配成 23330,且开放 API 关闭。
	// 额外塞入歌单数组,用来验证改写只发生在 setting 段内。
	const char *CLIENT_CONFIG_CONFLICT_JSON = R"json({"version":1,
		"playlist":[{"name":"list","openAPI.enable":false,"openAPI.port":"1"}],
		"setting":{"openAPI.enable":false,"openAPI.port":"23330","openAPI.bindLan":false,
			"sync.mode":"server","sync.enable":true,"sync.server.port":"23330"}})json";
} // namespace

// ==== /status 解析 ====

TEST(QmLxMusicParser, StatusConvertsSecondsToMilliseconds)
{
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(STATUS_JSON, &Status));
	EXPECT_TRUE(Status.m_Valid);
	EXPECT_EQ(Status.m_PlaybackStatus, "playing");
	EXPECT_TRUE(Status.IsPlaying());
	EXPECT_EQ(Status.m_Title, "晴天");
	EXPECT_EQ(Status.m_Artist, "周杰伦");
	EXPECT_EQ(Status.m_Album, "叶惠美");
	EXPECT_EQ(Status.m_CoverUrl, "https://img.example/cover.jpg");
	EXPECT_EQ(Status.m_PositionMs, 12750);
	EXPECT_EQ(Status.m_DurationMs, 269000);
	EXPECT_DOUBLE_EQ(Status.m_PlaybackRate, 1.0);
}

TEST(QmLxMusicParser, StatusTreatsStopedSpellingAsNotPlaying)
{
	// 上游拼写就是 stoped,不能被当成未知值。
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"status":"stoped"})json", &Status));
	EXPECT_FALSE(Status.IsPlaying());
	EXPECT_EQ(Status.m_PositionMs, 0);
	EXPECT_EQ(Status.m_DurationMs, 0);
}

TEST(QmLxMusicParser, StatusAcceptsIntegerSeconds)
{
	// 字段是浮点秒,但整数写法同样合法(简写响应、四舍五入后都可能出现)。
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"progress":12,"duration":269})json", &Status));
	EXPECT_EQ(Status.m_PositionMs, 12000);
	EXPECT_EQ(Status.m_DurationMs, 269000);
}

TEST(QmLxMusicParser, StatusIgnoresStringEncodedNumbers)
{
	// 字符串形式的秒数不按数字解释,免得把 "12.5" 当成 0 以外的意外值。
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"progress":"12.5","duration":"269"})json", &Status));
	EXPECT_EQ(Status.m_PositionMs, 0);
	EXPECT_EQ(Status.m_DurationMs, 0);
}

TEST(QmLxMusicParser, StatusRejectsNonJsonBody)
{
	QmLxMusic::SStatus Status;
	EXPECT_FALSE(QmLxMusic::ParseStatus(FORBIDDEN_BODY, &Status));
	EXPECT_FALSE(Status.m_Valid);
}

TEST(QmLxMusicParser, StatusRejectsJsonNonObject)
{
	QmLxMusic::SStatus Status;
	EXPECT_FALSE(QmLxMusic::ParseStatus("[1,2,3]", &Status));
	EXPECT_FALSE(QmLxMusic::ParseStatus("", &Status));
}

TEST(QmLxMusicParser, ClampsOutOfRangePlaybackRate)
{
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"playbackRate":0})json", &Status));
	// 0 或负数没有意义,回落到 1 倍速而不是让进度外推停死。
	EXPECT_DOUBLE_EQ(Status.m_PlaybackRate, 1.0);
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"playbackRate":999})json", &Status));
	EXPECT_DOUBLE_EQ(Status.m_PlaybackRate, 8.0);
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"playbackRate":0.5})json", &Status));
	EXPECT_DOUBLE_EQ(Status.m_PlaybackRate, 0.5);
}

TEST(QmLxMusicParser, StatusClampsAbsurdDuration)
{
	// 异常大的秒数不应该溢出成负数,上限取 24 小时。
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"duration":1000000000})json", &Status));
	EXPECT_EQ(Status.m_DurationMs, 24LL * 60 * 60 * 1000);
	ASSERT_TRUE(QmLxMusic::ParseStatus(R"json({"progress":-5})json", &Status));
	EXPECT_EQ(Status.m_PositionMs, 0);
}

TEST(QmLxMusicParser, StatusMissingFieldsStillParses)
{
	// 空对象意味着 LX 没有在播;解析成功但状态为空,调用方据此清空歌曲。
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus("{}", &Status));
	EXPECT_TRUE(Status.m_Valid);
	EXPECT_FALSE(Status.IsPlaying());
	EXPECT_TRUE(Status.m_Title.empty());
	EXPECT_TRUE(Status.m_Artist.empty());
	EXPECT_DOUBLE_EQ(Status.m_PlaybackRate, 1.0);
}

TEST(QmLxMusicParser, StatusFailureKeepsPreviousOutput)
{
	// 解析失败时保持输出不变:上一轮的状态不能被坏响应抹掉。
	QmLxMusic::SStatus Status;
	ASSERT_TRUE(QmLxMusic::ParseStatus(STATUS_JSON, &Status));
	EXPECT_FALSE(QmLxMusic::ParseStatus(FORBIDDEN_BODY, &Status));
	EXPECT_EQ(Status.m_Title, "晴天");
	EXPECT_EQ(Status.m_PositionMs, 12750);
}

// ==== /lyric-all 解析 ====

TEST(QmLxMusicParser, LyricAllExtractsBothTracks)
{
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(LYRIC_JSON, &Payload));
	EXPECT_TRUE(Payload.m_Valid);
	EXPECT_NE(Payload.m_Lyric.find("Hello world"), std::string::npos);
	EXPECT_NE(Payload.m_TranslationLrc.find("你好世界"), std::string::npos);
}

TEST(QmLxMusicParser, LyricAllAcceptsEmptyLyric)
{
	// 音源还没取到歌词时字段是空串;解析成功但 BuildLyrics 会拒绝。
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":"","tlyric":""})json", &Payload));
	EXPECT_TRUE(Payload.m_Lyric.empty());
	EXPECT_TRUE(Payload.m_TranslationLrc.empty());
}

TEST(QmLxMusicParser, LyricAllIgnoresNonStringTracks)
{
	// 字段类型不符时按空串处理,不能因为一条轨道异常就丢掉整首歌词。
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":123,"tlyric":null})json", &Payload));
	EXPECT_TRUE(Payload.m_Lyric.empty());
	EXPECT_TRUE(Payload.m_TranslationLrc.empty());
}

TEST(QmLxMusicParser, LyricAllFailureKeepsPreviousOutput)
{
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(LYRIC_JSON, &Payload));
	EXPECT_FALSE(QmLxMusic::ParseLyricAll(FORBIDDEN_BODY, &Payload));
	EXPECT_NE(Payload.m_Lyric.find("Hello world"), std::string::npos);
}

// ==== LRC 合成 ====

TEST(QmLxMusicParser, BuildLyricsProducesTimelineAndTranslation)
{
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseStatus(STATUS_JSON, &Status));
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(LYRIC_JSON, &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	ASSERT_TRUE(Lyrics.HasLyrics());
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "Hello world");
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_StartMs, 3500);

	// 歌曲信息随状态一起落到歌词数据,避免展示层再回查。
	EXPECT_EQ(Lyrics.m_Song.m_Title, "晴天");
	EXPECT_EQ(Lyrics.m_Song.m_Artist, "周杰伦");
	EXPECT_EQ(Lyrics.m_Song.m_Album, "叶惠美");
	EXPECT_EQ(Lyrics.m_Song.m_DurationMs, 269000);

	ASSERT_TRUE(Lyrics.HasTranslation());
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 2);
	EXPECT_EQ(Lyrics.m_vTranslations[0], "你好世界");
	EXPECT_EQ(Lyrics.m_vTranslations[1], "第二行");
}

TEST(QmLxMusicParser, BuildLyricsRejectsUntimedText)
{
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseStatus(STATUS_JSON, &Status));
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":"just plain text without timestamps"})json", &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	// 没有时间戳就不构造时间轴,调用方据此继续重试或保持无歌词。
	EXPECT_FALSE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	EXPECT_FALSE(Lyrics.HasLyrics());
}

TEST(QmLxMusicParser, BuildLyricsParsesColonCentiseconds)
{
	// LX 自己生成的提示行是 [mm:ss:xx],第三段是百分秒,不能被当成秒的分隔符。
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":"[00:01:50]colon\n[00:02:05]next"})json", &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_StartMs, 1500);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_StartMs, 2050);
}

TEST(QmLxMusicParser, BuildLyricsExpandsMultipleTimestampsPerLine)
{
	// 一行多个时间戳是 LRC 的重复副歌写法,应展开成多条独立行。
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":"[00:01.00][00:05.00]chorus"})json", &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	ASSERT_EQ(Lyrics.m_Timeline.m_vLines.size(), 2);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_StartMs, 1000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[1].m_StartMs, 5000);
	EXPECT_EQ(Lyrics.m_Timeline.m_vLines[0].m_Text, "chorus");
}

TEST(QmLxMusicParser, BuildLyricsWithoutTranslationLeavesNoTranslations)
{
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":"[00:01.00]only","tlyric":""})json", &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	EXPECT_FALSE(Lyrics.HasTranslation());
	EXPECT_TRUE(Lyrics.m_vTranslations.empty());
}

TEST(QmLxMusicParser, BuildLyricsRejectsMetadataOnlyLrc)
{
	// 只有 [ti:][ar:] 这类标签没有时间轴,应当继续等歌词而不是显示空岛。
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(R"json({"lyric":"[ti:title]\n[ar:artist]\n[by:someone]"})json", &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	EXPECT_FALSE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	EXPECT_FALSE(Lyrics.HasLyrics());
}

TEST(QmLxMusicParser, BuildLyricsAlignsTranslationWithinTolerance)
{
	// 原文与翻译的时间戳允许 150ms 误差;超出则留空而不是错配。
	QmLxMusic::SStatus Status;
	QmLxMusic::SLyricPayload Payload;
	ASSERT_TRUE(QmLxMusic::ParseLyricAll(
		R"json({"lyric":"[00:01.00]a\n[00:05.00]b","tlyric":"[00:01.10]A\n[00:05.90]B"})json", &Payload));

	QmMusicLyrics::SLyricsData Lyrics;
	ASSERT_TRUE(QmLxMusic::BuildLyrics(Status, Payload, &Lyrics));
	ASSERT_EQ(Lyrics.m_vTranslations.size(), 2);
	EXPECT_EQ(Lyrics.m_vTranslations[0], "A"); // 100ms 内,命中
	EXPECT_TRUE(Lyrics.m_vTranslations[1].empty()); // 900ms,不匹配
}

// ==== 身份哈希 ====

TEST(QmLxMusicParser, IdentityHashSeparatesTitleArtistBoundary)
{
	const uint64_t Abc = QmLxMusic::IdentityHash("AB", "C");
	const uint64_t AbcSplit = QmLxMusic::IdentityHash("A", "BC");
	EXPECT_NE(Abc, AbcSplit);
	EXPECT_EQ(QmLxMusic::IdentityHash("AB", "C"), Abc);
	EXPECT_NE(QmLxMusic::IdentityHash("AB", "C"), QmLxMusic::IdentityHash("AB", "D"));
}

TEST(QmLxMusicParser, IdentityHashHandlesEmptyInput)
{
	// 空标题 + 空歌手仍要得到稳定值,且与任一单字段不同。
	const uint64_t Empty = QmLxMusic::IdentityHash("", "");
	EXPECT_EQ(QmLxMusic::IdentityHash("", ""), Empty);
	EXPECT_NE(QmLxMusic::IdentityHash("", "x"), Empty);
	EXPECT_NE(QmLxMusic::IdentityHash("x", ""), Empty);
}

// ==== 歌词重试节奏 ====

TEST(QmLxMusicParser, LyricRetryDelayIsFastThenSlow)
{
	using namespace QmLxMusic;
	// 前 4 次快重试覆盖"切歌后歌词晚到 3 秒内"的常见情况。
	for(int Count = 0; Count < MAX_FAST_LYRIC_RETRIES; ++Count)
		EXPECT_EQ(LyricRetryDelayMs(Count), 1200);
	// 之后放慢,给慢音源最多几十秒的窗口。
	EXPECT_EQ(LyricRetryDelayMs(MAX_FAST_LYRIC_RETRIES), 5000);
	EXPECT_EQ(LyricRetryDelayMs(MAX_LYRIC_RETRIES - 1), 5000);
	EXPECT_GT(MAX_LYRIC_RETRIES, MAX_FAST_LYRIC_RETRIES);
}

// ==== 客户端配置解析 ====

TEST(QmLxMusicConfig, ReadsOpenApiPortAndEnableFlag)
{
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(CLIENT_CONFIG_CONFLICT_JSON, &Config));
	EXPECT_TRUE(Config.m_Found);
	EXPECT_FALSE(Config.m_OpenApiEnabled);
	EXPECT_EQ(Config.m_OpenApiPort, 23330);
	EXPECT_TRUE(Config.m_SyncServerEnabled);
	EXPECT_EQ(Config.m_SyncServerPort, 23330);
}

TEST(QmLxMusicConfig, DetectsPortConflictWithSyncService)
{
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(CLIENT_CONFIG_CONFLICT_JSON, &Config));
	// 两个服务同端口时只有一个能绑定,另一个静默失效。
	EXPECT_TRUE(Config.HasPortConflict());
}

TEST(QmLxMusicConfig, NoConflictWhenPortsDiffer)
{
	const char *JSON = R"json({"setting":{"openAPI.enable":true,"openAPI.port":"23340",
		"sync.enable":true,"sync.server.port":"23332"}})json";
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(JSON, &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 23340);
	EXPECT_FALSE(Config.HasPortConflict());
}

TEST(QmLxMusicConfig, SyncDisabledIsNotAConflict)
{
	// 同步服务关掉时,即使端口值相同也不会抢占。
	const char *JSON = R"json({"setting":{"openAPI.enable":true,"openAPI.port":"23330",
		"sync.enable":false,"sync.server.port":"23330"}})json";
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(JSON, &Config));
	EXPECT_FALSE(Config.HasPortConflict());
}

TEST(QmLxMusicConfig, MissingPortFallsBackToUpstreamDefaultForConflict)
{
	// 配置里没有 openAPI.port 时 LX 用默认 23330,冲突判定必须跟着用默认值。
	const char *JSON = R"json({"setting":{"openAPI.enable":true,"sync.enable":true,"sync.server.port":"23330"}})json";
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(JSON, &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 0);
	EXPECT_EQ(Config.EffectiveOpenApiPort(), QmLxMusic::DEFAULT_OPEN_API_PORT);
	EXPECT_TRUE(Config.HasPortConflict());
}

TEST(QmLxMusicConfig, MissingSyncPortIsNotAConflict)
{
	// 同步端口没写时不该凭空判定冲突,否则会把正常配置改成 23340。
	const char *JSON = R"json({"setting":{"openAPI.enable":true,"sync.enable":true}})json";
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(JSON, &Config));
	EXPECT_FALSE(Config.HasPortConflict());
}

TEST(QmLxMusicConfig, AcceptsIntegerPortAndRejectsOutOfRange)
{
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(R"json({"setting":{"openAPI.port":23341}})json", &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 23341);

	ASSERT_TRUE(QmLxMusic::ParseClientConfig(R"json({"setting":{"openAPI.port":"70000"}})json", &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 0);

	ASSERT_TRUE(QmLxMusic::ParseClientConfig(R"json({"setting":{"openAPI.port":""}})json", &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 0);

	ASSERT_TRUE(QmLxMusic::ParseClientConfig(R"json({"setting":{"openAPI.port":"abc"}})json", &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 0);
}

TEST(QmLxMusicConfig, RejectsJsonWithoutSetting)
{
	// 解析失败时保持输出不变:上一轮读到的配置不能被坏数据抹掉。
	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(CLIENT_CONFIG_CONFLICT_JSON, &Config));
	EXPECT_FALSE(QmLxMusic::ParseClientConfig(R"json({"version":1})json", &Config));
	EXPECT_FALSE(QmLxMusic::ParseClientConfig("[]", &Config));
	EXPECT_FALSE(QmLxMusic::ParseClientConfig("", &Config));
	EXPECT_TRUE(Config.m_Found);
	EXPECT_EQ(Config.m_OpenApiPort, 23330);
}

// ==== 配置文本改写 ====

TEST(QmLxMusicConfigText, SetBoolEnablesOpenApiAndKeepsOtherBytes)
{
	std::string Text = CLIENT_CONFIG_CONFLICT_JSON;
	ASSERT_TRUE(QmLxMusic::ConfigTextSetBool(Text, "openAPI.enable", true));

	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(Text, &Config));
	EXPECT_TRUE(Config.m_OpenApiEnabled);
	// 其余字段必须逐字保留:改写只允许动目标值。
	EXPECT_NE(Text.find(R"("openAPI.bindLan":false)"), std::string::npos);
	EXPECT_NE(Text.find(R"("sync.server.port":"23330")"), std::string::npos);
}

TEST(QmLxMusicConfigText, SetBoolOnlyTouchesSettingSection)
{
	// 歌单里同名键不能被误改:改写范围限定在 setting 段。
	std::string Text = CLIENT_CONFIG_CONFLICT_JSON;
	ASSERT_NE(Text.find(R"("setting")"), std::string::npos);
	ASSERT_LT(Text.find(R"("openAPI.enable":false)"), Text.find(R"("setting")")); // 前提:歌单在 setting 之前
	ASSERT_TRUE(QmLxMusic::ConfigTextSetBool(Text, "openAPI.enable", true));

	// 歌单那一处仍是 false。
	EXPECT_NE(Text.find(R"("openAPI.enable":false)"), std::string::npos);
	// setting 那一处已变成 true,且全文件只出现一次。
	const size_t EnabledPos = Text.find(R"("openAPI.enable":true)");
	ASSERT_NE(EnabledPos, std::string::npos);
	EXPECT_GT(EnabledPos, Text.find(R"("setting")"));
	EXPECT_EQ(EnabledPos, Text.rfind(R"("openAPI.enable":true)"));
}

TEST(QmLxMusicConfigText, SetBoolIsIdempotentWhenAlreadyEnabled)
{
	std::string Text = R"json({"setting":{"openAPI.enable":true}})json";
	ASSERT_TRUE(QmLxMusic::ConfigTextSetBool(Text, "openAPI.enable", true));
	EXPECT_EQ(Text, R"json({"setting":{"openAPI.enable":true}})json");
}

TEST(QmLxMusicConfigText, SetBoolFailsWhenKeyMissing)
{
	std::string Text = R"json({"setting":{"openAPI.port":"23330"}})json";
	const std::string Before = Text;
	EXPECT_FALSE(QmLxMusic::ConfigTextSetBool(Text, "openAPI.enable", true));
	EXPECT_EQ(Text, Before);
}

TEST(QmLxMusicConfigText, SetPortReplacesQuotedValue)
{
	std::string Text = CLIENT_CONFIG_CONFLICT_JSON;
	ASSERT_TRUE(QmLxMusic::ConfigTextSetPort(Text, "openAPI.port", 23340));

	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(Text, &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 23340);
	// 歌单里的同名字段不受影响。
	EXPECT_NE(Text.find(R"("openAPI.port":"1")"), std::string::npos);
}

TEST(QmLxMusicConfigText, SetPortRewritesBareNumberAsQuotedString)
{
	// 上游自己用字符串存端口;把裸数字改写成字符串才不会出现类型不一致。
	std::string Text = R"json({"setting":{"openAPI.port":23330}})json";
	ASSERT_TRUE(QmLxMusic::ConfigTextSetPort(Text, "openAPI.port", 23340));
	EXPECT_NE(Text.find(R"("openAPI.port":"23340")"), std::string::npos);

	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(Text, &Config));
	EXPECT_EQ(Config.m_OpenApiPort, 23340);
}

TEST(QmLxMusicConfigText, SetPortFailsWhenKeyMissing)
{
	std::string Text = R"json({"setting":{"openAPI.enable":true}})json";
	const std::string Before = Text;
	EXPECT_FALSE(QmLxMusic::ConfigTextSetPort(Text, "openAPI.port", 23340));
	EXPECT_EQ(Text, Before);
}

TEST(QmLxMusicConfigText, SetPortRejectsOutOfRangeValue)
{
	std::string Text = R"json({"setting":{"openAPI.port":"23330"}})json";
	const std::string Before = Text;
	EXPECT_FALSE(QmLxMusic::ConfigTextSetPort(Text, "openAPI.port", 0));
	EXPECT_FALSE(QmLxMusic::ConfigTextSetPort(Text, "openAPI.port", 65536));
	EXPECT_EQ(Text, Before);
}

TEST(QmLxMusicConfigText, SettingScanSkipsBracesInsideStrings)
{
	// 字段值里出现花括号时,范围扫描不能被提前截断。
	std::string Text = R"json({"setting":{"openAPI.path":"a}b","openAPI.enable":false}})json";
	ASSERT_TRUE(QmLxMusic::ConfigTextSetBool(Text, "openAPI.enable", true));

	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(Text, &Config));
	EXPECT_TRUE(Config.m_OpenApiEnabled);
}

TEST(QmLxMusicConfigText, EnableRoundTripKeepsUnrelatedSettings)
{
	// 完整的「一键开启」改写序列:开开关 + 冲突端口错开,其余配置逐字不动。
	std::string Text = CLIENT_CONFIG_CONFLICT_JSON;
	ASSERT_TRUE(QmLxMusic::ConfigTextSetBool(Text, "openAPI.enable", true));
	ASSERT_TRUE(QmLxMusic::ConfigTextSetPort(Text, "openAPI.port", 23340));

	QmLxMusic::SClientConfig Config;
	ASSERT_TRUE(QmLxMusic::ParseClientConfig(Text, &Config));
	EXPECT_TRUE(Config.m_OpenApiEnabled);
	EXPECT_EQ(Config.m_OpenApiPort, 23340);
	EXPECT_FALSE(Config.HasPortConflict());
	EXPECT_TRUE(Config.m_SyncServerEnabled);
	EXPECT_EQ(Config.m_SyncServerPort, 23330);
	EXPECT_NE(Text.find(R"("sync.mode":"server")"), std::string::npos);
	EXPECT_NE(Text.find(R"("name":"list")"), std::string::npos);
}
