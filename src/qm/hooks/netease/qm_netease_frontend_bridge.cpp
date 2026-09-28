#include "qm_netease_frontend_bridge.h"

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace QmNeteaseBridge
{
	namespace
	{
		constexpr uint64_t FRONTEND_REPORT_STALE_MS = 3000;
		constexpr uint64_t FALLBACK_SNAPSHOT_STALE_MS = 2000;

		const json_value *Field(const json_value *pObject, const char *pName)
		{
			if(pObject == nullptr || pObject->type != json_object || pName == nullptr)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
					return Entry.value;
			}
			return nullptr;
		}

		std::string StringValue(const json_value *pValue)
		{
			if(pValue == nullptr || pValue->type != json_string || pValue->u.string.ptr == nullptr)
				return {};
			return std::string(pValue->u.string.ptr, pValue->u.string.length);
		}

		uint64_t SongIdValue(const json_value *pValue)
		{
			if(pValue == nullptr)
				return 0;
			if(pValue->type == json_integer && pValue->u.integer > 0)
				return (uint64_t)pValue->u.integer;
			const std::string Text = StringValue(pValue);
			if(Text.empty())
				return 0;
			uint64_t Result = 0;
			const auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), Result);
			return Parsed.ec == std::errc{} && Parsed.ptr == Text.data() + Text.size() ? Result : 0;
		}

		bool NumberValue(const json_value *pValue, double *pOut)
		{
			if(pValue == nullptr || pOut == nullptr)
				return false;
			if(pValue->type == json_integer)
			{
				*pOut = (double)pValue->u.integer;
				return true;
			}
			if(pValue->type == json_double)
			{
				*pOut = pValue->u.dbl;
				return std::isfinite(*pOut);
			}
			return false;
		}

		std::string LyricField(const json_value *pObject, const char *pName)
		{
			const json_value *pValue = Field(pObject, pName);
			if(pValue == nullptr)
				return {};
			if(pValue->type == json_string)
				return StringValue(pValue);
			if(pValue->type == json_object)
				return StringValue(Field(pValue, "lyric"));
			return {};
		}

		void ExtractRaw(const json_value *pValue, NeteaseLyrics::SRawLyrics *pOut, int Depth = 0)
		{
			if(pValue == nullptr || pOut == nullptr || Depth > 8)
				return;
			if(pValue->type == json_string)
			{
				if(pOut->m_Lrc.empty())
					pOut->m_Lrc = StringValue(pValue);
				return;
			}
			if(pValue->type == json_array)
			{
				for(unsigned int Index = 0; Index < pValue->u.array.length; ++Index)
					ExtractRaw(pValue->u.array.values[Index], pOut, Depth + 1);
				return;
			}
			if(pValue->type != json_object)
				return;

			// YRC 具有更精确的行/词时间。先读取精确字段，再读取普通 LRC
			// 和翻译/罗马音字段，避免 yromalrc 抢在 lrc 之前成为主歌词。
			if(pOut->m_Yrc.empty())
				pOut->m_Yrc = LyricField(pValue, "yrc");
			for(const char *pName : {"lrc", "ytlrc", "tlyric", "ttlrc", "yromalrc", "romalrc"})
			{
				if(!pOut->m_Lrc.empty())
					break;
				pOut->m_Lrc = LyricField(pValue, pName);
			}
			for(unsigned int Index = 0; Index < pValue->u.object.length; ++Index)
				ExtractRaw(pValue->u.object.values[Index].value, pOut, Depth + 1);
		}

		uint64_t DirectSongId(const json_value *pObject, bool AllowTrackId)
		{
			if(pObject == nullptr || pObject->type != json_object)
				return 0;
			for(const char *pName : {"songId", "songID", "song_id"})
			{
				const uint64_t Id = SongIdValue(Field(pObject, pName));
				if(Id != 0)
					return Id;
			}
			return AllowTrackId ? SongIdValue(Field(pObject, "id")) : 0;
		}

		bool IsSongContainer(std::string_view Name)
		{
			for(const std::string_view Candidate : {"song", "track", "audio", "media", "currentSong", "currentTrack"})
				if(Name == Candidate)
					return true;
			return false;
		}

		bool IsReportContainer(std::string_view Name)
		{
			for(const std::string_view Candidate : {"payload", "data", "detail", "result", "progress", "lyrics", "rawLyrics"})
				if(Name == Candidate)
					return true;
			return false;
		}

		// 只沿网易云报告的已知容器查找 songId。不能在 raw lyrics 的任意
		// 元数据里递归寻找通用 id，否则 contributor/album 的 id 可能被误当歌曲。
		uint64_t NestedSongId(const json_value *pValue, int Depth = 0, bool AllowTrackId = false)
		{
			if(pValue == nullptr || Depth > 4)
				return 0;
			if(pValue->type == json_object)
			{
				const uint64_t DirectId = DirectSongId(pValue, AllowTrackId);
				if(DirectId != 0)
					return DirectId;
				for(unsigned int Index = 0; Index < pValue->u.object.length; ++Index)
				{
					const auto &Entry = pValue->u.object.values[Index];
					if(Entry.name == nullptr)
						continue;
					const std::string_view Name(Entry.name, Entry.name_length);
					const bool ChildAllowsTrackId = IsSongContainer(Name);
					if(!ChildAllowsTrackId && !IsReportContainer(Name))
						continue;
					const uint64_t Id = NestedSongId(Entry.value, Depth + 1, ChildAllowsTrackId);
					if(Id != 0)
						return Id;
				}
			}
			else if(pValue->type == json_array)
			{
				for(unsigned int Index = 0; Index < pValue->u.array.length; ++Index)
				{
					const uint64_t Id = NestedSongId(pValue->u.array.values[Index], Depth + 1, AllowTrackId);
					if(Id != 0)
						return Id;
				}
			}
			return 0;
		}

		bool ReadPlaying(const json_value *pObject, bool *pPlaying)
		{
			if(pObject == nullptr || pPlaying == nullptr || pObject->type != json_object)
				return false;
			for(const char *pName : {"playing", "isPlaying"})
			{
				const json_value *pValue = Field(pObject, pName);
				if(pValue != nullptr && pValue->type == json_boolean)
				{
					*pPlaying = pValue->u.boolean != 0;
					return true;
				}
			}
			const json_value *pPaused = Field(pObject, "paused");
			if(pPaused != nullptr && pPaused->type == json_boolean)
			{
				*pPlaying = pPaused->u.boolean == 0;
				return true;
			}
			const std::string State = StringValue(Field(pObject, "state"));
			if(State == "paused" || State == "stopped")
			{
				*pPlaying = false;
				return true;
			}
			if(State == "playing" || State == "play")
			{
				*pPlaying = true;
				return true;
			}
			return false;
		}

		bool ParseRawReport(std::string_view Report, std::string *pKind, uint64_t *pSongId, double *pPosition, bool *pHasPosition, bool *pPlaying, bool *pHasPlaying, NeteaseLyrics::SRawLyrics *pRaw)
		{
			if(pKind == nullptr || pSongId == nullptr || pPosition == nullptr || pHasPosition == nullptr || pPlaying == nullptr || pHasPlaying == nullptr || pRaw == nullptr)
				return false;
			*pKind = {};
			*pSongId = 0;
			*pPosition = 0;
			*pHasPosition = false;
			*pPlaying = false;
			*pHasPlaying = false;
			*pRaw = {};
			if(Report.empty() || Report.size() > 4 * 1024 * 1024)
				return false;
			json_value *pRoot = json_parse(Report.data(), Report.size());
			if(pRoot == nullptr || (pRoot->type != json_object && pRoot->type != json_array))
			{
				if(pRoot != nullptr)
					json_value_free(pRoot);
				return false;
			}
			const json_value *pKindValue = Field(pRoot, "kind");
			if(pKindValue != nullptr && pKindValue->type != json_string)
			{
				json_value_free(pRoot);
				return false;
			}
			*pKind = pKindValue != nullptr ? StringValue(pKindValue) : "lyrics";
			*pSongId = NestedSongId(pRoot);
			if(pRoot->type == json_object)
			{
				const json_value *pPositionMs = Field(pRoot, "positionMs");
				if(pPositionMs == nullptr)
					pPositionMs = Field(pRoot, "currentTimeMs");
				double Value = 0;
				if(NumberValue(pPositionMs, &Value) && Value >= 0)
				{
					*pHasPosition = true;
					*pPosition = Value;
				}
				else
				{
					// audioplayer.onPlayProgress 的 currentTime 单位固定为秒；
					// 单位转换由字段名决定，不再根据数值大小猜测。
					const json_value *pCurrentTime = Field(pRoot, "currentTime");
					if(NumberValue(pCurrentTime, &Value) && Value >= 0 && Value <= std::numeric_limits<double>::max() / 1000.0)
					{
						*pHasPosition = true;
						*pPosition = Value * 1000.0;
					}
				}
				*pHasPlaying = ReadPlaying(pRoot, pPlaying);
				ExtractRaw(Field(pRoot, "rawLyrics"), pRaw);
				if(pRaw->m_Lrc.empty() && pRaw->m_Yrc.empty())
					ExtractRaw(Field(pRoot, "lyrics"), pRaw);
			}
			if(*pKind == "lyrics" && pRaw->m_Lrc.empty() && pRaw->m_Yrc.empty())
				ExtractRaw(pRoot, pRaw);
			json_value_free(pRoot);
			return *pKind == "lyrics" || *pKind == "progress";
		}

		int64_t ToPosition(double Value)
		{
			if(!std::isfinite(Value) || Value <= 0)
				return 0;
			if(Value >= (double)std::numeric_limits<int64_t>::max())
				return std::numeric_limits<int64_t>::max();
			return (int64_t)std::llround(Value);
		}

		uint64_t NowTick()
		{
#if defined(_WIN32)
			// v5 的 timestamp 与客户端 reader 统一使用 GetTickCount64；
			// 不能混用 steady_clock 的 QPC epoch。
			return (uint64_t)GetTickCount64();
#else
			return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
		}
	}

	CFrontendLyricBridge::CFrontendLyricBridge() = default;
	CFrontendLyricBridge::~CFrontendLyricBridge() { Stop(); }

	bool CFrontendLyricBridge::Start(uint32_t CloudMusicPid, std::wstring CommandLine)
	{
		Stop();
		if(CloudMusicPid == 0 || !m_Writer.Open(true))
			return false;
		uint64_t PreviousGeneration = 0;
		QmNeteaseHook::SSnapshotV5 PreviousSnapshot{};
		if(m_Writer.Read(&PreviousSnapshot))
			PreviousGeneration = PreviousSnapshot.m_Generation;
		{
			std::scoped_lock Lock(m_Mutex);
			m_CloudMusicPid = CloudMusicPid;
			m_SongId = 0;
			// 保留共享 mapping 中的 generation，并在每次 helper instance
			// 启动时推进一次，避免同一歌曲的重连被误认成旧进程。
			m_Generation = PreviousGeneration == std::numeric_limits<uint64_t>::max() ? 1 : PreviousGeneration + 1;
			m_HasSong = false;
			m_PositionValid = false;
			m_PlayingHint = false;
			m_PlayingHintKnown = false;
			m_PositionMs = 0;
			m_Anchor.Reset();
			m_Timeline.Clear();
			m_Source = QmNeteaseHook::ENeteaseLyricSource::None;
			m_CurrentLyric.clear();
			m_LineStartMs = -1;
			m_LineEndMs = -1;
			m_LastReportTick = 0;
			m_LastProgressTick = 0;
			m_PendingLyricsSongId = 0;
			m_PendingTimeline.Clear();
		}
		m_Stop = false;
		if(!m_Worker.Start(CloudMusicPid, std::move(CommandLine), [this](std::string_view Report) { OnFrontendReport(Report); }))
		{
			m_Writer.Close();
			return false;
		}
		Publish(true);
		m_TickThread = std::thread([this] {
			while(!m_Stop.load(std::memory_order_acquire))
			{
				Tick();
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		});
		return true;
	}

	void CFrontendLyricBridge::Stop()
	{
		m_Stop = true;
		m_Worker.Stop();
		if(m_TickThread.joinable())
			m_TickThread.join();
		m_Writer.Close();
	}

	bool CFrontendLyricBridge::IsConnected() const
	{
		return m_Worker.IsConnected();
	}

	void CFrontendLyricBridge::ClearLyricsLocked()
	{
		m_Timeline.Clear();
		m_Source = QmNeteaseHook::ENeteaseLyricSource::None;
		m_CurrentLyric.clear();
		m_LineStartMs = -1;
		m_LineEndMs = -1;
	}

	void CFrontendLyricBridge::ClearPendingLyricsLocked()
	{
		m_PendingLyricsSongId = 0;
		m_PendingTimeline.Clear();
	}

	bool CFrontendLyricBridge::SwitchSongLocked(uint64_t SongId)
	{
		if((m_HasSong && m_SongId == SongId) || (!m_HasSong && SongId == 0))
			return false;
		m_SongId = SongId;
		m_HasSong = SongId != 0;
		m_Generation = m_Generation == std::numeric_limits<uint64_t>::max() ? 1 : m_Generation + 1;
		m_PositionValid = false;
		m_PlayingHint = false;
		m_PlayingHintKnown = false;
		m_PositionMs = 0;
		m_Anchor.Reset();
		m_LastProgressTick = 0;
		ClearLyricsLocked();
		if(m_HasSong && m_PendingLyricsSongId == SongId && m_PendingTimeline.m_HasTiming)
		{
			m_Timeline = std::move(m_PendingTimeline);
			m_Source = QmNeteaseHook::ENeteaseLyricSource::Frontend;
		}
		ClearPendingLyricsLocked();
		return true;
	}

	void CFrontendLyricBridge::OnFrontendReport(std::string_view Report)
	{
		std::string Kind;
		uint64_t SongId = 0;
		double Position = 0;
		bool HasPosition = false;
		bool Playing = false;
		bool HasPlaying = false;
		NeteaseLyrics::SRawLyrics Raw;
		if(!ParseRawReport(Report, &Kind, &SongId, &Position, &HasPosition, &Playing, &HasPlaying, &Raw))
			return;
		const bool HasRawLyrics = !Raw.m_Lrc.empty() || !Raw.m_Yrc.empty();
		// 只有携带可用业务字段的报告才能刷新 heartbeat。仅含 timestamp
		// 或未知字段的畸形 progress 不能无限延长旧歌词的生命周期。
		if(SongId == 0 && !HasPosition && !HasPlaying && !(Kind == "lyrics" && HasRawLyrics))
			return;
		NeteaseLyrics::STimeline ParsedTimeline;
		const bool ParsedLyrics = Kind == "lyrics" && NeteaseLyrics::ParseRawLyrics(Raw, &ParsedTimeline);
		// 加载中的空结果或格式异常不能清掉已经生效的时间轴。歌曲切换由
		// progress 的 songId 立即清理，合法歌词报告只负责安装新时间轴。
		if(Kind == "lyrics" && !ParsedLyrics)
			return;
		const uint64_t ReportTick = NowTick();
		bool NeedPublish = false;
		bool ForcePublish = false;
		bool PositionAnchored = false;
		{
			std::scoped_lock Lock(m_Mutex);
			if(Kind == "progress")
			{
				if(SongId != 0 && SwitchSongLocked(SongId))
					ForcePublish = true;
				m_LastReportTick = ReportTick;
				m_LastProgressTick = ReportTick;
				if(HasPlaying)
				{
					m_PlayingHint = Playing;
					m_PlayingHintKnown = true;
					if(m_PositionValid)
						m_Anchor.Update(m_PositionMs, m_PlayingHint);
				}
				if(HasPosition)
				{
					m_PositionMs = ToPosition(Position);
					m_PositionValid = true;
					PositionAnchored = true;
					// 没有状态字段时只把进度作为同步锚点，不能把“收到过
					// progress”当成标准 Playing 状态或后续 Paused 推断依据。
					// 进度事件本身足以作为时钟锚点的“正在推进”信号；这个
					// 临时值只影响私有歌词时钟，不会写入 SMTC 播放状态。
					m_Anchor.Update(m_PositionMs, m_PlayingHintKnown ? m_PlayingHint : true);
				}
				NeedPublish = true;
			}
			else
			{
				const NeteaseLyrics::ELyricSongDecision Decision = NeteaseLyrics::DecideLyricSongReport(
					m_SongId, m_HasSong, SongId, m_LastProgressTick, ReportTick, FRONTEND_REPORT_STALE_MS);
				if(Decision == NeteaseLyrics::ELyricSongDecision::Reject)
					return;
				if(Decision == NeteaseLyrics::ELyricSongDecision::Defer)
				{
					if(ParsedLyrics && SongId != 0)
					{
						m_PendingLyricsSongId = SongId;
						m_PendingTimeline = std::move(ParsedTimeline);
					}
					return;
				}
				if(Decision == NeteaseLyrics::ELyricSongDecision::SwitchSong && SwitchSongLocked(SongId))
					ForcePublish = true;
				m_LastReportTick = ReportTick;
				if(ParsedLyrics && m_Source == QmNeteaseHook::ENeteaseLyricSource::Frontend && NeteaseLyrics::AreTimelinesEquivalent(m_Timeline, ParsedTimeline))
				{
					// Worker 会周期性重放同一份歌词。仅刷新 heartbeat，不能
					// 清空当前句，否则 HUD 会收到无效脉冲并反复收起。
					return;
				}
				m_Timeline = std::move(ParsedTimeline);
				m_Source = QmNeteaseHook::ENeteaseLyricSource::Frontend;
				m_CurrentLyric.clear();
				m_LineStartMs = -1;
				m_LineEndMs = -1;
				ClearPendingLyricsLocked();
				NeedPublish = true;
			}
		}
		if(NeedPublish)
			Publish(ForcePublish, PositionAnchored);
	}

	void CFrontendLyricBridge::Tick()
	{
		const bool WorkerConnected = m_Worker.IsConnected();
		{
			std::scoped_lock Lock(m_Mutex);
			if(!m_HasSong)
				return;
			const uint64_t Now = NowTick();
			const uint64_t ReportTimeout = WorkerConnected ? FRONTEND_REPORT_STALE_MS : 1000;
			if(m_LastReportTick != 0 && Now - m_LastReportTick > ReportTimeout)
			{
				// 网易云暂停后通常不再发送 progress。连接仍然存在且已经收到
				// 明确 paused 状态时保留当前句；断线或播放中超时才清理 stale 数据。
				const bool KeepConnectedLine = NeteaseLyrics::ShouldPreserveConnectedLyric(
					WorkerConnected,
					m_PlayingHintKnown,
					m_PlayingHint,
					m_PositionValid,
					!m_CurrentLyric.empty() || !m_Timeline.m_vLines.empty());
				if(!KeepConnectedLine && (!m_CurrentLyric.empty() || !m_Timeline.m_vLines.empty()))
					ClearLyricsLocked();
				if(!KeepConnectedLine)
					m_PositionValid = false;
			}
			if(m_PositionValid)
			{
				m_PositionMs = m_Anchor.Estimate();
				const NeteaseLyrics::SSelectedLine Selected = NeteaseLyrics::SelectCurrentLine(m_Timeline, m_PositionMs);
				if(Selected.m_pLine == nullptr)
				{
					m_CurrentLyric.clear();
					m_LineStartMs = -1;
					m_LineEndMs = -1;
				}
				else
				{
					m_CurrentLyric = Selected.m_pLine->m_Text;
					m_LineStartMs = Selected.m_pLine->m_StartMs;
					m_LineEndMs = Selected.m_pLine->m_EndMs;
				}
			}
		}
		Publish(false);
	}

	void CFrontendLyricBridge::Publish(bool Force, bool PositionAnchored)
	{
		QmNeteaseHook::SSnapshotV5 Snapshot{};
		bool HighPriorityTimelineActive = false;
		{
			std::scoped_lock Lock(m_Mutex);
			Snapshot.m_CloudMusicPid = m_CloudMusicPid;
			Snapshot.m_Generation = m_Generation;
			Snapshot.m_SongId = m_SongId;
			Snapshot.m_UpdatedAtTick = NowTick();
			Snapshot.m_PositionMs = std::max<int64_t>(0, m_PositionMs);
			Snapshot.m_Flags = 0;
			if(m_HasSong && m_SongId != 0)
				Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_HAS_SONG;
			if(m_PositionValid)
				Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_POSITION_VALID;
			if(PositionAnchored && m_PositionValid)
				Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_POSITION_ANCHORED;
			if(m_PlayingHint)
				Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_PLAYING_HINT;
			HighPriorityTimelineActive = m_Timeline.m_HasTiming && !m_Timeline.m_vLines.empty() &&
						     m_Source == QmNeteaseHook::ENeteaseLyricSource::Frontend;
			if(HighPriorityTimelineActive)
				Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_LYRIC_TIMELINE_VALID;
			if(HighPriorityTimelineActive)
				Snapshot.m_LyricSource = (uint32_t)m_Source;
			if(!m_CurrentLyric.empty() && m_Source != QmNeteaseHook::ENeteaseLyricSource::None)
			{
				Snapshot.m_Flags |= QmNeteaseHook::V5_FLAG_LYRIC_VALID;
				Snapshot.m_LyricSource = (uint32_t)m_Source;
				Snapshot.m_LineStartMs = m_LineStartMs;
				Snapshot.m_LineEndMs = m_LineEndMs;
				QmNeteaseHook::CopyUtf8Truncated(Snapshot.m_aCurrentLyric, sizeof(Snapshot.m_aCurrentLyric), m_CurrentLyric.data(), m_CurrentLyric.size());
			}
		}
		if(!Force && !HighPriorityTimelineActive)
		{
			QmNeteaseHook::SSnapshotV5 Existing{};
			if(m_Writer.Read(&Existing) && QmNeteaseHook::ShouldPreserveDesktopFallbackV5(Existing, Snapshot, Snapshot.m_UpdatedAtTick, FALLBACK_SNAPSHOT_STALE_MS))
				return;
		}
		m_Writer.Publish(Snapshot);
	}
}
