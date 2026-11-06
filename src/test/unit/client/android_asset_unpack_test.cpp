#include <android/asset_unpack.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace
{
	struct CStreams
	{
		std::string m_Input = "asset";
		std::string m_Candidate;
		std::string m_Published = "old";
		size_t m_Offset = 0;
		size_t m_ReadLimit = std::numeric_limits<size_t>::max();
		size_t m_MaxRequest = 0;
		bool m_ReadError = false, m_ZeroRead = false, m_OversizedRead = false;
		bool m_ThrowRead = false, m_ThrowWrite = false, m_ThrowPublish = false;
		bool m_WriteOk = true, m_InputCloseOk = true, m_OutputCloseOk = true, m_PublishOk = true;
		bool m_OutputComplete = false;
		int m_Reads = 0, m_Writes = 0, m_InputCloses = 0, m_OutputCloses = 0, m_Publishes = 0;
		int m_Step = 0, m_InputCloseOrder = -1, m_OutputCloseOrder = -1, m_PublishOrder = -1;

		android_assets::EStage Run(std::int64_t Length)
		{
			return android_assets::Unpack(Length, [&](void *pData, size_t Size, size_t &Read) {
				++m_Reads;
				m_MaxRequest = std::max(m_MaxRequest, Size);
				if(m_ThrowRead)
					throw std::bad_alloc();
				Read = m_OversizedRead ? Size + 1 : m_ZeroRead ? 0 : std::min({Size, m_Input.size() - m_Offset, m_ReadLimit});
				if(!m_OversizedRead)
				{
					std::memcpy(pData, m_Input.data() + m_Offset, Read);
					m_Offset += Read;
				}
				return !m_ReadError; }, [&](const void *pData, size_t Size) {
				++m_Writes;
				if(m_ThrowWrite)
					throw std::bad_alloc();
				m_Candidate.append(static_cast<const char *>(pData), Size);
				return m_WriteOk; }, [&]() noexcept {
				++m_InputCloses;
				m_InputCloseOrder = m_Step++;
				return m_InputCloseOk; }, [&](bool Complete) noexcept {
				++m_OutputCloses;
				m_OutputComplete = Complete;
				m_OutputCloseOrder = m_Step++;
				return m_OutputCloseOk; }, [&] {
				++m_Publishes;
				m_PublishOrder = m_Step++;
				if(m_ThrowPublish)
					throw std::bad_alloc();
				if(!m_PublishOk)
					return false;
				m_Published.swap(m_Candidate);
				return true; });
		}
		void ExpectClosedWithoutPublishing() const
		{
			EXPECT_EQ(m_InputCloses, 1);
			EXPECT_EQ(m_OutputCloses, 1);
			EXPECT_EQ(m_Publishes, 0);
			EXPECT_EQ(m_Published, "old");
		}
	};
}

TEST(AndroidAssetUnpack, CompleteCopyUsesBoundedChunksAndClosesBeforePublishing)
{
	CStreams Streams;
	Streams.m_Input = std::string(40000, 'x') + "tail";
	EXPECT_EQ(Streams.Run(static_cast<std::int64_t>(Streams.m_Input.size())), android_assets::EStage::DONE);
	EXPECT_EQ(Streams.m_Published, Streams.m_Input);
	EXPECT_EQ(Streams.m_MaxRequest, 16 * 1024u);
	EXPECT_TRUE(Streams.m_OutputComplete);
	EXPECT_EQ(Streams.m_InputCloses, 1);
	EXPECT_EQ(Streams.m_OutputCloses, 1);
	EXPECT_EQ(Streams.m_Publishes, 1);
	EXPECT_LT(Streams.m_InputCloseOrder, Streams.m_OutputCloseOrder);
	EXPECT_LT(Streams.m_OutputCloseOrder, Streams.m_PublishOrder);
}

TEST(AndroidAssetUnpack, PartialReadsContinueUntilComplete)
{
	CStreams Streams;
	Streams.m_Input = "a complete asset payload";
	Streams.m_ReadLimit = 3;
	EXPECT_EQ(Streams.Run(static_cast<std::int64_t>(Streams.m_Input.size())), android_assets::EStage::DONE);
	EXPECT_GT(Streams.m_Reads, 1);
	EXPECT_EQ(Streams.m_Published, Streams.m_Input);
}

TEST(AndroidAssetUnpack, EarlyEofPreservesOldAsset)
{
	CStreams Streams;
	EXPECT_EQ(Streams.Run(static_cast<std::int64_t>(Streams.m_Input.size()) + 1), android_assets::EStage::READ);
	Streams.ExpectClosedWithoutPublishing();
	EXPECT_FALSE(Streams.m_OutputComplete);
}

