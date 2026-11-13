#include <base/mem.h>

#include <engine/gfx/image_loader.h>

#include <game/client/components/qmclient/map_cover_art.h>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{
	using EResult = QmMapCover::EPrepareResult;
	using EKind = CPngReadAllocator::EKind;

	struct CScopedImage
	{
		CImageInfo m_Image;
		CScopedImage() = default;
		CScopedImage(int Width, int Height)
		{
			m_Image.m_Width = Width;
			m_Image.m_Height = Height;
			m_Image.m_Format = CImageInfo::FORMAT_RGB;
			const size_t Bytes = static_cast<size_t>(Width) * Height * 3;
			m_Image.m_pData = static_cast<uint8_t *>(std::malloc(Bytes));
			if(m_Image.m_pData != nullptr)
			{
				for(size_t i = 0; i < Bytes; i += 3)
				{
					m_Image.m_pData[i] = 80;
					m_Image.m_pData[i + 1] = 40;
					m_Image.m_pData[i + 2] = 200;
				}
			}
		}
		~CScopedImage() { m_Image.Free(); }
	};

	struct COutputAllocation
	{
		int m_Calls = 0;
		bool m_Refuse = true;
		QmMapCover::SOutputAllocation m_Allocation{Allocate, this};
		static void *Allocate(size_t Size, void *pUser) noexcept
		{
			auto &Self = *static_cast<COutputAllocation *>(pUser);
			++Self.m_Calls;
			return Self.m_Refuse ? nullptr : std::malloc(Size);
		}
	};

	class CRefusingPngAllocator final : public CPngReadAllocator
	{
	public:
		const EKind m_Kind;
		bool m_Refuse = true;
		bool m_Refused = false;
		explicit CRefusingPngAllocator(EKind Kind) :
			m_Kind(Kind) {}
		void *Allocate(EKind Kind, size_t Size) noexcept override
		{
			if(Kind == m_Kind && m_Refuse)
			{
				m_Refused = true;
				return nullptr;
			}
			return CPngReadAllocator::Allocate(Kind, Size);
		}
	};

	void ExpectOriginalOutput(const CImageInfo &Image, const uint8_t *pOriginal)
	{
		EXPECT_EQ(Image.m_pData, pOriginal);
		EXPECT_EQ(Image.m_Width, 1u);
		EXPECT_EQ(Image.m_Height, 1u);
		EXPECT_EQ(Image.m_Format, CImageInfo::FORMAT_RGB);
		const uint8_t aOriginal[] = {80, 40, 200};
		ASSERT_NE(Image.m_pData, nullptr);
		EXPECT_EQ(mem_comp(Image.m_pData, aOriginal, sizeof(aOriginal)), 0);
	}
}

TEST(QmMapCoverResult, OutputAllocationRefusalPreservesTheImageAndRetryMatchesNormalCompose)
{
	CScopedImage Source(4, 2), Output(1, 1), Expected;
	ASSERT_NE(Source.m_Image.m_pData, nullptr);
	ASSERT_NE(Output.m_Image.m_pData, nullptr);
	const auto *pOriginal = Output.m_Image.m_pData;
	const QmMapCover::SArt Art;
	ASSERT_TRUE(QmMapCover::Compose(Source.m_Image, 64, 32, Art, Expected.m_Image));
	COutputAllocation Allocation;
	EXPECT_EQ(QmMapCover::ComposeChecked(Source.m_Image, 64, 32, Art, Output.m_Image, &Allocation.m_Allocation), EResult::RESOURCE_ERROR);
	EXPECT_EQ(Allocation.m_Calls, 1);
	ExpectOriginalOutput(Output.m_Image, pOriginal);
	Allocation.m_Refuse = false;
	ASSERT_EQ(QmMapCover::ComposeChecked(Source.m_Image, 64, 32, Art, Output.m_Image, &Allocation.m_Allocation), EResult::SUCCESS);
	EXPECT_TRUE(Output.m_Image.DataEquals(Expected.m_Image));
}

TEST(QmMapCoverResult, EveryPngAllocationKindIsResourceFailureAndCanRetryTheSameBytes)
{
	CScopedImage Source(4, 2), Expected;
	ASSERT_NE(Source.m_Image.m_pData, nullptr);
	CByteBufferWriter Png;
	ASSERT_TRUE(CImageLoader::SavePng(Png, Source.m_Image));
	const QmMapCover::SArt Art;
	ASSERT_TRUE(QmMapCover::DecodeAndCompose(Png.Data(), Png.Size(), "map_cover_result", 64, 32, Art, Expected.m_Image));
	for(const auto Kind : {EKind::STATE, EKind::PIXELS, EKind::ROWS, EKind::LIBPNG})
	{
		SCOPED_TRACE(static_cast<int>(Kind));
		CScopedImage Output(1, 1);
		ASSERT_NE(Output.m_Image.m_pData, nullptr);
		const auto *pOriginal = Output.m_Image.m_pData;
		CRefusingPngAllocator PngAllocation(Kind);
		COutputAllocation OutputAllocation;
		OutputAllocation.m_Refuse = false;
		EXPECT_EQ(QmMapCover::DecodeAndComposeChecked(Png.Data(), Png.Size(), "map_cover_result", 64, 32, Art, Output.m_Image,
				  &PngAllocation, &OutputAllocation.m_Allocation),
			EResult::RESOURCE_ERROR);
		EXPECT_TRUE(PngAllocation.m_Refused);
		EXPECT_EQ(OutputAllocation.m_Calls, 0);
		ExpectOriginalOutput(Output.m_Image, pOriginal);
		PngAllocation.m_Refuse = false;
		ASSERT_EQ(QmMapCover::DecodeAndComposeChecked(Png.Data(), Png.Size(), "map_cover_result", 64, 32, Art, Output.m_Image,
				  &PngAllocation, &OutputAllocation.m_Allocation),
			EResult::SUCCESS);
		EXPECT_EQ(OutputAllocation.m_Calls, 1);
		EXPECT_TRUE(Output.m_Image.DataEquals(Expected.m_Image));
	}
}

