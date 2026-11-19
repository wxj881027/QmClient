#include <game/client/components/qmclient/map_history_storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <new>
#include <string>

namespace
{
	using EResult = qm_map_history_storage::ELoadResult;
	class CReadFaults : public qm_map_history_storage::CFileOperations
	{
	public:
		bool m_Throw = false;
		bool m_Unsupported = false;
		int m_Reads = 0;
		IStorage::EReadFileResult Read(IStorage &Storage, void **ppData, unsigned *pSize) override
		{
			++m_Reads;
			if(m_Throw)
				throw std::bad_alloc();
			if(m_Unsupported)
				return IStorage::EReadFileResult::UNSUPPORTED;
			return CFileOperations::Read(Storage, ppData, pSize);
		}
	};

	class CQmMapHistoryLoadTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		QmMapHistory::CMapHistory m_History;
		CReadFaults m_Operations;
		bool m_Dirty = true;
		char m_aError[128]{};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			m_History.RecordVisit("Keep", "keep", 1, "2026-10-31");
			m_History.MarkFinished("keep", 100, 200);
			m_History.RecordVisit("Other", "other", 2, "2026-10-31");
			Write(R"({"version":1,"entries":[{"map_name":"Loaded","map_id":"loaded","deaths":4}]})");
		}
		void Write(const std::string &Data)
		{
			IOHANDLE File = m_pStorage->OpenFile(qm_map_history_storage::FILE_PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Data.data(), static_cast<unsigned>(Data.size())), Data.size());
			EXPECT_EQ(io_close(File), 0);
		}
		EResult Load(int Limit = 1)
		{
			return qm_map_history_storage::Load(m_pStorage.get(), m_History, Limit, m_Dirty, m_aError, sizeof(m_aError), &m_Operations);
		}
		void ExpectPreserved(EResult Expected, bool Dirty = true)
		{
			m_Dirty = Dirty;
			const std::string Before = m_History.ToJson();
			const auto *pKeep = m_History.Find("keep");
			EXPECT_EQ(Load(), Expected);
			EXPECT_EQ(m_History.ToJson(), Before);
			EXPECT_EQ(m_History.Find("keep"), pKeep);
			EXPECT_EQ(m_Dirty, Dirty);
		}
	};
}

TEST_F(CQmMapHistoryLoadTest, CompleteLoadReplacesTheOldCollectionAndClearsDirtyWhenNoTrimIsNeeded)
{
	ASSERT_EQ(Load(), EResult::SUCCESS);
	ASSERT_EQ(m_History.Size(), 1u);
	const auto *pLoaded = m_History.Find("loaded");
	ASSERT_NE(pLoaded, nullptr);
	EXPECT_EQ(pLoaded->m_MapName, "Loaded");
	EXPECT_EQ(pLoaded->m_DeathCount, 4);
	EXPECT_EQ(m_History.Find("keep"), nullptr);
	EXPECT_FALSE(m_Dirty);
}

TEST_F(CQmMapHistoryLoadTest, SuccessfulLoadAppliesTheExistingFinishedFirstLimitAndMarksDirty)
{
	Write(R"({"entries":[
		{"map_name":"Finished","map_id":"finished","last_entered":100,"finished":true},
		{"map_name":"Unfinished","map_id":"unfinished","last_entered":50}
	]})");
	ASSERT_EQ(Load(), EResult::SUCCESS);
	EXPECT_EQ(m_History.Size(), 1u);
	EXPECT_EQ(m_History.Find("finished"), nullptr);
	EXPECT_NE(m_History.Find("unfinished"), nullptr);
	EXPECT_TRUE(m_Dirty);
}

TEST_F(CQmMapHistoryLoadTest, MissingFilePreservesBothCleanAndDirtyModels)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(qm_map_history_storage::FILE_PATH, IStorage::TYPE_SAVE));
	ExpectPreserved(EResult::NOT_FOUND, false);
	ExpectPreserved(EResult::NOT_FOUND, true);
}

TEST_F(CQmMapHistoryLoadTest, InvalidJsonDoesNotTrimOrClearThePendingOldCollection)
{
	Write("{invalid");
	ExpectPreserved(EResult::PARSE_FAILED);
	EXPECT_NE(m_aError[0], '\0');
}

TEST_F(CQmMapHistoryLoadTest, RawNullByteIsRejectedWithoutParsingTheValidPrefix)
{
	std::string Data = R"({"entries":[]})";
	Data.push_back('\0');
	Data += "trailing data";
	Write(Data);
	ExpectPreserved(EResult::READ_FAILED);
}

TEST_F(CQmMapHistoryLoadTest, DirectoryAtTheFilePathIsAnIoFailureAndPreservesHistory)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(qm_map_history_storage::FILE_PATH, IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder(qm_map_history_storage::FILE_PATH, IStorage::TYPE_SAVE));
	ExpectPreserved(EResult::READ_FAILED);
}

TEST_F(CQmMapHistoryLoadTest, ReadBoundaryAllocationExceptionPreservesTheOldCollection)
{
	m_Operations.m_Throw = true;
	ExpectPreserved(EResult::READ_FAILED);
	EXPECT_EQ(m_Operations.m_Reads, 1);
}

TEST_F(CQmMapHistoryLoadTest, UnsupportedCheckedReadDoesNotFallBackToAnUncheckedPath)
{
	m_Operations.m_Unsupported = true;
	ExpectPreserved(EResult::READ_FAILED);
	EXPECT_EQ(m_Operations.m_Reads, 1);
}

TEST_F(CQmMapHistoryLoadTest, MissingStorageDoesNotChangeTheHistoryOrDirtyFlag)
{
	const std::string Before = m_History.ToJson();
	EXPECT_EQ(qm_map_history_storage::Load(nullptr, m_History, 1, m_Dirty, m_aError, sizeof(m_aError)), EResult::READ_FAILED);
	EXPECT_EQ(m_History.ToJson(), Before);
	EXPECT_TRUE(m_Dirty);
}

TEST_F(CQmMapHistoryLoadTest, EscapedNullFieldsAreHandledByTheModelWithoutLosingValidRecords)
{
	Write(R"({"entries":[
		{"map_name":"Real","map_id":"a","deaths":3,"last_played_date":"2026\u0000suffix"},
		{"map_name":"Injected","map_id":"a\u0000suffix","deaths":99}
	]})");
	ASSERT_EQ(Load(), EResult::SUCCESS);
	ASSERT_EQ(m_History.Size(), 1u);
	const auto *pReal = m_History.Find("a");
	ASSERT_NE(pReal, nullptr);
	EXPECT_EQ(pReal->m_MapName, "Real");
	EXPECT_EQ(pReal->m_DeathCount, 3);
	EXPECT_TRUE(pReal->m_LastPlayedDate.empty());
	EXPECT_FALSE(m_Dirty);
}
