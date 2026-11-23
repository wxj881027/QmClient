#include <game/editor/mapitems/map.h>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{
	// 编辑器选中状态的独立副本，便于按资源数量调用生产夹紧逻辑。
	struct SSelectionFixture
	{
		std::vector<int> m_vGroupLayerCounts{1, 1};
		size_t m_EnvelopeCount = 1;
		size_t m_ImageCount = 1;
		size_t m_SoundCount = 1;

		int m_SelectedGroup = 1;
		std::vector<int> m_vSelectedLayers{0};
		std::vector<int> m_vSelectedQuads{0};
		int m_SelectedQuadPoints = 1;
		int m_SelectedQuadEnvelope = 0;
		int m_CurrentQuadIndex = 0;
		int m_SelectedEnvelope = 0;
		std::vector<std::pair<int, int>> m_vSelectedEnvelopePoints{{0, 0}};
		std::pair<int, int> m_SelectedTangentIn{0, 0};
		std::pair<int, int> m_SelectedTangentOut{0, 0};
		int m_SelectedImage = 0;
		int m_SelectedSound = 0;
		int m_SelectedSoundSource = 0;

		bool Normalize()
		{
			return CEditorMap::NormalizeSelectionState({
				m_SelectedGroup,
				m_vSelectedLayers,
				m_vGroupLayerCounts,
				m_EnvelopeCount,
				m_ImageCount,
				m_SoundCount,
				m_vSelectedQuads,
				m_SelectedQuadPoints,
				m_SelectedQuadEnvelope,
				m_CurrentQuadIndex,
				m_SelectedEnvelope,
				m_vSelectedEnvelopePoints,
				m_SelectedTangentIn,
				m_SelectedTangentOut,
				m_SelectedImage,
				m_SelectedSound,
				m_SelectedSoundSource,
			});
		}
	};
}

TEST(EditorMapNormalizeSelection, RemovedSelectedGroupClampsAndClearsDependentSelection)
{
	SSelectionFixture Fixture;
	Fixture.m_SelectedEnvelope = 1;
	// 两个组各一层，选中第二组；模拟撤销截掉最后一个组。
	Fixture.m_vGroupLayerCounts.pop_back();

	EXPECT_TRUE(Fixture.Normalize());

	EXPECT_EQ(Fixture.m_SelectedGroup, 0);
	ASSERT_EQ(Fixture.m_vSelectedLayers.size(), 1u);
	EXPECT_EQ(Fixture.m_vSelectedLayers[0], 0);
	EXPECT_TRUE(Fixture.m_vSelectedQuads.empty());
	EXPECT_EQ(Fixture.m_SelectedQuadPoints, 0);
	EXPECT_EQ(Fixture.m_SelectedQuadEnvelope, -1);
	EXPECT_EQ(Fixture.m_CurrentQuadIndex, -1);
	EXPECT_EQ(Fixture.m_SelectedSoundSource, -1);
	EXPECT_EQ(Fixture.m_SelectedEnvelope, 0);
	EXPECT_TRUE(Fixture.m_vSelectedEnvelopePoints.empty());
	EXPECT_EQ(Fixture.m_SelectedTangentIn, std::pair(-1, -1));
	EXPECT_EQ(Fixture.m_SelectedTangentOut, std::pair(-1, -1));
}

TEST(EditorMapNormalizeSelection, ValidSelectionIsKept)
{
	SSelectionFixture Fixture;
	// 撤销只删掉了未选中的追加资源时（截断后剩余资源与选择均有效），已有选择不应被清掉。

	EXPECT_FALSE(Fixture.Normalize());

	EXPECT_EQ(Fixture.m_SelectedGroup, 1);
	EXPECT_EQ(Fixture.m_vSelectedLayers, std::vector<int>{0});
	EXPECT_EQ(Fixture.m_vSelectedQuads, std::vector<int>{0});
	EXPECT_EQ(Fixture.m_SelectedQuadPoints, 1);
	EXPECT_EQ(Fixture.m_SelectedQuadEnvelope, 0);
	EXPECT_EQ(Fixture.m_CurrentQuadIndex, 0);
	EXPECT_EQ(Fixture.m_SelectedSoundSource, 0);
	EXPECT_EQ(Fixture.m_vSelectedEnvelopePoints.size(), 1u);
	EXPECT_EQ(Fixture.m_SelectedTangentIn, std::pair(0, 0));
}

TEST(EditorMapNormalizeSelection, RemovedEnvelopeClearsPointsWithoutTouchingLayerSelection)
{
	SSelectionFixture Fixture;
	// 选中组不变，但选中的包络与四边形包络都被截掉。
	Fixture.m_SelectedEnvelope = 1;
	Fixture.m_SelectedQuadEnvelope = 1;

	EXPECT_FALSE(Fixture.Normalize());

	EXPECT_EQ(Fixture.m_SelectedGroup, 1);
	EXPECT_EQ(Fixture.m_vSelectedQuads, std::vector<int>{0});
	EXPECT_EQ(Fixture.m_SelectedQuadEnvelope, -1);
	EXPECT_EQ(Fixture.m_SelectedEnvelope, 0);
	EXPECT_TRUE(Fixture.m_vSelectedEnvelopePoints.empty());
	EXPECT_EQ(Fixture.m_SelectedTangentIn, std::pair(-1, -1));
	EXPECT_EQ(Fixture.m_SelectedTangentOut, std::pair(-1, -1));
}

TEST(EditorMapNormalizeSelection, RemovedSelectedLayerFallsBackToLastLayer)
{
	SSelectionFixture Fixture;
	Fixture.m_vGroupLayerCounts = {1, 3};
	Fixture.m_vSelectedLayers = {4};

	EXPECT_TRUE(Fixture.Normalize());

	EXPECT_EQ(Fixture.m_SelectedGroup, 1);
	EXPECT_EQ(Fixture.m_vSelectedLayers, std::vector<int>{2});
	EXPECT_TRUE(Fixture.m_vSelectedQuads.empty());
	EXPECT_EQ(Fixture.m_CurrentQuadIndex, -1);
}
