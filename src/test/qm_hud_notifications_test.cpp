// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/hud_editor.h>
#include <game/client/components/qmclient/hud_notifications/hud_notification_catalog.h>
#include <game/client/components/qmclient/hud_notifications/hud_notification_rules.h>
#include <game/client/components/qmclient/hud_notifications/hud_notification_static_rules.h>
#include <game/client/components/qmclient/hud_notifications/hud_notifications.h>

#ifdef QM_HUD_NOTIFICATION_STATIC_RULES
#error Old mixed static rule table should not be exposed through hud_notification_rules.h
#endif

#include <base/color.h>

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

void CComponentInterfaces::OnInterfacesInit(CGameClient *pClient)
{
}

void CQmHudNotifications::OnReset()
{
	m_vNotifications.clear();
	m_HasLastSolo = false;
	m_LastSolo = false;
	m_PendingCompatPrompt = QmHudNotifications::ESoloPrompt::None;
	m_PendingCompatUntil = 0;
}

void CQmHudNotifications::OnRelease()
{
	OnReset();
}

void CQmHudNotifications::OnNewSnapshot()
{
}

void CQmHudNotifications::OnRender()
{
}

namespace
{
	class CTestHudNotifications final : public CQmHudNotifications
	{
	public:
	};

	bool HasLegacyStaticCompatibilityLiteral(const char *pNeedle)
	{
		struct SLiteralPair
		{
			const char *m_pOriginal;
			const char *m_pLocalized;
		};
		static const SLiteralPair s_aLegacyLiterals[] = {
#define QM_LEGACY_LITERAL(pOriginal, pLocalized) {pOriginal, pLocalized},
			QM_HUD_NOTIFICATION_STATIC_TEAM_RULES(QM_LEGACY_LITERAL)
				QM_HUD_NOTIFICATION_STATIC_SWAP_RESCUE_RULES(QM_LEGACY_LITERAL)
					QM_HUD_NOTIFICATION_STATIC_VOTE_MODERATION_RULES(QM_LEGACY_LITERAL)
						QM_HUD_NOTIFICATION_STATIC_STATUS_RULES(QM_LEGACY_LITERAL)
#undef QM_LEGACY_LITERAL
		};
		for(const SLiteralPair &Literal : s_aLegacyLiterals)
		{
			if(str_comp(Literal.m_pOriginal, pNeedle) == 0 || str_comp(Literal.m_pLocalized, pNeedle) == 0)
				return true;
		}
		return false;
	}

	std::string ReadHudNotificationTestFile(const char *pPath)
	{
		std::ifstream File(std::string(DDNET_TEST_SOURCE_DIR) + "/" + pPath, std::ios::binary);
		EXPECT_TRUE(File.good()) << pPath;
		std::ostringstream Buffer;
		Buffer << File.rdbuf();
		return Buffer.str();
	}

	std::string FunctionBodySource(const std::string &Source, const std::string &Signature)
	{
		const size_t Start = Source.find(Signature);
		if(Start == std::string::npos)
			return {};
		const size_t OpenBrace = Source.find('{', Start);
		if(OpenBrace == std::string::npos)
			return {};
		int Depth = 0;
		for(size_t i = OpenBrace; i < Source.size(); ++i)
		{
			if(Source[i] == '{')
				++Depth;
			else if(Source[i] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(Start, i - Start + 1);
			}
		}
		return {};
	}
} // namespace

TEST(QmHudNotifications, MatchesKnownSoloPrompts)
{
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("You are now in a solo part"), QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("You are now out of the solo part"), QmHudNotifications::ESoloPrompt::Leave);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("你现在处于单人区域"), QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("你现在已离开单人区域"), QmHudNotifications::ESoloPrompt::Leave);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("regular server message"), QmHudNotifications::ESoloPrompt::None);
}

TEST(QmHudNotifications, HudEdgeMarginHelperOffsetsAnchoredRects)
{
	const CUIRect Rect{10.0f, 20.0f, 100.0f, 40.0f};
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform(8.0f);

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(Rect, Margin, true, false, true, false);
	EXPECT_FLOAT_EQ(LeftTop.x, 18.0f);
	EXPECT_FLOAT_EQ(LeftTop.y, 28.0f);
	EXPECT_FLOAT_EQ(LeftTop.w, Rect.w);
	EXPECT_FLOAT_EQ(LeftTop.h, Rect.h);

	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(Rect, Margin, false, true, false, true);
	EXPECT_FLOAT_EQ(RightBottom.x, 2.0f);
	EXPECT_FLOAT_EQ(RightBottom.y, 12.0f);
	EXPECT_FLOAT_EQ(RightBottom.w, Rect.w);
	EXPECT_FLOAT_EQ(RightBottom.h, Rect.h);
}

TEST(QmHudNotifications, ChatEdgeBaseRectPreservesLegacyDefaultAndSupportsRightEdge)
{
	const CUIRect Legacy = QmHudEditor::ChatEdgeBaseRect(600.0f, 200.0f, 0.0f, false);
	EXPECT_FLOAT_EQ(Legacy.x, 0.0f);
	EXPECT_FLOAT_EQ(Legacy.y, 50.0f);
	EXPECT_FLOAT_EQ(Legacy.w, 232.0f);
	EXPECT_FLOAT_EQ(Legacy.h, 250.0f);

	const CUIRect Right = QmHudEditor::ChatEdgeBaseRect(600.0f, 200.0f, 8.0f, true);
	EXPECT_FLOAT_EQ(Right.x, 600.0f - 232.0f - 8.0f);
	EXPECT_FLOAT_EQ(Right.y, 50.0f);
	EXPECT_FLOAT_EQ(Right.w, 232.0f);
	EXPECT_FLOAT_EQ(Right.h, 250.0f);
}

TEST(QmHudNotifications, HudEditorEdgeAnchoringUsesDragSnapDistance)
{
	const std::string Source = ReadHudNotificationTestFile("src/game/client/components/hud_editor.cpp");

	EXPECT_NE(Source.find("HUD_EDITOR_EDGE_ANCHOR_DISTANCE = QmHudEditor::SNAP_DISTANCE"), std::string::npos);
	EXPECT_EQ(Source.find("HUD_EDITOR_EDGE_ANCHOR_DISTANCE = QmHudEditor::EPSILON"), std::string::npos);
}

TEST(QmHudNotifications, CatalogProvidesCanonicalTextAndMetadata)
{
	using namespace QmHudNotifications;

	const auto *pMeta = FindMessageMetadata(EMessageKey::WhispersOn);
	ASSERT_NE(pMeta, nullptr);
	EXPECT_EQ(FindMessageMetadata(EMessageKey::Count), nullptr);
	EXPECT_EQ(pMeta->m_Domain, EServerMessageDomain::Status);
	EXPECT_EQ(pMeta->m_Class, EServerMessageClass::Prompt);
	EXPECT_STREQ(CanonicalMessageText(EMessageKey::WhispersOn), "You will receive whispers");
	EXPECT_STREQ(CanonicalMessageText(EMessageKey::TeamSaveInProgress), "Team save already in progress");
}

