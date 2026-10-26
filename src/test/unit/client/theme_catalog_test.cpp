#include <game/client/components/qmclient/theme_catalog.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <vector>

TEST(ThemeCatalog, DefaultEntriesPublishAllThreeChoicesWithExistingIconPaths)
{
	std::vector<CTheme> Themes;
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	ASSERT_EQ(Themes.size(), 3u);
	EXPECT_EQ(Themes[0].m_Name, "");
	EXPECT_EQ(Themes[0].m_IconPath, "themes/none.png");
	EXPECT_EQ(Themes[1].m_Name, "auto");
	EXPECT_EQ(Themes[1].m_IconPath, "themes/auto.png");
	EXPECT_EQ(Themes[2].m_Name, "rand");
	EXPECT_EQ(Themes[2].m_IconPath, "themes/rand.png");
	for(const auto &Theme : Themes)
	{
		EXPECT_TRUE(Theme.m_HasDay);
		EXPECT_TRUE(Theme.m_HasNight);
		EXPECT_FALSE(Theme.m_IconLoadRequested);
		EXPECT_FALSE(Theme.m_IconLoadFailed);
	}
	EXPECT_TRUE(EnsureDefaultThemeEntries(Themes));
	EXPECT_EQ(Themes.size(), 3u);
}

TEST(ThemeCatalog, ExistingEntriesAndIconStateArePreserved)
{
	std::vector<CTheme> Themes;
	Themes.emplace_back("selected.png", false, true);
	Themes[0].m_IconPath = "selected-icon.png";
	Themes[0].m_IconLoadRequested = true;
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	ASSERT_EQ(Themes.size(), 1u);
	EXPECT_EQ(Themes[0].m_Name, "selected.png");
	EXPECT_EQ(Themes[0].m_IconPath, "selected-icon.png");
	EXPECT_TRUE(Themes[0].m_IconLoadRequested);
	EXPECT_FALSE(Themes[0].m_HasDay);
}

TEST(ThemeCatalog, AllocationFailureLeavesNoPartialDefaultsAndSameContainerCanRetry)
{
	std::array<std::byte, 4096> aBuffer;
	std::pmr::monotonic_buffer_resource Memory(aBuffer.data(), aBuffer.size(), std::pmr::null_memory_resource());
	(void)Memory.allocate(aBuffer.size(), 1);
	std::pmr::vector<CTheme> Themes(&Memory);
	EXPECT_FALSE(EnsureDefaultThemeEntries(Themes));
	EXPECT_TRUE(Themes.empty());
	// 失败前没有容器获得缓冲，恢复内存后仍通过同一生产入口提交完整列表。
	Memory.release();
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	ASSERT_EQ(Themes.size(), 3u);
	EXPECT_EQ(Themes[0].m_Name, "");
	EXPECT_EQ(Themes[1].m_Name, "auto");
	EXPECT_EQ(Themes[2].m_Name, "rand");
}

TEST(ThemeCatalog, MetadataOnlyCatalogCanClearWithoutGraphicsAndRebuildDefaults)
{
	std::vector<CTheme> Themes;
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	ASSERT_TRUE(ClearThemeCatalog(Themes, nullptr));
	EXPECT_TRUE(Themes.empty());
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	ASSERT_EQ(Themes.size(), 3u);
	EXPECT_EQ(Themes[0].m_IconPath, "themes/none.png");
}
