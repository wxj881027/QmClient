#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_INTERSECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_INTERSECTION_H

#include <base/vmath.h>

#include <algorithm>
#include <vector>

// 仅用于提示线，复用每段固定几何量，不参与实际钩子或角色预测。
struct SQmHookCollSegment
{
	vec2 m_Start;
	vec2 m_Delta;
	float m_LengthSquared;
	vec2 m_Min;
	vec2 m_Max;

	SQmHookCollSegment(vec2 Start, vec2 End, float Radius) :
		m_Start(Start),
		m_Delta(End - Start),
		m_LengthSquared(dot(m_Delta, m_Delta))
	{
		// 与最近点计算使用相同的重建终点；额外一单位只放宽粗筛，不改变命中半径。
		const vec2 ProjectedEnd = m_Start + m_Delta;
		const float Padding = Radius + 1.0f;
		m_Min = vec2(std::min(Start.x, ProjectedEnd.x) - Padding, std::min(Start.y, ProjectedEnd.y) - Padding);
		m_Max = vec2(std::max(Start.x, ProjectedEnd.x) + Padding, std::max(Start.y, ProjectedEnd.y) + Padding);
	}

	bool MayIntersect(vec2 Position) const
	{
		return !(Position.x < m_Min.x || Position.x > m_Max.x || Position.y < m_Min.y || Position.y > m_Max.y);
	}

	vec2 ClosestPoint(vec2 Position) const
	{
		// 保持 closest_point_on_line 的浮点运算顺序，不用倒数乘法替代除法。
		const vec2 Offset = Position - m_Start;
		const float Projection = dot(Offset, m_Delta) / m_LengthSquared;
		return m_Start + m_Delta * std::clamp(Projection, 0.0f, 1.0f);
	}
};

template<typename F>
int QmIntersectHookCollTargets(const SQmHookCollSegment &Segment, vec2 &Hit, const std::vector<int> &vCandidates, F &&PositionOf, float Radius, vec2 *pPlayerPosition = nullptr)
{
	if(!(Segment.m_LengthSquared > 0.0f))
		return -1;

	float Distance = 0.0f;
	int ClosestId = -1;
	for(const int Id : vCandidates)
	{
		const vec2 Position = PositionOf(Id);
		if(!Segment.MayIntersect(Position))
			continue;

		const vec2 ClosestPoint = Segment.ClosestPoint(Position);
		if(distance(Position, ClosestPoint) < Radius)
		{
			const float CandidateDistance = distance(Segment.m_Start, Position);
			// 同距离保留先出现的候选；未命中时不写输出参数。
			if(ClosestId == -1 || CandidateDistance < Distance)
			{
				Hit = ClosestPoint;
				ClosestId = Id;
				Distance = CandidateDistance;
				if(pPlayerPosition)
					*pPlayerPosition = Position;
			}
		}
	}
	return ClosestId;
}

template<typename F>
int QmIntersectHookCollTargets(vec2 Start, vec2 End, vec2 &Hit, const std::vector<int> &vCandidates, F &&PositionOf, float Radius, vec2 *pPlayerPosition = nullptr)
{
	return QmIntersectHookCollTargets(SQmHookCollSegment(Start, End, Radius), Hit, vCandidates, PositionOf, Radius, pPlayerPosition);
}

#endif
