#include "qm_lxmusic_integration.h"

#include "qm_lxmusic_config.h"

#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace
{
	// 本机回环请求,超时与重试都可以压得很短。
	constexpr int64_t STATUS_INTERVAL_MS = 500;
	constexpr int64_t STATUS_IDLE_INTERVAL_MS = 2000;
	constexpr int64_t STATUS_FAILURE_INTERVAL_MS = 5000;
	// 状态快照超过这个时长就不再外推进度,避免请求失败时歌词继续跑飞。
	constexpr int64_t STATUS_FRESH_MS = 3000;
	// 重新读 LX 自身配置的间隔:用户改端口后最迟这么久跟上。
	constexpr int64_t CONFIG_REFRESH_INTERVAL_MS = 3000;
	constexpr size_t MAX_RESPONSE_SIZE = 1 << 20;

	constexpr const char *STATUS_FILTER = "status,name,singer,albumName,picUrl,progress,duration,playbackRate";

	int64_t MonotonicTickMs()
	{
		return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	// Http() 是 CComponentInterfaces 的受保护访问器,匿名命名空间里需要显式传入。
	std::shared_ptr<IHttpRequest> StartRequest(IHttp *pHttp, const char *pUrl)
	{
		auto pRequest = std::shared_ptr<IHttpRequest>(HttpGet(pUrl));
		pRequest->LogProgress(HTTPLOG::FAILURE);
		// LX 的开放 API 只提供本机明文 http:引擎默认禁 http 且会继承环境代理,
		// 两者都会让请求在连到 LX 之前就失败(libcurl error 1/7)。
		pRequest->AllowInsecureProtocol(true);
		pRequest->Proxy("");
		// 401 等错误状态需要读取响应体来区分"开放 API 未开启"与端口被占用。
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{1000, 2500, 250, 3});
		pRequest->MaxResponseSize(MAX_RESPONSE_SIZE);
		pRequest->HeaderString("Accept", "application/json");
		pHttp->Run(pRequest);
		return pRequest;
	}

	void CopyUtf8(char *pBuffer, size_t BufferSize, std::string_view Text)
	{
		if(pBuffer == nullptr || BufferSize == 0)
			return;
		const std::string Truncated = NeteaseLyrics::TruncateUtf8(Text, BufferSize - 1);
		str_copy(pBuffer, Truncated.c_str(), BufferSize);
	}
} // namespace

struct CLxMusicIntegration::SImpl
{
	// 配置同步。
	bool m_ConfigInitialized = false;
	bool m_LastEnabled = false;
	int m_LastPort = 0;

	// 从 LX Music 自身配置里发现的开放 API 信息。
	QmLxMusic::SClientConfig m_ClientConfig;
	int64_t m_NextConfigRefreshTick = 0;
	int m_ResolvedPort = 0;

	// 连接与请求。
	std::shared_ptr<IHttpRequest> m_pStatusRequest;
	std::shared_ptr<IHttpRequest> m_pLyricRequest;
	int64_t m_NextStatusTick = 0;
	std::string m_Error;
	bool m_OpenApiRejected = false;

	// 播放状态。m_StatusBaseTick 是本帧应用该快照的单调时刻,用于外推进度。
	QmLxMusic::SStatus m_Status;
	bool m_HasStatus = false;
	int64_t m_StatusBaseTick = 0;

	// 歌曲身份。
	bool m_HasIdentity = false;
	std::string m_IdentityTitle;
	std::string m_IdentityArtist;
	uint64_t m_SongId = 0;

	// 歌词。
	QmMusicLyrics::SLyricsData m_Lyrics;
	bool m_HasLyrics = false;
	bool m_ActiveLyrics = false;
	uint64_t m_PendingSongId = 0;
	int m_LyricRetryCount = 0;
	int64_t m_NextLyricRetryTick = 0;

	// 当前句。
	int64_t m_PositionMs = 0;
	std::string m_CurrentLyric;

	// 「一键开启」的结果提示:短时优先展示,避免下一轮轮询立刻覆盖掉。
	std::string m_SetupNotice;
	int64_t m_SetupNoticeUntil = 0;

	void AbortRequests()
	{
		if(m_pStatusRequest != nullptr)
		{
			m_pStatusRequest->Abort();
			m_pStatusRequest.reset();
		}
		if(m_pLyricRequest != nullptr)
		{
			m_pLyricRequest->Abort();
			m_pLyricRequest.reset();
		}
	}

