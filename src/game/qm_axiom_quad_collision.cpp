/* 四边形碰撞改编自 Xyrion-0805/xyrionclient，规则源于 TeeworldsAxiom/teeworlds-Axiom。 */
#include "qm_axiom_quad_collision.h"

#include <base/str.h>

#include <engine/map.h>

#include <game/gamecore.h>
#include <game/layers.h>
#include <game/mapitems_ex.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace
{
	constexpr float EPSILON = 0.00001f;
	constexpr int GRID_SIZE = 256;
	constexpr int MAX_GRID_CELLS = 4096;

	uint64_t GridKey(int X, int Y)
	{
		return (uint64_t(uint32_t(X)) << 32) | uint32_t(Y);
	}

	float Cross(vec2 A, vec2 B)
	{
		return A.x * B.y - A.y * B.x;
	}

	vec2 Rotate(vec2 Point, float Angle)
	{
		const float C = std::cos(Angle), S = std::sin(Angle);
		return vec2(Point.x * C - Point.y * S, Point.x * S + Point.y * C);
	}

	bool Solid(int Tile)
	{
		return Tile == TILE_SOLID || Tile == TILE_NOHOOK;
	}

	int LayerTile(const char *pName)
	{
		static const struct
		{
			const char *m_pName;
			int m_Tile;
		} s_aRules[] = {
			{"QFr", TILE_FREEZE}, {"QUnFr", TILE_UNFREEZE},
			{"QDeep", TILE_DFREEZE}, {"QUnDeep", TILE_DUNFREEZE},
			{"QLiveFr", TILE_LFREEZE}, {"QUnLiveFr", TILE_LUNFREEZE},
			{"QKill", TILE_DEATH}, {"QFinish", TILE_FINISH},
			{"QCfrm", TILE_TELECHECKINEVIL}, {"QHook", TILE_SOLID},
			{"QStopa", TILE_NOHOOK}, {"QTune", TILE_TUNE},
			{"QReJump", TILE_REFILL_JUMPS}};
		for(const auto &Rule : s_aRules)
			if(str_comp_nocase(pName, Rule.m_pName) == 0)
				return Rule.m_Tile;
		return 0;
	}

	// 裁剪真实碰撞多边形，保留薄四边形与跨裁剪边界的盒体碰撞。
	void ClipPolygon(std::vector<vec2> &vPolygon, std::vector<vec2> &vScratch, vec2 Min, vec2 Max)
	{
		for(int Side = 0; Side < 4 && !vPolygon.empty(); ++Side)
		{
			const bool Horizontal = Side < 2;
			const bool Lower = Side % 2 == 0;
			const float Limit = Horizontal ? (Lower ? Min.x : Max.x) : (Lower ? Min.y : Max.y);
			const auto Coordinate = [Horizontal](vec2 Point) { return Horizontal ? Point.x : Point.y; };
			const auto Inside = [&](vec2 Point) { return Lower ? Coordinate(Point) >= Limit : Coordinate(Point) <= Limit; };
			vScratch.clear();
			vec2 Prev = vPolygon.back();
			for(vec2 Point : vPolygon)
			{
				if(Inside(Point) != Inside(Prev))
				{
					const float Fraction = (Limit - Coordinate(Prev)) / (Coordinate(Point) - Coordinate(Prev));
					vScratch.push_back(mix(Prev, Point, Fraction));
				}
				if(Inside(Point))
					vScratch.push_back(Point);
				Prev = Point;
			}
			vPolygon.swap(vScratch);
		}
	}

	float Cubic(float P0, float P1, float P2, float P3, float T)
	{
		return bezier(P0, P1, P2, P3, T);
	}
} // namespace

