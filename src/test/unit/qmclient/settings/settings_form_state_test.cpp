#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/UiForms.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

TEST(SettingsInputField, LayoutKeepsTrailingUnitInsideTheShell)
{
	const CUIRect Shell{10.0f, 20.0f, 120.0f, 24.0f};
	const ui_widget::SInputFieldLayout Layout = ui_widget::ResolveInputFieldLayout(Shell, false, false, 1.0f, 24.0f);

	EXPECT_FLOAT_EQ(Layout.m_ShellRect.x, Shell.x);
	EXPECT_FLOAT_EQ(Layout.m_ShellRect.w, Shell.w);
	EXPECT_GT(Layout.m_TrailingRect.w, 0.0f);
	EXPECT_GE(Layout.m_TrailingRect.x, Shell.x);
	EXPECT_LE(Layout.m_TrailingRect.x + Layout.m_TrailingRect.w, Shell.x + Shell.w);
	EXPECT_LE(Layout.m_ContentRect.x + Layout.m_ContentRect.w, Layout.m_TrailingRect.x);
}

TEST(SettingsDropDown, DisablingOpenStateRequestsPopupCloseAndReleasesSelectionState)
{
	CQmDropdownState State;
	SQmDropdownInput Open;
	Open.m_TogglePressed = true;
	Open.m_InitialIndex = 2;
	ASSERT_TRUE(State.Update(Open, 4).m_Opened);
	ASSERT_TRUE(State.IsOpen());

	EXPECT_TRUE(State.Disable(true));
	EXPECT_FALSE(State.IsOpen());
	EXPECT_EQ(State.ActiveIndex(), -1);
	EXPECT_FALSE(State.Disable(false));
}

TEST(SettingsDropDown, OpenSelectAndCloseTransitionsReleaseThePopup)
{
	CQmDropdownState State;
	SQmDropdownInput Open;
	Open.m_TogglePressed = true;
	Open.m_InitialIndex = 0;
	EXPECT_TRUE(State.Update(Open, 9).m_Opened);
	EXPECT_TRUE(State.IsOpen());

	SQmDropdownInput Select;
	Select.m_HoveredIndex = 4;
	Select.m_MouseSelectPressed = true;
	const SQmDropdownUpdateResult Selected = State.Update(Select, 9);
	EXPECT_TRUE(Selected.m_Selected);
	EXPECT_EQ(Selected.m_SelectedIndex, 4);
	EXPECT_FALSE(State.IsOpen());

	SQmDropdownInput Reopen;
	Reopen.m_TogglePressed = true;
	Reopen.m_InitialIndex = 4;
	EXPECT_TRUE(State.Update(Reopen, 9).m_Opened);
	SQmDropdownInput Escape;
	Escape.m_KeyEscape = true;
	EXPECT_TRUE(State.Update(Escape, 9).m_Closed);
	EXPECT_FALSE(State.IsOpen());
}
