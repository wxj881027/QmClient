#include "netease_lyric_state.h"

#include <limits>

namespace NeteaseLyrics
{
	int SourcePriority(ESource Source)
	{
		switch(Source)
		{
		case ESource::Frontend: return 0;
		case ESource::DesktopLyricsFallback: return 1;
		case ESource::None: return 3;
		}
		return 3;
	}

	bool CanReplaceSource(ESource Existing, ESource Candidate, bool ExistingValid)
	{
		return !ExistingValid || Candidate == Existing || SourcePriority(Candidate) < SourcePriority(Existing);
	}

	ELyricSongDecision DecideLyricSongReport(uint64_t CurrentSongId, bool HasCurrentSong, uint64_t ReportSongId, uint64_t LastProgressTick, uint64_t NowTick, uint64_t ProgressFreshMs)
	{
		if(ReportSongId == 0)
			return HasCurrentSong ? ELyricSongDecision::ApplyCurrent : ELyricSongDecision::Reject;
		if(!HasCurrentSong)
			return ELyricSongDecision::SwitchSong;
		if(ReportSongId == CurrentSongId)
			return ELyricSongDecision::ApplyCurrent;
		const bool ProgressFresh = LastProgressTick != 0 && (NowTick < LastProgressTick || NowTick - LastProgressTick <= ProgressFreshMs);
		return ProgressFresh ? ELyricSongDecision::Defer : ELyricSongDecision::SwitchSong;
	}

	bool IsMeaningfulMediaIdentityChange(std::string_view PreviousTitle, std::string_view PreviousArtist, std::string_view CurrentTitle, std::string_view CurrentArtist)
	{
		if(PreviousTitle.empty() || CurrentTitle.empty())
			return false;
		if(PreviousTitle != CurrentTitle)
			return true;
		return !PreviousArtist.empty() && !CurrentArtist.empty() && PreviousArtist != CurrentArtist;
	}

	bool IsBridgeIdentityStillBlocked(uint64_t BlockedSongId, uint64_t BlockedGeneration, uint64_t CandidateSongId, uint64_t CandidateGeneration)
	{
		if(BlockedSongId == 0)
			return false;
		if(CandidateSongId == 0)
		{
			// 空 songId 不能证明已经切到新歌。generation 前进只表示
			// Helper 身份变化，调用方仍需保留旧 songId 的阻断直到新歌出现。
			return BlockedGeneration == 0 || CandidateGeneration == 0 || CandidateGeneration <= BlockedGeneration;
		}
		if(CandidateSongId != BlockedSongId)
			return false;
		// 同一歌曲的旧/当前 generation 都可能是排队中的旧快照；只有
		// 明确更新到更高 generation 才允许解除阻断。
		return BlockedGeneration == 0 || CandidateGeneration == 0 || CandidateGeneration <= BlockedGeneration;
	}

	ESmtcBridgeSyncDecision DecideSmtcBridgeSync(uint64_t BaselineSongId, uint64_t BaselineGeneration, uint64_t CandidateSongId, uint64_t CandidateGeneration)
	{
		if(BaselineSongId == 0)
			return ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED;
		if(CandidateSongId == 0)
			return ESmtcBridgeSyncDecision::WAIT_FOR_BRIDGE_ADVANCE;
		if(CandidateSongId != BaselineSongId)
			return ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED;
		if(BaselineGeneration == 0 || CandidateGeneration == 0)
			return ESmtcBridgeSyncDecision::WAIT_FOR_BRIDGE_ADVANCE;
		return CandidateGeneration > BaselineGeneration ? ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED : ESmtcBridgeSyncDecision::WAIT_FOR_BRIDGE_ADVANCE;
	}

	bool ShouldWaitForBridgeIdentity(uint64_t BaselineSongId, uint64_t BaselineGeneration, uint64_t CandidateSongId, uint64_t CandidateGeneration, uint64_t WaitStartTick, uint64_t NowTick, uint64_t MaxWaitMs)
	{
		if(DecideSmtcBridgeSync(BaselineSongId, BaselineGeneration, CandidateSongId, CandidateGeneration) == ESmtcBridgeSyncDecision::BRIDGE_ALREADY_ADVANCED)
			return false;
		if(WaitStartTick == 0 || NowTick < WaitStartTick)
			return false;
		return NowTick - WaitStartTick <= MaxWaitMs;
	}

	bool ShouldPreservePausedLyric(bool Paused, bool HasSong, bool LyricValid, uint64_t LastBridgeTick, uint64_t NowTick, uint64_t GraceMs)
	{
		if(!Paused || !HasSong || !LyricValid || LastBridgeTick == 0 || NowTick < LastBridgeTick)
			return false;
		return NowTick - LastBridgeTick <= GraceMs;
	}

	bool ShouldPreserveBridgeState(bool HasLyricState, uint64_t LastBridgeTick, uint64_t NowTick, uint64_t GraceMs)
	{
		if(!HasLyricState || LastBridgeTick == 0 || NowTick < LastBridgeTick)
			return false;
		return NowTick - LastBridgeTick <= GraceMs;
	}

