#include "collab_camera.h"

#include <base/math.h>

#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

// 本文件只承载不依赖编辑器图层派生类的核心逻辑；依赖 CLayerTiles 等派生类
// 的 Capture 在 collab_camera_map_capture.cpp，仅编入 CLIENT 目标，行为测试
// 不链接编辑器图层实现，避免 testrunner 缺少派生类 typeinfo 的链接失败。

namespace
{
	using SLayer = CEditorCollabPlacementSnapshot::SLayer;
	constexpr size_t MAX_CAPTURE_BYTES = 64 * 1024 * 1024;

	uint64_t ContentHash(const SLayer &Layer)
	{
		uint64_t Hash = 14695981039346656037ULL;
		const auto Add = [&Hash](uint64_t Value) {
			Hash = (Hash ^ Value) * 1099511628211ULL;
		};
		Add(Layer.m_Kind);
		Add(Layer.m_Width);
		Add(Layer.m_Height);
		for(const uint16_t Tile : Layer.m_vTiles)
			Add(Tile);
		for(const auto &Quad : Layer.m_vQuads)
			for(const int Value : Quad)
				Add(Value);
		for(const auto &Sound : Layer.m_vSounds)
			for(const int Value : Sound)
				Add(Value);
		return Hash;
	}

	bool SameContent(const SLayer &A, const SLayer &B)
	{
		return A.m_Kind == B.m_Kind && A.m_Width == B.m_Width && A.m_Height == B.m_Height &&
		       A.m_vTiles == B.m_vTiles && A.m_vQuads == B.m_vQuads && A.m_vSounds == B.m_vSounds;
	}

	struct SRegion
	{
		SEditorCollabPlacementTarget m_Target;
		vec2 m_Min = vec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		vec2 m_Max = vec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
		size_t m_Placements = 0;

		void Add(vec2 Point)
		{
			m_Min.x = std::min(m_Min.x, Point.x);
			m_Min.y = std::min(m_Min.y, Point.y);
			m_Max.x = std::max(m_Max.x, Point.x);
			m_Max.y = std::max(m_Max.y, Point.y);
		}
	};

	template<typename T, typename F>
	void VisitAdded(const std::vector<T> &Current, const std::vector<T> &Previous, F &&Visit)
	{
		size_t OldIndex = 0;
		for(const auto &Item : Current)
		{
			while(OldIndex < Previous.size() && Previous[OldIndex] < Item)
				++OldIndex;
			if(OldIndex < Previous.size() && Previous[OldIndex] == Item)
				++OldIndex;
			else
				Visit(Item);
		}
	}
}

bool ShouldFollowEditorCollabPlacement(bool Enabled, bool RemoteUpdate, int PreviousRevision, int Revision)
{
	return Enabled && RemoteUpdate && Revision > PreviousRevision;
}

void SEditorCollabPlacementTarget::CenterCamera(vec2 &WorldOffset, vec2 &EditorOffset) const
{
	const auto CenterAxis = [](float Center, int GroupOffset, int Parallax, float &World, float &Editor) {
		if(Parallax == 0)
			Editor = Center - GroupOffset;
		else
			World = (Center - GroupOffset - Editor) * 100.0f / Parallax;
	};
	CenterAxis(m_Center.x, m_GroupOffset.x, m_Parallax.x, WorldOffset.x, EditorOffset.x);
	CenterAxis(m_Center.y, m_GroupOffset.y, m_Parallax.y, WorldOffset.y, EditorOffset.y);
}

