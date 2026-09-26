#include "music_lyrics_integration.h"

#include "qm_soda_lyric_file.h"

#include <base/color.h>

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace
{
	uint64_t MonotonicTickMs()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	// 保留旧数字查询接口；真正的切歌判断同时比较完整字符串与 generation。
	uint64_t MediaIdToken(std::string_view Text)
	{
		uint64_t Result = 14695981039346656037ULL;
		for(const unsigned char Byte : Text)
			Result = (Result ^ Byte) * 1099511628211ULL;
		return Text.empty() ? 0 : Result;
	}

	constexpr uint64_t LYRIC_LOAD_RETRY_DELAY_MS = 1000;

	class CSodaLyricLoadJob final : public IJob
	{
	public:
		struct SResult
		{
			std::string m_Path;
			uint64_t m_SongId = 0;
			uint64_t m_Generation = 0;
			uint64_t m_Epoch = 0;
			bool m_Parsed = false;
			QmMusicLyrics::SLyricsData m_Lyrics;
		};

	private:
		std::string m_Path;
		uint64_t m_SongId;
		uint64_t m_Generation;
		uint64_t m_Epoch;
		SResult m_Result;

	protected:
		void Run() override
		{
			m_Result.m_Path = m_Path;
			m_Result.m_SongId = m_SongId;
			m_Result.m_Generation = m_Generation;
			m_Result.m_Epoch = m_Epoch;
			std::ifstream File(std::filesystem::path(reinterpret_cast<const char8_t *>(m_Path.c_str())), std::ios::binary);
			if(!File)
				return;
			std::ostringstream Buffer;
			Buffer << File.rdbuf();
			const std::string Json = Buffer.str();
			if(Json.empty())
				return;
			std::string Error;
			m_Result.m_Parsed = QmSodaLyricFile::ParseLyricFileJson(Json, &m_Result.m_Lyrics, &Error);
		}

	public:
		CSodaLyricLoadJob(std::string Path, uint64_t SongId, uint64_t Generation, uint64_t Epoch) :
			m_Path(std::move(Path)),
			m_SongId(SongId),
			m_Generation(Generation),
			m_Epoch(Epoch)
		{
		}

		SResult TakeResult()
		{
			return std::move(m_Result);
		}
	};
}

struct CMusicLyricsIntegration::SImpl
{
	CQmSodaHookProvider m_Provider;
	bool m_HookConfigInitialized = false;
	int m_Source = 0;
	int m_TimeoutMs = 1500;
	std::string m_MediaId;
	uint32_t m_SongProcessId = 0;
	std::string m_Status;
	std::string m_SetupError;
	std::string m_LastHelperPath;
	QmSodaHook::SSnapshot m_Snapshot{};
	bool m_HasSnapshot = false;
	uint64_t m_LastReadTick = 0;

	// 歌词数据。
	QmMusicLyrics::SLyricsData m_Lyrics;
	bool m_HasLyrics = false;
	std::string m_LoadedFilePath;
	uint64_t m_LoadedGeneration = 0;
	uint64_t m_SongId = 0;
	bool m_HasSong = false;
	int64_t m_PositionMs = 0;
	bool m_PositionValid = false;
	bool m_ActiveLyrics = false;
	std::shared_ptr<CSodaLyricLoadJob> m_pLyricLoadJob;
	std::string m_LyricLoadPath;
	uint64_t m_LyricLoadSongId = 0;
	uint64_t m_LyricLoadGeneration = 0;
	uint64_t m_LyricLoadEpoch = 0;
	uint64_t m_NextLyricLoadRetryTick = 0;

	// 当前句选择。
	std::string m_CurrentLyric;
	int64_t m_LineStartMs = -1;
	int64_t m_LineEndMs = -1;
};

CMusicLyricsIntegration::CMusicLyricsIntegration() :
	m_pImpl(std::make_unique<SImpl>()) {}

CMusicLyricsIntegration::~CMusicLyricsIntegration() = default;

