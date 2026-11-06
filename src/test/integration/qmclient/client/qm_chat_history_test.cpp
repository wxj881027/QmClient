#include <game/client/components/qmclient/chat_history.h>

#include <gtest/gtest.h>

#include <string>

TEST(QmChatHistory, PlainRecordInitializesMetadataAndReturnsItsOwnText)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 0, "!shrug"));
	const auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Team, 0);
	EXPECT_EQ(pEntry->m_OriginalOffset, 0);
	EXPECT_EQ(pEntry->InputText(), pEntry->m_aText);
	EXPECT_STREQ(pEntry->InputText(), "!shrug");
}

TEST(QmChatHistory, TransformedRecordKeepsDisplayAndOriginalInputSeparately)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 1, "translated", "original"));
	const auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Team, 1);
	EXPECT_EQ(pEntry->m_OriginalOffset, 11);
	EXPECT_STREQ(pEntry->m_aText, "translated");
	EXPECT_STREQ(pEntry->InputText(), "original");
}

TEST(QmChatHistory, EmptyOriginalUsesThePlainRecordRepresentation)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 1, "plain", ""));
	const auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_OriginalOffset, 0);
	EXPECT_STREQ(pEntry->InputText(), "plain");
}

TEST(QmChatHistory, ClearedTranslatedStorageDoesNotLeakItsOffsetIntoLocalCommands)
{
	for(const char *pText : {"!flip", "/specid 4"})
	{
		SCOPED_TRACE(pText);
		CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
		ASSERT_TRUE(QmAppendChatHistory(History, 1, "translated", "original"));
		const auto *pOld = History.Last();
		ASSERT_GT(pOld->m_OriginalOffset, 0);
		History.Clear();
		ASSERT_TRUE(QmAppendChatHistory(History, 0, pText));
		const auto *pNew = History.Last();
		ASSERT_EQ(pNew, pOld);
		EXPECT_EQ(pNew->m_OriginalOffset, 0);
		EXPECT_EQ(pNew->m_Team, 0);
		EXPECT_STREQ(pNew->InputText(), pText);
	}
}

TEST(QmChatHistory, RecyclingTranslatedStorageInitializesEveryNewBindOffset)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 128, CRingBufferBase::FLAG_RECYCLE> History;
	const std::string Translated(64, 't'), Bind(64, 'b');
	ASSERT_TRUE(QmAppendChatHistory(History, 1, Translated, "o"));
	const auto *pOld = History.Last();
	ASSERT_GT(pOld->m_OriginalOffset, 0);
	ASSERT_TRUE(QmAppendChatHistory(History, 0, Bind));
	const auto *pNew = History.Last();
	ASSERT_EQ(pNew, pOld);
	EXPECT_EQ(pNew->m_OriginalOffset, 0);
	EXPECT_EQ(pNew->m_Team, 0);
	EXPECT_STREQ(pNew->InputText(), Bind.c_str());
	EXPECT_EQ(History.First(), pNew);
}

TEST(QmChatHistory, NavigationResolvesEachMixedRecordToItsOriginalInput)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 0, "!shrug"));
	ASSERT_TRUE(QmAppendChatHistory(History, 1, "translated", "original"));
	ASSERT_TRUE(QmAppendChatHistory(History, 0, "!flip"));
	auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_STREQ(pEntry->InputText(), "!flip");
	pEntry = History.Prev(pEntry);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_STREQ(pEntry->InputText(), "original");
	pEntry = History.Prev(pEntry);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_STREQ(pEntry->InputText(), "!shrug");
	EXPECT_EQ(History.Prev(pEntry), nullptr);
	pEntry = History.Next(pEntry);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_STREQ(pEntry->InputText(), "original");
}

TEST(QmChatHistory, OversizedTextReturnsFailureAndKeepsExistingHistory)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 128, CRingBufferBase::FLAG_RECYCLE> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 0, "old"));
	const auto *pOld = History.Last();
	const std::string TooLong(256, 'x');
	EXPECT_FALSE(QmAppendChatHistory(History, 1, TooLong));
	ASSERT_EQ(History.Last(), pOld);
	EXPECT_STREQ(pOld->InputText(), "old");
}

TEST(QmChatHistory, OversizedOriginalDoesNotPublishAPartialTranslatedEntry)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 128, CRingBufferBase::FLAG_RECYCLE> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 0, "old"));
	const auto *pOld = History.Last();
	const std::string TooLong(256, 'x');
	EXPECT_FALSE(QmAppendChatHistory(History, 1, "display", TooLong));
	ASSERT_EQ(History.Last(), pOld);
	EXPECT_STREQ(pOld->InputText(), "old");
	EXPECT_EQ(pOld->m_OriginalOffset, 0);
}

TEST(QmChatHistory, FullNonRecyclingStorageReturnsFailureWithoutDereferencingNull)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 128> History;
	const std::string Text(64, 'x');
	ASSERT_TRUE(QmAppendChatHistory(History, 0, Text));
	const auto *pOld = History.Last();
	EXPECT_FALSE(QmAppendChatHistory(History, 1, Text, "original"));
	ASSERT_EQ(History.Last(), pOld);
	EXPECT_EQ(pOld->m_Team, 0);
	EXPECT_STREQ(pOld->InputText(), Text.c_str());
}

TEST(QmChatHistory, OriginalOffsetCountsUtf8BytesAndKeepsBothTextsIntact)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
	const std::string Display = "你好", Original = "原文";
	ASSERT_TRUE(QmAppendChatHistory(History, 1, Display, Original));
	const auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_OriginalOffset, Display.size() + 1);
	EXPECT_STREQ(pEntry->m_aText, Display.c_str());
	EXPECT_STREQ(pEntry->InputText(), Original.c_str());
}

TEST(QmChatHistory, EmptyTextStillInitializesACompleteRecord)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 128> History;
	ASSERT_TRUE(QmAppendChatHistory(History, 0, {}));
	const auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_OriginalOffset, 0);
	EXPECT_STREQ(pEntry->InputText(), "");
}

TEST(QmChatHistory, ASmallerAppendCanSucceedAfterAnOversizedFailure)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 128, CRingBufferBase::FLAG_RECYCLE> History;
	const std::string TooLong(256, 'x');
	ASSERT_FALSE(QmAppendChatHistory(History, 0, TooLong));
	EXPECT_EQ(History.Last(), nullptr);
	ASSERT_TRUE(QmAppendChatHistory(History, 0, "!retry"));
	ASSERT_NE(History.Last(), nullptr);
	EXPECT_STREQ(History.Last()->InputText(), "!retry");
	EXPECT_EQ(History.Last()->m_OriginalOffset, 0);
}

TEST(QmChatHistory, IncompleteUtf8TailsKeepTheExistingStringCopyBehavior)
{
	CStaticRingBuffer<SQmChatHistoryEntry, 1024> History;
	const std::string Display = "ok\xe4\xbd", Original = "raw\xe4\xbd";
	ASSERT_TRUE(QmAppendChatHistory(History, 1, Display, Original));
	const auto *pEntry = History.Last();
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_OriginalOffset, Display.size() + 1);
	EXPECT_STREQ(pEntry->m_aText, "ok");
	EXPECT_STREQ(pEntry->InputText(), "raw");
}
