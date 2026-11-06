#include <engine/shared/json.h>

#include <game/client/components/qmclient/qm_realtime.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	bool Parse(const std::string &Data, SQmRealtimeMessage &Message)
	{
		return ParseQmRealtimeMessage(Data.data(), Data.size(), Message);
	}

	void ExpectEmpty(const SQmRealtimeMessage &Message)
	{
		EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::INVALID);
		EXPECT_TRUE(Message.m_Type.empty());
		EXPECT_FALSE(Message.m_HasRealtimeData);
		EXPECT_FALSE(Message.m_HasBroadcast);
		EXPECT_FALSE(Message.m_HasEmoticon);
		EXPECT_FALSE(Message.m_HasTitleProfile);
		EXPECT_EQ(Message.m_pPayload, nullptr);
		EXPECT_EQ(Message.m_pTitlePayload, nullptr);
	}

	std::string Emoticon(const std::string &Identity, const std::string &Flags = R"("launch_mode":true)")
	{
		return R"({"type":"emoticon","data":{)" + Identity + R"(,"player_id":3,"emoticon":5,)" + Flags + "}}";
	}
}

TEST(QmRealtimeEnvelope, NullSuffixedKnownTagsAreRejectedAndClearPreviousPayload)
{
	SQmRealtimeMessage Message;
	for(const char *pType : {"ping", "pong", "state", "broadcast", "titles", "emoticon", "sponsors", "users", "users_sync", "developers", "playtime", "time", "title_profile", "title_status", "error"})
	{
		SCOPED_TRACE(pType);
		ASSERT_TRUE(Parse(R"({"type":"title_profile","data":{"title":"old"}})", Message));
		ASSERT_NE(Message.m_pPayload, nullptr);
		const std::string Data = std::string(R"({"type":")") + pType + R"(\u0000extra","data":{}})";
		EXPECT_FALSE(Parse(Data, Message));
		ExpectEmpty(Message);
	}
}

TEST(QmRealtimeEnvelope, InvalidUtf8TagsAreRejected)
{
	SQmRealtimeMessage Message;
	for(const std::string Bad : {"\x80", "\xe4\xbd", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"})
	{
		SCOPED_TRACE(::testing::PrintToString(Bad));
		const std::string Data = std::string(R"({"type":"ping)") + Bad + R"("})";
		EXPECT_FALSE(Parse(Data, Message));
		ExpectEmpty(Message);
	}
}

TEST(QmRealtimeEnvelope, TypeMemberNameMustMatchItsCompleteLength)
{
	SQmRealtimeMessage Message;
	EXPECT_FALSE(Parse(R"({"type\u0000extra":"ping"})", Message));
	ExpectEmpty(Message);
}

TEST(QmRealtimeEnvelope, UnknownTypeMemberCannotShadowExactType)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type\u0000extra":"ping","type":"pong"})", Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::PONG);
	EXPECT_EQ(Message.m_Type, "pong");
}

TEST(QmRealtimeEnvelope, ValidUnknownUnicodeAndLongTagsKeepCompleteText)
{
	for(const std::string Type : {std::string("新事件😀"), std::string(2048, 'x')})
	{
		SQmRealtimeMessage Message;
		ASSERT_TRUE(Parse(R"({"type":")" + Type + R"(","data":{"anything":true}})", Message));
		EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::UNKNOWN);
		EXPECT_EQ(Message.m_Type, Type);
		EXPECT_FALSE(Message.m_HasRealtimeData);
	}
}

TEST(QmRealtimeEnvelope, NullSuffixedDataDoesNotProvideStateOrGenericPayload)
{
	for(const char *pType : {"state", "broadcast", "users", "users_sync", "sponsors", "developers", "playtime", "time", "title_profile", "title_status", "error"})
	{
		SCOPED_TRACE(pType);
		SQmRealtimeMessage Message;
		ASSERT_TRUE(Parse(std::string(R"({"type":")") + pType + R"(","data\u0000extra":{"online_users":2,"markdown":"bad","title":"bad"}})", Message));
		EXPECT_FALSE(Message.m_StatePayloadValid);
		EXPECT_FALSE(Message.m_HasOnlineUsers);
		EXPECT_FALSE(Message.m_HasBroadcast);
		EXPECT_FALSE(Message.m_HasTitleProfile);
		EXPECT_FALSE(Message.m_HasRealtimeData);
		EXPECT_EQ(Message.m_pPayload, nullptr);
	}
}

TEST(QmRealtimeEnvelope, ExactDataMemberCannotBeShadowed)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"state","data\u0000extra":{"online_users":99},"data":{"online_users":2}})", Message));
	ASSERT_TRUE(Message.m_StatePayloadValid);
	ASSERT_TRUE(Message.m_HasOnlineUsers);
	EXPECT_EQ(Message.m_OnlineUsers, 2);
}

