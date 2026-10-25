#include <engine/storage.h>

#include <game/client/components/qmclient/community_icon_image.h>
#include <game/client/components/qmclient/community_icon_scan.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace
{
	using qm_community_icons::ELoadResult;

	bool Write(IStorage &Storage, const char *pPath, const std::vector<uint8_t> &Data)
	{
		IOHANDLE File = Storage.OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Written = io_write(File, Data.data(), static_cast<unsigned>(Data.size())) == Data.size();
		return io_close(File) == 0 && Written;
	}

	// 文件读取仍使用真实存储；仅确定性安排“读完快照后文件被替换/抛异常”的边界。
	struct SReader
	{
		IStorage &m_Storage;
		std::vector<uint8_t> m_Replacement;
		bool m_Replace = false;
		bool m_Replaced = false;
		bool m_ThrowAfterRead = false;
		int m_Reads = 0;

		explicit SReader(IStorage &Storage) :
			m_Storage(Storage) {}

		bool ReadFile(const char *pPath, int Type, void **ppData, unsigned *pSize)
		{
			++m_Reads;
			const bool Success = m_Storage.ReadFile(pPath, Type, ppData, pSize);
			if(Success && m_Replace)
				m_Replaced = Write(m_Storage, pPath, m_Replacement);
			if(Success && m_ThrowAfterRead)
				throw std::runtime_error("storage boundary failed after returning buffer");
			return Success;
		}
	};

	class CommunityIconImage : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CImageInfo m_Original;
		CImageInfo m_Gray;
		SHA256_DIGEST m_Digest{};

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Digest.data[0] = 99;
		}
		void TearDown() override
		{
			m_Original.Free();
			m_Gray.Free();
		}
		std::vector<uint8_t> Png(uint8_t Red = 200, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA, const char *pComment = nullptr)
		{
			std::array<uint8_t, 4> aPixel{Red, 40, 10, 123};
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 1;
			Image.m_Format = Format;
			Image.m_pData = aPixel.data();
			CByteBufferWriter Writer;
			if(!CImageLoader::SavePng(Writer, Image, pComment))
				throw std::runtime_error("could not prepare PNG fixture");
			return {Writer.Data(), Writer.Data() + Writer.Size()};
		}
		ELoadResult Load(const char *pPath = "icon.png")
		{
			return qm_community_icons::LoadImage(*m_pStorage, pPath, IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest);
		}
	};
}

TEST_F(CommunityIconImage, ActualStoragePngAndGrayscalePreserveOriginalPixelsAndAlpha)
{
	const auto Data = Png();
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Data));
	ASSERT_EQ(Load(), ELoadResult::LOADED);
	EXPECT_EQ(m_Digest, sha256(Data.data(), Data.size()));
	EXPECT_EQ(m_Original.m_Width, 1u);
	EXPECT_EQ(m_Original.m_Height, 1u);
	EXPECT_EQ(m_Gray.m_Format, CImageInfo::FORMAT_RGBA);
	ASSERT_NE(m_Original.m_pData, nullptr);
	ASSERT_NE(m_Gray.m_pData, nullptr);
	EXPECT_NE(m_Original.m_pData, m_Gray.m_pData);
	EXPECT_EQ(m_Original.m_pData[0], 200);
	EXPECT_EQ(m_Original.m_pData[1], 40);
	EXPECT_EQ(m_Original.m_pData[2], 10);
	EXPECT_EQ(m_Original.m_pData[3], 123);
	EXPECT_EQ(m_Gray.m_pData[0], m_Gray.m_pData[1]);
	EXPECT_EQ(m_Gray.m_pData[1], m_Gray.m_pData[2]);
	EXPECT_EQ(m_Gray.m_pData[3], 123);
	m_Gray.m_pData[0] = 0;
	EXPECT_EQ(m_Original.m_pData[0], 200);
}

