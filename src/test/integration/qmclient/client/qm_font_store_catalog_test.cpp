#include <game/client/components/qmclient/font_store_catalog.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory_resource>
#include <new>

namespace
{
	class CQmFontStoreCatalog : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(qm_font_download::EnsureDirectory(*m_pStorage));
		}
		void WriteFile(const char *pPath, const char *pText)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			io_write(File, pText, str_length(pText));
			io_close(File);
		}
		fontstore::SCatalog Load()
		{
			auto pJob = std::make_shared<fontstore::CCatalogLoadJob>(m_pStorage.get());
			fontstore::SCatalog Result;
			EXPECT_FALSE(pJob->TakeResult(Result));
			EXPECT_FALSE(Result.m_Loaded);
			CJobPool Pool;
			Pool.Init(1);
			Pool.Add(pJob);
			Pool.Shutdown();
			EXPECT_TRUE(pJob->TakeResult(Result));
			EXPECT_TRUE(Result.m_Loaded);
			EXPECT_FALSE(pJob->TakeResult(Result));
			return Result;
		}
	};
}

TEST_F(CQmFontStoreCatalog, AggregatesCaseInsensitiveFamiliesAndRequiresAllInstalledFiles)
{
	WriteFile("fonts/fonts_store/font_catalog.json", R"({"fonts":[
		{"n":"Alpha","f":"ofl/alpha/Alpha-Regular.ttf","c":"sans-serif"},
		{"n":"alpha","f":"ofl/alpha/Alpha-Bold.ttf","c":"sans-serif"},
		{"n":"Beta","f":"ofl/beta/Beta.ttf","c":"serif"}]})");
	WriteFile("fonts/fonts_store/Alpha-Regular.ttf", "font");
	WriteFile("fonts/fonts_store/Beta.ttf", "font");
	const auto Result = Load();
	ASSERT_EQ(Result.m_vEntries.size(), 3u);
	ASSERT_EQ(Result.m_vFamilies.size(), 2u);
	EXPECT_EQ(Result.m_vFamilies[0].m_Name, "Alpha");
	EXPECT_EQ(Result.m_vFamilies[0].m_vEntryIndices.size(), 2u);
	EXPECT_FALSE(Result.m_vFamilies[0].m_Installed);
	EXPECT_TRUE(Result.m_vFamilies[1].m_Installed);
}

TEST_F(CQmFontStoreCatalog, RejectsUnsafeEntriesWhileRetainingValidMetadata)
{
	WriteFile("fonts/fonts_store/font_catalog.json", R"({"fonts":[
		{"n":"Unsafe","f":"../escape.ttf"},
		{"n":"Invalid","f":"ofl/test/font.exe"},
		{"n":"Valid","f":"ofl/test/font.ttf","v":1,"s":["latin","cyrillic"],"u":"https://fonts.gstatic.cn/font.ttf"}]})");
	const auto Result = Load();
	ASSERT_EQ(Result.m_vEntries.size(), 1u);
	EXPECT_EQ(Result.m_vEntries[0].m_Family, "Valid");
	EXPECT_TRUE(Result.m_vEntries[0].m_Variable);
	EXPECT_EQ(Result.m_vEntries[0].m_vSubsets.size(), 2u);
	EXPECT_EQ(Result.m_vEntries[0].m_DirectUrl, "https://fonts.gstatic.cn/font.ttf");
}

TEST_F(CQmFontStoreCatalog, EmptyCatalogCompletesWithEmptyResult)
{
	WriteFile("fonts/fonts_store/font_catalog.json", R"({"fonts":[]})");
	const auto Result = Load();
	EXPECT_TRUE(Result.m_vEntries.empty());
	EXPECT_TRUE(Result.m_vFamilies.empty());
}

TEST_F(CQmFontStoreCatalog, MalformedCatalogCompletesWithEmptyResult)
{
	WriteFile("fonts/fonts_store/font_catalog.json", "{broken");
	const auto Result = Load();
	EXPECT_TRUE(Result.m_vEntries.empty());
	EXPECT_TRUE(Result.m_vFamilies.empty());
}

