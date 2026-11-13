#include <engine/shared/datafile.h>

#include <gtest/gtest.h>
#include <test/support/datafile_reader_operations.h>
#include <test/support/datafile_wire_fixture.h>
#include <test/test.h>

#include <array>
#include <limits>
#include <memory>
#include <utility>

namespace
{
	class DatafileReaderLimits : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CTestDatafileReaderOperations m_Operations;
		CDataFileReader m_Reader;
		int m_ProcessorCalls = 0;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "small.map"));
			const std::array<unsigned char, 8> aData{1, 2, 3, 4, 5, 6, 7, 8};
			ASSERT_EQ(Writer.AddData(aData.size(), aData.data()), 0);
			ASSERT_EQ(Writer.AddDataString("safe"), 1);
			ASSERT_TRUE(Writer.Finish());
		}
		bool Open()
		{
			return m_Reader.Open(m_pStorage.get(), "small.map", IStorage::TYPE_SAVE, nullptr, &m_Operations);
		}
		void SetBudget(size_t Budget)
		{
			m_Reader.AddDataProcessor(0, Budget, [this](void *pData, size_t Size) {
				++m_ProcessorCalls;
				return std::make_pair(pData, Size);
			});
		}
	};
}

TEST_F(DatafileReaderLimits, NormalOperationsPreserveHashSizeAndLazyPayload)
{
	CDataFileReader Native;
	ASSERT_TRUE(Native.Open(m_pStorage.get(), "small.map", IStorage::TYPE_SAVE));
	ASSERT_TRUE(Open());
	EXPECT_EQ(m_Reader.Size(), Native.Size());
	EXPECT_EQ(m_Reader.Crc(), Native.Crc());
	const int Reads = m_Operations.m_Reads;
	EXPECT_STREQ(m_Reader.GetDataString(1), "safe");
	EXPECT_GT(m_Operations.m_Reads, Reads);
	EXPECT_EQ(m_Operations.m_RejectedAllocations, 0);
}

TEST_F(DatafileReaderLimits, UnrepresentableReportedLengthsRejectBeforeHashingAndCanRetry)
{
	m_Operations.m_OverrideLength = true;
	for(const int64_t Length : std::array<int64_t, 3>{-1, 35, (int64_t)std::numeric_limits<int>::max() + 1})
	{
		SCOPED_TRACE(Length);
		m_Operations.m_Length = Length;
		EXPECT_FALSE(Open());
		EXPECT_FALSE(m_Reader.IsOpen());
		EXPECT_EQ(m_Operations.m_Reads, 0);
		EXPECT_EQ(m_Operations.m_Allocations, 0);
	}
	m_Operations.m_OverrideLength = false;
	ASSERT_TRUE(Open());
	EXPECT_STREQ(m_Reader.GetDataString(1), "safe");
}

TEST_F(DatafileReaderLimits, ActualBytesDetermineIdentityWhenReportedLengthShrinks)
{
	CDataFileReader Native;
	ASSERT_TRUE(Native.Open(m_pStorage.get(), "small.map", IStorage::TYPE_SAVE));
	m_Operations.m_OverrideLength = true;
	m_Operations.m_Length = std::numeric_limits<int>::max();
	ASSERT_TRUE(Open());
	EXPECT_EQ(m_Reader.Size(), Native.Size());
	EXPECT_EQ(m_Reader.Crc(), Native.Crc());
	EXPECT_STREQ(m_Reader.GetDataString(1), "safe");
}

TEST_F(DatafileReaderLimits, ImpossibleReadCountFailsBeforeHashingUnwrittenBytes)
{
	m_Operations.m_InvalidReadCount = true;
	EXPECT_FALSE(Open());
	EXPECT_FALSE(m_Reader.IsOpen());
	EXPECT_EQ(m_Operations.m_Allocations, 0);
	m_Operations.m_InvalidReadCount = false;
	EXPECT_TRUE(Open());
}

TEST_F(DatafileReaderLimits, ShortHeaderReadFailsAndCanReopen)
{
	m_Operations.m_MaxRead = 8;
	EXPECT_FALSE(Open());
	EXPECT_FALSE(m_Reader.IsOpen());
	m_Operations.m_MaxRead = std::numeric_limits<unsigned>::max();
	EXPECT_TRUE(Open());
}

TEST_F(DatafileReaderLimits, MoveRetainsExternalOperationsForLazyData)
{
	CDataFileReader Destination;
	{
		CDataFileReader Source;
		ASSERT_TRUE(Source.Open(m_pStorage.get(), "small.map", IStorage::TYPE_SAVE, nullptr, &m_Operations));
		Destination = std::move(Source);
		EXPECT_FALSE(Source.IsOpen());
	}
	const int Reads = m_Operations.m_Reads;
	EXPECT_STREQ(Destination.GetDataString(1), "safe");
	EXPECT_GT(m_Operations.m_Reads, Reads);
}

