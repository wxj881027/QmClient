#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PROGRESS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PROGRESS_H

#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <queue>
#include <tuple>
#include <vector>

namespace QmMapProgress
{
	constexpr int NUM_TIME_CHECKPOINTS = TILE_TIME_CHECKPOINT_LAST - TILE_TIME_CHECKPOINT_FIRST + 1;
	constexpr int INF = std::numeric_limits<int>::max();

	struct STile
	{
		int m_Cost = 100;
		int m_Restrictions = 0;
		int m_TimeCheckpoint = -1;
		int m_TeleType = 0;
		int m_TeleNumber = 0;
		bool m_Start = false;
		bool m_Finish = false;
		bool m_Spawn = false;
	};

	inline STile MakeTile(int Game, int Front = TILE_AIR, int TeleType = 0, int TeleNumber = 0, int Restrictions = 0)
	{
		STile Tile;
		Tile.m_Restrictions = Restrictions;
		Tile.m_TeleType = TeleType;
		Tile.m_TeleNumber = TeleNumber;
		Tile.m_Start = Game == TILE_START || Front == TILE_START;
		Tile.m_Finish = Game == TILE_FINISH || Front == TILE_FINISH;
		Tile.m_Spawn = Game == ENTITY_OFFSET + ENTITY_SPAWN;
		for(const int Type : {Game, Front})
		{
			if(Type == TILE_SOLID || Type == TILE_NOHOOK || Type == TILE_DEATH)
				Tile.m_Cost = 0;
			else if(Tile.m_Cost != 0 && (Type == TILE_FREEZE || Type == TILE_DFREEZE || Type == TILE_LFREEZE))
				Tile.m_Cost = 300;
			if(Type >= TILE_TIME_CHECKPOINT_FIRST && Type <= TILE_TIME_CHECKPOINT_LAST)
				Tile.m_TimeCheckpoint = Type - TILE_TIME_CHECKPOINT_FIRST;
		}
		return Tile;
	}

	class CMap
	{
		friend class CField;
		std::vector<STile> m_vTiles;
		std::vector<int> m_vStarts;
		std::vector<int> m_vFinishes;
		std::vector<int> m_vSpawns;
		std::array<std::vector<int>, NUM_TIME_CHECKPOINTS> m_avTimeCheckpoints;
		std::array<std::vector<int>, 256> m_avTeleInputs;
		std::array<std::vector<int>, 256> m_avTeleOutputs;
		std::array<std::vector<int>, 256> m_avCheckOutputs;
		std::vector<int> m_vCheckInputs;
		int m_Width = 0;
		int m_ScanCursor = 0;
		int m_CheckpointCount = 0;
		bool m_Finalized = false;

	public:
		CMap() = default;
		CMap(int Width, int Height)
		{
			const int64_t Size = (int64_t)Width * Height;
			if(Width > 0 && Height > 0 && Size <= INF)
			{
				m_Width = Width;
				m_vTiles.resize((size_t)Size);
			}
		}

		int Size() const { return (int)m_vTiles.size(); }
		const STile &Tile(int Index) const { return m_vTiles[(size_t)Index]; }
		void SetTile(int Index, const STile &Tile) { m_vTiles[(size_t)Index] = Tile; }
		bool Finalized() const { return m_Finalized; }
		bool Ready() const { return m_Finalized && !m_vStarts.empty() && !m_vFinishes.empty(); }
		int CheckpointCount() const { return m_CheckpointCount; }

		void FinalizeStep(int Budget)
		{
			if(m_Finalized)
				return;
			while(Budget-- > 0 && m_ScanCursor < Size())
			{
				const int Index = m_ScanCursor++;
				const STile &Cell = Tile(Index);
				if(Cell.m_Cost == 0)
					continue;
				if(Cell.m_Start)
					m_vStarts.push_back(Index);
				if(Cell.m_Finish)
					m_vFinishes.push_back(Index);
				if(Cell.m_Spawn)
					m_vSpawns.push_back(Index);
				if(Cell.m_TimeCheckpoint >= 0)
					m_avTimeCheckpoints[(size_t)Cell.m_TimeCheckpoint].push_back(Index);
				const int Number = Cell.m_TeleNumber;
				if(Cell.m_TeleType == TILE_TELECHECKIN || Cell.m_TeleType == TILE_TELECHECKINEVIL)
					m_vCheckInputs.push_back(Index);
				if(Number <= 0 || Number >= 256)
					continue;
				if(Cell.m_TeleType == TILE_TELEIN || Cell.m_TeleType == TILE_TELEINEVIL)
					m_avTeleInputs[(size_t)Number].push_back(Index);
				else if(Cell.m_TeleType == TILE_TELEOUT)
					m_avTeleOutputs[(size_t)Number].push_back(Index);
				else if(Cell.m_TeleType == TILE_TELECHECKOUT)
					m_avCheckOutputs[(size_t)Number].push_back(Index);
			}
			if(m_ScanCursor != Size())
				return;
			m_Finalized = true;
			bool Gap = false;
			for(const auto &vCheckpoint : m_avTimeCheckpoints)
			{
				if(vCheckpoint.empty())
					Gap = true;
				else if(Gap)
				{
					m_CheckpointCount = 0;
					break;
				}
				else
					++m_CheckpointCount;
			}
		}

