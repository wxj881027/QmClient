#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_LOGIC_H

#include "map_progress.h"

#include <base/vmath.h>

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <functional>
#include <limits>
#include <tuple>
#include <vector>

namespace QmCheckpointGuide
{
	// 只搜索身边的路段；遇到会改变房间或 CP 的格子时停止继续展开。
	constexpr int SEARCH_RADIUS = 32;
	constexpr int MAX_COST = SEARCH_RADIUS * 100;
	constexpr int WARNING_COST = 6 * 100;

	struct SResult
	{
		int m_Checkpoint = 0;
		int m_TargetIndex = -1;
		bool m_Warning = false;
		std::vector<int> m_vRoute;

		// 线段固定在地图上，只裁掉角色脚下的起始部分。
		template<typename FGetPos, typename FDraw>
		void VisitSegments(std::vector<int>::const_iterator Entry, vec2 StartCenter, vec2 Position, float Radius, FGetPos GetPos, FDraw Draw) const
		{
			vec2 From = StartCenter;
			const float RadiusSquared = Radius * Radius;
			bool Clipping = distance_squared(From, Position) <= RadiusSquared;
			for(auto It = Entry; It != m_vRoute.end(); ++It)
			{
				const vec2 To = GetPos(*It);
				if(Clipping && distance_squared(To, Position) <= RadiusSquared)
				{
					From = To;
					continue;
				}
				if(Clipping)
				{
					// 起点在圆内、终点在圆外，沿线段方向的最后一个交点就是出圆点。
					vec2 aHits[2];
					const int Hits = intersect_line_circle(From, To, Position, Radius, aHits);
					if(Hits > 0)
						From = aHits[Hits - 1];
					Clipping = false;
				}
				if(distance_squared(From, To) > 0.01f)
					Draw(From, To);
				From = To;
			}
		}
	};

	class CPlanner
	{
		struct SLabel
		{
			QmMapProgress::STile m_Tile;
			int m_Cost = std::numeric_limits<int>::max();
			int m_Previous = -1;
			bool m_Read = false;
		};
		using TNode = std::tuple<int, int>;
		std::vector<SLabel> m_vLabels;
		std::vector<TNode> m_vQueue;
		std::vector<int> m_vCheckpointHistory;
		std::bitset<256> m_Collected;
		int m_Checkpoint = -1;
		SResult m_Result;

		static bool IsReturn(int Type)
		{
			return Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL;
		}

		bool StopsAt(const QmMapProgress::STile &Tile) const
		{
			return IsReturn(Tile.m_TeleType) || Tile.m_TeleType == TILE_TELEIN || Tile.m_TeleType == TILE_TELEINEVIL ||
				(Tile.m_TeleType == TILE_TELECHECK && Tile.m_TeleNumber > 0 && Tile.m_TeleNumber < 256 && Tile.m_TeleNumber != m_Checkpoint);
		}

	public:
		void Reset()
		{
			m_Collected.reset();
			m_vCheckpointHistory.clear();
			m_Checkpoint = -1;
			ClearRoute();
		}

		void ClearRoute()
		{
			m_Result.m_Checkpoint = 0;
			m_Result.m_TargetIndex = -1;
			m_Result.m_Warning = false;
			m_Result.m_vRoute.clear();
		}

		void ObserveCheckpoint(int Checkpoint)
		{
			if(Checkpoint < 0 || Checkpoint > 255)
				Checkpoint = -1;
			if(Checkpoint == m_Checkpoint)
				return;
			if(Checkpoint <= 0)
			{
				m_Collected.reset();
				m_vCheckpointHistory.clear();
			}
			else
			{
				// 回到旧 CP 会覆盖存档点；允许重新引导这之后吃过的 CP。
				const auto Previous = std::find(m_vCheckpointHistory.begin(), m_vCheckpointHistory.end(), Checkpoint);
				if(Previous != m_vCheckpointHistory.end())
				{
					for(auto It = Previous + 1; It != m_vCheckpointHistory.end(); ++It)
						m_Collected.reset((size_t)*It);
					m_vCheckpointHistory.erase(Previous + 1, m_vCheckpointHistory.end());
				}
				else
					m_vCheckpointHistory.push_back(Checkpoint);
				m_Collected.set((size_t)Checkpoint);
			}
			m_Checkpoint = Checkpoint;
			ClearRoute();
		}

		int Checkpoint() const { return m_Checkpoint; }
		const SResult &Result() const { return m_Result; }

		template<typename FReadTile>
		std::vector<int>::const_iterator FindRouteEntry(int Width, int StartIndex, FReadTile ReadTile) const
		{
			const auto &vRoute = m_Result.m_vRoute;
			const auto Current = std::find(vRoute.begin(), vRoute.end(), StartIndex);
			if(Current != vRoute.end() || vRoute.empty() || Width <= 0 || StartIndex < 0)
				return Current;

			// 切弯时可能短暂经过路线旁的一格，只接回当前能直接走到的相邻格。
			const auto Tile = ReadTile(StartIndex);
			if(Tile.Blocked() || StopsAt(Tile))
				return vRoute.end();
			for(auto It = vRoute.end(); It != vRoute.begin();)
			{
				--It;
				const int Dx = *It % Width - StartIndex % Width;
				const int Dy = *It / Width - StartIndex / Width;
				if((Dx == 0 && Dy == -1 && !(Tile.m_Restrictions & CANTMOVE_UP)) ||
					(Dx == 0 && Dy == 1 && !(Tile.m_Restrictions & CANTMOVE_DOWN)) ||
					(Dy == 0 && Dx == -1 && !(Tile.m_Restrictions & CANTMOVE_LEFT)) ||
					(Dy == 0 && Dx == 1 && !(Tile.m_Restrictions & CANTMOVE_RIGHT)))
					return It;
			}
			return vRoute.end();
		}

