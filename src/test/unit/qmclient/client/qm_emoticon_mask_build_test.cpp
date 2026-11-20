#include <base/borrowed_allocator.h>

#include <game/client/components/qmclient/emoticon_projectile.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <new>

namespace
{
	class CFailureResource : public IAllocationResource
	{
		void *Allocate(std::size_t Bytes, std::size_t Alignment) override
		{
			++m_Calls;
			if(m_Calls == m_FailAt)
				throw std::bad_alloc();
			void *pMemory = IAllocationResource::AllocateDefault(Bytes, Alignment);
			++m_LiveBlocks;
			return pMemory;
		}
		void Deallocate(void *pMemory, std::size_t Bytes, std::size_t Alignment) noexcept override
		{
			--m_LiveBlocks;
			IAllocationResource::DeallocateDefault(pMemory, Bytes, Alignment);
		}

	public:
		int m_Calls = 0;
		int m_FailAt = 0;
		int m_LiveBlocks = 0;
	};

	std::array<unsigned char, 16 * 16 * 4> CheckerboardPixels()
	{
		std::array<unsigned char, 16 * 16 * 4> Pixels{};
		for(int Y = 0; Y < 16; ++Y)
			for(int X = 0; X < 16; ++X)
				if((X + Y) % 2 == 0)
					Pixels[(Y * 16 + X) * 4 + 3] = 255;
		return Pixels;
	}

	CImageInfo RgbaImage(unsigned char *pPixels)
	{
		CImageInfo Image;
		Image.m_pData = pPixels;
		Image.m_Width = Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		return Image;
	}
}

TEST(QmEmoticonMaskBuild, EveryAllocationFailureClearsOldAndPartialGeometryAndCanRetry)
{
	const unsigned char aOpaque[] = {255, 255, 255, 255};
	const auto Pixels = CheckerboardPixels();
	int Allocations = 0;
	CFailureResource BaselineMemory;
	{
		QmEmoticon::CAlphaMask Mask(&BaselineMemory);
		ASSERT_TRUE(Mask.Build(aOpaque, 1, 1));
		BaselineMemory.m_Calls = 0;
		ASSERT_TRUE(Mask.Build(Pixels.data(), 16, 16));
		Allocations = BaselineMemory.m_Calls;
		ASSERT_GT(Allocations, 3);
	}
	EXPECT_EQ(BaselineMemory.m_LiveBlocks, 0);

	for(int FailAt = 1; FailAt <= Allocations; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		CFailureResource Memory;
		{
			QmEmoticon::CAlphaMask Mask(&Memory);
			ASSERT_TRUE(Mask.Build(aOpaque, 1, 1));
			Memory.m_Calls = 0;
			Memory.m_FailAt = FailAt;
			EXPECT_FALSE(Mask.Build(Pixels.data(), 16, 16));
			EXPECT_EQ(Mask.NumRects(), 0u);
			EXPECT_EQ(Memory.m_LiveBlocks, 0);
			EXPECT_FALSE(Mask.Overlaps(vec2(0, 0), 64.0f, 0.0f, [](int, int) { return true; }));
			EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(1, 1)));

			Memory.m_FailAt = 0;
			ASSERT_TRUE(Mask.Build(Pixels.data(), 16, 16));
			EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(-30, -30), vec2(0.5f, 0.5f)));
			EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(-26, -30), vec2(0.5f, 0.5f)));
		}
		EXPECT_EQ(Memory.m_LiveBlocks, 0);
	}
}

TEST(QmEmoticonMaskBuild, FirstBuildFailureLeavesEmptyMaskAndCanRetry)
{
	const unsigned char aOpaque[] = {255, 255, 255, 255};
	CFailureResource Memory;
	Memory.m_FailAt = 1;
	{
		QmEmoticon::CAlphaMask Mask(&Memory);
		EXPECT_FALSE(Mask.Build(aOpaque, 1, 1));
		EXPECT_EQ(Mask.NumRects(), 0u);
		Memory.m_FailAt = 0;
		ASSERT_TRUE(Mask.Build(aOpaque, 1, 1));
		EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(1, 1)));
	}
	EXPECT_EQ(Memory.m_LiveBlocks, 0);
}

