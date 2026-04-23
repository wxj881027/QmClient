#include <game/client/components/assets_preview_scale.h>

#include <gtest/gtest.h>

TEST(AssetsPreviewScale, KeepsLocalPreviewAtOrAbove1024Cap)
{
	const auto Scaled = ComputePreviewTargetSize(2048, 1024, LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
	EXPECT_EQ(Scaled.m_Width, 1024);
	EXPECT_EQ(Scaled.m_Height, 512);
}

TEST(AssetsPreviewScale, KeepsWorkshopPreviewAtOrAbove512Cap)
{
	const auto Scaled = ComputePreviewTargetSize(2048, 1024, WORKSHOP_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
	EXPECT_EQ(Scaled.m_Width, 512);
	EXPECT_EQ(Scaled.m_Height, 256);
}

TEST(AssetsPreviewScale, DoesNotUpscaleSmallImages)
{
	const auto Scaled = ComputePreviewTargetSize(640, 320, LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE);
	EXPECT_EQ(Scaled.m_Width, 640);
	EXPECT_EQ(Scaled.m_Height, 320);
}
