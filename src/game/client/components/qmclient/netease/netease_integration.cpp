#include "netease_integration.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/components/system_media_controls.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace
{
	bool ContainsAsciiInsensitive(std::string_view Text, std::string_view Needle)
	{
		if(Needle.empty() || Text.size() < Needle.size())
			return false;
		for(size_t Offset = 0; Offset <= Text.size() - Needle.size(); ++Offset)
		{
			bool Match = true;
			for(size_t Index = 0; Index < Needle.size(); ++Index)
			{
				const char TextChar = Text[Offset + Index] >= 'A' && Text[Offset + Index] <= 'Z' ? (char)(Text[Offset + Index] - 'A' + 'a') : Text[Offset + Index];
				const char NeedleChar = Needle[Index] >= 'A' && Needle[Index] <= 'Z' ? (char)(Needle[Index] - 'A' + 'a') : Needle[Index];
				if(TextChar != NeedleChar)
				{
					Match = false;
					break;
				}
			}
			if(Match)
				return true;
		}
		return false;
	}

	bool IsNeteaseSourceAppId(std::string_view SourceAppId)
	{
		return ContainsAsciiInsensitive(SourceAppId, "cloudmusic") || ContainsAsciiInsensitive(SourceAppId, "netease");
	}

	uint64_t MonotonicTickMs()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}
}

class CNeteaseIntegration::CLyricsParseJob : public IJob
{
public:
	NeteaseLyrics::SRawLyrics m_Raw;
	QmMusicLyrics::SLyricsData m_Result;
	void Run() override
	{
		if(NeteaseLyrics::ParseRawLyrics(m_Raw, &m_Result.m_Timeline))
			QmMusicLyrics::AlignTranslations(m_Result.m_Timeline, m_Raw.m_TranslationLrc, &m_Result.m_vTranslations);
		m_Raw = {};
	}
};

void CNeteaseIntegration::ResetLyricsData()
{
	m_pLyricsParseJob.reset();
	m_LyricsData = {};
	m_DataSongId = 0;
	m_DataGeneration = 0;
	m_DataPid = 0;
	m_DataRevision = 0;
	m_NextLyricsReadTick = 0;
	m_LyricsChannel.Close();
}

void CNeteaseIntegration::UpdateLyricsData(const QmNeteaseHook::SSnapshotV5 &Snapshot)
{
	if(m_DataSongId != Snapshot.m_SongId || m_DataGeneration != Snapshot.m_Generation || m_DataPid != Snapshot.m_CloudMusicPid)
	{
		ResetLyricsData();
		m_DataSongId = Snapshot.m_SongId;
		m_DataGeneration = Snapshot.m_Generation;
		m_DataPid = Snapshot.m_CloudMusicPid;
	}
	if(m_pLyricsParseJob && m_pLyricsParseJob->State() == IJob::STATE_DONE)
	{
		m_LyricsData = std::move(m_pLyricsParseJob->m_Result);
		m_pLyricsParseJob.reset();
	}
	const uint64_t Now = MonotonicTickMs();
	if(Now < m_NextLyricsReadTick)
		return;
	m_NextLyricsReadTick = Now + 250;
	NeteaseLyrics::SRawLyrics Raw;
	if(m_LyricsChannel.Read(m_DataPid, m_DataSongId, m_DataGeneration, m_DataRevision, Raw))
	{
		auto pJob = std::make_shared<CLyricsParseJob>();
		pJob->m_Raw = std::move(Raw);
		m_pLyricsParseJob = pJob;
		Engine()->AddJob(pJob);
	}
}

CNeteaseIntegration::CNeteaseIntegration() = default;

void CNeteaseIntegration::OnInit()
{
	ResetLyricsData();
	m_LyricState.Reset();
	m_LastBridgeSequence = 0;
	m_LastBridgeGeneration = 0;
	m_LastBridgeSnapshotTick = 0;
	m_LastBridgePositionMs = 0;
	m_LastCloudMusicPid = 0;
	m_BlockedBridgeSongId = 0;
	m_BlockedBridgeGeneration = 0;
	m_LastMediaTitle.clear();
	m_LastMediaArtist.clear();
	m_WaitingForBridgeIdentity = false;
	m_BridgeSyncWaitStartTick = 0;
	m_LastSmtcAlignedBridgeSongId = 0;
	m_LastSmtcAlignedBridgeGeneration = 0;
	m_ActiveLyrics = false;
	m_LastBridgePositionValid = false;
	m_Initialized = true;
}

