#include <test/support/qm_real_ui_fixture.h>

#include <string>

namespace
{
	class QmTextUnbufferedPasses : public qm_ui_test::CRealUiFixture
	{
	protected:
		STextContainerIndex m_Container;
		ITextRender &Text() { return *m_Ui.TextRender(); }
		void Prepare(size_t Glyphs)
		{
			Graphics().m_TextBuffering = false;
			Text().TextColor(ColorRGBA(1, 1, 1, 1));
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(0, 0));
			Cursor.m_FontSize = 14;
			Cursor.m_LineWidth = -1;
			const std::string Value(Glyphs, 'a');
			ASSERT_TRUE(Text().CreateTextContainer(m_Container, &Cursor, Value.c_str()));
			ASSERT_EQ(Cursor.m_GlyphCount, Glyphs);
			Graphics().m_RecordLegacyText = true;
		}
		void TearDown() override
		{
			Graphics().m_RecordLegacyText = false;
			if(m_Container.Valid())
				Text().DeleteTextContainer(m_Container);
			Graphics().m_TextBuffering = true;
			CRealUiFixture::TearDown();
		}
	};
}

TEST_F(QmTextUnbufferedPasses, ShortTextReusesTheRetainedOutlineVertices)
{
	Prepare(3);
	ASSERT_TRUE(m_Container.Valid());
	Text().RenderTextContainer(m_Container, ColorRGBA(0.5f, 0.5f, 0.5f, 1), ColorRGBA(0, 0, 0, 1));
	EXPECT_EQ(Graphics().m_vLegacyDraws.size(), 3u);
	EXPECT_EQ(Graphics().m_LegacyKeepCalls, 1);
	EXPECT_EQ(Graphics().m_LegacyReplayCalls, 1);
	ASSERT_EQ(Graphics().m_vLegacyRecolors.size(), 3u);
	EXPECT_EQ(Graphics().m_vLegacyRecolors.front().first, 0u);
	EXPECT_EQ(Graphics().m_vLegacyRecolors.back().first, 2u);
	EXPECT_FLOAT_EQ(Graphics().m_vLegacyRecolors.front().second.r, 127 / 255.0f);
}

TEST_F(QmTextUnbufferedPasses, LongTextSubmitsEveryOutlineBeforeEveryFillWithoutRecoloringOldVertices)
{
	Prepare(9000);
	ASSERT_TRUE(m_Container.Valid());
	Text().RenderTextContainer(m_Container, ColorRGBA(0.5f, 0.5f, 0.5f, 1), ColorRGBA(0, 0, 0, 1));
	const auto &vDraws = Graphics().m_vLegacyDraws;
	ASSERT_EQ(vDraws.size(), 18000u);
	EXPECT_EQ(Graphics().m_LegacyKeepCalls, 0);
	EXPECT_EQ(Graphics().m_LegacyReplayCalls, 0);
	EXPECT_TRUE(Graphics().m_vLegacyRecolors.empty());
	const int Outline = vDraws.front().m_Texture, Fill = vDraws[9000].m_Texture;
	ASSERT_NE(Outline, Fill);
	for(size_t i = 0; i < 9000; ++i)
	{
		EXPECT_EQ(vDraws[i].m_Texture, Outline) << i;
		EXPECT_EQ(vDraws[9000 + i].m_Texture, Fill) << i;
		EXPECT_FLOAT_EQ(vDraws[9000 + i].m_Item.m_X, vDraws[i].m_Item.m_X) << i;
		EXPECT_FLOAT_EQ(vDraws[9000 + i].m_Item.m_Y, vDraws[i].m_Item.m_Y) << i;
	}
	EXPECT_FLOAT_EQ(vDraws[9000].m_Color.r, 127 / 255.0f);
	EXPECT_FLOAT_EQ(vDraws.back().m_Color.r, 127 / 255.0f);
}

TEST_F(QmTextUnbufferedPasses, TransparentFillOnlySubmitsTheOutline)
{
	Prepare(9000);
	ASSERT_TRUE(m_Container.Valid());
	Text().RenderTextContainer(m_Container, ColorRGBA(1, 1, 1, 0), ColorRGBA(0, 0, 0, 1));
	EXPECT_EQ(Graphics().m_vLegacyDraws.size(), 9000u);
	EXPECT_EQ(Graphics().m_LegacyKeepCalls, 0);
	EXPECT_EQ(Graphics().m_LegacyReplayCalls, 0);
	EXPECT_TRUE(Graphics().m_vLegacyRecolors.empty());
}

TEST_F(QmTextUnbufferedPasses, OutlineSubmissionFailurePropagatesBeforeTheFillStarts)
{
	Prepare(9000);
	ASSERT_TRUE(m_Container.Valid());
	Graphics().m_FailLegacyDrawAfter = 1;
	EXPECT_THROW(Text().RenderTextContainer(m_Container, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1)), std::bad_alloc);
	EXPECT_EQ(Graphics().m_vLegacyDraws.size(), 1u);
	EXPECT_EQ(Graphics().m_LegacyReplayCalls, 0);
	EXPECT_TRUE(Graphics().m_vLegacyRecolors.empty());
}