	void ResetSongData()
	{
		if(m_pLyricRequest != nullptr)
		{
			m_pLyricRequest->Abort();
			m_pLyricRequest.reset();
		}
		m_HasIdentity = false;
		m_IdentityTitle.clear();
		m_IdentityArtist.clear();
		m_SongId = 0;
		m_Lyrics = {};
		m_HasLyrics = false;
		m_ActiveLyrics = false;
		m_PendingSongId = 0;
		m_LyricRetryCount = 0;
		m_NextLyricRetryTick = 0;
		m_PositionMs = 0;
		m_CurrentLyric.clear();
	}

	void ResetConnection()
	{
		AbortRequests();
		m_HasStatus = false;
		m_Status = {};
		m_StatusBaseTick = 0;
		m_NextStatusTick = 0;
		m_Error.clear();
		m_OpenApiRejected = false;
		ResetSongData();
	}
};

CLxMusicIntegration::CLxMusicIntegration() :
	m_pImpl(std::make_unique<SImpl>()) {}

CLxMusicIntegration::~CLxMusicIntegration() = default;

void CLxMusicIntegration::OnInit()
{
	m_pImpl->m_ConfigInitialized = false;
	m_pImpl->m_LastEnabled = false;
	m_pImpl->m_LastPort = 0;
	m_pImpl->m_ClientConfig = {};
	m_pImpl->m_NextConfigRefreshTick = 0;
	m_pImpl->m_ResolvedPort = 0;
	m_pImpl->ResetConnection();
}

void CLxMusicIntegration::OnShutdown()
{
	m_pImpl->AbortRequests();
	m_pImpl->ResetConnection();
}

void CLxMusicIntegration::OnReset()
{
	// 地图切换等重置:清掉歌曲与歌词,保留失败状态以外的一切由下一轮轮询重建。
	m_pImpl->ResetSongData();
	m_pImpl->m_HasStatus = false;
	m_pImpl->m_NextStatusTick = 0;
}

void CLxMusicIntegration::OnUpdate()
{
	if(GameClient() == nullptr)
		return;

	const bool Enabled = g_Config.m_QmLxMusicEnable != 0;
	const int ManualPort = g_Config.m_QmLxMusicPort;
	if(!m_pImpl->m_ConfigInitialized || Enabled != m_pImpl->m_LastEnabled || ManualPort != m_pImpl->m_LastPort)
	{
		m_pImpl->m_ConfigInitialized = true;
		m_pImpl->m_LastEnabled = Enabled;
		m_pImpl->m_LastPort = ManualPort;
		// 用户刚改过端口覆盖设置,下一帧立刻重读 LX 配置而不是等刷新间隔。
		m_pImpl->m_NextConfigRefreshTick = 0;
		m_pImpl->ResetConnection();
	}
	if(!Enabled)
	{
		if(m_pImpl->m_HasStatus || !m_pImpl->m_Error.empty())
			m_pImpl->ResetConnection();
		return;
	}

	RefreshClientConfig();
	const int Port = ResolvePort();
	if(Port != m_pImpl->m_ResolvedPort)
	{
		m_pImpl->m_ResolvedPort = Port;
		m_pImpl->ResetConnection();
	}

	// 读到 LX 明确关闭开放 API 时不再空转请求;手动指定端口仍尝试,
	// 因为配置文件可能滞后或来自 portable 版(不写 %APPDATA%)。
	if(g_Config.m_QmLxMusicPort == 0 && m_pImpl->m_ClientConfig.m_Found && !m_pImpl->m_ClientConfig.m_OpenApiEnabled)
	{
		if(m_pImpl->m_pStatusRequest != nullptr || m_pImpl->m_HasStatus)
			m_pImpl->ResetConnection();
		m_pImpl->m_Error = "LX Music 未开启开放 API（设置 → 开放 API）";
		return;
	}

	PollStatusPipeline();
	PollLyricPipeline();

	// 进度外推:LX 的 /status 每次给出当时的秒数,不附带更新时间戳,
	// 因此用本地单调时钟补齐两次轮询之间的滚动。
	const int64_t NowMs = MonotonicTickMs();
	int64_t PositionMs = m_pImpl->m_Status.m_PositionMs;
	if(m_pImpl->m_HasStatus && m_pImpl->m_Status.IsPlaying() && NowMs - m_pImpl->m_StatusBaseTick <= STATUS_FRESH_MS)
	{
		const int64_t ElapsedMs = NowMs - m_pImpl->m_StatusBaseTick;
		if(ElapsedMs > 0)
			PositionMs += (int64_t)((double)ElapsedMs * m_pImpl->m_Status.m_PlaybackRate);
	}
	m_pImpl->m_PositionMs = std::max<int64_t>(0, PositionMs);

	if(m_pImpl->m_HasLyrics)
	{
		const NeteaseLyrics::SSelectedLine Selected = NeteaseLyrics::SelectCurrentLine(m_pImpl->m_Lyrics.m_Timeline, m_pImpl->m_PositionMs);
		if(Selected.m_pLine == nullptr)
			m_pImpl->m_CurrentLyric.clear();
		else
			m_pImpl->m_CurrentLyric = Selected.m_pLine->m_Text;
		m_pImpl->m_ActiveLyrics = true;
	}
	else
	{
		m_pImpl->m_CurrentLyric.clear();
		m_pImpl->m_ActiveLyrics = false;
	}
}