void CNeteaseIntegration::OnShutdown()
{
	ResetLyricsData();
	m_LyricState.Reset();
	m_LastBridgeSequence = 0;
	m_LastBridgeGeneration = 0;
	m_LastBridgeSnapshotTick = 0;
	m_LastBridgePositionMs = 0;
	m_LastCloudMusicPid = 0;
	m_BlockedBridgeSongId = 0;
	m_BlockedBridgeGeneration = 0;
	m_LastMediaTitle.clear();
	m_LastMediaArtist.clear();
	m_WaitingForBridgeIdentity = false;
	m_BridgeSyncWaitStartTick = 0;
	m_LastSmtcAlignedBridgeSongId = 0;
	m_LastSmtcAlignedBridgeGeneration = 0;
	m_ActiveLyrics = false;
	m_LastBridgePositionValid = false;
	m_Initialized = false;
}

void CNeteaseIntegration::OnReset()
{
	ResetLyricsData();
	m_LyricState.Reset();
	m_LastBridgeSequence = 0;
	m_LastBridgeGeneration = 0;
	m_LastBridgeSnapshotTick = 0;
	m_LastBridgePositionMs = 0;
	m_LastCloudMusicPid = 0;
	m_BlockedBridgeSongId = 0;
	m_BlockedBridgeGeneration = 0;
	m_LastMediaTitle.clear();
	m_LastMediaArtist.clear();
	m_WaitingForBridgeIdentity = false;
	m_BridgeSyncWaitStartTick = 0;
	m_LastSmtcAlignedBridgeSongId = 0;
	m_LastSmtcAlignedBridgeGeneration = 0;
	m_ActiveLyrics = false;
	m_LastBridgePositionValid = false;
}

void CNeteaseIntegration::ClearForStaleMedia()
{
	ResetLyricsData();
	const NeteaseLyrics::SCurrentState &State = m_LyricState.Snapshot();
	const bool HasLyricState = State.m_SongId != 0 || State.m_Generation != 0 || State.m_HasSong || State.m_LyricValid || !State.m_CurrentLyric.empty();
	const bool HasBridgeState = m_LastBridgeSequence != 0 || m_LastBridgeGeneration != 0 || m_LastBridgeSnapshotTick != 0 || m_LastBridgePositionMs != 0;
	const bool HasIdentityState = m_LastCloudMusicPid != 0 || m_BlockedBridgeSongId != 0 || m_BlockedBridgeGeneration != 0 || m_WaitingForBridgeIdentity || m_BridgeSyncWaitStartTick != 0 || m_LastSmtcAlignedBridgeSongId != 0 || m_LastSmtcAlignedBridgeGeneration != 0 || m_ActiveLyrics || m_LastBridgePositionValid || !m_LastMediaTitle.empty() || !m_LastMediaArtist.empty();
	if(!HasLyricState && !HasBridgeState && !HasIdentityState)
		return;

	m_LyricState.MarkStopped();
	m_LastBridgeSequence = 0;
	m_LastBridgeGeneration = 0;
	m_LastBridgeSnapshotTick = 0;
	m_LastBridgePositionMs = 0;
	m_LastCloudMusicPid = 0;
	m_BlockedBridgeSongId = 0;
	m_BlockedBridgeGeneration = 0;
	m_WaitingForBridgeIdentity = false;
	m_BridgeSyncWaitStartTick = 0;
	m_LastSmtcAlignedBridgeSongId = 0;
	m_LastSmtcAlignedBridgeGeneration = 0;
	m_ActiveLyrics = false;
	m_LastBridgePositionValid = false;
	m_LastMediaTitle.clear();
	m_LastMediaArtist.clear();
}

