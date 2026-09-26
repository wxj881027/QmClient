#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>
#include <game/client/components/qmclient/qmclient_utils.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

TEST(QmRealtime, ParsesStateAndBroadcast)
{
	SQmRealtimeMessage Message;
	const char *pState = "{\"type\":\"state\",\"data\":{\"online_users\":12}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pState, std::strlen(pState), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::STATE);
	EXPECT_TRUE(Message.m_HasOnlineUsers);
	EXPECT_EQ(Message.m_OnlineUsers, 12);
	const char *pBroadcast = "{\"type\":\"broadcast\",\"data\":{\"markdown\":\"hello\",\"version\":2}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pBroadcast, std::strlen(pBroadcast), Message));
	EXPECT_TRUE(Message.m_HasBroadcast);
	EXPECT_EQ(Message.m_BroadcastVersion, 2);
}

TEST(QmRealtime, EmptyConfigurationUsesDedicatedWebSocket)
{
	EXPECT_STREQ(QmRealtimeEffectiveUrl(nullptr), "wss://qmclient.icu/ws");
	EXPECT_STREQ(QmRealtimeEffectiveUrl(""), "wss://qmclient.icu/ws");
	EXPECT_STREQ(QmRealtimeEffectiveUrl("wss://example.test/ws"), "wss://example.test/ws");
}

TEST(QmRealtime, NormalizesAnonymousServerAddresses)
{
	EXPECT_EQ(NormalizeQmServerAddress(" Example.COM:08303/ "), "example.com:8303");
	EXPECT_EQ(NormalizeQmServerAddress("ws://EXAMPLE.com:8303"), "example.com:8303");
	EXPECT_EQ(NormalizeQmServerAddress("[2001:DB8::1]:08303"), "[2001:db8::1]:8303");
	EXPECT_EQ(NormalizeQmServerAddress("2001:DB8::1"), "[2001:db8::1]");
	EXPECT_TRUE(NormalizeQmServerAddress("example.com:not-a-port").empty());
}

TEST(QmRealtime, CredentialsAreOnlyAllowedOnDedicatedEncryptedEndpoint)
{
	EXPECT_TRUE(QmRealtimeAllowsCredentials(QmRealtimeEffectiveUrl("")));
	EXPECT_FALSE(QmRealtimeAllowsCredentials(nullptr));
	EXPECT_FALSE(QmRealtimeAllowsCredentials("ws://qmclient.icu/ws"));
	EXPECT_FALSE(QmRealtimeAllowsCredentials("wss://qmclient.icu/ws/other"));
	EXPECT_FALSE(QmRealtimeAllowsCredentials("wss://example.test/ws"));
}

