#include <game/client/components/settings_resource_jobs.h>
#include <game/client/components/settings_warmup.h>

#include <gtest/gtest.h>

#include <limits>

TEST(SettingsRuntimeCache, BudgetStopsEveryMainThreadCost)
{
	SSettingsWarmupFrameBudget Budget;
	Budget.m_MaxTextContainers = 1;
	Budget.m_MaxGpuUploads = 1;
	Budget.m_MaxJobResultMerges = 1;

	EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::TEXT_CONTAINER));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::TEXT_CONTAINER));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::TEXT_BUDGET);

	Budget.m_StopReason = ESettingsWarmupStopReason::NONE;
	EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);

	Budget.m_StopReason = ESettingsWarmupStopReason::NONE;
	EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::JOB_RESULT_MERGE));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::JOB_RESULT_MERGE));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::MERGE_BUDGET);
}

TEST(SettingsRuntimeCache, DefaultGpuBudgetAllowsOneSkinUploadBatch)
{
	SSettingsWarmupFrameBudget Budget;
	for(int Upload = 0; Upload < 14; ++Upload)
		EXPECT_TRUE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_FALSE(SettingsWarmupConsumeBudget(Budget, ESettingsWarmupCost::GPU_UPLOAD));
	EXPECT_EQ(Budget.m_StopReason, ESettingsWarmupStopReason::GPU_UPLOAD_BUDGET);
}

TEST(SettingsRuntimeCache, BudgetStopReasonsMapToProductionMissReasons)
{
	EXPECT_STREQ(SettingsWarmupBudgetStopMissReasonName(ESettingsWarmupStopReason::TEXT_BUDGET), "text_budget");
	EXPECT_STREQ(SettingsWarmupBudgetStopMissReasonName(ESettingsWarmupStopReason::NONE), "none");
}

TEST(SettingsRuntimeCache, TClientPerfStageNamesAreStable)
{
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::TAB_SHELL), "tclient_tab_shell");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::SECTION_LAYOUT), "tclient_section_layout");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::TEXT_CACHE), "tclient_text_cache");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::RESOURCE_PRETRIGGER), "tclient_resource_pretrigger");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::STATIC_LAYER), "tclient_static_layer");
	EXPECT_STREQ(SettingsTClientPerfStageName(ETClientSettingsPerfStage::INTERACTIVE_LAYER), "tclient_interactive_layer");
}

TEST(SettingsRuntimeCache, PerfReasonNamesAreStable)
{
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::NONE), "none");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::DEPENDENCY_NOT_READY), "dependency_not_ready");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::RESOURCE_PLAN_PENDING), "resource_plan_pending");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::JOB_RESULT_PENDING), "job_result_pending");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::GPU_UPLOAD_BUDGET), "gpu_upload_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::SHARED_HEAVY_BUDGET), "shared_heavy_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::UPLOAD_BYTES_BUDGET), "upload_bytes_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::OVERSIZED_UPLOAD_DEFERRED), "oversized_upload_deferred");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::TEXT_BUDGET), "text_budget");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::ACTIVE_ITEM), "active_item");
	EXPECT_STREQ(SettingsWarmupMissReasonName(ESettingsWarmupMissReason::INVALID_RUNTIME_KEY), "invalid_runtime_key");
}

TEST(SettingsRuntimeCache, InvalidationReasonNamesAreStable)
{
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::LANGUAGE_CHANGED), "language_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::FONT_CHANGED), "font_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::BACKEND_CHANGED), "backend_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED), "window_or_scale_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::DPI_CHANGED), "dpi_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::UI_SCALE_CHANGED), "ui_scale_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::CONFIG_HASH_CHANGED), "config_hash_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::SECTION_SIZE_CHANGED), "section_size_changed");
	EXPECT_STREQ(SettingsInvalidationReasonName(ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED), "resource_directory_changed");
}

TEST(SettingsRuntimeCache, ClearsTextPoolOnlyForContentChangingReasons)
{
	// 只有真正改变 label 文字内容、字形或渲染后端的 reason 才全池失效。
	EXPECT_TRUE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::LANGUAGE_CHANGED));
	EXPECT_TRUE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::FONT_CHANGED));
	EXPECT_TRUE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::BACKEND_CHANGED));

	// 布局尺寸 / 控件配置状态变化不影响文字内容或字形；DoMenuLabelStreamed 的
	// SizeChanged / TextChanged / ColorChanged 单 entry 检测已兜底，不需要全池失效。
	// 这是 ingame ESC 打开设置时 OnReset -> CONFIG_HASH_CHANGED 不再清池、
	// 避免“闪 + 卡 + 重加载文本池”的语义保障。
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::WINDOW_OR_SCALE_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::DPI_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::UI_SCALE_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::CONFIG_HASH_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::SECTION_SIZE_CHANGED));
	EXPECT_FALSE(SettingsInvalidationClearsTextPool(ESettingsInvalidationReason::RESOURCE_DIRECTORY_CHANGED));
}

