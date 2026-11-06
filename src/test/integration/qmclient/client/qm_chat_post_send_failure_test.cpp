#include <game/client/components/qmclient/chat_translation_notes.h>
#include <game/client/components/qmclient/local_saves.h>
#include <game/client/components/qmclient/pending_chat_messages.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <new>
#include <string>

TEST(QmChatPostSendFailureIntegration, AcceptedPacketLeavesThePendingQueueEvenWhenAnnotationAllocationFails)
{
	CTrackedAllocationResource Memory;
	COutgoingChatAnnotations Notes(&Memory);
	CPendingChatMessages Pending;
	const std::string Text(96, 'x');
	int AcceptedPackets = 0;
	ASSERT_EQ(Pending.Admit(100, 101, 10, {0, 0, 7, Text, "original"}, [](const auto &) { return true; }), CPendingChatMessages::EAdmission::QUEUED);
	Memory.m_FailAt = Memory.m_Allocations + 1;
	const auto Send = [&](const CPendingChatMessages::SMessage &Message) {
		// 替身仅表示传输边界已接收；附注与队首确认仍使用生产模块。
		++AcceptedPackets;
		EXPECT_FALSE(Notes.Record(Message.m_Connection, Message.m_ClientId, Message.m_Team, Message.m_Text.c_str(), Message.m_Original.c_str(), 111, 300));
		return true;
	};
	EXPECT_TRUE(Pending.ProcessReady(100, 111, 10, Send));
	EXPECT_EQ(Pending.Size(), 0u);
	EXPECT_FALSE(Pending.ProcessReady(111, 122, 10, Send));
	EXPECT_EQ(AcceptedPackets, 1);
	EXPECT_TRUE(Notes.Consume(0, 7, 0, Text.c_str(), 123).empty());
}

TEST(QmChatPostSendFailureIntegration, FailedLocalLoadTrackingDoesNotReturnAnAcceptedCommandToTheQueue)
{
	CPendingChatMessages Pending;
	QmLocalSaves::CConfirmation Confirmation;
	QmLocalSaves::CRestore Restore;
	Confirmation.Track({"map", "old", 0, 2, 50, false, 10000});
	Restore.Begin({"time", "A, B", "map", "old"}, {"A", "B"}, 100, 1000);
	ASSERT_EQ(Pending.Admit(100, 101, 10, {0, 0, 7, "/load code", ""}, [](const auto &) { return true; }), CPendingChatMessages::EAdmission::QUEUED);
	int AcceptedPackets = 0;
	const auto Send = [&](const auto &) {
		++AcceptedPackets;
		EXPECT_FALSE(QmLocalSaves::TrackAfterSend(Confirmation, Restore, [] { throw std::bad_alloc(); }));
		return true;
	};
	EXPECT_TRUE(Pending.ProcessReady(100, 111, 10, Send));
	EXPECT_FALSE(Confirmation.Active());
	EXPECT_FALSE(Restore.Active());
	EXPECT_EQ(Pending.Size(), 0u);
	EXPECT_FALSE(Pending.ProcessReady(111, 122, 10, Send));
	EXPECT_EQ(AcceptedPackets, 1);
}

TEST(QmChatPostSendFailureIntegration, EarlierUntrackedEchoCannotTakeTheNextAcceptedMessagesOriginal)
{
	CTrackedAllocationResource Memory;
	COutgoingChatAnnotations Notes(&Memory);
	CPendingChatMessages Pending;
	const std::string Text(96, 'x');
	ASSERT_EQ(Pending.Admit(100, 101, 10, {0, 0, 7, Text, "lost original"}, [](const auto &) { return true; }), CPendingChatMessages::EAdmission::QUEUED);
	int AcceptedPackets = 0;
	Memory.m_FailAt = Memory.m_Allocations + 1;
	EXPECT_TRUE(Pending.ProcessReady(100, 111, 10, [&](const auto &Message) {
		++AcceptedPackets;
		EXPECT_FALSE(Notes.Record(0, 7, 0, Message.m_Text.c_str(), Message.m_Original.c_str(), 111, 300));
		return true;
	}));
	Memory.m_FailAt = -1;
	EXPECT_EQ(Pending.Admit(111, 122, 10, {0, 0, 7, Text, "next original"}, [&](const auto &Message) {
		++AcceptedPackets;
		EXPECT_TRUE(Notes.Record(0, 7, 0, Message.m_Text.c_str(), Message.m_Original.c_str(), 122, 300));
		return true;
	}),
		CPendingChatMessages::EAdmission::SENT);
	EXPECT_EQ(AcceptedPackets, 2);
	EXPECT_EQ(Pending.Size(), 0u);
	EXPECT_TRUE(Notes.Consume(0, 7, 0, Text.c_str(), 123).empty());
	EXPECT_TRUE(Notes.Consume(0, 7, 0, Text.c_str(), 124).empty());
}