TEST(QmRealtimeEnvelope, StateFieldsUseCompleteNamesAndRetainIntegerClamps)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"state","data":{"online_users\u0000extra":9,"online_dummies\u0000extra":8}})", Message));
	EXPECT_TRUE(Message.m_StatePayloadValid);
	EXPECT_FALSE(Message.m_HasOnlineUsers);
	EXPECT_FALSE(Message.m_HasOnlineDummies);
	ASSERT_TRUE(Parse(R"({"type":"state","data":{"online_users\u0000extra":9,"online_users":-1,"online_dummies":1000001}})", Message));
	EXPECT_TRUE(Message.m_HasOnlineUsers);
	EXPECT_TRUE(Message.m_HasOnlineDummies);
	EXPECT_EQ(Message.m_OnlineUsers, 0);
	EXPECT_EQ(Message.m_OnlineDummies, 1000000);
}

TEST(QmRealtimeEnvelope, GenericTimeAliasesUseOnlyCompleteNames)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"playtime","data":{"server_time\u0000extra":99,"time":4,"playtime_seconds\u0000extra":88,"playtime":5}})", Message));
	EXPECT_TRUE(Message.m_HasServerTime);
	EXPECT_EQ(Message.m_ServerTime, 4);
	EXPECT_TRUE(Message.m_HasPlaytimeSeconds);
	EXPECT_EQ(Message.m_PlaytimeSeconds, 5);
	ASSERT_TRUE(Parse(R"({"type":"time","data":{"server_time\u0000extra":99,"time\u0000extra":4}})", Message));
	EXPECT_FALSE(Message.m_HasServerTime);
	EXPECT_TRUE(Message.m_HasRealtimeData);
}

TEST(QmRealtimeEnvelope, TitlesKeepRootFallbackWithoutAcceptingPrefixedPresenceFields)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"titles","data":{"presences\u0000extra":[],"server_time":1}})", Message));
	EXPECT_FALSE(Message.m_HasTitles);
	EXPECT_TRUE(Message.m_HasRealtimeData);
	ASSERT_TRUE(Parse(R"({"type":"titles","data\u0000extra":{"presences":[]},"presences":[],"server_time":1})", Message));
	EXPECT_TRUE(Message.m_HasTitles);
	EXPECT_FALSE(Message.m_HasRealtimeData);
	EXPECT_NE(Message.m_pTitlePayload, nullptr);
	EXPECT_EQ(Message.m_pPayload, Message.m_pTitlePayload);
	ASSERT_TRUE(Parse(R"({"type":"titles","presences":[],"server_time\u0000extra":1})", Message));
	EXPECT_FALSE(Message.m_HasTitles);
}

TEST(QmRealtimeEnvelope, InvalidProfileTextRejectsEntireEventAndClearsPreviousOutput)
{
	SQmRealtimeMessage Message;
	for(const char *pField : {"title", "bound_name", "style"})
	{
		SCOPED_TRACE(pField);
		for(const std::string Bad : {std::string("text\\u0000extra"), std::string("\x80"), std::string("\xe4\xbd")})
		{
			SCOPED_TRACE(::testing::PrintToString(Bad));
			ASSERT_TRUE(Parse(R"({"type":"title_profile","data":{"title":"old"}})", Message));
			EXPECT_FALSE(Parse(std::string(R"({"type":"title_profile","data":{")") + pField + R"(":")" + Bad + R"(","status":200}})", Message));
			ExpectEmpty(Message);
		}
	}
}

TEST(QmRealtimeEnvelope, ExactProfileTextCannotBeShadowedByUnknownMembers)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"title_profile","data":{"title\u0000extra":"bad","title":"好","bound_name\u0000extra":"bad","bound_name":"名字","style\u0000extra":"bad","style":"gold"}})", Message));
	EXPECT_TRUE(Message.m_HasTitleProfile);
	EXPECT_EQ(Message.m_TitleText, "好");
	EXPECT_EQ(Message.m_TitleBoundName, "名字");
	EXPECT_EQ(Message.m_TitleStyle, "gold");
}

