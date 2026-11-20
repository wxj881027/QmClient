#include <game/client/components/binds_deepfly_mode.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(BindCommandNormalization, LeavesRoomForTerminatorWhenSeparatorAndCharacterDoNotFit)
{
	char aNormalized[] = ".....";
	NormalizeBindCommand("ab c", aNormalized, 4);
	EXPECT_STREQ(aNormalized, "ab");
	EXPECT_EQ(aNormalized[4], '.');

	NormalizeBindCommand("ab c", aNormalized, 5);
	EXPECT_STREQ(aNormalized, "ab c");
}

TEST(DeepflyBindMode, EndsAtAnUnquotedComment)
{
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("echo \"DF #1; ready\"; +fire # note; +toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_NORMAL);
}

TEST(DeepflyBindMode, AcceptsTheConsoleMultiCommandPrefix)
{
	EXPECT_EQ(DetectDeepflyModeFromBindCommand("mc;+fire;+toggle cl_dummy_hammer 1 0"), DEEPFLY_MODE_DF);
}

TEST(TopLevelBindCommand, KeepsLongArgumentsAndContinuesAfterSeparator)
{
	const std::string LongCommand = "+toggle cl_dummy_hammer " + std::string(2048, '0') + "1 0";
	const std::string Binding = LongCommand + ";+fire";
	std::vector<std::string> vCommands;
	ForEachTopLevelBindCommand(Binding.c_str(), [&](const char *pCommand) {
		vCommands.emplace_back(pCommand);
	});
	EXPECT_EQ(vCommands, (std::vector<std::string>{LongCommand, "+fire"}));
}
