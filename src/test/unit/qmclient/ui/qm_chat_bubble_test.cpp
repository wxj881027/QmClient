#include <game/client/QmUi/QmChatBubble.h>
#include <game/client/QmUi/QmCloudBubble.h>

#include <gtest/gtest.h>

TEST(QmChatBubble, EdgeFeatherTracksPhysicalPixelsAtDifferentViewportScales)
{
	const float Normal = qm_chat_bubble::AntialiasWidth(vec2(1200, 600), vec2(1200, 600));
	const float HiDpi = qm_chat_bubble::AntialiasWidth(vec2(1200, 600), vec2(2400, 1200));
	const float EnlargedMapping = qm_chat_bubble::AntialiasWidth(vec2(2400, 1200), vec2(1200, 600));
	EXPECT_FLOAT_EQ(Normal, 1.25f);
	EXPECT_FLOAT_EQ(HiDpi, Normal * 0.5f);
	EXPECT_FLOAT_EQ(EnlargedMapping, Normal * 2.0f);
}

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

TEST(QmChatBubble, OutlinedStylesKeepContoursWoundAndAnchoredAtFontAndAnimationExtremes)
{
	for(int StyleIndex = 0; StyleIndex < 2; ++StyleIndex)
		for(const float FontSize : {8.0f, 20.0f, 32.0f})
			for(const float Scale : {0.2f, 1.0f, 1.05f})
				for(const vec2 TextSize : {vec2(0, 0), vec2(230, 200)})
				{
					SCOPED_TRACE(::testing::Message() << "style=" << StyleIndex << " font=" << FontSize << " scale=" << Scale << " width=" << TextSize.x);
					const auto Bubble = qm_chat_bubble::Layout(vec2(0, 0), TextSize, FontSize, Scale, qm_chat_bubble::StyleFromConfig(StyleIndex));
					const auto Inner = qm_chat_bubble::Contour(Bubble, Bubble.m_Stroke);
					const auto Outer = qm_chat_bubble::Contour(Bubble);
					ASSERT_GT(Inner.m_Count, 0);
					ASSERT_LE(Inner.m_Count, qm_chat_bubble::MAX_CONTOUR_POINTS);
					ASSERT_EQ(Inner.m_Count, Outer.m_Count);
					const vec2 Center = Bubble.m_Position + Bubble.m_Size * 0.5f;
					float LowestY = Bubble.m_Position.y;
					for(int i = 0; i < Inner.m_Count; ++i)
					{
						const vec2 A = Inner.m_aPoints[i] - Center;
						const vec2 B = Inner.m_aPoints[(i + 1) % Inner.m_Count] - Center;
						EXPECT_GE(A.x * B.y - A.y * B.x, -0.0001f);
						EXPECT_LE(Outer.m_aPoints[i].y, 0.0001f);
						LowestY = std::max(LowestY, Outer.m_aPoints[i].y);
					}
					EXPECT_NEAR(LowestY, 0.0f, 0.0001f);
				}
}

TEST(QmChatBubble, CustomColorsOverrideEveryPresetWithoutChangingSavedColors)
{
	const ColorRGBA Dark(0.1f, 0.05f, 0.2f, 0.8f);
	const ColorRGBA Text(0.9f, 0.8f, 0.7f);
	for(int i = 0; i < qm_chat_bubble::STYLE_COUNT; ++i)
	{
		const auto Colors = qm_chat_bubble::Palette(qm_chat_bubble::StyleFromConfig(i), true, Dark, Text, 0.5f);
		EXPECT_EQ(Colors.m_Fill, Dark.WithAlpha(0.4f));
		EXPECT_EQ(Colors.m_FillBottom, Colors.m_Fill);
		EXPECT_EQ(Colors.m_Text, Text.WithAlpha(0.5f));
		EXPECT_GT(Colors.m_Outline.r, Dark.r);
	}
}

TEST(QmChatBubble, ZeroOpacityOrTransparentCustomBackgroundHidesAllDecoration)
{
	for(int i = 0; i < qm_chat_bubble::STYLE_COUNT; ++i)
		for(const bool Custom : {false, true})
		{
			const auto Style = qm_chat_bubble::StyleFromConfig(i);
			const auto Colors = qm_chat_bubble::Palette(Style, Custom, ColorRGBA(1, 1, 1, 1), ColorRGBA(1, 1, 1), 0.0f);
			EXPECT_FLOAT_EQ(Colors.m_Fill.a, 0.0f);
			EXPECT_FLOAT_EQ(Colors.m_Outline.a, 0.0f);
			EXPECT_FLOAT_EQ(Colors.m_Shadow.a, 0.0f);
			EXPECT_FLOAT_EQ(Colors.m_Text.a, 0.0f);
			const auto Transparent = qm_chat_bubble::Palette(Style, true, ColorRGBA(1, 1, 1, 0), ColorRGBA(1, 1, 1), 1.0f);
			EXPECT_FLOAT_EQ(Transparent.m_Fill.a, 0.0f);
			EXPECT_FLOAT_EQ(Transparent.m_Outline.a, 0.0f);
			EXPECT_FLOAT_EQ(Transparent.m_Shadow.a, 0.0f);
			EXPECT_FLOAT_EQ(Transparent.m_Text.a, 1.0f);
		}
}

