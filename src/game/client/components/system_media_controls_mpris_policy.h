// MPRIS 的源身份、数值边界和轮询预算；不依赖 DBus，供生产后端和行为测试共用。
#ifndef GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_MPRIS_POLICY_H
#define GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_MPRIS_POLICY_H
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
namespace SystemMediaControls
{
	inline int64_t MprisMicrosecondsTo100ns(int64_t Value)
	{
		return std::clamp<int64_t>(Value, 0, std::numeric_limits<int64_t>::max() / 10) * 10;
	}
	class CMprisSessionIdentity
	{
		std::string m_Name;
		std::string m_Owner;
		uint64_t m_Generation = 0;

	public:
		bool Update(const std::string &Name, const std::string &Owner)
		{
			if(Name == m_Name && Owner == m_Owner)
				return false;
			m_Name = Name;
			m_Owner = Owner;
			++m_Generation;
			return true;
		}
		void Clear()
		{
			m_Name.clear();
			m_Owner.clear();
		}
		uint64_t Generation() const { return m_Generation; }
		bool Accepts(uint64_t Generation) const { return !m_Owner.empty() && Generation == m_Generation; }
	};
	struct SMprisPropertiesSnapshot
	{
		std::string m_Title;
		std::string m_Artist;
		std::string m_Album;
		std::string m_TrackId;
		bool m_CanControl = false;
		bool m_CanPlay = false;
		bool m_CanPause = false;
		bool m_CanPrev = false;
		bool m_CanNext = false;
		void ApplyControlAvailability()
		{
			m_CanPlay &= m_CanControl;
			m_CanPause &= m_CanControl;
			m_CanPrev &= m_CanControl;
			m_CanNext &= m_CanControl;
		}
	};
	class CMprisTrackIdentity
	{
		SMprisPropertiesSnapshot m_Last;
		bool m_HasTrack = false;

	public:
		void Clear()
		{
			m_Last = {};
			m_HasTrack = false;
		}
		bool Update(const SMprisPropertiesSnapshot &Current)
		{
			SMprisPropertiesSnapshot Normalized = Current;
			// MPRIS 的 NoTrack 哨兵没有曲目身份，必须与缺失 trackid 一样使用元数据回退。
			if(Normalized.m_TrackId == "/org/mpris/MediaPlayer2/TrackList/NoTrack")
				Normalized.m_TrackId.clear();
			const bool Changed = !m_HasTrack || Normalized.m_TrackId != m_Last.m_TrackId ||
					     (Normalized.m_TrackId.empty() && (Normalized.m_Title != m_Last.m_Title || Normalized.m_Artist != m_Last.m_Artist || Normalized.m_Album != m_Last.m_Album));
			m_Last = std::move(Normalized);
			m_HasTrack = true;
			return Changed;
		}
	};
	class CMprisPollBudget
	{
		int64_t m_Deadline;

	public:
		CMprisPollBudget(int64_t NowMs, int64_t DurationMs) : m_Deadline(NowMs + DurationMs) {}
		int Remaining(int64_t NowMs, bool Stop) const { return Stop ? 0 : static_cast<int>(std::clamp<int64_t>(m_Deadline - NowMs, 0, 1000)); }
	};
	inline int MprisRequestTimeout(int RemainingMs, int ReservedMs)
	{
		return std::clamp(RemainingMs - ReservedMs, 0, 250);
	}
	// 活动源优先；其他候选按跨轮游标轮转，保留位置读取预算，失败源不能永远占据队首。
	class CMprisCandidateScheduler
	{
		size_t m_Next = 0;

	public:
		template<typename FNow, typename FStop, typename FVisit>
		void Visit(const std::vector<std::string> &Names, const std::string &Active, const CMprisPollBudget &Budget, FNow Now, FStop Stop, FVisit Visitor)
		{
			if(Names.empty())
				return;
			const auto Allowed = [&] { return Budget.Remaining(Now(), Stop()) > 200; };
			const auto ActiveIt = std::find(Names.begin(), Names.end(), Active);
			const size_t ActiveIndex = static_cast<size_t>(ActiveIt - Names.begin());
			if(ActiveIt != Names.end() && Allowed() && !Visitor(Names[ActiveIndex]))
				return;
			const size_t Start = m_Next % Names.size();
			for(size_t Offset = 0; Offset < Names.size() && Allowed(); ++Offset)
			{
				const size_t Index = (Start + Offset) % Names.size();
				if(Index == ActiveIndex)
					continue;
				m_Next = (Index + 1) % Names.size();
				if(!Visitor(Names[Index]))
					return;
			}
		}
	};

	// 传输等待只用可观察条件推进；每片最多 20ms，停止或轮询预算耗尽即返回。
	template<typename FStop, typename FNow, typename FCompleted, typename FDispatch>
	bool MprisWaitForReply(const CMprisPollBudget &Budget, FStop Stop, FNow Now, FCompleted Completed, FDispatch Dispatch)
	{
		while(!Completed())
		{
			const int Remaining = Budget.Remaining(Now(), Stop());
			if(Remaining <= 0 || !Dispatch(std::min(Remaining, 20)))
				return false;
		}
		return Budget.Remaining(Now(), Stop()) > 0;
	}
	struct SMprisPosition
	{
		int64_t m_PositionMs = 0;
		int64_t m_UpdatedTick = 0;
		uint64_t m_Generation = 0;
	};
	inline SMprisPosition MprisResolvePosition(bool SourceChanged, bool HasPosition, const SMprisPosition &Previous, const SMprisPosition &Current)
	{
		return !HasPosition && !SourceChanged ? Previous : Current;
	}
}
#endif
