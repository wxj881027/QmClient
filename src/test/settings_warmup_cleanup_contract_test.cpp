// 设置预热旧缓存清理合同。运行时行为保留在 settings_warmup_test.cpp。
#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>

TEST(SettingsWarmupCleanup, SourceNoLongerReferencesSettingsPageFboPaths)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	EXPECT_TRUE(ContainsAll(Menus, {"PrewarmSettingsPageRuntimeCache"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"DrawSettingsPageRuntimeCache"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"InvalidateSettingsPageRuntimeCache"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"PrewarmSettingsSectionRuntimeCache"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"DrawSettingsSectionRuntimeCache"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"InvalidateSettingsSectionRuntimeCache"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"DestroySettingsPageRuntimeCaches"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"PrepareGenericSettingsRuntimeCacheSection"}) == false);
	EXPECT_TRUE(ContainsAll(Menus, {"MakeSettingsPageRuntimeKey"}) == false);
}

TEST(SettingsWarmupCleanup, SourceNoLongerReferencesSettingsRuntimeFboContracts)
{
	const std::string RuntimeHeader = ReadRepoFile("src/game/client/components/settings_runtime_cache.h");
	const std::string WarmupHeader = ReadRepoFile("src/game/client/components/settings_warmup.h");
	const std::string Resource = ReadRepoFile("src/game/client/components/settings_resource_jobs.cpp");
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");

	EXPECT_FALSE(ContainsAny(RuntimeHeader, {"RENDER_TARGET_RECORD", "FBO_BUDGET", "GPU_READBACK_BUDGET", "PREVIEW_CACHE_IO_BUDGET", "PAGE_FBO_UNSUPPORTED", "PAGE_FBO_NOT_READY", "SECTION_FBO_NOT_READY", "m_MaxRenderTargetRecords", "m_MaxGpuReadbacks", "m_MaxPreviewCacheIo", "SSettingsPageRuntimeRegistry", "SSettingsRuntimeCacheMetadata", "SSettingsWarmupPageJob", "SETTINGS_PAGE_RUNTIME_CACHE_SLOTS", "SettingsPageRuntimeCacheSlot", "SettingsSectionCanRecordStaticFbo", "SettingsWarmupEnabled", "SettingsRuntimeCachingEnabled", "SettingsInvalidationClearsSectionFbo", "SettingsInvalidationClearsPageFbo"}));
	EXPECT_FALSE(ContainsAny(WarmupHeader, {"RUNTIME_FBO", "SSettingsPageRuntimeCacheState", "SettingsPageRuntimeCacheShouldShortCircuit"}));
	EXPECT_FALSE(ContainsAny(Resource, {"SettingsPageCacheCanUseRecordedResources", "SettingsPageRecordedCacheMissReason", "SettingsPageCanUsePageFbo"}));
	EXPECT_FALSE(ContainsAny(Skins, {"FBO_BUDGET"}));
}

TEST(SettingsWarmupCleanup, SourceKeepsTeeMemoryPreviewCacheAndWorkshopThumbCache)
{
	const std::string MenusSettings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_TRUE(ContainsAll(MenusSettings, {"SSettingsTeeListPreviewCache", "gs_TeeListPreviewCache"}));
	EXPECT_FALSE(ContainsAny(MenusSettings, {"settings_skin_preview_cache"}));
	EXPECT_TRUE(ContainsAll(Assets, {"qmclient/workshop/thumbs/%s.webp", "m_ThumbCachePath"}));
}