void CMusicLyricsIntegration::OnInit()
{
	m_pImpl->m_HookConfigInitialized = false;
	m_pImpl->m_Source = 0;
	m_pImpl->m_MediaId.clear();
	m_pImpl->m_Status.clear();
	m_pImpl->m_LastHelperPath.clear();
	m_pImpl->m_HasSnapshot = false;
	m_pImpl->m_Snapshot = {};
	m_pImpl->m_HasLyrics = false;
	m_pImpl->m_LoadedFilePath.clear();
	m_pImpl->m_LoadedGeneration = 0;
	m_pImpl->m_pLyricLoadJob.reset();
	m_pImpl->m_LyricLoadPath.clear();
	m_pImpl->m_LyricLoadSongId = 0;
	m_pImpl->m_LyricLoadGeneration = 0;
	m_pImpl->m_LyricLoadEpoch = 0;
	m_pImpl->m_NextLyricLoadRetryTick = 0;
	m_pImpl->m_SongId = 0;
	m_pImpl->m_HasSong = false;
	m_pImpl->m_PositionValid = false;
	m_pImpl->m_ActiveLyrics = false;
	m_pImpl->m_CurrentLyric.clear();
	m_pImpl->m_LineStartMs = -1;
	m_pImpl->m_LineEndMs = -1;
	SyncHookConfiguration();
}

void CMusicLyricsIntegration::OnShutdown()
{
	m_pImpl->m_Provider.Stop();
	m_pImpl->m_HasLyrics = false;
	m_pImpl->m_HasSnapshot = false;
	m_pImpl->m_ActiveLyrics = false;
	m_pImpl->m_CurrentLyric.clear();
	++m_pImpl->m_LyricLoadEpoch;
	m_pImpl->m_pLyricLoadJob.reset();
}

void CMusicLyricsIntegration::OnReset()
{
	ClearForStaleMedia();
}

void CMusicLyricsIntegration::SyncHookConfiguration()
{
	const int Source = g_Config.m_QmSodaHookEnable ? 1 : g_Config.m_QmKugouHookEnable ? 2 :
						     g_Config.m_QmQQMusicHookEnable       ? 3 :
											    0;
	const char *pPath = Source == 1 ? g_Config.m_QmSodaHookHelperPath : Source == 2 ? g_Config.m_QmKugouHookHelperPath :
								    Source == 3         ? g_Config.m_QmQQMusicHookHelperPath :
											  "";
	m_pImpl->m_TimeoutMs = Source == 1 ? g_Config.m_QmSodaHookTimeoutMs : Source == 2 ? g_Config.m_QmKugouHookTimeoutMs :
											    g_Config.m_QmQQMusicHookTimeoutMs;
	const bool ConfigurationChanged = !m_pImpl->m_HookConfigInitialized || Source != m_pImpl->m_Source || m_pImpl->m_LastHelperPath != pPath;
	if(ConfigurationChanged)
	{
		m_pImpl->m_Provider.Stop();
		ClearForStaleMedia();
		m_pImpl->m_HookConfigInitialized = true;
		m_pImpl->m_Source = Source;
		m_pImpl->m_LastHelperPath = pPath;
		m_pImpl->m_Status.clear();
		m_pImpl->m_SetupError.clear();
	}
	// Start 自带重启节流，异常退出的采集器不会永久停止或每帧重启。
	if(Source != 0)
		m_pImpl->m_Provider.Start(pPath, Source == 1 ? "soda" : Source == 2 ? "kugou" :
										      "qqmusic");
}

