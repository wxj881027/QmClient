#include <game/editor/quad_slice.h>

#include <gtest/gtest.h>

namespace
{
	CQuad SourceQuad()
	{
		CQuad Quad{};
		Quad.m_aPoints[0] = {0, 0};
		Quad.m_aPoints[1] = {i2fx(100), 0};
		Quad.m_aPoints[2] = {0, i2fx(100)};
		Quad.m_aPoints[3] = {i2fx(100), i2fx(100)};
		Quad.m_aPoints[4] = {i2fx(50), i2fx(50)};
		Quad.m_aTexcoords[0] = {0, 0};
		Quad.m_aTexcoords[1] = {i2fx(1), 0};
		Quad.m_aTexcoords[2] = {0, i2fx(1)};
		Quad.m_aTexcoords[3] = {i2fx(1), i2fx(1)};
		std::fill(std::begin(Quad.m_aColors), std::end(Quad.m_aColors), CColor{255, 255, 255, 255});
		return Quad;
	}
}

TEST(EditorQuadSlice, RectangleKeepsSelectedTextureRegion)
{
	const CQuad Source = SourceQuad();
	CQuad Result{};
	ASSERT_TRUE(quad_slice::SliceRectangle(Source, {25, 25}, {75, 75}, Result));
	EXPECT_EQ(Result.m_aPoints[0], CPoint(i2fx(25), i2fx(25)));
	EXPECT_EQ(Result.m_aPoints[3], CPoint(i2fx(75), i2fx(75)));
	EXPECT_EQ(Result.m_aTexcoords[0], CPoint(f2fx(0.25f), f2fx(0.25f)));
	EXPECT_EQ(Result.m_aTexcoords[3], CPoint(f2fx(0.75f), f2fx(0.75f)));
	EXPECT_EQ(Source.m_aPoints[0], CPoint(0, 0));
	EXPECT_EQ(Source.m_aTexcoords[3], CPoint(i2fx(1), i2fx(1)));
}

TEST(EditorQuadSlice, AllDragDirectionsProduceSameRectangle)
{
	const std::array<std::pair<vec2, vec2>, 4> Directions{{{{25, 25}, {75, 75}}, {{75, 75}, {25, 25}}, {{25, 75}, {75, 25}}, {{75, 25}, {25, 75}}}};
	for(size_t i = 0; i < Directions.size(); ++i)
	{
		SCOPED_TRACE(i);
		CQuad Result{};
		ASSERT_TRUE(quad_slice::SliceRectangle(SourceQuad(), Directions[i].first, Directions[i].second, Result));
		EXPECT_EQ(Result.m_aPoints[0], CPoint(i2fx(25), i2fx(25)));
		EXPECT_EQ(Result.m_aPoints[1], CPoint(i2fx(75), i2fx(25)));
		EXPECT_EQ(Result.m_aPoints[2], CPoint(i2fx(25), i2fx(75)));
		EXPECT_EQ(Result.m_aPoints[3], CPoint(i2fx(75), i2fx(75)));
	}
}

TEST(EditorQuadSlice, OutsideRectangleDoesNotCommitPartialResult)
{
	CQuad Result{};
	Result.m_PosEnv = 7;
	EXPECT_FALSE(quad_slice::SliceRectangle(SourceQuad(), {25, 25}, {101, 75}, Result));
	EXPECT_EQ(Result.m_PosEnv, 7);
	EXPECT_EQ(Result.m_aPoints[3], CPoint(0, 0));
}

TEST(EditorQuadSlice, CompletingOtherCornersCannotCrossSkewedQuadBoundary)
{
	CQuad Source = SourceQuad();
	Source.m_aPoints[0] = {i2fx(50), 0};
	Source.m_aPoints[1] = {i2fx(100), i2fx(50)};
	Source.m_aPoints[2] = {0, i2fx(50)};
	Source.m_aPoints[3] = {i2fx(50), i2fx(100)};
	CQuad Result{};
	EXPECT_FALSE(quad_slice::SliceRectangle(Source, {50, 0}, {0, 50}, Result));
}

TEST(EditorQuadSlice, EmptyAndSubpixelRectanglesAreRejected)
{
	CQuad Result{};
	EXPECT_FALSE(quad_slice::SliceRectangle(SourceQuad(), {25, 25}, {25, 75}, Result));
	EXPECT_FALSE(quad_slice::SliceRectangle(SourceQuad(), {25, 25}, {75, 25}, Result));
	EXPECT_FALSE(quad_slice::SliceRectangle(SourceQuad(), {25, 25}, {25.00001f, 75}, Result));
}

TEST(EditorQuadSlice, RectangleEdgesCannotCrossConcaveQuadNotch)
{
	CQuad Source = SourceQuad();
	Source.m_aPoints[0] = {i2fx(5), i2fx(5)};
	Source.m_aPoints[1] = {0, 0};
	Source.m_aPoints[2] = {i2fx(10), 0};
	Source.m_aPoints[3] = {i2fx(5), i2fx(10)};
	CQuad Result{};
	EXPECT_FALSE(quad_slice::SliceRectangle(Source, {3, 4}, {7, 6}, Result));
}

TEST(EditorQuadSlice, FullBoundaryIsAcceptedAndDegenerateSourceIsRejected)
{
	CQuad Result{};
	ASSERT_TRUE(quad_slice::SliceRectangle(SourceQuad(), {0, 0}, {100, 100}, Result));
	EXPECT_EQ(Result.m_aTexcoords[3], CPoint(i2fx(1), i2fx(1)));
	CQuad Empty{};
	EXPECT_FALSE(quad_slice::SliceRectangle(Empty, {0, 0}, {10, 10}, Result));
}

TEST(EditorQuadSlice, CornerColorsAreInterpolated)
{
	CQuad Source = SourceQuad();
	Source.m_aColors[0] = {0, 0, 255, 255};
	Source.m_aColors[1] = {200, 0, 255, 255};
	Source.m_aColors[2] = {0, 200, 255, 255};
	Source.m_aColors[3] = {200, 200, 255, 255};
	CQuad Result{};
	ASSERT_TRUE(quad_slice::SliceRectangle(Source, {25, 25}, {75, 75}, Result));
	EXPECT_EQ(Result.m_aColors[0], CColor(50, 50, 255, 255));
	EXPECT_EQ(Result.m_aColors[3], CColor(150, 150, 255, 255));
}

TEST(EditorQuadSlice, CroppedQuadKeepsAnimationBindingsAndRotationPivot)
{
	CQuad Source = SourceQuad();
	Source.m_PosEnv = 3;
	Source.m_ColorEnv = 4;
	Source.m_PosEnvOffset = 500;
	Source.m_ColorEnvOffset = 750;
	Source.m_aPoints[4] = {i2fx(-10), i2fx(5)};
	CQuad Result{};
	ASSERT_TRUE(quad_slice::SliceRectangle(Source, {25, 25}, {75, 75}, Result));
	EXPECT_EQ(Result.m_PosEnv, 3);
	EXPECT_EQ(Result.m_ColorEnv, 4);
	EXPECT_EQ(Result.m_PosEnvOffset, 500);
	EXPECT_EQ(Result.m_ColorEnvOffset, 750);
	EXPECT_EQ(Result.m_aPoints[4], Source.m_aPoints[4]);
}
