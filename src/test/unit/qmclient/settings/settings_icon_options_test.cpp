#include <game/client/QmUi/SettingsIconOptions.h>

#include <gtest/gtest.h>

TEST(SettingsIconOptions, LegacyCustomColorRemainsEnabledWhilePresetChanges)
{
	int Preset = 3;
	int Enabled = 0;
	EXPECT_TRUE(qm_icon_settings::CustomColorEnabled(Preset, Enabled));
	qm_icon_settings::SelectPreset(2, Preset, Enabled);
	EXPECT_EQ(Preset, 4);
	EXPECT_EQ(Enabled, 1);
	qm_icon_settings::ToggleCustomColor(Preset, Enabled);
	EXPECT_EQ(Preset, 4);
	EXPECT_FALSE(qm_icon_settings::CustomColorEnabled(Preset, Enabled));
	qm_icon_settings::ToggleCustomColor(Preset, Enabled);
	EXPECT_TRUE(qm_icon_settings::CustomColorEnabled(Preset, Enabled));
}

TEST(SettingsIconOptions, DisablingLegacyCustomColorRestoresWhiteAndCanReenable)
{
	int Preset = 3;
	int Enabled = 0;
	qm_icon_settings::ToggleCustomColor(Preset, Enabled);
	EXPECT_EQ(Preset, 1);
	EXPECT_EQ(Enabled, 0);
	qm_icon_settings::ToggleCustomColor(Preset, Enabled);
	EXPECT_EQ(Enabled, 1);
}

TEST(SettingsIconOptions, PresetsRemainSelectedAcrossCustomColorToggle)
{
	for(int Index = 0; Index < 3; ++Index)
	{
		SCOPED_TRACE(Index);
		int Preset = 1;
		int Enabled = 0;
		qm_icon_settings::SelectPreset(Index, Preset, Enabled);
		EXPECT_EQ(qm_icon_settings::PresetIndex(Preset), Index);
		EXPECT_FALSE(qm_icon_settings::CustomColorEnabled(Preset, Enabled));
		qm_icon_settings::ToggleCustomColor(Preset, Enabled);
		qm_icon_settings::SelectPreset(Index, Preset, Enabled);
		EXPECT_TRUE(qm_icon_settings::CustomColorEnabled(Preset, Enabled));
		qm_icon_settings::ToggleCustomColor(Preset, Enabled);
		EXPECT_EQ(qm_icon_settings::PresetIndex(Preset), Index);
	}
}

TEST(SettingsIconOptions, OptionalPickerHeightMatchesActualRowFlowAtEveryScale)
{
	for(const float Scale : {0.5f, 1.0f, 2.0f})
	{
		SCOPED_TRACE(Scale);
		SSettingsContentMetrics Metrics;
		Metrics.m_LineHeight = 20.0f * Scale;
		Metrics.m_ButtonHeight = 28.0f * Scale;
		Metrics.m_LineSpacing = 4.0f * Scale;
		Metrics.m_UiScale = Scale;
		for(const float ContentWidth : {140.0f * Scale, 1000.0f * Scale})
		{
			SCOPED_TRACE(ContentWidth);
			for(const bool CustomEnabled : {false, true})
			{
				CUIRect Content{0.0f, 0.0f, ContentWidth, 1000.0f};
				const float Width = Content.w;
				CSettingsContentRowFlow Rows(Content, Metrics);
				Rows.Next(ResolveSettingsRadioRowLayout(Content, 3, Metrics).m_Height);
				Rows.NextLine();
				if(CustomEnabled)
					Rows.NextButton();
				Rows.NextButton();
				Rows.NextButton();
				Rows.Next(ResolveSettingsRadioRowLayout(Content, 4, Metrics).m_Height);
				EXPECT_FLOAT_EQ(qm_icon_settings::ContentHeight(Metrics, CustomEnabled, Width), 1000.0f - Content.h);
			}
		}
	}
}