void CMusicLyricsIntegration::ClearForStaleMedia()
{
	m_pImpl->m_HasLyrics = false;
	m_pImpl->m_HasSnapshot = false;
	m_pImpl->m_Snapshot = {};
	m_pImpl->m_HasSong = false;
	m_pImpl->m_SongId = 0;
	m_pImpl->m_MediaId.clear();
	m_pImpl->m_PositionValid = false;
	m_pImpl->m_ActiveLyrics = false;
	m_pImpl->m_CurrentLyric.clear();
	m_pImpl->m_LineStartMs = -1;
	m_pImpl->m_LineEndMs = -1;
	m_pImpl->m_LoadedFilePath.clear();
	m_pImpl->m_LoadedGeneration = 0;
	m_pImpl->m_LyricLoadPath.clear();
	m_pImpl->m_LyricLoadSongId = 0;
	m_pImpl->m_LyricLoadGeneration = 0;
	++m_pImpl->m_LyricLoadEpoch;
	m_pImpl->m_NextLyricLoadRetryTick = 0;
}

void CMusicLyricsIntegration::LoadLyricFile(const char *pPath)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return;
	const std::string Path(pPath);
	const bool IdentityChanged = Path != m_pImpl->m_LyricLoadPath ||
				     m_pImpl->m_LyricLoadSongId != m_pImpl->m_SongId ||
				     m_pImpl->m_LyricLoadGeneration != m_pImpl->m_LoadedGeneration;
	if(IdentityChanged)
	{
		m_pImpl->m_LyricLoadPath = Path;
		m_pImpl->m_LyricLoadSongId = m_pImpl->m_SongId;
		m_pImpl->m_LyricLoadGeneration = m_pImpl->m_LoadedGeneration;
		++m_pImpl->m_LyricLoadEpoch;
		m_pImpl->m_NextLyricLoadRetryTick = 0;
	}
	ProcessLyricLoadJob();
	if(m_pImpl->m_HasLyrics)
		return;
	if(m_pImpl->m_pLyricLoadJob || MonotonicTickMs() < m_pImpl->m_NextLyricLoadRetryTick)
		return;
	auto pJob = std::make_shared<CSodaLyricLoadJob>(
		m_pImpl->m_LyricLoadPath,
		m_pImpl->m_LyricLoadSongId,
		m_pImpl->m_LyricLoadGeneration,
		m_pImpl->m_LyricLoadEpoch);
	m_pImpl->m_pLyricLoadJob = pJob;
	Engine()->AddJob(pJob);
}

void CMusicLyricsIntegration::ProcessLyricLoadJob()
{
	if(!m_pImpl->m_pLyricLoadJob || m_pImpl->m_pLyricLoadJob->State() != IJob::STATE_DONE)
		return;
	CSodaLyricLoadJob::SResult Result = m_pImpl->m_pLyricLoadJob->TakeResult();
	m_pImpl->m_pLyricLoadJob.reset();
	if(!m_pImpl->m_HasSong || Result.m_SongId != m_pImpl->m_SongId ||
		Result.m_Generation != m_pImpl->m_LoadedGeneration || Result.m_Path != m_pImpl->m_LyricLoadPath ||
		Result.m_Epoch != m_pImpl->m_LyricLoadEpoch)
		return;
	m_pImpl->m_LoadedFilePath = Result.m_Path;
	if(Result.m_Parsed && Result.m_Lyrics.HasLyrics())
	{
		m_pImpl->m_Lyrics = std::move(Result.m_Lyrics);
		m_pImpl->m_HasLyrics = true;
		m_pImpl->m_ActiveLyrics = true;
		m_pImpl->m_NextLyricLoadRetryTick = 0;
	}
	else
	{
		m_pImpl->m_HasLyrics = false;
		m_pImpl->m_ActiveLyrics = false;
		m_pImpl->m_NextLyricLoadRetryTick = MonotonicTickMs() + LYRIC_LOAD_RETRY_DELAY_MS;
	}
}

