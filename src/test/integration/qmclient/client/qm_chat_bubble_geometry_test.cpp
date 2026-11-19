#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/components/qmclient/chat_bubble_state.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmChatBubbleGeometryIntegration : public qm_ui_test::CRealUiFixture
	{
	protected:
		CQmChatBubbleState m_State;
		ITextRender &Text() { return *m_Ui.TextRender(); }
		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(::testing::Test::HasFatalFailure())
				return;
			Text().SetFontPreset(EFontPreset::DEFAULT_FONT);
			Text().SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
			Graphics().MapScreen(1, 2, 301, 202);
		}
		void TearDown() override
		{
			if(m_Ui.TextRender())
				m_State.Reset(Text());
			CRealUiFixture::TearDown();
		}
		void ExpectRestored()
		{
			EXPECT_EQ(Text().GetRenderFlags(), unsigned(TEXT_RENDER_FLAG_KERNING));
			float X0, Y0, X1, Y1;
			Graphics().GetScreen(&X0, &Y0, &X1, &Y1);
			EXPECT_EQ(X0, 1);
			EXPECT_EQ(Y0, 2);
			EXPECT_EQ(X1, 301);
			EXPECT_EQ(Y1, 202);
		}
	};
}

TEST_F(QmChatBubbleGeometryIntegration, NativeMetricsProduceFiniteAnimatedTextAndBackground)
{
	CQmChatBubbleRenderContext Context(Text(), Graphics());
	Context.Apply([&] { Graphics().MapScreen(10, 20, 310, 220); });
	m_State.PrepareLayout(Text(), 14, 230, "same message");
	const auto Anchor = QmChatBubbleProjectAnchor({-100, -50, 100, 50}, {10, 20, 310, 220}, vec2(0, -25));
	ASSERT_TRUE(Anchor);
	const auto Geometry = QmChatBubbleGeometry(*Anchor, vec2(m_State.m_CachedTextWidth, m_State.m_CachedTextHeight), 0.92f, 12, 5, false);
	ASSERT_TRUE(Geometry);
	ASSERT_TRUE(QmChatBubbleTextSizeSafe(14 * 0.92f, 200, Graphics().ScreenHeight()));
	const size_t Before = Graphics().m_vRectSubmissions.size();
	Graphics().DrawRect(Geometry->m_Position.x, Geometry->m_Position.y, Geometry->m_Size.x, Geometry->m_Size.y, ColorRGBA(0, 0, 0, 1), IGraphics::CORNER_ALL, 10);
	CTextCursor Cursor;
	Cursor.m_FontSize = 14 * 0.92f;
	Cursor.m_LineWidth = 230 * 0.92f;
	Cursor.m_Flags = TEXTFLAG_RENDER;
	Cursor.SetPosition(Geometry->m_ContentPosition);
	Text().TextEx(&Cursor, "same message");
	EXPECT_EQ(Graphics().m_vRectSubmissions.size(), Before + 1);
	EXPECT_TRUE(std::isfinite(Cursor.m_AlignedFontSize));
	EXPECT_GT(Cursor.m_AlignedFontSize, 0);
	Context.Finish();
	ExpectRestored();
}

TEST_F(QmChatBubbleGeometryIntegration, CachedNativeLayoutRemainsUnscaledNearUnitScale)
{
	m_State.PrepareLayout(Text(), 14, 230, "same message");
	ASSERT_TRUE(m_State.m_TextContainerIndex.Valid());
	const auto Usage = m_State.m_TextContainerIndex.m_UseCount;
	const auto Geometry = QmChatBubbleGeometry(vec2(100, 80), vec2(m_State.m_CachedTextWidth, m_State.m_CachedTextHeight), 1.0005f, 0, 5, true);
	ASSERT_TRUE(Geometry);
	EXPECT_EQ(Geometry->m_ContentSize, Text().GetBoundingBoxTextContainer(m_State.m_TextContainerIndex).Size());
	Text().RenderTextContainer(m_State.m_TextContainerIndex, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1), Geometry->m_ContentPosition.x, Geometry->m_ContentPosition.y);
	EXPECT_EQ(m_State.m_TextContainerIndex.m_UseCount, Usage);
	EXPECT_TRUE(m_State.m_TextContainerIndex.Valid());
}

TEST_F(QmChatBubbleGeometryIntegration, AnimationValueCanRecoverBeforeUtf8CharacterSelection)
{
	CQmAnimationBackend Animation;
	Animation.SetValue(1, EUiAnimProperty::WIDTH, std::numeric_limits<float>::quiet_NaN());
	EXPECT_FALSE(QmChatBubbleVisibleCharacters(Animation.GetValue(1, EUiAnimProperty::WIDTH), 4));
	Animation.SetValue(1, EUiAnimProperty::WIDTH, 2.6f);
	const auto Visible = QmChatBubbleVisibleCharacters(Animation.GetValue(1, EUiAnimProperty::WIDTH), 4);
	ASSERT_TRUE(Visible);
	char aVisible[32];
	str_utf8_copy_num(aVisible, "中abc", sizeof(aVisible), *Visible);
	EXPECT_STREQ(aVisible, "中ab");
}

TEST_F(QmChatBubbleGeometryIntegration, RejectedProjectionRestoresNativeContextWithoutDrawing)
{
	const auto Before = Graphics().m_Draws;
	{
		CQmChatBubbleRenderContext Context(Text(), Graphics());
		Context.Apply([&] { Graphics().MapScreen(10, 20, 310, 220); });
		EXPECT_FALSE(QmChatBubbleProjectAnchor({0, 0, 0, 100}, {10, 20, 310, 220}, vec2(0, 0)));
	}
	EXPECT_EQ(Graphics().m_Draws, Before);
	ExpectRestored();
}

TEST_F(QmChatBubbleGeometryIntegration, SubunitViewportGuardAgreesWithNativePixelFallback)
{
	CQmChatBubbleRenderContext Context(Text(), Graphics());
	Context.Apply([&] { Graphics().MapScreen(0, 0, 300, 0.5f); });
	ASSERT_TRUE(QmChatBubbleTextSizeSafe(14, 0.5f, Graphics().ScreenHeight()));
	CTextCursor Cursor;
	Cursor.m_FontSize = 14;
	Cursor.m_LineWidth = 230;
	Text().TextEx(&Cursor, "message");
	EXPECT_FLOAT_EQ(Cursor.m_AlignedFontSize, 14);
	Context.Finish();
	ExpectRestored();
}