namespace
{
	class CFontCatalogMemory : public std::pmr::memory_resource
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

TEST_F(CQmFontStoreCatalog, EachOuterVectorAllocationFailurePreservesBothCatalogHalvesAndCanRetry)
{
	ASSERT_NO_FATAL_FAILURE(WriteFile("fonts/fonts_store/font_catalog.json", R"({"fonts":[
		{"n":"Alpha","f":"ofl/alpha/Alpha-Regular.ttf"},
		{"n":"alpha","f":"ofl/alpha/Alpha-Bold.ttf"},
		{"n":"Beta","f":"ofl/beta/Beta.ttf"}]})"));
	const auto Seed = [](auto &Entries, auto &Families) {
		Entries.emplace_back().m_Family = "Old";
		auto &Family = Families.emplace_back();
		Family.m_Name = "Old";
		Family.m_PreviewReady = true;
		Family.m_vEntryIndices.push_back(0);
	};
	CFontCatalogMemory BaselineMemory;
	std::pmr::vector<fontstore::SEntry> BaselineEntries(&BaselineMemory);
	std::pmr::vector<fontstore::SFamily> BaselineFamilies(&BaselineMemory);
	Seed(BaselineEntries, BaselineFamilies);
	const int Before = BaselineMemory.m_Allocations;
	fontstore::LoadCatalog(m_pStorage.get(), BaselineEntries, BaselineFamilies);
	const int Allocations = BaselineMemory.m_Allocations - Before;
	ASSERT_GT(Allocations, 0);
	for(int Allowed = 0; Allowed < Allocations; ++Allowed)
	{
		SCOPED_TRACE(Allowed);
		CFontCatalogMemory Memory;
		std::pmr::vector<fontstore::SEntry> Entries(&Memory);
		std::pmr::vector<fontstore::SFamily> Families(&Memory);
		Seed(Entries, Families);
		const auto *pEntries = Entries.data();
		const auto *pFamilies = Families.data();
		Memory.m_AllowedAllocations = Allowed;
		EXPECT_THROW(fontstore::LoadCatalog(m_pStorage.get(), Entries, Families), std::bad_alloc);
		EXPECT_EQ(Entries.data(), pEntries);
		EXPECT_EQ(Families.data(), pFamilies);
		ASSERT_EQ(Entries.size(), 1u);
		ASSERT_EQ(Families.size(), 1u);
		EXPECT_EQ(Entries[0].m_Family, "Old");
		EXPECT_TRUE(Families[0].m_PreviewReady);
		ASSERT_EQ(Families[0].m_vEntryIndices.size(), 1u);
		EXPECT_EQ(Families[0].m_vEntryIndices[0], 0u);
		Memory.m_AllowedAllocations = -1;
		fontstore::LoadCatalog(m_pStorage.get(), Entries, Families);
		ASSERT_EQ(Entries.size(), 4u);
		ASSERT_EQ(Families.size(), 3u);
		for(const auto &Family : Families)
			for(size_t Index : Family.m_vEntryIndices)
				EXPECT_LT(Index, Entries.size());
	}
}

TEST_F(CQmFontStoreCatalog, AppendingCatalogRetainsExistingStateAndUsesCurrentEntryOffsets)
{
	ASSERT_NO_FATAL_FAILURE(WriteFile("fonts/fonts_store/font_catalog.json", R"({"fonts":[
		{"n":"Alpha","f":"ofl/alpha/Alpha-Regular.ttf"},
		{"n":"alpha","f":"ofl/alpha/Alpha-Bold.ttf"}]})"));
	std::vector<fontstore::SEntry> Entries(1);
	Entries[0].m_Family = "Old";
	std::vector<fontstore::SFamily> Families(1);
	Families[0].m_Name = "Old";
	Families[0].m_PreviewReady = true;
	Families[0].m_vEntryIndices = {0};
	fontstore::LoadCatalog(m_pStorage.get(), Entries, Families);
	ASSERT_EQ(Entries.size(), 3u);
	ASSERT_EQ(Families.size(), 2u);
	EXPECT_EQ(Families[0].m_Name, "Old");
	EXPECT_TRUE(Families[0].m_PreviewReady);
	EXPECT_EQ(Families[1].m_vEntryIndices, (std::vector<size_t>{1, 2}));
	EXPECT_EQ(Entries[1].m_File, "Alpha-Regular.ttf");
	EXPECT_EQ(Entries[2].m_File, "Alpha-Bold.ttf");
}

TEST_F(CQmFontStoreCatalog, ObjectWithoutFontArrayKeepsTheExistingPair)
{
	ASSERT_NO_FATAL_FAILURE(WriteFile("fonts/fonts_store/font_catalog.json", R"({"fonts":[{"n":"Alpha","f":"ofl/alpha/Alpha.ttf"}]})"));
	std::vector<fontstore::SEntry> Entries;
	std::vector<fontstore::SFamily> Families;
	fontstore::LoadCatalog(m_pStorage.get(), Entries, Families);
	ASSERT_EQ(Entries.size(), 1u);
	ASSERT_EQ(Families.size(), 1u);
	// 保持首个存储根中的覆盖文件，避免TYPE_ALL回退到工作区随包目录。
	ASSERT_NO_FATAL_FAILURE(WriteFile("fonts/fonts_store/font_catalog.json", R"({"other":[]})"));
	fontstore::LoadCatalog(m_pStorage.get(), Entries, Families);
	ASSERT_EQ(Entries.size(), 1u);
	ASSERT_EQ(Families.size(), 1u);
	EXPECT_EQ(Entries[0].m_File, "Alpha.ttf");
	EXPECT_EQ(Families[0].m_vEntryIndices, (std::vector<size_t>{0}));
}

TEST_F(CQmFontStoreCatalog, MalformedReplacementDoesNotChangeExistingMetadata)
{
	std::vector<fontstore::SEntry> Entries(1);
	Entries[0].m_File = "Old.ttf";
	std::vector<fontstore::SFamily> Families(1);
	Families[0].m_Name = "Old";
	Families[0].m_vEntryIndices = {0};
	ASSERT_NO_FATAL_FAILURE(WriteFile("fonts/fonts_store/font_catalog.json", "{broken"));
	fontstore::LoadCatalog(m_pStorage.get(), Entries, Families);
	ASSERT_EQ(Entries.size(), 1u);
	ASSERT_EQ(Families.size(), 1u);
	EXPECT_EQ(Entries[0].m_File, "Old.ttf");
	EXPECT_EQ(Families[0].m_Name, "Old");
	EXPECT_EQ(Families[0].m_vEntryIndices, (std::vector<size_t>{0}));
}
