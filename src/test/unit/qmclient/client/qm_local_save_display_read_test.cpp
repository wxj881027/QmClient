#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/local_save_display.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>

#include <cstring>
#include <new>

namespace
{
	struct CReadOperations
	{
		std::string m_Text = "date,A,Map,new-1\ndate,B,Map,new-2\ndate,C,Other,new-3\n";
		bool m_NotFound = false;
		bool m_OpenFailure = false;
		bool m_ThrowOpen = false;
		bool m_NullRead = false;
		bool m_ThrowRead = false;
		bool m_ReadError = false;
		bool m_CloseFailure = false;
		int m_Closes = 0;
		int m_LiveBuffers = 0;
		int m_Frees = 0;
		IOHANDLE Open(const char *, bool &NotFound)
		{
			if(m_ThrowOpen)
				throw 7;
			NotFound = m_NotFound;
			return m_NotFound || m_OpenFailure ? nullptr : reinterpret_cast<IOHANDLE>(this);
		}
		char *Read(IOHANDLE)
		{
			if(m_ThrowRead)
				throw std::bad_alloc();
			if(m_NullRead)
				return nullptr;
			auto *pBuffer = static_cast<char *>(malloc(m_Text.size() + 1));
			if(!pBuffer)
				throw std::bad_alloc();
			std::memcpy(pBuffer, m_Text.c_str(), m_Text.size() + 1);
			++m_LiveBuffers;
			return pBuffer;
		}
		bool Error(IOHANDLE) const { return m_ReadError; }
		bool Close(IOHANDLE) noexcept
		{
			++m_Closes;
			return !m_CloseFailure;
		}
		void Free(char *pBuffer) noexcept
		{
			free(pBuffer);
			--m_LiveBuffers;
			++m_Frees;
		}
	};

	class QmLocalSaveDisplayRead : public testing::Test
	{
	protected:
		CReadOperations m_Operations;
		std::vector<qm_local_saves::SLocalSaveDisplayEntry> m_Output{1};
		bool m_Exists = true;
		void SetUp() override { m_Output[0].m_Code = "old"; }
		bool Read() { return qm_local_saves::ReadDisplayEntries(m_Operations, "saves.csv", m_Output, m_Exists); }
		void ExpectOld() const
		{
			ASSERT_EQ(m_Output.size(), 1u);
			EXPECT_EQ(m_Output[0].m_Code, "old");
			EXPECT_TRUE(m_Exists);
			EXPECT_EQ(m_Operations.m_LiveBuffers, 0);
		}
	};

	class QmLocalSaveDisplayAllocation : public testing::Test
	{
	protected:
		using TEntries = std::vector<qm_local_saves::SLocalSaveDisplayEntry, CBorrowedAllocator<qm_local_saves::SLocalSaveDisplayEntry>>;
		CTrackedAllocationResource m_Memory;
		void CheckAllocationFailures()
		{
			int Points;
			{
				CReadOperations Probe;
				TEntries Entries{CBorrowedAllocator<qm_local_saves::SLocalSaveDisplayEntry>(&m_Memory)};
				bool Exists = false;
				const int Before = m_Memory.m_Allocations;
				ASSERT_TRUE(qm_local_saves::ReadDisplayEntries(Probe, "saves.csv", Entries, Exists));
				Points = m_Memory.m_Allocations - Before;
			}
			ASSERT_GT(Points, 0);
			for(int Point = 1; Point <= Points; ++Point)
			{
				SCOPED_TRACE(Point);
				CReadOperations Operations;
				{
					TEntries Entries{CBorrowedAllocator<qm_local_saves::SLocalSaveDisplayEntry>(&m_Memory)};
					Entries.resize(1);
					Entries[0].m_Code = "old";
					bool Exists = true;
					const int Live = m_Memory.m_Live;
					m_Memory.m_FailAt = m_Memory.m_Allocations + Point;
					EXPECT_FALSE(qm_local_saves::ReadDisplayEntries(Operations, "saves.csv", Entries, Exists));
					ASSERT_EQ(Entries.size(), 1u);
					EXPECT_EQ(Entries[0].m_Code, "old");
					EXPECT_TRUE(Exists);
					EXPECT_EQ(m_Memory.m_Live, Live);
					EXPECT_EQ(Operations.m_LiveBuffers, 0);
					EXPECT_EQ(Operations.m_Closes, 1);
					EXPECT_EQ(Operations.m_Frees, 1);
					m_Memory.m_FailAt = -1;
					ASSERT_TRUE(qm_local_saves::ReadDisplayEntries(Operations, "saves.csv", Entries, Exists));
					ASSERT_EQ(Entries.size(), 3u);
					EXPECT_EQ(Entries.back().m_Code, "new-3");
					EXPECT_EQ(Operations.m_LiveBuffers, 0);
				}
				EXPECT_EQ(m_Memory.m_Live, 0);
			}
		}
	};
}

