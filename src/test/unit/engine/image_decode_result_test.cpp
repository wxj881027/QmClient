#include <engine/gfx/image_loader.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace
{
	using EKind = CPngReadAllocator::EKind;
	using EResult = CImageLoader::EPngLoadResult;

	struct SDecodedImage
	{
		CImageInfo m_Image;
		~SDecodedImage() { m_Image.Free(); }
	};

	// 只拒绝真实分配入口；PNG 读取、转换与 longjmp 清理仍执行生产实现。
	class CReadAllocator final : public CPngReadAllocator
	{
		struct SAllocation
		{
			void *m_pData = nullptr;
			EKind m_Kind = EKind::STATE;
		};
		std::array<SAllocation, 256> m_aLive{};
		std::array<size_t, 4> m_aCalls{};
		EKind m_RejectKind = EKind::STATE;
		size_t m_BeforeRejection = std::numeric_limits<size_t>::max();
		bool m_Reject = false;

	public:
		bool m_Rejected = false;
		bool m_TrackingFailed = false;
		size_t m_Live = 0;

		void Reject(EKind Kind, size_t BeforeRejection = 0) noexcept
		{
			m_RejectKind = Kind;
			m_BeforeRejection = BeforeRejection;
			m_Reject = true;
			m_Rejected = false;
		}
		void Accept() noexcept { m_Reject = false; }
		size_t Calls(EKind Kind) const noexcept { return m_aCalls[static_cast<size_t>(Kind)]; }
		void *Allocate(EKind Kind, size_t Size) noexcept override
		{
			++m_aCalls[static_cast<size_t>(Kind)];
			if(m_Reject && Kind == m_RejectKind)
			{
				if(m_BeforeRejection == 0)
				{
					m_Reject = false;
					m_Rejected = true;
					return nullptr;
				}
				--m_BeforeRejection;
			}
			void *pData = CPngReadAllocator::Allocate(Kind, Size);
			// 输出像素独立交给 CImageInfo::Free，不借用这个分配器跟踪其寿命。
			if(pData != nullptr && Kind != EKind::PIXELS)
			{
				for(auto &Allocation : m_aLive)
				{
					if(Allocation.m_pData == nullptr)
					{
						Allocation = {pData, Kind};
						++m_Live;
						return pData;
					}
				}
				m_TrackingFailed = true;
			}
			return pData;
		}
		void Deallocate(EKind Kind, void *pData) noexcept override
		{
			if(pData == nullptr)
				return;
			bool Found = false;
			for(auto &Allocation : m_aLive)
			{
				if(Allocation.m_pData == pData)
				{
					m_TrackingFailed |= Allocation.m_Kind != Kind;
					Allocation.m_pData = nullptr;
					--m_Live;
					Found = true;
					break;
				}
			}
			m_TrackingFailed |= !Found;
			CPngReadAllocator::Deallocate(Kind, pData);
		}
	};

	class ImageDecodeResult : public ::testing::Test
	{
	protected:
		std::array<uint8_t, 16> m_aPixels{12, 34, 56, 255, 78, 90, 12, 128, 34, 56, 78, 64, 90, 12, 34, 0};
		CByteBufferWriter m_Png;

		void SetUp() override
		{
			CImageInfo Image;
			Image.m_Width = Image.m_Height = 2;
			Image.m_Format = CImageInfo::FORMAT_RGBA;
			Image.m_pData = m_aPixels.data();
			// 非关键文字使真实 libpng 的可选分配也进入分配拒绝矩阵。
			ASSERT_TRUE(CImageLoader::SavePng(m_Png, Image, "decode allocation classification"));
		}
		EResult Decode(CImageInfo &Image, CReadAllocator &Allocator, size_t Budget = std::numeric_limits<size_t>::max())
		{
			return CImageLoader::LoadPngChecked(m_Png.Data(), m_Png.Size(), "decode-result", Image, Budget, &Allocator);
		}
		void ExpectPixels(const CImageInfo &Image)
		{
			ASSERT_NE(Image.m_pData, nullptr);
			EXPECT_EQ(Image.m_Width, 2u);
			EXPECT_EQ(Image.m_Height, 2u);
			EXPECT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
			EXPECT_EQ(std::memcmp(Image.m_pData, m_aPixels.data(), m_aPixels.size()), 0);
		}
		void ExpectReleased(const CReadAllocator &Allocator)
		{
			EXPECT_EQ(Allocator.m_Live, 0u);
			EXPECT_FALSE(Allocator.m_TrackingFailed);
		}
	};
}

TEST_F(ImageDecodeResult, CheckedReaderPreservesDecodedPixelsAndCompatibility)
{
	CReadAllocator Allocator;
	SDecodedImage Checked;
	CByteBufferReader Reader(m_Png.Data(), m_Png.Size());
	int CheckedIncompatible = -1;
	EXPECT_EQ(CImageLoader::LoadPngChecked(Reader, "checked-reader", Checked.m_Image, CheckedIncompatible, m_aPixels.size(), &Allocator), EResult::SUCCESS);
	ExpectPixels(Checked.m_Image);
	ExpectReleased(Allocator);

	SDecodedImage Legacy;
	CByteBufferReader LegacyReader(m_Png.Data(), m_Png.Size());
	int LegacyIncompatible = -2;
	ASSERT_TRUE(CImageLoader::LoadPng(LegacyReader, "legacy-reader", Legacy.m_Image, LegacyIncompatible, m_aPixels.size()));
	ExpectPixels(Legacy.m_Image);
	EXPECT_EQ(CheckedIncompatible, LegacyIncompatible);
}

