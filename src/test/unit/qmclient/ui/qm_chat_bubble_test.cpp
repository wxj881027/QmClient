#include <game/client/QmUi/QmChatBubble.h>

#include <gtest/gtest.h>

TEST(QmChatBubble, EmptyTypingPreviewKeepsRoomForOneCharacter)
{
	const auto Bubble = qm_chat_bubble::Layout(vec2(100, 200), vec2(0, 0), 20.0f, 1.0f);
	EXPECT_GE(Bubble.m_Size.x, 20.0f);
	EXPECT_GE(Bubble.m_Size.y, 20.0f);
	EXPECT_GT(Bubble.m_TextPosition.x, Bubble.m_Position.x);
	EXPECT_GT(Bubble.m_TextPosition.y, Bubble.m_Position.y);
}

TEST(QmChatBubble, MultilineTextGrowsUpwardsAndKeepsTailAtNameplateAnchor)
{
	const vec2 Anchor(100, 200);
	const auto Short = qm_chat_bubble::Layout(Anchor, vec2(100, 20), 20.0f, 1.0f);
	const auto Long = qm_chat_bubble::Layout(Anchor, vec2(200, 60), 20.0f, 1.0f);
	EXPECT_LT(Long.m_Position.y, Short.m_Position.y);
	EXPECT_GT(Long.m_Size.y, Short.m_Size.y);
	EXPECT_FLOAT_EQ(Long.m_Position.y + Long.m_Size.y + Long.m_TailHeight, Anchor.y);
	EXPECT_FLOAT_EQ(Long.m_TextPosition.x + 100.0f, Anchor.x);
	EXPECT_GT(Long.m_Position.y + Long.m_Size.y - Long.m_TextPosition.y, 60.0f);
}

TEST(QmChatBubble, ShrinkingKeepsTextAndTailInTheSameCoordinateSystem)
{
	const vec2 Anchor(100, 200);
	const vec2 TextSize(100, 40);
	const auto Full = qm_chat_bubble::Layout(Anchor, TextSize, 20.0f, 1.0f);
	const auto Small = qm_chat_bubble::Layout(Anchor, TextSize, 20.0f, 0.2f);
	EXPECT_NEAR(Small.m_Position.x - Anchor.x, (Full.m_Position.x - Anchor.x) * 0.2f, 0.0001f);
	EXPECT_NEAR(Small.m_Position.y - Anchor.y, (Full.m_Position.y - Anchor.y) * 0.2f, 0.0001f);
	EXPECT_NEAR(Small.m_TextPosition.y - Anchor.y, (Full.m_TextPosition.y - Anchor.y) * 0.2f, 0.0001f);
	EXPECT_FLOAT_EQ(Small.m_TailHeight, Full.m_TailHeight * 0.2f);
	EXPECT_FLOAT_EQ(Small.m_Stroke, Full.m_Stroke * 0.2f);
}

TEST(QmChatBubble, FillTrianglesStayInsideContourAtFontAndAnimationExtremes)
{
	for(const float FontSize : {8.0f, 20.0f, 32.0f})
		for(const float Scale : {0.2f, 1.0f, 1.05f})
		{
			SCOPED_TRACE(::testing::Message() << "font=" << FontSize << " scale=" << Scale);
			const auto Bubble = qm_chat_bubble::Layout(vec2(0, 0), vec2(0, 0), FontSize, Scale);
			const auto aInner = qm_chat_bubble::Contour(Bubble, Bubble.m_Stroke);
			const auto aOuter = qm_chat_bubble::Contour(Bubble);
			EXPECT_GT(Bubble.m_Size.x * 0.5f - Bubble.m_Radius, Bubble.m_TailLean + Bubble.m_TailHalfWidth);
			const vec2 Center = Bubble.m_Position + Bubble.m_Size * 0.5f;
			float LowestY = Bubble.m_Position.y;
			for(size_t i = 0; i < aInner.size(); ++i)
			{
				const vec2 A = aInner[i] - Center;
				const vec2 B = aInner[(i + 1) % aInner.size()] - Center;
				EXPECT_GE(A.x * B.y - A.y * B.x, -0.0001f);
				EXPECT_LE(aOuter[i].y, 0.0001f);
				LowestY = std::max(LowestY, aOuter[i].y);
			}
			EXPECT_NEAR(LowestY, 0.0f, 0.0001f);
		}
}

TEST(QmChatBubble, CustomBackgroundKeepsItsColorAndGetsContrastingInk)
{
	const ColorRGBA Dark(0.1f, 0.05f, 0.2f, 0.8f);
	const auto DarkPalette = qm_chat_bubble::Palette(Dark, 0.5f);
	EXPECT_FLOAT_EQ(DarkPalette.m_Fill.r, Dark.r);
	EXPECT_FLOAT_EQ(DarkPalette.m_Fill.g, Dark.g);
	EXPECT_FLOAT_EQ(DarkPalette.m_Fill.b, Dark.b);
	EXPECT_FLOAT_EQ(DarkPalette.m_Fill.a, 0.4f);
	EXPECT_GT(DarkPalette.m_Outline.r, Dark.r);
	const ColorRGBA Light(1.0f, 0.95f, 0.8f, 1.0f);
	const auto LightPalette = qm_chat_bubble::Palette(Light, 1.0f);
	EXPECT_LT(LightPalette.m_Outline.r, Light.r);
}

TEST(QmChatBubble, ZeroOpacityOrTransparentBackgroundHidesAllDecoration)
{
	for(const auto &Colors : {
		    qm_chat_bubble::Palette(ColorRGBA(1, 1, 1, 1), 0.0f),
		    qm_chat_bubble::Palette(ColorRGBA(1, 1, 1, 0), 1.0f)})
	{
		EXPECT_FLOAT_EQ(Colors.m_Fill.a, 0.0f);
		EXPECT_FLOAT_EQ(Colors.m_Outline.a, 0.0f);
		EXPECT_FLOAT_EQ(Colors.m_Shadow.a, 0.0f);
	}
}