void CQmAxiomQuadCollision::LoadEnvelopes(CLayers *pLayers)
{
	IMap *pMap = pLayers->Map();
	int Start, Num;
	pMap->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
	m_vEnvelopes.resize(Num);
	bool UpstreamBezier = false;
	for(int i = 0; i < Num; ++i)
	{
		if(pMap->GetItemSize(Start + i) >= (int)sizeof(CMapItemEnvelope_v1))
		{
			const auto *pEnv = static_cast<CMapItemEnvelope_v1 *>(pMap->GetItem(Start + i));
			UpstreamBezier |= pEnv && pEnv->m_Version >= CMapItemEnvelope::VERSION_TEEWORLDS_BEZIER;
		}
	}
	int PointsStart, PointsNum;
	pMap->GetType(MAPITEMTYPE_ENVPOINTS, &PointsStart, &PointsNum);
	if(PointsNum == 0)
		return;
	const void *pPoints = pMap->GetItem(PointsStart);
	const int NumPoints = pMap->GetItemSize(PointsStart) / (UpstreamBezier ? sizeof(CEnvPointBezier_upstream) : sizeof(CEnvPoint));
	int BezierStart, BezierNum;
	pMap->GetType(MAPITEMTYPE_ENVPOINTS_BEZIER, &BezierStart, &BezierNum);
	const auto *pBezier = BezierNum > 0 && pMap->GetItemSize(BezierStart) / (int)sizeof(CEnvPointBezier) == NumPoints ? static_cast<CEnvPointBezier *>(pMap->GetItem(BezierStart)) : nullptr;
	if(!pPoints)
		return;
	for(int i = 0; i < Num; ++i)
	{
		if(pMap->GetItemSize(Start + i) < (int)sizeof(CMapItemEnvelope_v1))
			continue;
		const auto *pEnv = static_cast<CMapItemEnvelope_v1 *>(pMap->GetItem(Start + i));
		if(!pEnv || pEnv->m_Channels <= 0 || pEnv->m_Channels > 4 || pEnv->m_StartPoint < 0 || pEnv->m_StartPoint > NumPoints || pEnv->m_NumPoints < 0 || pEnv->m_NumPoints > NumPoints - pEnv->m_StartPoint)
			continue;
		CEnvelope &Env = m_vEnvelopes[i];
		Env.m_Channels = pEnv->m_Channels;
		for(int j = 0; j < pEnv->m_NumPoints; ++j)
		{
			const int Index = pEnv->m_StartPoint + j;
			if(UpstreamBezier)
			{
				const auto &Point = static_cast<const CEnvPointBezier_upstream *>(pPoints)[Index];
				Env.m_vPoints.push_back(Point);
				Env.m_vBezier.push_back(Point.m_Bezier);
			}
			else
			{
				Env.m_vPoints.push_back(static_cast<const CEnvPoint *>(pPoints)[Index]);
				if(pBezier)
					Env.m_vBezier.push_back(pBezier[Index]);
			}
		}
		// 无效范围和逆序包络不参与求值。
		if(!std::is_sorted(Env.m_vPoints.begin(), Env.m_vPoints.end()))
		{
			Env.m_vPoints.clear();
			Env.m_vBezier.clear();
		}
	}
}

