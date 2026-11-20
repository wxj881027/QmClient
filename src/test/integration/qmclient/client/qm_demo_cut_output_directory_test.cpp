#include <game/client/components/menus.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>

TEST(QmDemoCutOutputDirectory, UsesConfiguredDemoDirectoryWhileBrowsingScreenshots)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	CConfig Config{};
	str_copy(Config.m_QmDemoDirectory, "clips");

	const auto ExpectedPath = (std::filesystem::absolute(std::filesystem::u8path(Info.StoragePath())) / "clips").generic_u8string();
	const std::string Expected(reinterpret_cast<const char *>(ExpectedPath.data()), ExpectedPath.size());

	EXPECT_EQ(qmclient::demo_browser::DemoCutOutputDirectory(*pStorage, Config, "screenshots", true), Expected);
}

TEST(QmDemoCutOutputDirectory, KeepsCurrentDemoSubdirectory)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const CConfig Config{};

	EXPECT_EQ(qmclient::demo_browser::DemoCutOutputDirectory(*pStorage, Config, "demos/runs", false), "demos/runs");
}

TEST(QmDemoCutOutputDirectory, UsesDemoRootAtStorageSelection)
{
	CTestInfo Info;
	const auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const CConfig Config{};

	EXPECT_EQ(qmclient::demo_browser::DemoCutOutputDirectory(*pStorage, Config, "", false), "demos");
}