void CNeteaseIntegration::OnUpdate()
{
	if(!m_Initialized || GameClient() == nullptr)
		return;
	if(!g_Config.m_QmNeteaseHookEnable)
	{
		ClearForStaleMedia();
		return;
	}

	QmNeteaseHook::SSnapshotV5 BridgeSnapshot{};
	const bool HasBridgeSnapshot = GameClient()->m_SystemMediaControls.GetNeteaseSnapshot(BridgeSnapshot);
	CSystemMediaControls::SState MediaState{};
	const bool HasMedia = GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState);
	// SMTC 是标准媒体状态的唯一权威来源。没有媒体时立即清除；Bridge
	// 短暂失联时仅对暂停中的已确认当前句使用有限宽限期。
	if(!HasMedia)
	{
		ClearForStaleMedia();
		return;
	}
	if(MediaState.m_aSourceAppId[0] != '\0' && !IsNeteaseSourceAppId(MediaState.m_aSourceAppId))
	{
		// 当前系统媒体会话属于其它播放器时，不能把网易云私有歌词
		// 合并到它的标题、封面或播放状态上。
		ClearForStaleMedia();
		return;
	}
	const std::string_view MediaTitle(MediaState.m_aTitle);
	const std::string_view MediaArtist(MediaState.m_aArtist);
	if(NeteaseLyrics::IsMeaningfulMediaIdentityChange(m_LastMediaTitle, m_LastMediaArtist, MediaTitle, MediaArtist))
	{
		// 只使用上一次已经和 SMTC 元数据对齐的 Bridge 身份作为基线。
		// Bridge 若先切歌，当前快照已经前进，后续 SMTC 变化不应再次阻断它。
		m_BlockedBridgeSongId = m_LastSmtcAlignedBridgeSongId;
		m_BlockedBridgeGeneration = m_LastSmtcAlignedBridgeGeneration;
		m_WaitingForBridgeIdentity = m_BlockedBridgeSongId != 0;
		m_BridgeSyncWaitStartTick = m_WaitingForBridgeIdentity ? MonotonicTickMs() : 0;
		m_LyricState.ClearLyrics();
		m_ActiveLyrics = false;
		m_LastBridgeSequence = 0;
		m_LastBridgePositionMs = 0;
		m_LastBridgePositionValid = false;
	}
	if(!MediaTitle.empty())
	{
		if(m_LastMediaTitle != MediaTitle)
			m_LastMediaTitle.assign(MediaTitle);
		if(m_LastMediaArtist != MediaArtist)
			m_LastMediaArtist.assign(MediaArtist);
	}
	if(!HasBridgeSnapshot)
	{
		const uint64_t HookTimeout = (uint64_t)std::max(1, g_Config.m_QmNeteaseHookTimeoutMs);
		const uint64_t PausedGrace = std::max<uint64_t>(1500, HookTimeout);
		const NeteaseLyrics::SCurrentState &State = m_LyricState.Snapshot();
		if(NeteaseLyrics::ShouldPreserveBridgeState(State.m_HasSong && (State.m_LyricValid || m_ActiveLyrics), m_LastBridgeSnapshotTick, MonotonicTickMs(), PausedGrace))
			return;
		// 切歌后的阻断必须跨过短暂的 reader 失败；否则旧快照恢复时
		// 会再次被当成新歌接受。超过上限则放弃这次等待，避免永久卡住。
		if(m_WaitingForBridgeIdentity)
		{
			const uint64_t Now = MonotonicTickMs();
			if(m_BridgeSyncWaitStartTick == 0 || Now < m_BridgeSyncWaitStartTick || Now - m_BridgeSyncWaitStartTick <= 3000)
				return;
			m_WaitingForBridgeIdentity = false;
			m_BlockedBridgeSongId = 0;
			m_BlockedBridgeGeneration = 0;
			m_BridgeSyncWaitStartTick = 0;
		}
		if(State.m_HasSong)
			ClearForStaleMedia();
		return;
	}
	m_LastBridgeSnapshotTick = MonotonicTickMs();
	const uint64_t NowTick = m_LastBridgeSnapshotTick;
	if(m_LastCloudMusicPid != 0 && BridgeSnapshot.m_CloudMusicPid != m_LastCloudMusicPid)
	{
		m_LyricState.ClearLyrics();
		m_ActiveLyrics = false;
		m_LastBridgeSequence = 0;
		m_LastBridgeGeneration = 0;
		m_LastBridgePositionMs = 0;
		m_LastBridgePositionValid = false;
		m_LastSmtcAlignedBridgeSongId = 0;
		m_LastSmtcAlignedBridgeGeneration = 0;
	}
	m_LastCloudMusicPid = BridgeSnapshot.m_CloudMusicPid;
	if(m_WaitingForBridgeIdentity)
	{
		const bool HasCandidateSong = (BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_HAS_SONG) != 0 && BridgeSnapshot.m_SongId != 0;
		const bool StillWaiting = NeteaseLyrics::ShouldWaitForBridgeIdentity(
			m_BlockedBridgeSongId,
			m_BlockedBridgeGeneration,
			HasCandidateSong ? BridgeSnapshot.m_SongId : 0,
			BridgeSnapshot.m_Generation,
			m_BridgeSyncWaitStartTick,
			NowTick,
			3000);
		if(StillWaiting)
		{
			if(!HasCandidateSong && m_BlockedBridgeGeneration != 0 && BridgeSnapshot.m_Generation > m_BlockedBridgeGeneration)
				m_BlockedBridgeGeneration = BridgeSnapshot.m_Generation;
			m_LastBridgeSequence = BridgeSnapshot.m_Sequence;
			return;
		}
		m_BlockedBridgeSongId = 0;
		m_BlockedBridgeGeneration = 0;
		m_WaitingForBridgeIdentity = false;
		m_BridgeSyncWaitStartTick = 0;
		if(HasCandidateSong)
		{
			m_LastSmtcAlignedBridgeSongId = BridgeSnapshot.m_SongId;
			m_LastSmtcAlignedBridgeGeneration = BridgeSnapshot.m_Generation;
		}
	}

	if((BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_HAS_SONG) == 0 || BridgeSnapshot.m_SongId == 0 ||
		MediaState.m_PlaybackState == CSystemMediaControls::EPlaybackState::Stopped)
	{
		ClearForStaleMedia();
		return;
	}
	if(m_LastSmtcAlignedBridgeSongId == 0 && BridgeSnapshot.m_SongId != 0)
	{
		m_LastSmtcAlignedBridgeSongId = BridgeSnapshot.m_SongId;
		m_LastSmtcAlignedBridgeGeneration = BridgeSnapshot.m_Generation;
	}

	const bool SongChanged = m_LyricState.UpdateSong(BridgeSnapshot.m_SongId);
	if(SongChanged)
	{
		// UpdateSong 已经清空旧歌词；这一步必须发生在接受新快照之前。
		m_LastBridgeSequence = 0;
		m_LastBridgeGeneration = 0;
		m_LastBridgePositionMs = 0;
		m_LastBridgePositionValid = false;
	}
	if(m_LastBridgeGeneration != 0 && BridgeSnapshot.m_Generation != m_LastBridgeGeneration)
	{
		// Helper/DLL 重启后 generation 变化，即使 songId 恰好相同也不能
		// 把旧时间轴当成新进程的结果。
		m_LyricState.ClearLyrics();
		m_LastBridgeSequence = 0;
		m_LastBridgePositionMs = 0;
		m_LastBridgePositionValid = false;
	}
	m_LastBridgeGeneration = BridgeSnapshot.m_Generation;
	m_LyricState.AdoptGeneration(BridgeSnapshot.m_Generation);
	UpdateLyricsData(BridgeSnapshot);
	const bool PositionValid = (BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_POSITION_VALID) != 0;
	const bool PositionAnchored = (BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_POSITION_ANCHORED) != 0;
	const bool PausedPositionChanged = NeteaseLyrics::HasPausedLyricSeek(
		MediaState.m_PlaybackState == CSystemMediaControls::EPlaybackState::Paused,
		PositionValid,
		PositionAnchored,
		BridgeSnapshot.m_PositionMs,
		m_LastBridgePositionValid,
		m_LastBridgePositionMs);
	const bool KeepPausedLyric = MediaState.m_PlaybackState == CSystemMediaControls::EPlaybackState::Paused &&
				     m_LyricState.Snapshot().m_LyricValid && !SongChanged && !PausedPositionChanged;

	if(BridgeSnapshot.m_Sequence == m_LastBridgeSequence)
	{
		if((MediaState.m_Playing || PausedPositionChanged) && PositionValid)
			m_LyricState.UpdatePosition(BridgeSnapshot.m_PositionMs);
		if(PositionAnchored)
		{
			m_LastBridgePositionMs = BridgeSnapshot.m_PositionMs;
			m_LastBridgePositionValid = true;
		}
		return;
	}
	m_LastBridgeSequence = BridgeSnapshot.m_Sequence;
	m_ActiveLyrics = (BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_LYRIC_TIMELINE_VALID) != 0 ||
			 ((BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_LYRIC_VALID) != 0 && BridgeSnapshot.m_LyricSource != (uint32_t)QmNeteaseHook::ENeteaseLyricSource::None);
	if(PositionAnchored)
	{
		m_LastBridgePositionMs = BridgeSnapshot.m_PositionMs;
		m_LastBridgePositionValid = true;
	}
	if((BridgeSnapshot.m_Flags & QmNeteaseHook::V5_FLAG_LYRIC_VALID) == 0 || BridgeSnapshot.m_aCurrentLyric[0] == '\0')
	{
		if(!KeepPausedLyric)
			m_LyricState.ClearLyrics();
		return;
	}

	// SMTC 暂停时保留已经选中的句子；新歌曲仍可接受第一句，只有检测到
	// Bridge 进度锚点发生 seek 时才允许暂停快照重新定位当前句。
	if(KeepPausedLyric)
		return;

	NeteaseLyrics::ESource Source = NeteaseLyrics::ESource::Frontend;
	switch((QmNeteaseHook::ENeteaseLyricSource)BridgeSnapshot.m_LyricSource)
	{
	case QmNeteaseHook::ENeteaseLyricSource::DesktopLyricsFallback: Source = NeteaseLyrics::ESource::DesktopLyricsFallback; break;
	case QmNeteaseHook::ENeteaseLyricSource::Frontend: Source = NeteaseLyrics::ESource::Frontend; break;
	case QmNeteaseHook::ENeteaseLyricSource::None: return;
	case QmNeteaseHook::ENeteaseLyricSource::ReservedLegacyApi: return;
	}
	const int64_t PositionMs = PositionValid ? BridgeSnapshot.m_PositionMs : m_LyricState.Snapshot().m_PositionMs;
	m_LyricState.ApplyCurrentLine(Source, BridgeSnapshot.m_aCurrentLyric, BridgeSnapshot.m_LineStartMs, BridgeSnapshot.m_LineEndMs, PositionMs);
}

