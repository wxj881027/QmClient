#include <game/client/map_settings.h>

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

TEST(MapSettings, VisitsCompleteCommandsInOrderIncludingEmptyCommands)
{
	const char aSettings[] = "first\0\0second";
	std::vector<std::string_view> vCommands;
	ForEachMapSetting(aSettings, sizeof(aSettings), [&](const char *pCommand) {
		vCommands.emplace_back(pCommand);
	});
	EXPECT_EQ(vCommands, (std::vector<std::string_view>{"first", "", "second"}));
}

TEST(MapSettings, NoCommandsForMissingOrEmptyData)
{
	int Commands = 0;
	const auto Count = [&](const char *) { ++Commands; };
	ForEachMapSetting(nullptr, 5, Count);
	ForEachMapSetting("", 0, Count);
	EXPECT_EQ(Commands, 0);
}

TEST(MapSettings, IgnoresUnterminatedTailAfterCompleteCommands)
{
	const char aSettings[] = "complete\0partial";
	std::vector<std::string_view> vCommands;
	// 字面量末尾的 NUL 在本次可读范围之外。
	ForEachMapSetting(aSettings, sizeof(aSettings) - 1, [&](const char *pCommand) {
		vCommands.emplace_back(pCommand);
	});
	EXPECT_EQ(vCommands, (std::vector<std::string_view>{"complete"}));
}
