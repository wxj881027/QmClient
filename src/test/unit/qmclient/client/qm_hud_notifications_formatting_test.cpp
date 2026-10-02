#include <base/color.h>

#include <game/client/components/qmclient/hud_notifications/hud_notifications.h>

#include <gtest/gtest.h>

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

TEST(QmHudNotifications, LocalizesServerChatWithoutChangingRawMessageFallbacks)
{
	char aBuf[256];

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("Team save already in progress", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Team save already in progress");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("队伍存档已在进行中", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Team save already in progress");

	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedServerChatMessage("regular server message", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");

	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedServerChatMessage("'Alice' performed an unknown action", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");
}

TEST(QmHudNotifications, LocalizesChineseServerChatToItsCanonicalKey)
{
	char aBuf[256];

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("你已经死亡，但会继续保持练习模式，直到你输入 kill。", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "You died, but will stay in practice until you use kill.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("没有可返回的位置。", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "There is nowhere to go back to.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("无效的 X 坐标。", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Invalid X coordinate.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("服务器踢人/观战投票已不再由管理员主动监管。", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Server kick/spec votes are no longer actively moderated.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("队伍功能已禁用", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Teams are disabled");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("目标玩家不在你的队伍里", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Player is on a different team");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("计时器不会显示。", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Timer isn't displayed.");

	EXPECT_TRUE(QmHudNotifications::TryFormatLocalizedServerChatMessage("未找到该玩家", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "Player not found");

	// canonical 归一表命中但当前没有可用译文时，必须保持原文，
	// 不能把中文服务端消息显示成英文 canonical。
	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedServerChatMessage("投票通过", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");

	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedServerChatMessage("------- 队伍前 5 名 -------", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");

	// 未登记的中文服务端消息仍走原文回退，不得被当作翻译 key。
	str_copy(aBuf, "sentinel", sizeof(aBuf));
	EXPECT_FALSE(QmHudNotifications::TryFormatLocalizedServerChatMessage("这是一条未登记的服务端消息", aBuf, sizeof(aBuf)));
	EXPECT_STREQ(aBuf, "");
}

TEST(QmHudNotifications, StaticEnglishAndChineseMessagesShareTheSameSemanticKey)
{
	const auto English = QmHudNotifications::AnalyzeServerMessage("Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee ", QmHudNotifications::ESoloPrompt::None);
	const auto Chinese = QmHudNotifications::AnalyzeServerMessage("你的超时保护码已设置。0.7 客户端在超时后无法重新认领自己的 tee；不过 0.6 客户端可以认领你的 tee ", QmHudNotifications::ESoloPrompt::None);

	EXPECT_EQ(English.m_MessageKey, Chinese.m_MessageKey);
	EXPECT_STREQ(English.m_aLocalizedText, Chinese.m_aLocalizedText);
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
