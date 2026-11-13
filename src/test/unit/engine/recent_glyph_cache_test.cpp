#include <engine/client/qm_recent_glyphs.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <cstring>
#include <new>

namespace
{
	using QmRecentGlyphs::CCollection;
	using TItems = std::vector<std::pair<int, int>>;
	struct SInput
	{
		std::string m_Bytes;
		size_t m_Pos = 0, m_Chunk = 4096;
		int m_Reads = 0, m_Closes = 0, m_FailRead = -1;
		bool m_Failed = false, m_ThrowRead = false, m_CloseOk = true, m_ThrowClose = false;
		unsigned Read(char *pBuffer, unsigned Size)
		{
			if(++m_Reads == m_FailRead)
			{
				if(m_ThrowRead)
					throw 7;
				m_Failed = true;
				return 0;
			}
			const auto Count = std::min({m_Bytes.size() - m_Pos, m_Chunk, static_cast<size_t>(Size)});
			std::memcpy(pBuffer, m_Bytes.data() + m_Pos, Count);
			m_Pos += Count;
			return static_cast<unsigned>(Count);
		}
		bool Failed() const { return m_Failed; }
		bool Close()
		{
			++m_Closes;
			if(m_ThrowClose)
				throw 7;
			return m_CloseOk;
		}
	};
	enum class EFailure
	{
		NONE,
		OPEN,
		WRITE,
		SYNC,
		CLOSE,
		COMMIT,
		THROW_WRITE,
		THROW_CLOSE,
		THROW_COMMIT
	};
	struct SOutput
	{
		std::string m_Final = "previous cache", m_Candidate;
		EFailure m_Failure = EFailure::NONE;
		int m_Opens = 0, m_Closes = 0, m_Commits = 0, m_Discards = 0;
		bool m_Open = false, m_CommitSawClosed = false;
		bool Open()
		{
			++m_Opens;
			m_Open = m_Failure != EFailure::OPEN;
			return m_Open;
		}
		unsigned Write(const char *pText, unsigned Size)
		{
			if(m_Failure == EFailure::THROW_WRITE)
				throw 7;
			if(m_Failure == EFailure::WRITE)
				return Size - 1;
			m_Candidate.append(pText, Size);
			return Size;
		}
		bool Sync() const { return m_Failure != EFailure::SYNC; }
		bool Close()
		{
			if(!std::exchange(m_Open, false))
				return true;
			++m_Closes;
			if(m_Failure == EFailure::THROW_CLOSE)
				throw 7;
			return m_Failure != EFailure::CLOSE;
		}
		bool Commit()
		{
			++m_Commits;
			m_CommitSawClosed = !m_Open;
			if(m_Failure == EFailure::THROW_COMMIT)
				throw 7;
			if(m_Failure == EFailure::COMMIT)
				return false;
			m_Final = m_Candidate;
			return true;
		}
		void Discard()
		{
			++m_Discards;
			m_Candidate.clear();
		}
	};
	void SaveFailure(EFailure Failure)
	{
		CCollection Collection;
		Collection.Record(65, 16);
		SOutput Output;
		Output.m_Failure = Failure;
		EXPECT_FALSE(QmRecentGlyphs::Save(Output, Collection));
		EXPECT_EQ(Output.m_Final, "previous cache");
		EXPECT_FALSE(Output.m_Open);
		EXPECT_EQ(Output.m_Closes, Failure == EFailure::OPEN ? 0 : 1);
		EXPECT_EQ(Output.m_Discards, 1);
		EXPECT_TRUE(Output.m_Candidate.empty());
		EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	}
}

TEST(RecentGlyphCollection, DeduplicatesPairsAndPreservesFirstSeenOrder)
{
	CCollection Collection;
	Collection.Record(65, 16);
	Collection.Record(65, 16);
	Collection.Record(65, 18);
	Collection.Record(66, 16);
	Collection.Record(0, 16);
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}, {65, 18}, {66, 16}}));
}

TEST(RecentGlyphCollection, KeepsTheExisting4096EntryCapacity)
{
	CCollection Collection;
	for(size_t Index = 0; Index < QmRecentGlyphs::MAX_ENTRIES + 1; ++Index)
		Collection.Record(static_cast<int>(Index + 1), 16);
	EXPECT_EQ(Collection.Items().size(), QmRecentGlyphs::MAX_ENTRIES);
	EXPECT_EQ(Collection.Items().back().first, static_cast<int>(QmRecentGlyphs::MAX_ENTRIES));
}