TEST(QmRealtimeEnvelope, ExplicitEmptyProfileTextRemainsPresent)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"title_profile","data":{"title":"","bound_name":"","style":""}})", Message));
	EXPECT_TRUE(Message.m_HasTitleProfile);
	EXPECT_TRUE(Message.m_HasTitleText);
	EXPECT_TRUE(Message.m_HasTitleBoundName);
	EXPECT_TRUE(Message.m_HasTitleStyle);
	EXPECT_TRUE(Message.m_TitleText.empty());
	EXPECT_TRUE(Message.m_TitleBoundName.empty());
	EXPECT_TRUE(Message.m_TitleStyle.empty());
}

TEST(QmRealtimeEnvelope, MissingAndNonStringProfileFieldsKeepExistingAbsencePolicy)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"title_profile","data":{"title":4,"bound_name":null,"style\u0000extra":"bad"}})", Message));
	EXPECT_FALSE(Message.m_HasTitleProfile);
	EXPECT_TRUE(Message.m_HasRealtimeData);
	EXPECT_NE(Message.m_pPayload, nullptr);
}

TEST(QmRealtimeEnvelope, StatusAndAuthenticationFlagsUseOnlyCompleteNames)
{
	SQmRealtimeMessage Message;
	for(const char *pType : {"title_profile", "title_status"})
	{
		SCOPED_TRACE(pType);
		ASSERT_TRUE(Parse(std::string(R"({"type":")") + pType + R"(","data":{"status\u0000extra":200,"authenticated\u0000extra":true}})", Message));
		EXPECT_FALSE(Message.m_HasTitleStatusCode);
		EXPECT_FALSE(Message.m_HasTitleAuthenticated);
	}
	ASSERT_TRUE(Parse(R"({"type":"title_profile","data":{"status\u0000extra":401,"status":200,"authenticated":false}})", Message));
	EXPECT_TRUE(Message.m_HasTitleStatusCode);
	EXPECT_EQ(Message.m_TitleStatusCode, 200);
	EXPECT_TRUE(Message.m_TitleAuthenticated);
}

TEST(QmRealtimeEnvelope, InvalidBroadcastTextRejectsAndClearsPreviousOutput)
{
	SQmRealtimeMessage Message;
	for(const std::string Bad : {std::string("hello\\u0000hidden"), std::string("\x80"), std::string("\xc0\xaf")})
	{
		SCOPED_TRACE(::testing::PrintToString(Bad));
		ASSERT_TRUE(Parse(R"({"type":"broadcast","data":{"markdown":"old"}})", Message));
		EXPECT_FALSE(Parse(R"({"type":"broadcast","data":{"markdown":")" + Bad + R"("}})", Message));
		ExpectEmpty(Message);
	}
}

TEST(QmRealtimeEnvelope, BroadcastMarkdownAndVersionUseCompleteMemberNames)
{
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"broadcast","data":{"markdown\u0000extra":"bad"}})", Message));
	EXPECT_FALSE(Message.m_HasBroadcast);
	ASSERT_TRUE(Parse(R"({"type":"broadcast","data":{"markdown\u0000extra":"bad","markdown":"good","version\u0000extra":9}})", Message));
	EXPECT_TRUE(Message.m_HasBroadcast);
	EXPECT_EQ(Message.m_BroadcastMarkdown, "good");
	EXPECT_EQ(Message.m_BroadcastVersion, 0);
}

TEST(QmRealtimeEnvelope, EmptyUnicodeMultilineAndLimitSizeBroadcastsRemainValid)
{
	for(const std::string Markdown : {std::string(""), std::string("你好😀"), std::string(64 * 1024, 'x')})
	{
		SQmRealtimeMessage Message;
		ASSERT_TRUE(Parse(R"({"type":"broadcast","data":{"markdown":")" + Markdown + R"("}})", Message));
		EXPECT_TRUE(Message.m_HasBroadcast);
		EXPECT_EQ(Message.m_BroadcastMarkdown, Markdown);
	}
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(R"({"type":"broadcast","data":{"markdown":"一\n二\t三"}})", Message));
	EXPECT_TRUE(Message.m_HasBroadcast);
	EXPECT_EQ(Message.m_BroadcastMarkdown, "一\n二\t三");
}

