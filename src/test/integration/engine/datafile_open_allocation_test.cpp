#include <base/borrowed_allocator.h>

#include <engine/shared/datafile.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <memory>
#include <new>

namespace
{
	class CValidationMemory : public IAllocationResource
	{
		struct CBlock
		{
			void *m_pData = nullptr;
			size_t m_Bytes = 0;
			size_t m_Alignment = 0;
		};
		std::array<CBlock, 64> m_aBlocks{};

	public:
		int m_Allocations = 0;
		int m_Live = 0;
		int m_FailAt = -1;
		bool m_ReturnNull = false;
		bool m_ThrowUnknown = false;
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			++m_Allocations;
			if(m_Allocations == m_FailAt)
			{
				if(m_ReturnNull)
					return nullptr;
				if(m_ThrowUnknown)
					throw 7;
				throw std::bad_alloc();
			}
			for(auto &Block : m_aBlocks)
			{
				if(Block.m_pData)
					continue;
				Block = {AllocateDefault(Bytes, Alignment), Bytes, Alignment};
				++m_Live;
				return Block.m_pData;
			}
			throw std::bad_alloc();
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			for(auto &Block : m_aBlocks)
			{
				if(Block.m_pData != pData)
					continue;
				EXPECT_EQ(Bytes, Block.m_Bytes);
				EXPECT_EQ(Alignment, Block.m_Alignment);
				DeallocateDefault(pData, Block.m_Bytes, Block.m_Alignment);
				Block = {};
				--m_Live;
				return;
			}
			ADD_FAILURE() << "validation released an unowned block";
		}
	};

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
	CValidationMemory Memory;
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
	CValidationMemory Baseline;
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Baseline));
	Reader.Close();
	ASSERT_GT(Baseline.m_Allocations, 0);
	ASSERT_EQ(Baseline.m_Live, 0);
	// 分配次数来自本机标准容器的成功调用，不依赖节点/桶布局或具体标准库。
	for(int FailAt = 1; FailAt <= Baseline.m_Allocations; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		CValidationMemory Memory;
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
	CValidationMemory Memory;
	Memory.m_FailAt = 1;
	Memory.m_ReturnNull = true;
	CDataFileReader Reader;
	EXPECT_FALSE(Reader.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_FALSE(Reader.IsOpen());
	EXPECT_EQ(Memory.m_Live, 0);
}

TEST_F(DatafileOpenAllocation, UnknownAllocatorExceptionIsContained)
{
	CValidationMemory Memory;
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
	CValidationMemory Memory;
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
	CValidationMemory Memory;
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
	CValidationMemory Memory;
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
		CValidationMemory Memory;
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
	CValidationMemory Memory;
	ASSERT_TRUE(Default.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Custom.Open(m_pStorage.get(), "valid.map", IStorage::TYPE_SAVE, &Memory));
	EXPECT_EQ(Default.Size(), Custom.Size());
	EXPECT_EQ(Default.Crc(), Custom.Crc());
	EXPECT_EQ(Default.Sha256(), Custom.Sha256());
	ExpectContents(Custom);
}