TEST(QmEmoticonMaskBuild, ShortStrideClearsOldMaskWithoutReadingPixels)
{
	const unsigned char aOpaque[] = {255, 255, 255, 255};
	QmEmoticon::CAlphaMask Mask;
	ASSERT_TRUE(Mask.Build(aOpaque, 1, 1));
	EXPECT_FALSE(Mask.Build(aOpaque, 2, 1, 4));
	EXPECT_EQ(Mask.NumRects(), 0u);
	EXPECT_FALSE(Mask.Build(aOpaque, std::numeric_limits<int>::max(), 1));
	EXPECT_EQ(Mask.NumRects(), 0u);
}

TEST(QmEmoticonMaskBuild, AlphaOffsetCanExceedSignedIntRange)
{
	EXPECT_EQ(QmEmoticon::AlphaByteOffset(0, 16384, 131072), std::size_t{2147483651u});
	EXPECT_EQ(QmEmoticon::AlphaByteOffset(3, 16384, 131072), std::size_t{2147483663u});
}

TEST(QmEmoticonMaskBuild, PaddedRowsUseTheAtlasStride)
{
	std::array<unsigned char, 12> Pixels{};
	Pixels[11] = 255;
	QmEmoticon::CAlphaMask Mask;
	ASSERT_TRUE(Mask.Build(Pixels.data(), 1, 2, 8));
	EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 16), vec2(1, 1)));
	EXPECT_FALSE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, -16), vec2(1, 1)));
}

TEST(QmEmoticonMaskBuild, SpritePreparationReportsAllocationFailureAndRetries)
{
	unsigned char aOpaque[] = {255, 255, 255, 255};
	const auto Image = RgbaImage(aOpaque);
	CDataSpriteset Set{nullptr, 1, 1};
	const CDataSprite Sprite{"test", &Set, 0, 0, 1, 1};
	CFailureResource Memory;
	Memory.m_FailAt = 1;
	{
		QmEmoticon::CAlphaMask Mask(&Memory);
		EXPECT_FALSE(Mask.BuildSprite(Image, Sprite));
		EXPECT_EQ(Mask.NumRects(), 0u);
		Memory.m_FailAt = 0;
		ASSERT_TRUE(Mask.BuildSprite(Image, Sprite));
		EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(1, 1)));
	}
	EXPECT_EQ(Memory.m_LiveBlocks, 0);
}

TEST(QmEmoticonMaskBuild, FailedFallbackRebuildDoesNotKeepOldContour)
{
	unsigned char aOpaque[] = {255, 255, 255, 255};
	unsigned char aBlank[] = {0, 0, 0, 0};
	const auto Image = RgbaImage(aBlank);
	const auto Fallback = RgbaImage(aOpaque);
	CDataSpriteset Set{nullptr, 1, 1};
	const CDataSprite Sprite{"test", &Set, 0, 0, 1, 1};
	CFailureResource Memory;
	{
		QmEmoticon::CAlphaMask Mask(&Memory);
		ASSERT_TRUE(Mask.Build(aOpaque, 1, 1));
		Memory.m_FailAt = Memory.m_Calls + 1;
		EXPECT_FALSE(Mask.BuildSprite(Image, Sprite, &Fallback));
		EXPECT_EQ(Mask.NumRects(), 0u);
		EXPECT_FALSE(Mask.Overlaps(vec2(0, 0), 64.0f, 0.0f, [](int, int) { return true; }));
		Memory.m_FailAt = 0;
		ASSERT_TRUE(Mask.BuildSprite(Image, Sprite, &Fallback));
		EXPECT_TRUE(Mask.OverlapsBox(vec2(0, 0), 64.0f, 0.0f, vec2(0, 0), vec2(1, 1)));
	}
	EXPECT_EQ(Memory.m_LiveBlocks, 0);
}
