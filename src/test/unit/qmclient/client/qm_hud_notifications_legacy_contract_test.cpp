#include <base/str.h>

#include <game/client/components/qmclient/hud_notifications/hud_notification_static_rules.h>

#include <gtest/gtest.h>

namespace
{
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

}
TEST(QmHudNotificationContract, LegacyStaticCompatibilityLayerExcludesMigratedSemanticStatics)
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
