#include <game/editor/collab_camera.h>
#include <game/mapitems.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace
{
	using SLayer = CEditorCollabPlacementSnapshot::SLayer;

	SLayer Tiles(int Width, int Height, const char *pName = "Game")
	{
		SLayer Layer;
		Layer.m_Kind = LAYERTYPE_TILES;
		Layer.m_GroupName = "Game";
		Layer.m_LayerName = pName;
		Layer.m_Width = Width;
		Layer.m_Height = Height;
		Layer.m_vTiles.resize((size_t)Width * Height);
		return Layer;
	}

	void ExpectCenter(const CEditorCollabPlacementSnapshot &Previous, const CEditorCollabPlacementSnapshot &Current, float X, float Y)
	{
		const auto Target = Current.FindPlacement(Previous);
		ASSERT_TRUE(Target.has_value());
		EXPECT_FLOAT_EQ(Target->m_Center.x, X);
		EXPECT_FLOAT_EQ(Target->m_Center.y, Y);
	}
}

TEST(EditorCollabCamera, ReloadRestoresWholeOpaqueZoomAndOffsetsAfterSuccessOrFailure)
{
	// 不启动编辑器；加载回调代表会清空相机与工具状态的地图替换边界。
	// 缩放是不可分割的值，回归约束包含其全部内部动画状态。
	struct SView
	{
		std::array<float, 8> m_Zoom;
		float m_WorldZoom;
		vec2 m_WorldOffset;
		vec2 m_EditorOffset;
		bool m_Dragging;
	};
	for(const bool Success : {true, false})
	{
		SCOPED_TRACE(Success);
		SView View{{240.0f, 320.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f}, 2.4f, vec2(1024.0f, -512.0f), vec2(17.0f, 33.0f), true};
		const auto Previous = View;
		EXPECT_EQ(ReloadEditorCollabSnapshotKeepingCamera(View, [&]() {
			View = {{}, 1.0f, vec2(0.0f, 0.0f), vec2(0.0f, 0.0f), false};
			return Success;
		}),
			Success);
		EXPECT_EQ(View.m_Zoom, Previous.m_Zoom);
		EXPECT_FLOAT_EQ(View.m_WorldZoom, Previous.m_WorldZoom);
		EXPECT_EQ(View.m_WorldOffset, Previous.m_WorldOffset);
		EXPECT_EQ(View.m_EditorOffset, Previous.m_EditorOffset);
		EXPECT_FALSE(View.m_Dragging);
	}
}

TEST(EditorCollabCamera, OnlyEnabledNewRemoteRevisionsCanFollow)
{
	EXPECT_FALSE(ShouldFollowEditorCollabPlacement(false, true, 4, 5));
	EXPECT_FALSE(ShouldFollowEditorCollabPlacement(true, false, 4, 5));
	EXPECT_FALSE(ShouldFollowEditorCollabPlacement(true, true, 4, 4));
	EXPECT_FALSE(ShouldFollowEditorCollabPlacement(true, true, 4, 3));
	EXPECT_TRUE(ShouldFollowEditorCollabPlacement(true, true, 4, 5));
}

TEST(EditorCollabCamera, AddedAndReplacedTilesLocateTheirCombinedRegion)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(8, 6));
	Previous.m_vLayers[0].m_vTiles[1 * 8 + 2] = 1;
	auto Current = Previous;
	Current.m_vLayers[0].m_vTiles[1 * 8 + 2] = 2;
	Current.m_vLayers[0].m_vTiles[3 * 8 + 6] = 1;
	ExpectCenter(Previous, Current, 144.0f, 80.0f);
}

TEST(EditorCollabCamera, TileDeletionDoesNotMoveCamera)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(3, 2));
	Previous.m_vLayers[0].m_vTiles[4] = 1;
	auto Current = Previous;
	Current.m_vLayers[0].m_vTiles[4] = 0;
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
	Current.m_vLayers.clear();
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
}