void CQmAxiomQuadCollision::EvalEnvelope(int Index, double Seconds, float *pValues, int Channels) const
{
	if(Index < 0 || Index >= (int)m_vEnvelopes.size())
		return;
	const CEnvelope &Env = m_vEnvelopes[Index];
	if(Env.m_vPoints.empty())
		return;
	Channels = std::min(Channels, Env.m_Channels);
	const auto Copy = [&](const CEnvPoint &Point) {
		for(int c = 0; c < Channels; ++c)
			pValues[c] = fx2f(Point.m_aValues[c]);
	};
	if(Env.m_vPoints.size() == 1)
	{
		Copy(Env.m_vPoints.front());
		return;
	}
	const double Duration = Env.m_vPoints.back().m_Time.GetInternal();
	double Time = Duration > 0 ? std::fmod(Seconds, Duration / 1000.0) * 1000.0 : 0;
	if(Time < 0)
		Time += Duration;
	// 预测会回退到旧 tick，每次按时间二分定位包络区间。
	const auto SegmentEnd = std::upper_bound(Env.m_vPoints.begin(), Env.m_vPoints.end(), Time,
		[](double Value, const CEnvPoint &Point) { return Value < Point.m_Time.GetInternal(); });
	if(SegmentEnd == Env.m_vPoints.begin() || SegmentEnd == Env.m_vPoints.end())
	{
		Copy(Env.m_vPoints.back());
		return;
	}
	const size_t Segment = SegmentEnd - Env.m_vPoints.begin() - 1;
	for(size_t i = Segment; i + 1 < Env.m_vPoints.size(); ++i)
	{
		const CEnvPoint &A = Env.m_vPoints[i], &B = Env.m_vPoints[i + 1];
		const double Start = A.m_Time.GetInternal(), End = B.m_Time.GetInternal();
		if(Time < Start || Time >= End)
			continue;
		float Fraction = (Time - Start) / (End - Start);
		switch(A.m_Curvetype)
		{
		case CURVETYPE_STEP: Fraction = 0; break;
		case CURVETYPE_SLOW: Fraction = Fraction * Fraction * Fraction; break;
		case CURVETYPE_FAST: Fraction = 1 - (1 - Fraction) * (1 - Fraction) * (1 - Fraction); break;
		case CURVETYPE_SMOOTH: Fraction = Fraction * Fraction * (3 - 2 * Fraction); break;
		default: break;
		}
		for(int c = 0; c < Channels; ++c)
		{
			const float V0 = fx2f(A.m_aValues[c]), V3 = fx2f(B.m_aValues[c]);
			if(A.m_Curvetype == CURVETYPE_BEZIER && Env.m_vBezier.size() == Env.m_vPoints.size())
			{
				const float X1 = std::clamp(float(Start + Env.m_vBezier[i].m_aOutTangentDeltaX[c].GetInternal()), float(Start), float(End));
				const float X2 = std::clamp(float(End + Env.m_vBezier[i + 1].m_aInTangentDeltaX[c].GetInternal()), float(Start), float(End));
				float Low = 0, High = 1;
				for(int Iteration = 0; Iteration < 24; ++Iteration)
				{
					const float Mid = (Low + High) * 0.5f;
					if(Cubic(Start, X1, X2, End, Mid) < Time)
						Low = Mid;
					else
						High = Mid;
				}
				pValues[c] = Cubic(V0, V0 + fx2f(Env.m_vBezier[i].m_aOutTangentDeltaY[c]), V3 + fx2f(Env.m_vBezier[i + 1].m_aInTangentDeltaY[c]), V3, (Low + High) * 0.5f);
			}
			else
				pValues[c] = mix(V0, V3, Fraction);
		}
		return;
	}
	Copy(Env.m_vPoints.back());
}