std::optional<SEditorCollabPlacementTarget> CEditorCollabPlacementSnapshot::FindPlacement(const CEditorCollabPlacementSnapshot &Previous) const
{
	if(!m_Complete || !Previous.m_Complete)
		return std::nullopt;

	// 先消去内容相同的图层，改名、属性变化与重排不会被误判为新放置。
	std::unordered_multimap<uint64_t, size_t> OldByContent;
	for(size_t i = 0; i < Previous.m_vLayers.size(); ++i)
		OldByContent.emplace(ContentHash(Previous.m_vLayers[i]), i);
	std::vector<bool> vOldUsed(Previous.m_vLayers.size(), false);
	std::vector<bool> vUnchanged(m_vLayers.size(), false);
	const auto MatchUnchanged = [&](size_t i, bool SameIdentity) {
		const auto Range = OldByContent.equal_range(ContentHash(m_vLayers[i]));
		for(auto It = Range.first; It != Range.second; ++It)
		{
			const SLayer &Old = Previous.m_vLayers[It->second];
			if(SameIdentity && (m_vLayers[i].m_GroupName != Old.m_GroupName || m_vLayers[i].m_LayerName != Old.m_LayerName))
				continue;
			if(SameContent(m_vLayers[i], Old))
			{
				vOldUsed[It->second] = true;
				vUnchanged[i] = true;
				OldByContent.erase(It);
				break;
			}
		}
	};
	// 同内容的不同命名图层先各自保留基线，不能让未改的 B 吃掉已改的 A。
	for(size_t i = 0; i < m_vLayers.size(); ++i)
		MatchUnchanged(i, true);
	for(size_t i = 0; i < m_vLayers.size(); ++i)
		if(!vUnchanged[i])
			MatchUnchanged(i, false);

	using TLayerKey = std::tuple<int, std::string, std::string>;
	const auto Key = [](const SLayer &Layer) { return TLayerKey(Layer.m_Kind, Layer.m_GroupName, Layer.m_LayerName); };
	std::multimap<TLayerKey, size_t> OldByIdentity;
	for(size_t i = 0; i < Previous.m_vLayers.size(); ++i)
		if(!vOldUsed[i])
			OldByIdentity.emplace(Key(Previous.m_vLayers[i]), i);
	std::map<TLayerKey, size_t> NewIdentityCounts;
	for(size_t i = 0; i < m_vLayers.size(); ++i)
		if(!vUnchanged[i])
			++NewIdentityCounts[Key(m_vLayers[i])];
	std::map<int, SRegion> Regions;
	const SLayer Empty;
	for(size_t i = 0; i < m_vLayers.size(); ++i)
	{
		if(vUnchanged[i])
			continue;
		const SLayer &Layer = m_vLayers[i];
		const SLayer *pOld = &Empty;
		const auto Range = OldByIdentity.equal_range(Key(Layer));
		if(Range.first != Range.second)
		{
			auto Next = Range.first;
			++Next;
			// 快照没有稳定图层 ID；同名多层同时变化时不猜测对应关系。
			if(Next != Range.second || NewIdentityCounts[Key(Layer)] > 1)
				continue;
			pOld = &Previous.m_vLayers[Range.first->second];
			OldByIdentity.erase(Range.first);
		}
		SRegion &Region = Regions[Layer.m_GroupIndex];
		Region.m_Target.m_GroupOffset = Layer.m_GroupOffset;
		Region.m_Target.m_Parallax = Layer.m_Parallax;
		for(int y = 0; y < Layer.m_Height; ++y)
		{
			for(int x = 0; x < Layer.m_Width; ++x)
			{
				const uint16_t Tile = Layer.m_vTiles[(size_t)y * Layer.m_Width + x];
				const uint16_t OldTile = x < pOld->m_Width && y < pOld->m_Height ? pOld->m_vTiles[(size_t)y * pOld->m_Width + x] : 0;
				if(Tile != 0 && Tile != OldTile)
				{
					Region.Add(vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f));
					++Region.m_Placements;
				}
			}
		}
		VisitAdded(Layer.m_vQuads, pOld->m_vQuads, [&Region](const auto &Quad) {
			for(int Point = 0; Point < 4; ++Point)
				Region.Add(vec2(fx2f(Quad[2 * Point]), fx2f(Quad[2 * Point + 1])));
			++Region.m_Placements;
		});
		VisitAdded(Layer.m_vSounds, pOld->m_vSounds, [&Region](const auto &Sound) {
			Region.Add(vec2(fx2f(Sound[0]), fx2f(Sound[1])));
			++Region.m_Placements;
		});
	}
	// 各组视差不同，选放置最多的组，避免把不同组的坐标混成不可见的中心。
	const SRegion *pBest = nullptr;
	for(const auto &[Index, Region] : Regions)
		if(Region.m_Placements > 0 && (!pBest || Region.m_Placements > pBest->m_Placements))
			pBest = &Region;
	if(!pBest)
		return std::nullopt;
	auto Target = pBest->m_Target;
	Target.m_Center = (pBest->m_Min + pBest->m_Max) * 0.5f;
	return Target;
}
