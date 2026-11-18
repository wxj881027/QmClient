#include <game/editor/collab_camera.h>

#include <gtest/gtest.h>

namespace
{
	struct SView
	{
		float m_Zoom = 2.0f;
		float m_WorldZoom = 2.0f;
		vec2 m_WorldOffset = vec2(10.0f, 20.0f);
		vec2 m_EditorOffset = vec2(0.0f, 0.0f);
	};
}

TEST(EditorCollabCameraReload, FirstCaptureFailureStillLoadsTheMapOnce)
{
	SView View;
	int Captures = 0;
	int Loads = 0;
	const auto Capture = [&]() -> CEditorCollabPlacementSnapshot {
		++Captures;
		throw std::bad_alloc();
	};
	const bool Loaded = ReloadEditorCollabSnapshot(View, true, Capture, [&] {
		++Loads;
		View.m_WorldOffset = vec2(0, 0);
		return true;
	});
	EXPECT_TRUE(Loaded);
	EXPECT_EQ(Loads, 1);
	EXPECT_EQ(Captures, 1);
	EXPECT_EQ(View.m_WorldOffset, vec2(10.0f, 20.0f));
}

TEST(EditorCollabCameraReload, SecondCaptureFailureKeepsSuccessfulMapAndCamera)
{
	SView View;
	int Captures = 0;
	bool MapReplaced = false;
	const auto Capture = [&]() -> CEditorCollabPlacementSnapshot {
		++Captures;
		if(MapReplaced)
			throw std::length_error("placement capacity");
		return {};
	};
	const bool Loaded = ReloadEditorCollabSnapshot(View, true, Capture, [&] {
		MapReplaced = true;
		View.m_WorldOffset = vec2(0, 0);
		return true;
	});
	EXPECT_TRUE(Loaded);
	EXPECT_TRUE(MapReplaced);
	EXPECT_EQ(Captures, 2);
	EXPECT_EQ(View.m_WorldOffset, vec2(10.0f, 20.0f));
}

TEST(EditorCollabCameraReload, FailedMapLoadSkipsSecondCapture)
{
	SView View;
	int Captures = 0;
	const bool Loaded = ReloadEditorCollabSnapshot(View, true, [&] {
		++Captures;
		return CEditorCollabPlacementSnapshot{};
	}, [] { return false; });
	EXPECT_FALSE(Loaded);
	EXPECT_EQ(Captures, 1);
}

TEST(EditorCollabCameraReload, SuccessfulLoadFollowsNewPlacementAfterRestoringCamera)
{
	SView View;
	bool MapReplaced = false;
	const auto Capture = [&] {
		CEditorCollabPlacementSnapshot Snapshot;
		auto &Layer = Snapshot.m_vLayers.emplace_back();
		Layer.m_Width = Layer.m_Height = 1;
		Layer.m_vTiles.push_back(MapReplaced ? 1 : 0);
		return Snapshot;
	};
	ASSERT_TRUE(ReloadEditorCollabSnapshot(View, true, Capture, [&] {
		MapReplaced = true;
		return true;
	}));
	EXPECT_EQ(View.m_WorldOffset, vec2(16.0f, 16.0f));
}
