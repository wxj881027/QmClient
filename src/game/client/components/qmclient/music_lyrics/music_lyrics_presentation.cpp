#include "music_lyrics_presentation.h"

#include "music_lyrics_display.h"

#include <base/secure.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/netease_hook/qm_netease_hook_protocol.h>
#include <game/client/gameclient.h>

namespace
{
	int NextGroupDelayMs()
	{
		return (30 + secure_rand_below(61)) * 1000;
	}
}

void CQmLyricsPresentation::OnReset()
{
	m_Frame = {};
	m_Humming.Reset();
}

void CQmLyricsPresentation::OnStateChange(int NewState, int OldState)
{
	OnReset();
}

void CQmLyricsPresentation::OnUpdate()
{
	QmMusicLyrics::SSourceSnapshot Source;
	int SourceId = 0;
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

	m_Frame = {};
	if(g_Config.m_QmLyrics && g_Config.m_QmLyricsInMediaIsland)
	{
		m_Frame.m_Active = Source.m_Active;
		const auto Rows = QmMusicLyrics::DisplayRows(Source.m_Original, Source.m_Translation, g_Config.m_QmLyricsLanguage);
		m_Frame.m_RowCount = Rows.m_Count;
		for(int Index = 0; Index < Rows.m_Count; ++Index)
			QmNeteaseHook::CopyUtf8Truncated(m_Frame.m_aaRows[Index], sizeof(m_Frame.m_aaRows[Index]), Rows.m_aRows[Index].data(), Rows.m_aRows[Index].size());
	}

	CSystemMediaControls::SState Media;
	const int Connection = g_Config.m_ClDummy;
	const bool Online = Client()->State() == IClient::STATE_ONLINE &&
			    (Connection == IClient::CONN_MAIN || Client()->DummyConnected());
	const bool Available = g_Config.m_QmHumming && Online && Source.m_SongId != 0 &&
			       Source.m_Active && Source.m_pData != nullptr && Source.m_pData->HasLyrics() &&
			       GameClient()->m_SystemMediaControls.GetStateSnapshot(Media) && Media.m_Playing;
	const QmMusicLyrics::SHummingIdentity Identity{SourceId, Source.m_SongId, Source.m_Generation,
		Source.m_ProducerPid, Connection, Online ? GameClient()->m_aLocalIds[Connection] : -1};
	m_Humming.Synchronize(Available, Identity);
	if(!Available)
		return;
	const int64_t NowMs = time_get() * 1000 / time_freq();
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