bool CNeteaseIntegration::GetCurrentLyric(char *pBuffer, size_t BufferSize, ColorRGBA *pColor) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	pBuffer[0] = '\0';
	if(!g_Config.m_QmLyrics || !g_Config.m_QmLyricsInMediaIsland)
		return false;
	const NeteaseLyrics::SCurrentState &State = m_LyricState.Snapshot();
	if(!State.m_HasSong || !State.m_LyricValid || State.m_CurrentLyric.empty())
		return false;
	QmNeteaseHook::CopyUtf8Truncated(pBuffer, BufferSize, State.m_CurrentLyric.data(), State.m_CurrentLyric.size());
	if(pColor != nullptr)
		*pColor = ColorRGBA(0.97f, 0.98f, 1.0f, 0.90f);
	return pBuffer[0] != '\0';
}

bool CNeteaseIntegration::HasCurrentLyric() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 && m_LyricState.Snapshot().m_LyricValid;
}

bool CNeteaseIntegration::HasActiveLyrics() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 && m_LyricState.Snapshot().m_HasSong && m_ActiveLyrics;
}

uint64_t CNeteaseIntegration::CurrentSongId() const
{
	return m_LyricState.Snapshot().m_SongId;
}

uint64_t CNeteaseIntegration::CurrentGeneration() const
{
	return m_LyricState.Snapshot().m_Generation;
}

