#include <game/client/components/assets_resource_registry.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

TEST(AssetsResourceMigration, DefaultNeverNeedsLegacyImport)
{
	EXPECT_FALSE(AssetResourceNeedsLegacyImport("default"));
}

TEST(AssetsResourceMigration, FirstImportedLegacyUsesStableName)
{
	std::array<std::string, 1> aNames{"default"};
	EXPECT_EQ(NextLegacyAssetName(aNames), "legacy");
}

TEST(AssetsResourceMigration, NameCollisionUsesIncrementingSuffix)
{
	std::array<std::string, 4> aNames{"default", "legacy", "legacy_2", "custom"};
	EXPECT_EQ(NextLegacyAssetName(aNames), "legacy_3");
}