TEST(QmRealtime, RejectsOversizedBroadcastPayload)
{
	std::string Markdown(64 * 1024 + 1, 'x');
	const std::string Data = "{\"type\":\"broadcast\",\"data\":{\"markdown\":\"" + Markdown + "\"}}";
	SQmRealtimeMessage Message;
	ASSERT_TRUE(ParseQmRealtimeMessage(Data.data(), Data.size(), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::BROADCAST);
	EXPECT_FALSE(Message.m_HasBroadcast);
}

TEST(QmRealtime, RejectsMalformedMessages)
{
	SQmRealtimeMessage Message;
	EXPECT_FALSE(ParseQmRealtimeMessage(nullptr, 0, Message));
	EXPECT_FALSE(ParseQmRealtimeMessage("[]", 2, Message));
	EXPECT_FALSE(ParseQmRealtimeMessage("{\"type\":5}", 10, Message));
}

TEST(QmRealtime, ParsesAnonymousEmoticonLaunch)
{
	SQmRealtimeMessage Message;
	const char *pData = "{\"type\":\"emoticon\",\"data\":{\"client_id\":\"anon\",\"player_id\":3,\"player_name\":\"tee\",\"server_address\":\"example:8303\",\"emoticon\":5,\"launch_mode\":true,\"super_launch\":false}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_TRUE(Message.m_HasEmoticon);
	EXPECT_EQ(Message.m_Emoticon, 5);
	EXPECT_EQ(Message.m_PlayerId, 3);
	EXPECT_TRUE(Message.m_EmoticonPayloadValid);
	EXPECT_EQ(Message.m_EmoticonPlayerId, 3);
	EXPECT_EQ(Message.m_EmoticonId, 5);
	EXPECT_TRUE(Message.m_EmoticonLaunch);
	EXPECT_FALSE(Message.m_EmoticonSuperLaunch);
	EXPECT_EQ(Message.m_EmoticonPlayerName, "tee");
}

TEST(QmRealtime, ParsesAnonymousEmoticonServerAddress)
{
	SQmRealtimeMessage Message;
	const char *pData = "{\"type\":\"emoticon\",\"data\":{\"client_id\":\"anon\",\"player_id\":3,\"player_name\":\"tee\",\"emoticon\":5,\"server_address\":\"[::1]:8303\",\"launch_mode\":true}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_EQ(Message.m_EmoticonServerAddress, "[::1]:8303");
}

TEST(QmRealtime, ParsesLaunchMode)
{
	SQmRealtimeMessage Message;
	const char *pData = "{\"type\":\"emoticon\",\"data\":{\"client_id\":\"anon\",\"player_id\":3,\"player_name\":\"tee\",\"server_address\":\"example:8303\",\"emoticon\":5,\"launch_mode\":true}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_TRUE(Message.m_EmoticonLaunch);
}

TEST(QmRealtime, ParsesSuperLaunchAlongsideLaunchMode)
{
	SQmRealtimeMessage Message;
	const char *pData = "{\"type\":\"emoticon\",\"data\":{\"client_id\":\"anon\",\"player_id\":3,\"player_name\":\"tee\",\"server_address\":\"example:8303\",\"emoticon\":5,\"launch_mode\":true,\"super_launch\":true}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_TRUE(Message.m_EmoticonPayloadValid);
	EXPECT_TRUE(Message.m_EmoticonLaunch);
	EXPECT_TRUE(Message.m_EmoticonSuperLaunch);
}

TEST(QmRealtime, ParsesServiceEventsWithoutTreatingThemAsUnknown)
{
	SQmRealtimeMessage Message;
	const char *pData = "{\"type\":\"titles\",\"data\":{\"server_time\":123}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::TITLES);
	EXPECT_TRUE(Message.m_HasRealtimeData);

	const char *pError = "{\"type\":\"error\",\"data\":{\"code\":\"busy\"}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(pError, std::strlen(pError), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::ERROR);
	EXPECT_TRUE(Message.m_HasRealtimeData);
}

TEST(QmRealtime, KeepsValidatedTitleSnapshotAliveUntilConsumed)
{
	SQmRealtimeMessage Message;
	const char *pSnapshot = R"({"type":"titles","data":{"server_address":"127.0.0.1:8303","server_time":1700000000,"presences":[]}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pSnapshot, std::strlen(pSnapshot), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::TITLES);
	ASSERT_TRUE(Message.m_HasTitles);
	ASSERT_NE(Message.m_pTitlePayload, nullptr);
	EXPECT_EQ(json_object_get(Message.m_pTitlePayload.get(), "presences")->type, json_array);

	const char *pInvalid = R"({"type":"titles","data":{"server_time":1700000000,"presences":"invalid"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pInvalid, std::strlen(pInvalid), Message));
	EXPECT_FALSE(Message.m_HasTitles);

	const char *pMissing = R"({"type":"titles","data":{"server_time":1700000000}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pMissing, std::strlen(pMissing), Message));
	EXPECT_FALSE(Message.m_HasTitles);

	const char *pTopLevel = R"({"type":"titles","server_time":1700000000,"presences":[]})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pTopLevel, std::strlen(pTopLevel), Message));
	EXPECT_TRUE(Message.m_HasTitles);
	EXPECT_FALSE(Message.m_HasRealtimeData);
	ASSERT_NE(Message.m_pPayload, nullptr);
	EXPECT_EQ(Message.m_pPayload, Message.m_pTitlePayload);
}

TEST(QmRealtime, KeepsServicePayloadAliveForMainThreadConsumers)
{
	SQmRealtimeMessage Message;
	const char *pUsers = R"({"type":"users","data":{"server_address":"127.0.0.1:8303","users":[]}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pUsers, std::strlen(pUsers), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::USERS);
	ASSERT_NE(Message.m_pPayload, nullptr);
	EXPECT_EQ(json_object_get(Message.m_pPayload.get(), "users")->type, json_array);

	const char *pPlaytime = R"({"type":"playtime","data":{"ts":1700000000,"total_seconds":42,"last_start_at":1699999990,"action":"start"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pPlaytime, std::strlen(pPlaytime), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::PLAYTIME);
	ASSERT_NE(Message.m_pPayload, nullptr);
	EXPECT_EQ(json_object_get(Message.m_pPayload.get(), "total_seconds")->u.integer, 42);
}

TEST(QmRealtime, ClampsLargeOnlineCountBeforeNarrowing)
{
	SQmRealtimeMessage Message;
	const char *pData = R"({"type":"state","data":{"online_users":9223372036854775807}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_TRUE(Message.m_HasOnlineUsers);
	EXPECT_EQ(Message.m_OnlineUsers, 1000000);
}

TEST(QmRealtime, ParsesServiceDataForLocalStateApplication)
{
	SQmRealtimeMessage Message;
	const char *pData = R"({"type":"title_profile","data":{"server_time":1700000000,"playtime_seconds":42,"title":"Dream","bound_name":"Player","style":"exotic_rainbow"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
	EXPECT_TRUE(Message.m_HasServerTime);
	EXPECT_EQ(Message.m_ServerTime, 1700000000);
	EXPECT_TRUE(Message.m_HasPlaytimeSeconds);
	EXPECT_EQ(Message.m_PlaytimeSeconds, 42);
	EXPECT_TRUE(Message.m_HasTitleProfile);
	EXPECT_EQ(Message.m_TitleText, "Dream");
	EXPECT_EQ(Message.m_TitleBoundName, "Player");
	EXPECT_TRUE(Message.m_HasTitleStyle);
	EXPECT_EQ(Message.m_TitleStyle, "exotic_rainbow");
}

TEST(QmRealtime, PartialTitleProfileDoesNotClearStyleButExplicitEmptyDoes)
{
	SQmRealtimeMessage Message;
	const char *pPartial = R"({"type":"title_profile","data":{"title":"Dream"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pPartial, std::strlen(pPartial), Message));
	EXPECT_TRUE(Message.m_HasTitleProfile);
	EXPECT_FALSE(Message.m_HasTitleStyle);

	const char *pCleared = R"({"type":"title_profile","data":{"style":""}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pCleared, std::strlen(pCleared), Message));
	EXPECT_TRUE(Message.m_HasTitleProfile);
	EXPECT_TRUE(Message.m_HasTitleStyle);
	EXPECT_TRUE(Message.m_TitleStyle.empty());
}

TEST(QmRealtime, TitleProfileDistinguishesOmittedAndClearedFields)
{
	SQmRealtimeMessage Message;
	const char *pPartial = R"({"type":"title_profile","data":{"style":"exotic_rainbow"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pPartial, std::strlen(pPartial), Message));
	EXPECT_FALSE(Message.m_HasTitleText);
	EXPECT_FALSE(Message.m_HasTitleBoundName);

	const char *pCleared = R"({"type":"title_profile","data":{"title":"","bound_name":"","style":""}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pCleared, std::strlen(pCleared), Message));
	EXPECT_TRUE(Message.m_HasTitleText);
	EXPECT_TRUE(Message.m_TitleText.empty());
	EXPECT_TRUE(Message.m_HasTitleBoundName);
	EXPECT_TRUE(Message.m_TitleBoundName.empty());
	EXPECT_TRUE(Message.m_HasTitleStyle);
	EXPECT_TRUE(Message.m_TitleStyle.empty());
}

TEST(QmRealtime, ParsesTitleServiceStatusCodes)
{
	SQmRealtimeMessage Message;
	const char *pProfile = R"({"type":"title_profile","data":{"status":200,"title":"Dream"}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pProfile, std::strlen(pProfile), Message));
	EXPECT_TRUE(Message.m_HasTitleAuthenticated);
	EXPECT_TRUE(Message.m_TitleAuthenticated);
	EXPECT_TRUE(Message.m_HasTitleStatusCode);
	EXPECT_EQ(Message.m_TitleStatusCode, 200);

	const char *pConflict = R"({"type":"title_status","data":{"status":409}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pConflict, std::strlen(pConflict), Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::TITLE_STATUS);
	EXPECT_TRUE(Message.m_HasTitleStatusCode);
	EXPECT_EQ(Message.m_TitleStatusCode, 409);
	EXPECT_FALSE(Message.m_HasTitleAuthenticated);
}

TEST(QmRealtime, PreservesAnonymousEmoticonIdentityAndSequence)
{
	SQmRealtimeMessage Message;
	std::string Data = "{\"type\":\"emoticon\",\"data\":{\"client_id\":\"abc\",\"sequence\":42,\"player_id\":3,\"player_name\":\"tee\",\"server_address\":\"example:8303\",\"emoticon\":5,\"launch_mode\":true}}";
	ASSERT_TRUE(ParseQmRealtimeMessage(Data.data(), Data.size(), Message));
	Data.clear();
	EXPECT_TRUE(Message.m_HasRealtimeData);
	ASSERT_NE(Message.m_pPayload, nullptr);
	EXPECT_EQ(json_object_get(Message.m_pPayload.get(), "sequence")->u.integer, 42);
	EXPECT_EQ(Message.m_EmoticonClientId, "abc");
	EXPECT_EQ(Message.m_EmoticonSequence, 42U);
}

TEST(QmRealtime, RejectsIncompleteAndLegacyAnonymousEmoticons)
{
	for(const char *pData : {
		    R"({"type":"emoticon","data":{"player_id":3,"player_name":"tee","server_address":"example:8303","emoticon":5,"launch_mode":true}})",
		    R"({"type":"emoticon","data":{"client_id":"anon","player_id":3,"player_name":"tee","server_address":"example:8303","emoticon":5,"launch":true}})",
		    R"({"type":"emoticon","data":{"client_id":"anon","player_id":3,"player_name":"tee","server_address":"example:8303","emoticon":999,"launch_mode":true}})"})
	{
		SQmRealtimeMessage Message;
		ASSERT_TRUE(ParseQmRealtimeMessage(pData, std::strlen(pData), Message));
		EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::EMOTICON);
		EXPECT_FALSE(Message.m_HasEmoticon);
		EXPECT_FALSE(Message.m_EmoticonPayloadValid);
	}
}

