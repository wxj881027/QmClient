#include <engine/client/sixup_map_change.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>

namespace
{
	class CSixupMapChangeTest : public testing::Test
	{
	protected:
		CTranslationContext m_Context;
		CPacker m_Input;
		CPacker m_Output;
		CUnpacker m_Unpacker;

		void SetUp() override
		{
			m_Context.m_MapdownloadTotalsize = 123;
			m_Context.m_MapDownloadChunksPerRequest = 4;
			m_Context.m_MapDownloadChunkSize = 25;
			m_Output.Reset();
			PrepareInput();
		}

		void PrepareInput(int Size = 300, int Chunks = 8, int ChunkSize = 64)
		{
			m_Input.Reset();
			m_Input.AddString("  map_name");
			m_Input.AddInt(std::numeric_limits<int>::min());
			m_Input.AddInt(Size);
			m_Input.AddInt(Chunks);
			m_Input.AddInt(ChunkSize);
			m_Unpacker.Reset(m_Input.Data(), m_Input.Size());
		}

		void ExpectPreviousContext()
		{
			EXPECT_EQ(m_Context.m_MapdownloadTotalsize, 123);
			EXPECT_EQ(m_Context.m_MapDownloadChunksPerRequest, 4);
			EXPECT_EQ(m_Context.m_MapDownloadChunkSize, 25);
		}
	};
}

TEST_F(CSixupMapChangeTest, ConvertsRequiredFieldsAndLeavesTrailingHashUnread)
{
	const std::array<unsigned char, 32> aHash{1, 2, 3};
	m_Input.AddRaw(aHash.data(), static_cast<int>(aHash.size()));
	m_Unpacker.Reset(m_Input.Data(), m_Input.Size());
	ASSERT_TRUE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
	CUnpacker Result;
	Result.Reset(m_Output.Data(), m_Output.Size());
	EXPECT_STREQ(Result.GetString(), "map_name");
	EXPECT_EQ(Result.GetInt(), std::numeric_limits<int>::min());
	EXPECT_EQ(Result.GetInt(), 300);
	EXPECT_FALSE(Result.Error());
	EXPECT_EQ(m_Context.m_MapdownloadTotalsize, 300);
	EXPECT_EQ(m_Context.m_MapDownloadChunksPerRequest, 8);
	EXPECT_EQ(m_Context.m_MapDownloadChunkSize, 64);
	const auto *pHash = m_Unpacker.GetRaw(static_cast<int>(aHash.size()));
	ASSERT_NE(pHash, nullptr);
	EXPECT_EQ(std::memcmp(pHash, aHash.data(), aHash.size()), 0);
	EXPECT_FALSE(m_Unpacker.Error());
}

TEST_F(CSixupMapChangeTest, EveryTruncatedRequiredPrefixPreservesDownloadContext)
{
	for(int Size = 0; Size < m_Input.Size(); ++Size)
	{
		SCOPED_TRACE(Size);
		m_Unpacker.Reset(m_Input.Data(), Size);
		m_Output.Reset();
		EXPECT_FALSE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
		EXPECT_TRUE(m_Unpacker.Error());
		EXPECT_EQ(m_Output.Size(), 0);
		ExpectPreviousContext();
	}
}

TEST_F(CSixupMapChangeTest, ExistingInputErrorDoesNotPublishParameters)
{
	m_Unpacker.GetRaw(m_Input.Size() + 1);
	ASSERT_TRUE(m_Unpacker.Error());
	EXPECT_FALSE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
	EXPECT_EQ(m_Output.Size(), 0);
	ExpectPreviousContext();
}

TEST_F(CSixupMapChangeTest, OutputCapacityFailureDoesNotPublishParameters)
{
	const std::array<unsigned char, CPacker::PACKER_BUFFER_SIZE - 1> aFill{};
	m_Output.AddRaw(aFill.data(), static_cast<int>(aFill.size()));
	ASSERT_FALSE(m_Output.Error());
	EXPECT_FALSE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
	EXPECT_TRUE(m_Output.Error());
	ExpectPreviousContext();
}

TEST_F(CSixupMapChangeTest, ExistingOutputErrorDoesNotPublishParameters)
{
	const std::array<unsigned char, CPacker::PACKER_BUFFER_SIZE> aFill{};
	m_Output.AddRaw(aFill.data(), static_cast<int>(aFill.size()));
	m_Output.AddInt(1);
	ASSERT_TRUE(m_Output.Error());
	EXPECT_FALSE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
	ExpectPreviousContext();
}

TEST_F(CSixupMapChangeTest, CompleteMessageCanPublishAfterRejectedPrefix)
{
	m_Unpacker.Reset(m_Input.Data(), m_Input.Size() - 1);
	EXPECT_FALSE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
	ExpectPreviousContext();
	m_Output.Reset();
	PrepareInput(900, 3, 300);
	ASSERT_TRUE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
	EXPECT_EQ(m_Context.m_MapdownloadTotalsize, 900);
	EXPECT_EQ(m_Context.m_MapDownloadChunksPerRequest, 3);
	EXPECT_EQ(m_Context.m_MapDownloadChunkSize, 300);
}

TEST_F(CSixupMapChangeTest, CompleteNumericBoundaryValuesRetainExistingTranslationSemantics)
{
	for(int Value : {0, -1, std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Value);
		PrepareInput(Value, Value, Value);
		m_Output.Reset();
		ASSERT_TRUE(sixup_translation::TranslateMapChange(m_Context, m_Unpacker, m_Output));
		EXPECT_EQ(m_Context.m_MapdownloadTotalsize, Value);
		EXPECT_EQ(m_Context.m_MapDownloadChunksPerRequest, Value);
		EXPECT_EQ(m_Context.m_MapDownloadChunkSize, Value);
	}
}
