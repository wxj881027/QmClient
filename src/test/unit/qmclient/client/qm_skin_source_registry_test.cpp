#include <engine/storage.h>

#include <game/client/components/qmclient/skin_source_registry.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>

TEST(QmSkinSourceRegistry, PersistsSourcesWithCaseInsensitiveIdentity)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("downloadedskins", IStorage::TYPE_SAVE));

	CSkinSourceRegistry Registry;
	EXPECT_FALSE(Registry.Find("Alpha").has_value());
	EXPECT_TRUE(Registry.Set("Alpha", ESkinSource::DDSTATS));
	EXPECT_FALSE(Registry.Set("alpha", ESkinSource::DDSTATS));
	ASSERT_TRUE(Registry.Find("ALPHA").has_value());
	EXPECT_EQ(Registry.Find("ALPHA").value(), ESkinSource::DDSTATS);

	ASSERT_TRUE(Registry.Save(pStorage.get()));
	EXPECT_TRUE(pStorage->FileExists(CSkinSourceRegistry::FILE, IStorage::TYPE_SAVE));

	CSkinSourceRegistry Loaded;
	Loaded.Load(pStorage.get());
	ASSERT_TRUE(Loaded.Find("alpha").has_value());
	EXPECT_EQ(Loaded.Find("alpha").value(), ESkinSource::DDSTATS);

	EXPECT_TRUE(Loaded.Set("Alpha", ESkinSource::COMMUNITY));
	ASSERT_TRUE(Loaded.Save(pStorage.get()));

	CSkinSourceRegistry Reloaded;
	Reloaded.Load(pStorage.get());
	ASSERT_TRUE(Reloaded.Find("ALPHA").has_value());
	EXPECT_EQ(Reloaded.Find("ALPHA").value(), ESkinSource::COMMUNITY);
}

TEST(QmSkinSourceRegistry, IgnoresUnknownAndLocalSources)
{
	CSkinSourceRegistry Registry;
	EXPECT_FALSE(Registry.Set("Alpha", ESkinSource::UNKNOWN));
	EXPECT_FALSE(Registry.Set("Alpha", ESkinSource::LOCAL));
	EXPECT_TRUE(Registry.Empty());
}
