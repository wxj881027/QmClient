#include <game/client/components/qmclient/chat_translation_notes.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <limits>
#include <string>

namespace
{
	class QmOutgoingAnnotationFailure : public ::testing::Test
	{
	protected:
		CTrackedAllocationResource m_Memory;
		COutgoingChatAnnotations m_Notes{&m_Memory};
		const std::string m_LongText = std::string(96, 'x');

		void FailNext() { m_Memory.m_FailAt = m_Memory.m_Allocations + 1; }
		bool FailedRecord(int Connection = 0, int64_t Now = 100, int64_t Lifetime = 30)
		{
			FailNext();
			return m_Notes.Record(Connection, 7, 0, m_LongText.c_str(), "lost original", Now, Lifetime);
		}
		void TearDown() override
		{
			m_Notes.Clear();
			EXPECT_EQ(m_Memory.m_Live, 0);
		}
	};
}

TEST_F(QmOutgoingAnnotationFailure, UnusedCacheAndMissingEchoDoNotAllocateStorage)
{
	FailNext();
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "missing", 100).empty());
	m_Notes.InvalidateConnection(0);
	m_Notes.Clear();
	EXPECT_EQ(m_Memory.m_Allocations, 0);
}

TEST_F(QmOutgoingAnnotationFailure, TextAllocationFailureReturnsFalseWithoutPublishingAnEntry)
{
	EXPECT_FALSE(FailedRecord());
	EXPECT_EQ(m_Notes.Size(), 0u);
	EXPECT_EQ(m_Memory.m_Live, 0);
}

TEST_F(QmOutgoingAnnotationFailure, NullTextAllocationReturnsFailureWithoutPublishingAnEntry)
{
	m_Memory.m_ReturnNull = true;
	EXPECT_FALSE(FailedRecord());
	EXPECT_EQ(m_Notes.Size(), 0u);
}

TEST_F(QmOutgoingAnnotationFailure, UnknownTextAllocationExceptionReturnsFailureWithoutPublishingAnEntry)
{
	m_Memory.m_ThrowUnknown = true;
	EXPECT_FALSE(FailedRecord());
	EXPECT_EQ(m_Notes.Size(), 0u);
}

TEST_F(QmOutgoingAnnotationFailure, FailedCandidateAtCapacityPreservesTheOldestAndNewestEntries)
{
	for(size_t Index = 0; Index < COutgoingChatAnnotations::CAPACITY; ++Index)
		ASSERT_TRUE(m_Notes.Record(0, 7, 0, std::to_string(Index).c_str(), "old", 100, 100));
	ASSERT_FALSE(FailedRecord(0, 101));
	EXPECT_EQ(m_Notes.Size(), COutgoingChatAnnotations::CAPACITY);
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "0", 102), "old");
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, std::to_string(COutgoingChatAnnotations::CAPACITY - 1).c_str(), 102), "old");
}

TEST_F(QmOutgoingAnnotationFailure, GapKeepsOldDistinctNotesAndOtherConnectionButBlocksNewOriginals)
{
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "old", "old original", 99, 100));
	ASSERT_TRUE(m_Notes.Record(1, 7, 0, m_LongText.c_str(), "other original", 99, 100));
	ASSERT_FALSE(FailedRecord());
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, m_LongText.c_str(), "new original", 101, 30));
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, m_LongText.c_str(), 102).empty());
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, m_LongText.c_str(), 103).empty());
	EXPECT_EQ(m_Notes.Consume(1, 7, 0, m_LongText.c_str(), 103), "other original");
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "old", 103), "old original");
}

TEST_F(QmOutgoingAnnotationFailure, NewDistinctOriginalsRecoverAtTheEndOfTheUntrackedEchoWindow)
{
	ASSERT_FALSE(FailedRecord());
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "fresh", "fresh original", 130, 30));
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "fresh", 130), "fresh original");
}

TEST_F(QmOutgoingAnnotationFailure, LaterShortFailureDoesNotShortenTheEarlierGap)
{
	ASSERT_FALSE(FailedRecord(0, 100, 100));
	ASSERT_FALSE(FailedRecord(0, 150, 10));
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "guarded", "unsafe original", 161, 30));
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "guarded", 161).empty());
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "recovered", "safe original", 200, 30));
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "recovered", 200), "safe original");
}

