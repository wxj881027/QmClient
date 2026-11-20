#include <game/client/components/qmclient/bg_draw_persistence.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace
{
	struct SStroke
	{
		CBgDrawItemData m_Data;
		const CBgDrawItemData &Data() const { return m_Data; }
	};

	class CFileFaults : public qm_bg_draw_persistence::CFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			WRITE,
			WRITE_THROW,
			SYNC,
			ERROR_FLAG,
			CLOSE,
			PUBLISH,
			PUBLISH_THROW,
			READ_THROW,
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Closes = 0, m_Publishes = 0, m_Reads = 0;
		bool m_PublishedAfterClose = false;
		bool Write(IOHANDLE File, const CBgDrawItemData &Data) override
		{
			if(m_Failure == EFailure::WRITE || m_Failure == EFailure::WRITE_THROW)
			{
				io_write(File, "partial", 7);
				if(m_Failure == EFailure::WRITE_THROW)
					throw std::bad_alloc();
				return false;
			}
			return CFileOperations::Write(File, Data);
		}
		bool Read(IOHANDLE File, CBgDrawItemData &Data) override
		{
			++m_Reads;
			if(m_Failure == EFailure::READ_THROW && m_Reads == 2)
				throw std::bad_alloc();
			return CFileOperations::Read(File, Data);
		}
		int Sync(IOHANDLE File) override
		{
			const int Result = CFileOperations::Sync(File);
			return m_Failure == EFailure::SYNC ? -1 : Result;
		}
		int Error(IOHANDLE File) override
		{
			const int Result = CFileOperations::Error(File);
			return m_Failure == EFailure::ERROR_FLAG ? 1 : Result;
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_Failure == EFailure::CLOSE ? -1 : Result;
		}
		bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget) override
		{
			++m_Publishes;
			m_PublishedAfterClose = m_Closes > 0;
			if(m_Failure == EFailure::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Failure != EFailure::PUBLISH && CFileOperations::Publish(Storage, pTemporary, pTarget);
		}
	};

	class CQmBgDrawPersistenceTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CFileFaults m_Operations;
		std::vector<SStroke> m_Items;
		const std::string m_Old = "7,8,2,1,0,0,1\n,\n";

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("bgdraw", IStorage::TYPE_SAVE));
			Store("bgdraw/saved.csv", m_Old);
			m_Items.push_back({CBgDrawItemData{{1, 2, 3, 1, 0, 0, 1}, {2, 3, 4, 0, 1, 0, 1}}});
			m_Items.push_back({CBgDrawItemData{{10, 20, 5, 0, 0, 1, 1}}});
		}
		void Store(const char *pPath, const std::string &Content)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), Content.size()), Content.size());
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Contents(const char *pPath)
		{
			void *pRaw = nullptr;
			unsigned Size = 0;
			EXPECT_TRUE(m_pStorage->ReadFile(pPath, IStorage::TYPE_SAVE, &pRaw, &Size));
			const std::unique_ptr<void, decltype(&free)> Data(pRaw, free);
			return pRaw ? std::string(static_cast<const char *>(pRaw), Size) : "";
		}
		auto Save()
		{
			return qm_bg_draw_persistence::Save(m_pStorage.get(), "bgdraw/saved.csv", m_Items, &m_Operations);
		}
		void ExpectOldFile(bool CandidateAbsent = true)
		{
			EXPECT_EQ(Contents("bgdraw/saved.csv"), m_Old);
			if(CandidateAbsent)
				EXPECT_FALSE(m_pStorage->FileExists(qm_bg_draw_persistence::TemporaryPath("bgdraw/saved.csv").c_str(), IStorage::TYPE_SAVE));
		}
		void FailedSave(CFileFaults::EFailure Failure, int Publishes = 0)
		{
			m_Operations.m_Failure = Failure;
			EXPECT_FALSE(Save().m_Success);
			ExpectOldFile();
			EXPECT_EQ(m_Operations.m_Closes, 1);
			EXPECT_EQ(m_Operations.m_Publishes, Publishes);
		}
		void PreparePriorOutput(qm_bg_draw_persistence::SLoadedData &Output)
		{
			Output.m_Queue.push_back(CBgDrawItemData{{99, 98, 1, 1, 1, 1, 1}});
			Output.m_Loaded = 77;
			Output.m_Discarded = 5;
		}
		void ExpectPriorOutput(const qm_bg_draw_persistence::SLoadedData &Output)
		{
			ASSERT_EQ(Output.m_Queue.size(), 1u);
			ASSERT_EQ(Output.m_Queue.front().size(), 1u);
			EXPECT_FLOAT_EQ(Output.m_Queue.front().front().x, 99);
			EXPECT_EQ(Output.m_Loaded, 77);
			EXPECT_EQ(Output.m_Discarded, 5);
		}
	};
}

