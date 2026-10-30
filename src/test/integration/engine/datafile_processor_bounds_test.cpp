#include <engine/shared/datafile.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>

namespace
{
	class DatafileProcessorBounds : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		int m_ProcessorCalls = 0;
		CDataFileReader m_Reader;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "processed.map"));
			const std::array<int, 2> aData{1, 2};
			ASSERT_EQ(Writer.AddData(sizeof(aData), aData.data()), 0);
			ASSERT_EQ(Writer.AddDataString("safe"), 1);
			ASSERT_TRUE(Writer.Finish());
			ASSERT_TRUE(m_Reader.Open(m_pStorage.get(), "processed.map", IStorage::TYPE_SAVE));
		}
		void SetReturnedLength(size_t Length)
		{
			// 注入非法长度元数据，保留真实小缓冲；不按伪造长度读写或分配。
			m_Reader.AddDataProcessor(0, [this, Length](void *pData, size_t) {
				++m_ProcessorCalls;
				return std::make_pair(pData, Length);
			});
		}
	};
}

TEST_F(DatafileProcessorBounds, LengthBeyondIntRangeRejectsTheReturnedBuffer)
{
	SetReturnedLength((size_t)std::numeric_limits<int>::max() + 1);
	EXPECT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileProcessorBounds, MaximumSizeTLengthCannotWrapIntoTheSizeCache)
{
	SetReturnedLength(std::numeric_limits<size_t>::max());
	EXPECT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileProcessorBounds, RejectedProcessedBlockStaysFailedAfterUnload)
{
	SetReturnedLength((size_t)std::numeric_limits<int>::max() + 1);
	ASSERT_EQ(m_Reader.GetData(0), nullptr);
	m_Reader.UnloadData(0);
	EXPECT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileProcessorBounds, ReopenDiscardsRejectedProcessorAndRestoresOriginalData)
{
	SetReturnedLength((size_t)std::numeric_limits<int>::max() + 1);
	ASSERT_EQ(m_Reader.GetData(0), nullptr);
	m_Reader.Close();
	ASSERT_TRUE(m_Reader.Open(m_pStorage.get(), "processed.map", IStorage::TYPE_SAVE));
	ASSERT_NE(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_Reader.GetDataSize(0), 2 * sizeof(int));
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileProcessorBounds, OversizedReplacementDoesNotInvalidateOtherDataBlocks)
{
	std::unique_ptr<void, decltype(&free)> Replacement(malloc(8), free);
	ASSERT_NE(Replacement, nullptr);
	m_Reader.AddDataProcessor(0, [&](void *pData, size_t) {
		free(pData);
		return std::make_pair(Replacement.release(), std::numeric_limits<size_t>::max());
	});
	ASSERT_EQ(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(Replacement, nullptr);
	EXPECT_STREQ(m_Reader.GetDataString(1), "safe");
	m_Reader.Close();
	EXPECT_FALSE(m_Reader.IsOpen());
}

TEST_F(DatafileProcessorBounds, SwappedReadUsesTheSameProcessedLengthLimit)
{
	SetReturnedLength((size_t)std::numeric_limits<int>::max() + 1);
	EXPECT_EQ(m_Reader.GetDataSwapped(0), nullptr);
	EXPECT_EQ(m_ProcessorCalls, 1);
}

TEST_F(DatafileProcessorBounds, ZeroLogicalLengthKeepsTheExistingRawBufferContract)
{
	SetReturnedLength(0);
	ASSERT_NE(m_Reader.GetData(0), nullptr);
	EXPECT_EQ(m_Reader.GetDataSize(0), 0);
	EXPECT_EQ(m_ProcessorCalls, 1);
}
