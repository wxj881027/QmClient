#ifndef GAME_EDITOR_COLLAB_CAMERA_H
#define GAME_EDITOR_COLLAB_CAMERA_H

#include <base/vmath.h>

#include <array>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class CEditorMap;

namespace EditorCollab
{
	template<typename FRestore>
	class CRestoreOnExit
	{
	public:
		explicit CRestoreOnExit(FRestore Restore) :
			m_Restore(std::move(Restore)) {}
		CRestoreOnExit(const CRestoreOnExit &) = delete;
		CRestoreOnExit &operator=(const CRestoreOnExit &) = delete;
		~CRestoreOnExit() { m_Restore(); }

	private:
		FRestore m_Restore;
	};

	template<typename FLoad>
	bool TryApplyingSnapshot(bool &Applying, const FLoad &Load)
	{
		const CRestoreOnExit RestoreFlag([&Applying, Previous = Applying] { Applying = Previous; });
		Applying = true;
		try
		{
			return Load();
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
}

// 快照替换会取消当前工具操作，只保存相机；缩放对象按值保存，包含进行中的平滑曲线。
template<typename TViewState, typename FLoad>
bool ReloadEditorCollabSnapshotKeepingCamera(TViewState &State, FLoad &&Load)
{
	const auto Zoom = State.m_Zoom;
	const float WorldZoom = State.m_WorldZoom;
	const vec2 WorldOffset = State.m_WorldOffset;
	const vec2 EditorOffset = State.m_EditorOffset;
	const EditorCollab::CRestoreOnExit RestoreCamera([&] {
		State.m_Zoom = Zoom;
		State.m_WorldZoom = WorldZoom;
		State.m_WorldOffset = WorldOffset;
		State.m_EditorOffset = EditorOffset;
	});
	return std::forward<FLoad>(Load)();
}

bool ShouldFollowEditorCollabPlacement(bool Enabled, bool RemoteUpdate, int PreviousRevision, int Revision);

struct SEditorCollabPlacementTarget
{
	vec2 m_Center;
	ivec2 m_GroupOffset;
	ivec2 m_Parallax;

	void CenterCamera(vec2 &WorldOffset, vec2 &EditorOffset) const;
};

// 只记录放置内容，组/图层属性和对象颜色、包络等属性不会产生跟随目标。
class CEditorCollabPlacementSnapshot
{
public:
	struct SLayer
	{
		int m_Kind = 0;
		int m_GroupIndex = 0;
		std::string m_GroupName;
		std::string m_LayerName;
		ivec2 m_GroupOffset = ivec2(0, 0);
		ivec2 m_Parallax = ivec2(100, 100);
		int m_Width = 0;
		int m_Height = 0;
		std::vector<uint16_t> m_vTiles;
		std::vector<std::array<int, 8>> m_vQuads;
		std::vector<std::array<int, 2>> m_vSounds;
	};

	bool m_Complete = true;
	std::vector<SLayer> m_vLayers;

	static CEditorCollabPlacementSnapshot Capture(const CEditorMap &Map);
	std::optional<SEditorCollabPlacementTarget> FindPlacement(const CEditorCollabPlacementSnapshot &Previous) const;
};

// 镜头跟随的捕获或匹配失败时跳过居中，地图加载本身的结果由 Load 返回。
template<typename TViewState, typename FCapture, typename FLoad>
bool ReloadEditorCollabSnapshot(TViewState &State, bool FollowPlacement, const FCapture &Capture, const FLoad &Load)
{
	if(!FollowPlacement)
		return ReloadEditorCollabSnapshotKeepingCamera(State, Load);

	const auto TryPlacement = [](const auto &Operation) {
		try
		{
			Operation();
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	};

	CEditorCollabPlacementSnapshot Previous;
	const bool Captured = TryPlacement([&] { Previous = Capture(); });
	const bool Loaded = ReloadEditorCollabSnapshotKeepingCamera(State, Load);
	if(Loaded && Captured && Previous.m_Complete)
	{
		TryPlacement([&] {
			if(const auto Target = Capture().FindPlacement(Previous))
				Target->CenterCamera(State.m_WorldOffset, State.m_EditorOffset);
		});
	}
	return Loaded;
}

#endif