void CLxMusicIntegration::RefreshClientConfig()
{
	const int64_t NowMs = MonotonicTickMs();
	if(NowMs < m_pImpl->m_NextConfigRefreshTick)
		return;
	m_pImpl->m_NextConfigRefreshTick = NowMs + CONFIG_REFRESH_INTERVAL_MS;

	QmLxMusic::SClientConfig Config;
	if(QmLxMusic::LoadClientConfig(&Config))
	{
		m_pImpl->m_ClientConfig = Config;
	}
	else
	{
		m_pImpl->m_ClientConfig = {};
	}
}

int CLxMusicIntegration::ResolvePort() const
{
	// 手动值优先:覆盖设置存在就是为了应对读取不到配置的场景(portable 版等)。
	if(g_Config.m_QmLxMusicPort > 0)
		return g_Config.m_QmLxMusicPort;
	if(m_pImpl->m_ClientConfig.m_OpenApiPort > 0)
		return m_pImpl->m_ClientConfig.m_OpenApiPort;
	return QmLxMusic::DEFAULT_OPEN_API_PORT;
}

void CLxMusicIntegration::PollStatusPipeline()
{
	const int64_t NowMs = MonotonicTickMs();

	if(m_pImpl->m_pStatusRequest != nullptr)
	{
		if(!m_pImpl->m_pStatusRequest->Done())
			return;
		std::shared_ptr<IHttpRequest> pRequest = m_pImpl->m_pStatusRequest;
		m_pImpl->m_pStatusRequest.reset();

		const int StatusCode = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
		if(StatusCode == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataLength = 0;
			pRequest->Result(&pData, &DataLength);
			QmLxMusic::SStatus Status;
			if(pData != nullptr && QmLxMusic::ParseStatus(std::string_view((const char *)pData, DataLength), &Status))
			{
				ApplyStatus(Status);
				m_pImpl->m_OpenApiRejected = false;
				m_pImpl->m_Error.clear();
				m_pImpl->m_NextStatusTick = NowMs + (Status.IsPlaying() ? STATUS_INTERVAL_MS : STATUS_IDLE_INTERVAL_MS);
				return;
			}
			m_pImpl->m_Error = "LX Music 返回了无法解析的状态";
		}
		else if(StatusCode == 401 || StatusCode == 403)
		{
			// 该端口的 /status 也需要鉴权,说明监听者不是 LX 开放 API
			// (常见于 LX 数据同步服务占用了同一端口)。只提示一次,避免刷屏。
			if(!m_pImpl->m_OpenApiRejected)
			{
				m_pImpl->m_OpenApiRejected = true;
				log_info("lxmusic", "port %d does not expose LX Music open API (HTTP %d); enable it in LX Music settings or change qm_lxmusic_port", m_pImpl->m_ResolvedPort, StatusCode);
			}
			char aError[192];
			if(m_pImpl->m_ClientConfig.HasPortConflict())
				str_format(aError, sizeof(aError), "开放 API 与数据同步服务都配置为 %d 端口，请在 LX 设置中错开", m_pImpl->m_ResolvedPort);
			else
				str_format(aError, sizeof(aError), "%d 端口被其他服务占用，请在 LX 设置中改开放 API 端口", m_pImpl->m_ResolvedPort);
			m_pImpl->m_Error = aError;
			m_pImpl->ResetSongData();
		}
		else
		{
			m_pImpl->m_Error = m_pImpl->m_ClientConfig.m_Found ?
						   "未连接到 LX Music（请确认已启动并开启开放 API）" :
						   "未找到 LX Music（未安装或从未启动过）";
			m_pImpl->ResetSongData();
		}

		m_pImpl->m_NextStatusTick = NowMs + STATUS_FAILURE_INTERVAL_MS;
		return;
	}

	if(NowMs < m_pImpl->m_NextStatusTick)
		return;

	char aUrl[256];
	str_format(aUrl, sizeof(aUrl), "http://127.0.0.1:%d/status?filter=%s", m_pImpl->m_ResolvedPort, STATUS_FILTER);
	m_pImpl->m_pStatusRequest = StartRequest(Http(), aUrl);
}