		void Finalize()
		{
			while(!Finalized())
				FinalizeStep(4096);
		}

		const std::vector<int> &Sources(int Stage) const
		{
			return Stage <= 0 ? m_vStarts : m_avTimeCheckpoints[(size_t)Stage - 1];
		}

		const std::vector<int> &Targets(int Stage) const
		{
			return Stage < 0 || Stage == m_CheckpointCount ? m_vFinishes : m_avTimeCheckpoints[(size_t)Stage];
		}
	};

	class CField
	{
		struct SLabel
		{
			int m_Cost = INF;
			int m_Length = INF;
			int m_Next = -1;
			bool m_Settled = false;
		};
		using TNode = std::tuple<int, int, int>;
		std::vector<SLabel> m_vLabels;
		std::priority_queue<TNode, std::vector<TNode>, std::greater<TNode>> m_Queue;
		std::array<const std::vector<int> *, 2> m_apPending = {};
		int m_PendingSlot = 0;
		int m_PendingCursor = 0;
		int m_PendingTarget = -1;
		int m_TeleCheckpoint = -1;
		int m_CheckOutputNumber = 0;
		int m_Stage = -2;

		bool Allowed(const CMap &Map, int Index) const
		{
			const STile &Cell = Map.Tile(Index);
			if(Cell.m_Cost == 0)
				return false;
			if(m_Stage < 0)
				return true;
			if(Cell.m_Finish && m_Stage != Map.CheckpointCount())
				return false;
			return Cell.m_TimeCheckpoint < 0 || Cell.m_TimeCheckpoint == m_Stage || Cell.m_TimeCheckpoint == m_Stage - 1;
		}

		void Relax(int Index, int Target, int Cost, int Length)
		{
			const auto &Next = m_vLabels[(size_t)Target];
			if(Next.m_Cost > INF - Cost || Next.m_Length > INF - Length)
				return;
			auto &Label = m_vLabels[(size_t)Index];
			const int NewCost = Next.m_Cost + Cost;
			const int NewLength = Next.m_Length + Length;
			if(NewCost > Label.m_Cost || (NewCost == Label.m_Cost && NewLength >= Label.m_Length))
				return;
			Label.m_Cost = NewCost;
			Label.m_Length = NewLength;
			Label.m_Next = Target;
			m_Queue.emplace(NewCost, NewLength, Index);
		}

		bool ForcesTeleport(const CMap &Map, const STile &Cell) const
		{
			if(Cell.m_TeleType == TILE_TELECHECKIN || Cell.m_TeleType == TILE_TELECHECKINEVIL)
				return true;
			return (Cell.m_TeleType == TILE_TELEIN || Cell.m_TeleType == TILE_TELEINEVIL) &&
			       Cell.m_TeleNumber > 0 && Cell.m_TeleNumber < 256 &&
			       !Map.m_avTeleOutputs[(size_t)Cell.m_TeleNumber].empty();
		}

	public:
		bool Matches(int TeleCheckpoint, int Stage) const { return m_TeleCheckpoint == TeleCheckpoint && m_Stage == Stage; }
		bool Complete() const { return m_Queue.empty() && m_apPending[0] == nullptr && m_apPending[1] == nullptr; }
		bool Settled(int Index) const { return Index >= 0 && Index < (int)m_vLabels.size() && m_vLabels[(size_t)Index].m_Settled; }
		int Length(int Index) const { return Settled(Index) ? m_vLabels[(size_t)Index].m_Length : -1; }
		int Cost(int Index) const { return Settled(Index) ? m_vLabels[(size_t)Index].m_Cost : -1; }
		int Next(int Index) const { return Settled(Index) ? m_vLabels[(size_t)Index].m_Next : -1; }