TEST(LocalSaveDisplay, ParsingPreservesQuotedFieldsHeaderAndRawLine)
{
	const char *pText = "Time,Players,Map,Code\n2026-09-20,\"A, B\", Map ,\"say \"\"hi\"\"\"\r\n\n";
	const auto vEntries = qm_local_saves::ParseEntries(pText);
	ASSERT_EQ(vEntries.size(), 1u);
	EXPECT_EQ(vEntries[0].m_Time, "2026-09-20");
	EXPECT_EQ(vEntries[0].m_Players, "A, B");
	EXPECT_EQ(vEntries[0].m_Map, "Map");
	EXPECT_EQ(vEntries[0].m_Code, "say \"hi\"");
	EXPECT_EQ(vEntries[0].m_RawLine, "2026-09-20,\"A, B\", Map ,\"say \"\"hi\"\"\"");
	EXPECT_TRUE(qm_local_saves::ParseEntries("").empty());
	EXPECT_EQ(qm_local_saves::ParseEntries("date,player,map,code").size(), 1u);
}

TEST_F(QmLocalSaveDisplayRead, CompleteReadPublishesAllEntriesAndReleasesBuffer)
{
	ASSERT_TRUE(Read());
	ASSERT_EQ(m_Output.size(), 3u);
	EXPECT_EQ(m_Output[0].m_Code, "new-1");
	EXPECT_EQ(m_Output.back().m_Code, "new-3");
	EXPECT_TRUE(m_Exists);
	EXPECT_EQ(m_Operations.m_LiveBuffers, 0);
	EXPECT_EQ(m_Operations.m_Frees, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(QmLocalSaveDisplayRead, MissingFileClearsOldListAsSuccessfulAbsence)
{
	m_Operations.m_NotFound = true;
	EXPECT_TRUE(Read());
	EXPECT_TRUE(m_Output.empty());
	EXPECT_FALSE(m_Exists);
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(QmLocalSaveDisplayRead, EmptyFileIsSuccessfulExistingEmptyList)
{
	m_Operations.m_Text.clear();
	EXPECT_TRUE(Read());
	EXPECT_TRUE(m_Output.empty());
	EXPECT_TRUE(m_Exists);
	EXPECT_EQ(m_Operations.m_Frees, 1);
}

TEST_F(QmLocalSaveDisplayRead, OpenFailurePreservesOldList)
{
	m_Operations.m_OpenFailure = true;
	EXPECT_FALSE(Read());
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(QmLocalSaveDisplayRead, FailedReadClosesFileAndPreservesOldList)
{
	m_Operations.m_NullRead = true;
	EXPECT_FALSE(Read());
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(QmLocalSaveDisplayRead, OpenPreparationExceptionPreservesOldList)
{
	m_Operations.m_ThrowOpen = true;
	EXPECT_FALSE(Read());
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(QmLocalSaveDisplayRead, StreamErrorRejectsAllocatedContent)
{
	m_Operations.m_ReadError = true;
	EXPECT_FALSE(Read());
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Frees, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(QmLocalSaveDisplayRead, CloseFailureRejectsContentWithoutDoubleClose)
{
	m_Operations.m_CloseFailure = true;
	EXPECT_FALSE(Read());
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Frees, 1);
}

TEST_F(QmLocalSaveDisplayRead, ReadExceptionClosesFileAndRetainsOldList)
{
	m_Operations.m_ThrowRead = true;
	EXPECT_FALSE(Read());
	ExpectOld();
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(QmLocalSaveDisplayAllocation, EveryCandidateAllocationFailurePreservesOldListAndAllowsRetry)
{
	CheckAllocationFailures();
}

TEST_F(QmLocalSaveDisplayAllocation, NullAllocationReleasesContentAndAllowsRetry)
{
	m_Memory.m_ReturnNull = true;
	CheckAllocationFailures();
}

TEST_F(QmLocalSaveDisplayAllocation, UnknownAllocationExceptionReleasesContentAndAllowsRetry)
{
	m_Memory.m_ThrowUnknown = true;
	CheckAllocationFailures();
}
