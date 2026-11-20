#include <engine/shared/datafile.h>

#include <gtest/gtest.h>
#include <test/support/tracked_allocation_resource.h>
#include <test/test.h>

#include <memory>

namespace
{

	class DatafileOpenAllocation : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void Write(const char *pFilename, bool DuplicateId = false)
		{
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), pFilename));
			ASSERT_EQ(Writer.AddDataString("payload"), 0);
			const int Value = 42;
			for(int Id = 0; Id < 3; ++Id)
				ASSERT_GE(Writer.AddItem(11, DuplicateId ? 0 : Id, sizeof(Value), &Value), 0);
			for(int Id = 0; Id < 2; ++Id)
				ASSERT_GE(Writer.AddItem(22, Id, sizeof(Value), &Value), 0);
			ASSERT_TRUE(Writer.Finish());
		}
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_NO_FATAL_FAILURE(Write("valid.map"));
		}
		void ExpectContents(CDataFileReader &Reader)
		{
			EXPECT_EQ(Reader.NumItems(), 5);
			EXPECT_EQ(Reader.NumData(), 1);
			EXPECT_STREQ(Reader.GetDataString(0), "payload");
			const auto *pItem = static_cast<const int *>(Reader.FindItem(22, 1));
			ASSERT_NE(pItem, nullptr);
			EXPECT_EQ(*pItem, 42);
		}
	};
}

TEST_F(DatafileOpenAllocation, DefaultAllocationPreservesValidItemsAndData)
{
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE));
	ExpectContents(Reader);
}

TEST_F(DatafileOpenAllocation, SuccessfulValidationReleasesAllBorrowedScratchBeforeReturning)
{
	CTrackedAllocationResource Memory;
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_GT(Memory.m_Allocations, 0);
	EXPECT_EQ(Memory.m_Live, 0);
	ExpectContents(Reader);
	Reader.Close();
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST_F(DatafileOpenAllocation, EveryValidationAllocationFailureReturnsFalseAndCanRetry)
{
	CTrackedAllocationResource Baseline;
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Baseline));
	Reader.Close();
	ASSERT_GT(Baseline.m_Allocations, 0);
	ASSERT_EQ(Baseline.m_Live, 0);
	// 分配次数来自本机标准容器的成功调用，不依赖节点/桶布局或具体标准库。
	for(int FailAt = 1; FailAt <= Baseline.m_Allocations; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		CTrackedAllocationResource Memory;
		Memory.m_FailAt = FailAt;
		bool Opened = true;
		EXPECT_NO_THROW(Opened = Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
		ASSERT_FALSE(Opened);
		ASSERT_FALSE(Reader.IsOpen());
		EXPECT_EQ(Memory.m_Allocations, FailAt);
		EXPECT_EQ(Memory.m_Live, 0);
		Memory.m_FailAt = -1;
		ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
		EXPECT_EQ(Memory.m_Live, 0);
		ExpectContents(Reader);
		Reader.Close();
	}
}

TEST_F(DatafileOpenAllocation, NullAllocationResultIsAClosedReaderFailure)
{
	CTrackedAllocationResource Memory;
	Memory.m_FailAt = 1;
	Memory.m_ReturnNull = true;
	CDataFileReader Reader;
	EXPECT_FALSE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_FALSE(Reader.IsOpen());
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST_F(DatafileOpenAllocation, UnknownAllocatorExceptionIsContained)
{
	CTrackedAllocationResource Memory;
	Memory.m_FailAt = 1;
	Memory.m_ThrowUnknown = true;
	CDataFileReader Reader;
	EXPECT_FALSE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_FALSE(Reader.IsOpen());
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST_F(DatafileOpenAllocation, DuplicateIdsStillFailAndReleaseValidationScratch)
{
	ASSERT_NO_FATAL_FAILURE(Write("duplicate.map", true));
	CTrackedAllocationResource Memory;
	CDataFileReader Reader;
	EXPECT_FALSE(Reader.Open(m_pStorage.get(), "duplicate.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_FALSE(Reader.IsOpen());
	EXPECT_GT(Memory.m_Allocations, 0);
	EXPECT_EQ(Memory.m_Live, 0);
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	ExpectContents(Reader);
}

TEST_F(DatafileOpenAllocation, TruncatedHeaderFailsBeforeValidationAndCanRetry)
{
	IOHANDLE File = m_pStorage->OpenFile("truncated.map", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	EXPECT_EQ(io_write(File, "DATA", 4), 4);
	EXPECT_EQ(io_close(File), 0);
	CTrackedAllocationResource Memory;
	CDataFileReader Reader;
	EXPECT_FALSE(Reader.Open(m_pStorage.get(), "truncated.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_FALSE(Reader.IsOpen());
	EXPECT_EQ(Memory.m_Allocations, 0);
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	ExpectContents(Reader);
}

TEST_F(DatafileOpenAllocation, EmptyDatafileRemainsValidWithBorrowedScratch)
{
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "empty.map"));
	ASSERT_TRUE(Writer.Finish());
	CTrackedAllocationResource Memory;
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "empty.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Reader.NumItems(), 0);
	EXPECT_EQ(Reader.NumData(), 0);
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST_F(DatafileOpenAllocation, ValidationResourceCanBeDestroyedBeforeReader)
{
	CDataFileReader Reader;
	{
		CTrackedAllocationResource Memory;
		ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
		EXPECT_EQ(Memory.m_Live, 0);
	}
	ExpectContents(Reader);
	Reader.Close();
	EXPECT_FALSE(Reader.IsOpen());
}

TEST_F(DatafileOpenAllocation, CustomAndDefaultValidationProduceTheSameFileIdentity)
{
	CDataFileReader Default, Custom;
	CTrackedAllocationResource Memory;
	ASSERT_TRUE(Default.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Custom.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Default.Size(), Custom.Size());
	EXPECT_EQ(Default.Crc(), Custom.Crc());
	EXPECT_EQ(Default.Sha256(), Custom.Sha256());
	ExpectContents(Custom);
}