TEST(SettingsRuntimeCache, RuntimeKeyMismatchNamesDirtyReason)
{
	SSettingsSectionCacheRuntimeKey Base;
	Base.m_ViewportWidth = 900;
	Base.m_ViewportHeight = 620;
	Base.m_UiScale = 100;
	Base.m_ConfigHash = 10;
	Base.m_LanguageHash = 11;
	Base.m_FontHash = 12;
	Base.m_BackendHash = 13;
	Base.m_WindowHash = 14;

	SSettingsSectionCacheRuntimeKey Language = Base;
	Language.m_LanguageHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Language), ESettingsCacheDirtyReason::LANGUAGE);

	SSettingsSectionCacheRuntimeKey Font = Base;
	Font.m_FontHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Font), ESettingsCacheDirtyReason::FONT);

	SSettingsSectionCacheRuntimeKey Backend = Base;
	Backend.m_BackendHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Backend), ESettingsCacheDirtyReason::GRAPHICS_RESET);

	SSettingsSectionCacheRuntimeKey UiScale = Base;
	UiScale.m_UiScale++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, UiScale), ESettingsCacheDirtyReason::UI_SCALE);

	SSettingsSectionCacheRuntimeKey Viewport = Base;
	Viewport.m_ViewportHeight++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Viewport), ESettingsCacheDirtyReason::WINDOW_SIZE);

	SSettingsSectionCacheRuntimeKey Config = Base;
	Config.m_ConfigHash++;
	EXPECT_EQ(SettingsRuntimeKeyMismatchDirtyReason(Base, Config), ESettingsCacheDirtyReason::CONFIG);
}

TEST(SettingsRuntimeCache, SectionCacheMetadataRequiresMatchingRuntimeKey)
{
	SSettingsSectionCacheRuntimeKey RuntimeKey;
	RuntimeKey.m_ViewportWidth = 900;
	RuntimeKey.m_ViewportHeight = 620;
	RuntimeKey.m_UiScale = 100;
	RuntimeKey.m_ConfigHash = 1234;
	RuntimeKey.m_LanguageHash = 5678;
	RuntimeKey.m_FontHash = 9012;
	RuntimeKey.m_BackendHash = 3456;
	RuntimeKey.m_WindowHash = 7890;

	SSettingsSectionCacheMetadata Metadata;
	Metadata.m_LastPage = EClassicSettingsPage::TCLIENT;
	Metadata.m_LastTab = 0;
	Metadata.m_LastScrollY = 140.0f;
	Metadata.m_SectionNameHash = 42;
	Metadata.m_SectionHeight = 180.0f;
	Metadata.m_RuntimeKey = RuntimeKey;

	EXPECT_TRUE(Metadata.Matches(RuntimeKey));

	RuntimeKey.m_ViewportHeight += 1;
	EXPECT_FALSE(Metadata.Matches(RuntimeKey));
}

TEST(SettingsRuntimeCache, NumericKeysRejectNonFiniteValues)
{
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(-std::numeric_limits<float>::infinity()), 1);
	EXPECT_EQ(SettingsRuntimeCachePositiveRoundedKey(-std::numeric_limits<float>::infinity()), 1);
	EXPECT_EQ(SettingsRuntimeCacheRoundedKey(std::numeric_limits<float>::infinity(), 7), 7);
}

TEST(SettingsRuntimeCache, NumericKeysClampBeforeIntConversion)
{
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(0.0f), 1);
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(900.9f), 900);
	EXPECT_EQ(SettingsRuntimeCachePositiveRoundedKey(125.4f), 125);
	EXPECT_EQ(SettingsRuntimeCacheRoundedKey(12.6f), 13);
	EXPECT_EQ(SettingsRuntimeCacheDimensionKey(std::numeric_limits<float>::max()), std::numeric_limits<int>::max());
	EXPECT_EQ(SettingsRuntimeCacheRoundedKey(-std::numeric_limits<float>::max()), std::numeric_limits<int>::min());
}

TEST(SettingsRuntimeCache, CompactVisibleTextIsRejected)
{
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientPetSection"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("Controls:Mouse"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText(nullptr));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientDeferredSummary"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientCompactSummary"));
	EXPECT_FALSE(SettingsRuntimeCacheAllowsVisibleCompactText("TClientSummaryBlock"));
}