void CMusicLyricsIntegration::OnUpdate()
{
	SyncHookConfiguration();
	if(m_pImpl->m_Source == 0)
	{
		ClearForStaleMedia();
		return;
	}

	QmSodaHook::SSnapshot Snapshot{};
	const bool HasSnapshot = m_pImpl->m_Provider.Read(&Snapshot, m_pImpl->m_TimeoutMs);
	if(!HasSnapshot)
	{
		char aError[256];
		m_pImpl->m_Status = m_pImpl->m_Provider.GetStatus(aError, sizeof(aError)) ? aError : "等待音乐应用的歌词采集器";
		// 短暂读取失败保留有限窗口,避免闪烁;超时后清理。
		if(m_pImpl->m_HasSnapshot)
		{
			const uint64_t Now = MonotonicTickMs();
			if(m_pImpl->m_LastReadTick == 0 || Now - m_pImpl->m_LastReadTick <= (uint64_t)std::max(1, m_pImpl->m_TimeoutMs))
				return;
		}
		ClearForStaleMedia();
		return;
	}
	if(!m_pImpl->m_HasSnapshot || m_pImpl->m_Snapshot.m_Sequence != Snapshot.m_Sequence)
		m_pImpl->m_LastReadTick = MonotonicTickMs();
	m_pImpl->m_Status = Snapshot.m_aError[0] != '\0'                         ? Snapshot.m_aError :
			    (Snapshot.m_Flags & QmSodaHook::FLAG_HAS_LYRIC_FILE) ? "已连接，正在跟随歌词" :
			    (Snapshot.m_Flags & QmSodaHook::FLAG_HAS_SONG)       ? "已连接，等待当前歌曲歌词" :
										   "等待音乐应用播放歌曲";
	m_pImpl->m_Snapshot = Snapshot;
	m_pImpl->m_HasSnapshot = true;

	const bool HasSong = (Snapshot.m_Flags & QmSodaHook::FLAG_HAS_SONG) != 0;
	const std::string MediaId = Snapshot.m_aMediaId;
	const uint64_t SongId = MediaIdToken(MediaId);
	const uint64_t Generation = Snapshot.m_Generation;
	const bool SongChanged = HasSong && (!m_pImpl->m_HasSong || m_pImpl->m_MediaId != MediaId || m_pImpl->m_SongProcessId != Snapshot.m_SodaMusicPid || m_pImpl->m_LoadedGeneration != Generation ||
						    (Snapshot.m_aLyricFilePath[0] != '\0' && !m_pImpl->m_LoadedFilePath.empty() && m_pImpl->m_LoadedFilePath != Snapshot.m_aLyricFilePath));
	if(SongChanged)
	{
		// 歌曲或 generation 变化:清空旧歌词并加载新歌词文件。
		m_pImpl->m_HasLyrics = false;
		m_pImpl->m_CurrentLyric.clear();
		m_pImpl->m_LineStartMs = -1;
		m_pImpl->m_LineEndMs = -1;
		m_pImpl->m_HasSong = true;
		m_pImpl->m_SongId = SongId;
		m_pImpl->m_MediaId = MediaId;
		m_pImpl->m_SongProcessId = Snapshot.m_SodaMusicPid;
		m_pImpl->m_PositionValid = false;
		m_pImpl->m_LoadedGeneration = Generation;
		m_pImpl->m_LoadedFilePath.clear();
		m_pImpl->m_LyricLoadPath.clear();
		m_pImpl->m_LyricLoadSongId = 0;
		m_pImpl->m_LyricLoadGeneration = 0;
		++m_pImpl->m_LyricLoadEpoch;
		m_pImpl->m_NextLyricLoadRetryTick = 0;
		m_pImpl->m_ActiveLyrics = false;
	}
	else if(!HasSong)
	{
		ClearForStaleMedia();
		return;
	}
	// 同一首歌持续播放时,helper 每次发布都带歌词路径(歌曲变化时重写文件)。
	// 若客户端启动晚于 helper、首次快照没有路径,这里在后续快照带路径时补加载。
	if(HasSong && !m_pImpl->m_HasLyrics && (Snapshot.m_Flags & QmSodaHook::FLAG_HAS_LYRIC_FILE) != 0)
		LoadLyricFile(Snapshot.m_aLyricFilePath);

	// 进度有效性每次取快照，不能沿用上一首歌或断连前的进度。
	m_pImpl->m_PositionValid = false;
	if((Snapshot.m_Flags & QmSodaHook::FLAG_POSITION_VALID) != 0)
	{
		m_pImpl->m_PositionMs = Snapshot.m_PositionMs;
		m_pImpl->m_PositionValid = true;
	}

	// 选择当前句(使用统一时间轴选择逻辑)。
	if(m_pImpl->m_HasLyrics && m_pImpl->m_PositionValid)
	{
		const NeteaseLyrics::STimeline &Timeline = m_pImpl->m_Lyrics.m_Timeline;
		// 酷狗和 QQ 歌词的显式行时长可能短于下一句起点,间奏中继续显示最近已开始的一句。
		const NeteaseLyrics::SSelectedLine Selected = (m_pImpl->m_Source == 2 || m_pImpl->m_Source == 3) ?
								      NeteaseLyrics::SelectLatestStartedLine(Timeline, m_pImpl->m_PositionMs) :
								      NeteaseLyrics::SelectCurrentLine(Timeline, m_pImpl->m_PositionMs);
		if(Selected.m_pLine == nullptr)
		{
			m_pImpl->m_CurrentLyric.clear();
			m_pImpl->m_LineStartMs = -1;
			m_pImpl->m_LineEndMs = -1;
		}
		else
		{
			m_pImpl->m_CurrentLyric = Selected.m_pLine->m_Text;
			m_pImpl->m_LineStartMs = Selected.m_pLine->m_StartMs;
			m_pImpl->m_LineEndMs = Selected.m_pLine->m_EndMs;
		}
		m_pImpl->m_ActiveLyrics = true;
	}
	else if(!m_pImpl->m_PositionValid)
		m_pImpl->m_CurrentLyric.clear();
}