TEST_F(CQmBgDrawPersistenceTest, SaveClosesBeforeReplacementAndRoundTripsTheExistingCsv)
{
	const auto Result = Save();
	ASSERT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_Written, 2);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 1);
	EXPECT_TRUE(m_Operations.m_PublishedAfterClose);
	qm_bg_draw_persistence::SLoadedData Output;
	ASSERT_TRUE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/saved.csv", 2, Output));
	ASSERT_EQ(Output.m_Queue.size(), 2u);
	ASSERT_EQ(Output.m_Queue.front().size(), 2u);
	EXPECT_FLOAT_EQ(Output.m_Queue.front().back().x, 2);
	EXPECT_FLOAT_EQ(Output.m_Queue.back().front().x, 10);
	EXPECT_EQ(Output.m_Loaded, 2);
	EXPECT_EQ(Output.m_Discarded, 0);
}

TEST_F(CQmBgDrawPersistenceTest, EmptyDrawingPublishesAnEmptyFile)
{
	m_Items.clear();
	const auto Result = Save();
	ASSERT_TRUE(Result.m_Success);
	EXPECT_EQ(Result.m_Written, 0);
	EXPECT_TRUE(Contents("bgdraw/saved.csv").empty());
}

TEST_F(CQmBgDrawPersistenceTest, FirstSaveCreatesACompleteFileWithoutChangingOtherDrawings)
{
	ASSERT_TRUE(qm_bg_draw_persistence::Save(m_pStorage.get(), "bgdraw/created.csv", m_Items, &m_Operations).m_Success);
	qm_bg_draw_persistence::SLoadedData Output;
	ASSERT_TRUE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/created.csv", 2, Output));
	EXPECT_EQ(Output.m_Loaded, 2);
	ExpectOldFile();
}

TEST_F(CQmBgDrawPersistenceTest, EmptyInputSuccessfullyReplacesPriorParsedOutput)
{
	Store("bgdraw/empty.csv", "");
	qm_bg_draw_persistence::SLoadedData Output;
	PreparePriorOutput(Output);
	ASSERT_TRUE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/empty.csv", 2, Output));
	EXPECT_TRUE(Output.m_Queue.empty());
	EXPECT_EQ(Output.m_Loaded, 0);
	EXPECT_EQ(Output.m_Discarded, 0);
}

TEST_F(CQmBgDrawPersistenceTest, ShortWritePreservesOldFileAndAllowsRetry)
{
	m_Operations.m_Failure = CFileFaults::EFailure::WRITE;
	const auto Result = Save();
	EXPECT_FALSE(Result.m_Success);
	EXPECT_TRUE(Result.m_WriteFailed);
	EXPECT_EQ(Result.m_Written, 0);
	ExpectOldFile();
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 0);
	m_Operations.m_Failure = CFileFaults::EFailure::NONE;
	EXPECT_TRUE(Save().m_Success);
}

TEST_F(CQmBgDrawPersistenceTest, WriteExceptionClosesCandidateAndPreservesOldFile)
{
	FailedSave(CFileFaults::EFailure::WRITE_THROW);
}

TEST_F(CQmBgDrawPersistenceTest, SyncFailureDoesNotReplaceTheOldFile)
{
	FailedSave(CFileFaults::EFailure::SYNC);
}

TEST_F(CQmBgDrawPersistenceTest, StreamErrorDoesNotReplaceTheOldFile)
{
	FailedSave(CFileFaults::EFailure::ERROR_FLAG);
}

TEST_F(CQmBgDrawPersistenceTest, CloseFailureDoesNotReplaceTheOldFile)
{
	FailedSave(CFileFaults::EFailure::CLOSE);
}

TEST_F(CQmBgDrawPersistenceTest, ReplacementRefusalPreservesTheOldFile)
{
	FailedSave(CFileFaults::EFailure::PUBLISH, 1);
}

