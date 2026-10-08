#include <game/client/QmUi/QmPieMenuRender.h>

#include <gtest/gtest.h>

namespace
{
	using namespace qm_pie_menu_ui;

	float Cross(vec2 A, vec2 B)
	{
		return A.x * B.y - A.y * B.x;
	}

	TEST(PieMenuGeometry, ArcChordErrorStaysSubpixelAcrossMenuSizesAndOptionCounts)
	{
		for(float Scale : {0.5f, 1.0f, 2.0f})
		{
			for(int Count : {1, 2, 6, 11, 64})
			{
				SCOPED_TRACE(Scale);
				SCOPED_TRACE(Count);
				const float Angle = 360.0f / Count;
				const float Gap = Count == 1 ? 0.0f : std::min(3.6f, Angle * 0.35f);
				const float Radius = 288.0f * Scale;
				const auto Geometry = BuildSector(108.0f * Scale, Radius, -90.0f + Gap * 0.5f, -90.0f + Angle - Gap * 0.5f, Gap, 1.0f);
				ASSERT_GT(Geometry.m_ArcSegments, 0);
				ASSERT_LE(Geometry.m_ArcSegments, MAX_ARC_SEGMENTS);
				for(int Segment = Geometry.m_CornerSegments; Segment < Geometry.m_ArcSegments - Geometry.m_CornerSegments; ++Segment)
				{
					const auto aQuad = Geometry.FillQuad(Segment);
					EXPECT_LE(Radius - length((aQuad[0] + aQuad[1]) * 0.5f), ARC_ERROR_PIXELS + 0.0001f);
				}
			}
		}
	}

	TEST(PieMenuGeometry, RoundedCornersExcludeOldSharpPointsWhileKeepingSectorBody)
	{
		const auto Geometry = BuildSector(100, 200, 0, 90, 3.6f, 1);
		ASSERT_GT(Geometry.m_CornerSegments, 0);
		EXPECT_FALSE(Geometry.Contains(vec2(199.8f, 0.2f)));
		EXPECT_FALSE(Geometry.Contains(vec2(100.2f, 0.2f)));
		EXPECT_FALSE(Geometry.Contains(vec2(0.2f, 199.8f)));
		EXPECT_FALSE(Geometry.Contains(vec2(0.2f, 100.2f)));
		EXPECT_TRUE(Geometry.Contains(vec2(110, 110)));
		EXPECT_TRUE(Geometry.Contains(vec2(150, 1)));
		EXPECT_FALSE(Geometry.Contains(vec2(50, 50)));
	}

	TEST(PieMenuGeometry, PointerSelectionRejectsGapsAndOutsideRingsAtEveryScale)
	{
		for(float Scale : {0.5f, 1.0f, 2.0f})
		{
			SCOPED_TRACE(Scale);
			const auto At = [Scale](float Angle, float Radius) { return vec2(std::cos(Angle * pi / 180), std::sin(Angle * pi / 180)) * Radius * Scale; };
			EXPECT_EQ(HoveredSector(At(22.5f, 150), 100 * Scale, 200 * Scale, 0, 8, 3.6f, 1), 0);
			EXPECT_EQ(HoveredSector(At(67.5f, 150), 100 * Scale, 200 * Scale, 0, 8, 3.6f, 1), 1);
			EXPECT_EQ(HoveredSector(At(45, 150), 100 * Scale, 200 * Scale, 0, 8, 3.6f, 1), -1);
			EXPECT_EQ(HoveredSector(At(22.5f, 90), 100 * Scale, 200 * Scale, 0, 8, 3.6f, 1), -1);
			EXPECT_EQ(HoveredSector(At(22.5f, 210), 100 * Scale, 200 * Scale, 0, 8, 3.6f, 1), -1);
		}
	}

	TEST(PieMenuGeometry, SingleOptionHasNoCutoutSeamAndEmptyMenuCannotBeSelected)
	{
		EXPECT_EQ(HoveredSector(vec2(150, 0), 100, 200, 0, 1, 3.6f, 1), 0);
		EXPECT_EQ(HoveredSector(vec2(-150, 0), 100, 200, 0, 1, 3.6f, 1), 0);
		EXPECT_EQ(HoveredSector(vec2(0, 0), 100, 200, 0, 1, 3.6f, 1), -1);
		EXPECT_EQ(HoveredSector(vec2(150, 0), 100, 200, 0, 0, 3.6f, 1), -1);
	}

