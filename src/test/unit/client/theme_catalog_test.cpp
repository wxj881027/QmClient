#include <game/client/components/qmclient/theme_catalog.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <new>
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

namespace
{
	class CThemeCatalogMemory : public std::pmr::memory_resource
	{
		void *do_allocate(size_t Bytes, size_t Alignment) override
		{
			if(m_AllowedAllocations == 0)
				throw std::bad_alloc();
			if(m_AllowedAllocations > 0)
				--m_AllowedAllocations;
			void *pData = std::pmr::new_delete_resource()->allocate(Bytes, Alignment);
			++m_Allocations;
			return pData;
		}
		void do_deallocate(void *pData, size_t Bytes, size_t Alignment) override
		{
			std::pmr::new_delete_resource()->deallocate(pData, Bytes, Alignment);
		}
		bool do_is_equal(const memory_resource &Other) const noexcept override { return this == &Other; }

	public:
		int m_AllowedAllocations = -1;
		int m_Allocations = 0;
	};
}

TEST(ThemeCatalog, ScanEntriesPublishTogetherAfterReservedChoicesAndSortOnlyTheTail)
{
	std::vector<CTheme> Themes;
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	Themes.emplace_back("z.map", false, true);
	Themes.back().m_IconLoadRequested = true;
	Themes.back().m_IconPath = "keep.png";
	const std::vector<SThemeFileEntry> Entries{
		{"b.map", true, false, "b.png"},
		{"a.map", false, true, "a.png"},
		{"b.map", false, true, "ignored.png"},
		{"z.map", true, false, "ignored.png"}};
	ASSERT_TRUE(PublishThemeEntries(Themes, 3, [&] { return Entries; }));
	ASSERT_EQ(Themes.size(), 6u);
	EXPECT_EQ(Themes[0].m_Name, "");
	EXPECT_EQ(Themes[1].m_Name, "auto");
	EXPECT_EQ(Themes[2].m_Name, "rand");
	EXPECT_EQ(Themes[3].m_Name, "a.map");
	EXPECT_EQ(Themes[3].m_IconPath, "a.png");
	EXPECT_FALSE(Themes[3].m_HasDay);
	EXPECT_EQ(Themes[4].m_Name, "b.map");
	EXPECT_EQ(Themes[4].m_IconPath, "b.png");
	EXPECT_FALSE(Themes[4].m_HasNight);
	EXPECT_EQ(Themes[5].m_Name, "z.map");
	EXPECT_EQ(Themes[5].m_IconPath, "keep.png");
	EXPECT_TRUE(Themes[5].m_IconLoadRequested);
	EXPECT_FALSE(Themes[5].m_HasDay);
}

TEST(ThemeCatalog, SnapshotAllocationFailureRetainsCatalogAndCanPublishTheSameResultLater)
{
	std::vector<CTheme> Themes;
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	const auto *pOriginal = Themes.data();
	EXPECT_FALSE(PublishThemeEntries(Themes, 3, []() -> std::vector<SThemeFileEntry> { throw std::bad_alloc(); }));
	EXPECT_EQ(Themes.data(), pOriginal);
	ASSERT_EQ(Themes.size(), 3u);
	EXPECT_EQ(Themes[2].m_Name, "rand");
	const std::vector<SThemeFileEntry> Entries{{"new.map", true, true, "new.png"}};
	ASSERT_TRUE(PublishThemeEntries(Themes, 3, [&] { return Entries; }));
	ASSERT_EQ(Themes.size(), 4u);
	EXPECT_EQ(Themes.back().m_Name, "new.map");
}

TEST(ThemeCatalog, EachCandidateVectorAllocationFailurePreservesOriginalStateForRetry)
{
	std::vector<SThemeFileEntry> Entries;
	for(int i = 0; i < 40; ++i)
		Entries.push_back({"theme-" + std::to_string(i) + ".map", false, true, "icon.png"});
	CThemeCatalogMemory BaselineMemory;
	std::pmr::vector<CTheme> Baseline(&BaselineMemory);
	ASSERT_TRUE(EnsureDefaultThemeEntries(Baseline));
	const int Before = BaselineMemory.m_Allocations;
	ASSERT_TRUE(PublishThemeEntries(Baseline, 3, [&] { return Entries; }));
	const int Allocations = BaselineMemory.m_Allocations - Before;
	ASSERT_GT(Allocations, 0);
	// 按生产调用观察到的容器分配逐点注入，不依赖特定vector扩容倍率。
	for(int Allowed = 0; Allowed < Allocations; ++Allowed)
	{
		SCOPED_TRACE(Allowed);
		CThemeCatalogMemory Memory;
		std::pmr::vector<CTheme> Themes(&Memory);
		ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
		Themes[1].m_IconLoadFailed = true;
		const auto *pOriginal = Themes.data();
		Memory.m_AllowedAllocations = Allowed;
		EXPECT_FALSE(PublishThemeEntries(Themes, 3, [&] { return Entries; }));
		EXPECT_EQ(Themes.data(), pOriginal);
		ASSERT_EQ(Themes.size(), 3u);
		EXPECT_TRUE(Themes[1].m_IconLoadFailed);
		Memory.m_AllowedAllocations = -1;
		ASSERT_TRUE(PublishThemeEntries(Themes, 3, [&] { return Entries; }));
		EXPECT_EQ(Themes.get_allocator().resource(), &Memory);
		ASSERT_EQ(Themes.size(), 43u);
		EXPECT_TRUE(Themes[1].m_IconLoadFailed);
		EXPECT_FALSE(Themes[3].m_HasDay);
	}
}

TEST(ThemeCatalog, RepeatedPublicationKeepsExistingMetadataAndDoesNotDuplicateNames)
{
	std::vector<CTheme> Themes;
	ASSERT_TRUE(EnsureDefaultThemeEntries(Themes));
	const std::vector<SThemeFileEntry> Entries{{"selected.png", false, true, "icon.png"}};
	ASSERT_TRUE(PublishThemeEntries(Themes, 3, [&] { return Entries; }));
	Themes.back().m_IconLoadFailed = true;
	Themes.back().m_IconPath = "retained.png";
	ASSERT_TRUE(PublishThemeEntries(Themes, 3, [&] { return Entries; }));
	ASSERT_EQ(Themes.size(), 4u);
	EXPECT_TRUE(Themes.back().m_IconLoadFailed);
	EXPECT_EQ(Themes.back().m_IconPath, "retained.png");
	const std::vector<SThemeFileEntry> Empty;
	ASSERT_TRUE(PublishThemeEntries(Themes, 3, [&] { return Empty; }));
	EXPECT_EQ(Themes.size(), 4u);
}
