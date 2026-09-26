// QmClient 语音集成行为测试。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1
#include "test.h"

#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/voice/voice_capture_pipeline.h>
#include <game/client/components/qmclient/voice/voice_core.h>
#include <game/client/components/qmclient/voice/voice_utils.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(CONF_RNNOISE)
#include <rnnoise.h>
#endif

using namespace VoiceUtils;

namespace VoiceUtils
{
	int ResolveNoiseSuppressMode(int ConfigValue, bool RnnoiseRuntimeAvailable, bool *pFallbackUsed);
}

static constexpr int TEST_VOICE_NOISE_SUPPRESS_OFF = 0;
static constexpr int TEST_VOICE_NOISE_SUPPRESS_SIMPLE = 1;
static constexpr int TEST_VOICE_NOISE_SUPPRESS_RNNOISE = 2;

TEST(QmClient, ParseQmClientUsersJsonSupportsNameFieldsAndLocalMarks)
{
	const char *pJsonText =
		"{\"users\":["
		"{\"server_address\":\"addr-b\",\"player_name\":\"RemoteDummy\",\"dummy\":true},"
		"{\"server\":\"addr-a\",\"name\":\"QmSeven\",\"client_type\":\"arg\",\"qid\":\"qm-7\",\"foot_particles_enabled\":1,"
		"\"remote_particles_enabled\":true,\"voice_supported\":0},"
		"{\"server_address\":\"addr-a\",\"player_name\":\"QmEight\",\"dummy\":false,\"type\":\"qm\"}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmClientUsersParseResult Result;
	EXPECT_TRUE(ParseQmClientUsersJson(pJson, "addr-a", Result));
	EXPECT_TRUE(Result.m_Parsed);
	ASSERT_EQ(Result.m_vServerDistribution.size(), 2u);
	EXPECT_EQ(Result.m_vServerDistribution[0].m_ServerAddress, "addr-a");
	EXPECT_EQ(Result.m_vServerDistribution[0].m_UserCount, 2);
	EXPECT_EQ(Result.m_vServerDistribution[0].m_DummyCount, 0);
	EXPECT_EQ(Result.m_vServerDistribution[1].m_ServerAddress, "addr-b");
	EXPECT_EQ(Result.m_vServerDistribution[1].m_UserCount, 0);
	EXPECT_EQ(Result.m_vServerDistribution[1].m_DummyCount, 1);
	EXPECT_EQ(Result.m_OnlineUserCount, 2);
	EXPECT_EQ(Result.m_OnlineDummyCount, 1);

	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "QmSeven");
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_FootParticlesEnabled);
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_RemoteParticlesEnabled);
	EXPECT_FALSE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_ClientBrand, EClientBrand::ARG);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Qid, "qm-7");

	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Name, "QmEight");
	EXPECT_TRUE(Result.m_vLocalServerMarks[1].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_ClientBrand, EClientBrand::QM);

	json_value_free(pJson);
}

TEST(QmClient, ParseQmClientUsersJsonRejectsMissingUsersArrayAndSkipsBrokenEntries)
{
	const char *pJsonText =
		"["
		"{\"server_address\":\"addr-a\",\"player_name\":\"QmThree\",\"dummy\":0},"
		"{\"server_address\":17,\"player_name\":\"QmFour\"},"
		"{\"server_address\":\"addr-a\"},"
		"{\"server_address\":\"addr-a\",\"player_id\":5,\"voice_supported\":true},"
		"{\"server_address\":\"addr-a\",\"name\":\"QmFive\",\"voice_supported\":true}"
		"]";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmClientUsersParseResult Result;
	EXPECT_TRUE(ParseQmClientUsersJson(pJson, "addr-a", Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_OnlineUserCount, 2);
	EXPECT_EQ(Result.m_OnlineDummyCount, 0);
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 2u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "QmThree");
	EXPECT_TRUE(Result.m_vLocalServerMarks[0].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_ClientBrand, EClientBrand::QM);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_Name, "QmFive");
	EXPECT_TRUE(Result.m_vLocalServerMarks[1].m_VoiceSupported);
	EXPECT_EQ(Result.m_vLocalServerMarks[1].m_ClientBrand, EClientBrand::QM);

	json_value_free(pJson);

	const char *pBadRootText = "{\"not_users\":{}}";
	pJson = json_parse(pBadRootText, str_length(pBadRootText));
	ASSERT_NE(pJson, nullptr);
	EXPECT_FALSE(ParseQmClientUsersJson(pJson, "addr-a", Result));
	EXPECT_FALSE(Result.m_Parsed);
	EXPECT_TRUE(Result.m_vServerDistribution.empty());
	EXPECT_TRUE(Result.m_vLocalServerMarks.empty());
	json_value_free(pJson);
}

