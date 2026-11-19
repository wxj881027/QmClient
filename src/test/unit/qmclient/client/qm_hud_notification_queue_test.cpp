#include <game/client/components/qmclient/hud_notifications/hud_notification_queue.h>

#include <gtest/gtest.h>

namespace
{
	using EKind = QmHudNotifications::ENotificationKind;
	class QmHudNotificationQueue : public ::testing::Test
	{
	protected:
		QmHudNotifications::CNotificationQueue m_Queue;
		bool Add(const char *pText, int Limit = 8, EKind Kind = EKind::System, unsigned Color = 0, bool HasColor = false, int64_t Now = 100)
		{
			return m_Queue.Queue(Kind, pText, Color, HasColor, Limit, Now, [](char *) {});
		}
		void Fill()
		{
			for(int i = 0; i < 8; ++i)
			{
				char aText[16];
				str_format(aText, sizeof(aText), "message%d", i);
				ASSERT_TRUE(Add(aText));
			}
		}
	};
}

TEST_F(QmHudNotificationQueue, EmptyInputDoesNotPublishANotification)
{
	EXPECT_FALSE(Add(nullptr));
	EXPECT_FALSE(Add(""));
	EXPECT_TRUE(m_Queue.empty());
	EXPECT_EQ(m_Queue.size(), 0u);
}

TEST_F(QmHudNotificationQueue, PublishedEntryRetainsColorTimeAndFreshAnimationState)
{
	ASSERT_TRUE(Add("hello", 8, EKind::Echo, 0x123456, true, 77));
	const auto &Entry = m_Queue.back();
	EXPECT_STREQ(Entry.m_aText, "hello");
	EXPECT_EQ(Entry.m_EchoColor, 0x123456u);
	EXPECT_TRUE(Entry.m_HasEchoColor);
	EXPECT_EQ(Entry.m_StartTime, 77);
	EXPECT_EQ(Entry.m_RepeatAnimTime, 0);
	EXPECT_EQ(Entry.m_RepeatCount, 1);
	EXPECT_EQ(Entry.m_VisualY, -1.0f);
}

TEST_F(QmHudNotificationQueue, NewEntriesStayInArrivalOrder)
{
	ASSERT_TRUE(Add("first"));
	ASSERT_TRUE(Add("second"));
	ASSERT_TRUE(Add("third"));
	EXPECT_STREQ(m_Queue.front().m_aText, "first");
	EXPECT_STREQ(m_Queue[1].m_aText, "second");
	EXPECT_STREQ(m_Queue.back().m_aText, "third");
}

TEST_F(QmHudNotificationQueue, NinthEntryEvictsOnlyTheOldestEntry)
{
	ASSERT_NO_FATAL_FAILURE(Fill());
	ASSERT_TRUE(Add("ninth"));
	EXPECT_EQ(m_Queue.size(), 8u);
	EXPECT_STREQ(m_Queue.front().m_aText, "message1");
	EXPECT_STREQ(m_Queue.back().m_aText, "ninth");
}

TEST_F(QmHudNotificationQueue, RepeatedWraparoundPreservesTheNewestEightEntries)
{
	for(int i = 0; i < 20; ++i)
	{
		char aText[16];
		str_format(aText, sizeof(aText), "message%d", i);
		ASSERT_TRUE(Add(aText));
	}
	ASSERT_EQ(m_Queue.size(), 8u);
	EXPECT_STREQ(m_Queue[0].m_aText, "message12");
	EXPECT_STREQ(m_Queue[3].m_aText, "message15");
	EXPECT_STREQ(m_Queue[7].m_aText, "message19");
}

TEST_F(QmHudNotificationQueue, SmallerVisibleLimitKeepsOnlyTheNewestEntries)
{
	ASSERT_TRUE(Add("first", 2));
	ASSERT_TRUE(Add("second", 2));
	ASSERT_TRUE(Add("third", 2));
	EXPECT_EQ(m_Queue.size(), 2u);
	EXPECT_STREQ(m_Queue.front().m_aText, "second");
}