void CQmAxiomQuadCollision::Init(CLayers *pLayers)
{
	m_vQuads.clear();
	m_vEnvelopes.clear();
	m_vEnvelopeSamples.clear();
	m_vAnimatedQuads.clear();
	m_Grid.clear();
	m_vLargeQuads.clear();
	m_Time = -1;
	if(!pLayers || !pLayers->Map())
		return;
	IMap *pMap = pLayers->Map();
	LoadEnvelopes(pLayers);
	int LayerStart, LayerNum;
	pMap->GetType(MAPITEMTYPE_LAYER, &LayerStart, &LayerNum);
	int GroupStart, GroupNum;
	pMap->GetType(MAPITEMTYPE_GROUP, &GroupStart, &GroupNum);
	for(int g = 0; g < GroupNum; ++g)
	{
		if(pMap->GetItemSize(GroupStart + g) < (int)sizeof(CMapItemGroup_v1))
			continue;
		const CMapItemGroup *pGroup = pLayers->GetGroup(g);
		if(!pGroup || pGroup->m_StartLayer < 0 || pGroup->m_StartLayer > LayerNum || pGroup->m_NumLayers < 0 || pGroup->m_NumLayers > LayerNum - pGroup->m_StartLayer)
			continue;
		// Axiom 使用四边形原坐标判定碰撞，组偏移与视差不参与服务端物理。
		const bool Clip = pGroup->m_Version >= 2 && pMap->GetItemSize(GroupStart + g) >= (int)sizeof(CMapItemGroup) && pGroup->m_UseClipping;
		for(int l = 0; l < pGroup->m_NumLayers; ++l)
		{
			const int LayerIndex = pGroup->m_StartLayer + l;
			if(pMap->GetItemSize(LayerStart + LayerIndex) < (int)sizeof(CMapItemLayerQuads))
				continue;
			const auto *pLayer = reinterpret_cast<CMapItemLayerQuads *>(pLayers->GetLayer(LayerIndex));
			if(!pLayer || pLayer->m_Layer.m_Type != LAYERTYPE_QUADS || pLayer->m_Version < 2 || pLayer->m_Data < 0 || pLayer->m_Data >= pMap->NumData() || pLayer->m_NumQuads < 0)
				continue;
			char aName[13];
			IntsToStr(pLayer->m_aName, std::size(pLayer->m_aName), aName, sizeof(aName));
			const int Tile = LayerTile(aName);
			if(!Tile || pLayer->m_NumQuads > pMap->GetDataSize(pLayer->m_Data) / (int)sizeof(CQuad))
				continue;
			const auto *pQuads = static_cast<CQuad *>(pMap->GetDataSwapped(pLayer->m_Data));
			if(!pQuads)
				continue;
			for(int q = 0; q < pLayer->m_NumQuads; ++q)
			{
				CCollisionQuad Quad{};
				Quad.m_Quad = pQuads[q];
				Quad.m_Tile = Tile;
				Quad.m_Clipped = Clip;
				Quad.m_ClipMin = Clip ? vec2(pGroup->m_ClipX, pGroup->m_ClipY) : vec2(0, 0);
				Quad.m_ClipMax = Clip ? Quad.m_ClipMin + vec2(pGroup->m_ClipW, pGroup->m_ClipH) : vec2(0, 0);
				m_vQuads.push_back(std::move(Quad));
			}
		}
	}
	// 相同包络与时间偏移共用采样，避免逐四边形重复求值。
	std::map<std::tuple<int, int, int>, int> Samples;
	const auto Sample = [&](int Envelope, int Offset, int Channels) {
		const auto Key = std::make_tuple(Envelope, Offset, Channels);
		const auto Existing = Samples.find(Key);
		if(Existing != Samples.end())
			return Existing->second;
		const int Index = m_vEnvelopeSamples.size();
		CEnvelopeSample Entry{Envelope, Offset, Channels, false, {0, 0, 0, 0}};
		if(Channels == 4)
			Entry.m_aValues.fill(1.0f);
		Entry.m_Animated = Envelope >= 0 && Envelope < (int)m_vEnvelopes.size() && m_vEnvelopes[Envelope].m_vPoints.size() > 1;
		EvalEnvelope(Envelope, Offset / 1000.0, Entry.m_aValues.data(), Channels);
		Samples.emplace(Key, Index);
		m_vEnvelopeSamples.push_back(Entry);
		return Index;
	};
	for(size_t Index = 0; Index < m_vQuads.size(); ++Index)
	{
		auto &Quad = m_vQuads[Index];
		Quad.m_vPolygon.reserve(Quad.m_Clipped ? 8 : 4);
		if(Quad.m_Clipped)
			Quad.m_vClipScratch.reserve(8);
		Quad.m_PositionSample = Sample(Quad.m_Quad.m_PosEnv, Quad.m_Quad.m_PosEnvOffset, 3);
		Quad.m_ColorSample = Sample(Quad.m_Quad.m_ColorEnv, Quad.m_Quad.m_ColorEnvOffset, 4);
		if(m_vEnvelopeSamples[Quad.m_PositionSample].m_Animated || m_vEnvelopeSamples[Quad.m_ColorSample].m_Animated)
			m_vAnimatedQuads.push_back(Index);
		UpdateQuad(Index);
	}
	m_Time = 0;
}

void CQmAxiomQuadCollision::SetTime(double Seconds)
{
	if(!std::isfinite(Seconds) || Seconds == m_Time)
		return;
	m_Time = Seconds;
	for(auto &Sample : m_vEnvelopeSamples)
	{
		if(Sample.m_Animated)
			EvalEnvelope(Sample.m_Envelope, Seconds + Sample.m_Offset / 1000.0, Sample.m_aValues.data(), Sample.m_Channels);
	}
	for(int Index : m_vAnimatedQuads)
		UpdateQuad(Index);
}

