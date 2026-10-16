#include <game/client/QmUi/SettingsColorPicker.h>

#include <gtest/gtest.h>

TEST(SettingsAlphaColorPicker, RoundTripUpdatesColorAndOpacityTogether)
{
	const unsigned int SourceColor = ColorHSLA(0.31f, 0.72f, 0.44f, 1.0f).Pack(false);
	const unsigned int Packed = PackSettingsAlphaColor(SourceColor, 37);
	unsigned int UpdatedColor = 0;
	int UpdatedOpacity = 0;
	UnpackSettingsAlphaColor(Packed, UpdatedColor, UpdatedOpacity);

	const ColorHSLA Expected(SourceColor);
	const ColorHSLA Actual(UpdatedColor);
	EXPECT_NEAR(Actual.h, Expected.h, 1.0f / 255.0f);
	EXPECT_NEAR(Actual.s, Expected.s, 1.0f / 255.0f);
	EXPECT_NEAR(Actual.l, Expected.l, 1.0f / 255.0f);
	EXPECT_NEAR(UpdatedOpacity, 37, 1);
}

TEST(SettingsAlphaColorPicker, CancelRestoresColorAndOpacityAfterPopupCloses)
{
	constexpr unsigned int InitialColor = 0x526C81;
	constexpr int InitialOpacity = 37;
	unsigned int Color = InitialColor;
	int Opacity = InitialOpacity;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);
	const unsigned int InitialPackedColor = State.m_PackedColor;

	State.RefreshFromConfig(Color, Opacity, true);
	State.m_PackedColor = PackSettingsAlphaColor(0x00FFFF, 81);
	ASSERT_TRUE(State.ApplyToConfig(Color, Opacity));
	ASSERT_EQ(Color, 0x00FFFFu);
	ASSERT_EQ(Opacity, 81);

	// 弹窗在设置行之后处理叉号或 Esc，恢复快照时设置行已完成本帧回写。
	State.m_PackedColor = InitialPackedColor;
	State.RefreshFromConfig(Color, Opacity, false);
	EXPECT_TRUE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, InitialColor);
	EXPECT_EQ(Opacity, InitialOpacity);

	State.RefreshFromConfig(Color, Opacity, false);
	EXPECT_FALSE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, InitialColor);
	EXPECT_EQ(Opacity, InitialOpacity);
}

TEST(SettingsAlphaColorPicker, ConfirmKeepsLastChangeBeforePopupCloses)
{
	unsigned int Color = 0x526C81;
	int Opacity = 37;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);

	State.m_PackedColor = PackSettingsAlphaColor(0x00FFFF, 81);
	State.RefreshFromConfig(Color, Opacity, false);
	EXPECT_TRUE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, 0x00FFFFu);
	EXPECT_EQ(Opacity, 81);

	State.RefreshFromConfig(Color, Opacity, false);
	EXPECT_FALSE(State.ApplyToConfig(Color, Opacity));
}

TEST(SettingsAlphaColorPicker, OpenPopupPreviewsColorAndOpacityChanges)
{
	unsigned int Color = 0x526C81;
	int Opacity = 37;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);

	State.m_PackedColor = PackSettingsAlphaColor(0x00FFFF, 81);
	State.RefreshFromConfig(Color, Opacity, true);
	EXPECT_TRUE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, 0x00FFFFu);
	EXPECT_EQ(Opacity, 81);

	State.m_PackedColor = PackSettingsAlphaColor(0x94C047, 0);
	State.RefreshFromConfig(Color, Opacity, true);
	EXPECT_TRUE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, 0x94C047u);
	EXPECT_EQ(Opacity, 0);
}

TEST(SettingsAlphaColorPicker, ClosedPopupFollowsExternalConfigChanges)
{
	unsigned int Color = 0x526C81;
	int Opacity = 37;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);
	State.m_PackedColor = PackSettingsAlphaColor(0x00FFFF, 81);
	ASSERT_TRUE(State.ApplyToConfig(Color, Opacity));

	Color = 0x94C047;
	Opacity = 100;
	State.RefreshFromConfig(Color, Opacity, false);
	EXPECT_EQ(State.m_PackedColor, PackSettingsAlphaColor(Color, Opacity));
	EXPECT_FALSE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, 0x94C047u);
	EXPECT_EQ(Opacity, 100);
}

TEST(SettingsAlphaColorPicker, ResetAppliesWithoutOpeningPopup)
{
	unsigned int Color = 0x526C81;
	int Opacity = 37;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);

	State.m_PackedColor = PackSettingsAlphaColor(0x94C047, 100);
	EXPECT_TRUE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, 0x94C047u);
	EXPECT_EQ(Opacity, 100);

	State.RefreshFromConfig(Color, Opacity, false);
	EXPECT_FALSE(State.ApplyToConfig(Color, Opacity));
}

TEST(SettingsAlphaColorPicker, ReopeningAfterCancelStartsFromRestoredConfig)
{
	constexpr unsigned int InitialColor = 0x526C81;
	unsigned int Color = InitialColor;
	int Opacity = 37;
	SSettingsAlphaColorPickerState State;
	State.RefreshFromConfig(Color, Opacity, false);
	const unsigned int InitialPackedColor = State.m_PackedColor;
	State.m_PackedColor = PackSettingsAlphaColor(0x00FFFF, 81);
	ASSERT_TRUE(State.ApplyToConfig(Color, Opacity));

	State.m_PackedColor = InitialPackedColor;
	State.RefreshFromConfig(Color, Opacity, false);
	ASSERT_TRUE(State.ApplyToConfig(Color, Opacity));
	State.RefreshFromConfig(Color, Opacity, false);
	State.RefreshFromConfig(Color, Opacity, true);
	EXPECT_EQ(State.m_PackedColor, InitialPackedColor);

	State.m_PackedColor = PackSettingsAlphaColor(0x94C047, 100);
	EXPECT_TRUE(State.ApplyToConfig(Color, Opacity));
	EXPECT_EQ(Color, 0x94C047u);
	EXPECT_EQ(Opacity, 100);
}