TEST_F(CommunityIconImage, ReplacementAfterReadCannotLabelOldPixelsWithNewFileDigest)
{
	const auto OldData = Png(200);
	const auto NewData = Png(20);
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", OldData));
	SReader Reader{*m_pStorage};
	Reader.m_Replacement = NewData;
	Reader.m_Replace = true;
	ASSERT_EQ(qm_community_icons::LoadImage(Reader, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest), ELoadResult::LOADED);
	ASSERT_TRUE(Reader.m_Replaced);
	EXPECT_EQ(Reader.m_Reads, 1);
	ASSERT_NE(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Original.m_pData[0], 200);
	EXPECT_EQ(m_Digest, sha256(OldData.data(), OldData.size()));
	SHA256_DIGEST DiskDigest{};
	ASSERT_TRUE(m_pStorage->CalculateHashes("icon.png", IStorage::TYPE_SAVE, &DiskDigest));
	EXPECT_EQ(DiskDigest, sha256(NewData.data(), NewData.size()));
	EXPECT_NE(m_Digest, DiskDigest);
	CCommunity Current("a", "A", DiskDigest, "https://example.test/a");
	EXPECT_FALSE(qm_community_icons::AcceptsResult(true, &Current, m_Digest));
}

TEST_F(CommunityIconImage, IdenticalPixelsWithDifferentPngCommentsKeepDistinctFileDigests)
{
	const auto First = Png(200, CImageInfo::FORMAT_RGBA, "first");
	const auto Second = Png(200, CImageInfo::FORMAT_RGBA, "second");
	ASSERT_NE(First, Second);
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", First));
	ASSERT_EQ(Load(), ELoadResult::LOADED);
	const auto FirstDigest = m_Digest;
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Second));
	ASSERT_EQ(Load(), ELoadResult::LOADED);
	EXPECT_NE(FirstDigest, m_Digest);
	EXPECT_EQ(m_Digest, sha256(Second.data(), Second.size()));
	ASSERT_NE(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Original.m_pData[0], 200);
}

TEST_F(CommunityIconImage, MissingFileLeavesPreviouslyPreparedPairAndDigestUnchanged)
{
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Png()));
	ASSERT_EQ(Load(), ELoadResult::LOADED);
	const auto *pOriginal = m_Original.m_pData;
	const auto *pGray = m_Gray.m_pData;
	const auto Before = m_Digest;
	EXPECT_EQ(Load("missing.png"), ELoadResult::READ_FAILED);
	EXPECT_EQ(m_Original.m_pData, pOriginal);
	EXPECT_EQ(m_Gray.m_pData, pGray);
	EXPECT_EQ(m_Digest, Before);
}

TEST_F(CommunityIconImage, CorruptPngDoesNotPublishPartialOutputsAndLaterValidFileLoads)
{
	const auto Before = m_Digest;
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", {1, 2, 3}));
	EXPECT_EQ(Load(), ELoadResult::DECODE_FAILED);
	EXPECT_EQ(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Gray.m_pData, nullptr);
	EXPECT_EQ(m_Digest, Before);
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Png(80)));
	ASSERT_EQ(Load(), ELoadResult::LOADED);
	ASSERT_NE(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Original.m_pData[0], 80);
}

TEST_F(CommunityIconImage, NonRgbaPngRetainsExistingFormatRejection)
{
	const auto Before = m_Digest;
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Png(200, CImageInfo::FORMAT_RGB)));
	EXPECT_EQ(Load(), ELoadResult::FORMAT_UNSUPPORTED);
	EXPECT_EQ(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Gray.m_pData, nullptr);
	EXPECT_EQ(m_Digest, Before);
}

TEST_F(CommunityIconImage, TruncatedPixelChunkLeavesNoPublishedImageOrDigest)
{
	auto Data = Png();
	const std::array<uint8_t, 4> aIdat{'I', 'D', 'A', 'T'};
	const auto It = std::search(Data.begin(), Data.end(), aIdat.begin(), aIdat.end());
	ASSERT_NE(It, Data.end());
	Data.resize(static_cast<size_t>(It - Data.begin()) + aIdat.size() + 1);
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Data));
	const auto Before = m_Digest;
	EXPECT_EQ(Load(), ELoadResult::DECODE_FAILED);
	EXPECT_EQ(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Gray.m_pData, nullptr);
	EXPECT_EQ(m_Digest, Before);
}

TEST_F(CommunityIconImage, StorageExceptionAfterSnapshotIsReportedAndNextLoadCanRecover)
{
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Png()));
	SReader Reader{*m_pStorage};
	Reader.m_ThrowAfterRead = true;
	const auto Before = m_Digest;
	EXPECT_EQ(qm_community_icons::LoadImage(Reader, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest), ELoadResult::PROCESSING_FAILED);
	EXPECT_EQ(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Gray.m_pData, nullptr);
	EXPECT_EQ(m_Digest, Before);
	Reader.m_ThrowAfterRead = false;
	EXPECT_EQ(qm_community_icons::LoadImage(Reader, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest), ELoadResult::LOADED);
}