void CQmAxiomQuadCollision::UpdateQuad(int Index)
{
	auto &Quad = m_vQuads[Index];
	const auto &Position = m_vEnvelopeSamples[Quad.m_PositionSample].m_aValues;
	const auto &Color = m_vEnvelopeSamples[Quad.m_ColorSample].m_aValues;
	const vec2 Offset(Position[0], Position[1]);
	const float Angle = Position[2] * pi / 180.0f;
	const bool Visible = Color[3] > 0 && std::isfinite(Angle) && std::isfinite(Offset.x) && std::isfinite(Offset.y);
	if(Quad.m_GeometryInitialized && Offset == Quad.m_Position && Angle == Quad.m_Angle && Visible == Quad.m_Visible)
		return;
	Quad.m_GeometryInitialized = true;
	Quad.m_Position = Offset;
	Quad.m_Angle = Angle;
	Quad.m_Visible = Visible;
	Quad.m_vPolygon.clear();
	Quad.m_Active = Visible;
	if(Visible)
	{
		const vec2 Pivot(fx2f(Quad.m_Quad.m_aPoints[4].x), fx2f(Quad.m_Quad.m_aPoints[4].y));
		const float C = std::cos(Angle), S = std::sin(Angle);
		// CQuad 按三角带存储，碰撞多边形改用轮廓顺序。
		for(int Corner : {0, 1, 3, 2})
		{
			const auto &Point = Quad.m_Quad.m_aPoints[Corner];
			const vec2 Local = vec2(fx2f(Point.x), fx2f(Point.y)) - Pivot;
			Quad.m_vPolygon.push_back(Pivot + vec2(Local.x * C - Local.y * S, Local.x * S + Local.y * C) + Offset);
		}
		if(Quad.m_Clipped)
			ClipPolygon(Quad.m_vPolygon, Quad.m_vClipScratch, Quad.m_ClipMin, Quad.m_ClipMax);
		float Area = 0;
		// 以首顶点为原点，避免远离地图原点的小方块因大数相减被误判为退化。
		for(size_t i = 1; i + 1 < Quad.m_vPolygon.size(); ++i)
			Area += Cross(Quad.m_vPolygon[i] - Quad.m_vPolygon.front(), Quad.m_vPolygon[i + 1] - Quad.m_vPolygon.front());
		Quad.m_Active = Quad.m_vPolygon.size() >= 3 && std::fabs(Area) > EPSILON;
		if(Quad.m_Active)
		{
			Quad.m_Min = Quad.m_Max = Quad.m_vPolygon.front();
			for(vec2 Point : Quad.m_vPolygon)
			{
				Quad.m_Min.x = std::min(Quad.m_Min.x, Point.x);
				Quad.m_Min.y = std::min(Quad.m_Min.y, Point.y);
				Quad.m_Max.x = std::max(Quad.m_Max.x, Point.x);
				Quad.m_Max.y = std::max(Quad.m_Max.y, Point.y);
			}
		}
	}
	UpdateQuadIndex(Index);
}

void CQmAxiomQuadCollision::UpdateQuadIndex(int Index)
{
	auto &Quad = m_vQuads[Index];
	int MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	bool Large = false;
	if(Quad.m_Active)
	{
		MinX = std::floor(Quad.m_Min.x / GRID_SIZE);
		MinY = std::floor(Quad.m_Min.y / GRID_SIZE);
		MaxX = std::floor(Quad.m_Max.x / GRID_SIZE);
		MaxY = std::floor(Quad.m_Max.y / GRID_SIZE);
		Large = (int64_t(MaxX) - MinX + 1) * (int64_t(MaxY) - MinY + 1) > MAX_GRID_CELLS;
		// 四边形没有跨网格时复用原索引。
		if(Quad.m_Indexed && Large == Quad.m_Large && (Large || (MinX == Quad.m_MinCellX && MinY == Quad.m_MinCellY && MaxX == Quad.m_MaxCellX && MaxY == Quad.m_MaxCellY)))
			return;
	}
	if(Quad.m_Indexed)
	{
		if(Quad.m_Large)
			m_vLargeQuads.erase(std::lower_bound(m_vLargeQuads.begin(), m_vLargeQuads.end(), Index));
		else
			for(int y = Quad.m_MinCellY; y <= Quad.m_MaxCellY; ++y)
				for(int x = Quad.m_MinCellX; x <= Quad.m_MaxCellX; ++x)
				{
					auto Cell = m_Grid.find(GridKey(x, y));
					auto &vIndices = Cell->second;
					vIndices.erase(std::lower_bound(vIndices.begin(), vIndices.end(), Index));
					if(vIndices.empty())
						m_Grid.erase(Cell);
				}
	}
	Quad.m_Indexed = Quad.m_Active;
	if(!Quad.m_Active)
		return;
	Quad.m_Large = Large;
	Quad.m_MinCellX = MinX;
	Quad.m_MinCellY = MinY;
	Quad.m_MaxCellX = MaxX;
	Quad.m_MaxCellY = MaxY;
	if(Large)
		m_vLargeQuads.insert(std::lower_bound(m_vLargeQuads.begin(), m_vLargeQuads.end(), Index), Index);
	else
		for(int y = MinY; y <= MaxY; ++y)
			for(int x = MinX; x <= MaxX; ++x)
			{
				auto &vIndices = m_Grid[GridKey(x, y)];
				vIndices.insert(std::lower_bound(vIndices.begin(), vIndices.end(), Index), Index);
			}
}

