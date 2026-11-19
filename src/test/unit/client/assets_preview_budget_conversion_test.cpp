#include <game/client/components/assets_preview_scale.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

TEST(AssetsPreviewBudgetConversion, Uint32OverridePreservesLargestWholeMegabyteThatFits)
{
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(4095, 8, 4096.0f), 4293918720u);
}

TEST(AssetsPreviewBudgetConversion, Uint32AllowedOverridesAboveFourGiBSaturateInsteadOfDisablingBudget)
{
	for(size_t OverrideMb : std::array<size_t, 3>{4096, 8192, 16384})
	{
		SCOPED_TRACE(OverrideMb);
		EXPECT_EQ(PreviewBudgetBytes<uint32_t>(OverrideMb, 8, 4096.0f), std::numeric_limits<uint32_t>::max());
	}
}

TEST(AssetsPreviewBudgetConversion, Uint64PreservesAllowedOverridesWithoutNarrowing)
{
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(4096, 8, 4096.0f), 4294967296ull);
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(8192, 8, 4096.0f), 8589934592ull);
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(16384, 8, 4096.0f), 17179869184ull);
}

TEST(AssetsPreviewBudgetConversion, Uint32PercentagePreservesLargestWholeMegabyteThatFits)
{
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 100, 4095.0f), 4293918720u);
}

TEST(AssetsPreviewBudgetConversion, Uint32PercentageAtOrAboveFourGiBSaturates)
{
	for(float GpuBudgetMb : std::array<float, 3>{4096.0f, 8192.0f, 65536.0f})
	{
		SCOPED_TRACE(GpuBudgetMb);
		EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 100, GpuBudgetMb), std::numeric_limits<uint32_t>::max());
	}
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 8, 65536.0f), std::numeric_limits<uint32_t>::max());
}

TEST(AssetsPreviewBudgetConversion, Uint32PercentageBelowLimitKeepsExistingWholeMegabyteRounding)
{
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 100, 4095.75f), 4293918720u);
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 8, 4096.0f), 342884352u);
}

TEST(AssetsPreviewBudgetConversion, Uint64PercentageAtFirstUnrepresentableWholeMegabyteSaturates)
{
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(0, 100, 0x1p44f), std::numeric_limits<uint64_t>::max());
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(0, 100, 0x1p45f), std::numeric_limits<uint64_t>::max());
}

TEST(AssetsPreviewBudgetConversion, DisabledOrUnavailableAutoBudgetKeepsZeroForBothWidths)
{
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 0, 4096.0f), 0u);
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 8, -1.0f), 0u);
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(0, 8, 0.0f), 0u);
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(0, 0, 4096.0f), 0ull);
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(0, 8, -1.0f), 0ull);
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(0, 8, 0.0f), 0ull);
}

TEST(AssetsPreviewBudgetConversion, SaturatedExplicitOverrideStillPrecedesDisabledAutoBudget)
{
	EXPECT_EQ(PreviewBudgetBytes<uint32_t>(4096, 0, -1.0f), std::numeric_limits<uint32_t>::max());
	EXPECT_EQ(PreviewBudgetBytes<uint64_t>(4096, 0, -1.0f), 4294967296ull);
}