TEST_F(ImageDecodeResult, CallerAllocationRefusalsAreResourceErrorsAndCanRecover)
{
	for(const auto Kind : {EKind::STATE, EKind::PIXELS, EKind::ROWS})
	{
		SCOPED_TRACE(static_cast<int>(Kind));
		CReadAllocator Allocator;
		SDecodedImage Image;
		Allocator.Reject(Kind);
		EXPECT_EQ(Decode(Image.m_Image, Allocator), EResult::RESOURCE_ERROR);
		EXPECT_TRUE(Allocator.m_Rejected);
		EXPECT_EQ(Image.m_Image.m_pData, nullptr);
		ExpectReleased(Allocator);
		Allocator.Accept();
		ASSERT_EQ(Decode(Image.m_Image, Allocator), EResult::SUCCESS);
		ExpectPixels(Image.m_Image);
		ExpectReleased(Allocator);
	}
}

TEST_F(ImageDecodeResult, EveryLibpngAllocationRefusalKeepsValidDataSeparateFromInvalidInput)
{
	size_t Allocations;
	{
		CReadAllocator Allocator;
		SDecodedImage Image;
		ASSERT_EQ(Decode(Image.m_Image, Allocator), EResult::SUCCESS);
		Allocations = Allocator.Calls(EKind::LIBPNG);
		ExpectReleased(Allocator);
	}
	ASSERT_GE(Allocations, 2u);
	ASSERT_LE(Allocations, 256u);
	for(size_t BeforeRejection = 0; BeforeRejection < Allocations; ++BeforeRejection)
	{
		SCOPED_TRACE(BeforeRejection);
		CReadAllocator Allocator;
		SDecodedImage Image;
		Allocator.Reject(EKind::LIBPNG, BeforeRejection);
		const auto Result = Decode(Image.m_Image, Allocator);
		EXPECT_TRUE(Allocator.m_Rejected);
		EXPECT_NE(Result, EResult::INVALID);
		if(Result == EResult::SUCCESS)
			ExpectPixels(Image.m_Image);
		else
			EXPECT_EQ(Image.m_Image.m_pData, nullptr);
		ExpectReleased(Allocator);
		Image.m_Image.Free();
		Allocator.Accept();
		ASSERT_EQ(Decode(Image.m_Image, Allocator), EResult::SUCCESS);
		ExpectPixels(Image.m_Image);
		ExpectReleased(Allocator);
	}
}

TEST_F(ImageDecodeResult, InvalidSignatureReturnsInvalidWithoutPixels)
{
	CReadAllocator Allocator;
	SDecodedImage Image;
	const std::array<uint8_t, 8> aInvalid{};
	EXPECT_EQ(CImageLoader::LoadPngChecked(aInvalid.data(), aInvalid.size(), "invalid-signature", Image.m_Image, 16, &Allocator), EResult::INVALID);
	EXPECT_FALSE(Allocator.m_Rejected);
	EXPECT_EQ(Allocator.Calls(EKind::PIXELS), 0u);
	EXPECT_EQ(Image.m_Image.m_pData, nullptr);
	ExpectReleased(Allocator);
}

TEST_F(ImageDecodeResult, TruncatedStreamReturnsInvalidAndReleasesReaderState)
{
	CReadAllocator Allocator;
	SDecodedImage Image;
	// 保留签名与部分内容，由真实读取回调触发 libpng 错误回跳。
	ASSERT_GT(m_Png.Size(), 16u);
	EXPECT_EQ(CImageLoader::LoadPngChecked(m_Png.Data(), m_Png.Size() / 2, "truncated-result", Image.m_Image, 16, &Allocator), EResult::INVALID);
	EXPECT_EQ(Image.m_Image.m_pData, nullptr);
	ExpectReleased(Allocator);
	ASSERT_EQ(Decode(Image.m_Image, Allocator), EResult::SUCCESS);
	ExpectPixels(Image.m_Image);
	ExpectReleased(Allocator);
}

TEST_F(ImageDecodeResult, DecodeBudgetRejectionIsInvalidBeforePixelAllocation)
{
	CReadAllocator Allocator;
	SDecodedImage Image;
	EXPECT_EQ(Decode(Image.m_Image, Allocator, m_aPixels.size() - 1), EResult::INVALID);
	EXPECT_EQ(Allocator.Calls(EKind::PIXELS), 0u);
	EXPECT_EQ(Allocator.Calls(EKind::ROWS), 0u);
	EXPECT_EQ(Image.m_Image.m_pData, nullptr);
	ExpectReleased(Allocator);
	ASSERT_EQ(Decode(Image.m_Image, Allocator, m_aPixels.size()), EResult::SUCCESS);
	ExpectPixels(Image.m_Image);
	ExpectReleased(Allocator);
}

TEST_F(ImageDecodeResult, SuccessfulPixelsOutliveTheBorrowedAllocator)
{
	SDecodedImage Image;
	{
		CReadAllocator Allocator;
		ASSERT_EQ(Decode(Image.m_Image, Allocator), EResult::SUCCESS);
		ExpectReleased(Allocator);
	}
	ExpectPixels(Image.m_Image);
	Image.m_Image.Free();
	EXPECT_EQ(Image.m_Image.m_pData, nullptr);
}
