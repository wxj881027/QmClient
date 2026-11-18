#include <game/editor/editor_server_settings.h>

#include <gtest/gtest.h>

TEST(EditorServerSettingsValidation, IncompleteQuotedArgumentStopsAtTerminator)
{
	const char aInput[] = "\"still typing";
	const char *pIterator = aInput;
	int Length = 0;

	EXPECT_FALSE(EditorServerSettings::ReadArgument(pIterator, Length));
	EXPECT_EQ(pIterator, aInput + sizeof(aInput) - 1);
	EXPECT_EQ(Length, (int)sizeof(aInput) - 1);
}

TEST(EditorServerSettingsValidation, EscapedQuoteKeepsSpacesInArgument)
{
	const char aInput[] = R"("keep \"quoted\" text" next)";
	const char *pIterator = aInput;
	int Length = 0;

	ASSERT_TRUE(EditorServerSettings::ReadArgument(pIterator, Length));
	EXPECT_EQ(pIterator, aInput + Length);
	EXPECT_STREQ(pIterator, " next");
}

TEST(EditorServerSettingsValidation, CommentStartsAfterQuoteFollowingEscapedBackslash)
{
	const char aInput[] = R"(tune_zone_enter 1 "path#name\\" # comment)";
	const int Comment = EditorServerSettings::FindComment(aInput);

	ASSERT_GE(Comment, 0);
	EXPECT_STREQ(aInput + Comment, "# comment");
}

TEST(EditorServerSettingsValidation, DifferentTuningsInSameZoneDoNotCollide)
{
	const std::vector<SCurrentSettingArg> vArgs{{"1"}, {"ground_friction"}};
	const std::vector<CEditorMapSetting> vSettings{"tune_zone 1 ground_accel 1"};

	EXPECT_EQ(EditorServerSettings::FindCommandCollision("tune_zone", vArgs, 1, vSettings), -1);
}

TEST(EditorServerSettingsValidation, IncompleteRowsDoNotHideMatchingTuning)
{
	const std::vector<SCurrentSettingArg> vArgs{{"1"}, {"ground_friction"}};
	const std::vector<CEditorMapSetting> vSettings{"tune_zone", "tune_zone 1", "TUNE_ZONE 1 ground_friction 0.5"};

	EXPECT_EQ(EditorServerSettings::FindCommandCollision("tune_zone", vArgs, 1, vSettings), 2);
}

TEST(EditorServerSettingsValidation, DuplicateConfirmationKeepsOriginalRowByDefault)
{
	std::vector<CEditorMapSetting> vSettings{"sv_deepfly 1", "# separator", "sv_deepfly 1"};
	const std::vector<SInvalidSetting> vInvalid{SInvalidSetting(2, "sv_deepfly 1", SInvalidSetting::TYPE_DUPLICATE, 0, false)};

	EditorServerSettings::ApplyLoadedSettingFixes(vSettings, vInvalid);

	ASSERT_EQ(vSettings.size(), 2u);
	EXPECT_STREQ(vSettings[0].m_aCommand, "sv_deepfly 1");
	EXPECT_STREQ(vSettings[1].m_aCommand, "# separator");
}

TEST(EditorServerSettingsValidation, ChosenDuplicateKeepsItsOriginalPosition)
{
	std::vector<CEditorMapSetting> vSettings{"sv_deepfly 1", "# separator", "sv_deepfly 1"};
	std::vector<SInvalidSetting> vInvalid{SInvalidSetting(2, "sv_deepfly 1", SInvalidSetting::TYPE_DUPLICATE, 0, false)};
	vInvalid.front().m_Context.m_Chosen = true;

	EditorServerSettings::ApplyLoadedSettingFixes(vSettings, vInvalid);

	ASSERT_EQ(vSettings.size(), 2u);
	EXPECT_STREQ(vSettings[0].m_aCommand, "# separator");
	EXPECT_STREQ(vSettings[1].m_aCommand, "sv_deepfly 1");
}

TEST(EditorServerSettingsValidation, DeletingOneRowKeepsIdenticalFixedRow)
{
	std::vector<CEditorMapSetting> vSettings{"custom 1", "# separator", "custom 1"};
	std::vector<SInvalidSetting> vInvalid{
		SInvalidSetting(0, "custom 1", SInvalidSetting::TYPE_INVALID, -1, true),
		SInvalidSetting(2, "custom 1", SInvalidSetting::TYPE_INVALID, -1, true),
	};
	vInvalid[0].m_Context.m_Deleted = true;
	vInvalid[1].m_Context.m_Fixed = true;

	EditorServerSettings::ApplyLoadedSettingFixes(vSettings, vInvalid);

	ASSERT_EQ(vSettings.size(), 2u);
	EXPECT_STREQ(vSettings[0].m_aCommand, "# separator");
	EXPECT_STREQ(vSettings[1].m_aCommand, "custom 1");
}
