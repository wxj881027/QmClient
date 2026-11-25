// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmRt.h"

#include "../gameclient.h"

#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/components/qmclient/perf_logging.h>

#include <cmath>

namespace
{
	void LogPerfStage(IClient *pClient, const char *pPage, const char *pStage, const double DurationMs, const bool Force = false, const char *pExtra = nullptr)
	{
		QmPerfLogStage("perf/ui_runtime", pStage, DurationMs, Force, pClient, pPage, nullptr, pExtra);
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
	ClearPerfContext();
}

bool CUiRuntimeV2::Enabled() const
{
	return m_pGameClient != nullptr;
}

void CUiRuntimeV2::SetPerfContext(const char *pPage, const char *pOperation)
{
	str_copy(m_aPerfPage, pPage != nullptr ? pPage : "", sizeof(m_aPerfPage));
	str_copy(m_aPerfOperation, pOperation != nullptr ? pOperation : "", sizeof(m_aPerfOperation));
}

void CUiRuntimeV2::ClearPerfContext()
{
	m_aPerfPage[0] = '\0';
	m_aPerfOperation[0] = '\0';
}

void CUiRuntimeV2::OnRender()
{
	if(!Enabled())
		return;

	const bool PerfEnabled = QmPerfEnabled();
	const bool TimingEnabled = PerfEnabled || g_Config.m_QmUiRuntimeV2Debug != 0;
	CPerfTimer RenderTimer(TimingEnabled);
	const float RealDt = std::clamp(m_pGameClient->Client()->RenderFrameTime(), 0.0f, QM_UI_LOGIC_MAX_CATCHUP_SEC * 2.0f);
	float Dt = QmUiRenderDeltaSeconds(RealDt);
	m_FrameDt = Dt;
	// 状态动画（页/页签切换、弹层开合）走固定逻辑步长；纯视觉插值继续用
	// 截断后的渲染时间，长帧不会让状态过渡跳帧，也不会让淡入变成瞬切。
	const SQmUiLogicStep LogicStep = QmUiAdvanceLogicClockStep(m_LogicClockAccumulatorSec, RealDt);

	float TreeBeginMs = 0.0f;
	float AnimAdvanceMs = 0.0f;
	float RenderBridgeBeginMs = 0.0f;
	float TreeEndMs = 0.0f;

	{
		CPerfTimer StageTimer(TimingEnabled);
		m_AnimRuntime.Advance(Dt, LogicStep.m_Seconds, LogicStep.m_Fraction);
		AnimAdvanceMs = StageTimer.ElapsedMs();
		LogPerfStage(m_pGameClient->Client(), m_aPerfPage[0] != '\0' ? m_aPerfPage : nullptr, "anim_advance", AnimAdvanceMs);
	}
	{
		CPerfTimer StageTimer(TimingEnabled);
		m_RenderBridge.BeginFrame();
		RenderBridgeBeginMs = StageTimer.ElapsedMs();
		LogPerfStage(m_pGameClient->Client(), m_aPerfPage[0] != '\0' ? m_aPerfPage : nullptr, "render_bridge_begin_frame", RenderBridgeBeginMs);
	}
	{
		// 组件在本函数返回后才登记本帧节点，先用现有集合结算上一帧。
		CPerfTimer StageTimer(TimingEnabled);
		m_Tree.EndFrame(m_AnimRuntime);
		TreeEndMs = StageTimer.ElapsedMs();
	}
	{
		CPerfTimer StageTimer(TimingEnabled);
		m_Tree.BeginFrame();
		TreeBeginMs = StageTimer.ElapsedMs();
	}
	if(PerfEnabled)
	{
		const char *pPerfPage = m_aPerfPage[0] != '\0' ? m_aPerfPage : nullptr;
		char aExtra[96];
		str_format(aExtra, sizeof(aExtra), "dt_ms=%.3f", Dt * 1000.0f);
		LogPerfStage(m_pGameClient->Client(), pPerfPage, "tree_end_frame", TreeEndMs, false, aExtra);
		LogPerfStage(m_pGameClient->Client(), pPerfPage, "tree_begin_frame", TreeBeginMs);
	}

	m_LastStats.m_BuildTreeMs = TreeBeginMs + TreeEndMs;
	m_LastStats.m_AnimMs = AnimAdvanceMs;
	m_LastStats.m_RenderBridgeMs = RenderBridgeBeginMs;
	m_LastStats.m_NodeCount = m_Tree.NodeCount();
	m_LastStats.m_ActiveAnimCount = m_AnimRuntime.ActiveTrackCount();
	m_LastStats.m_QueuedAnimCount = m_AnimRuntime.QueuedTrackCount();

	if(g_Config.m_QmUiRuntimeV2Debug)
	{
		m_DebugLogAccumulator += Dt;
		if(m_DebugLogAccumulator >= 2.0f && m_LastStats.m_AnimMs >= QmPerfThresholdMs())
		{
			m_DebugLogAccumulator = 0.0f;
			dbg_msg("qm_ui", "runtime active: nodes=%d, anim_ms=%.3f", m_LastStats.m_NodeCount, m_LastStats.m_AnimMs);
		}
	}

	// 活动轨道计数仍供菜单调度使用，关闭日志只跳过计时与字符串准备。
	if(!PerfEnabled)
		return;
	const double DurationMs = RenderTimer.ElapsedMs();
	char aExtra[96];
	str_format(aExtra, sizeof(aExtra), "nodes=%d", m_LastStats.m_NodeCount);
	LogPerfStage(m_pGameClient->Client(), m_aPerfPage[0] != '\0' ? m_aPerfPage : nullptr, "ui_runtime_total", DurationMs, false, aExtra);

	// 普通帧每 30 帧汇总一次，慢帧即时记录；未到采样时不构造日志文本。
	m_PerfLogFrameCounter = (m_PerfLogFrameCounter + 1) % 30;
	const bool PerfLogDue = m_PerfLogFrameCounter == 0 || DurationMs >= QmPerfThresholdMs();
	if(!PerfLogDue)
		return;

	char aPayload[256];
	str_format(aPayload, sizeof(aPayload),
		"event=ui_runtime operation=%s nodes=%d anim_ms=%.3f active_anims=%d queued_anims=%d render_bridge_ms=%.3f duration_ms=%.3f",
		m_aPerfOperation[0] != '\0' ? m_aPerfOperation : "unknown",
		m_LastStats.m_NodeCount,
		m_LastStats.m_AnimMs,
		m_LastStats.m_ActiveAnimCount,
		m_LastStats.m_QueuedAnimCount,
		m_LastStats.m_RenderBridgeMs,
		DurationMs);
	QmPerfLogPayload("perf/ui_runtime", aPayload, m_pGameClient->Client(), m_aPerfPage[0] != '\0' ? m_aPerfPage : nullptr);
}

const SUiV2PerfStats &CUiRuntimeV2::LastStats() const
{
	return m_LastStats;
}
