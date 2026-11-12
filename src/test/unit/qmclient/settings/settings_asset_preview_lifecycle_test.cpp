#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsAssetPreviewLifecycle, DecodeCanStartWhileMerging)
{
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(true, false, false));
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(false, true, false));
	EXPECT_TRUE(SettingsAssetListCanStartPreviewDecode(false, false, true));
	EXPECT_FALSE(SettingsAssetListCanStartPreviewDecode(false, false, false));
}

TEST(SettingsAssetPreviewLifecycle, FinalizeBudgetDefersAfterLimit)
{
	EXPECT_FALSE(SettingsAssetPreviewShouldDeferFinalize(0, 10.0, 2, 4.0));
	EXPECT_FALSE(SettingsAssetPreviewShouldDeferFinalize(1, 3.5, 2, 4.0));
	EXPECT_TRUE(SettingsAssetPreviewShouldDeferFinalize(1, 4.0, 2, 4.0));
	EXPECT_TRUE(SettingsAssetPreviewShouldDeferFinalize(2, 0.0, 2, 4.0));
}

// 软重扫合并会重排资源列表，条目下标随之变化。收尾与上传都按名字认领：
// 下标变了的句柄仍然算这张卡，否则刚解码好的贴图会被当成过期丢掉，
// 用户看到「加载完又变回灰块」「一直停在占位」。
TEST(SettingsAssetPreviewLifecycle, HandleStillClaimsAssetWhenListIndexShifted)
{
	SSettingsAssetPreviewHandle Handle;
	Handle.m_Tab = 3;
	Handle.m_Epoch = 7;
	Handle.m_Index = 5;
	Handle.m_Name = "hud_custom";

	// 同名的卡片换了座位：仍然属于同一张卡。
	EXPECT_TRUE(SettingsAssetPreviewHandleReferencesAsset(Handle, "hud_custom"));
	// 别的卡（哪怕现在占着旧下标）不认。
	EXPECT_FALSE(SettingsAssetPreviewHandleReferencesAsset(Handle, "hud_other"));
	// 空名字是拿不到卡的兜底路径，不能误判成匹配。
	EXPECT_FALSE(SettingsAssetPreviewHandleReferencesAsset(Handle, nullptr));
}

TEST(SettingsAssetPreviewLifecycle, StrictHandleMatchStillRejectsShiftedIndex)
{
	SSettingsAssetPreviewHandle Handle;
	Handle.m_Tab = 3;
	Handle.m_Epoch = 7;
	Handle.m_Index = 5;
	Handle.m_Name = "hud_custom";

	// 同 tab、同 epoch、同名，只有下标变了：严格比对必须判为过期，
	// 这正是「按名字认领」要在收尾侧替代掉的旧语义。
	EXPECT_FALSE(SettingsAssetPreviewHandleMatches(Handle, 3, 7, 4, "hud_custom"));
	EXPECT_TRUE(SettingsAssetPreviewHandleMatches(Handle, 3, 7, 5, "hud_custom"));
}