TEST(QmClient, ParseQmDeveloperPresencesSupportsArbitraryNamesAndDummy)
{
	const char *pJsonText =
		"{\"server_time\":1000,\"presences\":["
		"{\"developer_id\":\"qimen-main\",\"server_address\":\"addr-a\",\"player_id\":7,\"player_name\":\"Any nickname\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":19},"
		"{\"developer_id\":\"qimen-dummy\",\"server_address\":\"addr-a\",\"player_id\":8,\"player_name\":\"完全不同的昵称\",\"dummy\":true,\"issued_at\":950,\"expires_at\":1050,\"style_bucket\":20}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	EXPECT_TRUE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 1000);
	ASSERT_EQ(Result.m_vPresences.size(), 2u);

	const SQmDeveloperPresence *pMain = FindQmDeveloperPresence(Result.m_vPresences, "addr-a", 7, "Any nickname", 1000);
	ASSERT_NE(pMain, nullptr);
	EXPECT_EQ(pMain->m_DeveloperId, "qimen-main");
	EXPECT_FALSE(pMain->m_Dummy);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(pMain->m_StyleBucket), EQmDeveloperBadgeStyle::RAINBOW);

	const SQmDeveloperPresence *pDummy = FindQmDeveloperPresence(Result.m_vPresences, "addr-a", 8, "完全不同的昵称", 1000);
	ASSERT_NE(pDummy, nullptr);
	EXPECT_EQ(pDummy->m_DeveloperId, "qimen-dummy");
	EXPECT_TRUE(pDummy->m_Dummy);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(pDummy->m_StyleBucket), EQmDeveloperBadgeStyle::BLACK);

	json_value_free(pJson);
}

TEST(QmClient, FindQmDeveloperPresenceRequiresExactServerIdAndName)
{
	std::vector<SQmDeveloperPresence> vPresences = {
		{"developer-one", "addr-a", 4, "SameName", false, 900, 1100, 1},
		{"developer-two", "addr-a", 5, "SameName", true, 900, 1100, 2},
	};

	const SQmDeveloperPresence *pFound = FindQmDeveloperPresence(vPresences, "addr-a", 5, "SameName", 1000);
	ASSERT_NE(pFound, nullptr);
	EXPECT_EQ(pFound->m_DeveloperId, "developer-two");

	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 4, "Renamed", 1000), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-b", 4, "SameName", 1000), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 6, "SameName", 1000), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 4, "samename", 1000), nullptr);
}

TEST(QmClient, ParseQmDeveloperPresencesKeepsOnlyValidLocalEntries)
{
	const char *pJsonText =
		"{\"server_time\":1000,\"presences\":["
		"{\"developer_id\":\"valid\",\"server_address\":\"addr-a\",\"player_id\":3,\"player_name\":\"Valid Name\",\"dummy\":false,\"issued_at\":1000,\"expires_at\":1001,\"style_bucket\":99},"
		"{\"developer_id\":\"remote\",\"server_address\":\"addr-b\",\"player_id\":3,\"player_name\":\"Valid Name\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"expired\",\"server_address\":\"addr-a\",\"player_id\":4,\"player_name\":\"Expired\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1000,\"style_bucket\":0},"
		"{\"developer_id\":\"future\",\"server_address\":\"addr-a\",\"player_id\":5,\"player_name\":\"Future\",\"dummy\":false,\"issued_at\":1001,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"\",\"server_address\":\"addr-a\",\"player_id\":6,\"player_name\":\"No developer\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"no-name\",\"server_address\":\"addr-a\",\"player_id\":7,\"player_name\":\"\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"negative-id\",\"server_address\":\"addr-a\",\"player_id\":-1,\"player_name\":\"Bad id\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"large-id\",\"server_address\":\"addr-a\",\"player_id\":128,\"player_name\":\"Bad id\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"negative-style\",\"server_address\":\"addr-a\",\"player_id\":8,\"player_name\":\"Bad style\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":-1},"
		"{\"developer_id\":\"large-style\",\"server_address\":\"addr-a\",\"player_id\":9,\"player_name\":\"Bad style\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":100},"
		"{\"developer_id\":\"bad-dummy\",\"server_address\":\"addr-a\",\"player_id\":10,\"player_name\":\"Bad dummy\",\"dummy\":1,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"bad-time\",\"server_address\":\"addr-a\",\"player_id\":11,\"player_name\":\"Bad time\",\"dummy\":false,\"issued_at\":\"900\",\"expires_at\":1100,\"style_bucket\":0},"
		"{\"developer_id\":\"missing-style\",\"server_address\":\"addr-a\",\"player_id\":12,\"player_name\":\"Missing style\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	ASSERT_EQ(Result.m_vPresences.size(), 1u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "valid");
	EXPECT_EQ(Result.m_vPresences[0].m_PlayerId, 3);
	EXPECT_EQ(Result.m_vPresences[0].m_StyleBucket, 99);

	json_value_free(pJson);
}

