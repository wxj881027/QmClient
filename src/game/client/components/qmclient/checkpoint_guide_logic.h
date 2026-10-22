#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_LOGIC_H

#include "map_progress.h"

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
			if(Read(Start).m_Tile.m_Cost <= 0)
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
				if(IsReturn(Tile.m_TeleType) || Tile.m_TeleType == TILE_TELEIN || Tile.m_TeleType == TILE_TELEINEVIL)
					continue;
				if(Tile.m_TeleType == TILE_TELECHECK && Tile.m_TeleNumber > 0 && Tile.m_TeleNumber < 256 && Tile.m_TeleNumber != m_Checkpoint)
				{
					if(!m_Collected.test((size_t)Tile.m_TeleNumber))
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
					const int TileCost = NextLabel.m_Tile.m_Cost;
					if(TileCost <= 0 || TileCost > MAX_COST - Cost || Cost + TileCost >= NextLabel.m_Cost)
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