TEST(QmRealtime, DistributionLeaseExpiryKeepsSnapshotWithoutConsumingRecognitionMarks)
{
	SQmClientDistributionSnapshot Snapshot;
	EXPECT_FALSE(Snapshot.IsStale(100));

	SQmClientUsersParseResult Result;
	Result.m_Parsed = true;
	Result.m_vServerDistribution = {{"one:8303", 2, 1}};
	Result.m_OnlineUserCount = 2;
	Result.m_OnlineDummyCount = 1;
	Result.m_vLocalServerMarks.emplace_back().m_Name = "player";
	ASSERT_TRUE(Snapshot.Apply(Result, 120));

	EXPECT_FALSE(Snapshot.IsStale(119));
	EXPECT_TRUE(Snapshot.IsStale(120));
	ASSERT_EQ(Snapshot.m_vServers.size(), 1u);
	EXPECT_EQ(Snapshot.m_vServers[0].m_ServerAddress, "one:8303");
	EXPECT_EQ(Snapshot.m_OnlineUserCount, 2);
	EXPECT_EQ(Snapshot.m_OnlineDummyCount, 1);
	// 识别标记仍由客户端按在线状态和自身租约处理，不被展示快照消耗。
	ASSERT_EQ(Result.m_vLocalServerMarks.size(), 1u);
	EXPECT_EQ(Result.m_vLocalServerMarks[0].m_Name, "player");
}

