#include <game/client/QmUi/UiButtonStyle.h>

#include <gtest/gtest.h>

// 按钮表面圆角是填充、外层描边与焦点环的唯一来源：早先填充按「宽 ≥ 1.8 倍高」
// 选胶囊端头，描边却按用途档位取 8/10，悬浮时就成了「胶囊填充 + 圆角矩形边框」。
TEST(QmButtonSurfaceRadius, WideBarsUseCapsuleEndsForFillAndBorder)
{
	for(const EUiButtonGeometry Geometry : {EUiButtonGeometry::CONTROL, EUiButtonGeometry::START_MENU})
	{
		SCOPED_TRACE(static_cast<int>(Geometry));
		for(const float Ratio : {1.8f, 2.0f, 4.5f})
		{
			SCOPED_TRACE(::testing::Message() << "ratio=" << Ratio);
			const CUIRect Bar{0.0f, 0.0f, 140.0f, 140.0f / Ratio};
			EXPECT_FLOAT_EQ(ResolveUiSurfaceRadius(Bar, Geometry), ui_token::radius::PILL);
		}
	}
}

TEST(QmButtonSurfaceRadius, NearSquareControlsKeepTheirPurposeRadius)
{
	const CUIRect Square{0.0f, 0.0f, 20.0f, 20.0f};
	EXPECT_FLOAT_EQ(ResolveUiSurfaceRadius(Square, EUiButtonGeometry::CONTROL), ui_token::radius::BASE);
	EXPECT_FLOAT_EQ(ResolveUiSurfaceRadius(Square, EUiButtonGeometry::START_MENU), ui_token::radius::MENU);

	// 1.8 倍是长条判定的边界：略低于它仍按用途档位，不能在这里悄悄放宽。
	const CUIRect JustUnderCapsule{0.0f, 0.0f, 35.8f, 20.0f};
	EXPECT_FLOAT_EQ(ResolveUiSurfaceRadius(JustUnderCapsule, EUiButtonGeometry::CONTROL), ui_token::radius::BASE);
	EXPECT_FLOAT_EQ(ResolveUiSurfaceRadius(JustUnderCapsule, EUiButtonGeometry::START_MENU), ui_token::radius::MENU);
}
