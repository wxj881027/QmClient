#include <game/client/QmUi/SettingsFontSelection.h>

#include <gtest/gtest.h>

TEST(SettingsFontSelection, RecognizesFamilyAndBookStyleWithLegacySeparators)
{
	const std::vector<std::string> vFamilies{"DejaVu Sans", "Source Han Sans", "Source Han Sans SC"};
	EXPECT_EQ(QmFontFamilySelection("DejaVu Sans", vFamilies), 0);
	EXPECT_EQ(QmFontFamilySelection("DejaVuSans Book", vFamilies), 0);
	EXPECT_EQ(QmFontFamilySelection("dejavu-sans-book", vFamilies), 0);
	EXPECT_EQ(QmFontFamilySelection("Source Han Sans SC Regular", vFamilies), 2);
	EXPECT_EQ(QmFontFamilySelection("Source Han Sans SC", vFamilies), 2);
	EXPECT_EQ(QmFontFamilySelection("DejaVu SansExtra", vFamilies), -1);
	EXPECT_EQ(QmFontFamilySelection("", vFamilies), -1);
	EXPECT_FALSE(QmFontFamilyMatchesConfig(nullptr, "DejaVu Sans"));
	EXPECT_FALSE(QmFontFamilyMatchesConfig("", " - "));
	EXPECT_TRUE(QmFontFamilyMatchesConfig("中文字体 Book", "中文字体"));
}

TEST(SettingsFontSelection, MissingConfiguredFamilyRemainsVisibleAndRestoresAfterInstall)
{
	CSettingsFontSelection Selection;
	const std::vector<std::string> vInitial{"DejaVu Sans"};
	ASSERT_TRUE(Selection.Update(vInitial, "My Font Book", nullptr, "Default"));
	ASSERT_EQ(Selection.Names().size(), 2u);
	EXPECT_STREQ(Selection.Names()[Selection.Selected()], "My Font Book");
	EXPECT_FALSE(Selection.IsFamilySelection(Selection.Selected()));
	EXPECT_FALSE(Selection.Update(vInitial, "My Font Book", nullptr, "Default"));
	ASSERT_TRUE(Selection.Update({"DejaVu Sans", "My Font"}, "My Font Book", nullptr, "Default"));
	EXPECT_EQ(Selection.Selected(), 1);
	EXPECT_STREQ(Selection.Names()[Selection.Selected()], "My Font");
	EXPECT_TRUE(Selection.IsFamilySelection(Selection.Selected()));
}

TEST(SettingsFontSelection, ConfigChangesRefreshSelectionWithoutFontDirectoryChanges)
{
	CSettingsFontSelection Selection;
	const std::vector<std::string> vFamilies{"DejaVu Sans", "Source Han Sans SC"};
	Selection.Update(vFamilies, "DejaVu Sans", nullptr, "Default");
	ASSERT_TRUE(Selection.Update(vFamilies, "Source Han Sans SC Regular", nullptr, "Default"));
	EXPECT_EQ(Selection.Selected(), 1);
	ASSERT_TRUE(Selection.Update(vFamilies, "Missing Font", nullptr, "Default"));
	EXPECT_STREQ(Selection.Names()[Selection.Selected()], "Missing Font");
	ASSERT_TRUE(Selection.Update(vFamilies, "DejaVu Sans", nullptr, "Default"));
	EXPECT_EQ(Selection.Names().size(), 2u);
	EXPECT_EQ(Selection.Selected(), 0);
}

TEST(SettingsFontSelection, FollowPrefixRemainsDistinctFromMissingConfiguration)
{
	CSettingsFontSelection Selection;
	const std::vector<std::string> vFamilies{"Source Han Sans SC"};
	Selection.Update(vFamilies, "", "Follow English", "Default");
	EXPECT_EQ(Selection.Selected(), 0);
	EXPECT_FALSE(Selection.IsFamilySelection(0));
	EXPECT_TRUE(Selection.IsFamilySelection(1));
	ASSERT_TRUE(Selection.Update(vFamilies, "Missing Font", "Follow English", "Default"));
	EXPECT_EQ(Selection.Selected(), 2);
	EXPECT_FALSE(Selection.IsFamilySelection(2));
	EXPECT_STREQ(Selection.Names()[2], "Missing Font");
	ASSERT_TRUE(Selection.Update(vFamilies, "Source Han Sans SC", "跟随英文字体", "默认"));
	EXPECT_EQ(Selection.Selected(), 1);
	EXPECT_STREQ(Selection.Names()[0], "跟随英文字体");
}

TEST(SettingsFontSelection, EmptyConfigurationWithoutFamiliesDisplaysDefaultLabel)
{
	CSettingsFontSelection Selection;
	Selection.Update({}, "", nullptr, "Default");
	ASSERT_EQ(Selection.Names().size(), 1u);
	EXPECT_EQ(Selection.Selected(), 0);
	EXPECT_STREQ(Selection.Names()[0], "Default");
	EXPECT_FALSE(Selection.IsFamilySelection(0));
	EXPECT_FALSE(Selection.Update({}, "", nullptr, "Default"));
	ASSERT_TRUE(Selection.Update({}, "", nullptr, "默认"));
	EXPECT_STREQ(Selection.Names()[0], "默认");
}