bool CQmAxiomQuadCollision::BoundsOverlap(const CCollisionQuad &Quad, vec2 Min, vec2 Max)
{
	return Quad.m_Active && Quad.m_Min.x <= Max.x && Quad.m_Max.x >= Min.x && Quad.m_Min.y <= Max.y && Quad.m_Max.y >= Min.y;
}

void CQmAxiomQuadCollision::QueryBox(vec2 Min, vec2 Max, std::vector<int> &vIndices) const
{
	vIndices.clear();
	for(int Index : m_vLargeQuads)
		if(BoundsOverlap(m_vQuads[Index], Min, Max))
			vIndices.push_back(Index);
	const int MinX = std::floor(Min.x / GRID_SIZE), MinY = std::floor(Min.y / GRID_SIZE);
	const int MaxX = std::floor(Max.x / GRID_SIZE), MaxY = std::floor(Max.y / GRID_SIZE);
	if((int64_t(MaxX) - MinX + 1) * (int64_t(MaxY) - MinY + 1) > MAX_GRID_CELLS)
	{
		vIndices.clear();
		for(size_t i = 0; i < m_vQuads.size(); ++i)
			if(BoundsOverlap(m_vQuads[i], Min, Max))
				vIndices.push_back(i);
		return;
	}
	for(int y = MinY; y <= MaxY; ++y)
		for(int x = MinX; x <= MaxX; ++x)
		{
			const auto Cell = m_Grid.find(GridKey(x, y));
			if(Cell != m_Grid.end())
				for(int Index : Cell->second)
					if(BoundsOverlap(m_vQuads[Index], Min, Max))
						vIndices.push_back(Index);
		}
	std::sort(vIndices.begin(), vIndices.end());
	vIndices.erase(std::unique(vIndices.begin(), vIndices.end()), vIndices.end());
}

const std::vector<int> &CQmAxiomQuadCollision::QueryPoint(vec2 Pos, std::vector<int> &vFallback) const
{
	if(!m_vLargeQuads.empty())
	{
		QueryBox(Pos, Pos, vFallback);
		return vFallback;
	}
	const auto Cell = m_Grid.find(GridKey(std::floor(Pos.x / GRID_SIZE), std::floor(Pos.y / GRID_SIZE)));
	// 网格索引保持图层顺序，点查询直接借用索引。
	return Cell != m_Grid.end() ? Cell->second : vFallback;
}

bool CQmAxiomQuadCollision::PointInside(vec2 Pos, const std::vector<vec2> &vPolygon)
{
	bool Inside = false;
	for(size_t i = 0, j = vPolygon.size() - 1; i < vPolygon.size(); j = i++)
	{
		const vec2 A = vPolygon[j], B = vPolygon[i];
		const vec2 Edge = B - A;
		if(length(Edge) > EPSILON && std::fabs(Cross(Edge, Pos - A)) <= EPSILON * length(Edge) && dot(Pos - A, Pos - B) <= EPSILON)
			return true;
		if((A.y > Pos.y) != (B.y > Pos.y) && Pos.x < (B.x - A.x) * (Pos.y - A.y) / (B.y - A.y) + A.x)
			Inside = !Inside;
	}
	return Inside;
}

int CQmAxiomQuadCollision::TileAt(vec2 Pos, bool SolidOnly) const
{
	std::vector<int> vIndices;
	for(int Index : QueryPoint(Pos, vIndices))
	{
		const auto &Quad = m_vQuads[Index];
		if((!SolidOnly || Solid(Quad.m_Tile)) && BoundsOverlap(Quad, Pos, Pos) && PointInside(Pos, Quad.m_vPolygon))
			return Quad.m_Tile;
	}
	return 0;
}

