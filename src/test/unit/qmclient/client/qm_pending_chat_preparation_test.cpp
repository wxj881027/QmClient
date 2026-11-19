#include <game/client/components/qmclient/pending_chat_messages.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using EAdmission = CPendingChatMessages::EAdmission;
	using SMessage = CPendingChatMessages::SMessage;

	class QmPendingChatPreparation : public ::testing::Test
	{
	protected:
		CPendingChatMessages m_Queue;

		static SMessage Message(const char *pText)
		{
			return {1, 0, 7, pText, "original"};
		}
		static bool Accept(const SMessage &) { return true; }

		template<typename TPrepare>
		void ExpectFailurePreservesOldMessages(TPrepare &&Prepare)
		{
			ASSERT_EQ(m_Queue.Admit(100, 101, 10, Message("first"), Accept), EAdmission::QUEUED);
			ASSERT_EQ(m_Queue.Admit(100, 102, 10, Message("second"), Accept), EAdmission::QUEUED);
			int SendCalls = 0;
			EXPECT_EQ(m_Queue.PrepareAndAdmit(100, 103, 10, std::forward<TPrepare>(Prepare), [&](const SMessage &) {
				++SendCalls;
				return true;
			}),
				EAdmission::REJECTED);
			EXPECT_EQ(SendCalls, 0);
			ASSERT_EQ(m_Queue.Size(), 2u);
			std::vector<std::string> vSent;
			const auto Send = [&](const SMessage &Pending) {
				EXPECT_EQ(Pending.m_Original, "original");
				EXPECT_EQ(Pending.m_Connection, 0);
				EXPECT_EQ(Pending.m_ClientId, 7);
				vSent.push_back(Pending.m_Text);
				return true;
			};
			EXPECT_TRUE(m_Queue.ProcessReady(100, 111, 10, Send));
			EXPECT_TRUE(m_Queue.ProcessReady(100, 112, 10, Send));
			EXPECT_EQ(vSent, (std::vector<std::string>{"first", "second"}));
			EXPECT_EQ(m_Queue.Size(), 0u);
		}
	};
}

TEST_F(QmPendingChatPreparation, AllocationFailureRejectsNewMessageAndPreservesOldMetadataAndOrder)
{
	ExpectFailurePreservesOldMessages([]() -> SMessage { throw std::bad_alloc(); });
}

TEST_F(QmPendingChatPreparation, LengthFailureRejectsNewMessageWithoutSendingOrChangingOldQueue)
{
	ExpectFailurePreservesOldMessages([]() -> SMessage { throw std::length_error("message preparation"); });
}

TEST_F(QmPendingChatPreparation, UnknownPreparationFailureRejectsNewMessageAndPreservesOldQueue)
{
	ExpectFailurePreservesOldMessages([]() -> SMessage { throw 7; });
}

TEST_F(QmPendingChatPreparation, FullQueueRejectsBeforePreparingOrSendingTheNewMessage)
{
	for(size_t Index = 0; Index < CPendingChatMessages::CAPACITY; ++Index)
		ASSERT_EQ(m_Queue.Admit(100, 101, 10, Message("old"), Accept), EAdmission::QUEUED);
	int PrepareCalls = 0;
	int SendCalls = 0;
	EXPECT_EQ(m_Queue.PrepareAndAdmit(100, 111, 10, [&]() -> SMessage {
		++PrepareCalls;
		throw std::bad_alloc(); }, [&](const SMessage &) {
			++SendCalls;
			return true; }), EAdmission::REJECTED);
	EXPECT_EQ(PrepareCalls, 0);
	EXPECT_EQ(SendCalls, 0);
	EXPECT_EQ(m_Queue.Size(), CPendingChatMessages::CAPACITY);
}

TEST_F(QmPendingChatPreparation, PreparationFailureOnEmptyQueueAllowsASubsequentImmediateSend)
{
	EXPECT_EQ(m_Queue.PrepareAndAdmit(100, 111, 10, []() -> SMessage { throw std::bad_alloc(); }, Accept), EAdmission::REJECTED);
	EXPECT_EQ(m_Queue.Size(), 0u);
	int SendCalls = 0;
	EXPECT_EQ(m_Queue.PrepareAndAdmit(100, 112, 10, [] { return Message("retry"); }, [&](const SMessage &Pending) {
		++SendCalls;
		EXPECT_EQ(Pending.m_Text, "retry");
		return true; }), EAdmission::SENT);
	EXPECT_EQ(SendCalls, 1);
	EXPECT_EQ(m_Queue.Size(), 0u);
}

TEST_F(QmPendingChatPreparation, SendExceptionIsNotReportedAsAMessagePreparationRejection)
{
	EXPECT_THROW(m_Queue.PrepareAndAdmit(100, 111, 10, [] { return Message("ready"); }, [](const SMessage &) -> bool { throw std::runtime_error("send boundary"); }), std::runtime_error);
	EXPECT_EQ(m_Queue.Size(), 0u);
}