		// 读取回调与地图进度共用 STile/MakeTile，生产端实时补入当前角色的阻挡状态。
		template<typename FReadTile>
		void Update(int Width, int Height, int StartIndex, FReadTile &&ReadTile)
		{
			const int PreviousTarget = m_Result.m_TargetIndex;
			ClearRoute();
			const int64_t Size = (int64_t)Width * Height;
			if(m_Checkpoint < 0 || Width <= 0 || Height <= 0 || Size > std::numeric_limits<int>::max() || StartIndex < 0 || StartIndex >= Size)
				return;
			const int StartX = StartIndex % Width;
			const int StartY = StartIndex / Width;
			const int Left = std::max(0, StartX - SEARCH_RADIUS);
			const int Top = std::max(0, StartY - SEARCH_RADIUS);
			const int Right = (int)std::min<int64_t>(Width - 1, (int64_t)StartX + SEARCH_RADIUS);
			const int Bottom = (int)std::min<int64_t>(Height - 1, (int64_t)StartY + SEARCH_RADIUS);
			const int LocalWidth = Right - Left + 1;
			const int LocalHeight = Bottom - Top + 1;
			m_vLabels.assign((size_t)LocalWidth * LocalHeight, {});
			m_vQueue.clear();
			const auto GlobalIndex = [&](int Index) { return (Top + Index / LocalWidth) * Width + Left + Index % LocalWidth; };
			const auto Read = [&](int Index) -> SLabel & {
				auto &Label = m_vLabels[(size_t)Index];
				if(!Label.m_Read)
				{
					Label.m_Tile = ReadTile(GlobalIndex(Index));
					Label.m_Read = true;
				}
				return Label;
			};
			const int Start = (StartY - Top) * LocalWidth + StartX - Left;
			if(Read(Start).m_Tile.Blocked())
				return;
			m_vLabels[(size_t)Start].m_Cost = 0;
			m_vQueue.emplace_back(0, Start);
			int Target = -1;
			int StickyTarget = -1;
			int ReturnCost = MAX_COST + 1;
			while(!m_vQueue.empty())
			{
				std::pop_heap(m_vQueue.begin(), m_vQueue.end(), std::greater<TNode>());
				const auto [Cost, Index] = m_vQueue.back();
				m_vQueue.pop_back();
				const auto &Label = m_vLabels[(size_t)Index];
				if(Cost != Label.m_Cost)
					continue;
				const auto &Tile = Label.m_Tile;
				if(IsReturn(Tile.m_TeleType))
					ReturnCost = std::min(ReturnCost, Cost);
				if(StopsAt(Tile))
				{
					if(Tile.m_TeleType == TILE_TELECHECK && !m_Collected.test((size_t)Tile.m_TeleNumber))
					{
						if(Target < 0 || Cost < m_vLabels[(size_t)Target].m_Cost)
							Target = Index;
						if(GlobalIndex(Index) == PreviousTarget)
							StickyTarget = Index;
					}
					continue;
				}
				const int X = Index % LocalWidth;
				const int aNext[] = {X > 0 ? Index - 1 : -1, X + 1 < LocalWidth ? Index + 1 : -1, Index - LocalWidth, Index + LocalWidth};
				const int aRestriction[] = {CANTMOVE_LEFT, CANTMOVE_RIGHT, CANTMOVE_UP, CANTMOVE_DOWN};
				for(int Direction = 0; Direction < 4; ++Direction)
				{
					const int Next = aNext[Direction];
					if(Next < 0 || Next >= (int)m_vLabels.size() || (Tile.m_Restrictions & aRestriction[Direction]) != 0)
						continue;
					auto &NextLabel = Read(Next);
					if(NextLabel.m_Tile.Blocked())
						continue;
					const int TileCost = NextLabel.m_Tile.m_Water ? 300 : 100;
					if(TileCost > MAX_COST - Cost || Cost + TileCost >= NextLabel.m_Cost)
						continue;
					NextLabel.m_Cost = Cost + TileCost;
					NextLabel.m_Previous = Index;
					m_vQueue.emplace_back(NextLabel.m_Cost, Next);
					std::push_heap(m_vQueue.begin(), m_vQueue.end(), std::greater<TNode>());
				}
			}
			if(Target < 0)
				return;
			// 代价接近时保留目标，避免玩家在两个 CP 之间移动时引导线反复跳动。
			if(StickyTarget >= 0 && m_vLabels[(size_t)StickyTarget].m_Cost <= m_vLabels[(size_t)Target].m_Cost + 200)
				Target = StickyTarget;
			m_Result.m_Checkpoint = m_vLabels[(size_t)Target].m_Tile.m_TeleNumber;
			m_Result.m_TargetIndex = GlobalIndex(Target);
			m_Result.m_Warning = ReturnCost <= WARNING_COST;
			for(int Index = Target; Index >= 0; Index = m_vLabels[(size_t)Index].m_Previous)
				m_Result.m_vRoute.push_back(GlobalIndex(Index));
			std::reverse(m_Result.m_vRoute.begin(), m_Result.m_vRoute.end());
		}
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_LOGIC_H
