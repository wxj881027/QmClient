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

TEST(EditorCollabCameraReload, ThrowingLoadRestoresViewAndAllowsRetry)
{
	SView View;
	const SView Previous = View;
	bool Applying = false;
	int Captures = 0;
	const auto Capture = [&] {
		++Captures;
		return CEditorCollabPlacementSnapshot{};
	};
	const bool Loaded = EditorCollab::TryApplyingSnapshot(Applying, [&] {
		return ReloadEditorCollabSnapshot(View, true, Capture, [&]() -> bool {
			EXPECT_TRUE(Applying);
			View.m_Zoom = View.m_WorldZoom = 1.0f;
			View.m_WorldOffset = View.m_EditorOffset = vec2(5.0f, 6.0f);
			throw std::bad_alloc();
		});
	});
	EXPECT_FALSE(Loaded);
	EXPECT_FALSE(Applying);
	EXPECT_EQ(Captures, 1);
	EXPECT_EQ(View.m_Zoom, Previous.m_Zoom);
	EXPECT_EQ(View.m_WorldZoom, Previous.m_WorldZoom);
	EXPECT_EQ(View.m_WorldOffset, Previous.m_WorldOffset);
	EXPECT_EQ(View.m_EditorOffset, Previous.m_EditorOffset);
	EXPECT_TRUE(EditorCollab::TryApplyingSnapshot(Applying, [&] { return Applying; }));
	EXPECT_FALSE(Applying);
}

TEST(EditorCollabCameraReload, NestedCapacityFailureLeavesOuterLoadActive)
{
	bool Applying = false;
	EXPECT_TRUE(EditorCollab::TryApplyingSnapshot(Applying, [&] {
		EXPECT_FALSE(EditorCollab::TryApplyingSnapshot(Applying, []() -> bool {
			throw std::length_error("map capacity");
		}));
		return Applying;
	}));
	EXPECT_FALSE(Applying);
}

TEST(EditorCollabCameraReload, UnhandledExceptionRestoresApplyingFlag)
{
	bool Applying = false;
	EXPECT_THROW(EditorCollab::TryApplyingSnapshot(Applying, []() -> bool {
		throw std::runtime_error("unexpected load error");
	}), std::runtime_error);
	EXPECT_FALSE(Applying);
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
