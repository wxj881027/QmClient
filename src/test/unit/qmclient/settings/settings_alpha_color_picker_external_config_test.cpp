#include <game/client/QmUi/SettingsColorPicker.h>

#include <gtest/gtest.h>

TEST(SettingsAlphaColorPicker, OpenIdlePopupKeepsExternalConfigChanges)
{
	unsigned int Color = 0x526C81;
	int Opacity = 37;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);
	State.RefreshFromConfig(Color, Opacity, true);

	constexpr unsigned int ExternalColor = 0x94C047;
	constexpr int ExternalOpacity = 81;
	Color = ExternalColor;
	Opacity = ExternalOpacity;
	State.RefreshFromConfig(Color, Opacity, true);

	EXPECT_FALSE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, ExternalColor);
	EXPECT_EQ(Opacity, ExternalOpacity);
}