		void Start(const CMap &Map, int TeleCheckpoint, int Stage = -1)
		{
			m_vLabels.assign((size_t)Map.Size(), {});
			m_Queue = {};
			m_apPending = {};
			m_PendingSlot = 0;
			m_PendingCursor = 0;
			m_PendingTarget = -1;
			m_TeleCheckpoint = TeleCheckpoint;
			m_Stage = Stage;
			// 与角色回传一致：向下找最近的 CTO，没有 CTO 时使用出生点。
			m_CheckOutputNumber = std::clamp(TeleCheckpoint, 0, 255);
			while(m_CheckOutputNumber > 0 && Map.m_avCheckOutputs[(size_t)m_CheckOutputNumber].empty())
				--m_CheckOutputNumber;
			for(const int Index : Map.Targets(Stage))
			{
				if(!Allowed(Map, Index))
					continue;
				auto &Label = m_vLabels[(size_t)Index];
				Label.m_Cost = 0;
				Label.m_Length = 0;
				m_Queue.emplace(0, 0, Index);
			}
		}

		void Step(const CMap &Map, int Budget)
		{
			while(Budget > 0)
			{
				if(m_PendingSlot < 2 && m_apPending[(size_t)m_PendingSlot])
				{
					const auto &vInputs = *m_apPending[(size_t)m_PendingSlot];
					while(Budget > 0 && m_PendingCursor < (int)vInputs.size())
					{
						--Budget;
						const int Index = vInputs[(size_t)m_PendingCursor++];
						if(Allowed(Map, Index))
							Relax(Index, m_PendingTarget, 0, 0);
					}
					if(m_PendingCursor < (int)vInputs.size())
						return;
					m_apPending[(size_t)m_PendingSlot++] = nullptr;
					m_PendingCursor = 0;
					continue;
				}
				if(m_PendingSlot == 0 && m_apPending[1])
				{
					m_PendingSlot = 1;
					continue;
				}
				if(m_Queue.empty())
					return;
				--Budget;
				const auto [Cost, Length, Index] = m_Queue.top();
				m_Queue.pop();
				auto &Label = m_vLabels[(size_t)Index];
				if(Label.m_Settled || Cost != Label.m_Cost || Length != Label.m_Length)
					continue;
				Label.m_Settled = true;
				const STile &Cell = Map.Tile(Index);
				const int X = Index % Map.m_Width;
				const int aPred[4] = {X > 0 ? Index - 1 : -1, X + 1 < Map.m_Width ? Index + 1 : -1, Index - Map.m_Width, Index + Map.m_Width};
				const int aDirection[4] = {CANTMOVE_RIGHT, CANTMOVE_LEFT, CANTMOVE_DOWN, CANTMOVE_UP};
				for(int Direction = 0; Direction < 4; ++Direction)
				{
					const int Pred = aPred[Direction];
					if(Pred < 0 || Pred >= Map.Size() || !Allowed(Map, Pred))
						continue;
					const STile &From = Map.Tile(Pred);
					if((From.m_Restrictions & aDirection[Direction]) == 0 && !ForcesTeleport(Map, From))
						Relax(Pred, Index, Cell.m_Cost, 1);
				}

				m_apPending = {};
				m_PendingSlot = 0;
				m_PendingCursor = 0;
				m_PendingTarget = Index;
				if(Cell.m_TeleType == TILE_TELEOUT && Cell.m_TeleNumber > 0 && Cell.m_TeleNumber < 256)
					m_apPending[0] = &Map.m_avTeleInputs[(size_t)Cell.m_TeleNumber];
				if((m_CheckOutputNumber > 0 && Cell.m_TeleType == TILE_TELECHECKOUT && Cell.m_TeleNumber == m_CheckOutputNumber) ||
					(m_CheckOutputNumber == 0 && Cell.m_Spawn))
					m_apPending[1] = &Map.m_vCheckInputs;
			}
		}

		int Source(const std::vector<int> &vSources, int ObservedSource) const
		{
			if(ObservedSource >= 0)
				return Settled(ObservedSource) ? ObservedSource : -1;
			int Best = -1;
			for(const int Index : vSources)
			{
				if(Settled(Index) && (Best < 0 || Cost(Index) < Cost(Best)))
					Best = Index;
			}
			return Best;
		}

		bool BuildRoute(int Index, std::vector<int> &vRoute) const
		{
			vRoute.clear();
			for(size_t Step = 0; Step < m_vLabels.size() && Settled(Index); ++Step)
			{
				vRoute.push_back(Index);
				Index = Next(Index);
				if(Index < 0)
					return true;
			}
			return false;
		}
	};

	struct SEstimate
	{
		bool m_Valid = false;
		float m_Progress = 0.0f;
	};

