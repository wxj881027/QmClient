#include <engine/gfx/image_loader.h>

#include <gtest/gtest.h>
#include <test/support/image_writer_test_resource.h>

#include <limits>
#include <memory>
#include <string>

namespace
{
	class CImageByteWriterTest : public ::testing::Test
	{
	protected:
		CImageWriterTestResource m_Resource;
		std::unique_ptr<CByteBufferWriter> m_pWriter;
		void SetUp() override { m_pWriter = std::make_unique<CByteBufferWriter>(&m_Resource.m_Allocation); }
		void TearDown() override
		{
			m_pWriter.reset();
			EXPECT_EQ(m_Resource.m_Live, 0);
		}
		std::string Bytes() const
		{
			return m_pWriter->Size() == 0 ? std::string{} : std::string(reinterpret_cast<const char *>(m_pWriter->Data()), m_pWriter->Size());
		}
	};
}

TEST_F(CImageByteWriterTest, ConsecutiveBinaryWritesAppendCompleteData)
{
	const std::string First("a\0b", 3);
	ASSERT_TRUE(m_pWriter->Write(First.data(), First.size()));
	ASSERT_TRUE(m_pWriter->Write("end", 3));
	EXPECT_EQ(Bytes(), First + "end");
}

TEST_F(CImageByteWriterTest, OverflowAfterPrefixIsRejectedBeforeAllocation)
{
	ASSERT_TRUE(m_pWriter->Write("old", 3));
	const int Calls = m_Resource.m_Calls;
	const auto *pOld = m_pWriter->Data();
	EXPECT_FALSE(m_pWriter->Write("x", std::numeric_limits<size_t>::max()));
	EXPECT_EQ(Bytes(), "old");
	EXPECT_EQ(m_pWriter->Data(), pOld);
	EXPECT_EQ(m_Resource.m_Calls, Calls);
}

TEST_F(CImageByteWriterTest, MissingNonemptyInputDoesNotChangeExistingBytes)
{
	ASSERT_TRUE(m_pWriter->Write("old", 3));
	const int Calls = m_Resource.m_Calls;
	EXPECT_FALSE(m_pWriter->Write(nullptr, 1));
	EXPECT_EQ(Bytes(), "old");
	EXPECT_EQ(m_Resource.m_Calls, Calls);
}

TEST_F(CImageByteWriterTest, EmptyWriteAllowsNullInputWithoutAllocation)
{
	const int Calls = m_Resource.m_Calls;
	EXPECT_TRUE(m_pWriter->Write(nullptr, 0));
	EXPECT_EQ(m_pWriter->Size(), 0u);
	EXPECT_EQ(m_Resource.m_Calls, Calls);
}

TEST_F(CImageByteWriterTest, InitialAllocationFailureReturnsFalseAndCanRetry)
{
	m_Resource.m_Reject = true;
	EXPECT_FALSE(m_pWriter->Write("new", 3));
	EXPECT_EQ(m_Resource.m_Rejections, 1);
	EXPECT_EQ(m_pWriter->Size(), 0u);
	m_Resource.m_Reject = false;
	ASSERT_TRUE(m_pWriter->Write("new", 3));
	EXPECT_EQ(Bytes(), "new");
}

TEST_F(CImageByteWriterTest, GrowthFailureKeepsExistingPrefixAndCanAppendLater)
{
	ASSERT_TRUE(m_pWriter->Write("old", 3));
	const std::string More(4096, 'x');
	const auto *pOld = m_pWriter->Data();
	m_Resource.m_Reject = true;
	EXPECT_FALSE(m_pWriter->Write(More.data(), More.size()));
	EXPECT_EQ(Bytes(), "old");
	EXPECT_EQ(m_pWriter->Data(), pOld);
	m_Resource.m_Reject = false;
	ASSERT_TRUE(m_pWriter->Write(More.data(), More.size()));
	EXPECT_EQ(Bytes(), "old" + More);
}

TEST_F(CImageByteWriterTest, UnknownAllocatorExceptionDoesNotEscapeWrite)
{
	m_Resource.m_Reject = true;
	m_Resource.m_Unknown = true;
	EXPECT_FALSE(m_pWriter->Write("new", 3));
	EXPECT_EQ(m_Resource.m_Rejections, 1);
	EXPECT_EQ(m_pWriter->Size(), 0u);
	m_Resource.m_Reject = false;
	ASSERT_TRUE(m_pWriter->Write("new", 3));
	EXPECT_EQ(Bytes(), "new");
}

TEST_F(CImageByteWriterTest, DestructionReleasesLocallyAllocatedOutput)
{
	const std::string More(4096, 'x');
	ASSERT_TRUE(m_pWriter->Write(More.data(), More.size()));
	ASSERT_GT(m_Resource.m_Live, 0);
	m_pWriter.reset();
	EXPECT_EQ(m_Resource.m_Live, 0);
}

TEST_F(CImageByteWriterTest, NullAllocationResultReturnsFalseWithoutPublishingBytes)
{
	m_Resource.m_Reject = true;
	m_Resource.m_ReturnNull = true;
	EXPECT_FALSE(m_pWriter->Write("new", 3));
	EXPECT_EQ(m_Resource.m_Rejections, 1);
	EXPECT_EQ(m_pWriter->Size(), 0u);
	m_Resource.m_Reject = false;
	EXPECT_TRUE(m_pWriter->Write("new", 3));
}
