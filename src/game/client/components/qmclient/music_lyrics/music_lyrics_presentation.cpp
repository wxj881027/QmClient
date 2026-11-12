#include "music_lyrics_presentation.h"

#include "music_lyrics_display.h"

#include <base/secure.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>
#include <game/client/gameclient.h>

namespace
{
	int NextGroupDelayMs()
	{
		// 组间按整秒随机等待 360～1200 秒，组内句间隔仍为 3 秒。
		return (360 + secure_rand_below(841)) * 1000;
	}
}

void CQmLyricsPresentation::OnConsoleInit()
{
	Console()->Register("+qm_sing", "", CFGFLAG_CLIENT, ConSing, this, "Double-tap the bound key within 400 ms to send the current lyric to public chat");
}

void CQmLyricsPresentation::ConSing(IConsole::IResult *pResult, void *pUserData)
{
	auto *pThis = static_cast<CQmLyricsPresentation *>(pUserData);
	if(pResult->GetInteger(0) == 0)
	{
		pThis->m_Singing.OnStroke(false, 0, {});
		return;
	}
	int SourceId;
	const auto Source = pThis->CurrentSource(SourceId);
	pThis->SynchronizeSinging(Source, SourceId);
	const int64_t NowMs = time_get() * 1000 / time_freq();
	const std::string Message = pThis->m_Singing.OnStroke(true, NowMs, Source.m_Original);
	if(!Message.empty())
	{
		const bool Sent = pThis->GameClient()->m_Chat.SendAutomaticChat(g_Config.m_ClDummy, Message.c_str());
		pThis->m_Humming.OnSingingResult(Sent, NowMs, NextGroupDelayMs());
	}
}

void CQmLyricsPresentation::OnReset()
{
	m_Frame = {};
	m_Humming.Reset();
	m_Singing.Reset();
	OnMapLoad();
}

void CQmLyricsPresentation::OnMapLoad()
{
	m_WorldLyrics.Reset();
	m_WorldLyricsRenderer.Reset(*TextRender());
}

void CQmLyricsPresentation::OnShutdown()
{
	m_WorldLyricsRenderer.Reset(*TextRender());
}

void CQmLyricsPresentation::OnWindowResize()
{
	m_WorldLyricsRenderer.Reset(*TextRender());
}

void CQmLyricsPresentation::OnRender()
{
	if(!g_Config.m_QmWorldLyrics || Client()->State() != IClient::STATE_ONLINE ||
		(!m_WorldLyrics.Current().m_Active && !m_WorldLyrics.Previous().m_Active))
		return;
	m_WorldLyricsRenderer.Render(*TextRender(), *Graphics(), *RenderTools(), m_WorldLyrics,
		GameClient()->m_Camera.m_Center, GameClient()->m_Camera.m_Zoom, time_get() / static_cast<double>(time_freq()));
}

void CQmLyricsPresentation::OnStateChange(int NewState, int OldState)
{
	OnReset();
}

QmMusicLyrics::SSourceSnapshot CQmLyricsPresentation::CurrentSource(int &SourceId) const
{
	QmMusicLyrics::SSourceSnapshot Source;
	SourceId = 0;
	if(g_Config.m_QmNeteaseHookEnable)
	{
		Source = GameClient()->m_NeteaseIntegration.LyricsSnapshot();
		SourceId = 1;
	}
	if(Source.m_SongId == 0 && (g_Config.m_QmSodaHookEnable || g_Config.m_QmKugouHookEnable || g_Config.m_QmQQMusicHookEnable))
	{
		Source = GameClient()->m_MusicLyricsIntegration.LyricsSnapshot();
		SourceId = 1 + GameClient()->m_MusicLyricsIntegration.ActiveSource();
	}
	if(Source.m_SongId == 0 && g_Config.m_QmSpotifyEnable)
	{
		Source = GameClient()->m_SpotifyIntegration.LyricsSnapshot();
		SourceId = 5;
	}
	return Source;
}