void CLxMusicIntegration::PollLyricPipeline()
{
	if(!m_pImpl->m_HasIdentity || m_pImpl->m_HasLyrics)
		return;

	const int64_t NowMs = MonotonicTickMs();

	if(m_pImpl->m_pLyricRequest != nullptr)
	{
		if(!m_pImpl->m_pLyricRequest->Done())
			return;
		std::shared_ptr<IHttpRequest> pRequest = m_pImpl->m_pLyricRequest;
		m_pImpl->m_pLyricRequest.reset();

		bool Accepted = false;
		if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataLength = 0;
			pRequest->Result(&pData, &DataLength);
			QmLxMusic::SLyricPayload Payload;
			// /lyric-all 不携带歌曲身份;若响应到达前已切歌,这份歌词属于上一首,丢弃。
			if(m_pImpl->m_PendingSongId == m_pImpl->m_SongId && pData != nullptr &&
				QmLxMusic::ParseLyricAll(std::string_view((const char *)pData, DataLength), &Payload))
			{
				QmMusicLyrics::SLyricsData Lyrics;
				if(QmLxMusic::BuildLyrics(m_pImpl->m_Status, Payload, &Lyrics))
				{
					m_pImpl->m_Lyrics = std::move(Lyrics);
					m_pImpl->m_HasLyrics = true;
					m_pImpl->m_LyricRetryCount = 0;
					Accepted = true;
				}
			}
		}
		if(!Accepted)
		{
			// 音源可能仍在取歌词(lyric 为空)或请求失败,按退避重试。
			++m_pImpl->m_LyricRetryCount;
			m_pImpl->m_NextLyricRetryTick = NowMs + QmLxMusic::LyricRetryDelayMs(m_pImpl->m_LyricRetryCount);
		}
		return;
	}

	if(m_pImpl->m_LyricRetryCount >= QmLxMusic::MAX_LYRIC_RETRIES || NowMs < m_pImpl->m_NextLyricRetryTick)
		return;

	char aUrl[128];
	str_format(aUrl, sizeof(aUrl), "http://127.0.0.1:%d/lyric-all", m_pImpl->m_ResolvedPort);
	m_pImpl->m_PendingSongId = m_pImpl->m_SongId;
	m_pImpl->m_pLyricRequest = StartRequest(Http(), aUrl);
}

void CLxMusicIntegration::ApplyStatus(const QmLxMusic::SStatus &Status)
{
	// 首次拿到状态说明协议、端口与代理全部打通,留一行日志便于事后排查。
	if(!m_pImpl->m_HasStatus)
		log_info("lxmusic", "connected to LX Music open API on port %d", m_pImpl->m_ResolvedPort);

	// 停止或被清空:没有可跟随的歌曲,连同歌词一起清掉。
	if(Status.m_Title.empty() && Status.m_Artist.empty())
	{
		m_pImpl->ResetSongData();
		m_pImpl->m_Status = Status;
		m_pImpl->m_HasStatus = true;
		m_pImpl->m_StatusBaseTick = MonotonicTickMs();
		return;
	}

	const bool IdentityChanged = !m_pImpl->m_HasIdentity ||
				     m_pImpl->m_IdentityTitle != Status.m_Title ||
				     m_pImpl->m_IdentityArtist != Status.m_Artist;
	if(IdentityChanged)
	{
		m_pImpl->ResetSongData();
		m_pImpl->m_HasIdentity = true;
		m_pImpl->m_IdentityTitle = Status.m_Title;
		m_pImpl->m_IdentityArtist = Status.m_Artist;
		m_pImpl->m_SongId = QmLxMusic::IdentityHash(Status.m_Title, Status.m_Artist);
	}

	m_pImpl->m_Status = Status;
	m_pImpl->m_HasStatus = true;
	m_pImpl->m_StatusBaseTick = MonotonicTickMs();
}

bool CLxMusicIntegration::GetCurrentLyric(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	pBuffer[0] = '\0';
	if(!g_Config.m_QmLyrics || !g_Config.m_QmLyricsInMediaIsland)
		return false;
	if(!m_pImpl->m_HasIdentity || !m_pImpl->m_HasLyrics || m_pImpl->m_CurrentLyric.empty())
		return false;
	CopyUtf8(pBuffer, BufferSize, m_pImpl->m_CurrentLyric);
	return pBuffer[0] != '\0';
}

