#include <game/client/QmUi/SettingsFontSelection.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(SettingsFontSelectionAllocation, FailedFirstPublicationStaysEmptyAndRetriesTheSameInput)
{
	const std::vector<std::string> vFamilies{"A font family with a long owned name", "Another long font family name"};
	const char *pConfig = "Missing font with a long configuration";
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 64; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CSettingsFontSelection Selection;
		bool Updated = false, Escaped = false, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				Updated = Selection.Update(vFamilies, pConfig, "Follow the English font", "Default font label");
			}
			catch(...)
			{
				Escaped = true;
			}
			Triggered = Failure.Triggered();
		}
		ASSERT_FALSE(Escaped);
		if(!Triggered)
		{
			ReachedSuccess = Updated;
			break;
		}
		++Failures;
		EXPECT_FALSE(Updated);
		EXPECT_TRUE(Selection.Names().empty());
		EXPECT_TRUE(Selection.Families().empty());
		EXPECT_EQ(Selection.Selected(), -1);
		EXPECT_FALSE(Selection.IsFamilySelection(0));
		ASSERT_TRUE(Selection.Update(vFamilies, pConfig, "Follow the English font", "Default font label"));
		ASSERT_EQ(Selection.Names().size(), 4u);
		EXPECT_STREQ(Selection.Names()[Selection.Selected()], pConfig);
		EXPECT_EQ(Selection.Families(), vFamilies);
		EXPECT_FALSE(Selection.Update(vFamilies, pConfig, "Follow the English font", "Default font label"));
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(ReachedSuccess);
}

TEST(SettingsFontSelectionAllocation, FailedRefreshKeepsLabelsPointersAndFamilyIndicesUntilRetry)
{
	const std::vector<std::string> vOldFamilies{"Old family with a long owned name"};
	const std::vector<std::string> vNewFamilies{"New family with a long owned name", "Second new family with a long name", "Third new family with a long name"};
	const char *pOldConfig = "Missing old font with a long configuration";
	const char *pNewConfig = "Missing new font with a long configuration";
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 64; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CSettingsFontSelection Selection;
		ASSERT_TRUE(Selection.Update(vOldFamilies, pOldConfig, "Follow the old English font", "Old default font label"));
		const auto vpOldNames = Selection.Names();
		const auto *ppOldNames = Selection.Names().data();
		bool Updated = false, Escaped = false, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			try
			{
				Updated = Selection.Update(vNewFamilies, pNewConfig, nullptr, "New default font label");
			}
			catch(...)
			{
				Escaped = true;
			}
			Triggered = Failure.Triggered();
		}
		ASSERT_FALSE(Escaped);
		if(!Triggered)
		{
			ReachedSuccess = Updated;
			break;
		}
		++Failures;
		EXPECT_FALSE(Updated);
		EXPECT_EQ(Selection.Families(), vOldFamilies);
		ASSERT_EQ(Selection.Names(), vpOldNames);
		EXPECT_EQ(Selection.Names().data(), ppOldNames);
		EXPECT_STREQ(Selection.Names()[0], "Follow the old English font");
		EXPECT_STREQ(Selection.Names()[1], vOldFamilies[0].c_str());
		EXPECT_STREQ(Selection.Names()[2], pOldConfig);
		EXPECT_EQ(Selection.Selected(), 2);
		EXPECT_FALSE(Selection.IsFamilySelection(0));
		EXPECT_TRUE(Selection.IsFamilySelection(1));
		// 新目录更长也不能把旧缓存的缺失字体占位解释为真实字体索引。
		EXPECT_FALSE(Selection.IsFamilySelection(2));
		EXPECT_FALSE(Selection.IsFamilySelection(3));
		ASSERT_TRUE(Selection.Update(vNewFamilies, pNewConfig, nullptr, "New default font label"));
		EXPECT_EQ(Selection.Families(), vNewFamilies);
		ASSERT_EQ(Selection.Names().size(), 4u);
		EXPECT_EQ(Selection.Selected(), 3);
		EXPECT_TRUE(Selection.IsFamilySelection(0));
		EXPECT_TRUE(Selection.IsFamilySelection(2));
		EXPECT_FALSE(Selection.IsFamilySelection(3));
		EXPECT_STREQ(Selection.Names()[3], pNewConfig);
		EXPECT_FALSE(Selection.Update(vNewFamilies, pNewConfig, nullptr, "New default font label"));
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(ReachedSuccess);
}

TEST(SettingsFontSelectionAllocation, UnchangedInputDoesNotAllocateOrReplaceBorrowedNames)
{
	const std::vector<std::string> vFamilies{"Family with a long name to avoid small string storage"};
	CSettingsFontSelection Selection;
	ASSERT_TRUE(Selection.Update(vFamilies, vFamilies[0].c_str(), nullptr, "Default font label"));
	const auto vpNames = Selection.Names();
	const auto *ppNames = Selection.Names().data();
	bool Updated, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Updated = Selection.Update(vFamilies, vFamilies[0].c_str(), nullptr, "Default font label");
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_FALSE(Updated);
	EXPECT_EQ(Selection.Names(), vpNames);
	EXPECT_EQ(Selection.Names().data(), ppNames);
}