bool CQmAxiomQuadCollision::HasTile(vec2 Pos, int Tile) const
{
	std::vector<int> vIndices;
	for(int Index : QueryPoint(Pos, vIndices))
		if(m_vQuads[Index].m_Tile == Tile && BoundsOverlap(m_vQuads[Index], Pos, Pos) && PointInside(Pos, m_vQuads[Index].m_vPolygon))
			return true;
	return false;
}

std::vector<int> CQmAxiomQuadCollision::TilesAt(vec2 Pos) const
{
	std::vector<int> vIndices, vTiles;
	for(int Index : QueryPoint(Pos, vIndices))
		if(BoundsOverlap(m_vQuads[Index], Pos, Pos) && PointInside(Pos, m_vQuads[Index].m_vPolygon))
			vTiles.push_back(m_vQuads[Index].m_Tile);
	return vTiles;
}

int CQmAxiomQuadCollision::TuneZoneAt(vec2 Pos) const
{
	std::vector<int> vIndices;
	for(int Index : QueryPoint(Pos, vIndices))
	{
		const auto &Quad = m_vQuads[Index];
		if(Quad.m_Tile == TILE_TUNE && BoundsOverlap(Quad, Pos, Pos) && PointInside(Pos, Quad.m_vPolygon))
			return Quad.m_Quad.m_ColorEnvOffset;
	}
	return 0;
}

bool CQmAxiomQuadCollision::BoxPushout(vec2 Pos, vec2 HalfSize, const std::vector<vec2> &vPolygon, vec2 *pPush, vec2 *pNormal)
{
	float Best = std::numeric_limits<float>::max();
	vec2 Normal(0, 0);
	// SAT 同时检查盒体坐标轴和所有多边形边法线。
	for(size_t i = 0; i < vPolygon.size() + 2; ++i)
	{
		vec2 Axis;
		if(i < 2)
			Axis = i == 0 ? vec2(1, 0) : vec2(0, 1);
		else
		{
			const size_t EdgeIndex = i - 2;
			const vec2 Edge = vPolygon[(EdgeIndex + 1) % vPolygon.size()] - vPolygon[EdgeIndex];
			if(length(Edge) <= EPSILON)
				continue;
			Axis = normalize(vec2(-Edge.y, Edge.x));
		}
		float Min = dot(Axis, vPolygon.front()), Max = Min;
		for(vec2 Point : vPolygon)
		{
			Min = std::min(Min, dot(Axis, Point));
			Max = std::max(Max, dot(Axis, Point));
		}
		const float Center = dot(Axis, Pos);
		const float Radius = std::fabs(Axis.x) * HalfSize.x + std::fabs(Axis.y) * HalfSize.y;
		const float Neg = Center + Radius - Min, PosDepth = Max - (Center - Radius);
		if(Neg <= EPSILON || PosDepth <= EPSILON)
			return false;
		// 计算完全分离距离，兼容盒体被四边形完全包含的情况。
		const float Depth = std::min(Neg, PosDepth);
		if(Depth < Best)
		{
			Best = Depth;
			Normal = Neg < PosDepth ? -Axis : Axis;
		}
	}
	if(pPush)
		*pPush = Normal * (Best + 0.001f);
	if(pNormal)
		*pNormal = Normal;
	return true;
}

bool CQmAxiomQuadCollision::TestBox(vec2 Pos, vec2 HalfSize) const
{
	std::vector<int> vIndices;
	QueryBox(Pos - HalfSize, Pos + HalfSize, vIndices);
	for(int Index : vIndices)
	{
		const auto &Quad = m_vQuads[Index];
		if(Solid(Quad.m_Tile) && Quad.m_Active && BoxPushout(Pos, HalfSize, Quad.m_vPolygon, nullptr, nullptr))
			return true;
	}
	return false;
}

bool CQmAxiomQuadCollision::Pushout(vec2 Pos, vec2 HalfSize, vec2 *pPush, vec2 *pNormal) const
{
	std::vector<int> vIndices;
	QueryBox(Pos - HalfSize, Pos + HalfSize, vIndices);
	float Deepest = 0;
	for(int Index : vIndices)
	{
		const auto &Quad = m_vQuads[Index];
		vec2 Push, Normal;
		if(Solid(Quad.m_Tile) && Quad.m_Active && BoxPushout(Pos, HalfSize, Quad.m_vPolygon, &Push, &Normal) && length(Push) > Deepest)
		{
			Deepest = length(Push);
			*pPush = Push;
			*pNormal = Normal;
		}
	}
	return Deepest > 0;
}