bool CLxMusicIntegration::HasCurrentLyric() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 &&
	       m_pImpl->m_HasIdentity && m_pImpl->m_HasLyrics && !m_pImpl->m_CurrentLyric.empty();
}

bool CLxMusicIntegration::HasActiveLyrics() const
{
	return g_Config.m_QmLyrics != 0 && g_Config.m_QmLyricsInMediaIsland != 0 &&
	       m_pImpl->m_HasIdentity && m_pImpl->m_ActiveLyrics;
}

uint64_t CLxMusicIntegration::CurrentSongId() const
{
	return m_pImpl->m_SongId;
}

QmMusicLyrics::SSourceSnapshot CLxMusicIntegration::LyricsSnapshot() const
{
	if(!m_pImpl->m_HasIdentity || !m_pImpl->m_HasLyrics)
		return {};
	const auto Selected = NeteaseLyrics::SelectCurrentLine(m_pImpl->m_Lyrics.m_Timeline, m_pImpl->m_PositionMs);
	return {m_pImpl->m_SongId, 0, 0, m_pImpl->m_ActiveLyrics, &m_pImpl->m_Lyrics,
		m_pImpl->m_CurrentLyric, QmMusicLyrics::TranslationAt(m_pImpl->m_Lyrics, Selected.m_Index)};
}

bool CLxMusicIntegration::GetStatus(char *pBuffer, size_t BufferSize) const
{
	if(pBuffer == nullptr || BufferSize == 0)
		return false;
	pBuffer[0] = '\0';
	if(g_Config.m_QmLxMusicEnable == 0)
		return false;

	// 「一键开启」的结果优先显示一段时间,否则会被下一轮轮询的状态覆盖掉。
	if(!m_pImpl->m_SetupNotice.empty() && MonotonicTickMs() < m_pImpl->m_SetupNoticeUntil)
	{
		CopyUtf8(pBuffer, BufferSize, m_pImpl->m_SetupNotice);
		return pBuffer[0] != '\0';
	}

	if(!m_pImpl->m_Error.empty())
	{
		CopyUtf8(pBuffer, BufferSize, m_pImpl->m_Error);
		return pBuffer[0] != '\0';
	}
	if(!m_pImpl->m_HasStatus)
	{
		CopyUtf8(pBuffer, BufferSize, "正在连接 LX Music…");
		return pBuffer[0] != '\0';
	}
	if(!m_pImpl->m_HasIdentity)
	{
		CopyUtf8(pBuffer, BufferSize, "LX Music 未在播放");
		return pBuffer[0] != '\0';
	}
	if(!m_pImpl->m_HasLyrics)
	{
		CopyUtf8(pBuffer, BufferSize, "已识别歌曲，等待歌词");
		return pBuffer[0] != '\0';
	}

	char aBuffer[256];
	str_format(aBuffer, sizeof(aBuffer), "%s - %s%s", m_pImpl->m_IdentityTitle.c_str(), m_pImpl->m_IdentityArtist.c_str(),
		m_pImpl->m_Lyrics.HasTranslation() ? "（含翻译）" : "");
	CopyUtf8(pBuffer, BufferSize, aBuffer);
	return pBuffer[0] != '\0';
}

bool CLxMusicIntegration::NeedsOpenApiSetup() const
{
	if(g_Config.m_QmLxMusicEnable == 0)
		return false;
	// 手动指定端口说明用户自己配过,不再代劳。
	if(g_Config.m_QmLxMusicPort > 0)
		return false;
	// 配置还没读到时按「需要」处理:设置页的测量发生在首帧,
	// 先占住这一行才不会在读到配置后把卡片撑高、截掉下面的状态行。
	return !m_pImpl->m_ClientConfig.m_Found || !m_pImpl->m_ClientConfig.m_OpenApiEnabled;
}

bool CLxMusicIntegration::EnableOpenApi()
{
	std::string Message;
	const QmLxMusic::EEnableResult Result = QmLxMusic::EnableOpenApi(&Message);
	const int64_t NowMs = MonotonicTickMs();
	m_pImpl->m_SetupNotice = Message;
	m_pImpl->m_SetupNoticeUntil = NowMs + 15000;
	if(Result != QmLxMusic::EEnableResult::Ok)
		return false;

	// 配置刚被改写:立刻重读并重建连接,不必等下一次刷新间隔。
	m_pImpl->m_NextConfigRefreshTick = 0;
	m_pImpl->ResetConnection();
	return true;
}