TEST(QmClient, FindQmDeveloperPresenceRechecksLifetime)
{
	const std::vector<SQmDeveloperPresence> vPresences = {
		{"developer", "addr-a", 1, "Name", false, 100, 200, 0},
	};

	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 99), nullptr);
	EXPECT_NE(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 100), nullptr);
	EXPECT_NE(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 199), nullptr);
	EXPECT_EQ(FindQmDeveloperPresence(vPresences, "addr-a", 1, "Name", 200), nullptr);
}

TEST(QmClient, QmDeveloperBadgeStyleUsesExactlyTwentyRainbowBuckets)
{
	int RainbowCount = 0;
	for(int Bucket = 0; Bucket < 100; ++Bucket)
	{
		if(QmDeveloperBadgeStyleFromBucket(Bucket) == EQmDeveloperBadgeStyle::RAINBOW)
			++RainbowCount;
	}

	EXPECT_EQ(RainbowCount, 20);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(-1), EQmDeveloperBadgeStyle::BLACK);
	EXPECT_EQ(QmDeveloperBadgeStyleFromBucket(100), EQmDeveloperBadgeStyle::BLACK);
}

TEST(QmClient, DeveloperBadgeVisibilityRequiresAuthenticationAndVisibleIdentity)
{
	EXPECT_TRUE(ShouldShowQmDeveloperBadge(true, true, false));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(false, true, false));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(true, false, false));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(true, true, true));
	EXPECT_FALSE(ShouldShowQmDeveloperBadge(false, false, true));
}

TEST(QmClient, DeveloperMarkRequiresTheSameActivePlayerNameAndUnexpiredLease)
{
	EXPECT_TRUE(IsQmDeveloperMarkCurrent(true, "Authenticated Name", "Authenticated Name", 101, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(false, "Authenticated Name", "Authenticated Name", 101, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(true, "Authenticated Name", "Renamed", 101, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(true, "Authenticated Name", "Authenticated Name", 100, 100));
	EXPECT_FALSE(IsQmDeveloperMarkCurrent(true, "", "Authenticated Name", 101, 100));
}

TEST(QmClient, ParseQmDeveloperPresencesResetsOutputAndKeepsOrder)
{
	const char *pJsonText =
		"{\"server_time\":1000,\"presences\":["
		"{\"developer_id\":\"first\",\"server_address\":\"addr-a\",\"player_id\":1,\"player_name\":\"First\",\"dummy\":false,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":1},"
		"{\"developer_id\":\"second\",\"server_address\":\"addr-a\",\"player_id\":2,\"player_name\":\"Second\",\"dummy\":true,\"issued_at\":900,\"expires_at\":1100,\"style_bucket\":2}"
		"]}";

	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	ASSERT_EQ(Result.m_vPresences.size(), 2u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "first");
	EXPECT_EQ(Result.m_vPresences[1].m_DeveloperId, "second");

	ASSERT_TRUE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	ASSERT_EQ(Result.m_vPresences.size(), 2u);
	EXPECT_EQ(Result.m_vPresences[0].m_DeveloperId, "first");
	EXPECT_EQ(Result.m_vPresences[1].m_DeveloperId, "second");

	json_value_free(pJson);
}

TEST(QmClient, ParseQmDeveloperPresencesRejectsMalformedRoot)
{
	const char *pJsonText = "{\"server_time\":1000,\"presences\":{}}";
	json_value *pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);

	SQmDeveloperPresenceParseResult Result;
	EXPECT_FALSE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	EXPECT_FALSE(Result.m_Parsed);
	EXPECT_TRUE(Result.m_vPresences.empty());

	json_value_free(pJson);

	pJsonText = "{\"presences\":[]}";
	pJson = json_parse(pJsonText, str_length(pJsonText));
	ASSERT_NE(pJson, nullptr);
	EXPECT_FALSE(ParseQmDeveloperPresencesJson(pJson, "addr-a", Result));
	EXPECT_FALSE(Result.m_Parsed);
	EXPECT_EQ(Result.m_ServerTime, 0);
	json_value_free(pJson);
}
