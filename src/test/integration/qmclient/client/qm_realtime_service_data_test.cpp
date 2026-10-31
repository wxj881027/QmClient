#include <game/client/components/qmclient/qm_realtime_service_data.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>

namespace
{
	SQmRealtimeMessage Message(const std::string &Type, const std::string &Fields)
	{
		const std::string Json = R"({"type":")" + Type + R"(","data":{)" + Fields + "}}";
		SQmRealtimeMessage Result;
		EXPECT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Result));
		return Result;
	}
}

TEST(QmRealtimeServiceData, PrefixedNumericMembersDoNotSupplyNativeStatistics)
{
	const auto Input = Message("playtime", R"("ts\u0000extra":9,"total_seconds\u0000extra":8,"last_start_at\u0000extra":7)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_FALSE(Data.m_HasServerTime);
	EXPECT_FALSE(Data.m_HasPlaytimeSeconds);
	EXPECT_FALSE(Data.m_HasSessionStart);
}

TEST(QmRealtimeServiceData, ExactNumericMembersCannotBeShadowed)
{
	const auto Input = Message("playtime", R"("ts\u0000extra":99,"ts":4,"total_seconds\u0000extra":88,"total_seconds":5,"last_start_at\u0000extra":77,"last_start_at":6)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_TRUE(Data.m_HasServerTime);
	EXPECT_EQ(Data.m_ServerTime, 4);
	EXPECT_TRUE(Data.m_HasPlaytimeSeconds);
	EXPECT_EQ(Data.m_PlaytimeSeconds, 5);
	EXPECT_TRUE(Data.m_HasSessionStart);
	EXPECT_EQ(Data.m_SessionStart, 6);
}

TEST(QmRealtimeServiceData, ZeroTotalIsPresentWhileZeroTimestampsStayAbsent)
{
	const auto Input = Message("playtime", R"("ts":0,"total_seconds":0,"last_start_at":0)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_FALSE(Data.m_HasServerTime);
	EXPECT_TRUE(Data.m_HasPlaytimeSeconds);
	EXPECT_EQ(Data.m_PlaytimeSeconds, 0);
	EXPECT_FALSE(Data.m_HasSessionStart);
}

TEST(QmRealtimeServiceData, FractionsKeepOriginalTruncationAndPositiveTimestampChecks)
{
	const auto Input = Message("playtime", R"("ts":1234.75,"total_seconds":12.9,"last_start_at":0.75)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_TRUE(Data.m_HasServerTime);
	EXPECT_EQ(Data.m_ServerTime, 1234);
	EXPECT_TRUE(Data.m_HasPlaytimeSeconds);
	EXPECT_EQ(Data.m_PlaytimeSeconds, 12);
	EXPECT_FALSE(Data.m_HasSessionStart);
}

TEST(QmRealtimeServiceData, InvalidNumericValuesRemainAbsent)
{
	for(const char *pValue : {"-1", "-0.25", "9223372036854775808.0", "true", "\"1\"", "null", "[]", "{}"})
	{
		SCOPED_TRACE(pValue);
		const auto Input = Message("playtime", std::string(R"("ts":)") + pValue + R"(,"total_seconds":)" + pValue + R"(,"last_start_at":)" + pValue);
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_FALSE(Data.m_HasServerTime);
		EXPECT_FALSE(Data.m_HasPlaytimeSeconds);
		EXPECT_FALSE(Data.m_HasSessionStart);
	}
}

TEST(QmRealtimeServiceData, MaximumIntegerStatisticsRetainTheirFullRange)
{
	const auto Input = Message("playtime", R"("ts":9223372036854775807,"total_seconds":9223372036854775807,"last_start_at":9223372036854775807)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_TRUE(Data.m_HasServerTime);
	EXPECT_EQ(Data.m_ServerTime, std::numeric_limits<int64_t>::max());
	EXPECT_TRUE(Data.m_HasPlaytimeSeconds);
	EXPECT_EQ(Data.m_PlaytimeSeconds, std::numeric_limits<int64_t>::max());
	EXPECT_TRUE(Data.m_HasSessionStart);
	EXPECT_EQ(Data.m_SessionStart, std::numeric_limits<int64_t>::max());
}

TEST(QmRealtimeServiceData, ParserAliasesDoNotBecomeNewNativeStatisticsFields)
{
	const auto Input = Message("playtime", R"("server_time":20,"time":30,"playtime_seconds":40,"playtime":50)");
	ASSERT_TRUE(Input.m_HasServerTime);
	ASSERT_TRUE(Input.m_HasPlaytimeSeconds);
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_FALSE(Data.m_HasServerTime);
	EXPECT_FALSE(Data.m_HasPlaytimeSeconds);
	EXPECT_FALSE(Data.m_HasSessionStart);
}

TEST(QmRealtimeServiceData, InvalidActionTextCannotBecomeStartOrStop)
{
	for(const std::string Action : {std::string("start\\u0000extra"), std::string("stop\\u0000extra"), std::string("\x80"), std::string("start\\n"), std::string("")})
	{
		SCOPED_TRACE(::testing::PrintToString(Action));
		const auto Input = Message("playtime", R"("total_seconds":5,"action":")" + Action + '"');
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_EQ(Data.m_PlaytimeAction, EQmRealtimePlaytimeAction::NONE);
		EXPECT_TRUE(Data.m_HasPlaytimeSeconds);
		EXPECT_EQ(Data.m_PlaytimeSeconds, 5);
	}
}

TEST(QmRealtimeServiceData, ActionMemberMustMatchCompletelyAndCannotShadowExactMember)
{
	const auto Missing = Message("playtime", R"("action\u0000extra":"stop")");
	EXPECT_EQ(ReadQmRealtimeServiceData(Missing).m_PlaytimeAction, EQmRealtimePlaytimeAction::NONE);
	const auto Exact = Message("playtime", R"("action\u0000extra":"stop","action":"start")");
	EXPECT_EQ(ReadQmRealtimeServiceData(Exact).m_PlaytimeAction, EQmRealtimePlaytimeAction::START);
}

TEST(QmRealtimeServiceData, ActionsRetainCaseSensitiveMatching)
{
	for(const auto &[Action, Expected] : {std::pair{"start", EQmRealtimePlaytimeAction::START}, std::pair{"stop", EQmRealtimePlaytimeAction::STOP}, std::pair{"START", EQmRealtimePlaytimeAction::NONE}, std::pair{"resume", EQmRealtimePlaytimeAction::NONE}})
	{
		SCOPED_TRACE(Action);
		const auto Input = Message("playtime", std::string(R"("action":")") + Action + '"');
		EXPECT_EQ(ReadQmRealtimeServiceData(Input).m_PlaytimeAction, Expected);
	}
}

TEST(QmRealtimeServiceData, EventKindsKeepTheirExistingConsumptionBoundaries)
{
	for(const char *pType : {"time", "sponsors", "title_profile", "title_status"})
	{
		SCOPED_TRACE(pType);
		const auto Input = Message(pType, R"("ts":4,"total_seconds":5,"last_start_at":6,"action":"stop")");
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_EQ(Data.m_HasServerTime, std::string(pType) == "time");
		EXPECT_FALSE(Data.m_HasPlaytimeSeconds);
		EXPECT_FALSE(Data.m_HasSessionStart);
		EXPECT_EQ(Data.m_PlaytimeAction, EQmRealtimePlaytimeAction::NONE);
	}
}

TEST(QmRealtimeServiceData, ProfileAuthenticationStillRequiresInteger200AndValidTitle)
{
	for(const auto &[Status, Expected] : {std::pair{"200", true}, std::pair{"200.0", false}, std::pair{"401", false}, std::pair{"true", false}, std::pair{"\"200\"", false}, std::pair{"null", false}})
	{
		SCOPED_TRACE(Status);
		const auto Input = Message("title_profile", std::string(R"("title":"Title","authenticated":true,"status":)") + Status);
		EXPECT_EQ(ReadQmRealtimeServiceData(Input).m_TitleAuthenticated, Expected);
	}
}

TEST(QmRealtimeServiceData, UnknownStatusCannotAuthenticateOrShadowAnExactStatus)
{
	const auto Missing = Message("title_profile", R"("title":"Title","status\u0000extra":200,"authenticated":true)");
	EXPECT_FALSE(ReadQmRealtimeServiceData(Missing).m_TitleAuthenticated);
	const auto Exact = Message("title_profile", R"("title":"Title","status\u0000extra":401,"status":200)");
	EXPECT_TRUE(ReadQmRealtimeServiceData(Exact).m_TitleAuthenticated);
}

TEST(QmRealtimeServiceData, NativeProfileTextUsesExactMembersInsteadOfUnknownPrefixes)
{
	const auto Input = Message("title_profile", R"("title\u0000extra":"Bad","title":"好名字","bound_name\u0000extra":"Bad","bound_name":"玩家😀","style\u0000extra":"Bad","style":"gold","status":200)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_STREQ(Data.m_pTitle, "好名字");
	EXPECT_STREQ(Data.m_pBoundName, "玩家😀");
	EXPECT_STREQ(Data.m_pStyle, "gold");
	EXPECT_TRUE(Data.m_TitleAuthenticated);
}

TEST(QmRealtimeServiceData, MissingProfileFieldsKeepWholeSnapshotDefaults)
{
	const auto Input = Message("title_profile", R"("status":200,"title\u0000extra":"Bad","bound_name\u0000extra":"Bad","style\u0000extra":"Bad")");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_STREQ(Data.m_pTitle, "");
	EXPECT_STREQ(Data.m_pBoundName, "");
	EXPECT_STREQ(Data.m_pStyle, "");
	EXPECT_FALSE(Data.m_TitleAuthenticated);
	EXPECT_FALSE(Data.m_ChatStyleSupported);
	EXPECT_FALSE(Data.m_AppearanceSupported);
}

TEST(QmRealtimeServiceData, TitleValidationKeepsExistingWidthAndControlCharacterRules)
{
	for(const char *pTitle : {"", "[bad]", "1234567890123", "bad\\ntext"})
	{
		SCOPED_TRACE(pTitle);
		const auto Input = Message("title_profile", std::string(R"("status":200,"title":")") + pTitle + '"');
		EXPECT_FALSE(ReadQmRealtimeServiceData(Input).m_TitleAuthenticated);
	}
	const auto Input = Message("title_profile", R"("status":200,"title":"123456789012")");
	EXPECT_TRUE(ReadQmRealtimeServiceData(Input).m_TitleAuthenticated);
}

TEST(QmRealtimeServiceData, InvalidOptionalChatStyleDoesNotDiscardValidProfile)
{
	for(const std::string Style : {std::string("soft_glow\\u0000extra"), std::string("platinum\\u0000extra"), std::string("\x80")})
	{
		SCOPED_TRACE(::testing::PrintToString(Style));
		const auto Input = Message("title_profile", R"("status":200,"title":"Title","chat_style":")" + Style + '"');
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_TRUE(Data.m_TitleAuthenticated);
		EXPECT_FALSE(Data.m_ChatStyleSupported);
		EXPECT_EQ(Data.m_ChatStyle, EQmSponsorChatStyle::NONE);
	}
}

TEST(QmRealtimeServiceData, ChatStyleUsesExactMemberAndKeepsValidUnknownOrEmptySupport)
{
	for(const auto &[Style, Expected] : {std::pair{"soft_glow", EQmSponsorChatStyle::SOFT_GLOW}, std::pair{"platinum", EQmSponsorChatStyle::PLATINUM}, std::pair{"future", EQmSponsorChatStyle::NONE}, std::pair{"", EQmSponsorChatStyle::NONE}})
	{
		SCOPED_TRACE(Style);
		const auto Input = Message("title_profile", std::string(R"("title":"Title","status":200,"chat_style\u0000extra":"soft_glow","chat_style":")") + Style + '"');
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_TRUE(Data.m_ChatStyleSupported);
		EXPECT_EQ(Data.m_ChatStyle, Expected);
	}
}

TEST(QmRealtimeServiceData, UnknownAppearanceMemberDoesNotSupplyProfileAppearance)
{
	const auto Input = Message("title_profile", R"("title":"Title","status":200,"appearance\u0000extra":{"style":"sun","color":0,"opacity":0})");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_TRUE(Data.m_TitleAuthenticated);
	EXPECT_FALSE(Data.m_AppearanceSupported);
	EXPECT_EQ(Data.m_Appearance, SQmTitleAppearance{});
}

TEST(QmRealtimeServiceData, NestedAppearanceRequiredMembersMustMatchCompletely)
{
	const char *apNames[] = {"style", "color", "opacity"};
	const char *apValues[] = {"\"sun\"", "0", "0"};
	for(int BadField = 0; BadField < 3; ++BadField)
	{
		SCOPED_TRACE(apNames[BadField]);
		std::string Fields = R"("title":"Title","status":200,"appearance":{)";
		for(int Field = 0; Field < 3; ++Field)
			Fields += std::string(Field == 0 ? "\"" : ",\"") + apNames[Field] + (Field == BadField ? "\\u0000extra" : "") + "\":" + apValues[Field];
		Fields += '}';
		const auto Input = Message("title_profile", Fields);
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_TRUE(Data.m_TitleAuthenticated);
		EXPECT_FALSE(Data.m_AppearanceSupported);
		EXPECT_EQ(Data.m_Appearance, SQmTitleAppearance{});
	}
}

TEST(QmRealtimeServiceData, ExactAppearanceCannotBeShadowedAndPreservesZeroValues)
{
	const auto Input = Message("title_profile", R"("title":"Title","status":200,"appearance\u0000extra":{},"appearance":{"style\u0000extra":"moon","style":"sun","color\u0000extra":9,"color":0,"opacity\u0000extra":99,"opacity":0})");
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_TRUE(Data.m_AppearanceSupported);
	EXPECT_EQ(Data.m_Appearance, (SQmTitleAppearance{EQmTitleBadgeStyle::SUN, 0, 0}));
}

TEST(QmRealtimeServiceData, InvalidAppearanceStyleFallsBackWithoutLosingValidTitle)
{
	for(const std::string Style : {std::string("sun\\u0000extra"), std::string("moon\\u0000extra"), std::string("\x80")})
	{
		SCOPED_TRACE(::testing::PrintToString(Style));
		const auto Input = Message("title_profile", R"("title":"Title","status":200,"appearance":{"style":")" + Style + R"(","color":0,"opacity":0})");
		const auto Data = ReadQmRealtimeServiceData(Input);
		EXPECT_TRUE(Data.m_TitleAuthenticated);
		EXPECT_FALSE(Data.m_AppearanceSupported);
		EXPECT_EQ(Data.m_Appearance, SQmTitleAppearance{});
	}
}

TEST(QmRealtimeServiceData, AddressLimitStatusKeepsExactMemberTypeAndEventRules)
{
	for(const char *pFields : {R"("status\u0000extra":409)", R"("status":409.0)", R"("status":"409")"})
	{
		SCOPED_TRACE(pFields);
		const auto Input = Message("title_status", pFields);
		EXPECT_FALSE(ReadQmRealtimeServiceData(Input).m_TitleAddressLimitReached);
	}
	const auto Exact = Message("title_status", R"("status\u0000extra":200,"status":409)");
	EXPECT_TRUE(ReadQmRealtimeServiceData(Exact).m_TitleAddressLimitReached);
	const auto Profile = Message("title_profile", R"("status":409)");
	EXPECT_FALSE(ReadQmRealtimeServiceData(Profile).m_TitleAddressLimitReached);
}

TEST(QmRealtimeServiceData, BorrowedProfileTextRemainsValidWhileMovedPayloadOwnerLives)
{
	auto Input = Message("title_profile", R"("title":"Title","bound_name":"玩家😀","style":"gold","status":200)");
	const auto Data = ReadQmRealtimeServiceData(Input);
	const auto Owner = std::move(Input);
	ASSERT_NE(Owner.m_pPayload, nullptr);
	EXPECT_STREQ(Data.m_pTitle, "Title");
	EXPECT_STREQ(Data.m_pBoundName, "玩家😀");
	EXPECT_STREQ(Data.m_pStyle, "gold");
}

TEST(QmRealtimeServiceData, PingWithoutPayloadProducesNoServiceChanges)
{
	SQmRealtimeMessage Input;
	const std::string Json = R"({"type":"ping"})";
	ASSERT_TRUE(ParseQmRealtimeMessage(Json.data(), Json.size(), Input));
	ASSERT_EQ(Input.m_pPayload, nullptr);
	const auto Data = ReadQmRealtimeServiceData(Input);
	EXPECT_FALSE(Data.m_HasServerTime);
	EXPECT_FALSE(Data.m_HasPlaytimeSeconds);
	EXPECT_FALSE(Data.m_HasSessionStart);
	EXPECT_FALSE(Data.m_TitleAuthenticated);
	EXPECT_FALSE(Data.m_TitleAddressLimitReached);
	EXPECT_EQ(Data.m_PlaytimeAction, EQmRealtimePlaytimeAction::NONE);
}