TEST_F(QmHudNotificationQueue, InvalidLowerLimitKeepsOneEntry)
{
	ASSERT_TRUE(Add("first", -1));
	ASSERT_TRUE(Add("second", 0));
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_STREQ(m_Queue.front().m_aText, "second");
}

TEST_F(QmHudNotificationQueue, ExcessiveVisibleLimitStillUsesEightSlots)
{
	ASSERT_NO_FATAL_FAILURE(Fill());
	ASSERT_TRUE(Add("new", std::numeric_limits<int>::max()));
	EXPECT_EQ(m_Queue.size(), 8u);
}

TEST_F(QmHudNotificationQueue, ConsecutiveMatchUpdatesRepeatPulseWithoutRestartingLifetime)
{
	ASSERT_TRUE(Add("same", 8, EKind::System, 0, false, 100));
	ASSERT_TRUE(Add("same", 8, EKind::System, 0, false, 200));
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_EQ(m_Queue.back().m_StartTime, 100);
	EXPECT_EQ(m_Queue.back().m_RepeatAnimTime, 200);
	EXPECT_EQ(m_Queue.back().m_RepeatCount, 2);
	EXPECT_STREQ(m_Queue.back().m_aRepeatText, "x2");
}

TEST_F(QmHudNotificationQueue, DifferentKindsDoNotCollapse)
{
	ASSERT_TRUE(Add("same", 8, EKind::System));
	ASSERT_TRUE(Add("same", 8, EKind::Echo));
	EXPECT_EQ(m_Queue.size(), 2u);
}

TEST_F(QmHudNotificationQueue, DifferentColorsDoNotCollapse)
{
	ASSERT_TRUE(Add("same", 8, EKind::Echo, 1, true));
	ASSERT_TRUE(Add("same", 8, EKind::Echo, 2, true));
	EXPECT_EQ(m_Queue.size(), 2u);
}

TEST_F(QmHudNotificationQueue, DifferentColorPresenceDoesNotCollapse)
{
	ASSERT_TRUE(Add("same", 8, EKind::Echo, 1, false));
	ASSERT_TRUE(Add("same", 8, EKind::Echo, 1, true));
	EXPECT_EQ(m_Queue.size(), 2u);
}

TEST_F(QmHudNotificationQueue, TextPreparationChangesDisplayWithoutChangingCollapseIdentity)
{
	ASSERT_TRUE(m_Queue.Queue(EKind::System, "raw", 0, false, 8, 100, [](char *pText) { str_copy(pText, "filtered", 256); }));
	ASSERT_TRUE(Add("raw"));
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_STREQ(m_Queue.back().m_aText, "filtered");
	EXPECT_STREQ(m_Queue.back().m_aCollapseText, "raw");
}

TEST_F(QmHudNotificationQueue, AllocationFailureDoesNotPublishAPartiallyPreparedEntry)
{
	ASSERT_TRUE(Add("old"));
	EXPECT_FALSE(m_Queue.Queue(EKind::System, "new", 0, false, 8, 100, [](char *pText) {
		str_copy(pText, "partial", 256);
		throw std::bad_alloc();
	}));
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_STREQ(m_Queue.back().m_aText, "old");
}

TEST_F(QmHudNotificationQueue, FailedPreparationAtCapacityDoesNotEvictTheOldestEntry)
{
	ASSERT_NO_FATAL_FAILURE(Fill());
	EXPECT_FALSE(m_Queue.Queue(EKind::System, "new", 0, false, 8, 100, [](char *) { throw std::bad_alloc(); }));
	EXPECT_EQ(m_Queue.size(), 8u);
	EXPECT_STREQ(m_Queue.front().m_aText, "message0");
	EXPECT_STREQ(m_Queue.back().m_aText, "message7");
}

