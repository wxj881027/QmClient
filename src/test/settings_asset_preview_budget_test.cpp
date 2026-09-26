#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsAssetPreviewBudget, TextureSizeFitsUploadBudget)
{
	const int TextureSize = SettingsAssetPreviewBudgetedTextureSize(
		LOCAL_ASSET_PREVIEW_MAX_TEXTURE_SIZE,
		ASSET_PREVIEW_MIN_TEXTURE_SIZE,
		512ull * 1024ull * 1024ull,
		0,
		0);
	EXPECT_EQ(TextureSize, ASSET_PREVIEW_MIN_TEXTURE_SIZE);
	EXPECT_LE(PreviewTextureSizeBytesEstimate(TextureSize), ASSET_PREVIEW_UPLOAD_MAX_BYTES_PER_FRAME);
}