	TEST(PieMenuGeometry, OpeningSelectionFollowsVisibleRingAndLeavesUnopenedAreaEmpty)
	{
		const auto Layout = ResolvePrimaryRing(108, 288, 0.5f);
		const auto At = [](float Angle, float Radius) { return vec2(std::cos(Angle * pi / 180), std::sin(Angle * pi / 180)) * Radius; };
		const float Start = -90.0f + Layout.m_AngleOffset;
		const float MiddleRadius = (Layout.m_InnerRadius + Layout.m_OuterRadius) * 0.5f;
		EXPECT_EQ(HoveredSector(At(Start + 12, MiddleRadius), Layout.m_InnerRadius, Layout.m_OuterRadius, Start, 8, 3.6f, 1, Layout.m_SpanFactor), 0);
		EXPECT_EQ(HoveredSector(At(Start + 39, MiddleRadius), Layout.m_InnerRadius, Layout.m_OuterRadius, Start, 8, 3.6f, 1, Layout.m_SpanFactor), -1);
		EXPECT_EQ(HoveredSector(At(Start + 12, 250), Layout.m_InnerRadius, Layout.m_OuterRadius, Start, 8, 3.6f, 1, Layout.m_SpanFactor), -1);
	}

	TEST(PieMenuGeometry, OpeningSecondaryRingKeepsItsVisibleBoundarySeparateFromPrimary)
	{
		for(float Progress : {0.0f, 0.1f, 0.5f, 1.0f})
		{
			SCOPED_TRACE(Progress);
			const auto Primary = ResolvePrimaryRing(108, 288, Progress);
			const auto Secondary = ResolveSecondaryRing(Primary.m_OuterRadius, 300, 396, 1, Progress);
			EXPECT_GE(Secondary.m_InnerRadius, Primary.m_OuterRadius);
			EXPECT_GT(Secondary.m_OuterRadius, Secondary.m_InnerRadius);
			const float Angle = (-90.0f + Secondary.m_AngleOffset + 10.0f) * pi / 180.0f;
			const vec2 Pointer = vec2(std::cos(Angle), std::sin(Angle)) * ((Secondary.m_InnerRadius + Secondary.m_OuterRadius) * 0.5f);
			EXPECT_EQ(HoveredSector(Pointer, Secondary.m_InnerRadius, Secondary.m_OuterRadius, -90.0f + Secondary.m_AngleOffset, 8, 3.6f, 1, Secondary.m_SpanFactor), 0);
		}
	}

	TEST(PieMenuGeometry, FullRingClosesExactlyWithoutARadialFeatherSeam)
	{
		const auto Geometry = BuildSector(108.0f, 288.0f, -90.0f, 270.0f, 0.0f, 1.0f);
		ASSERT_TRUE(Geometry.m_FullRing);
		ASSERT_GT(Geometry.m_ArcSegments, 0);
		const auto aFirst = Geometry.FillQuad(0);
		const auto aLast = Geometry.FillQuad(Geometry.m_ArcSegments - 1);
		EXPECT_EQ(aFirst[0], aLast[1]);
		EXPECT_EQ(aFirst[2], aLast[3]);
		EXPECT_FALSE(Geometry.HasEdge(Geometry.m_ArcSegments));
		EXPECT_FALSE(Geometry.HasEdge(Geometry.m_PointCount - 1));

		int EdgeCount = 0;
		for(int Index = 0; Index < Geometry.m_PointCount; ++Index)
		{
			if(!Geometry.HasEdge(Index))
				continue;
			++EdgeCount;
			const auto &Point = Geometry.m_aPoints[Index];
			if(Index < Geometry.m_ArcSegments)
				EXPECT_GT(length(Point.m_Fringe), 288.0f);
			else
				EXPECT_LT(length(Point.m_Fringe), 108.0f);
		}
		EXPECT_EQ(EdgeCount, 2 * Geometry.m_ArcSegments);
	}

