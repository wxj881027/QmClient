#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_START_DETECTION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_START_DETECTION_H

#include "collision_position.h"

#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <vector>

namespace qm_race_start
{

	inline constexpr int SHORT_SCAN_MAX_SAMPLES = 4096;

	// 同步借用碰撞层；消费迭代器期间不能换图或修改碰撞层。
	class CTileStartMapIndexScan
	{
		const CCollision &m_Collision;
		vec2 m_Prev;
		vec2 m_Pos;
		int m_Width;
		int m_Height;
		float m_Distance = 0.0f;
		int m_End = 0;
		int m_Sample = 0;
		int m_Cell = 0;
		int m_LastCell = 0;
		int m_LastAcceptedIndex = 0;
		bool m_Valid = false;
		bool m_AdvancePending = false;
		uint64_t m_SampleEvaluations = 0;
		uint64_t m_CellGroups = 0;

		vec2 SamplePosition(int Sample)
		{
			++m_SampleEvaluations;
			if(!m_Distance)
				return m_Pos;
			// 与GetMapIndices保持相同的float转换和运算顺序，尾样本的a可能大于1。
			const float Amount = static_cast<float>(Sample) / m_Distance;
			return mix(m_Prev, m_Pos, Amount);
		}

		int MapIndex(vec2 Pos) const
		{
			const int X = std::clamp(static_cast<int>(Pos.x) / 32, 0, m_Width - 1);
			const int Y = std::clamp(static_cast<int>(Pos.y) / 32, 0, m_Height - 1);
			return Y * m_Width + X;
		}

		int SampleIndex(int Sample)
		{
			return MapIndex(SamplePosition(Sample));
		}

		void AdvanceCell()
		{
			if(m_Cell == m_LastCell)
			{
				m_Sample = m_End;
				return;
			}
			// 各轴单调，离开当前cell后不会返回；index数值本身不要求单调。
			int Low = m_Sample + 1;
			int High = m_End;
			while(Low < High)
			{
				const int Middle = Low + (High - Low) / 2;
				if(SampleIndex(Middle) == m_Cell)
					Low = Middle + 1;
				else
					High = Middle;
			}
			m_Sample = Low;
			if(m_Sample < m_End)
				m_Cell = SampleIndex(m_Sample);
		}

	public:
		CTileStartMapIndexScan(const CCollision &Collision, vec2 Prev, vec2 Pos) :
			m_Collision(Collision), m_Prev(Prev), m_Pos(Pos), m_Width(Collision.GetWidth()), m_Height(Collision.GetHeight())
		{
			if(!Collision.GameLayer() || m_Width <= 0 || m_Height <= 0 || !QmCollision::CanQueryLine(Prev, Pos))
				return;
			m_Distance = distance(Prev, Pos);
			m_End = static_cast<int>(m_Distance + 1.0f);
			const vec2 First = SamplePosition(0);
			const vec2 Last = m_End > 1 ? SamplePosition(m_End - 1) : First;
			// 首末实际样本可转换，结合逐轴单调性保证二分中的截断转换也安全。
			if(!QmCollision::CanRoundPosition(First) || !QmCollision::CanRoundPosition(Last))
				return;
			m_Cell = MapIndex(First);
			m_LastCell = MapIndex(Last);
			m_Valid = true;
		}

		bool IsValid() const { return m_Valid; }
		int EndSampleCount() const { return m_End; }
		// 包含首末准入和二分求值；缓存的cell复用不重复计数。
		uint64_t SampleEvaluations() const { return m_SampleEvaluations; }
		uint64_t CellGroups() const { return m_CellGroups; }

		bool NextAcceptedIndex(int &Index)
		{
			while(m_Valid && m_Sample < m_End)
			{
				if(m_AdvancePending)
				{
					AdvanceCell();
					if(m_Sample == m_End)
						break;
				}
				m_AdvancePending = true;
				++m_CellGroups;
				// 零距离的原分支不使用LastIndex过滤，包含index 0。
				if(m_Collision.TileExists(m_Cell) && (!m_Distance || m_LastAcceptedIndex != m_Cell))
				{
					m_LastAcceptedIndex = m_Cell;
					Index = m_Cell;
					return true;
				}
			}
			return false;
		}
	};

	inline bool IsTileStart(const CCollision &Collision, vec2 Prev, vec2 Pos)
	{
		try
		{
			CTileStartMapIndexScan Scan(Collision, Prev, Pos);
			if(!Scan.IsValid())
				return false;
			const auto IsStartIndex = [&](int Index) {
				return Collision.GetTileIndex(Index) == TILE_START || Collision.GetFrontTileIndex(Index) == TILE_START;
			};
			bool AnyAccepted = false;
			if(Scan.EndSampleCount() <= SHORT_SCAN_MAX_SAMPLES)
			{
				// 短线保留原扫描与分配失败回退。
				const std::vector<int> vIndices = Collision.GetMapIndices(Prev, Pos);
				AnyAccepted = !vIndices.empty();
				for(const int Index : vIndices)
				{
					if(IsStartIndex(Index))
						return true;
				}
			}
			else
			{
				int Index;
				while(Scan.NextAcceptedIndex(Index))
				{
					AnyAccepted = true;
					if(IsStartIndex(Index))
						return true;
				}
			}
			if(!AnyAccepted)
				return IsStartIndex(Collision.GetPureMapIndex(Pos));
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		return false;
	}
}

#endif