	bool HasPausedLyricSeek(bool Paused, bool PositionValid, bool PositionAnchored, int64_t PositionMs, bool LastPositionValid, int64_t LastPositionMs)
	{
		return Paused && PositionValid && PositionAnchored && LastPositionValid && PositionMs != LastPositionMs;
	}

	bool ShouldPreserveConnectedLyric(bool WorkerConnected, bool PlayingHintKnown, bool PlayingHint, bool PositionValid, bool HasLyricData)
	{
		if(!WorkerConnected || !PositionValid || !HasLyricData)
			return false;
		return !PlayingHintKnown || !PlayingHint;
	}

	void CLyricState::Reset()
	{
		m_State = {};
		m_Timeline.Clear();
		m_TimelineSource = ESource::None;
	}

	bool CLyricState::UpdateSong(uint64_t SongId)
	{
		if((m_State.m_HasSong && m_State.m_SongId == SongId) || (!m_State.m_HasSong && SongId == 0))
			return false;
		m_State.m_SongId = SongId;
		m_State.m_HasSong = SongId != 0;
		if(m_State.m_Generation == std::numeric_limits<uint64_t>::max())
			m_State.m_Generation = 1;
		else
			++m_State.m_Generation;
		ClearLyrics();
		return true;
	}

	void CLyricState::AdoptGeneration(uint64_t Generation)
	{
		if(Generation != 0)
			m_State.m_Generation = Generation;
	}

	bool CLyricState::ApplyCurrentLine(ESource Source, std::string_view Text, int64_t LineStartMs, int64_t LineEndMs, int64_t PositionMs)
	{
		if(!m_State.m_HasSong || Source == ESource::None || Text.empty() || !IsValidUtf8(Text))
			return false;
		if(!CanReplaceSource(m_TimelineSource, Source, m_TimelineSource != ESource::None))
			return false;
		if(LineStartMs < -1 || LineEndMs < -1 || (LineStartMs == -1 && LineEndMs != -1) ||
			(LineStartMs >= 0 && LineEndMs >= 0 && LineEndMs <= LineStartMs))
			return false;
		// 字符串准备失败时保留旧状态；成功后只提交不会分配的来源、位置和边界。
		m_State.m_CurrentLyric.assign(Text);
		m_TimelineSource = Source;
		m_State.m_Source = Source;
		m_State.m_PositionMs = PositionMs < 0 ? 0 : PositionMs;
		m_State.m_LyricValid = true;
		m_State.m_LineStartMs = LineStartMs;
		m_State.m_LineEndMs = LineEndMs;
		return true;
	}

	void CLyricState::ClearLyrics()
	{
		m_State.m_Source = ESource::None;
		m_State.m_LyricValid = false;
		m_State.m_CurrentLyric.clear();
		m_State.m_LineStartMs = -1;
		m_State.m_LineEndMs = -1;
		m_Timeline.Clear();
		m_TimelineSource = ESource::None;
	}

	bool CLyricState::ApplyTimeline(ESource Source, const STimeline &Timeline, int64_t PositionMs)
	{
		if(!m_State.m_HasSong || !Timeline.m_HasTiming || Timeline.m_vLines.empty() || Source == ESource::None)
			return false;
		if(!CanReplaceSource(m_TimelineSource, Source, m_Timeline.m_HasTiming))
			return false;
		m_Timeline = Timeline;
		m_TimelineSource = Source;
		m_State.m_Source = Source;
		m_State.m_PositionMs = PositionMs < 0 ? 0 : PositionMs;
		const SSelectedLine Selected = SelectCurrentLine(m_Timeline, m_State.m_PositionMs);
		if(Selected.m_pLine == nullptr)
		{
			m_State.m_LyricValid = false;
			m_State.m_CurrentLyric.clear();
			m_State.m_LineStartMs = -1;
			m_State.m_LineEndMs = -1;
			return true;
		}
		m_State.m_LyricValid = true;
		m_State.m_CurrentLyric = Selected.m_pLine->m_Text;
		m_State.m_LineStartMs = Selected.m_pLine->m_StartMs;
		m_State.m_LineEndMs = Selected.m_pLine->m_EndMs;
		return true;
	}

	void CLyricState::UpdatePosition(int64_t PositionMs)
	{
		m_State.m_PositionMs = PositionMs < 0 ? 0 : PositionMs;
		if(!m_Timeline.m_HasTiming)
			return;
		const SSelectedLine Selected = SelectCurrentLine(m_Timeline, m_State.m_PositionMs);
		if(Selected.m_pLine == nullptr)
		{
			m_State.m_LyricValid = false;
			m_State.m_CurrentLyric.clear();
			m_State.m_LineStartMs = -1;
			m_State.m_LineEndMs = -1;
			return;
		}
		m_State.m_LyricValid = true;
		m_State.m_CurrentLyric = Selected.m_pLine->m_Text;
		m_State.m_LineStartMs = Selected.m_pLine->m_StartMs;
		m_State.m_LineEndMs = Selected.m_pLine->m_EndMs;
	}

	void CLyricState::MarkStopped()
	{
		Reset();
	}
} // namespace NeteaseLyrics
