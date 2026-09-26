#include <game/client/components/qmclient/hud_notifications/hud_notification_catalog.h>
#include <game/client/components/qmclient/hud_notifications/hud_notification_rules.h>

#include <gtest/gtest.h>

TEST(QmHudNotifications, MatchesKnownSoloPrompts)
{
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("You are now in a solo part"), QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("You are now out of the solo part"), QmHudNotifications::ESoloPrompt::Leave);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("你现在处于单人区域"), QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("你现在已离开单人区域"), QmHudNotifications::ESoloPrompt::Leave);
	EXPECT_EQ(QmHudNotifications::MatchKnownSoloPrompt("regular server message"), QmHudNotifications::ESoloPrompt::None);
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
