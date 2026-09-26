#include <game/client/components/qmclient/hud_notifications/hud_notification_catalog.h>
#include <game/client/components/qmclient/hud_notifications/hud_notification_rules.h>
#include <game/client/components/qmclient/hud_notifications/hud_notification_static_rules.h>

#ifdef QM_HUD_NOTIFICATION_STATIC_RULES
#error Old mixed static rule table should not be exposed through hud_notification_rules.h
#endif

#include <base/system.h>

#include <gtest/gtest.h>

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

TEST(QmHudNotificationRules, BasicInfoIsNotQueuedWhenItsCategoryIsDisabled)
{
	const auto Analysis = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 18.9", QmHudNotifications::ESoloPrompt::None);
	// 区间删掉了「按隐藏标志吞消息」的旁路，只剩分类开关决定是否入列。
	const auto Decision = QmHudNotifications::DecideServerMessageEntry(Analysis, true);
	EXPECT_FALSE(Decision.m_QueueNotification);
	EXPECT_FALSE(Decision.m_ClearPendingCompatPrompt);
	EXPECT_FALSE(Decision.m_UseFallbackNotification);
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
	EXPECT_EQ(Prompt.m_Route, QmHudNotifications::EServerMessageRoute::System);
	// 抑制只看分析结果：系统路由消息保持可见。
	EXPECT_FALSE(QmHudNotifications::ShouldSuppressServerMessageChat(Prompt));

	const auto BasicInfo = QmHudNotifications::AnalyzeServerMessage("DDraceNetwork Version: 20.0", QmHudNotifications::ESoloPrompt::None);
	EXPECT_FALSE(QmHudNotifications::ShouldSuppressServerMessageChat(BasicInfo));

	// 单人路由消息被抑制（区间把「按隐藏标志抑制」改成只按分析结果判定）。
	const auto Solo = QmHudNotifications::AnalyzeServerMessage("You are now in a solo part", QmHudNotifications::ESoloPrompt::Enter);
	EXPECT_EQ(Solo.m_Route, QmHudNotifications::EServerMessageRoute::Solo);
	EXPECT_TRUE(QmHudNotifications::ShouldSuppressServerMessageChat(Solo));
}