	TEST(PieMenuGeometry, EveryArcAndRadialEdgeHasAnOutwardConnectedFeather)
	{
		const auto Geometry = BuildSector(108.0f, 288.0f, -88.2f, -55.8f, 3.6f, 1.0f);
		ASSERT_GT(Geometry.m_PointCount, 0);
		EXPECT_FLOAT_EQ(Geometry.m_Feather, FEATHER_PIXELS);
		for(int Index = 0; Index < Geometry.m_PointCount; ++Index)
		{
			SCOPED_TRACE(Index);
			const auto &Point = Geometry.m_aPoints[Index];
			const auto &Next = Geometry.m_aPoints[(Index + 1) % Geometry.m_PointCount];
			const vec2 Edge = normalize(Next.m_Position - Point.m_Position);
			EXPECT_NEAR(Cross(Edge, Point.m_Fringe - Point.m_Position), -FEATHER_PIXELS, 0.0001f);
			EXPECT_NEAR(Cross(Edge, Next.m_Fringe - Next.m_Position), -FEATHER_PIXELS, 0.0001f);
			EXPECT_LE(length(Point.m_Fringe - Point.m_Position), 2.0f * Geometry.m_Feather + 0.0001f);
		}
	}

	TEST(PieMenuGeometry, DenseRenameFringesStayInsideTheirOwnAngularSlot)
	{
		for(int Count : {16, 64, 128, 256})
		{
			for(float Scale : {0.5f, 1.0f, 2.0f})
			{
				SCOPED_TRACE(Count);
				SCOPED_TRACE(Scale);
				const float Angle = 360.0f / Count;
				const float Gap = std::min(3.6f, Angle * 0.35f);
				const auto Geometry = BuildSector(300.0f * Scale, 396.0f * Scale, Gap * 0.5f, Angle - Gap * 0.5f, Gap, 1.0f);
				const vec2 SlotStart(1.0f, 0.0f);
				const vec2 SlotEnd(std::cos(Angle * pi / 180.0f), std::sin(Angle * pi / 180.0f));
				ASSERT_GT(Geometry.m_PointCount, 0);
				for(int Index = 0; Index < Geometry.m_PointCount; ++Index)
				{
					const vec2 Fringe = Geometry.m_aPoints[Index].m_Fringe;
					EXPECT_GE(Cross(SlotStart, Fringe), 0.0f);
					EXPECT_LE(Cross(SlotEnd, Fringe), 0.0f);
				}
			}
		}
	}

	TEST(PieMenuGeometry, PreviewAndGameplayProjectionKeepTheSamePhysicalFeather)
	{
		const auto Gameplay = BuildSector(108.0f, 288.0f, -88.2f, -55.8f, 3.6f, 1.0f);
		for(float MappingScale : {0.25f, 0.5f, 1.0f})
		{
			SCOPED_TRACE(MappingScale);
			const float Pixel = UnitsPerPixel(vec2(1920.0f, 1080.0f) * MappingScale, vec2(1920.0f, 1080.0f));
			const auto Preview = BuildSector(108.0f * MappingScale, 288.0f * MappingScale, -88.2f, -55.8f, 3.6f, Pixel);
			ASSERT_EQ(Preview.m_PointCount, Gameplay.m_PointCount);
			EXPECT_FLOAT_EQ(Preview.m_Feather / Pixel, FEATHER_PIXELS);
			for(int Index = 0; Index < Preview.m_PointCount; ++Index)
			{
				EXPECT_NEAR(length(Preview.m_aPoints[Index].m_Position / Pixel - Gameplay.m_aPoints[Index].m_Position), 0.0f, 0.0001f);
				EXPECT_NEAR(length(Preview.m_aPoints[Index].m_Fringe / Pixel - Gameplay.m_aPoints[Index].m_Fringe), 0.0f, 0.0001f);
			}
		}
	}