TEST_F(CQmBgDrawPersistenceTest, ReplacementExceptionPreservesTheOldFile)
{
	FailedSave(CFileFaults::EFailure::PUBLISH_THROW, 1);
}

TEST_F(CQmBgDrawPersistenceTest, ExistingCandidateIsNeitherTruncatedNorRemoved)
{
	const auto Path = qm_bg_draw_persistence::TemporaryPath("bgdraw/saved.csv");
	Store(Path.c_str(), "keep candidate");
	EXPECT_FALSE(Save().m_Success);
	ExpectOldFile(false);
	EXPECT_EQ(Contents(Path.c_str()), "keep candidate");
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(CQmBgDrawPersistenceTest, ReadKeepsTheNewestStrokesWithinTheExistingCapacity)
{
	ASSERT_TRUE(Save().m_Success);
	qm_bg_draw_persistence::SLoadedData Output;
	ASSERT_TRUE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/saved.csv", 1, Output));
	ASSERT_EQ(Output.m_Queue.size(), 1u);
	EXPECT_FLOAT_EQ(Output.m_Queue.front().front().x, 10);
	EXPECT_EQ(Output.m_Loaded, 2);
	EXPECT_EQ(Output.m_Discarded, 1);
}

TEST_F(CQmBgDrawPersistenceTest, DisabledCapacityReadsAndCountsWithoutKeepingStrokes)
{
	ASSERT_TRUE(Save().m_Success);
	qm_bg_draw_persistence::SLoadedData Output;
	ASSERT_TRUE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/saved.csv", 0, Output));
	EXPECT_TRUE(Output.m_Queue.empty());
	EXPECT_EQ(Output.m_Loaded, 2);
	EXPECT_EQ(Output.m_Discarded, 2);
}

TEST_F(CQmBgDrawPersistenceTest, ReadExceptionAfterOneStrokeClosesAndKeepsPriorOutput)
{
	ASSERT_TRUE(Save().m_Success);
	m_Operations.m_Failure = CFileFaults::EFailure::READ_THROW;
	m_Operations.m_Closes = 0;
	qm_bg_draw_persistence::SLoadedData Output;
	PreparePriorOutput(Output);
	EXPECT_FALSE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/saved.csv", 2, Output, &m_Operations));
	ExpectPriorOutput(Output);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CQmBgDrawPersistenceTest, ReadErrorFlagRejectsTheWholeResult)
{
	m_Operations.m_Failure = CFileFaults::EFailure::ERROR_FLAG;
	qm_bg_draw_persistence::SLoadedData Output;
	PreparePriorOutput(Output);
	EXPECT_FALSE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/saved.csv", 2, Output, &m_Operations));
	ExpectPriorOutput(Output);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CQmBgDrawPersistenceTest, ReadCloseFailureKeepsPriorOutput)
{
	m_Operations.m_Failure = CFileFaults::EFailure::CLOSE;
	qm_bg_draw_persistence::SLoadedData Output;
	PreparePriorOutput(Output);
	EXPECT_FALSE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/saved.csv", 2, Output, &m_Operations));
	ExpectPriorOutput(Output);
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(CQmBgDrawPersistenceTest, MissingInputKeepsPriorOutput)
{
	qm_bg_draw_persistence::SLoadedData Output;
	PreparePriorOutput(Output);
	EXPECT_FALSE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/missing.csv", 2, Output, &m_Operations));
	ExpectPriorOutput(Output);
	EXPECT_EQ(m_Operations.m_Closes, 0);
}

TEST_F(CQmBgDrawPersistenceTest, ParentTraversalIsRejectedBeforeFileOperations)
{
	EXPECT_FALSE(qm_bg_draw_persistence::Save(m_pStorage.get(), "bgdraw/../outside.csv", m_Items, &m_Operations).m_Success);
	qm_bg_draw_persistence::SLoadedData Output;
	PreparePriorOutput(Output);
	EXPECT_FALSE(qm_bg_draw_persistence::Read(m_pStorage.get(), "bgdraw/../outside.csv", 2, Output, &m_Operations));
	ExpectPriorOutput(Output);
	EXPECT_EQ(m_Operations.m_Closes, 0);
	ExpectOldFile();
}
