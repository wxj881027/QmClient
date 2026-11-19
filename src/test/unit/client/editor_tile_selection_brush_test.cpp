#include <game/editor/mapitems/layer_tiles.h>

#include <gtest/gtest.h>

namespace
{
	class CBrushLayer
	{
	public:
		virtual ~CBrushLayer() = default;
	};

	class CTileBrush : public CBrushLayer
	{
	};

	class CSpecialTileBrush : public CTileBrush
	{
	};

	class COtherSpecialTileBrush : public CTileBrush
	{
	};

	class CQuadBrush : public CBrushLayer
	{
	};
}

TEST(EditorTileSelectionBrush, NonemptySelectionWithoutAMatchingBrushDoesNotPaint)
{
	const CBrushLayer *pMissing = nullptr;
	const CQuadBrush Brush;
	const CTileBrush *pTiles;
	EXPECT_FALSE(CLayerTiles::ResolveFillBrush(false, pMissing, pTiles));
	EXPECT_EQ(pTiles, nullptr);
	EXPECT_FALSE(CLayerTiles::ResolveFillBrush(false, &Brush, pTiles));
	EXPECT_EQ(pTiles, nullptr);
}

TEST(EditorTileSelectionBrush, PhysicsLayersWithSeparateDataRequireTheirOwnBrushType)
{
	const CTileBrush BasicBrush;
	const COtherSpecialTileBrush OtherBrush;
	const CSpecialTileBrush MatchingBrush;
	const CSpecialTileBrush *pTiles;
	EXPECT_FALSE(CLayerTiles::ResolveFillBrush(false, &BasicBrush, pTiles));
	EXPECT_FALSE(CLayerTiles::ResolveFillBrush(false, &OtherBrush, pTiles));
	EXPECT_TRUE(CLayerTiles::ResolveFillBrush(false, &MatchingBrush, pTiles));
	EXPECT_EQ(pTiles, &MatchingBrush);
}

TEST(EditorTileSelectionBrush, BasicTileFillKeepsTheExistingFallbackToATileBrush)
{
	const CSpecialTileBrush Brush;
	const CBrushLayer *pBrush = &Brush;
	const CTileBrush *pTiles;
	EXPECT_TRUE(CLayerTiles::ResolveFillBrush(false, pBrush, pTiles));
	EXPECT_EQ(pTiles, &Brush);
}

TEST(EditorTileSelectionBrush, EmptyBrushStillErasesWithoutALayer)
{
	const CBrushLayer *pMissing = nullptr;
	const CSpecialTileBrush *pTiles;
	EXPECT_TRUE(CLayerTiles::ResolveFillBrush(true, pMissing, pTiles));
	EXPECT_EQ(pTiles, nullptr);
}
