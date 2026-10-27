#include <game/client/QmUi/QmBadgeGeometry.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmBadgeGeometry, EveryMeshContainsFiniteGeometryWithinItsDesignBounds)
{
	for(const auto Kind : {EQmBadgeGeometry::DEVELOPER, EQmBadgeGeometry::LOGO, EQmBadgeGeometry::MOON_LEFT, EQmBadgeGeometry::MOON_MIDDLE, EQmBadgeGeometry::MOON_RIGHT, EQmBadgeGeometry::SUN_LEFT, EQmBadgeGeometry::SUN_MIDDLE, EQmBadgeGeometry::SUN_RIGHT, EQmBadgeGeometry::DEVELOPER_LETTERING})
	{
		SCOPED_TRACE(static_cast<int>(Kind));
		const auto Mesh = QmBadgeGeometryMesh(Kind);
		ASSERT_GT(Mesh.m_Count, 0u);
		for(size_t Index = 0; Index < Mesh.m_Count; ++Index)
		{
			const auto &Item = Mesh.m_pQuads[Index];
			const auto &Q = Item.m_Quad;
			for(const float X : {Q.m_X0, Q.m_X1, Q.m_X2, Q.m_X3})
			{
				EXPECT_TRUE(std::isfinite(X));
				EXPECT_GE(X, -0.0001f);
				EXPECT_LE(X, Mesh.m_Size.x + 0.0001f);
			}
			for(const float Y : {Q.m_Y0, Q.m_Y1, Q.m_Y2, Q.m_Y3})
			{
				EXPECT_TRUE(std::isfinite(Y));
				EXPECT_GE(Y, -0.0001f);
				EXPECT_LE(Y, Mesh.m_Size.y + 0.0001f);
			}
			EXPECT_GT(Item.m_Alpha, 0.0f);
			EXPECT_LE(Item.m_Alpha, 1.0f);
		}
	}
}

TEST(QmBadgeGeometry, PatternSlicesReconstructTheOriginalWidth)
{
	const auto Left = QmBadgeGeometryMesh(EQmBadgeGeometry::MOON_LEFT);
	const auto Middle = QmBadgeGeometryMesh(EQmBadgeGeometry::MOON_MIDDLE);
	const auto Right = QmBadgeGeometryMesh(EQmBadgeGeometry::MOON_RIGHT);
	EXPECT_FLOAT_EQ(Left.m_Size.x + Middle.m_Size.x + Right.m_Size.x, 117.0f);
	EXPECT_FLOAT_EQ(Left.m_Size.y, 24.0f);
	EXPECT_FLOAT_EQ(Middle.m_Size.y, Left.m_Size.y);
	EXPECT_FLOAT_EQ(Right.m_Size.y, Left.m_Size.y);
}

TEST(QmBadgeGeometry, ShortTitleClippingKeepsTheOriginalCoordinatesAndArea)
{
	const auto Result = QmClipBadgeGeometry({0, 0, 4, 0, 0, 3, 4, 3}, 2.0f);
	ASSERT_EQ(Result.m_Count, 2);
	float Area = 0.0f;
	for(int Index = 0; Index < Result.m_Count; ++Index)
	{
		const auto &Q = Result.m_aQuads[Index];
		EXPECT_LE(std::max({Q.m_X0, Q.m_X1, Q.m_X2, Q.m_X3}), 2.0f);
		Area += std::abs((Q.m_X1 - Q.m_X0) * (Q.m_Y2 - Q.m_Y0) - (Q.m_X2 - Q.m_X0) * (Q.m_Y1 - Q.m_Y0)) * 0.5f;
	}
	EXPECT_FLOAT_EQ(Area, 6.0f);
}

TEST(QmBadgeGeometry, FullyClippedDecorationDoesNotLeaveDegenerateTriangles)
{
	const auto Result = QmClipBadgeGeometry({2, 0, 4, 0, 2, 3, 4, 3}, 2.0f);
	EXPECT_EQ(Result.m_Count, 0);
}