TEST_F(DatafileReaderLimits, LegacyVersionFourSizeRepairKeepsExistingPayload)
{
	std::array<int, 9> aHeader{};
	ASSERT_NO_FATAL_FAILURE(ReadDatafileFixtureHeader(m_pStorage.get(), "small.map", aHeader));
	ASSERT_EQ(aHeader[1], 4);
	const int SizeTable = aHeader[6] * sizeof(int);
	ASSERT_NO_FATAL_FAILURE(PatchDatafileFixtureWord(m_pStorage.get(), "small.map", 2 * sizeof(int), aHeader[2] - SizeTable));
	ASSERT_NO_FATAL_FAILURE(PatchDatafileFixtureWord(m_pStorage.get(), "small.map", 3 * sizeof(int), aHeader[3] - SizeTable));
	ASSERT_TRUE(Open());
	EXPECT_STREQ(m_Reader.GetDataString(1), "safe");
}

TEST_F(DatafileReaderLimits, InputBeyondBudgetFailsBeforeRawReadsAllocationsOrProcessor)
{
	ASSERT_TRUE(Open());
	SetBudget(7);
	const int Reads = m_Operations.m_Reads;
	EXPECT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_Operations.m_Reads, Reads);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
	EXPECT_EQ(m_ProcessorCalls, 0);
	EXPECT_STREQ(m_Reader.GetDataString(1), "safe");
}

TEST_F(DatafileReaderLimits, ExactInputBudgetAllowsDataAndCachesProcessedResult)
{
	ASSERT_TRUE(Open());
	SetBudget(8);
	const auto *pBytes = static_cast<const unsigned char *>(m_Reader.GetData(0));
	ASSERT_NE(pBytes, nullptr);
	EXPECT_EQ(pBytes[7], 8);
	EXPECT_EQ(m_Reader.GetDataSize(0), 8);
	EXPECT_EQ(m_Reader.GetData(0), pBytes);
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileReaderLimits, InputBudgetFailureIsStickyAcrossUnload)
{
	ASSERT_TRUE(Open());
	SetBudget(0);
	ASSERT_EQ(m_Reader.GetData(0), nullptr);
	m_Reader.UnloadData(0);
	EXPECT_EQ(m_Reader.GetDataSwapped(0), nullptr);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
	EXPECT_EQ(m_ProcessorCalls, 0);
}

TEST_F(DatafileReaderLimits, ReopenDiscardsTheOldProcessorBudget)
{
	ASSERT_TRUE(Open());
	SetBudget(1);
	ASSERT_EQ(m_Reader.GetData(0), nullptr);
	m_Reader.Close();
	ASSERT_TRUE(Open());
	ASSERT_NE(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_Reader.GetDataSize(0), 8);
	EXPECT_EQ(m_ProcessorCalls, 0);
}

TEST_F(DatafileReaderLimits, ExistingProcessorEntryHasNoImplicitInputLimit)
{
	ASSERT_TRUE(Open());
	m_Reader.AddDataProcessor(0, [this](void *pData, size_t Size) {
		++m_ProcessorCalls;
		return std::make_pair(pData, Size);
	});
	EXPECT_NE(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileReaderLimits, VersionThreeRawBlocksUseTheirFileLengthForInputBudget)
{
	// v3: 36 字节头、一个 4 字节 offset、8 字节未压缩数据，无 sizes 表。
	std::array<int, 12> aWords{0, 3, 32, 24, 0, 0, 1, 0, 8, 0, 5, 6};
#if defined(CONF_ARCH_ENDIAN_BIG)
	swap_endian(aWords.data(), sizeof(int), aWords.size());
#endif
	mem_copy(aWords.data(), "DATA", 4);
	IOHANDLE File = m_pStorage->OpenFile("raw.map", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	const auto Written = io_write(File, aWords.data(), sizeof(aWords));
	const int Closed = io_close(File);
	ASSERT_EQ(Written, sizeof(aWords));
	ASSERT_EQ(Closed, 0);
	ASSERT_TRUE(m_Reader.Open(m_pStorage.get(), "raw.map", IStorage::TYPE_SAVE, nullptr, &m_Operations));
	SetBudget(7);
	const int Reads = m_Operations.m_Reads;
	EXPECT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_Operations.m_Reads, Reads);
	EXPECT_EQ(m_Operations.m_Allocations, 0);
	m_Reader.Close();
	ASSERT_TRUE(m_Reader.Open(m_pStorage.get(), "raw.map", IStorage::TYPE_SAVE, nullptr, &m_Operations));
	SetBudget(8);
	EXPECT_NE(m_Reader.GetDataSwapped(0), nullptr);
	EXPECT_EQ(m_Reader.GetDataSize(0), 8);
	EXPECT_EQ(m_ProcessorCalls, 1);
}
