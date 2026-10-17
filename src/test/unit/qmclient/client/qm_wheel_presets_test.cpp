#include <game/client/components/qmclient/wheel_presets.h>

#include <gtest/gtest.h>

using namespace qm_wheel_presets;

TEST(QmWheelPresets, NamePresetChangesOnlyTheSelectedTeeName)
{
	SPreset Preset;
	Preset.m_aTees[1].m_aValues[NAME] = "分身";
	Preset.m_aTees[1].m_aValues[CLAN] = "unused";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const auto Restored = ParseCommand(Command);
	ASSERT_TRUE(Restored);
	EXPECT_EQ(Restored->m_Type, EType::NAME);
	EXPECT_EQ(Restored->m_aTees[1].m_aValues[NAME], "分身");
	EXPECT_FALSE(Restored->m_aTees[1].m_aValues[CLAN]);
	EXPECT_FALSE(Restored->m_aTees[0].m_aValues[NAME]);
}

TEST(QmWheelPresets, IdentityKeepsDistinctMainAndDummyValues)
{
	SPreset Preset;
	Preset.m_Type = EType::IDENTITY;
	Preset.m_aTees[0].m_aValues[NAME] = "DYL";
	Preset.m_aTees[1].m_aValues[NAME] = "YL";
	Preset.m_aTees[0].m_aValues[SKIN] = "default_v2";
	Preset.m_aTees[1].m_aValues[SKIN] = "default";
	Preset.m_aTees[0].m_aValues[CLAN] = "";
	Preset.m_aTees[0].m_aValues[CUSTOM_COLORS] = "0";
	Preset.m_aTees[1].m_aValues[CUSTOM_COLORS] = "1";
	Preset.m_aTees[1].m_aValues[BODY_COLOR] = "1048831";
	Preset.m_aTees[1].m_aValues[FEET_COLOR] = "2603775";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const auto Restored = ParseCommand(Command);
	ASSERT_TRUE(Restored);
	EXPECT_EQ(Restored->m_Type, EType::IDENTITY);
	EXPECT_EQ(Restored->m_aTees[0].m_aValues, Preset.m_aTees[0].m_aValues);
	EXPECT_EQ(Restored->m_aTees[1].m_aValues, Preset.m_aTees[1].m_aValues);
}

TEST(QmWheelPresets, PartialIdentityDoesNotTurnUnspecifiedFieldsIntoResets)
{
	const auto Preset = ParseCommand("player_skin \"default\"; player_clan \"\"");
	ASSERT_TRUE(Preset);
	EXPECT_EQ(Preset->m_Type, EType::IDENTITY);
	EXPECT_FALSE(Preset->m_aTees[0].m_aValues[NAME]);
	EXPECT_FALSE(Preset->m_aTees[0].m_aValues[CUSTOM_COLORS]);
	ASSERT_TRUE(Preset->m_aTees[0].m_aValues[CLAN]);
	EXPECT_TRUE(Preset->m_aTees[0].m_aValues[CLAN]->empty());
}

TEST(QmWheelPresets, ChatPreservesQuotesBackslashesAndSemicolons)
{
	SPreset Preset;
	Preset.m_Type = EType::CHAT;
	Preset.m_Chat = "hi \"tee\"; path\\name # text";
	Preset.m_TeamChat = true;
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const auto Restored = ParseCommand(Command);
	ASSERT_TRUE(Restored);
	EXPECT_EQ(Restored->m_Type, EType::CHAT);
	EXPECT_TRUE(Restored->m_TeamChat);
	EXPECT_EQ(Restored->m_Chat, Preset.m_Chat);
}

TEST(QmWheelPresets, AssetSnapshotKeepsAllSupportedCategories)
{
	SPreset Preset;
	Preset.m_Type = EType::ASSETS;
	for(int Index = 0; Index < NUM_ASSETS; ++Index)
		Preset.m_aAssets[Index] = "资源 " + std::to_string(Index);
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const auto Restored = ParseCommand(Command);
	ASSERT_TRUE(Restored);
	EXPECT_EQ(Restored->m_Type, EType::ASSETS);
	EXPECT_EQ(Restored->m_aAssets, Preset.m_aAssets);
}