bool CMusicLyricsIntegration::GetCurrentLyric(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	pBuffer[0] = '\0';
	if(!g_Config.m_QmLyrics || !g_Config.m_QmLyricsInMediaIsland)
		return false;
	if(!m_pImpl->m_HasSong || !m_pImpl->m_HasLyrics || m_pImpl->m_CurrentLyric.empty())
		return false;
	QmSodaHook::CopyUtf8Truncated(pBuffer, BufferSize, m_pImpl->m_CurrentLyric.data(), m_pImpl->m_CurrentLyric.size());
	return pBuffer[0] != '\0';
}

bool CMusicLyricsIntegration::HasCurrentLyric() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 &&
	       m_pImpl->m_HasSong && m_pImpl->m_HasLyrics && !m_pImpl->m_CurrentLyric.empty();
}

bool CMusicLyricsIntegration::HasActiveLyrics() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 &&
	       m_pImpl->m_HasSong && m_pImpl->m_ActiveLyrics;
}

uint64_t CMusicLyricsIntegration::CurrentSongId() const
{
	return m_pImpl->m_SongId;
}

bool CMusicLyricsIntegration::GetStatus(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	const std::string &Status = m_pImpl->m_SetupError.empty() ? m_pImpl->m_Status : m_pImpl->m_SetupError;
	QmSodaHook::CopyUtf8Truncated(pBuffer, BufferSize, Status.data(), Status.size());
	return pBuffer[0] != '\0';
}

int CMusicLyricsIntegration::ActiveSource() const
{
	return m_pImpl->m_Source;
}

bool CMusicLyricsIntegration::RunKugouSetup(bool Restore)
{
	m_pImpl->m_SetupError.clear();
	if(m_pImpl->m_Provider.RunKugouSetup(Restore))
		return true;
	char aError[256];
	m_pImpl->m_SetupError = m_pImpl->m_Provider.GetStatus(aError, sizeof(aError)) ? aError : "无法启动酷狗接入程序，请检查 qm-music-helper.exe";
	return false;
}
