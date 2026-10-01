#include <game/client/components/settings_resource_jobs.h>

#include <gtest/gtest.h>

TEST(SettingsAssetAdmission, InactiveWindowBlocksAllAssetStarts)
{
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, false));
	EXPECT_TRUE(SettingsAssetWorkAllowedWhileWindowInactive(true, true));
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, true));
	EXPECT_FALSE(SettingsAssetWorkAllowedWhileWindowInactive(false, false));
}

TEST(SettingsAssetAdmission, VisibleResourceStartsCanUsePriorityBudget)
{
	EXPECT_TRUE(SettingsResourceCanUseHighPriorityBudget(5, 6, 12, false));
	EXPECT_FALSE(SettingsResourceCanUseHighPriorityBudget(6, 6, 12, false));
	EXPECT_TRUE(SettingsResourceCanUseHighPriorityBudget(6, 6, 12, true));
	EXPECT_FALSE(SettingsResourceCanUseHighPriorityBudget(12, 6, 12, true));
}

TEST(SettingsAssetAdmission, UploadByteBudgetRejectsOversizedFirstUpload)
{
	EXPECT_FALSE(SettingsResourceUploadWithinByteBudget(0, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_TRUE(SettingsResourceUploadWithinByteBudget(0, 0, 512 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceUploadWithinByteBudget(1, 512 * 1024, 768 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsAssetAdmission, OversizedUploadsStayDeferredAcrossFrameStates)
{
	SSettingsResourceFrameContext IdleVisible{};
	SSettingsResourceFrameContext ScrollActive = IdleVisible;
	ScrollActive.m_ScrollActive = true;
	SSettingsResourceFrameContext Recovery = IdleVisible;
	Recovery.m_PostScrollRecoveryFrames = 2;

	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, false, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, true, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(ScrollActive, false, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Recovery, false, ESettingsResourcePriority::VISIBLE, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(IdleVisible, false, ESettingsResourcePriority::BACKGROUND, 0, 4 * 1024 * 1024, 1 * 1024 * 1024));
}

TEST(SettingsAssetAdmission, StableFramesDoNotGrantOversizedUploadException)
{
	SSettingsResourceFrameContext Stable{};
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Stable, false, ESettingsResourcePriority::VISIBLE, 0, 2 * 1024 * 1024, 1 * 1024 * 1024));
	EXPECT_FALSE(SettingsResourceOversizedUploadAllowed(Stable, false, ESettingsResourcePriority::VISIBLE, 1, 2 * 1024 * 1024, 1 * 1024 * 1024));
}
