#include <base/borrowed_allocator.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/bounded_file_reader.h>

#include <gtest/gtest.h>
#include <test/support/rejectable_allocation_resource.h>
#include <test/test.h>

#include <cerrno>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#include <windows.h>

#include <io.h>
#elif defined(CONF_FAMILY_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
	class CFileMutationMemory : public IAllocationResource
	{
		IStorage &m_Storage;
		bool m_Append;

		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			void *pData = AllocateDefault(Bytes, Alignment);
			try
			{
				if(std::exchange(m_Armed, false))
				{
					// 长度已读取，候选内存已分配；在实际读取前修改同一个普通文件。
					std::unique_ptr<void, decltype(&io_close)> Writer(m_Storage.OpenFile("cache.json", m_Append ? IOFLAG_APPEND : IOFLAG_WRITE, IStorage::TYPE_SAVE), io_close);
					if(Writer)
					{
						const char *pText = m_Append ? "x" : "ab";
						const unsigned Length = m_Append ? 1 : 2;
						const bool Written = io_write(Writer.get(), pText, Length) == Length;
						m_Mutated = io_close(Writer.release()) == 0 && Written;
					}
				}
			}
			catch(...)
			{
				DeallocateDefault(pData, Bytes, Alignment);
				throw;
			}
			return pData;
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override { DeallocateDefault(pData, Bytes, Alignment); }

	public:
		bool m_Armed = false;
		bool m_Mutated = false;
		CFileMutationMemory(IStorage &Storage, bool Append) :
			m_Storage(Storage), m_Append(Append) {}
	};

	class CThrowingReadMemory : public IAllocationResource
	{
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_Failure == EFailure::LENGTH)
				throw std::length_error("controlled bounded read limit");
			if(m_Failure == EFailure::OTHER)
				throw std::logic_error("unrelated bounded read allocation failure");
			return AllocateDefault(Bytes, Alignment);
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override { DeallocateDefault(pData, Bytes, Alignment); }

	public:
		enum class EFailure
		{
			NONE,
			LENGTH,
			OTHER,
		};
		EFailure m_Failure = EFailure::NONE;
	};

#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_FAMILY_UNIX)
	intptr_t NativeFile(IOHANDLE File)
	{
#if defined(CONF_FAMILY_WINDOWS)
		return _get_osfhandle(_fileno(static_cast<FILE *>(File)));
#else
		return fileno(static_cast<FILE *>(File));
#endif
	}

	bool NativeFileClosed(intptr_t File)
	{
#if defined(CONF_FAMILY_WINDOWS)
		const DWORD PreviousError = GetLastError();
		DWORD Flags;
		const bool Closed = GetHandleInformation(reinterpret_cast<HANDLE>(File), &Flags) == FALSE && GetLastError() == ERROR_INVALID_HANDLE;
		SetLastError(PreviousError);
#else
		const int PreviousError = errno;
		errno = 0;
		const bool Closed = fcntl(static_cast<int>(File), F_GETFD) == -1 && errno == EBADF;
		errno = PreviousError;
#endif
		return Closed;
	}
#endif

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

TEST_F(CQmBoundedFileReaderTest, BinaryGrowthAfterLengthCheckRejectsThePrefixAndTheSameBufferCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(Write("12345678"));
	CFileMutationMemory Memory(*m_pStorage, true);
	std::vector<uint8_t, CBorrowedAllocator<uint8_t>> vBytes(&Memory);
	vBytes.push_back(99);
	Memory.m_Armed = true;
	EXPECT_FALSE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	ASSERT_TRUE(Memory.m_Mutated);
	EXPECT_TRUE(vBytes.empty());
	ASSERT_NO_FATAL_FAILURE(Write("ready"));
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_EQ(std::vector<uint8_t>(vBytes.begin(), vBytes.end()), (std::vector<uint8_t>{'r', 'e', 'a', 'd', 'y'}));
}

TEST_F(CQmBoundedFileReaderTest, BinaryTruncationAfterLengthCheckRejectsThePrefixAndCanReadTheNewFile)
{
	ASSERT_NO_FATAL_FAILURE(Write("12345678"));
	CFileMutationMemory Memory(*m_pStorage, false);
	std::vector<uint8_t, CBorrowedAllocator<uint8_t>> vBytes(&Memory);
	vBytes.push_back(99);
	Memory.m_Armed = true;
	EXPECT_FALSE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	ASSERT_TRUE(Memory.m_Mutated);
	EXPECT_TRUE(vBytes.empty());
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_EQ(std::vector<uint8_t>(vBytes.begin(), vBytes.end()), (std::vector<uint8_t>{'a', 'b'}));
}

#if defined(CONF_FAMILY_WINDOWS) || defined(CONF_FAMILY_UNIX)
TEST_F(CQmBoundedFileReaderTest, LengthExceptionClosesTheOwnedFileAndAllowsTheSameBufferToRetry)
{
	ASSERT_NO_FATAL_FAILURE(Write("12345678"));
	CThrowingReadMemory Memory;
	std::vector<uint8_t, CBorrowedAllocator<uint8_t>> vBytes(&Memory);
	vBytes.push_back(99);
	IOHANDLE File = m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const intptr_t Native = NativeFile(File);
	ASSERT_NE(Native, -1);
	Memory.m_Failure = CThrowingReadMemory::EFailure::LENGTH;
	const bool Read = QmReadFileBounded(File, 8, vBytes);
	const bool Closed = NativeFileClosed(Native);
	EXPECT_FALSE(Read);
	EXPECT_TRUE(Closed);
	EXPECT_TRUE(vBytes.empty());
	Memory.m_Failure = CThrowingReadMemory::EFailure::NONE;
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_EQ(std::vector<uint8_t>(vBytes.begin(), vBytes.end()), (std::vector<uint8_t>{'1', '2', '3', '4', '5', '6', '7', '8'}));
}

TEST_F(CQmBoundedFileReaderTest, UnknownExceptionPropagatesAfterClosingTheOwnedFileAndClearingOutput)
{
	ASSERT_NO_FATAL_FAILURE(Write("12345678"));
	CThrowingReadMemory Memory;
	std::vector<uint8_t, CBorrowedAllocator<uint8_t>> vBytes(&Memory);
	vBytes.push_back(99);
	IOHANDLE File = m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const intptr_t Native = NativeFile(File);
	ASSERT_NE(Native, -1);
	Memory.m_Failure = CThrowingReadMemory::EFailure::OTHER;
	bool Propagated = false;
	try
	{
		(void)QmReadFileBounded(File, 8, vBytes);
	}
	catch(const std::logic_error &)
	{
		Propagated = true;
	}
	const bool Closed = NativeFileClosed(Native);
	EXPECT_TRUE(Propagated);
	EXPECT_TRUE(Closed);
	EXPECT_TRUE(vBytes.empty());
	Memory.m_Failure = CThrowingReadMemory::EFailure::NONE;
	ASSERT_TRUE(QmReadFileBounded(m_pStorage->OpenFile("cache.json", IOFLAG_READ, IStorage::TYPE_SAVE), 8, vBytes));
	EXPECT_EQ(vBytes.size(), 8u);
}
#endif