TEST_F(QmOutgoingAnnotationFailure, OversizedUntrackedOriginalAlsoPreventsLaterEchoMisattribution)
{
	const std::string LargeOriginal(4097, 'o');
	EXPECT_FALSE(m_Notes.Record(0, 7, 0, "same", LargeOriginal.c_str(), 100, 30));
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "same", "new original", 101, 30));
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "same", 102).empty());
}

TEST_F(QmOutgoingAnnotationFailure, DuplicateBecomesAmbiguousWithoutRequestingAnotherAllocation)
{
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, m_LongText.c_str(), "old", 100, 30));
	const int Before = m_Memory.m_Allocations;
	FailNext();
	EXPECT_TRUE(m_Notes.Record(0, 7, 0, m_LongText.c_str(), "new", 101, 30));
	EXPECT_EQ(m_Memory.m_Allocations, Before);
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, m_LongText.c_str(), 102).empty());
}

TEST_F(QmOutgoingAnnotationFailure, EchoLookupAndOriginalTransferDoNotAllocateTrackedTextStorage)
{
	const std::string Original(192, 'o');
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, m_LongText.c_str(), Original.c_str(), 100, 30));
	const int Before = m_Memory.m_Allocations;
	FailNext();
	const std::string Echo = m_LongText + "　 \t";
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, Echo.c_str(), 101), Original);
	EXPECT_EQ(m_Memory.m_Allocations, Before);
	EXPECT_EQ(m_Notes.Size(), 0u);
}

TEST_F(QmOutgoingAnnotationFailure, ClearDiscardsTheGapAndAllowsFreshOriginals)
{
	ASSERT_FALSE(FailedRecord());
	m_Notes.Clear();
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "fresh", "original", 101, 30));
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "fresh", 102), "original");
}

TEST_F(QmOutgoingAnnotationFailure, DisconnectClearsOnlyTheChosenConnectionsGap)
{
	ASSERT_FALSE(FailedRecord(0, 100));
	ASSERT_FALSE(FailedRecord(1, 101));
	m_Memory.m_FailAt = -1;
	m_Notes.InvalidateConnection(1);
	ASSERT_TRUE(m_Notes.Record(1, 7, 0, "dummy", "dummy original", 102, 30));
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "main", "main original", 102, 30));
	EXPECT_EQ(m_Notes.Consume(1, 7, 0, "dummy", 103), "dummy original");
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "main", 103).empty());
}

TEST_F(QmOutgoingAnnotationFailure, ClockRollbackClearsTheGapBeforeFreshRecording)
{
	ASSERT_FALSE(FailedRecord());
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "fresh", "original", 50, 30));
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "fresh", 51), "original");
}

TEST_F(QmOutgoingAnnotationFailure, CapacityEvictionPreventsALaterSameTextFromClaimingTheOldEcho)
{
	for(size_t Index = 0; Index < COutgoingChatAnnotations::CAPACITY; ++Index)
		ASSERT_TRUE(m_Notes.Record(0, 7, 0, std::to_string(Index).c_str(), "old", 100, 100));
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "overflow", "unrelated original", 101, 100));
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "0", "new original", 102, 100));
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "0", 103).empty());
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "0", 104).empty());
	EXPECT_EQ(m_Notes.Consume(0, 7, 0, "overflow", 104), "unrelated original");
}

TEST_F(QmOutgoingAnnotationFailure, GapWindowDoesNotAddLifetimeToANearLimitTimestamp)
{
	const int64_t Now = std::numeric_limits<int64_t>::max() - 10;
	ASSERT_FALSE(FailedRecord(0, Now, 30));
	m_Memory.m_FailAt = -1;
	ASSERT_TRUE(m_Notes.Record(0, 7, 0, "guarded", "unsafe original", Now + 5, 30));
	EXPECT_TRUE(m_Notes.Consume(0, 7, 0, "guarded", Now + 6).empty());
}
