#include <engine/shared/datafile.h>

#include <game/mapitems.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace
{
	class DatafileStringProcessor : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CDataFileReader m_Reader;
		int m_DataIndex = -1;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CDataFileWriter Writer;
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "processed_string.map"));
			CMapItemVersion Version{};
			Version.m_Version = 1;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Version), &Version);
			m_DataIndex = Writer.AddDataString("word");
			ASSERT_GE(m_DataIndex, 0);
			Writer.Finish();
			ASSERT_TRUE(m_Reader.Open(m_pStorage.get(), "processed_string.map", IStorage::TYPE_SAVE));
		}

		static std::pair<void *, size_t> ReplaceData(void *pOldData, const char *pText, size_t Size)
		{
			void *pNewData = std::malloc(Size);
			if(pNewData != nullptr)
				std::memcpy(pNewData, pText, Size);
			// 数据处理器接管旧缓冲，reader 只回收返回的新缓冲。
			std::free(pOldData);
			return {pNewData, pNewData == nullptr ? 0 : Size};
		}
	};
}

TEST_F(DatafileStringProcessor, ShorterLogicalDataCannotUseTheOldTerminator)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		return std::pair<void *, size_t>{pData, sizeof("word") - 1};
	});
	EXPECT_EQ(m_Reader.GetDataString(m_DataIndex), nullptr);
}

TEST_F(DatafileStringProcessor, SmallerReplacementUsesItsOwnTerminator)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		const char aText[] = "x";
		return ReplaceData(pData, aText, sizeof(aText));
	});
	EXPECT_STREQ(m_Reader.GetDataString(m_DataIndex), "x");
	EXPECT_EQ(m_Reader.GetDataSize(m_DataIndex), 2);
}

TEST_F(DatafileStringProcessor, LargerReplacementUsesItsFullCurrentLength)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		const char aText[] = "expanded";
		return ReplaceData(pData, aText, sizeof(aText));
	});
	EXPECT_STREQ(m_Reader.GetDataString(m_DataIndex), "expanded");
}

TEST_F(DatafileStringProcessor, EmptyProcessedBlockIsNotAString)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		return std::pair<void *, size_t>{pData, 0};
	});
	EXPECT_EQ(m_Reader.GetDataString(m_DataIndex), nullptr);
}

TEST_F(DatafileStringProcessor, NullProcessedResultIsRejected)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		std::free(pData);
		return std::pair<void *, size_t>{nullptr, 0};
	});
	EXPECT_EQ(m_Reader.GetDataString(m_DataIndex), nullptr);
}

TEST_F(DatafileStringProcessor, SingleTerminatorRemainsAValidEmptyString)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		const char aText[] = "";
		return ReplaceData(pData, aText, sizeof(aText));
	});
	EXPECT_STREQ(m_Reader.GetDataString(m_DataIndex), "");
}

TEST_F(DatafileStringProcessor, MultipleProcessedStringsAreStillRejected)
{
	m_Reader.AddDataProcessor(m_DataIndex, [](void *pData, size_t) {
		const char aText[] = "one\0two";
		return ReplaceData(pData, aText, sizeof(aText));
	});
	EXPECT_EQ(m_Reader.GetDataString(m_DataIndex), nullptr);
}

TEST_F(DatafileStringProcessor, UnloadAndRereadValidateTheNewProcessedLengthAgain)
{
	int Reads = 0;
	m_Reader.AddDataProcessor(m_DataIndex, [&](void *pData, size_t) {
		++Reads;
		const char aText[] = "expanded";
		return ReplaceData(pData, aText, sizeof(aText));
	});
	EXPECT_STREQ(m_Reader.GetDataString(m_DataIndex), "expanded");
	m_Reader.UnloadData(m_DataIndex);
	EXPECT_STREQ(m_Reader.GetDataString(m_DataIndex), "expanded");
	EXPECT_EQ(Reads, 2);
}