TEST(RecentGlyphCollection, EveryRecordAllocationFailureLeavesNoGhostKeyAndCanRetry)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 32; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CCollection Collection;
		Collection.Record(65, 16);
		bool Threw = false, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				Collection.Record(66, 18);
			}
			catch(const std::bad_alloc &)
			{
				Threw = true;
			}
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_FALSE(Threw);
			ReachedSuccess = true;
			break;
		}
		EXPECT_TRUE(Threw);
		++Failures;
		EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
		Collection.Record(66, 18);
		EXPECT_EQ(Collection.Items(), (TItems{{65, 16}, {66, 18}}));
	}
	EXPECT_TRUE(ReachedSuccess);
	EXPECT_GT(Failures, 0);
}

TEST(RecentGlyphCollection, NullGlyphKeepsTheExistingSkipRuleAndDoesNotUseSuccessfulBudget)
{
	CCollection Collection;
	Collection.Record(65, 16);
	Collection.Record(66, 16);
	Collection.Record(67, 16);
	std::array<int, 3> Calls{};
	int Count = 0;
	const auto Warm = [&](int Chr, int) { Calls.at(Count++) = Chr; return Chr != 66; };
	EXPECT_EQ(Collection.Prewarm(1, Warm), 1);
	EXPECT_EQ(Collection.Cursor(), 1u);
	EXPECT_EQ(Collection.Prewarm(1, Warm), 1);
	EXPECT_EQ(Collection.Cursor(), 3u);
	EXPECT_EQ(Calls, (std::array<int, 3>{65, 66, 67}));
}

TEST(RecentGlyphCollection, WarmExceptionRestoresRecordingAndDoesNotConsumeTheEntry)
{
	CCollection Collection;
	Collection.Record(65, 16);
	EXPECT_EQ(Collection.Prewarm(8, [&](int, int) -> bool { Collection.Record(66, 16); throw 7; }), 0);
	EXPECT_FALSE(Collection.Warming());
	EXPECT_EQ(Collection.Cursor(), 0u);
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	Collection.Record(66, 16);
	EXPECT_EQ(Collection.Items().size(), 2u);
	std::array<int, 2> Calls{};
	int Count = 0;
	EXPECT_EQ(Collection.Prewarm(2, [&](int Chr, int) { Calls.at(Count++) = Chr; return true; }), 2);
	EXPECT_EQ(Calls, (std::array<int, 2>{65, 66}));
}

TEST(RecentGlyphCollection, NestedWarmingIsRejectedAndSuccessfulWarmStillRestoresRecording)
{
	CCollection Collection;
	Collection.Record(65, 16);
	int NestedCalls = 0;
	EXPECT_EQ(Collection.Prewarm(1, [&](int, int) {
		EXPECT_EQ(Collection.Prewarm(1, [&](int, int) { ++NestedCalls; return true; }), 0);
		Collection.Record(66, 16);
		return true;
	}),
		1);
	EXPECT_EQ(NestedCalls, 0);
	EXPECT_EQ(Collection.Items().size(), 1u);
	Collection.Record(66, 16);
	EXPECT_EQ(Collection.Items().size(), 2u);
}

TEST(RecentGlyphLoad, MergesChunkedRecordsAndRetainsPreviousWarmProgress)
{
	CCollection Collection;
	Collection.Record(65, 16);
	Collection.Prewarm(1, [](int, int) { return true; });
	SInput Input{"66 18\r\n67 20"};
	Input.m_Chunk = 1;
	ASSERT_TRUE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}, {66, 18}, {67, 20}}));
	EXPECT_EQ(Collection.Cursor(), 1u);
	EXPECT_EQ(Input.m_Closes, 1);
}

TEST(RecentGlyphLoad, OverflowingDecimalGroupsDiscardOnlyTheirLine)
{
	CCollection Collection;
	SInput Input{"2147483648 16\n65 999999999999999999999999\n66 18\n2147483647 1"};
	ASSERT_TRUE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{66, 18}, {std::numeric_limits<int>::max(), 1}}));
}

TEST(RecentGlyphLoad, PreservesTheLegacyFirstTwoDigitGroupsAndIgnoresTrailingGroups)
{
	CCollection Collection;
	SInput Input{"chr=65 size=16 extra=99999999999999999999\n66,18\n0 3\n67 0\n"};
	ASSERT_TRUE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}, {66, 18}}));
}

TEST(RecentGlyphLoad, ReadFailureDiscardsTheParsedPrefixAndClosesInput)
{
	CCollection Collection;
	Collection.Record(65, 16);
	SInput Input{"66 18\n67 20"};
	Input.m_Chunk = 6;
	Input.m_FailRead = 2;
	EXPECT_FALSE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	EXPECT_EQ(Input.m_Closes, 1);
}

TEST(RecentGlyphLoad, ReadExceptionDiscardsTheParsedPrefixAndClosesInput)
{
	CCollection Collection;
	Collection.Record(65, 16);
	SInput Input{"66 18\n67 20"};
	Input.m_Chunk = 6;
	Input.m_FailRead = 2;
	Input.m_ThrowRead = true;
	EXPECT_FALSE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	EXPECT_EQ(Input.m_Closes, 1);
}

