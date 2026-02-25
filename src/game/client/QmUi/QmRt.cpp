/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmRt.h"

#include "../gameclient.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <algorithm>

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

	float Dt = m_pGameClient->Client()->RenderFrameTime();
	if(Dt < 0.0f)
		Dt = 0.0f;
	Dt = std::min(Dt, 1.0f / 15.0f);

	m_Tree.BeginFrame();
	m_AnimRuntime.Advance(Dt);
	m_RenderBridge.BeginFrame();
	m_Tree.EndFrame();

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
}

const SUiV2PerfStats &CUiRuntimeV2::LastStats() const
{
	return m_LastStats;
}
