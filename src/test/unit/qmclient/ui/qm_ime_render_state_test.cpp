#include <game/client/QmUi/QmImeRenderState.h>

#include <gtest/gtest.h>

#include <array>
#include <new>
#include <stdexcept>

namespace
{
	struct SGraphics
	{
		std::array<float, 4> m_Screen{-123.0f, -45.0f, 333.0f, 222.0f};
		void GetScreen(float *pX0, float *pY0, float *pX1, float *pY1) const
		{
			*pX0 = m_Screen[0];
			*pY0 = m_Screen[1];
			*pX1 = m_Screen[2];
			*pY1 = m_Screen[3];
		}
		void MapScreen(float X0, float Y0, float X1, float Y1) { m_Screen = {X0, Y0, X1, Y1}; }
	};

	struct STextRender
	{
		unsigned m_Flags = 0x35;
		ColorRGBA m_Color{0.2f, 0.3f, 0.4f, 0.5f};
		ColorRGBA m_Outline{0.6f, 0.7f, 0.8f, 0.9f};
		int m_ColorWrites = 0;
		unsigned GetRenderFlags() const { return m_Flags; }
		void SetRenderFlags(unsigned Flags) { m_Flags = Flags; }
		ColorRGBA GetTextColor() const { return m_Color; }
		ColorRGBA GetTextOutlineColor() const { return m_Outline; }
		void TextColor(ColorRGBA Color)
		{
			m_Color = Color;
			++m_ColorWrites;
		}
		void TextOutlineColor(ColorRGBA Color)
		{
			m_Outline = Color;
			++m_ColorWrites;
		}
	};

	void ExpectColor(ColorRGBA Actual, ColorRGBA Expected)
	{
		EXPECT_FLOAT_EQ(Actual.r, Expected.r);
		EXPECT_FLOAT_EQ(Actual.g, Expected.g);
		EXPECT_FLOAT_EQ(Actual.b, Expected.b);
		EXPECT_FLOAT_EQ(Actual.a, Expected.a);
	}

	void ChangeState(SGraphics &Graphics, STextRender &Text)
	{
		Graphics.MapScreen(0.0f, 0.0f, 800.0f, 300.0f);
		Text.SetRenderFlags(0x80);
		Text.TextColor({1.0f, 0.0f, 0.0f, 0.4f});
		Text.TextOutlineColor({0.0f, 0.0f, 0.0f, 0.0f});
	}

	void ExpectRestored(const SGraphics &Graphics, const STextRender &Text)
	{
		EXPECT_EQ(Graphics.m_Screen, (std::array<float, 4>{-123.0f, -45.0f, 333.0f, 222.0f}));
		EXPECT_EQ(Text.GetRenderFlags(), 0x35u);
		ExpectColor(Text.GetTextColor(), {0.2f, 0.3f, 0.4f, 0.5f});
		ExpectColor(Text.GetTextOutlineColor(), {0.6f, 0.7f, 0.8f, 0.9f});
	}
}

TEST(QmImeRenderState, NormalDrawingRestoresCallerMappingFlagsAndColors)
{
	SGraphics Graphics;
	STextRender Text;
	{
		const CQmImeScreenState ScreenState(Graphics, Text);
		const CQmImeTextColorState TextState(Text);
		ChangeState(Graphics, Text);
	}
	ExpectRestored(Graphics, Text);
}

TEST(QmImeRenderState, EarlyReturnBeforeTextDoesNotWriteColors)
{
	SGraphics Graphics;
	STextRender Text;
	const auto Prepare = [&] {
		const CQmImeScreenState ScreenState(Graphics, Text);
		Text.SetRenderFlags(0x80);
		Graphics.MapScreen(0.0f, 0.0f, 800.0f, 300.0f);
		return;
	};
	Prepare();
	ExpectRestored(Graphics, Text);
	EXPECT_EQ(Text.m_ColorWrites, 0);
}

TEST(QmImeRenderState, MeasurementAllocationExceptionRestoresMappingAndFlags)
{
	SGraphics Graphics;
	STextRender Text;
	const auto Measure = [&] {
		const CQmImeScreenState ScreenState(Graphics, Text);
		Text.SetRenderFlags(0x80);
		Graphics.MapScreen(0.0f, 0.0f, 800.0f, 300.0f);
		throw std::bad_alloc();
	};
	EXPECT_THROW(Measure(), std::bad_alloc);
	ExpectRestored(Graphics, Text);
	EXPECT_EQ(Text.m_ColorWrites, 0);
}

TEST(QmImeRenderState, TextLengthExceptionRestoresColorsBeforeLeavingFrame)
{
	SGraphics Graphics;
	STextRender Text;
	const auto Draw = [&] {
		const CQmImeScreenState ScreenState(Graphics, Text);
		const CQmImeTextColorState TextState(Text);
		ChangeState(Graphics, Text);
		throw std::length_error("text preparation");
	};
	EXPECT_THROW(Draw(), std::length_error);
	ExpectRestored(Graphics, Text);
}

TEST(QmImeRenderState, UnexpectedExceptionPropagatesWithCallerStateRestored)
{
	SGraphics Graphics;
	STextRender Text;
	const auto Draw = [&] {
		const CQmImeScreenState ScreenState(Graphics, Text);
		const CQmImeTextColorState TextState(Text);
		ChangeState(Graphics, Text);
		throw 17;
	};
	try
	{
		Draw();
		FAIL() << "exception was swallowed";
	}
	catch(int Value)
	{
		EXPECT_EQ(Value, 17);
	}
	ExpectRestored(Graphics, Text);
}

TEST(QmImeRenderState, NestedPreviewRestoresParentThenOuterCaller)
{
	SGraphics Graphics;
	STextRender Text;
	{
		const CQmImeScreenState ScreenState(Graphics, Text);
		const CQmImeTextColorState TextState(Text);
		ChangeState(Graphics, Text);
		const auto ParentScreen = Graphics.m_Screen;
		const auto ParentColor = Text.GetTextColor();
		const auto ParentOutline = Text.GetTextOutlineColor();
		{
			const CQmImeScreenState ChildScreenState(Graphics, Text);
			const CQmImeTextColorState ChildTextState(Text);
			Graphics.MapScreen(10.0f, 20.0f, 200.0f, 150.0f);
			Text.SetRenderFlags(0);
			Text.TextColor({0.1f, 0.1f, 0.1f, 1.0f});
			Text.TextOutlineColor({1.0f, 1.0f, 1.0f, 1.0f});
		}
		EXPECT_EQ(Graphics.m_Screen, ParentScreen);
		EXPECT_EQ(Text.GetRenderFlags(), 0x80u);
		ExpectColor(Text.GetTextColor(), ParentColor);
		ExpectColor(Text.GetTextOutlineColor(), ParentOutline);
	}
	ExpectRestored(Graphics, Text);
}