TEST(QmRealtimeEnvelope, OversizedBroadcastKeepsExistingIgnorePolicyBeforeTextValidation)
{
	SQmRealtimeMessage Message;
	const std::string Markdown = std::string(64 * 1024, 'x') + "\\u0000\x80";
	ASSERT_TRUE(Parse(R"({"type":"broadcast","data":{"markdown":")" + Markdown + R"("}})", Message));
	EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::BROADCAST);
	EXPECT_FALSE(Message.m_HasBroadcast);
}

TEST(QmRealtimeEnvelope, InvalidEmoticonIdentityRejectsEntireMessage)
{
	for(int Field = 0; Field < 3; ++Field)
	{
		SCOPED_TRACE(Field);
		for(const std::string Bad : {std::string("value\\u0000extra"), std::string("\x80"), std::string("\xed\xa0\x80")})
		{
			SCOPED_TRACE(::testing::PrintToString(Bad));
			const std::string Identity = std::string(R"("client_id":")") + (Field == 0 ? Bad : "anon") + R"(","player_name":")" +
						     (Field == 1 ? Bad : "tee") + R"(","server_address":")" + (Field == 2 ? Bad : "one:8303") + '"';
			SQmRealtimeMessage Message;
			EXPECT_FALSE(Parse(Emoticon(Identity), Message));
			ExpectEmpty(Message);
		}
	}
}

TEST(QmRealtimeEnvelope, RequiredEmoticonMemberNamesMustMatchCompletely)
{
	const char *apNames[] = {"client_id", "player_name", "server_address", "player_id", "emoticon"};
	const char *apValues[] = {"\"anon\"", "\"tee\"", "\"one:8303\"", "3", "5"};
	for(int BadField = 0; BadField < 5; ++BadField)
	{
		SCOPED_TRACE(apNames[BadField]);
		std::string Data = R"({"type":"emoticon","data":{"launch_mode":true)";
		for(int Field = 0; Field < 5; ++Field)
			Data += std::string(",\"") + apNames[Field] + (Field == BadField ? "\\u0000extra" : "") + "\":" + apValues[Field];
		Data += "}}";
		SQmRealtimeMessage Message;
		ASSERT_TRUE(Parse(Data, Message));
		EXPECT_EQ(Message.m_Event, EQmRealtimeEvent::EMOTICON);
		EXPECT_FALSE(Message.m_HasEmoticon);
		EXPECT_FALSE(Message.m_HasRealtimeData);
	}
}

TEST(QmRealtimeEnvelope, OptionalEmoticonFlagsAndSequenceIgnorePrefixedMembers)
{
	const std::string Identity = R"("client_id":"anon","player_name":"tee","server_address":"one:8303")";
	SQmRealtimeMessage Message;
	ASSERT_TRUE(Parse(Emoticon(Identity, R"("launch_mode\u0000extra":true,"super_launch\u0000extra":true)"), Message));
	EXPECT_FALSE(Message.m_HasEmoticon);
	ASSERT_TRUE(Parse(Emoticon(Identity, R"("launch_mode":true,"super_launch\u0000extra":true,"sequence\u0000extra":42)"), Message));
	EXPECT_TRUE(Message.m_HasEmoticon);
	EXPECT_TRUE(Message.m_EmoticonLaunch);
	EXPECT_FALSE(Message.m_EmoticonSuperLaunch);
	EXPECT_EQ(Message.m_EmoticonSequence, 0u);
}

TEST(QmRealtimeEnvelope, ValidUnicodeEmoticonOwnsCompleteIdentityAfterInputIsReleased)
{
	SQmRealtimeMessage Message;
	{
		const std::string Data = Emoticon(R"("client_id":"用户😀","player_name":"名字","server_address":"[::1]:8303")");
		ASSERT_TRUE(Parse(Data, Message));
	}
	EXPECT_TRUE(Message.m_HasEmoticon);
	EXPECT_TRUE(Message.m_EmoticonPayloadValid);
	EXPECT_EQ(Message.m_EmoticonClientId, "用户😀");
	EXPECT_EQ(Message.m_EmoticonPlayerName, "名字");
	EXPECT_EQ(Message.m_EmoticonServerAddress, "[::1]:8303");
	EXPECT_EQ(Message.m_PlayerId, 3);
	EXPECT_EQ(Message.m_Emoticon, 5);
	ASSERT_NE(Message.m_pPayload, nullptr);
	EXPECT_STREQ(json_object_get(Message.m_pPayload.get(), "player_name")->u.string.ptr, "名字");
}