TEST(QmWheelPresets, EmptyAssetFieldsKeepExistingSelections)
{
	SPreset Preset;
	Preset.m_Type = EType::ASSETS;
	Preset.m_aAssets[1] = "default";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const auto Restored = ParseCommand(Command);
	ASSERT_TRUE(Restored);
	EXPECT_TRUE(Restored->m_aAssets[0].empty());
	EXPECT_EQ(Restored->m_aAssets[1], "default");
	EXPECT_TRUE(Restored->m_aAssets[2].empty());
}

TEST(QmWheelPresets, InvalidValuesDoNotReplaceThePreviousCommand)
{
	SPreset Preset;
	Preset.m_Type = EType::IDENTITY;
	std::string Command = "unchanged";
	for(const auto &Value : {"-1", "2", "invalid"})
	{
		SCOPED_TRACE(Value);
		Preset.m_aTees[0].m_aValues[CUSTOM_COLORS] = Value;
		EXPECT_FALSE(BuildCommand(Preset, Command));
		EXPECT_EQ(Command, "unchanged");
	}
}

TEST(QmWheelPresets, RejectsControlCharactersAndInvalidUtf8)
{
	SPreset Preset;
	Preset.m_Type = EType::CHAT;
	std::string Command;
	for(const auto &Value : {std::string("a\nb"), std::string("a\rb"), std::string("a\0b", 3), std::string("\xff")})
	{
		Preset.m_Chat = Value;
		EXPECT_FALSE(BuildCommand(Preset, Command));
	}
}

TEST(QmWheelPresets, RejectsValuesThatWouldBeTruncatedByPlayerConfiguration)
{
	SPreset Preset;
	Preset.m_aTees[0].m_aValues[NAME] = std::string(15, 'a');
	std::string Command;
	EXPECT_TRUE(BuildCommand(Preset, Command));
	Preset.m_aTees[0].m_aValues[NAME] = std::string(16, 'a');
	EXPECT_FALSE(BuildCommand(Preset, Command));
}

TEST(QmWheelPresets, ChecksWheelCapacityAfterEscapingAllFields)
{
	SPreset Preset;
	Preset.m_Type = EType::ASSETS;
	Preset.m_aAssets.fill(std::string(49, 'a'));
	std::string Command;
	EXPECT_TRUE(BuildCommand(Preset, Command));
	Preset.m_aAssets.fill(std::string(49, '"'));
	EXPECT_FALSE(BuildCommand(Preset, Command));
}

TEST(QmWheelPresets, KeepsNegativeCountryCodesSupportedByPlayerConfiguration)
{
	SPreset Preset;
	Preset.m_Type = EType::IDENTITY;
	Preset.m_aTees[0].m_aValues[COUNTRY] = "-999";
	std::string Command;
	ASSERT_TRUE(BuildCommand(Preset, Command));
	const auto Restored = ParseCommand(Command);
	ASSERT_TRUE(Restored);
	EXPECT_EQ(Restored->m_aTees[0].m_aValues[COUNTRY], "-999");
	Preset.m_aTees[0].m_aValues[COUNTRY] = "-1000";
	EXPECT_FALSE(BuildCommand(Preset, Command));
	Preset.m_aTees[0].m_aValues[COUNTRY] = "1001";
	EXPECT_FALSE(BuildCommand(Preset, Command));
}

TEST(QmWheelPresets, CustomScriptsRemainOutsideThePresetEditor)
{
	for(const char *pCommand : {"exec DYL.cfg", "say \"hi\"; kill", "say \"hi\"; say \"again\"", "player_name \"one\"; player_name \"two\"", "player_skin \"default\"; cl_asset_game \"default\"", "player_name \"unterminated", "player_default_eyes 6", "player_color_body 16777216", "player_country -1000", "cl_asset_game \"\""})
	{
		SCOPED_TRACE(pCommand);
		EXPECT_FALSE(ParseCommand(pCommand));
	}
}

TEST(QmWheelPresets, EmptyPresetsCannotCreateNoOpWheelEntries)
{
	std::string Command;
	for(EType Type : {EType::NAME, EType::IDENTITY, EType::CHAT, EType::ASSETS})
	{
		SPreset Preset;
		Preset.m_Type = Type;
		EXPECT_FALSE(BuildCommand(Preset, Command));
	}
}