TEST_F(CommunityIconImage, EmptyFileFailsDecodeWithoutChangingDigest)
{
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", {}));
	const auto Before = m_Digest;
	EXPECT_EQ(Load(), ELoadResult::DECODE_FAILED);
	EXPECT_EQ(m_Digest, Before);
	EXPECT_EQ(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Gray.m_pData, nullptr);
}

TEST_F(CommunityIconImage, BoundedReaderRejectsOversizedFileBeforeReturningAnySnapshot)
{
	const auto Data = Png();
	ASSERT_GT(Data.size(), 1u);
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Data));
	qm_community_icons::CBoundedFileReader Reader(*m_pStorage, Data.size() - 1);
	void *pData = nullptr;
	unsigned Size = 7;
	EXPECT_FALSE(Reader.ReadFile("icon.png", IStorage::TYPE_SAVE, &pData, &Size));
	EXPECT_TRUE(Reader.TooLarge());
	EXPECT_EQ(pData, nullptr);
	EXPECT_EQ(Size, 0u);
	std::free(pData);
}

TEST_F(CommunityIconImage, BoundedReaderAcceptsExactFileLimitAndKeepsSnapshotDigest)
{
	const auto Data = Png();
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Data));
	qm_community_icons::CBoundedFileReader Reader(*m_pStorage, Data.size());
	ASSERT_EQ(qm_community_icons::LoadImage(Reader, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest), ELoadResult::LOADED);
	EXPECT_FALSE(Reader.TooLarge());
	EXPECT_EQ(m_Digest, sha256(Data.data(), Data.size()));
	ASSERT_NE(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Original.m_pData[0], 200);
}

TEST_F(CommunityIconImage, OversizedReplacementKeepsPreviouslyPreparedPairAndDigest)
{
	const auto Data = Png();
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Data));
	qm_community_icons::CBoundedFileReader Reader(*m_pStorage, Data.size());
	ASSERT_EQ(qm_community_icons::LoadImage(Reader, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest), ELoadResult::LOADED);
	const auto *pOriginal = m_Original.m_pData;
	const auto *pGray = m_Gray.m_pData;
	const auto Before = m_Digest;
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", std::vector<uint8_t>(Data.size() + 1, 0)));
	EXPECT_EQ(qm_community_icons::LoadImage(Reader, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest), ELoadResult::READ_FAILED);
	EXPECT_TRUE(Reader.TooLarge());
	EXPECT_EQ(m_Original.m_pData, pOriginal);
	EXPECT_EQ(m_Gray.m_pData, pGray);
	EXPECT_EQ(m_Digest, Before);
}

TEST_F(CommunityIconImage, DecodedRgbaByteLimitRejectsBeforePublicationAndAcceptsExactBoundary)
{
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Png()));
	const auto Before = m_Digest;
	EXPECT_EQ(qm_community_icons::LoadImage(*m_pStorage, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest, 3), ELoadResult::DECODE_FAILED);
	EXPECT_EQ(m_Original.m_pData, nullptr);
	EXPECT_EQ(m_Gray.m_pData, nullptr);
	EXPECT_EQ(m_Digest, Before);
	EXPECT_EQ(qm_community_icons::LoadImage(*m_pStorage, "icon.png", IStorage::TYPE_SAVE, m_Original, m_Gray, m_Digest, 4), ELoadResult::LOADED);
	EXPECT_EQ(m_Original.DataSize(), 4u);
	EXPECT_EQ(m_Gray.DataSize(), 4u);
}

TEST_F(CommunityIconImage, MissingFileDoesNotReusePreviousOversizeFailureReason)
{
	ASSERT_TRUE(Write(*m_pStorage, "icon.png", Png()));
	qm_community_icons::CBoundedFileReader Reader(*m_pStorage, 1);
	void *pData = nullptr;
	unsigned Size = 0;
	EXPECT_FALSE(Reader.ReadFile("icon.png", IStorage::TYPE_SAVE, &pData, &Size));
	EXPECT_TRUE(Reader.TooLarge());
	std::free(pData);
	pData = nullptr;
	EXPECT_FALSE(Reader.ReadFile("missing.png", IStorage::TYPE_SAVE, &pData, &Size));
	EXPECT_FALSE(Reader.TooLarge());
	EXPECT_EQ(pData, nullptr);
	EXPECT_EQ(Size, 0u);
	std::free(pData);
}
