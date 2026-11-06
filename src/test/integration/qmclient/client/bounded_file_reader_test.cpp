#include <base/borrowed_allocator.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/bounded_file_reader.h>

#include <gtest/gtest.h>
#include <test/support/rejectable_allocation_resource.h>
#include <test/test.h>

namespace
{
	class CQmBoundedFileReaderTest : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void Write(const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile("cache.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Text.data(), (unsigned)Text.size()), Text.size());
			io_close(File);
		}
		bool Read(size_t Limit, std::string &Text)
		{
			return QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), Limit, Text);
		}
	};
}

TEST_F(CQmBoundedFileReaderTest, ReadsExactBudgetAndPublishesCompleteContents)
{
	Write("12345678");
	std::string Text = "old";
	ASSERT_TRUE(Read(8, Text));
	EXPECT_EQ(Text, "12345678");
}

TEST_F(CQmBoundedFileReaderTest, RejectsOverBudgetAndRecoversAfterFileIsReplaced)
{
	Write("123456789");
	std::string Text = "old";
	EXPECT_FALSE(Read(8, Text));
	EXPECT_TRUE(Text.empty());
	Write("valid");
	ASSERT_TRUE(Read(8, Text));
	EXPECT_EQ(Text, "valid");
}

TEST_F(CQmBoundedFileReaderTest, RejectsEmptyAndMissingFilesWithoutStaleOutput)
{
	std::string Text = "old";
	EXPECT_FALSE(Read(8, Text));
	EXPECT_TRUE(Text.empty());
	Write("");
	Text = "old";
	EXPECT_FALSE(Read(8, Text));
	EXPECT_TRUE(Text.empty());
}

TEST_F(CQmBoundedFileReaderTest, BinaryBufferPreservesNullBytesAtExactBudget)
{
	const std::string Bytes("a\0b\xff", 4);
	ASSERT_NO_FATAL_FAILURE(Write(Bytes));
	std::vector<uint8_t> vBytes{99};
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 4, vBytes));
	EXPECT_EQ(vBytes, (std::vector<uint8_t>{'a', 0, 'b', 255}));
}

TEST_F(CQmBoundedFileReaderTest, BinaryOverBudgetClearsStaleBytesAndLaterReadRecovers)
{
	ASSERT_NO_FATAL_FAILURE(Write("123456789"));
	std::vector<uint8_t> vBytes{99};
	EXPECT_FALSE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_TRUE(vBytes.empty());
	ASSERT_NO_FATAL_FAILURE(Write("valid"));
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_EQ(vBytes, (std::vector<uint8_t>{'v', 'a', 'l', 'i', 'd'}));
}

TEST_F(CQmBoundedFileReaderTest, BinaryAllocationFailureDiscardsOutputAndSameBufferCanRecover)
{
	ASSERT_NO_FATAL_FAILURE(Write("12345678"));
	CRejectableAllocationResource Memory;
	std::vector<uint8_t, CBorrowedAllocator<uint8_t>> vBytes(&Memory);
	vBytes.push_back(99);
	Memory.m_Reject = true;
	EXPECT_FALSE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_TRUE(vBytes.empty());
	Memory.m_Reject = false;
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_EQ(std::vector<uint8_t>(vBytes.begin(), vBytes.end()), (std::vector<uint8_t>{'1', '2', '3', '4', '5', '6', '7', '8'}));
}

TEST_F(CQmBoundedFileReaderTest, BinaryShortReadDoesNotPublishPrefix)
{
	ASSERT_NO_FATAL_FAILURE(Write("12345678"));
	IOHANDLE File = m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	class CAdvanceReadMemory : public IAllocationResource
	{
		IOHANDLE m_File;
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			// 在候选分配边界推进真实流，确定性地产生长度检查后的短读。
			uint8_t Byte;
			m_Consumed += io_read(m_File, &Byte, 1);
			return AllocateDefault(Bytes, Alignment);
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override { DeallocateDefault(pData, Bytes, Alignment); }

	public:
		unsigned m_Consumed = 0;
		explicit CAdvanceReadMemory(IOHANDLE File) :
			m_File(File) {}
	} Memory(File);
	std::vector<uint8_t, CBorrowedAllocator<uint8_t>> vBytes(&Memory);
	EXPECT_FALSE(QmReadFileBounded(File, 8, vBytes));
	EXPECT_EQ(Memory.m_Consumed, 1u);
	EXPECT_TRUE(vBytes.empty());
}

TEST_F(CQmBoundedFileReaderTest, MissingAndEmptyBinaryFilesDoNotRetainPreviousBytes)
{
	std::vector<uint8_t> vBytes{99};
	EXPECT_FALSE(QmReadFileBounded(nullptr, 8, vBytes));
	EXPECT_TRUE(vBytes.empty());
	ASSERT_NO_FATAL_FAILURE(Write(""));
	vBytes.push_back(99);
	EXPECT_FALSE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_TRUE(vBytes.empty());
}