TEST(EditorCollabCamera, RenamingPropertiesAndReorderingUnchangedLayersDoNotMoveCamera)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(4, 2, "Foreground"));
	Previous.m_vLayers.push_back(Tiles(2, 2, "Background"));
	Previous.m_vLayers[0].m_vTiles[2] = 3;
	Previous.m_vLayers[1].m_vTiles[3] = 5;
	auto Current = Previous;
	std::reverse(Current.m_vLayers.begin(), Current.m_vLayers.end());
	Current.m_vLayers[0].m_GroupName = "Renamed group";
	Current.m_vLayers[0].m_LayerName = "Renamed layer";
	Current.m_vLayers[0].m_GroupOffset = ivec2(1000, 500);
	Current.m_vLayers[0].m_Parallax = ivec2(50, 0);
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
}

TEST(EditorCollabCamera, ReorderWithOneEditedLayerOnlyLocatesThePlacement)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(8, 2, "First"));
	Previous.m_vLayers.push_back(Tiles(8, 2, "Second"));
	Previous.m_vLayers[0].m_vTiles[0] = 1;
	Previous.m_vLayers[1].m_vTiles[1] = 1;
	auto Current = Previous;
	std::reverse(Current.m_vLayers.begin(), Current.m_vLayers.end());
	Current.m_vLayers[1].m_vTiles[7] = 2;
	ExpectCenter(Previous, Current, 240.0f, 16.0f);
}

TEST(EditorCollabCamera, EqualContentNamedLayersKeepTheirOwnBaselineWhenOnlyOneChanges)
{
	for(const bool ReversePrevious : {false, true})
	{
		for(const bool ReverseCurrent : {false, true})
		{
			SCOPED_TRACE(::testing::Message() << ReversePrevious << ", " << ReverseCurrent);
			CEditorCollabPlacementSnapshot Previous;
			Previous.m_vLayers.push_back(Tiles(8, 1, "A"));
			Previous.m_vLayers.push_back(Tiles(8, 1, "B"));
			Previous.m_vLayers[0].m_vTiles[0] = 1;
			Previous.m_vLayers[1].m_vTiles[0] = 1;
			auto Current = Previous;
			Current.m_vLayers[0].m_vTiles[7] = 2;
			if(ReversePrevious)
				std::reverse(Previous.m_vLayers.begin(), Previous.m_vLayers.end());
			if(ReverseCurrent)
				std::reverse(Current.m_vLayers.begin(), Current.m_vLayers.end());
			ExpectCenter(Previous, Current, 240.0f, 16.0f);
		}
	}
}

TEST(EditorCollabCamera, ExpandingTileLayerDoesNotTreatUnchangedCoordinatesAsPlacements)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(2, 2));
	Previous.m_vLayers[0].m_vTiles[3] = 1;
	CEditorCollabPlacementSnapshot Current;
	Current.m_vLayers.push_back(Tiles(3, 3));
	Current.m_vLayers[0].m_vTiles[4] = 1;
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
	Current.m_vLayers[0].m_vTiles[8] = 1;
	ExpectCenter(Previous, Current, 80.0f, 80.0f);
}

TEST(EditorCollabCamera, AmbiguousSameNameLayerChangesRetainLocalView)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(8, 1));
	Previous.m_vLayers.push_back(Tiles(8, 1));
	Previous.m_vLayers[0].m_vTiles[0] = 1;
	Previous.m_vLayers[0].m_vTiles[7] = 1;
	Previous.m_vLayers[1].m_vTiles[1] = 1;
	Previous.m_vLayers[1].m_vTiles[6] = 1;
	auto Current = Previous;
	Current.m_vLayers[0].m_vTiles.assign(8, 0);
	Current.m_vLayers[1].m_vTiles[6] = 2;
	std::reverse(Current.m_vLayers.begin(), Current.m_vLayers.end());
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
}

TEST(EditorCollabCamera, NewlyAddedLayerLocatesOnlyItsNonemptyContent)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(10, 10));
	Previous.m_vLayers[0].m_vTiles[0] = 1;
	auto Current = Previous;
	Current.m_vLayers.push_back(Tiles(10, 10, "New layer"));
	Current.m_vLayers.back().m_vTiles[99] = 2;
	ExpectCenter(Previous, Current, 304.0f, 304.0f);
}