	TEST(PieMenuGeometry, CenterDiscUsesAClosedSmoothBoundaryAndSingleCoverageFans)
	{
		for(float Radius : {49.5f, 99.0f, 198.0f})
		{
			SCOPED_TRACE(Radius);
			const auto Geometry = BuildDisc(Radius, 1.0f);
			ASSERT_TRUE(Geometry.m_Disc);
			ASSERT_EQ(Geometry.m_ArcSegments, Geometry.m_PointCount);
			for(int Index = 0; Index < Geometry.m_ArcSegments; ++Index)
			{
				const auto aQuad = Geometry.FillQuad(Index);
				EXPECT_EQ(aQuad[2], vec2(0.0f, 0.0f));
				EXPECT_EQ(aQuad[3], vec2(0.0f, 0.0f));
				EXPECT_GT(Cross(aQuad[1] - aQuad[0], aQuad[3] - aQuad[0]), 0.0f);
				EXPECT_LE(Radius - length((aQuad[0] + aQuad[1]) * 0.5f), ARC_ERROR_PIXELS + 0.0001f);
				EXPECT_GT(length(Geometry.m_aPoints[Index].m_Fringe), Radius);
			}
			EXPECT_EQ(Geometry.FillQuad(Geometry.m_ArcSegments - 1)[1], Geometry.FillQuad(0)[0]);
		}
	}

	TEST(PieMenuGeometry, EmptyDimensionsDoNotProduceDegenerateShapes)
	{
		EXPECT_EQ(BuildSector(108.0f, 108.0f, 0.0f, 36.0f, 3.6f, 1.0f).m_PointCount, 0);
		EXPECT_EQ(BuildSector(108.0f, 288.0f, 0.0f, 0.0f, 3.6f, 1.0f).m_PointCount, 0);
		EXPECT_EQ(BuildSector(108.0f, 288.0f, 0.0f, 36.0f, 3.6f, 0.0f).m_PointCount, 0);
		EXPECT_EQ(BuildDisc(0.0f, 1.0f).m_PointCount, 0);
		EXPECT_EQ(BuildDisc(99.0f, 0.0f).m_PointCount, 0);
		EXPECT_FLOAT_EQ(UnitsPerPixel(vec2(600.0f, 600.0f), vec2(0.0f, 0.0f)), 0.0f);
	}

	TEST(PieMenuAppearance, HoverKeepsTransparentUserColorsAndGlobalOpacity)
	{
		const ColorRGBA Transparent(0.4f, 0.8f, 0.6f, 0.0f);
		EXPECT_FLOAT_EQ(OptionColor(Transparent, false).a, 0.0f);
		EXPECT_FLOAT_EQ(OptionColor(Transparent, true).a, 0.0f);
		const ColorRGBA Configured(0.4f, 0.8f, 0.6f, 0.75f);
		const ColorRGBA Highlighted = OptionColor(Configured, true);
		EXPECT_EQ(OptionColor(Configured, false), Configured);
		EXPECT_GT(Highlighted.r, Configured.r);
		EXPECT_LE(Highlighted.g, 1.0f);
		EXPECT_FLOAT_EQ(Highlighted.WithMultipliedAlpha(0.0f).a, 0.0f);
		EXPECT_FLOAT_EQ(Highlighted.WithMultipliedAlpha(0.5f).a, Highlighted.a * 0.5f);
	}

	TEST(PieMenuAppearance, HighlightTintOnlyChangesSelectedRgb)
	{
		const ColorRGBA Base(0.4f, 0.8f, 0.6f, 0.5f);
		const ColorRGBA Tint(1.0f, 0.1f, 0.2f, 1.0f);
		EXPECT_EQ(OptionColor(Base, false, Tint), Base);
		const ColorRGBA Selected = OptionColor(Base, true, Tint);
		EXPECT_FLOAT_EQ(Selected.r, Tint.r);
		EXPECT_FLOAT_EQ(Selected.g, Tint.g);
		EXPECT_FLOAT_EQ(Selected.b, Tint.b);
		EXPECT_FLOAT_EQ(Selected.a, OptionColor(Base, true).a);
		EXPECT_EQ(OptionColor(Base, true, Tint.WithAlpha(0.0f)), OptionColor(Base, true));
	}
}
