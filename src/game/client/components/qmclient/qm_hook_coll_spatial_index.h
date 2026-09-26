#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_SPATIAL_INDEX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_SPATIAL_INDEX_H

#include <engine/shared/protocol.h>

#include <game/client/components/qmclient/qm_hook_coll_intersection.h>

#include <algorithm>
#include <array>
#include <vector>

// 提示线在同一轮绘制内共享位置索引；只缩小几何候选，不改变队伍/solo/super 资格。
class CQmHookCollSpatialIndex
{
	struct SEntry
	{
		float m_Coordinate;
		int m_Id;
	};
	std::array<vec2, MAX_CLIENTS> m_aPositions;
	std::array<std::array<SEntry, MAX_CLIENTS>, 2> m_aaSorted;
	int m_Count = 0;
	size_t m_PendingCandidates = 0;
	bool m_Ready = false;
	std::vector<int> m_vNearby;

public:
	void Reset()
	{
		m_Ready = false;
		m_PendingCandidates = 0;
	}

	// vAllowed 由 CQmHookCollCandidates 生成，按客户端编号递增；返回值只使用到下次查询。
	// 快路径原样返回入参引用，调用方不会传入临时量：返回地址合同由 ddnet_19_9_sync_test.cpp 固定
	// （GetCandidates 的返回值必须与 vAllowed 同地址），因此这里保留返回常量引用。
	template<typename P, typename V>
	const std::vector<int> &GetCandidates(const SQmHookCollSegment &Segment, const std::vector<int> &vAllowed, P &&PositionOf, V &&Valid)
	{
		// 少量候选直接扫描，避免为每段的二分查找与名单过滤付出更多成本。
		if(vAllowed.size() < 16)
			return vAllowed; // NOLINT(bugprone-return-const-ref-from-parameter)

		if(!m_Ready)
		{
			// 单条短提示线沿用直接粗筛；本帧查询足够多时才摊销两轴排序成本。
			m_PendingCandidates += vAllowed.size();
			if(m_PendingCandidates <= MAX_CLIENTS * 8)
				return vAllowed; // NOLINT(bugprone-return-const-ref-from-parameter)
			m_Count = 0;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				if(!Valid(Id))
					continue;
				m_aPositions[Id] = PositionOf(Id);
				m_aaSorted[0][m_Count] = {m_aPositions[Id].x, Id};
				m_aaSorted[1][m_Count] = {m_aPositions[Id].y, Id};
				++m_Count;
			}
			for(auto &aSorted : m_aaSorted)
				std::sort(aSorted.begin(), aSorted.begin() + m_Count, [](const SEntry &Left, const SEntry &Right) { return Left.m_Coordinate < Right.m_Coordinate; });
			m_Ready = true;
		}

		// 整群玩家都落在这一段包围框内时直接扫描，省掉四次二分查询。
		if(m_Count > 0 &&
			Segment.m_Min.x <= m_aaSorted[0][0].m_Coordinate && Segment.m_Max.x >= m_aaSorted[0][m_Count - 1].m_Coordinate &&
			Segment.m_Min.y <= m_aaSorted[1][0].m_Coordinate && Segment.m_Max.y >= m_aaSorted[1][m_Count - 1].m_Coordinate)
			return vAllowed; // NOLINT(bugprone-return-const-ref-from-parameter)

		int aBegin[2], aEnd[2];
		for(int Axis = 0; Axis < 2; ++Axis)
		{
			const float Min = Axis == 0 ? Segment.m_Min.x : Segment.m_Min.y;
			const float Max = Axis == 0 ? Segment.m_Max.x : Segment.m_Max.y;
			const auto *pBegin = m_aaSorted[Axis].data();
			const auto *pEnd = pBegin + m_Count;
			aBegin[Axis] = (int)(std::lower_bound(pBegin, pEnd, Min, [](const SEntry &Entry, float Coordinate) { return Entry.m_Coordinate < Coordinate; }) - pBegin);
			aEnd[Axis] = (int)(std::upper_bound(pBegin + aBegin[Axis], pEnd, Max, [](float Coordinate, const SEntry &Entry) { return Coordinate < Entry.m_Coordinate; }) - pBegin);
		}
		const int Axis = aEnd[0] - aBegin[0] <= aEnd[1] - aBegin[1] ? 0 : 1;
		// 两轴都很密集时直接使用已有名单，不再做名单排序或逐项资格查找。
		if(aEnd[Axis] - aBegin[Axis] >= (int)vAllowed.size() / 2)
			return vAllowed; // NOLINT(bugprone-return-const-ref-from-parameter)

		m_vNearby.clear();
		for(int Index = aBegin[Axis]; Index < aEnd[Axis]; ++Index)
		{
			const int Id = m_aaSorted[Axis][Index].m_Id;
			if(Segment.MayIntersect(m_aPositions[Id]) && std::binary_search(vAllowed.begin(), vAllowed.end(), Id))
				m_vNearby.push_back(Id);
		}
		// 空间顺序不能改变同距离时的命中者，恢复原名单的客户端编号顺序。
		std::sort(m_vNearby.begin(), m_vNearby.end());
		return m_vNearby;
	}
};

#endif
