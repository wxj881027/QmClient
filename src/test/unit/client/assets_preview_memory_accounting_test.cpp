#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/menus.h>
#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>
#include <test/support/icon_benchmark_graphics.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
	constexpr uint64_t MiB = 1024ull * 1024ull;
	constexpr uint64_t FourGiB = 4096ull * MiB;
	constexpr uint64_t MaxByteCount = std::numeric_limits<uint64_t>::max();

	class CResidentMemoryGraphics : public CIconBenchmarkGraphics
	{
	public:
		CTextureHandle Handle(int Id) { return CreateTextureHandle(Id); }
	};
}

TEST(AssetsPreviewMemoryAccounting, EmptyOwnedListHasNoResidentBytes)
{
	const std::vector<CMenus::SCustomGame> vItems;
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), 0u);
}

TEST(AssetsPreviewMemoryAccounting, OwnedResidentCountOnlyIncludesValidRenderTextures)
{
	CResidentMemoryGraphics Graphics;
	std::vector<CMenus::SCustomGame> vItems(3);
	vItems[0].m_PreviewResidentBytes = 64 * MiB;
	vItems[1].m_RenderTexture = Graphics.Handle(1);
	vItems[1].m_PreviewResidentBytes = 32 * MiB;
	vItems[2].m_RenderTexture = Graphics.Handle(2);
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), 32 * MiB);

	vItems[1].m_RenderTexture.Invalidate();
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), 0u);
}

TEST(AssetsPreviewMemoryAccounting, OwnedResidentCounterCanCrossFourGiB)
{
	CResidentMemoryGraphics Graphics;
	std::vector<CMenus::SCustomGame> vItems(65);
	for(int Index = 0; Index < 64; ++Index)
	{
		vItems[Index].m_RenderTexture = Graphics.Handle(Index);
		vItems[Index].m_PreviewResidentBytes = 64 * MiB;
	}
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), FourGiB);

	vItems.back().m_RenderTexture = Graphics.Handle(64);
	vItems.back().m_PreviewResidentBytes = MiB;
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), FourGiB + MiB);
}

TEST(AssetsPreviewMemoryAccounting, OwnedListReallocationPreservesResidentCount)
{
	CResidentMemoryGraphics Graphics;
	std::vector<CMenus::SCustomGame> vItems(1);
	vItems[0].m_aName[0] = '\0';
	vItems[0].m_RenderTexture = Graphics.Handle(1);
	vItems[0].m_PreviewResidentBytes = 64 * MiB;
	const size_t PreviousCapacity = vItems.capacity();
	vItems.reserve(PreviousCapacity + 1);
	ASSERT_GT(vItems.capacity(), PreviousCapacity);
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), 64 * MiB);

	vItems.emplace_back();
	vItems.back().m_RenderTexture = Graphics.Handle(2);
	vItems.back().m_PreviewResidentBytes = 32 * MiB;
	EXPECT_EQ(SettingsAssetPreviewResidentBytes(vItems), 96 * MiB);
}

TEST(AssetsPreviewMemoryAccounting, BudgetWrapperRetains64BitByteValues)
{
	EXPECT_EQ(SettingsAssetPreviewResidentBudgetBytes(4096, 0, -1.0f), FourGiB);
	EXPECT_EQ(SettingsAssetPreviewResidentBudgetBytes(16384, 8, 4096.0f), 4 * FourGiB);
}

TEST(AssetsPreviewMemoryAccounting, BudgetWithEnoughHeadroomKeepsLargestTier)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 96 * MiB, 16 * MiB, 0), 4096);
}

TEST(AssetsPreviewMemoryAccounting, ReplacementDiscountsOnlyTheCurrentResidentTexture)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 80 * MiB, 64 * MiB, 64 * MiB), 4096);
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 80 * MiB, 64 * MiB, 0), 2048);
}

TEST(AssetsPreviewMemoryAccounting, ReplacementLargerThanUsageClampsBaseToZero)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 64 * MiB, 16 * MiB, 32 * MiB), 4096);
}

TEST(AssetsPreviewMemoryAccounting, ExactlyFittingEstimateKeepsLargestTier)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 80 * MiB, 16 * MiB, 0), 4096);
}

TEST(AssetsPreviewMemoryAccounting, EstimateOneByteAboveBudgetUsesNextTier)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 80 * MiB - 1, 16 * MiB, 0), 2048);
}

TEST(AssetsPreviewMemoryAccounting, BaseAlreadyAboveBudgetKeepsMinimumTier)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 16 * MiB, 32 * MiB, 0), 512);
}

TEST(AssetsPreviewMemoryAccounting, DisabledBudgetPreservesLargestTier)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 0, MaxByteCount, 0), 4096);
}

TEST(AssetsPreviewMemoryAccounting, MemoryAboveFourGiBIsNotNarrowed)
{
	// 显式宽字节量检查生产接口，不代表已执行原生32位客户端。
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 32 * MiB, FourGiB, 0), 512);
}

TEST(AssetsPreviewMemoryAccounting, BudgetAboveFourGiBRemainsEnforced)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, FourGiB, 2 * FourGiB, 0), 512);
}

TEST(AssetsPreviewMemoryAccounting, AdditionThatWouldWrapToZeroDoesNotAdmitLargestTier)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 32 * MiB, MaxByteCount - 64 * MiB + 1, 0), 512);
}

TEST(AssetsPreviewMemoryAccounting, MaximumBudgetRejectsEstimateAboveRemainingSpace)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, MaxByteCount, MaxByteCount - 64 * MiB + 1, 0), 2048);
}

TEST(AssetsPreviewMemoryAccounting, MaximumBudgetAcceptsExactRemainingSpace)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, MaxByteCount, MaxByteCount - 64 * MiB, 0), 4096);
}

TEST(AssetsPreviewMemoryAccounting, ReplacementDiscountPreservesHighCounterBits)
{
	EXPECT_EQ(ComputePreviewBudgetedTextureSize(4096, 512, 80 * MiB, MaxByteCount, MaxByteCount - 16 * MiB), 4096);
}

TEST(AssetsPreviewMemoryAccounting, UploadCapStillLimitsWideMemoryCountersTo512)
{
	struct SCase
	{
		const char *m_pName;
		uint64_t m_Budget;
		uint64_t m_Current;
		uint64_t m_Replaced;
	};
	for(const SCase &Case : std::array<SCase, 6>{
		    SCase{"disabled", 0, MaxByteCount, 0},
		    SCase{"normal", 512 * MiB, 0, 0},
		    SCase{"exact_with_replacement", 80 * MiB, 64 * MiB, 48 * MiB},
		    SCase{"over_budget", 16 * MiB, 32 * MiB, 0},
		    SCase{"addition_overflow", 32 * MiB, MaxByteCount - 64 * MiB + 1, 0},
		    SCase{"maximum_budget", MaxByteCount, MaxByteCount - 64 * MiB + 1, 0}})
	{
		SCOPED_TRACE(Case.m_pName);
		for(int MaxTextureSize : std::array<int, 2>{LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE, WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE})
		{
			SCOPED_TRACE(MaxTextureSize);
			const int TextureSize = SettingsAssetPreviewBudgetedTextureSize(MaxTextureSize, ASSET_PREVIEW_MIN_TEXTURE_SIZE, Case.m_Budget, Case.m_Current, Case.m_Replaced);
			EXPECT_EQ(TextureSize, ASSET_PREVIEW_MIN_TEXTURE_SIZE);
			EXPECT_LE(PreviewTextureSizeBytesEstimate(TextureSize), ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME);
		}
	}
}
