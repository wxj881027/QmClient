#include <engine/storage.h>

#include <game/client/components/section_loader.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <limits>

TEST(SectionLoader, SessionCacheRoundTripsLastTabAndScroll)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	SSessionUiCache Saved;
	Saved.m_LastSettingsPage = 8;
	Saved.m_LastTClientTab = 0;
	Saved.m_LastQmTab = -1;
	Saved.m_LastScrollY = -240.0f;
	Saved.m_RuntimeKey.m_ViewportWidth = 1600;
	Saved.m_RuntimeKey.m_ViewportHeight = 900;
	Saved.m_RuntimeKey.m_UiScale = 100;
	Saved.m_RuntimeKey.m_ConfigHash = 11;
	Saved.m_RuntimeKey.m_LanguageHash = 22;
	Saved.m_RuntimeKey.m_FontHash = 33;
	Saved.m_RuntimeKey.m_BackendHash = 44;
	Saved.m_RuntimeKey.m_WindowHash = 55;
	Saved.m_Valid = true;

	CSectionLoader::SaveSessionCache(Saved, "qmclient/settings_section_cache_metadata.cfg", pStorage.get());

	SSessionUiCache Loaded;
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, "qmclient/settings_section_cache_metadata.cfg", pStorage.get()));
	EXPECT_EQ(Loaded.m_LastSettingsPage, 8);
	EXPECT_EQ(Loaded.m_LastTClientTab, 0);
	EXPECT_EQ(Loaded.m_LastQmTab, -1);
	EXPECT_FLOAT_EQ(Loaded.m_LastScrollY, -240.0f);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportWidth, 1600);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportHeight, 900);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_UiScale, 100);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ConfigHash, 11u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_LanguageHash, 22u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_FontHash, 33u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_BackendHash, 44u);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_WindowHash, 55u);
	EXPECT_TRUE(Loaded.m_Valid);
}

TEST(SectionLoader, SessionCacheLoadKeepsWorkingWhenRuntimeKeyFieldsAreMissing)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	const char *pPath = "settings_section_cache_metadata.cfg";
	const char *pLegacyContents =
		"settings_page=8\n"
		"tab_tclient=0\n"
		"tab_qm=-1\n"
		"scroll_y=-240.000000\n";

	char aSavePath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, pPath, aSavePath, sizeof(aSavePath));
	IOHANDLE File = io_open(aSavePath, IOFLAG_WRITE);
	ASSERT_TRUE(File != nullptr);
	io_write(File, pLegacyContents, str_length(pLegacyContents));
	io_close(File);

	SSessionUiCache Loaded;
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, pPath, pStorage.get()));
	EXPECT_EQ(Loaded.m_LastSettingsPage, 8);
	EXPECT_EQ(Loaded.m_LastTClientTab, 0);
	EXPECT_EQ(Loaded.m_LastQmTab, -1);
	EXPECT_FLOAT_EQ(Loaded.m_LastScrollY, -240.0f);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportWidth, 0);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ViewportHeight, 0);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_UiScale, 0);
	EXPECT_EQ(Loaded.m_RuntimeKey.m_ConfigHash, 0u);
	EXPECT_TRUE(Loaded.m_Valid);
}

TEST(SectionLoaderSessionCache, MaximumScalarWidthsRoundTripEveryRuntimeKeyField)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	SSessionUiCache Saved;
	Saved.m_LastSettingsPage = std::numeric_limits<int>::max();
	Saved.m_LastTClientTab = std::numeric_limits<int>::max();
	Saved.m_LastQmTab = std::numeric_limits<int>::max();
	Saved.m_RuntimeKey.m_ViewportWidth = std::numeric_limits<int>::max();
	Saved.m_RuntimeKey.m_ViewportHeight = std::numeric_limits<int>::max();
	Saved.m_RuntimeKey.m_UiScale = std::numeric_limits<int>::max();
	Saved.m_RuntimeKey.m_ConfigHash = std::numeric_limits<uint64_t>::max();
	Saved.m_RuntimeKey.m_LanguageHash = std::numeric_limits<uint64_t>::max() - 1;
	Saved.m_RuntimeKey.m_FontHash = std::numeric_limits<uint64_t>::max() - 2;
	Saved.m_RuntimeKey.m_BackendHash = std::numeric_limits<uint64_t>::max() - 3;
	Saved.m_RuntimeKey.m_WindowHash = std::numeric_limits<uint64_t>::max() - 4;
	Saved.m_Valid = true;
	for(float Scroll : {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()})
	{
		SCOPED_TRACE(Scroll);
		Saved.m_LastScrollY = Scroll;
		CSectionLoader::SaveSessionCache(Saved, "qmclient/full_width.cfg", pStorage.get());
		SSessionUiCache Loaded;
		ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, "qmclient/full_width.cfg", pStorage.get()));
		EXPECT_EQ(Loaded.m_LastSettingsPage, Saved.m_LastSettingsPage);
		EXPECT_EQ(Loaded.m_LastTClientTab, Saved.m_LastTClientTab);
		EXPECT_EQ(Loaded.m_LastQmTab, Saved.m_LastQmTab);
		EXPECT_FLOAT_EQ(Loaded.m_LastScrollY, Scroll);
		EXPECT_TRUE(Loaded.m_RuntimeKey == Saved.m_RuntimeKey);
		EXPECT_TRUE(Loaded.m_Valid);
	}
}

TEST(SectionLoaderSessionCache, ReloadingLongMetadataReplacesThePreviousCompleteRuntimeKey)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	SSessionUiCache Saved;
	Saved.m_LastSettingsPage = 8;
	Saved.m_LastScrollY = -240;
	Saved.m_RuntimeKey.m_ViewportWidth = 1920;
	Saved.m_RuntimeKey.m_ViewportHeight = 1080;
	Saved.m_RuntimeKey.m_UiScale = 125;
	Saved.m_RuntimeKey.m_ConfigHash = std::numeric_limits<uint64_t>::max();
	Saved.m_RuntimeKey.m_LanguageHash = std::numeric_limits<uint64_t>::max() - 1;
	Saved.m_RuntimeKey.m_FontHash = std::numeric_limits<uint64_t>::max() - 2;
	Saved.m_RuntimeKey.m_BackendHash = std::numeric_limits<uint64_t>::max() - 3;
	Saved.m_RuntimeKey.m_WindowHash = std::numeric_limits<uint64_t>::max() - 4;
	Saved.m_Valid = true;
	CSectionLoader::SaveSessionCache(Saved, "qmclient/reload.cfg", pStorage.get());
	SSessionUiCache Loaded;
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, "qmclient/reload.cfg", pStorage.get()));
	ASSERT_TRUE(Loaded.m_RuntimeKey == Saved.m_RuntimeKey);
	Saved.m_LastScrollY = -480;
	Saved.m_RuntimeKey.m_FontHash = std::numeric_limits<uint64_t>::max() - 5;
	Saved.m_RuntimeKey.m_BackendHash = std::numeric_limits<uint64_t>::max() - 6;
	Saved.m_RuntimeKey.m_WindowHash = std::numeric_limits<uint64_t>::max() - 7;
	CSectionLoader::SaveSessionCache(Saved, "qmclient/reload.cfg", pStorage.get());
	ASSERT_TRUE(CSectionLoader::LoadSessionCache(Loaded, "qmclient/reload.cfg", pStorage.get()));
	EXPECT_FLOAT_EQ(Loaded.m_LastScrollY, -480);
	EXPECT_TRUE(Loaded.m_RuntimeKey == Saved.m_RuntimeKey);
}