	class CPlayer
	{
		CField m_Global;
		CField m_Segment;
		SEstimate m_Estimate;
		int m_Stage = -1;
		int m_StartIndex = -1;
		int m_SourceIndex = -1;
		int m_CurrentIndex = -1;
		int m_PreviousTimeCheckpoint = -1;
		int m_TeleCheckpoint = -1;
		bool m_OnStart = false;
		bool m_OnFinish = false;
		bool m_PendingDirection = false;
		bool m_SegmentsRejected = false;
		bool m_UseSegmentForRoute = false;

	public:
		void Reset() { *this = CPlayer(); }
		const SEstimate &Estimate() const { return m_Estimate; }

		void Observe(const STile &Tile, int Index)
		{
			if(Tile.m_Start)
			{
				if(!m_OnStart)
					m_SegmentsRejected = false;
				m_Stage = 0;
				m_StartIndex = Index;
				m_SourceIndex = Index;
				m_PendingDirection = false;
			}
			else if(Tile.m_TimeCheckpoint >= 0 && Tile.m_TimeCheckpoint != m_PreviousTimeCheckpoint)
			{
				m_Stage = Tile.m_TimeCheckpoint + 1;
				m_SourceIndex = Index;
				m_PendingDirection = true;
			}
			m_PreviousTimeCheckpoint = Tile.m_TimeCheckpoint;
			m_OnStart = Tile.m_Start;
			m_OnFinish = Tile.m_Finish;
		}

		void Update(const CMap &Map, int Index, int TeleCheckpoint, int Budget)
		{
			m_CurrentIndex = Index;
			if(!Map.Ready() || Index < 0 || Index >= Map.Size())
			{
				m_Estimate = {};
				return;
			}
			TeleCheckpoint = std::clamp(TeleCheckpoint, 0, 255);
			if(m_TeleCheckpoint != TeleCheckpoint)
			{
				m_TeleCheckpoint = TeleCheckpoint;
				m_Estimate = {};
			}
			if(!m_Global.Matches(TeleCheckpoint, -1))
				m_Global.Start(Map, TeleCheckpoint);
			const bool UseSegments = !m_SegmentsRejected && Map.CheckpointCount() > 0 && m_Stage >= 0 && m_Stage <= Map.CheckpointCount();
			const int GlobalBudget = UseSegments ? Budget / 2 : Budget;
			m_Global.Step(Map, GlobalBudget);
			m_UseSegmentForRoute = false;
			if(UseSegments)
			{
				if(!m_Segment.Matches(TeleCheckpoint, m_Stage))
					m_Segment.Start(Map, TeleCheckpoint, m_Stage);
				m_Segment.Step(Map, Budget - GlobalBudget);
				const int Source = m_Segment.Source(Map.Sources(m_Stage), m_SourceIndex);
				const int SourceLength = m_Segment.Length(Source);
				const int CurrentLength = m_Segment.Length(Index);
				if(m_Segment.Complete() && SourceLength <= 0)
					m_SegmentsRejected = true;
				else if(SourceLength > 0 && CurrentLength >= 0)
				{
					if(m_PendingDirection && Map.Tile(Index).m_TimeCheckpoint < 0)
					{
						m_PendingDirection = false;
						if(m_Stage > 0 && m_Segment.Cost(Index) > m_Segment.Cost(Source))
						{
							// 刚穿回计时 CP 的前一侧：切回上一段，避免把历史最高 CP 当进度。
							m_Estimate = {true, (float)m_Stage / (Map.CheckpointCount() + 1)};
							--m_Stage;
							m_SourceIndex = -1;
							return;
						}
					}
					const float Fraction = 1.0f - (float)CurrentLength / SourceLength;
					m_Estimate = {true, std::clamp((m_Stage + Fraction) / (Map.CheckpointCount() + 1), 0.0f, 1.0f)};
					m_UseSegmentForRoute = true;
					return;
				}
			}
			if(m_OnStart || m_OnFinish)
			{
				m_Estimate = {true, m_OnFinish ? 1.0f : 0.0f};
				return;
			}
			const int Source = m_Global.Source(Map.Sources(-1), m_StartIndex);
			const int SourceLength = m_Global.Length(Source);
			const int CurrentLength = m_Global.Length(Index);
			if(SourceLength > 0 && CurrentLength >= 0)
				m_Estimate = {true, std::clamp(1.0f - (float)CurrentLength / SourceLength, 0.0f, 1.0f)};
			else
				m_Estimate = {};
		}

		bool BuildRoute(std::vector<int> &vRoute) const
		{
			return (m_UseSegmentForRoute ? m_Segment : m_Global).BuildRoute(m_CurrentIndex, vRoute);
		}
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_PROGRESS_H