TEST(QmRealtime, InvalidDistributionDoesNotEraseOrRenewDisplayedSnapshot)
{
	SQmClientDistributionSnapshot Snapshot;
	SQmClientUsersParseResult Valid;
	Valid.m_Parsed = true;
	Valid.m_vServerDistribution = {{"one:8303", 2, 1}};
	Valid.m_OnlineUserCount = 2;
	Valid.m_OnlineDummyCount = 1;
	ASSERT_TRUE(Snapshot.Apply(Valid, 120));

	SQmRealtimeMessage Message;
	const char *pInvalid = R"({"type":"users","data":{"users":null}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pInvalid, std::strlen(pInvalid), Message));
	SQmClientUsersParseResult Invalid;
	ASSERT_FALSE(ParseQmClientUsersJson(Message.m_pPayload.get(), "one:8303", Invalid));
	ASSERT_FALSE(Snapshot.Apply(Invalid, 500));
	ASSERT_EQ(Snapshot.m_vServers.size(), 1u);
	EXPECT_EQ(Snapshot.m_vServers[0].m_ServerAddress, "one:8303");
	EXPECT_EQ(Snapshot.m_OnlineUserCount, 2);
	EXPECT_EQ(Snapshot.m_OnlineDummyCount, 1);
	EXPECT_TRUE(Snapshot.IsStale(120));
}