TEST(QmMapCoverResult, ComposeAllocationFailureAfterRealDecodePreservesOutputAndCanRetry)
{
	CScopedImage Source(4, 2), Output(1, 1), Expected;
	ASSERT_NE(Source.m_Image.m_pData, nullptr);
	ASSERT_NE(Output.m_Image.m_pData, nullptr);
	CByteBufferWriter Png;
	ASSERT_TRUE(CImageLoader::SavePng(Png, Source.m_Image));
	const QmMapCover::SArt Art;
	ASSERT_TRUE(QmMapCover::DecodeAndCompose(Png.Data(), Png.Size(), "map_cover_result", 64, 32, Art, Expected.m_Image));
	const auto *pOriginal = Output.m_Image.m_pData;
	COutputAllocation Allocation;
	EXPECT_EQ(QmMapCover::DecodeAndComposeChecked(Png.Data(), Png.Size(), "map_cover_result", 64, 32, Art, Output.m_Image,
			  nullptr, &Allocation.m_Allocation),
		EResult::RESOURCE_ERROR);
	EXPECT_EQ(Allocation.m_Calls, 1);
	ExpectOriginalOutput(Output.m_Image, pOriginal);
	Allocation.m_Refuse = false;
	ASSERT_EQ(QmMapCover::DecodeAndComposeChecked(Png.Data(), Png.Size(), "map_cover_result", 64, 32, Art, Output.m_Image,
			  nullptr, &Allocation.m_Allocation),
		EResult::SUCCESS);
	EXPECT_TRUE(Output.m_Image.DataEquals(Expected.m_Image));
}

TEST(QmMapCoverResult, InvalidSourceAndOutputSizesRemainInvalidWithoutOutputAllocation)
{
	CScopedImage Source(4, 2), Output(1, 1);
	ASSERT_NE(Source.m_Image.m_pData, nullptr);
	ASSERT_NE(Output.m_Image.m_pData, nullptr);
	const auto *pOriginal = Output.m_Image.m_pData;
	const QmMapCover::SArt Art;
	COutputAllocation Allocation;
	for(const int Width : {0, 1, QmMapCover::MAX_WIDTH + 1})
	{
		SCOPED_TRACE(Width);
		EXPECT_EQ(QmMapCover::ComposeChecked(Source.m_Image, Width, 32, Art, Output.m_Image, &Allocation.m_Allocation), EResult::INVALID);
	}
	Source.m_Image.m_Format = CImageInfo::FORMAT_UNDEFINED;
	EXPECT_EQ(QmMapCover::ComposeChecked(Source.m_Image, 64, 32, Art, Output.m_Image, &Allocation.m_Allocation), EResult::INVALID);
	Source.m_Image.m_Format = CImageInfo::FORMAT_RGB;
	const uint8_t aTruncated[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	EXPECT_EQ(QmMapCover::DecodeAndComposeChecked(aTruncated, sizeof(aTruncated), "map_cover_result", 64, 32, Art, Output.m_Image,
			  nullptr, &Allocation.m_Allocation),
		EResult::INVALID);
	EXPECT_FALSE(QmMapCover::DecodeAndCompose(aTruncated, sizeof(aTruncated), "map_cover_result", 64, 32, Art, Output.m_Image));
	EXPECT_EQ(Allocation.m_Calls, 0);
	ExpectOriginalOutput(Output.m_Image, pOriginal);
}

TEST(QmMapCoverResult, PngDecodedByteLimitIsInvalidAndKeepsThePreviousOutput)
{
	// RGB 文件可编码，按真实解码器的 RGBA 最坏情况预算仍超过封面的 8 MiB 上限。
	CScopedImage Source(2048, 1025), Output(1, 1);
	ASSERT_NE(Source.m_Image.m_pData, nullptr);
	ASSERT_NE(Output.m_Image.m_pData, nullptr);
	CByteBufferWriter Png;
	ASSERT_TRUE(CImageLoader::SavePng(Png, Source.m_Image));
	COutputAllocation Allocation;
	const auto *pOriginal = Output.m_Image.m_pData;
	EXPECT_EQ(QmMapCover::DecodeAndComposeChecked(Png.Data(), Png.Size(), "map_cover_result", 64, 32, {}, Output.m_Image,
			  nullptr, &Allocation.m_Allocation),
		EResult::INVALID);
	EXPECT_EQ(Allocation.m_Calls, 0);
	ExpectOriginalOutput(Output.m_Image, pOriginal);
}