void CQmLyricsPresentation::SynchronizeSinging(const QmMusicLyrics::SSourceSnapshot &Source, int SourceId)
{
	const int Connection = g_Config.m_ClDummy;
	const bool Online = Client()->State() == IClient::STATE_ONLINE &&
			    (Connection == IClient::CONN_MAIN || Client()->DummyConnected());
	CSystemMediaControls::SState Media;
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool WindowActive = pEngineGraphics == nullptr || pEngineGraphics->WindowActive() != 0;
	const bool Available = g_Config.m_QmSinging && Online && Source.m_SongId != 0 && Source.m_Active &&
			       !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive() &&
			       !GameClient()->m_GameConsole.IsActive() && WindowActive &&
			       GameClient()->m_SystemMediaControls.GetStateSnapshot(Media) && Media.m_Playing;
	m_Singing.Synchronize(Available, {SourceId, Source.m_SongId, Source.m_Generation, Source.m_ProducerPid,
						 Connection, Online ? GameClient()->m_aLocalIds[Connection] : -1});
}

void CQmLyricsPresentation::OnUpdate()
{
	int SourceId;
	const auto Source = CurrentSource(SourceId);
	SynchronizeSinging(Source, SourceId);

	m_Frame = {};
	if(g_Config.m_QmLyrics && g_Config.m_QmLyricsInMediaIsland)
	{
		m_Frame.m_Active = Source.m_Active && !QmMusicLyrics::IsLyricCredit(Source.m_Original);
		m_Frame.m_Progress = QmMusicLyrics::LyricProgress(Source);
		const auto Rows = QmMusicLyrics::DisplayRows(Source.m_Original, Source.m_Translation, g_Config.m_QmLyricsLanguage);
		m_Frame.m_RowCount = Rows.m_Count;
		for(int Index = 0; Index < Rows.m_Count; ++Index)
			QmNeteaseHook::CopyUtf8Truncated(m_Frame.m_aaRows[Index], sizeof(m_Frame.m_aaRows[Index]), Rows.m_aRows[Index].data(), Rows.m_aRows[Index].size());
	}

	CSystemMediaControls::SState Media;
	const int Connection = g_Config.m_ClDummy;
	const bool Online = Client()->State() == IClient::STATE_ONLINE &&
			    (Connection == IClient::CONN_MAIN || Client()->DummyConnected());
	const bool Playing = (g_Config.m_QmHumming || g_Config.m_QmWorldLyrics) && Online &&
			     GameClient()->m_SystemMediaControls.GetStateSnapshot(Media) && Media.m_Playing;
	const int64_t Now = time_get();
	const bool HadWorldLyrics = m_WorldLyrics.Current().m_Active || m_WorldLyrics.Previous().m_Active;
	if(g_Config.m_QmWorldLyrics || HadWorldLyrics)
	{
		// 每句生成时取当前场景上方的位置；后续镜头移动只改变投影。
		const vec2 Anchor = GameClient()->m_Camera.m_Center + vec2(0.0f, -140.0f * GameClient()->m_Camera.m_Zoom);
		m_WorldLyrics.Update(g_Config.m_QmWorldLyrics && Online && Playing, SourceId, Source, g_Config.m_QmLyricsLanguage, Anchor, Now / static_cast<double>(time_freq()));
		if(!m_WorldLyrics.Current().m_Active && !m_WorldLyrics.Previous().m_Active)
			m_WorldLyricsRenderer.Reset(*TextRender());
	}
	const bool Available = g_Config.m_QmHumming && Online && Source.m_SongId != 0 &&
			       Source.m_Active && Source.m_pData != nullptr && Source.m_pData->HasLyrics() &&
			       Playing;
	const QmMusicLyrics::SHummingIdentity Identity{SourceId, Source.m_SongId, Source.m_Generation,
		Source.m_ProducerPid, Connection, Online ? GameClient()->m_aLocalIds[Connection] : -1};
	m_Humming.Synchronize(Available, Identity);
	if(!Available)
		return;
	const int64_t NowMs = Now * 1000 / time_freq();
	if(m_Humming.NeedsSchedule())
		m_Humming.Schedule(NowMs, NextGroupDelayMs());
	if(m_Humming.GroupDue(NowMs) && !m_Humming.BeginGroup(Source.m_pData->m_Timeline, secure_rand_below(0x7fffffff), NowMs))
		m_Humming.Schedule(NowMs, NextGroupDelayMs());
	if(const std::string *pMessage = m_Humming.PendingLine(NowMs))
	{
		const bool Sent = GameClient()->m_Chat.SendAutomaticChat(Connection, pMessage->c_str());
		m_Humming.FinishLine(Sent, NowMs, NextGroupDelayMs());
	}
}