TEST(QmRealtime, NewDistributionReplacesOldSnapshotAndValidEmptyListClearsIt)
{
	SQmClientDistributionSnapshot Snapshot;
	SQmClientUsersParseResult First;
	First.m_Parsed = true;
	First.m_vServerDistribution = {{"one:8303", 2, 1}};
	First.m_OnlineUserCount = 2;
	First.m_OnlineDummyCount = 1;
	ASSERT_TRUE(Snapshot.Apply(First, 120));
	EXPECT_TRUE(Snapshot.IsStale(150));

	SQmClientUsersParseResult Next;
	Next.m_Parsed = true;
	Next.m_vServerDistribution = {{"two:8303", 1, 0}};
	Next.m_OnlineUserCount = 1;
	ASSERT_TRUE(Snapshot.Apply(Next, 170));
	ASSERT_EQ(Snapshot.m_vServers.size(), 1u);
	EXPECT_EQ(Snapshot.m_vServers[0].m_ServerAddress, "two:8303");
	EXPECT_EQ(Snapshot.m_OnlineUserCount, 1);
	EXPECT_EQ(Snapshot.m_OnlineDummyCount, 0);
	EXPECT_FALSE(Snapshot.IsStale(150));

	SQmRealtimeMessage Message;
	const char *pEmpty = R"({"type":"users","data":{"users":[]}})";
	ASSERT_TRUE(ParseQmRealtimeMessage(pEmpty, std::strlen(pEmpty), Message));
	SQmClientUsersParseResult Empty;
	ASSERT_TRUE(ParseQmClientUsersJson(Message.m_pPayload.get(), "two:8303", Empty));
	ASSERT_TRUE(Snapshot.Apply(Empty, 200));
	EXPECT_TRUE(Snapshot.m_vServers.empty());
	EXPECT_EQ(Snapshot.m_OnlineUserCount, 0);
	EXPECT_EQ(Snapshot.m_OnlineDummyCount, 0);
	EXPECT_FALSE(Snapshot.IsStale(180));
}