TEST(EditorCollabCamera, IdenticalLayerCopiedToAnotherGroupFollowsTheCopy)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(4, 1, "Detail"));
	Previous.m_vLayers[0].m_vTiles[2] = 1;
	auto Current = Previous;
	Current.m_vLayers.insert(Current.m_vLayers.begin(), Previous.m_vLayers.front());
	Current.m_vLayers[0].m_GroupName = "Decor";
	Current.m_vLayers[0].m_GroupOffset = ivec2(64, 32);
	Current.m_vLayers[1].m_GroupIndex = 1;

	const auto Target = Current.FindPlacement(Previous);
	ASSERT_TRUE(Target.has_value());
	EXPECT_EQ(Target->m_GroupOffset, ivec2(64, 32));
	EXPECT_EQ(Target->m_Center, vec2(80.0f, 16.0f));
}

TEST(EditorCollabCamera, NewOrMovedQuadLocatesItsGeometryWithoutDeletedQuad)
{
	CEditorCollabPlacementSnapshot Previous;
	SLayer Layer;
	Layer.m_Kind = LAYERTYPE_QUADS;
	Layer.m_vQuads.push_back({0, 0, 32768, 0, 0, 32768, 32768, 32768});
	Previous.m_vLayers.push_back(Layer);
	auto Current = Previous;
	Current.m_vLayers[0].m_vQuads = {{{65536, 98304, 98304, 98304, 65536, 131072, 98304, 131072}}};
	ExpectCenter(Previous, Current, 80.0f, 112.0f);
	Current.m_vLayers[0].m_vQuads.clear();
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
}

TEST(EditorCollabCamera, DuplicateSoundSourceAtSamePositionIsStillNewPlacement)
{
	CEditorCollabPlacementSnapshot Previous;
	SLayer Layer;
	Layer.m_Kind = LAYERTYPE_SOUNDS;
	Layer.m_vSounds.push_back({32768, 65536});
	Previous.m_vLayers.push_back(Layer);
	auto Current = Previous;
	Current.m_vLayers[0].m_vSounds.push_back({32768, 65536});
	ExpectCenter(Previous, Current, 32.0f, 64.0f);
}

TEST(EditorCollabCamera, CaptureLimitOrFailedCaptureDoesNotProduceTarget)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(1, 1));
	auto Current = Previous;
	Current.m_vLayers[0].m_vTiles[0] = 1;
	Current.m_Complete = false;
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
	Current.m_Complete = true;
	Previous.m_Complete = false;
	EXPECT_FALSE(Current.FindPlacement(Previous).has_value());
}

TEST(EditorCollabCamera, MultipleGroupsUseLargestPlacementRegion)
{
	CEditorCollabPlacementSnapshot Previous;
	Previous.m_vLayers.push_back(Tiles(4, 1));
	Previous.m_vLayers.push_back(Tiles(4, 1, "Other group layer"));
	Previous.m_vLayers[1].m_GroupIndex = 1;
	auto Current = Previous;
	Current.m_vLayers[0].m_vTiles[0] = 1;
	Current.m_vLayers[1].m_vTiles[2] = 1;
	Current.m_vLayers[1].m_vTiles[3] = 1;
	ExpectCenter(Previous, Current, 96.0f, 16.0f);
}

TEST(EditorCollabCamera, CenteringRespectsGroupOffsetParallaxAndLocalEditorOffset)
{
	SEditorCollabPlacementTarget Target{vec2(400.0f, 800.0f), ivec2(20, -40), ivec2(50, 200)};
	vec2 World(100.0f, 200.0f);
	vec2 Editor(30.0f, 40.0f);
	Target.CenterCamera(World, Editor);
	EXPECT_EQ(World, vec2(700.0f, 400.0f));
	EXPECT_EQ(Editor, vec2(30.0f, 40.0f));
}

TEST(EditorCollabCamera, ZeroParallaxUsesEditorOffsetWithoutChangingWorldAxis)
{
	SEditorCollabPlacementTarget Target{vec2(300.0f, 600.0f), ivec2(20, 40), ivec2(0, 0)};
	vec2 World(100.0f, 200.0f);
	vec2 Editor(30.0f, 40.0f);
	Target.CenterCamera(World, Editor);
	EXPECT_EQ(World, vec2(100.0f, 200.0f));
	EXPECT_EQ(Editor, vec2(280.0f, 560.0f));
}