TEST(QmHudNotifications, SuppressesOnlyMatchedSoloChatMessages)
{
	EXPECT_TRUE(QmHudNotifications::ShouldSuppressSoloChatMessage("You are now in a solo part", QmHudNotifications::ESoloPrompt::None));
	EXPECT_TRUE(QmHudNotifications::ShouldSuppressSoloChatMessage("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter));
	EXPECT_FALSE(QmHudNotifications::ShouldSuppressSoloChatMessage("regular server message", QmHudNotifications::ESoloPrompt::Enter));
	EXPECT_FALSE(QmHudNotifications::ShouldSuppressSoloChatMessage("You are now out of the solo part", QmHudNotifications::ESoloPrompt::Enter));
}

TEST(QmHudNotifications, RoutesServerSystemMessagesWhenEnabled)
{
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("regular server message", QmHudNotifications::ESoloPrompt::None, false), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("regular server message", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("Team save already in progress", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("队伍存档已在进行中", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter, true), QmHudNotifications::EServerMessageRoute::Solo);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter, false), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("You are now in a solo part", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::Solo);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("DDraceNetwork 版本: 18.9", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("请访问 DDNet.org，或输入 /info，并确保阅读 /rules", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("'nameless tee' entered and joined the game", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("'nameless tee' joined the game", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("'nameless tee' has left the game", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("'nameless tee' has left the game (Disconnected)", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageRoute(nullptr, QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::None);
}

TEST(QmHudNotifications, ClassifiesServerSystemMessages)
{
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("DDraceNetwork 版本: 18.9", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("请访问 DDNet.org，或输入 /info，并确保阅读 /rules", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("Available practice commands: /rescue /lasttp /telecursor", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::HelpInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("Example: /map adr3 to call vote for Adrenaline 3. This means that the map name must start with 'a' and contain the characters 'd', 'r' and '3' in that order", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::HelpInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("'nameless tee' joined the game", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("'nameless tee' has left the game", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("'nameless tee' has left the game (Disconnected)", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("请友善交流。", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("未设置服务器规则，请联系管理员。", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("Team save already in progress", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("队伍存档已在进行中", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter), QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::None);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass(nullptr, QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::None);
}

TEST(QmHudNotifications, KeepsShortSystemFeedbackOutOfBlacklist)
{
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("Players are not allowed to chat from VPNs at this time"));
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("You can see other players. To disable this use DDNet client and type /showothers"));
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("Unknown emote... Say /emote"));
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee "));
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee "));

	EXPECT_EQ(QmHudNotifications::ServerMessageRoute("Players are not allowed to chat from VPNs at this time", QmHudNotifications::ESoloPrompt::None, true), QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(QmHudNotifications::ServerMessageClass("Players are not allowed to chat from VPNs at this time", QmHudNotifications::ESoloPrompt::None), QmHudNotifications::EServerMessageClass::Prompt);
}

TEST(QmHudNotifications, ExcludesHelpAndExampleMessagesFromNotifications)
{
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("Available practice commands: /rescue /lasttp /telecursor"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("可用练习命令：/rescue /lasttp /telecursor"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("Available rescue modes: auto, manual"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("Example: /map adr3 to call vote for Adrenaline 3. This means that the map name must start with 'a' and contain the characters 'd', 'r' and '3' in that order"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("See /practicecmdlist for a list of all available practice commands. Most commonly used ones are /telecursor, /lasttp and /rescue"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("可用表情命令：/emote surprise /emote blink /emote close /emote angry /emote happy /emote pain /emote normal"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("'nameless tee' has left the game"));
	EXPECT_TRUE(QmHudNotifications::ShouldExcludeSystemNotification("'nameless tee' has left the game (Disconnected)"));
}

TEST(QmHudNotifications, FormatsKnownSystemNotifications)
{
	char aBuf[256];

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("Players are not allowed to chat from VPNs at this time", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Players are not allowed to chat from VPNs at this time");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("Unknown argument. Check '/rescuemode list'", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Unknown argument. Check '/rescuemode list'");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("未知救援模式参数", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Unknown argument. Check '/rescuemode list'");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("Team save already in progress", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Team save already in progress");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("队伍存档已在进行中", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Team save already in progress");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee ", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee ", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("你现在会收到私聊消息", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "You will receive whispers");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("你现在可以看到本服所有 tee，不受距离限制", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "You will now see all tees on this server, no matter the distance");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器未开启救援功能，而你所在的队伍也没有开启 /practice。注意：练习模式下无法获得排名。", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Rescue is not enabled on this server and you're not in a team with /practice turned on. Note that you can't earn a rank with practice enabled.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("未知表情。输入 /emote 查看帮助", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Unknown emote. Use /emote to see available emotes.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器允许组队；队伍上锁后，队内任意玩家死亡都会导致全队死亡", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Teams are available on this server; if the team is locked, any team member dying will kill the whole team");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器允许玩家碰撞", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Players can collide on this server");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器允许玩家互钩", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Players can hook each other on this server");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器的成绩是私密的", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Scores are private on this server");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器不允许查看全局积分排行榜", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Showing the global top points is not allowed on this server.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("本服务器不允许查看 checkpoint 时间", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Showing the checkpoint times is not allowed on this server.");

	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedNotificationMessage("regular server message", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");

	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedNotificationMessage("", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");

	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedNotificationMessage(nullptr, aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");
}

TEST(QmHudNotificationRules, AnalyzesSoloMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::Solo);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Solo);
	EXPECT_EQ(Analysis.m_SoloPrompt, QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You are now in a solo part");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesBasicInfoMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 18.9", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "");
	EXPECT_TRUE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesStaticTeamMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("Team save already in progress", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Team save already in progress");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, StaticMessageKeyPropagatesForCatalogMessages)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("You will receive whispers", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_MessageKey, QmHudNotifications::EMessageKey::WhispersOn);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You will receive whispers");

	Analysis = QmHudNotifications::AnalyzeServerMessage("Team save already in progress", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_MessageKey, QmHudNotifications::EMessageKey::TeamSaveInProgress);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
}

TEST(QmHudNotifications, StaticEnglishAndChineseMessagesShareTheSameSemanticKey)
{
	const auto English = QmHudNotifications::AnalyzeServerMessage("Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee ", QmHudNotifications::ESoloPrompt::None);
	const auto Chinese = QmHudNotifications::AnalyzeServerMessage("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee ", QmHudNotifications::ESoloPrompt::None);

	EXPECT_EQ(English.m_MessageKey, Chinese.m_MessageKey);
	EXPECT_STREQ(English.m_aLocalizedText, Chinese.m_aLocalizedText);
}

TEST(QmHudNotifications, RepeatedNotificationsAnimateOnlyTheCounter)
{
	const std::string Header = ReadHudNotificationTestFile("src/game/client/components/qmclient/hud_notifications/hud_notifications.h");
	EXPECT_NE(Header.find("int64_t m_RepeatAnimTime = 0;"), std::string::npos);
	EXPECT_NE(Header.find("Last.m_RepeatAnimTime = time_get();"), std::string::npos);
	EXPECT_EQ(Header.find("Last.m_StartTime = time_get();"), std::string::npos);
	EXPECT_NE(Header.find("str_format(Last.m_aRepeatText, sizeof(Last.m_aRepeatText), \"x%d\", Last.m_RepeatCount);"), std::string::npos);

	const std::string Source = ReadHudNotificationTestFile("src/game/client/components/qmclient/hud_notifications/hud_notifications.cpp");
	const std::string RenderNotifications = FunctionBodySource(Source, "void CQmHudNotifications::RenderNotifications(");
	ASSERT_FALSE(RenderNotifications.empty());
	EXPECT_NE(RenderNotifications.find("RepeatScale = QmHudNotifications::RepeatCountElasticScale"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("Notification.m_aRepeatText"), std::string::npos);
	EXPECT_NE(RenderNotifications.find("TextRender()->Text(RepeatX, Box.y + PaddingY, FontSize * RepeatScale"), std::string::npos);
}

TEST(QmHudNotifications, TimeoutCodeSetRequiresTheExplicitSemanticAliasText)
{
	char aBuf[256];
	str_copy(aBuf, "sentinel", sizeof(aBuf));

	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedNotificationMessage("你的超时保护码已设置。并非 0.7/0.6 reclaim 提示的其他文本", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");

	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("你的超时保护码已设置。并非 0.7/0.6 reclaim 提示的其他文本", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_MessageKey, QmHudNotifications::EMessageKey::None);
	EXPECT_TRUE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotifications, LegacyStaticCompatibilityStillFormatsNonSemanticCategories)
{
	char aBuf[256];

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("You are running a vote, please try again after the vote is done!", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "You are running a vote, please try again after the vote is done!");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedNotificationMessage("Unknown argument. Check '/rescuemode list'", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Unknown argument. Check '/rescuemode list'");
}

TEST(QmHudNotifications, LegacyStaticCompatibilityLayerExcludesMigratedSemanticStatics)
{
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("Team save already in progress"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("队伍存档已在进行中"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("Rescue is not enabled on this server and you're not in a team with /practice turned on. Note that you can't earn a rank with practice enabled."));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("本服务器未开启救援功能，而你所在的队伍也没有开启 /practice。注意：练习模式下无法获得排名。"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("You will now see all tees on this server, no matter the distance"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("你现在可以看到本服所有 tee，不受距离限制"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("You will no longer see all tees on this server"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("你将不再看到本服所有 tee"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("You will receive whispers"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("你现在会收到私聊消息"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("You will not receive any further whispers"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("你将不再收到私聊消息"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("Unknown emote... Say /emote"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("未知表情。输入 /emote 查看帮助"));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee "));
	EXPECT_FALSE(HasLegacyStaticCompatibilityLiteral("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee "));

	EXPECT_TRUE(HasLegacyStaticCompatibilityLiteral("You are running a vote, please try again after the vote is done!"));
	EXPECT_TRUE(HasLegacyStaticCompatibilityLiteral("你正在发起投票，请等当前投票结束后再试"));
	EXPECT_TRUE(HasLegacyStaticCompatibilityLiteral("Unknown argument. Check '/rescuemode list'"));
	EXPECT_TRUE(HasLegacyStaticCompatibilityLiteral("未知救援模式参数"));
}

TEST(QmHudNotifications, SemanticMetadataDrivesStaticAndDynamicRoutingClassificationAndBlacklist)
{
	const auto *pStaticMeta = QmHudNotifications::FindMessageMetadata(QmHudNotifications::EMessageKey::UnknownEmote);
	ASSERT_NE(pStaticMeta, nullptr);
	EXPECT_FALSE(pStaticMeta->m_ExcludeFromNotifications);

	const auto StaticEnglish = QmHudNotifications::AnalyzeServerMessage("Unknown emote... Say /emote", QmHudNotifications::ESoloPrompt::None);
	const auto StaticChinese = QmHudNotifications::AnalyzeServerMessage("未知表情。输入 /emote 查看帮助", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(StaticEnglish.m_Route, pStaticMeta->m_Route);
	EXPECT_EQ(StaticEnglish.m_Class, pStaticMeta->m_Class);
	EXPECT_EQ(StaticEnglish.m_Domain, pStaticMeta->m_Domain);
	EXPECT_EQ(StaticChinese.m_Route, pStaticMeta->m_Route);
	EXPECT_EQ(StaticChinese.m_Class, pStaticMeta->m_Class);
	EXPECT_EQ(StaticChinese.m_Domain, pStaticMeta->m_Domain);
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("Unknown emote... Say /emote"));
	EXPECT_FALSE(QmHudNotifications::ShouldExcludeSystemNotification("未知表情。输入 /emote 查看帮助"));
	EXPECT_EQ(QmHudNotifications::ShouldExcludeSystemNotification("Unknown emote... Say /emote"), pStaticMeta->m_ExcludeFromNotifications);
	EXPECT_EQ(QmHudNotifications::ShouldExcludeSystemNotification("未知表情。输入 /emote 查看帮助"), pStaticMeta->m_ExcludeFromNotifications);

	const auto *pTeamJoinedMeta = QmHudNotifications::FindMessageMetadata(QmHudNotifications::EDynamicMessageKey::TeamJoined);
	ASSERT_NE(pTeamJoinedMeta, nullptr);
	const auto TeamJoinedEnglish = QmHudNotifications::AnalyzeServerMessage("'Alpha' joined team 5", QmHudNotifications::ESoloPrompt::None);
	const auto TeamJoinedChinese = QmHudNotifications::AnalyzeServerMessage("'Alpha' 加入了 5 队", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(TeamJoinedEnglish.m_Route, pTeamJoinedMeta->m_Route);
	EXPECT_EQ(TeamJoinedEnglish.m_Class, pTeamJoinedMeta->m_Class);
	EXPECT_EQ(TeamJoinedEnglish.m_Domain, pTeamJoinedMeta->m_Domain);
	EXPECT_EQ(TeamJoinedEnglish.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::TeamJoined);
	EXPECT_EQ(TeamJoinedChinese.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::TeamJoined);
	EXPECT_FALSE(pTeamJoinedMeta->m_ExcludeFromNotifications);
	EXPECT_EQ(QmHudNotifications::ShouldExcludeSystemNotification("'Alpha' joined team 5"), pTeamJoinedMeta->m_ExcludeFromNotifications);
	EXPECT_EQ(QmHudNotifications::ShouldExcludeSystemNotification("'Alpha' 加入了 5 队"), pTeamJoinedMeta->m_ExcludeFromNotifications);

	const auto *pSwapMeta = QmHudNotifications::FindMessageMetadata(QmHudNotifications::EDynamicMessageKey::SwapRequestSent);
	ASSERT_NE(pSwapMeta, nullptr);
	const auto SwapEnglish = QmHudNotifications::AnalyzeServerMessage("You have requested to swap with Beta. Use /cancelswap to cancel the request.", QmHudNotifications::ESoloPrompt::None);
	const auto SwapChinese = QmHudNotifications::AnalyzeServerMessage("你已向 Beta 发出交换请求。输入 /cancelswap 可取消", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(SwapEnglish.m_Route, pSwapMeta->m_Route);
	EXPECT_EQ(SwapEnglish.m_Class, pSwapMeta->m_Class);
	EXPECT_EQ(SwapEnglish.m_Domain, pSwapMeta->m_Domain);
	EXPECT_EQ(SwapEnglish.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::SwapRequestSent);
	EXPECT_EQ(SwapChinese.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::SwapRequestSent);
	EXPECT_FALSE(pSwapMeta->m_ExcludeFromNotifications);
	EXPECT_EQ(QmHudNotifications::ShouldExcludeSystemNotification("You have requested to swap with Beta. Use /cancelswap to cancel the request."), pSwapMeta->m_ExcludeFromNotifications);
	EXPECT_EQ(QmHudNotifications::ShouldExcludeSystemNotification("你已向 Beta 发出交换请求。输入 /cancelswap 可取消"), pSwapMeta->m_ExcludeFromNotifications);
}

TEST(QmHudNotificationRules, AnalyzesStaticTeamMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("队伍存档已在进行中", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Team save already in progress");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesUpdatedChineseTeamValidationMessages)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("这个队伍已经开始比赛了", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "This team started already");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("你死亡或处于旁观状态时，不能切换队伍。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You can't change teams while you are dead/a spectator.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("你已经使用过练习模式了", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You have used practice mode already");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("这个队伍当前正在存档", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "This team is currently saving");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesUpdatedChineseSettingsMessages)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器允许组队；队伍上锁后，队内任意玩家死亡都会导致全队死亡", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Teams are available on this server; if the team is locked, any team member dying will kill the whole team");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器允许玩家碰撞", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Players can collide on this server");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器允许玩家互钩", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Players can hook each other on this server");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器的成绩是私密的", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Scores are private on this server");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesTeamDynamicMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' joined team 5", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_EQ(Analysis.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::TeamJoined);
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamA, "Alpha");
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamB, "5");
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' joined team 5");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesTeamDynamicMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 加入了 5 队", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_EQ(Analysis.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::TeamJoined);
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamA, "Alpha");
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamB, "5");
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' joined team 5");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesLockTeamMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 锁定了你们的队伍", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' locked your team.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesUnlockTeamMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 解锁了你们的队伍", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' unlocked your team.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesInviteMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 邀请你加入 5 队。输入 /team 5 即可加入。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' invited you to team 5. Use /team 5 to join");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesTeamAnnouncementInviteMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 邀请了 'Beta' 加入你们的队伍。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' invited 'Beta' to your team.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesTeam0ModeMessagesInChinese)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 关闭了 team 0 模式。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' disabled team 0 mode.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("'Alpha' 开启了 team 0 模式。你们的队伍现在会按 team 0 规则运作。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Team);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alpha' enabled team 0 mode. This will make your team behave like team 0.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}
TEST(QmHudNotificationRules, AnalyzesSwapDynamicMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("You have requested to swap with Beta. Use /cancelswap to cancel the request.", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::SwapRescue);
	EXPECT_EQ(Analysis.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::SwapRequestSent);
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamA, "Beta");
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You have requested to swap with Beta. Use /cancelswap to cancel the request.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesSwapDynamicMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("你已向 Beta 发出交换请求。输入 /cancelswap 可取消", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::SwapRescue);
	EXPECT_EQ(Analysis.m_DynamicSemantic.m_Key, QmHudNotifications::EDynamicMessageKey::SwapRequestSent);
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamA, "Beta");
	EXPECT_STREQ(Analysis.m_DynamicSemantic.m_aParamB, "");
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You have requested to swap with Beta. Use /cancelswap to cancel the request.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesStaticSwapRescueMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("Unknown argument. Check '/rescuemode list'", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::SwapRescue);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Unknown argument. Check '/rescuemode list'");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesStaticSwapRescueMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("未知救援模式参数", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::SwapRescue);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Unknown argument. Check '/rescuemode list'");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesWhisperToggleMessagesInChinese)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("你现在会收到私聊消息", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You will receive whispers");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("你将不再收到私聊消息", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You will not receive any further whispers");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesShowAllMessagesInChinese)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("你现在可以看到本服所有 tee，不受距离限制", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You will now see all tees on this server, no matter the distance");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("你将不再看到本服所有 tee", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You will no longer see all tees on this server");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesRescueDisabledMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器未开启救援功能，而你所在的队伍也没有开启 /practice。注意：练习模式下无法获得排名。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::SwapRescue);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Rescue is not enabled on this server and you're not in a team with /practice turned on. Note that you can't earn a rank with practice enabled.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesUnknownEmoteMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("未知表情。输入 /emote 查看帮助", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Unknown emote. Use /emote to see available emotes.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesTimeoutCodeMessageInChinese)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee ", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesHideScoreMessagesInChinese)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器不允许查看全局积分排行榜", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Showing the global top points is not allowed on this server.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("本服务器不允许查看 checkpoint 时间", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Showing the checkpoint times is not allowed on this server.");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesTimerAndRaceTimeMessagesInChinese)
{
	auto Analysis = QmHudNotifications::AnalyzeServerMessage("计时器显示在 广播。", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Timer is displayed in 广播。");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);

	Analysis = QmHudNotifications::AnalyzeServerMessage("你的当前用时是 01:23", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Your current race time is 01:23");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesVoteDynamicMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("'Alice' called vote to kick 'Bob' (afk)", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::VoteModeration);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "'Alice' called for vote to kick 'Bob' (reason: afk)");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, AnalyzesStaticStatusMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("Players are not allowed to chat from VPNs at this time", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Status);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "Players are not allowed to chat from VPNs at this time");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, SimplifiedChineseTranslationsUsePlainPromptText)
{
	const std::string Translations = ReadHudNotificationTestFile("qmclient_scripts/languages_qmclient/translations/i18n/qmclient.toml");

	EXPECT_NE(Translations.find("key = \"Scoreboard point check\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"计分板积分检查\""), std::string::npos);
	EXPECT_NE(Translations.find("key = \"Team can't be saved while a dragger is active\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"有拖拽器生效时不能保存队伍存档\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"本服务器不允许查看队伍前 5 名\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"你现在可以用锤子攻击其他玩家\""), std::string::npos);
	EXPECT_NE(Translations.find("simplified_chinese = \"你现在不能用锤子攻击其他玩家\""), std::string::npos);
}

TEST(QmHudNotificationRules, AnalyzesStaticVoteModerationMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("You are running a vote, please try again after the vote is done!", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::VoteModeration);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "You are running a vote, please try again after the vote is done!");
	EXPECT_FALSE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, FallsBackForUnknownMessage)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("regular server message", QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::System);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Unknown);
	EXPECT_STREQ(Analysis.m_aLocalizedText, "");
	EXPECT_TRUE(Analysis.m_UseFallbackLocalization);
}

TEST(QmHudNotificationRules, DoesNotQueueWhenSystemRouteIsDisabled)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("Team save already in progress", QmHudNotifications::ESoloPrompt::None);
	const auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, false);
	EXPECT_FALSE(Decision.m_QueueNotification);
	EXPECT_FALSE(Decision.m_ClearPendingCompatPrompt);
	EXPECT_FALSE(Decision.m_UseFallbackNotification);
}

TEST(QmHudNotificationRules, KeepsUnknownFallbackNotificationWhenSystemRouteIsEnabled)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("regular server message", QmHudNotifications::ESoloPrompt::None);
	const auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, true);
	EXPECT_TRUE(Decision.m_QueueNotification);
	EXPECT_FALSE(Decision.m_ClearPendingCompatPrompt);
	EXPECT_TRUE(Decision.m_UseFallbackNotification);
}

TEST(QmHudNotificationRules, CategoryFiltersKeepCurrentDefaultBehavior)
{
	QmHudNotifications::SServerMessageRouteConfig Config;
	Config.m_RouteSystemMessages = true;

	auto Analysis = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 18.9", QmHudNotifications::ESoloPrompt::None);
	auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_FALSE(Decision.m_QueueNotification);

	Analysis = QmHudNotifications::AnalyzeServerMessage("Available practice commands: /rescue /lasttp /telecursor", QmHudNotifications::ESoloPrompt::None);
	Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::HelpInfo);
	EXPECT_FALSE(Decision.m_QueueNotification);

	Analysis = QmHudNotifications::AnalyzeServerMessage("Team save already in progress", QmHudNotifications::ESoloPrompt::None);
	Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_TRUE(Decision.m_QueueNotification);

	Analysis = QmHudNotifications::AnalyzeServerMessage("regular server message", QmHudNotifications::ESoloPrompt::None);
	Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Unknown);
	EXPECT_TRUE(Decision.m_QueueNotification);
	EXPECT_TRUE(Decision.m_UseFallbackNotification);
}

TEST(QmHudNotificationRules, CategoryFiltersCanEnableBasicAndHelpMessages)
{
	QmHudNotifications::SServerMessageRouteConfig Config;
	Config.m_RouteSystemMessages = true;
	Config.m_ShowBasicInfo = true;
	Config.m_ShowHelpInfo = true;

	auto Analysis = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 18.9", QmHudNotifications::ESoloPrompt::None);
	auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_TRUE(Decision.m_QueueNotification);
	EXPECT_TRUE(Decision.m_UseFallbackNotification);

	Analysis = QmHudNotifications::AnalyzeServerMessage("Available practice commands: /rescue /lasttp /telecursor", QmHudNotifications::ESoloPrompt::None);
	Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_TRUE(Decision.m_QueueNotification);
	EXPECT_TRUE(Decision.m_UseFallbackNotification);
}

TEST(QmHudNotificationRules, CategoryFiltersCanDisablePromptAndUnknownMessages)
{
	QmHudNotifications::SServerMessageRouteConfig Config;
	Config.m_RouteSystemMessages = true;
	Config.m_ShowPrompts = false;
	Config.m_ShowUnknown = false;

	auto Analysis = QmHudNotifications::AnalyzeServerMessage("Team save already in progress", QmHudNotifications::ESoloPrompt::None);
	auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_FALSE(Decision.m_QueueNotification);

	Analysis = QmHudNotifications::AnalyzeServerMessage("regular server message", QmHudNotifications::ESoloPrompt::None);
	Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_FALSE(Decision.m_QueueNotification);
}

TEST(QmHudNotificationRules, DisabledCategoryFiltersRouteNonEmptySystemMessages)
{
	QmHudNotifications::SServerMessageRouteConfig Config;
	Config.m_RouteSystemMessages = true;
	Config.m_UseCategoryFilters = false;

	auto Analysis = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 18.9", QmHudNotifications::ESoloPrompt::None);
	auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_TRUE(Decision.m_QueueNotification);
	EXPECT_TRUE(Decision.m_UseFallbackNotification);

	Analysis = QmHudNotifications::AnalyzeServerMessage("Available practice commands: /rescue /lasttp /telecursor", QmHudNotifications::ESoloPrompt::None);
	Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, Config);
	EXPECT_TRUE(Decision.m_QueueNotification);
	EXPECT_TRUE(Decision.m_UseFallbackNotification);
}

TEST(QmHudNotificationRules, QueuedSystemNotificationsRemainVisibleInChat)
{
	const auto Prompt = QmHudNotifications::AnalyzeServerMessage("Welcome to DDraceNetwork!", QmHudNotifications::ESoloPrompt::None);
	EXPECT_FALSE(QmHudNotifications::ShouldSuppressServerMessageChat(Prompt));

	const auto BasicInfo = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 20.0", QmHudNotifications::ESoloPrompt::None);
	EXPECT_FALSE(QmHudNotifications::ShouldSuppressServerMessageChat(BasicInfo));

	const auto Solo = QmHudNotifications::AnalyzeServerMessage("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_TRUE(QmHudNotifications::ShouldSuppressServerMessageChat(Solo));
}

TEST(QmHudNotifications, HandleServerChatUsesFallbackNotificationForUnknownMessage)
{
	CTestHudNotifications Notifications;
	QmHudNotifications::SServerMessageAnalysis Analysis;
	EXPECT_TRUE(Notifications.HandleServerChat("regular server message", true, &Analysis));
	EXPECT_TRUE(Analysis.m_UseFallbackLocalization);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 1);
	EXPECT_STREQ(Notifications.LastNotificationTextForTests(), "regular server message");
}

TEST(QmHudNotifications, ConsecutiveIdenticalSystemNotificationsCollapseIntoRepeatCount)
{
	CTestHudNotifications Notifications;
	QmHudNotifications::SServerMessageAnalysis Analysis;

	EXPECT_TRUE(Notifications.HandleServerChat("Team save already in progress", true, &Analysis));
	EXPECT_TRUE(Notifications.HandleServerChat("Team save already in progress", true, &Analysis));
	EXPECT_TRUE(Notifications.HandleServerChat("Team save already in progress", true, &Analysis));

	EXPECT_EQ(Notifications.NotificationCountForTests(), 1);
	EXPECT_STREQ(Notifications.LastNotificationTextForTests(), "Team save already in progress");
	EXPECT_EQ(Notifications.LastNotificationRepeatCountForTests(), 3);
	EXPECT_STREQ(Notifications.LastNotificationRepeatTextForTests(), "x3");
}

TEST(QmHudNotifications, HandleServerChatRespectsDisabledSystemRoute)
{
	CTestHudNotifications Notifications;
	QmHudNotifications::SServerMessageAnalysis Analysis;
	EXPECT_FALSE(Notifications.HandleServerChat("Team save already in progress", false, &Analysis));
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::Prompt);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 0);
}

TEST(QmHudNotifications, HandleServerChatLeavesBasicInfoInChat)
{
	CTestHudNotifications Notifications;
	QmHudNotifications::SServerMessageAnalysis Analysis;
	EXPECT_FALSE(Notifications.HandleServerChat("DDraceNetwork Version: 18.9", true, &Analysis));
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::BasicInfo);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 0);
}

TEST(QmHudNotifications, HandleServerChatClearsPendingCompatAfterQueuedSoloPrompt)
{
	CTestHudNotifications Notifications;
	Notifications.SetPendingCompatPromptForTests(QmHudNotifications::ESoloPrompt::Enter, time_get() + time_freq());

	QmHudNotifications::SServerMessageAnalysis EnterAnalysis;
	EXPECT_TRUE(Notifications.HandleServerChat("You are now in a solo part", true, &EnterAnalysis));
	EXPECT_EQ(EnterAnalysis.m_Route, QmHudNotifications::EServerMessageRoute::Solo);
	EXPECT_EQ(Notifications.PendingCompatPromptForTests(), QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 1);

	QmHudNotifications::SServerMessageAnalysis FollowupAnalysis;
	EXPECT_TRUE(Notifications.HandleServerChat("You are now out of the solo part", true, &FollowupAnalysis));
	EXPECT_EQ(FollowupAnalysis.m_Route, QmHudNotifications::EServerMessageRoute::Solo);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 2);
	EXPECT_STREQ(Notifications.LastNotificationTextForTests(), "You are now out of the solo part");
}

TEST(QmHudNotifications, BuildsEchoPresentationFromQueuedMessage)
{
	constexpr unsigned FallbackEchoColor = 0x445566;
	const auto PlainEcho = QmHudNotifications::BuildEchoNotificationPayload("Regular echo", FallbackEchoColor);
	EXPECT_STREQ(PlainEcho.m_aText, "Regular echo");
	EXPECT_EQ(PlainEcho.m_Color, FallbackEchoColor);

	const auto ColoredEcho = QmHudNotifications::BuildEchoNotificationPayload("[[$FF7F7F]]Gores: 开启", FallbackEchoColor);
	const unsigned ExpectedColor = color_cast<ColorHSLA>(ColorRGBA(1.0f, 127.0f / 255.0f, 127.0f / 255.0f, 1.0f)).Pack(false);
	EXPECT_STREQ(ColoredEcho.m_aText, "Gores: 开启");
	EXPECT_EQ(ColoredEcho.m_Color, ExpectedColor);

	const auto EmptyEcho = QmHudNotifications::BuildEchoNotificationPayload(nullptr, FallbackEchoColor);
	EXPECT_STREQ(EmptyEcho.m_aText, "");
	EXPECT_EQ(EmptyEcho.m_Color, FallbackEchoColor);
}

TEST(QmHudNotifications, ClampsVisibleCount)
{
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(-1), 1);
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(0), 1);
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(3), 3);
	EXPECT_EQ(QmHudNotifications::ClampVisibleCount(20), 8);
}

TEST(QmHudNotifications, ClampsTiming)
{
	EXPECT_EQ(QmHudNotifications::ClampHoldMs(200), 500);
	EXPECT_EQ(QmHudNotifications::ClampHoldMs(2500), 2500);
	EXPECT_EQ(QmHudNotifications::ClampHoldMs(30000), 10000);
	EXPECT_EQ(QmHudNotifications::ClampAnimationMs(0), 0);
	EXPECT_EQ(QmHudNotifications::ClampAnimationMs(9000), 2000);
}

TEST(QmHudNotifications, ClampsTextSize)
{
	EXPECT_EQ(QmHudNotifications::ClampTextSize(0), 1);
	EXPECT_EQ(QmHudNotifications::ClampTextSize(8), 8);
	EXPECT_EQ(QmHudNotifications::ClampTextSize(40), 24);
}

TEST(QmHudNotifications, ScalesSmallTextChrome)
{
	EXPECT_FLOAT_EQ(QmHudNotifications::SmallTextScale(1.0f), 0.33f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingX(1.0f), 1.32f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingY(1.0f), 0.825f);
	EXPECT_FLOAT_EQ(QmHudNotifications::MinBoxWidth(1.0f), 27.06f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingX(8.0f), 4.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::PaddingY(8.0f), 2.5f);
	EXPECT_FLOAT_EQ(QmHudNotifications::MinBoxWidth(8.0f), 82.0f);
}

TEST(QmHudNotifications, SelectsTextColorByNotificationKind)
{
	constexpr unsigned SystemColor = 0x111111;
	constexpr unsigned EchoOverrideColor = 0xFF222222;
	constexpr unsigned ChatEchoColor = 0x333333;

	const QmHudNotifications::STextColorConfig System = QmHudNotifications::TextColorConfig(QmHudNotifications::ETextSource::System, 1, SystemColor, EchoOverrideColor, ChatEchoColor);
	EXPECT_EQ(System.m_Color, SystemColor);
	EXPECT_TRUE(System.m_HasAlpha);

	const QmHudNotifications::STextColorConfig EchoInherited = QmHudNotifications::TextColorConfig(QmHudNotifications::ETextSource::Echo, 1, SystemColor, EchoOverrideColor, ChatEchoColor);
	EXPECT_EQ(EchoInherited.m_Color, ChatEchoColor);
	EXPECT_FALSE(EchoInherited.m_HasAlpha);

	const QmHudNotifications::STextColorConfig EchoOverride = QmHudNotifications::TextColorConfig(QmHudNotifications::ETextSource::Echo, 0, SystemColor, EchoOverrideColor, ChatEchoColor);
	EXPECT_EQ(EchoOverride.m_Color, EchoOverrideColor);
	EXPECT_TRUE(EchoOverride.m_HasAlpha);
}

TEST(QmHudNotificationsGeometry, ResolvesHorizontalFlowFromAnchorsAndScreenSide)
{
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(true, false, 240.0f, 200.0f), QmHudNotifications::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(false, true, 120.0f, 200.0f), QmHudNotifications::EHorizontalFlow::RightToLeft);
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(false, false, 120.0f, 200.0f), QmHudNotifications::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudNotifications::ResolveHorizontalFlow(false, false, 280.0f, 200.0f), QmHudNotifications::EHorizontalFlow::RightToLeft);
}

TEST(QmHudNotificationsGeometry, ComputesVisibleRectFromRealContentWidth)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::LeftToRight);
	EXPECT_FLOAT_EQ(LeftVisible.x, 100.0f);
	EXPECT_FLOAT_EQ(LeftVisible.y, 40.0f);
	EXPECT_FLOAT_EQ(LeftVisible.w, 96.0f);
	EXPECT_FLOAT_EQ(LeftVisible.h, 34.0f);

	const CUIRect RightVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::RightToLeft);
	EXPECT_FLOAT_EQ(RightVisible.x, 176.0f);
	EXPECT_FLOAT_EQ(RightVisible.y, 40.0f);
	EXPECT_FLOAT_EQ(RightVisible.w, 96.0f);
	EXPECT_FLOAT_EQ(RightVisible.h, 34.0f);
}

TEST(QmHudNotificationsGeometry, PlacesBoxesDifferentlyForLeftAndRightFlow)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::LeftToRight, 0.0f), 100.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 0.0f), 212.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::LeftToRight, 12.0f), 88.0f);
	EXPECT_FLOAT_EQ(QmHudNotifications::NotificationBoxX(BaseRect, 60.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 12.0f), 224.0f);
}

TEST(QmHudNotificationsGeometry, ExpandsVisibleRectForSlideAnimation)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::LeftToRight, 32.0f);
	EXPECT_FLOAT_EQ(LeftVisible.x, 68.0f);
	EXPECT_FLOAT_EQ(LeftVisible.w, 128.0f);

	const CUIRect RightVisible = QmHudNotifications::NotificationVisibleRect(BaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 32.0f);
	EXPECT_FLOAT_EQ(RightVisible.x, 176.0f);
	EXPECT_FLOAT_EQ(RightVisible.w, 128.0f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewHeightUsesMaxVisibleStack)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};
	const float BoxHeight = 20.0f;
	const float Gap = 4.0f;

	const CUIRect PreviewRect = QmHudNotifications::EditorPreviewVisibleRect(
		BaseRect, 120.0f, BoxHeight, Gap, 4, QmHudNotifications::EHorizontalFlow::LeftToRight);

	EXPECT_FLOAT_EQ(PreviewRect.x, 100.0f);
	EXPECT_FLOAT_EQ(PreviewRect.w, 120.0f);
	EXPECT_FLOAT_EQ(PreviewRect.h, 92.0f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewWidthStaysStableAcrossHorizontalFlows)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftPreview = QmHudNotifications::EditorPreviewVisibleRect(
		BaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::LeftToRight);
	const CUIRect RightPreview = QmHudNotifications::EditorPreviewVisibleRect(
		BaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::RightToLeft);

	EXPECT_FLOAT_EQ(LeftPreview.w, RightPreview.w);
	EXPECT_FLOAT_EQ(LeftPreview.h, RightPreview.h);
	EXPECT_FLOAT_EQ(LeftPreview.x, 100.0f);
	EXPECT_FLOAT_EQ(RightPreview.x, 144.0f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewDragRectDoesNotShiftAcrossHorizontalFlows)
{
	const CUIRect BaseRect = {100.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftPreview = QmHudNotifications::EditorPreviewDragRect(BaseRect, 128.0f, 20.0f, 4.0f, 3);
	const CUIRect RightPreview = QmHudNotifications::EditorPreviewDragRect(BaseRect, 128.0f, 20.0f, 4.0f, 3);

	EXPECT_FLOAT_EQ(LeftPreview.x, RightPreview.x);
	EXPECT_FLOAT_EQ(LeftPreview.x + LeftPreview.w * 0.5f, RightPreview.x + RightPreview.w * 0.5f);
}

TEST(QmHudNotificationsGeometry, EditorPreviewRenderKeepsNaturalWidthAnchoredToStableRightEdge)
{
	const CUIRect EditorRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const CUIRect RenderBaseRect = QmHudNotifications::EditorPreviewRenderBaseRect(
		EditorRect, 172.0f, QmHudNotifications::EHorizontalFlow::RightToLeft);

	const float EditorBoxWidth = QmHudNotifications::NotificationBoxWidth(RenderBaseRect, 160.0f);
	const float BoxX = QmHudNotifications::NotificationBoxX(RenderBaseRect, EditorBoxWidth, QmHudNotifications::EHorizontalFlow::RightToLeft, 0.0f);

	EXPECT_FLOAT_EQ(EditorBoxWidth, 160.0f);
	EXPECT_FLOAT_EQ(BoxX + EditorBoxWidth, EditorRect.x + EditorRect.w);
}

TEST(QmHudNotificationsGeometry, RuntimeVisibleRectMayExpandWithoutChangingStableAnchor)
{
	const CUIRect AnchorRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const CUIRect RenderBaseRect = QmHudNotifications::EditorPreviewRenderBaseRect(
		AnchorRect, 172.0f, QmHudNotifications::EHorizontalFlow::RightToLeft);
	const CUIRect RuntimeVisibleRect = QmHudNotifications::NotificationVisibleRect(
		RenderBaseRect, 96.0f, 34.0f, QmHudNotifications::EHorizontalFlow::RightToLeft, 32.0f);

	EXPECT_FLOAT_EQ(RuntimeVisibleRect.x, AnchorRect.x + AnchorRect.w - 96.0f);
	EXPECT_FLOAT_EQ(RenderBaseRect.x + RenderBaseRect.w, AnchorRect.x + AnchorRect.w);
	EXPECT_GT(RuntimeVisibleRect.w, 96.0f);
}

TEST(QmHudNotificationsGeometry, EdgeMarginInsetsOnlyAnchoredPreviewEdges)
{
	const CUIRect AnchorRect = {0.0f, 0.0f, 128.0f, 92.0f};
	const CUIRect FreeRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform(8.0f);

	const CUIRect LeftInset = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, true, false, true, false);
	const CUIRect RightInset = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, false, true, false, true);
	const CUIRect FreeInset = QmHudEditor::ApplyEdgeMargin(FreeRect, Margin, false, false, false, false);

	EXPECT_FLOAT_EQ(LeftInset.x, 8.0f);
	EXPECT_FLOAT_EQ(LeftInset.y, 8.0f);
	EXPECT_FLOAT_EQ(LeftInset.w, 128.0f);
	EXPECT_FLOAT_EQ(RightInset.x, -8.0f);
	EXPECT_FLOAT_EQ(RightInset.y, -8.0f);
	EXPECT_FLOAT_EQ(RightInset.w, 128.0f);
	EXPECT_FLOAT_EQ(FreeInset.x, FreeRect.x);
	EXPECT_FLOAT_EQ(FreeInset.y, FreeRect.y);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginMatchesLegacyNotificationInsetAcrossAnchors)
{
	const CUIRect AnchorRect = {0.0f, 0.0f, 128.0f, 92.0f};
	const QmHudEditor::SEdgeMargin Margin = QmHudEditor::SEdgeMargin::Uniform(8.0f);

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, true, false, true, false);
	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, false, true, false, true);

	const CUIRect LegacyLeftTop = QmHudNotifications::InsetAnchoredRect(AnchorRect, 8.0f, true, false, true, false);
	const CUIRect LegacyRightBottom = QmHudNotifications::InsetAnchoredRect(AnchorRect, 8.0f, false, true, false, true);

	EXPECT_FLOAT_EQ(LeftTop.x, LegacyLeftTop.x);
	EXPECT_FLOAT_EQ(LeftTop.y, LegacyLeftTop.y);
	EXPECT_FLOAT_EQ(LeftTop.w, LegacyLeftTop.w);
	EXPECT_FLOAT_EQ(LeftTop.h, LegacyLeftTop.h);
	EXPECT_FLOAT_EQ(RightBottom.x, LegacyRightBottom.x);
	EXPECT_FLOAT_EQ(RightBottom.y, LegacyRightBottom.y);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginHandlesNonUniformMargins)
{
	const CUIRect AnchorRect = {0.0f, 0.0f, 64.0f, 32.0f};
	const QmHudEditor::SEdgeMargin Margin{4.0f, 8.0f, 2.0f, 16.0f};

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, true, false, true, false);
	EXPECT_FLOAT_EQ(LeftTop.x, 4.0f);
	EXPECT_FLOAT_EQ(LeftTop.y, 2.0f);

	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(AnchorRect, Margin, false, true, false, true);
	EXPECT_FLOAT_EQ(RightBottom.x, -8.0f);
	EXPECT_FLOAT_EQ(RightBottom.y, -16.0f);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginZeroIsIdentity)
{
	const CUIRect AnchorRect = {12.0f, 34.0f, 56.0f, 78.0f};
	const QmHudEditor::SEdgeMargin Zero{};

	const CUIRect LeftTop = QmHudEditor::ApplyEdgeMargin(AnchorRect, Zero, true, false, true, false);
	const CUIRect RightBottom = QmHudEditor::ApplyEdgeMargin(AnchorRect, Zero, false, true, false, true);
	const CUIRect Free = QmHudEditor::ApplyEdgeMargin(AnchorRect, Zero, false, false, false, false);

	EXPECT_FLOAT_EQ(LeftTop.x, AnchorRect.x);
	EXPECT_FLOAT_EQ(LeftTop.y, AnchorRect.y);
	EXPECT_FLOAT_EQ(RightBottom.x, AnchorRect.x);
	EXPECT_FLOAT_EQ(RightBottom.y, AnchorRect.y);
	EXPECT_FLOAT_EQ(Free.x, AnchorRect.x);
	EXPECT_FLOAT_EQ(Free.y, AnchorRect.y);
}