TEST(RecentGlyphLoad, CloseFailureDoesNotPublishTheCandidate)
{
	CCollection Collection;
	Collection.Record(65, 16);
	SInput Input{"66 18"};
	Input.m_CloseOk = false;
	EXPECT_FALSE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	EXPECT_EQ(Input.m_Closes, 1);
}

TEST(RecentGlyphLoad, CloseExceptionDoesNotPublishOrCloseTwice)
{
	CCollection Collection;
	Collection.Record(65, 16);
	SInput Input{"66 18"};
	Input.m_ThrowClose = true;
	EXPECT_FALSE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	EXPECT_EQ(Input.m_Closes, 1);
}

TEST(RecentGlyphLoad, FileOverTheFormatBudgetRetainsThePreviousCollection)
{
	CCollection Collection;
	Collection.Record(65, 16);
	SInput Input{std::string(QmRecentGlyphs::MAX_FILE_BYTES + 1, ' ')};
	EXPECT_FALSE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
	EXPECT_EQ(Input.m_Closes, 1);
	EXPECT_EQ(Input.m_Pos, QmRecentGlyphs::MAX_FILE_BYTES + 1);
}

TEST(RecentGlyphLoad, EveryAllocationFailureKeepsPreviousCollectionAndClosesExactlyOnce)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 64; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CCollection Collection;
		Collection.Record(65, 16);
		SInput Input{"66 18\n67 20\n68 22"};
		bool Loaded, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Loaded = QmRecentGlyphs::Load(Input, Collection);
			Triggered = Failure.Triggered();
		}
		EXPECT_EQ(Input.m_Closes, 1);
		if(!Triggered)
		{
			EXPECT_TRUE(Loaded);
			ReachedSuccess = true;
			break;
		}
		EXPECT_FALSE(Loaded);
		++Failures;
		EXPECT_EQ(Collection.Items(), (TItems{{65, 16}}));
		SInput Retry{"66 18\n67 20\n68 22"};
		ASSERT_TRUE(QmRecentGlyphs::Load(Retry, Collection));
		EXPECT_EQ(Collection.Items().size(), 4u);
	}
	EXPECT_TRUE(ReachedSuccess);
	EXPECT_GT(Failures, 0);
}

TEST(RecentGlyphLoad, ExactFormatBudgetStillChecksEofAndCommits)
{
	CCollection Collection;
	SInput Input{std::string(QmRecentGlyphs::MAX_FILE_BYTES, ' ')};
	ASSERT_TRUE(QmRecentGlyphs::Load(Input, Collection));
	EXPECT_TRUE(Collection.Items().empty());
	EXPECT_EQ(Input.m_Closes, 1);
}

TEST(RecentGlyphSave, PublishesTheOriginalLineFormatOnlyAfterClosing)
{
	CCollection Collection;
	Collection.Record(65, 16);
	Collection.Record(20013, 20);
	SOutput Output;
	ASSERT_TRUE(QmRecentGlyphs::Save(Output, Collection));
	EXPECT_EQ(Output.m_Final, "65 16\n20013 20\n");
	EXPECT_TRUE(Output.m_CommitSawClosed);
	EXPECT_EQ(Output.m_Closes, 1);
	EXPECT_EQ(Output.m_Discards, 0);
}

TEST(RecentGlyphSave, EmptyCollectionDoesNotOpenOrTruncateThePreviousCache)
{
	CCollection Collection;
	SOutput Output;
	EXPECT_TRUE(QmRecentGlyphs::Save(Output, Collection));
	EXPECT_EQ(Output.m_Final, "previous cache");
	EXPECT_EQ(Output.m_Opens, 0);
	EXPECT_EQ(Output.m_Discards, 0);
}

TEST(RecentGlyphSave, OpenFailurePreservesPreviousFile) { SaveFailure(EFailure::OPEN); }
TEST(RecentGlyphSave, ShortWritePreservesPreviousFile) { SaveFailure(EFailure::WRITE); }
TEST(RecentGlyphSave, SyncFailurePreservesPreviousFile) { SaveFailure(EFailure::SYNC); }
TEST(RecentGlyphSave, CloseFailurePreservesPreviousFile) { SaveFailure(EFailure::CLOSE); }
TEST(RecentGlyphSave, CommitFailurePreservesPreviousFile) { SaveFailure(EFailure::COMMIT); }
TEST(RecentGlyphSave, WriteExceptionPreservesPreviousFile) { SaveFailure(EFailure::THROW_WRITE); }
TEST(RecentGlyphSave, CloseExceptionPreservesPreviousFile) { SaveFailure(EFailure::THROW_CLOSE); }
TEST(RecentGlyphSave, CommitExceptionPreservesPreviousFile) { SaveFailure(EFailure::THROW_COMMIT); }
