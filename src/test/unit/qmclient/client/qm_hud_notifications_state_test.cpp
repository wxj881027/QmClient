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
	class CScopedNotificationConfig
	{
		CConfig m_Saved = g_Config;

	public:
		~CScopedNotificationConfig() { g_Config = m_Saved; }
	};

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
	EXPECT_TRUE(QmHudNotifications::ShouldSuppressServerMessageChat(Analysis));
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
	const CScopedNotificationConfig ConfigScope;
	// 此场景需要同时观察两条通知，容量不依赖其他 suite 的配置。
	g_Config.m_QmHudNotificationsMaxVisible = 4;
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

TEST(QmHudNotifications, ServerMessagesRetainOnlyTheLatestEightNotifications)
{
	const CScopedNotificationConfig ConfigScope;
	g_Config.m_QmHudNotificationsMaxVisible = 8;
	g_Config.m_QmStreamerFilterWords = 0;
	CTestHudNotifications Notifications;
	for(int i = 0; i < 10; ++i)
	{
		char aText[32];
		str_format(aText, sizeof(aText), "regular server message %d", i);
		ASSERT_TRUE(Notifications.HandleServerChat(aText, true));
	}
	EXPECT_EQ(Notifications.NotificationCountForTests(), 8);
	EXPECT_STREQ(Notifications.LastNotificationTextForTests(), "regular server message 9");
}

TEST(QmHudNotifications, RepeatingTheNewestServerMessageAtCapacityStillCollapses)
{
	const CScopedNotificationConfig ConfigScope;
	g_Config.m_QmHudNotificationsMaxVisible = 1;
	g_Config.m_QmStreamerFilterWords = 0;
	CTestHudNotifications Notifications;
	ASSERT_TRUE(Notifications.HandleServerChat("first server message", true));
	ASSERT_TRUE(Notifications.HandleServerChat("second server message", true));
	ASSERT_TRUE(Notifications.HandleServerChat("second server message", true));
	EXPECT_EQ(Notifications.NotificationCountForTests(), 1);
	EXPECT_EQ(Notifications.LastNotificationRepeatCountForTests(), 2);
}

TEST(QmHudNotifications, HiddenServerCategoryReturnsNotHandledWithoutAddingANotification)
{
	const CScopedNotificationConfig ConfigScope;
	g_Config.m_QmStreamerFilterWords = 0;
	CTestHudNotifications Notifications;
	QmHudNotifications::SServerMessageRouteConfig Route;
	Route.m_RouteSystemMessages = true;
	Route.m_ShowUnknown = false;
	QmHudNotifications::SServerMessageAnalysis Analysis;
	EXPECT_FALSE(Notifications.HandleServerChat("ordinary unknown server message", Route, &Analysis));
	EXPECT_EQ(Notifications.NotificationCountForTests(), 0);
	EXPECT_EQ(Analysis.m_Domain, QmHudNotifications::EServerMessageDomain::Unknown);
}

TEST(QmHudNotifications, NonQueuedAnalysisStillExpiresThePendingCompatibilityPrompt)
{
	const CScopedNotificationConfig ConfigScope;
	g_Config.m_QmHudNotificationsCompatSolo = 1;
	CTestHudNotifications Notifications;
	Notifications.SetPendingCompatPromptForTests(QmHudNotifications::ESoloPrompt::Enter, 0);
	EXPECT_FALSE(Notifications.HandleServerChat("ordinary server message", false));
	EXPECT_EQ(Notifications.PendingCompatPromptForTests(), QmHudNotifications::ESoloPrompt::None);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 0);
}

TEST(QmHudNotifications, EmptyMessageDoesNotReuseThePreviousAnalysis)
{
	const CScopedNotificationConfig ConfigScope;
	g_Config.m_QmStreamerFilterWords = 0;
	CTestHudNotifications Notifications;
	QmHudNotifications::SServerMessageAnalysis Analysis;
	ASSERT_TRUE(Notifications.HandleServerChat("ordinary server message", true, &Analysis));
	EXPECT_FALSE(Notifications.HandleServerChat("", true, &Analysis));
	EXPECT_EQ(Analysis.m_Route, QmHudNotifications::EServerMessageRoute::None);
	EXPECT_EQ(Analysis.m_Class, QmHudNotifications::EServerMessageClass::None);
	EXPECT_EQ(Notifications.NotificationCountForTests(), 1);
}
