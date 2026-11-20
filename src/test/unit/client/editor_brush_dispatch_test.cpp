#include <game/editor/mapitems/layer.h>

#include <gtest/gtest.h>

namespace
{
	class CBrushTarget : public CLayer
	{
	public:
		explicit CBrushTarget(int Type) :
			CLayer(nullptr, Type)
		{
		}

		int m_Fills = 0;
		bool m_Erased = false;
		const CLayer *m_pPlacedBrush = nullptr;
		int m_PlaceLayerIndex = -1;

		void FillSelection(bool Empty, CLayer *pBrush, CUIRect Rect) override
		{
			++m_Fills;
			m_Erased = Empty;
		}

		void BrushPlace(const CLayer *pBrush, vec2 WorldPos, int LayerIndex) override
		{
			m_pPlacedBrush = pBrush;
			m_PlaceLayerIndex = LayerIndex;
		}

		std::shared_ptr<CLayer> Duplicate() const override { return std::make_shared<CBrushTarget>(*this); }
		const char *TypeName() const override { return "test"; }
	};
}

TEST(EditorBrushDispatch, FilledBrushWithoutMatchingLayerLeavesTargetAlone)
{
	CBrushTarget Target(LAYERTYPE_TILES);
	Target.FillWithBrush(false, nullptr, {0, 0, 64, 64});
	EXPECT_EQ(Target.m_Fills, 0);
}

TEST(EditorBrushDispatch, EmptyBrushStillErasesSelection)
{
	CBrushTarget Target(LAYERTYPE_TILES);
	Target.FillWithBrush(true, nullptr, {0, 0, 64, 64});
	EXPECT_EQ(Target.m_Fills, 1);
	EXPECT_TRUE(Target.m_Erased);
}

TEST(EditorBrushDispatch, MatchingBrushFillsWithoutErasingSelection)
{
	CBrushTarget Target(LAYERTYPE_TILES);
	CBrushTarget Brush(LAYERTYPE_TILES);
	Target.FillWithBrush(false, &Brush, {0, 0, 64, 64});
	EXPECT_EQ(Target.m_Fills, 1);
	EXPECT_FALSE(Target.m_Erased);
}

TEST(EditorBrushDispatch, EachPlacementReceivesItsDestinationLayerIndex)
{
	CBrushTarget Brush(LAYERTYPE_QUADS);
	CBrushTarget First(LAYERTYPE_QUADS);
	CBrushTarget Second(LAYERTYPE_QUADS);
	First.PlaceBrush(&Brush, vec2(32, 64), 4);
	Second.PlaceBrush(&Brush, vec2(32, 64), 1);
	EXPECT_EQ(First.m_pPlacedBrush, &Brush);
	EXPECT_EQ(Second.m_pPlacedBrush, &Brush);
	EXPECT_EQ(First.m_PlaceLayerIndex, 4);
	EXPECT_EQ(Second.m_PlaceLayerIndex, 1);
}

TEST(EditorBrushDispatch, PlacementSkipsAnUnrelatedBrushType)
{
	CBrushTarget Target(LAYERTYPE_SOUNDS);
	CBrushTarget Brush(LAYERTYPE_QUADS);
	Target.PlaceBrush(&Brush, vec2(32, 64), 1);
	EXPECT_EQ(Target.m_pPlacedBrush, nullptr);
}