TEST(QmChatBubble, PresetsSupplyTheirOwnReadableTextAndGradientColors)
{
	using qm_chat_bubble::EStyle;
	const auto Comic = qm_chat_bubble::Palette(EStyle::COMIC, false, {}, {}, 1.0f);
	const auto Candy = qm_chat_bubble::Palette(EStyle::CANDY, false, {}, {}, 1.0f);
	EXPECT_EQ(Comic.m_Fill, Comic.m_FillBottom);
	EXPECT_GT(Comic.m_Fill.r - Comic.m_Text.r, 0.7f);
	EXPECT_NE(Candy.m_Fill, Candy.m_FillBottom);
	EXPECT_GT(Candy.m_Fill.b - Candy.m_Text.b, 0.5f);
}

TEST(QmChatBubble, InvalidStyleValuesStayWithinAvailablePresets)
{
	EXPECT_EQ(qm_chat_bubble::StyleFromConfig(-1), qm_chat_bubble::EStyle::COMIC);
	EXPECT_EQ(qm_chat_bubble::StyleFromConfig(999), qm_chat_bubble::EStyle::CLOUD);
}

TEST(QmChatBubble, ProceduralCloudHasDenseCenterSoftEdgesAndTransparentTileBoundary)
{
	for(int Variant = 0; Variant < 4; ++Variant)
	{
		SCOPED_TRACE(Variant);
		EXPECT_GT(qm_chat_bubble::CloudTexel(vec2(0, 0), Variant).a, 0.9f);
		bool HasSoftEdge = false;
		for(int Sample = 0; Sample <= 24; ++Sample)
		{
			const auto Color = qm_chat_bubble::CloudTexel(vec2(Sample * 0.05f, 0), Variant);
			EXPECT_GE(Color.a, 0.0f);
			EXPECT_LE(Color.a, 1.0f);
			HasSoftEdge |= Color.a > 0.02f && Color.a < 0.95f;
			EXPECT_GE(Color.r, 0.7f);
			EXPECT_LE(Color.b, 1.0f);
		}
		EXPECT_TRUE(HasSoftEdge);
		for(const vec2 Edge : {vec2(-1.2f, 0), vec2(1.2f, 0), vec2(0, -1.2f), vec2(0, 1.2f)})
			EXPECT_FLOAT_EQ(qm_chat_bubble::CloudTexel(Edge, Variant).a, 0.0f);
	}
}

TEST(QmChatBubble, CloudGatheringConvergesAndDispersalKeepsBoundedWork)
{
	const auto Layout = qm_chat_bubble::Layout(vec2(0, 0), vec2(230, 60), 20, 1, qm_chat_bubble::EStyle::CLOUD);
	const auto Gathered = qm_chat_bubble::CloudPuffs(Layout, 1);
	const auto Halfway = qm_chat_bubble::CloudPuffs(Layout, 0.5f);
	const auto Dispersed = qm_chat_bubble::CloudPuffs(Layout, 0);
	const auto Clamped = qm_chat_bubble::CloudPuffs(Layout, 2);
	ASSERT_EQ(Gathered.size(), 9u);
	for(size_t i = 0; i < Gathered.size(); ++i)
	{
		EXPECT_GT(distance(Dispersed[i].m_Center, Gathered[i].m_Center), distance(Halfway[i].m_Center, Gathered[i].m_Center));
		EXPECT_LT(Dispersed[i].m_Size.x, Gathered[i].m_Size.x);
		EXPECT_EQ(Clamped[i].m_Center, Gathered[i].m_Center);
		EXPECT_GE(Gathered[i].m_Variant, 0);
		EXPECT_LT(Gathered[i].m_Variant, 4);
	}
}

TEST(QmChatBubble, CloudTextAppearsAfterGatheringAndShortMessagesRetainReadingTime)
{
	EXPECT_FLOAT_EQ(qm_chat_bubble::CloudTextOpacity(0.1f), 0.0f);
	EXPECT_GT(qm_chat_bubble::CloudTextOpacity(0.6f), qm_chat_bubble::CloudTextOpacity(0.4f));
	EXPECT_FLOAT_EQ(qm_chat_bubble::CloudTextOpacity(1.0f), 1.0f);
	EXPECT_LT(qm_chat_bubble::CloudExitDuration(1), 0.5f);
	EXPECT_LE(qm_chat_bubble::CloudExitDuration(30), 1.0f);
}

TEST(QmChatBubble, MultilineTextCornersStayInsideOutlinedStyles)
{
	for(int StyleIndex = 0; StyleIndex < 2; ++StyleIndex)
		for(const vec2 TextSize : {vec2(230, 20), vec2(230, 200), vec2(20, 200)})
		{
			SCOPED_TRACE(::testing::Message() << "style=" << StyleIndex << " text=" << TextSize.x << "," << TextSize.y);
			const auto Bubble = qm_chat_bubble::Layout(vec2(0, 0), TextSize, 20.0f, 1.0f, qm_chat_bubble::StyleFromConfig(StyleIndex));
			const auto Inner = qm_chat_bubble::Contour(Bubble, Bubble.m_Stroke);
			for(const vec2 Offset : {vec2(0, 0), vec2(TextSize.x, 0), TextSize, vec2(0, TextSize.y)})
			{
				const vec2 Point = Bubble.m_TextPosition + Offset;
				bool Inside = false;
				// 用射线交点判定文字边界，不复刻生产轮廓生成过程。
				for(int i = 0, j = Inner.m_Count - 1; i < Inner.m_Count; j = i++)
				{
					const vec2 A = Inner.m_aPoints[i];
					const vec2 B = Inner.m_aPoints[j];
					if((A.y > Point.y) != (B.y > Point.y) && Point.x < (B.x - A.x) * (Point.y - A.y) / (B.y - A.y) + A.x)
						Inside = !Inside;
				}
				EXPECT_TRUE(Inside);
			}
		}
}
