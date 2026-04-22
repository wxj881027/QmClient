/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmRt.h"

#include "../gameclient.h"

#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <algorithm>

namespace
{
bool PerfDebugEnabled()
{
	return g_Config.m_QmPerfDebug != 0;
}

double PerfDebugThresholdMs()
{
	return g_Config.m_QmPerfDebugThresholdMs > 0 ? g_Config.m_QmPerfDebugThresholdMs : 1.0;
}

void LogPerfStage(const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
{
	if(!PerfDebugEnabled())
		return;
	if(!Force && DurationMs < PerfDebugThresholdMs())
		return;

	if(pExtra != nullptr && pExtra[0] != '\0')
		dbg_msg("perf/ui_runtime", "stage=%s duration_ms=%.3f %s", pStage, DurationMs, pExtra);
	else
		dbg_msg("perf/ui_runtime", "stage=%s duration_ms=%.3f", pStage, DurationMs);
}
}

void CUiRuntimeV2::Init(CGameClient *pGameClient)
{
	m_pGameClient = pGameClient;
	Reset();
}

void CUiRuntimeV2::Reset()
{
	m_Tree.Reset();
	m_AnimRuntime.Reset();
	m_RenderBridge.BeginFrame();
	m_LastStats = {};
	m_DebugLogAccumulator = 0.0f;
}

bool CUiRuntimeV2::Enabled() const
{
	return m_pGameClient != nullptr;
}

void CUiRuntimeV2::OnRender()
{
	if(!Enabled())
		return;

	CPerfTimer RenderTimer;
	float Dt = m_pGameClient->Client()->RenderFrameTime();
	if(Dt < 0.0f)
		Dt = 0.0f;
	Dt = std::min(Dt, 1.0f / 15.0f);

	{
		CPerfTimer StageTimer;
		m_Tree.BeginFrame();
		LogPerfStage("tree_begin_frame", StageTimer.ElapsedMs());
	}
	{
		CPerfTimer StageTimer;
		m_AnimRuntime.Advance(Dt);
		LogPerfStage("anim_advance", StageTimer.ElapsedMs());
	}
	{
		CPerfTimer StageTimer;
		m_RenderBridge.BeginFrame();
		LogPerfStage("render_bridge_begin_frame", StageTimer.ElapsedMs());
	}
	{
		CPerfTimer StageTimer;
		m_Tree.EndFrame();
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "dt_ms=%.3f", Dt * 1000.0f);
		LogPerfStage("tree_end_frame", StageTimer.ElapsedMs(), false, aExtra);
	}

	m_LastStats.m_BuildTreeMs = 0.0f;
	m_LastStats.m_LayoutMs = 0.0f;
	m_LastStats.m_AnimMs = Dt * 1000.0f;
	m_LastStats.m_RenderBridgeMs = 0.0f;
	m_LastStats.m_NodeCount = m_Tree.NodeCount();

	if(g_Config.m_QmUiRuntimeV2Debug)
	{
		m_DebugLogAccumulator += Dt;
		if(m_DebugLogAccumulator >= 2.0f)
		{
			m_DebugLogAccumulator = 0.0f;
			dbg_msg("qm_ui", "runtime active: nodes=%d, anim_ms=%.3f", m_LastStats.m_NodeCount, m_LastStats.m_AnimMs);
		}
	}

	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "nodes=%d", m_LastStats.m_NodeCount);
	LogPerfStage("ui_runtime_total", RenderTimer.ElapsedMs(), false, aExtra);
}

const SUiV2PerfStats &CUiRuntimeV2::LastStats() const
{
	return m_LastStats;
}