TEST(AndroidAssetUnpack, ReadErrorDoesNotWriteReturnedBytes)
{
	CStreams Streams;
	Streams.m_ReadError = true;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::READ);
	EXPECT_EQ(Streams.m_Writes, 0);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, OversizedReadCountIsRejectedBeforeWrite)
{
	CStreams Streams;
	Streams.m_OversizedRead = true;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::READ);
	EXPECT_EQ(Streams.m_Writes, 0);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, ZeroProgressDoesNotRetryForever)
{
	CStreams Streams;
	Streams.m_ZeroRead = true;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::READ);
	EXPECT_EQ(Streams.m_Reads, 1);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, IncompleteWritePreservesOldAssetAndClosesBothStreams)
{
	CStreams Streams;
	Streams.m_WriteOk = false;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::WRITE);
	Streams.ExpectClosedWithoutPublishing();
	EXPECT_FALSE(Streams.m_OutputComplete);
}

TEST(AndroidAssetUnpack, InvalidLengthDoesNotReadOrWriteButClosesStreams)
{
	CStreams Streams;
	EXPECT_EQ(Streams.Run(-1), android_assets::EStage::INVALID_LENGTH);
	EXPECT_EQ(Streams.m_Reads, 0);
	EXPECT_EQ(Streams.m_Writes, 0);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, EmptyAssetCanBePublishedAfterBothCloses)
{
	CStreams Streams;
	EXPECT_EQ(Streams.Run(0), android_assets::EStage::DONE);
	EXPECT_EQ(Streams.m_Reads, 0);
	EXPECT_EQ(Streams.m_Writes, 0);
	EXPECT_TRUE(Streams.m_Published.empty());
	EXPECT_EQ(Streams.m_InputCloses, 1);
	EXPECT_EQ(Streams.m_OutputCloses, 1);
}

TEST(AndroidAssetUnpack, InputCloseFailurePreventsPublication)
{
	CStreams Streams;
	Streams.m_InputCloseOk = false;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::INPUT_CLOSE);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, OutputCompletionFailurePreventsPublication)
{
	CStreams Streams;
	Streams.m_OutputCloseOk = false;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::OUTPUT_CLOSE);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, ReadExceptionClosesStreamsAndPreservesOldAsset)
{
	CStreams Streams;
	Streams.m_ThrowRead = true;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::PREPARATION);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, WriteExceptionClosesStreamsAndPreservesOldAsset)
{
	CStreams Streams;
	Streams.m_ThrowWrite = true;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::PREPARATION);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, PublicationFailureKeepsOldAssetAndCompleteCandidate)
{
	CStreams Streams;
	Streams.m_PublishOk = false;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::PUBLISH);
	EXPECT_EQ(Streams.m_Published, "old");
	EXPECT_EQ(Streams.m_Candidate, Streams.m_Input);
	EXPECT_EQ(Streams.m_Publishes, 1);
}

TEST(AndroidAssetUnpack, PublicationExceptionDoesNotEscape)
{
	CStreams Streams;
	Streams.m_ThrowPublish = true;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::PUBLISH);
	EXPECT_EQ(Streams.m_Published, "old");
	EXPECT_EQ(Streams.m_Candidate, Streams.m_Input);
}

TEST(AndroidAssetUnpack, CopyFailureTakesPriorityOverClosingFailures)
{
	CStreams Streams;
	Streams.m_ReadError = true;
	Streams.m_InputCloseOk = false;
	Streams.m_OutputCloseOk = false;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::READ);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, BothClosingFailuresReportFirstClose)
{
	CStreams Streams;
	Streams.m_InputCloseOk = false;
	Streams.m_OutputCloseOk = false;
	EXPECT_EQ(Streams.Run(5), android_assets::EStage::INPUT_CLOSE);
	Streams.ExpectClosedWithoutPublishing();
}

TEST(AndroidAssetUnpack, LengthBeyondUnsigned32BitDoesNotNarrowOrAllocateWholeAsset)
{
	constexpr std::int64_t Length = (std::int64_t{1} << 32) + 123;
	std::uint64_t Written = 0;
	size_t MaxRequest = 0;
	// 虚拟零流与丢弃输出只替换I/O边界，分块累计字节数而不存储4GiB数据。
	EXPECT_EQ(android_assets::Unpack(Length, [&](void *, size_t Requested, size_t &Read) {
		MaxRequest = std::max(MaxRequest, Requested);
		Read = Requested;
		return true; }, [&](const void *, size_t Size) {
		Written += Size;
		return true; }, []() noexcept { return true; }, [](bool Complete) noexcept { return Complete; }, [] { return true; }), android_assets::EStage::DONE);
	EXPECT_EQ(Written, static_cast<std::uint64_t>(Length));
	EXPECT_LE(MaxRequest, 16 * 1024u);
}