TEST(QmHudEditorGeometry, ApplyEdgeMarginIsZeroPredicate)
{
	EXPECT_TRUE((QmHudEditor::SEdgeMargin{}).IsZero());
	EXPECT_FALSE(QmHudEditor::SEdgeMargin::Uniform(1.0f).IsZero());
	EXPECT_FALSE((QmHudEditor::SEdgeMargin{0.0f, 0.0f, 0.0f, 0.5f}).IsZero());
}

TEST(QmHudEditorGeometry, ResolveHorizontalFlowFromVisibleRect)
{
	const float ScreenStartX = 0.0f;
	const float ScreenWidth = 400.0f;
	const CUIRect AnchoredLeft = {0.0f, 0.0f, 120.0f, 80.0f};
	const CUIRect AnchoredRight = {280.0f, 0.0f, 120.0f, 80.0f};
	const CUIRect CenterTilt = {100.0f, 0.0f, 80.0f, 60.0f};
	const CUIRect FarRightTilt = {260.0f, 0.0f, 60.0f, 60.0f};

	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(AnchoredLeft, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(AnchoredRight, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::RightToLeft);
	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(CenterTilt, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::LeftToRight);
	EXPECT_EQ(QmHudEditor::ResolveHorizontalFlow(FarRightTilt, ScreenStartX, ScreenWidth), QmHudEditor::EHorizontalFlow::RightToLeft);
}

TEST(QmHudNotificationsGeometry, EditorPreviewRightFlowBoxMatchesStableRightEdge)
{
	const CUIRect EditorRect = {100.0f, 40.0f, 128.0f, 92.0f};
	const float BoxWidth = QmHudNotifications::NotificationBoxWidth(EditorRect, 180.0f);
	const float BoxX = QmHudNotifications::NotificationBoxX(EditorRect, BoxWidth, QmHudNotifications::EHorizontalFlow::RightToLeft, 0.0f);

	EXPECT_FLOAT_EQ(BoxX, EditorRect.x);
	EXPECT_FLOAT_EQ(BoxX + BoxWidth, EditorRect.x + EditorRect.w);
}

TEST(QmHudEditorGeometry, SnapsOnlyToScreenEdges)
{
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenEdges(101.0f, 40.0f, 0.0f, 300.0f), 101.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenEdges(4.0f, 40.0f, 0.0f, 300.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenEdges(257.0f, 40.0f, 0.0f, 300.0f), 260.0f);
}

TEST(QmHudEditorGeometry, SnapsToScreenCenterGuide)
{
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(101.0f, 40.0f, 0.0f, 300.0f), 101.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(4.0f, 40.0f, 0.0f, 300.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(257.0f, 40.0f, 0.0f, 300.0f), 260.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToScreenGuides(127.0f, 40.0f, 0.0f, 300.0f), 130.0f);
}

TEST(QmHudEditorGeometry, SnapsToOtherModuleAlignmentGuides)
{
	const QmHudEditor::SAxisReference aReferences[] = {
		{40.0f, 60.0f},
	};

	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(44.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 40.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(57.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 55.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(68.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 70.0f);
	EXPECT_FLOAT_EQ(QmHudEditor::SnapAxisToGuides(120.0f, 30.0f, 0.0f, 300.0f, aReferences, 1), 120.0f);
}

TEST(QmHudEditorGeometry, ReportsVisibleSnapGuidePosition)
{
	const QmHudEditor::SAxisReference aReferences[] = {
		{40.0f, 60.0f},
	};

	const QmHudEditor::SSnapAxisResult ScreenCenter = QmHudEditor::SnapAxisToGuidesEx(127.0f, 40.0f, 0.0f, 300.0f, nullptr, 0);
	EXPECT_TRUE(ScreenCenter.m_HasGuide);
	EXPECT_FLOAT_EQ(ScreenCenter.m_Position, 130.0f);
	EXPECT_FLOAT_EQ(ScreenCenter.m_GuidePosition, 150.0f);
	EXPECT_EQ(ScreenCenter.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenCenter);

	const QmHudEditor::SSnapAxisResult ReferenceEnd = QmHudEditor::SnapAxisToGuidesEx(68.0f, 30.0f, 0.0f, 300.0f, aReferences, 1);
	EXPECT_TRUE(ReferenceEnd.m_HasGuide);
	EXPECT_FLOAT_EQ(ReferenceEnd.m_Position, 70.0f);
	EXPECT_FLOAT_EQ(ReferenceEnd.m_GuidePosition, 100.0f);
	EXPECT_EQ(ReferenceEnd.m_GuideKind, QmHudEditor::ESnapGuideKind::ReferenceEnd);

	const QmHudEditor::SSnapAxisResult Free = QmHudEditor::SnapAxisToGuidesEx(120.0f, 30.0f, 0.0f, 300.0f, aReferences, 1);
	EXPECT_FALSE(Free.m_HasGuide);
	EXPECT_FLOAT_EQ(Free.m_Position, 120.0f);
}

TEST(QmHudEditorGeometry, MediaIslandUsesWeakerScreenEdgeSnapOnly)
{
	const QmHudEditor::SAxisReference aReferences[] = {
		{40.0f, 60.0f},
	};

	const QmHudEditor::SSnapAxisResult FreeNearEdge = QmHudEditor::SnapAxisToGuidesEx(3.0f, 40.0f, 0.0f, 300.0f, nullptr, 0, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_FALSE(FreeNearEdge.m_HasGuide);
	EXPECT_FLOAT_EQ(FreeNearEdge.m_Position, 3.0f);

	const QmHudEditor::SSnapAxisResult SnappedEdge = QmHudEditor::SnapAxisToGuidesEx(2.0f, 40.0f, 0.0f, 300.0f, nullptr, 0, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_TRUE(SnappedEdge.m_HasGuide);
	EXPECT_FLOAT_EQ(SnappedEdge.m_Position, 0.0f);
	EXPECT_EQ(SnappedEdge.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenStart);

	const QmHudEditor::SSnapAxisResult ScreenCenter = QmHudEditor::SnapAxisToGuidesEx(127.0f, 40.0f, 0.0f, 300.0f, nullptr, 0, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_FLOAT_EQ(ScreenCenter.m_Position, 130.0f);
	EXPECT_EQ(ScreenCenter.m_GuideKind, QmHudEditor::ESnapGuideKind::ScreenCenter);

	const QmHudEditor::SSnapAxisResult ReferenceStart = QmHudEditor::SnapAxisToGuidesEx(44.0f, 30.0f, 0.0f, 300.0f, aReferences, 1, QmHudEditor::MEDIA_ISLAND_EDGE_SNAP_DISTANCE);
	EXPECT_FLOAT_EQ(ReferenceStart.m_Position, 40.0f);
	EXPECT_EQ(ReferenceStart.m_GuideKind, QmHudEditor::ESnapGuideKind::ReferenceStart);
}

TEST(QmHudEditorGeometry, HudNotificationsUsesStableLayoutToken)
{
	const char *pToken = QmHudEditor::ElementToken(EHudEditorElement::HudNotifications);

	EXPECT_STREQ(pToken, "hud_notifications");
	EXPECT_EQ(QmHudEditor::ElementFromToken(pToken), static_cast<int>(EHudEditorElement::HudNotifications));
	EXPECT_EQ(QmHudEditor::ElementFromToken(""), -1);
}

TEST(QmHudNotificationsGeometry, EditorPreviewCanAnchorToScreenEdges)
{
	const CUIRect LeftBaseRect = {8.0f, 40.0f, 172.0f, 68.0f};
	const CUIRect RightBaseRect = {120.0f, 40.0f, 172.0f, 68.0f};

	const CUIRect LeftPreview = QmHudNotifications::EditorPreviewVisibleRect(
		LeftBaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::LeftToRight);
	const CUIRect RightPreview = QmHudNotifications::EditorPreviewVisibleRect(
		RightBaseRect, 128.0f, 20.0f, 4.0f, 3, QmHudNotifications::EHorizontalFlow::RightToLeft);

	EXPECT_FLOAT_EQ(LeftPreview.x, 8.0f);
	EXPECT_FLOAT_EQ(RightPreview.x + RightPreview.w, 292.0f);
}