int CQmAxiomQuadCollision::IntersectLine(vec2 From, vec2 To, float *pFraction, vec2 *pNormal) const
{
	std::vector<int> vIndices;
	QueryBox(vec2(std::min(From.x, To.x), std::min(From.y, To.y)), vec2(std::max(From.x, To.x), std::max(From.y, To.y)), vIndices);
	float Best = 2;
	int Tile = 0;
	vec2 BestNormal(0, 0), Delta = To - From;
	for(int Index : vIndices)
	{
		const auto &Quad = m_vQuads[Index];
		if(!Solid(Quad.m_Tile) || !Quad.m_Active)
			continue;
		if(PointInside(From, Quad.m_vPolygon) && Best > 0)
		{
			Best = 0;
			Tile = Quad.m_Tile;
			BestNormal = length(Delta) > EPSILON ? -normalize(Delta) : vec2(0, -1);
		}
		for(size_t i = 0; i < Quad.m_vPolygon.size(); ++i)
		{
			const vec2 A = Quad.m_vPolygon[i], B = Quad.m_vPolygon[(i + 1) % Quad.m_vPolygon.size()], Edge = B - A;
			const float Denom = Cross(Delta, Edge);
			if(std::fabs(Denom) <= EPSILON)
				continue;
			const float Fraction = Cross(A - From, Edge) / Denom;
			const float AlongEdge = Cross(A - From, Delta) / Denom;
			if(Fraction >= 0 && Fraction <= 1 && AlongEdge >= 0 && AlongEdge <= 1 && Fraction < Best)
			{
				Best = Fraction;
				Tile = Quad.m_Tile;
				BestNormal = normalize(vec2(-Edge.y, Edge.x));
				if(dot(BestNormal, Delta) > 0)
					BestNormal = -BestNormal;
			}
		}
	}
	if(Tile)
	{
		if(pFraction)
			*pFraction = Best;
		if(pNormal)
			*pNormal = BestNormal;
	}
	return Tile;
}

bool CQmAxiomQuadCollision::FindHookAnchor(vec2 Pos, int *pQuadIndex, vec2 *pLocalPos) const
{
	std::vector<int> vIndices;
	QueryBox(Pos - vec2(1, 1), Pos + vec2(1, 1), vIndices);
	for(int Index : vIndices)
	{
		const auto &Quad = m_vQuads[Index];
		if(Quad.m_Tile != TILE_SOLID || !Quad.m_Active)
			continue;
		// 核心会将钩子坐标量化为整数，允许边缘量化误差。
		bool Hit = PointInside(Pos, Quad.m_vPolygon);
		for(vec2 Offset : {vec2(-0.75f, 0), vec2(0.75f, 0), vec2(0, -0.75f), vec2(0, 0.75f)})
			Hit |= PointInside(Pos + Offset, Quad.m_vPolygon);
		if(Hit)
		{
			const vec2 Pivot(fx2f(Quad.m_Quad.m_aPoints[4].x), fx2f(Quad.m_Quad.m_aPoints[4].y));
			*pQuadIndex = Index;
			*pLocalPos = Rotate(Pos - Pivot - Quad.m_Position, -Quad.m_Angle);
			return true;
		}
	}
	return false;
}

bool CQmAxiomQuadCollision::HookAnchor(int QuadIndex, vec2 LocalPos, vec2 *pWorldPos) const
{
	if(QuadIndex < 0 || QuadIndex >= (int)m_vQuads.size())
		return false;
	const auto &Quad = m_vQuads[QuadIndex];
	if(!Quad.m_Active || Quad.m_Tile != TILE_SOLID)
		return false;
	const vec2 Pivot(fx2f(Quad.m_Quad.m_aPoints[4].x), fx2f(Quad.m_Quad.m_aPoints[4].y));
	const vec2 Pos = Pivot + Quad.m_Position + Rotate(LocalPos, Quad.m_Angle);
	if(Quad.m_Clipped && (Pos.x < Quad.m_ClipMin.x || Pos.x > Quad.m_ClipMax.x || Pos.y < Quad.m_ClipMin.y || Pos.y > Quad.m_ClipMax.y))
		return false;
	*pWorldPos = Pos;
	return true;
}