QmMusicLyrics::SSourceSnapshot CNeteaseIntegration::LyricsSnapshot() const
{
	const auto &State = m_LyricState.Snapshot();
	if(!State.m_HasSong || m_WaitingForBridgeIdentity)
		return {};
	const QmMusicLyrics::SLyricsData *pData = m_LyricsData.HasLyrics() &&
		m_DataSongId == State.m_SongId && m_DataGeneration == State.m_Generation && m_DataPid == m_LastCloudMusicPid ? &m_LyricsData : nullptr;
	std::string_view Translation;
	if(pData && State.m_LyricValid)
	{
		const auto &vLines = pData->m_Timeline.m_vLines;
		auto It = std::lower_bound(vLines.begin(), vLines.end(), State.m_LineStartMs,
			[](const NeteaseLyrics::SLine &Line, int64_t StartMs) { return Line.m_StartMs < StartMs; });
		for(; It != vLines.end() && It->m_StartMs == State.m_LineStartMs; ++It)
		{
			if(It->m_Text == State.m_CurrentLyric)
			{
				Translation = QmMusicLyrics::TranslationAt(*pData, static_cast<int>(It - vLines.begin()));
				break;
			}
		}
	}
	return {State.m_SongId, State.m_Generation, m_LastCloudMusicPid, m_ActiveLyrics, pData,
		State.m_LyricValid ? std::string_view(State.m_CurrentLyric) : std::string_view(), Translation};
}