TEST_F(QmHudNotificationQueue, TextLengthFailureReturnsNotHandled)
{
	EXPECT_FALSE(m_Queue.Queue(EKind::Echo, "new", 0, false, 8, 100, [](char *) { throw std::length_error("controlled text limit"); }));
	EXPECT_TRUE(m_Queue.empty());
}

TEST_F(QmHudNotificationQueue, FailedPreparationCanBeRetried)
{
	EXPECT_FALSE(m_Queue.Queue(EKind::Echo, "new", 0, false, 8, 100, [](char *) { throw std::bad_alloc(); }));
	ASSERT_TRUE(Add("new", 8, EKind::Echo));
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_EQ(m_Queue.back().m_RepeatCount, 1);
}

TEST_F(QmHudNotificationQueue, ARepeatDoesNotRunTextPreparationAgain)
{
	ASSERT_TRUE(Add("same"));
	EXPECT_TRUE(m_Queue.Queue(EKind::System, "same", 0, false, 8, 200, [](char *) { throw std::bad_alloc(); }));
	EXPECT_EQ(m_Queue.back().m_RepeatCount, 2);
}

TEST_F(QmHudNotificationQueue, ClearAfterWraparoundStartsAFreshQueue)
{
	ASSERT_NO_FATAL_FAILURE(Fill());
	ASSERT_TRUE(Add("wrapped"));
	m_Queue.clear();
	EXPECT_TRUE(m_Queue.empty());
	ASSERT_TRUE(Add("fresh"));
	EXPECT_STREQ(m_Queue.front().m_aText, "fresh");
	EXPECT_EQ(m_Queue.back().m_RepeatCount, 1);
}

TEST_F(QmHudNotificationQueue, RemovingTheFrontPreservesRemainingOrder)
{
	m_Queue.pop_front();
	ASSERT_TRUE(Add("first"));
	ASSERT_TRUE(Add("second"));
	m_Queue.pop_front();
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_STREQ(m_Queue.front().m_aText, "second");
}

TEST_F(QmHudNotificationQueue, LoweredLimitAppliesOnNewEntriesAndDoesNotAlterRepeatSemantics)
{
	ASSERT_NO_FATAL_FAILURE(Fill());
	ASSERT_TRUE(Add("message7", 1));
	EXPECT_EQ(m_Queue.size(), 8u);
	ASSERT_TRUE(Add("different", 1));
	EXPECT_EQ(m_Queue.size(), 1u);
}

TEST_F(QmHudNotificationQueue, RepeatCountAtIntegerLimitStillUpdatesThePulse)
{
	ASSERT_TRUE(Add("same"));
	m_Queue.back().m_RepeatCount = std::numeric_limits<int>::max();
	ASSERT_TRUE(Add("same", 8, EKind::System, 0, false, 999));
	EXPECT_EQ(m_Queue.back().m_RepeatCount, std::numeric_limits<int>::max());
	EXPECT_EQ(m_Queue.back().m_RepeatAnimTime, 999);
	EXPECT_STREQ(m_Queue.back().m_aRepeatText, "x2147483647");
}

TEST_F(QmHudNotificationQueue, RepeatCountCanReachTheIntegerLimitWithoutWrapping)
{
	ASSERT_TRUE(Add("same"));
	m_Queue.back().m_RepeatCount = std::numeric_limits<int>::max() - 1;
	ASSERT_TRUE(Add("same"));
	EXPECT_EQ(m_Queue.back().m_RepeatCount, std::numeric_limits<int>::max());
}

TEST_F(QmHudNotificationQueue, OuterPreparationFailureReturnsNotHandledAndKeepsTheQueue)
{
	ASSERT_TRUE(Add("old"));
	EXPECT_FALSE(QmHudNotifications::TryDelivery([]() -> bool { throw std::bad_alloc(); }));
	EXPECT_EQ(m_Queue.size(), 1u);
	EXPECT_STREQ(m_Queue.back().m_aText, "old");
}
