#include <game/client/components/qmclient/font_store_catalog.h>

#include <gtest/gtest.h>
#include <test/test.h>

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
