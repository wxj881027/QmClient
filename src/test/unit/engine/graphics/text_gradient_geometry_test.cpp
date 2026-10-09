#include <engine/client/text_gradient.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
	ColorRGBA SamplePosition(vec2 Position, const void *pContext)
	{
		const float Alpha = *static_cast<const float *>(pContext);
		return ColorRGBA(Position.x / 16.0f, Position.y / 8.0f, 0.0f, Alpha);
	}
}

TEST(TextGradientGeometry, CellsCoverTheGlyphOnceAndKeepTheTextureCoordinates)
{
	const float Alpha = 0.4f;
	int Count = 0;
	float Area = 0.0f;
	ForEachTextGradientCell(vec2(0.0f, 0.0f), vec2(16.0f, 8.0f), vec2(100.0f, 200.0f), vec2(116.0f, 208.0f),
		SamplePosition, &Alpha, [&](const STextGradientCell &Cell) {
			++Count;
			Area += (Cell.m_aPositions[1].x - Cell.m_aPositions[0].x) * (Cell.m_aPositions[2].y - Cell.m_aPositions[0].y);
			for(int i = 0; i < 4; ++i)
			{
				EXPECT_GE(Cell.m_aPositions[i].x, 0.0f);
				EXPECT_LE(Cell.m_aPositions[i].x, 16.0f);
				EXPECT_GE(Cell.m_aPositions[i].y, 0.0f);
				EXPECT_LE(Cell.m_aPositions[i].y, 8.0f);
				EXPECT_FLOAT_EQ(Cell.m_aUvs[i].x, Cell.m_aPositions[i].x + 100.0f);
				EXPECT_FLOAT_EQ(Cell.m_aUvs[i].y, Cell.m_aPositions[i].y + 200.0f);
				EXPECT_FLOAT_EQ(Cell.m_aColors[i].a, Alpha);
			}
		});
	EXPECT_EQ(Count, 64);
	EXPECT_FLOAT_EQ(Area, 128.0f);
}

TEST(TextGradientGeometry, SamplingIncludesTheCenterAndBothVerticalEdges)
{
	const float Alpha = 1.0f;
	bool CenterSampled = false;
	float MinGreen = 1.0f, MaxGreen = 0.0f;
	ForEachTextGradientCell(vec2(0.0f, 0.0f), vec2(16.0f, 8.0f), vec2(0.0f, 0.0f), vec2(1.0f, 1.0f),
		SamplePosition, &Alpha, [&](const STextGradientCell &Cell) {
			for(int i = 0; i < 4; ++i)
			{
				CenterSampled |= Cell.m_aPositions[i] == vec2(8.0f, 4.0f);
				MinGreen = std::min(MinGreen, Cell.m_aColors[i].g);
				MaxGreen = std::max(MaxGreen, Cell.m_aColors[i].g);
			}
		});
	EXPECT_TRUE(CenterSampled);
	EXPECT_FLOAT_EQ(MinGreen, 0.0f);
	EXPECT_FLOAT_EQ(MaxGreen, 1.0f);
}

TEST(TextGradientGeometry, AdjacentCellsShareTheirGeometryAndColorEdges)
{
	const float Alpha = 1.0f;
	int Column = 0;
	STextGradientCell Previous;
	ForEachTextGradientCell(vec2(0.0f, 0.0f), vec2(16.0f, 8.0f), vec2(0.0f, 0.0f), vec2(1.0f, 1.0f),
		SamplePosition, &Alpha, [&](const STextGradientCell &Cell) {
			if(Column % 8 != 0)
			{
				EXPECT_EQ(Previous.m_aPositions[1], Cell.m_aPositions[0]);
				EXPECT_EQ(Previous.m_aPositions[3], Cell.m_aPositions[2]);
				EXPECT_FLOAT_EQ(Previous.m_aColors[1].r, Cell.m_aColors[0].r);
				EXPECT_FLOAT_EQ(Previous.m_aColors[3].g, Cell.m_aColors[2].g);
			}
			Previous = Cell;
			++Column;
		});
}
