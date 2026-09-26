#include <game/client/components/qmclient/hud_notifications/hud_notifications.h>

#include <gtest/gtest.h>

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
} // namespace

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
