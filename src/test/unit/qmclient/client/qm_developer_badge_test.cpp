#include <game/client/components/qmclient/qm_developer_badge.h>

#include <gtest/gtest.h>

TEST(QmDeveloperBadge, ScalingPreservesDesignAspectRatio)
{
	const vec2 Small = QmDeveloperBadgeSize(7.0f);
	const vec2 Large = QmDeveloperBadgeSize(21.0f);
	EXPECT_FLOAT_EQ(Small.x, 24.0f);
	EXPECT_FLOAT_EQ(Small.y, 7.0f);
	EXPECT_FLOAT_EQ(Large.x, Small.x * 3.0f);
	EXPECT_FLOAT_EQ(Large.y, Small.y * 3.0f);
}

TEST(QmDeveloperBadge, InlinePlacementReservesSpaceBeforeNickname)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_X = 15.0f;
	Cursor.m_FontSize = 7.0f;
	const CUIRect Rect = QmLayoutDeveloperBadge(Cursor);
	EXPECT_FLOAT_EQ(Rect.x, 15.0f);
	EXPECT_FLOAT_EQ(Rect.y, 20.0f);
	EXPECT_GT(Cursor.m_X, Rect.x + Rect.w);
	EXPECT_FLOAT_EQ(Cursor.m_LongestLineWidth, Cursor.m_X - 10.0f);
	EXPECT_FLOAT_EQ(Cursor.m_MaxCharacterHeight, Rect.h);
	EXPECT_EQ(Cursor.m_LineCount, 1);
}

TEST(QmDeveloperBadge, InsufficientRemainingWidthMovesWholeBadgeToNextLine)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(10.0f, 20.0f));
	Cursor.m_X = 95.0f;
	Cursor.m_LineWidth = 90.0f;
	Cursor.m_FontSize = 7.0f;
	Cursor.m_AlignedFontSize = 8.0f;
	Cursor.m_AlignedLineSpacing = 2.0f;
	const CUIRect Rect = QmLayoutDeveloperBadge(Cursor);
	EXPECT_EQ(Cursor.m_LineCount, 2);
	EXPECT_FLOAT_EQ(Cursor.m_Y, 30.0f);
	EXPECT_FLOAT_EQ(Rect.x, 10.0f);
	EXPECT_FLOAT_EQ(Rect.y, 30.5f);
	EXPECT_FLOAT_EQ(Rect.w, 24.0f);
	EXPECT_FLOAT_EQ(Cursor.m_LongestLineWidth, 85.0f);
}

TEST(QmDeveloperBadge, ExactFitKeepsCurrentLine)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = 7.0f;
	Cursor.m_X = 10.0f;
	Cursor.m_LineWidth = 10.0f + QmDeveloperBadgeAdvance(7.0f);
	const CUIRect Rect = QmLayoutDeveloperBadge(Cursor);
	EXPECT_EQ(Cursor.m_LineCount, 1);
	EXPECT_FLOAT_EQ(Rect.x, 10.0f);
	EXPECT_FLOAT_EQ(Cursor.m_X, Cursor.m_LineWidth);
}

TEST(QmDeveloperBadge, NarrowLineFitsBadgeWithoutChangingAspectRatio)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(5.0f, 2.0f));
	Cursor.m_FontSize = 14.0f;
	Cursor.m_LineWidth = 15.0f;
	const CUIRect Rect = QmLayoutDeveloperBadge(Cursor);
	EXPECT_LE(Rect.x + Rect.w, 20.0f);
	EXPECT_FLOAT_EQ(Cursor.m_X, 20.0f);
	EXPECT_GT(Rect.h, 0.0f);
	EXPECT_LT(Rect.h, 14.0f);
	EXPECT_NEAR(Rect.w / Rect.h, 24.0f / 7.0f, 0.0001f);
	EXPECT_EQ(Cursor.m_LineCount, 1);
}

TEST(QmDeveloperBadge, ConsecutiveAuthorsDoNotOverlapAndWrapTogether)
{
	CTextCursor Cursor;
	Cursor.m_FontSize = 7.0f;
	Cursor.m_LineWidth = 60.0f;
	const CUIRect First = QmLayoutDeveloperBadge(Cursor);
	const CUIRect Second = QmLayoutDeveloperBadge(Cursor);
	const CUIRect Third = QmLayoutDeveloperBadge(Cursor);
	EXPECT_GT(Second.x, First.x + First.w);
	EXPECT_FLOAT_EQ(Second.y, First.y);
	EXPECT_FLOAT_EQ(Third.x, Cursor.m_StartX);
	EXPECT_GE(Third.y, First.y + First.h);
	EXPECT_EQ(Cursor.m_LineCount, 2);
}

TEST(QmDeveloperBadge, MeasurementAndRenderingUseIdenticalPlacement)
{
	CTextCursor Measure;
	Measure.m_Flags = 0;
	Measure.SetPosition(vec2(4.0f, 12.0f));
	Measure.m_X = 50.0f;
	Measure.m_FontSize = 9.5f;
	Measure.m_AlignedFontSize = 10.0f;
	Measure.m_LineWidth = 60.0f;
	CTextCursor Render = Measure;
	Render.m_Flags = TEXTFLAG_RENDER;
	const CUIRect Measured = QmLayoutDeveloperBadge(Measure);
	const CUIRect Rendered = QmLayoutDeveloperBadge(Render);
	EXPECT_FLOAT_EQ(Measured.x, Rendered.x);
	EXPECT_FLOAT_EQ(Measured.y, Rendered.y);
	EXPECT_FLOAT_EQ(Measured.w, Rendered.w);
	EXPECT_FLOAT_EQ(Measured.h, Rendered.h);
	EXPECT_FLOAT_EQ(Measure.m_X, Render.m_X);
	EXPECT_FLOAT_EQ(Measure.m_Y, Render.m_Y);
	EXPECT_EQ(Measure.m_LineCount, Render.m_LineCount);
	EXPECT_FLOAT_EQ(Measure.m_LongestLineWidth, Render.m_LongestLineWidth);
	EXPECT_FLOAT_EQ(Render.m_X - Render.m_StartX, QmDeveloperBadgeAdvance(9.5f));
}

TEST(QmDeveloperBadge, NonpositiveFontSizeDoesNotMoveCursor)
{
	for(const float FontSize : {0.0f, -5.0f})
	{
		SCOPED_TRACE(FontSize);
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(10.0f, 20.0f));
		Cursor.m_FontSize = FontSize;
		const CUIRect Rect = QmLayoutDeveloperBadge(Cursor);
		EXPECT_FLOAT_EQ(Rect.w, 0.0f);
		EXPECT_FLOAT_EQ(Rect.h, 0.0f);
		EXPECT_FLOAT_EQ(Cursor.m_X, 10.0f);
		EXPECT_FLOAT_EQ(Cursor.m_Y, 20.0f);
		EXPECT_EQ(Cursor.m_LineCount, 1);
	}
}
