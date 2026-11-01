#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/UiTextRenderState.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>

namespace
{
	struct STextRender
	{
		ColorRGBA m_Color{0.2f, 0.3f, 0.4f, 0.5f};
		ColorRGBA m_Outline{0.1f, 0.2f, 0.3f, 0.4f};
		ColorRGBA m_Selection{0.6f, 0.7f, 0.8f, 0.9f};
		EFontPreset m_Preset = EFontPreset::ICON_FONT_FILL;
		unsigned m_Flags = TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT;
		int m_Writes = 0;
		ColorRGBA GetTextColor() const { return m_Color; }
		ColorRGBA GetTextOutlineColor() const { return m_Outline; }
		ColorRGBA GetTextSelectionColor() const { return m_Selection; }
		EFontPreset GetFontPreset() const { return m_Preset; }
		unsigned GetRenderFlags() const { return m_Flags; }
		void TextColor(ColorRGBA Value)
		{
			m_Color = Value;
			++m_Writes;
		}
		void TextOutlineColor(ColorRGBA Value)
		{
			m_Outline = Value;
			++m_Writes;
		}
		void TextSelectionColor(ColorRGBA Value)
		{
			m_Selection = Value;
			++m_Writes;
		}
		void SetFontPreset(EFontPreset Value)
		{
			m_Preset = Value;
			++m_Writes;
		}
		void SetRenderFlags(unsigned Value)
		{
			m_Flags = Value;
			++m_Writes;
		}
	};

	void ChangeTextState(STextRender &Text)
	{
		Text.TextColor({1.0f, 0.0f, 0.0f, 0.7f});
		Text.TextOutlineColor({0.0f, 1.0f, 0.0f, 0.6f});
		Text.TextSelectionColor({0.0f, 0.0f, 1.0f, 0.5f});
		Text.SetFontPreset(EFontPreset::DEFAULT_FONT);
		Text.SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING);
	}

	void ExpectState(const STextRender &Text, const STextRender &Before)
	{
		EXPECT_EQ(Text.m_Color, Before.m_Color);
		EXPECT_EQ(Text.m_Outline, Before.m_Outline);
		EXPECT_EQ(Text.m_Selection, Before.m_Selection);
		EXPECT_EQ(Text.m_Preset, Before.m_Preset);
		EXPECT_EQ(Text.m_Flags, Before.m_Flags);
	}
}

TEST(SettingsCardTextState, EarlyReturnRestoresCallerFontFlagsAndColors)
{
	STextRender Text;
	const STextRender Before = Text;
	const auto Render = [&]() {
		const CUiScopedTextRenderState State(Text);
		ChangeTextState(Text);
		return;
	};
	Render();
	ExpectState(Text, Before);
}

TEST(SettingsCardTextState, ExplicitRestoreDoesNotOwnFollowingDrawState)
{
	STextRender Text;
	const STextRender Before = Text;
	const ColorRGBA FollowingColor(0.8f, 0.7f, 0.2f, 0.4f);
	int WritesAfterFollowingDraw = 0;
	{
		CUiScopedTextRenderState State(Text);
		ChangeTextState(Text);
		State.Restore();
		ExpectState(Text, Before);
		Text.TextColor(FollowingColor);
		WritesAfterFollowingDraw = Text.m_Writes;
		State.Restore();
		EXPECT_EQ(Text.m_Writes, WritesAfterFollowingDraw);
	}
	EXPECT_EQ(Text.m_Color, FollowingColor);
	EXPECT_EQ(Text.m_Writes, WritesAfterFollowingDraw);
}

TEST(SettingsCardTextState, IconResourceExceptionRestoresAllStateAndPropagates)
{
	for(bool LengthError : {false, true})
	{
		SCOPED_TRACE(LengthError);
		STextRender Text;
		const STextRender Before = Text;
		int Draws = 0;
		try
		{
			ExecuteSettingsCardHeaderIcon(Text, {1, 1, 1, 0.5f}, [&]() {
				++Draws;
				ChangeTextState(Text);
				if(LengthError)
					throw std::length_error("icon draw");
				throw std::bad_alloc();
			});
			FAIL() << "Drawing exception must propagate";
		}
		catch(const std::length_error &)
		{
			EXPECT_TRUE(LengthError);
		}
		catch(const std::bad_alloc &)
		{
			EXPECT_FALSE(LengthError);
		}
		EXPECT_EQ(Draws, 1);
		ExpectState(Text, Before);
	}
}

TEST(SettingsCardTextState, NestedIconExceptionRestoresOuterStateThenCallerState)
{
	STextRender Text;
	const STextRender Before = Text;
	ExecuteSettingsCardHeaderIcon(Text, {1, 0, 0, 0.7f}, [&]() {
		const STextRender Outer = Text;
		int Error = 0;
		try
		{
			ExecuteSettingsCardHeaderIcon(Text, {0, 1, 0, 0.4f}, [&]() {
				ChangeTextState(Text);
				throw 17;
			});
		}
		catch(int Value)
		{
			Error = Value;
		}
		EXPECT_EQ(Error, 17);
		ExpectState(Text, Outer);
	});
	ExpectState(Text, Before);
}
