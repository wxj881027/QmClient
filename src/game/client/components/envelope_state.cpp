#include "envelope_state.h"

#include <game/client/gameclient.h>
#include <game/map/map_render_items.h>

#include <chrono>

CEnvelopeState::CEnvelopeState(IMap *pMap, bool OnlineOnly) :
	m_pMap(pMap)
{
	m_pEnvelopePoints = std::make_shared<CMapBasedEnvelopePointAccess>(m_pMap);
	m_OnlineOnly = OnlineOnly;
}

void CEnvelopeState::EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const
{
	using namespace std::chrono;

	nanoseconds Time = milliseconds(TimeOffsetMillis);
	if(!m_OnlineOnly)
	{
		// 离线渲染使用本机时钟。
		Time += time_get_nanoseconds();
	}
	else if(const auto *pGameInfo = GameClient()->m_Snap.m_pGameInfoObj)
	{
		const int Conn = g_Config.m_ClDummy;
		const bool UseSnapshot = Client()->State() == IClient::STATE_DEMOPLAYBACK || !g_Config.m_ClPredict ||
					 (GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW);
		const int Tick = UseSnapshot ? Client()->GameTick(Conn) : Client()->PredGameTick(Conn);
		const double IntraTick = UseSnapshot ? Client()->IntraGameTick(Conn) : Client()->PredIntraGameTick(Conn);
		const auto SampleTime = EnvelopeStateTime::OnlineSampleTime(
			pGameInfo->m_RoundStartTick, Tick, Client()->PrevGameTick(Conn), IntraTick, !UseSnapshot, TimeOffsetMillis);
		if(!SampleTime)
			return;
		Time = *SampleTime;
	}

	EvalAtTime(EnvelopeIndex, Result, Channels, Time);
}

void CEnvelopeState::EnvelopeEvalAtTick(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels,
	int Tick, float IntraTick) const
{
	const auto Time = EnvelopeStateTime::OnlineSampleTime(-1, Tick, Tick, IntraTick, true, TimeOffsetMillis);
	if(Time)
		EvalAtTime(EnvelopeIndex, Result, Channels, *Time);
}

void CEnvelopeState::EvalAtTime(int EnvelopeIndex, ColorRGBA &Result, size_t Channels,
	std::chrono::nanoseconds Time) const
{
	if(!m_pMap || !m_pEnvelopePoints)
		return;

	int EnvelopeStart, EnvelopeNum;
	m_pMap->GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeNum);
	if(EnvelopeIndex < 0 || EnvelopeIndex >= EnvelopeNum)
		return;

	const auto Item = MapRenderItems::ReadEnvelope(*m_pMap, EnvelopeStart + EnvelopeIndex);
	if(!Item || Item->m_Channels <= 0)
		return;
	Channels = minimum<size_t>(Channels, Item->m_Channels, CEnvPoint::MAX_CHANNELS);

	m_pEnvelopePoints->SetPointsRange(Item->m_StartPoint, Item->m_NumPoints);
	if(m_pEnvelopePoints->NumPoints() == 0)
		return;

	CRenderMap::RenderEvalEnvelope(m_pEnvelopePoints.get(), Time, Result, Channels);
}
