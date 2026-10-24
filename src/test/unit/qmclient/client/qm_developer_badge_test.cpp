#include <game/client/components/qmclient/qm_developer_badge.h>

#include <gtest/gtest.h>

TEST(QmDeveloperBadge, ScalingPreservesDesignAspectRatio)
{
	const vec2 Small = QmDeveloperBadgeSize(7.0f);
	const vec2 Large = QmDeveloperBadgeSize(21.0f);
	EXPECT_FLOAT_EQ(Small.x, 7.0f * (91.0f / 24.0f));
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
	EXPECT_FLOAT_EQ(Rect.w, 7.0f * (91.0f / 24.0f));
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
	EXPECT_NEAR(Rect.w / Rect.h, 91.0f / 24.0f, 0.0001f);
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

TEST(QmDeveloperBadgeMotion, SixSecondLoopPreservesEveryLightPosition)
{
	for(const double Time : {0.0, 0.37, 1.5, 3.0, 5.999})
	{
		SCOPED_TRACE(Time);
		const auto First = QmSampleDeveloperBadgeMotion(Time);
		const auto Repeated = QmSampleDeveloperBadgeMotion(Time + 6.0);
		for(size_t i = 0; i < First.m_aOrbit.size(); ++i)
			EXPECT_NEAR(distance(First.m_aOrbit[i], Repeated.m_aOrbit[i]), 0.0f, 0.0001f);
		for(size_t i = 0; i < First.m_aCircuit.size(); ++i)
			EXPECT_NEAR(distance(First.m_aCircuit[i], Repeated.m_aCircuit[i]), 0.0f, 0.0001f);
	}
}

TEST(QmDeveloperBadgeMotion, OrbitTraversesBothEdgesAndRoundedEnds)
{
	const auto Start = QmSampleDeveloperBadgeMotion(0.0).m_aOrbit[0];
	EXPECT_NEAR(Start.x, 30.75f, 0.0001f);
	EXPECT_NEAR(Start.y, 0.5f, 0.0001f);
	const auto Bottom = QmSampleDeveloperBadgeMotion(3.0).m_aOrbit[0];
	EXPECT_NEAR(Bottom.x, 60.25f, 0.0001f);
	EXPECT_NEAR(Bottom.y, 23.5f, 0.0001f);
	const auto Right = QmSampleDeveloperBadgeMotion(2.0).m_aOrbit[0];
	EXPECT_GT(Right.x, 89.0f);
	EXPECT_GT(Right.y, 10.0f);
	EXPECT_LT(Right.y, 17.0f);
	const auto Left = QmSampleDeveloperBadgeMotion(5.0).m_aOrbit[0];
	EXPECT_LT(Left.x, 2.0f);
	EXPECT_GT(Left.y, 7.0f);
	EXPECT_LT(Left.y, 14.0f);
}

TEST(QmDeveloperBadgeMotion, CircuitLightsMoveInOppositeDirections)
{
	const auto Start = QmSampleDeveloperBadgeMotion(0.0);
	const auto Moving = QmSampleDeveloperBadgeMotion(1.0);
	EXPECT_GT(Moving.m_aCircuit[0].x, Start.m_aCircuit[0].x);
	EXPECT_LT(Moving.m_aCircuit[1].x, Start.m_aCircuit[1].x);
	EXPECT_FLOAT_EQ(Moving.m_aCircuit[0].y, 5.0f);
	EXPECT_FLOAT_EQ(Moving.m_aCircuit[1].y, 19.0f);
	const auto Returning = QmSampleDeveloperBadgeMotion(4.0);
	EXPECT_LT(Returning.m_aCircuit[0].x, Moving.m_aCircuit[0].x);
	EXPECT_GT(Returning.m_aCircuit[1].x, Moving.m_aCircuit[1].x);
}

TEST(QmDeveloperBadgeMotion, LongRunningClockAndLoopBoundaryRemainContinuous)
{
	const auto Local = QmSampleDeveloperBadgeMotion(0.5);
	const auto Later = QmSampleDeveloperBadgeMotion(3600000.5);
	EXPECT_NEAR(distance(Local.m_aOrbit[0], Later.m_aOrbit[0]), 0.0f, 0.0001f);
	EXPECT_NEAR(distance(Local.m_aCircuit[0], Later.m_aCircuit[0]), 0.0f, 0.0001f);
	const auto Before = QmSampleDeveloperBadgeMotion(-0.0001);
	const auto After = QmSampleDeveloperBadgeMotion(0.0001);
	for(size_t i = 0; i < Before.m_aOrbit.size(); ++i)
		EXPECT_LT(distance(Before.m_aOrbit[i], After.m_aOrbit[i]), 0.01f);
}
