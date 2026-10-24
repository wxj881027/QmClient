#include "stutter_capture.h"

#include "perf_logging.h"

#include <engine/config.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cinttypes>
#include <utility>

namespace
{
	void CollectFeature(const SConfigVariable *pVariable, void *pUser)
	{
		if(pVariable == nullptr || pVariable->m_Type != SConfigVariable::VAR_INT)
			return;
		const char *pName = pVariable->m_pScriptName;
		if(!str_startswith(pName, "qm_") && !str_startswith(pName, "tc_") && !str_startswith(pName, "cl_"))
			return;
		const auto &Variable = static_cast<const SIntConfigVariable &>(*pVariable);
		if(Variable.m_Max <= 0 || Variable.m_Min != 0 || *Variable.m_pVariable == 0)
			return;
		static_cast<std::vector<std::pair<std::string, int>> *>(pUser)->emplace_back(pName, *Variable.m_pVariable);
	}
}

void CQmStutterCapture::COperations::PrepareComponents(std::vector<SComponent> &vComponents, size_t Count)
{
	vComponents.resize(Count);
}

void CQmStutterCapture::COperations::PrepareSeries(CQmStutterSampleSeries &Series, double Value)
{
	Series.PrepareRecord(Value);
}

CQmStutterCapture::SFeatures CQmStutterCapture::COperations::CaptureFeatures(const SFrame &Frame)
{
	SFeatures Result;
	if(Frame.m_pConfig)
		Frame.m_pConfig->PossibleConfigVariables("", CFGFLAG_CLIENT, CollectFeature, &Result.m_vValues);
	std::sort(Result.m_vValues.begin(), Result.m_vValues.end());
	const SUiContext Context = Frame.m_pfnUiContext ? Frame.m_pfnUiContext(Frame.m_pUser) : SUiContext{};
	Result.m_Page = Context.m_pPage && Context.m_pPage[0] ? Context.m_pPage : "game";
	Result.m_Operation = Context.m_pOperation && Context.m_pOperation[0] ? Context.m_pOperation : "none";
	Result.m_LimitCause = Context.m_LimitCause;
	return Result;
}

void CQmStutterCapture::COperations::Emit(const SFrame &Frame, bool Force, const char *pPayload)
{
	if(Force)
		QmPerfLogPayloadForce("perf/stutter", pPayload, Frame.m_pClient);
	else
		QmPerfLogPayload("perf/stutter", pPayload, Frame.m_pClient);
}

bool CQmStutterCapture::PrepareComponents(size_t Count) noexcept
{
	if(m_vComponents.size() == Count)
		return true;
	try
	{
		COperations DefaultOperations;
		auto &Operations = m_pOperations ? *m_pOperations : DefaultOperations;
		std::vector<SComponent> vPrepared;
		Operations.PrepareComponents(vPrepared, Count);
		m_vComponents.swap(vPrepared);
		ResetWindow();
		m_Tracker.Flush(EQmStutterFlushReason::NONE);
		return true;
	}
	catch(...)
	{
		m_RejectFrame = true;
		return false;
	}
}

void CQmStutterCapture::RecordUpdate(size_t Index, double DurationMs) noexcept
{
	if(Index < m_vComponents.size())
		m_vComponents[Index].m_PendingUpdate += DurationMs;
}

void CQmStutterCapture::RecordRender(size_t Index, double DurationMs) noexcept
{
	if(Index < m_vComponents.size())
		m_vComponents[Index].m_PendingRender += DurationMs;
}

void CQmStutterCapture::ClearPending() noexcept
{
	for(auto &Component : m_vComponents)
	{
		Component.m_PendingUpdate = 0.0;
		Component.m_PendingRender = 0.0;
	}
	m_RejectFrame = false;
}

void CQmStutterCapture::ResetWindow() noexcept
{
	m_FrameSamples.Reset();
	for(auto &Component : m_vComponents)
	{
		Component.m_Update.Reset();
		Component.m_Render.Reset();
	}
	m_Features.m_vValues.clear();
	m_Features.m_Page.clear();
	m_Features.m_Operation.clear();
	m_Features.m_LimitCause = EQmStutterLimitCause::NONE;
	m_WindowStartFrame = 0;
	m_WorstFrame = 0;
	m_WorstFrameMs = 0.0;
}

bool CQmStutterCapture::Emit(const SFrame &Frame, bool Force, const char *pPayload) noexcept
{
	try
	{
		COperations DefaultOperations;
		auto &Operations = m_pOperations ? *m_pOperations : DefaultOperations;
		Operations.Emit(Frame, Force, pPayload);
		return true;
	}
	catch(...)
	{
		// 输出可能已发生，不能因异常重放；仍继续其他记录与窗口收尾。
		return false;
	}
}

bool CQmStutterCapture::EmitWindow(const SFrame &Frame, const SQmStutterFrameDecision &Decision, bool ForceLog)
{
	if(!Decision.m_FlushWindow || m_FrameSamples.Empty())
		return true;
	if(Frame.m_ComponentCount != m_vComponents.size() || (Frame.m_ComponentCount != 0 && !Frame.m_ppComponentNames))
		return false;

	bool Complete = true;
	const double SampleSeconds = m_FrameSamples.Total() / 1000.0;
	const double FrameMsP95 = m_FrameSamples.Percentile(95.0);
	const double FrameMsP99 = m_FrameSamples.Percentile(99.0);
	const double FpsAverage = SampleSeconds > 0.0 ? m_FrameSamples.Count() / SampleSeconds : 0.0;
	const double FpsMinimum = m_FrameSamples.Max() > 0.0 ? 1000.0 / m_FrameSamples.Max() : 0.0;
	const double FpsOnePctLow = FrameMsP99 > 0.0 ? 1000.0 / FrameMsP99 : 0.0;
	const bool CapLimited = m_Features.m_LimitCause != EQmStutterLimitCause::NONE;
	const char *pClassification = SampleSeconds >= 1.0 || Decision.m_Reason == EQmStutterFlushReason::PERIODIC ? "sustained_low" : "frame_drop";

	char aPayload[1024];
	str_format(aPayload, sizeof(aPayload),
		"schema=1 event=stutter_event stutter_id=%" PRIu64 " segment=%" PRIu64 " classification=%s end_reason=%s window_start_frame=%" PRIu64 " window_end_frame=%" PRIu64 " worst_frame=%" PRIu64 " target_fps=300 target_ms=%.6f sample_frames=%d sample_seconds=%.3f below_target_frames=%d fps_avg=%.3f fps_min=%.3f fps_1pct_low=%.3f frame_ms_avg=%.3f frame_ms_p95=%.3f frame_ms_p99=%.3f frame_ms_max=%.3f cap_limited=%d cap_reason=%s context=%s page=%s tab=%s",
		Decision.m_StutterId,
		Decision.m_Segment,
		pClassification,
		QmStutterFlushReasonName(Decision.m_Reason),
		m_WindowStartFrame,
		m_Tracker.LastBelowTargetFrame(),
		m_WorstFrame,
		QmStutterFrameBudgetMs(),
		(int)m_FrameSamples.Count(),
		SampleSeconds,
		(int)m_FrameSamples.Count(),
		FpsAverage,
		FpsMinimum,
		FpsOnePctLow,
		m_FrameSamples.Average(),
		FrameMsP95,
		FrameMsP99,
		m_FrameSamples.Max(),
		CapLimited ? 1 : 0,
		QmStutterLimitCauseName(m_Features.m_LimitCause),
		Frame.m_Online ? "online" : "offline",
		m_Features.m_Page.empty() ? "game" : m_Features.m_Page.c_str(),
		m_Features.m_Operation.empty() ? "none" : m_Features.m_Operation.c_str());
	if(!Emit(Frame, ForceLog, aPayload))
		Complete = false;

	for(size_t i = 0; i < m_vComponents.size(); ++i)
	{
		const auto LogSamples = [&](const char *pCallback, CQmStutterSampleSeries &Samples) {
			if(Samples.Empty())
				return;
			char aComponentPayload[512];
			str_format(aComponentPayload, sizeof(aComponentPayload),
				"schema=1 event=component_sample stutter_id=%" PRIu64 " segment=%" PRIu64 " scope=component owner=client module=%s callback=%s sample_count=%d total_ms=%.3f avg_ms=%.3f p95_ms=%.3f max_ms=%.3f max_frame=%" PRIu64,
				Decision.m_StutterId,
				Decision.m_Segment,
				Frame.m_ppComponentNames[i] ? Frame.m_ppComponentNames[i] : "unknown",
				pCallback,
				(int)Samples.Count(),
				Samples.Total(),
				Samples.Average(),
				Samples.Percentile(95.0),
				Samples.Max(),
				Samples.MaxFrame());
			if(!Emit(Frame, ForceLog, aComponentPayload))
				Complete = false;
		};
		LogSamples("on_update", m_vComponents[i].m_Update);
		LogSamples("on_render", m_vComponents[i].m_Render);
	}

	for(const auto &[Name, Value] : m_Features.m_vValues)
	{
		const char *pOwner = str_startswith(Name.c_str(), "qm_") != nullptr ? "qmclient" :
				     str_startswith(Name.c_str(), "tc_") != nullptr ? "tclient" :
										      "ddnet";
		char aFeaturePayload[512];
		str_format(aFeaturePayload, sizeof(aFeaturePayload),
			"schema=1 event=feature_snapshot stutter_id=%" PRIu64 " segment=%" PRIu64 " frame=%" PRIu64 " owner=%s feature=%s module=unmapped config_enabled=1 config_value=%d hud_visible=-1 settings_visible=-1 executed_in_frame=-1 executed_in_window=-1 current_page=%s current_tab=%s",
			Decision.m_StutterId,
			Decision.m_Segment,
			m_WorstFrame,
			pOwner,
			Name.c_str(),
			Value,
			m_Features.m_Page.empty() ? "game" : m_Features.m_Page.c_str(),
			m_Features.m_Operation.empty() ? "none" : m_Features.m_Operation.c_str());
		if(!Emit(Frame, ForceLog, aFeaturePayload))
			Complete = false;
	}
	return Complete;
}

bool CQmStutterCapture::ProcessFrame(const SFrame &Frame) noexcept
{
	struct CPendingGuard
	{
		CQmStutterCapture &m_Capture;
		~CPendingGuard() { m_Capture.ClearPending(); }
	} Guard{*this};
	if(!Frame.m_Enabled)
		return Finish(Frame, EQmStutterFlushReason::DISABLED);
	if(m_RejectFrame || (Frame.m_ComponentCount != 0 && !Frame.m_ppComponentNames))
		return false;
	// 菜单可在渲染回调中刚刚开启诊断，此帧尚未走过组件计时分支。
	if(m_vComponents.size() != Frame.m_ComponentCount && !PrepareComponents(Frame.m_ComponentCount))
		return false;
	try
	{
		COperations DefaultOperations;
		auto &Operations = m_pOperations ? *m_pOperations : DefaultOperations;
		auto NextTracker = m_WasEnabled ? m_Tracker : CQmStutterEpisodeTracker{};
		const auto Decision = NextTracker.RecordFrame(Frame.m_Frame, Frame.m_FrameMs);
		const bool NewWindow = !m_WasEnabled || Decision.m_Started;
		const bool Capture = Decision.m_BelowTarget && Frame.m_FrameMs > (NewWindow ? 0.0 : m_WorstFrameMs);
		SFeatures PreparedFeatures;
		if(Decision.m_BelowTarget)
		{
			Operations.PrepareSeries(m_FrameSamples, Frame.m_FrameMs);
			for(auto &Component : m_vComponents)
			{
				Operations.PrepareSeries(Component.m_Update, Component.m_PendingUpdate);
				Operations.PrepareSeries(Component.m_Render, Component.m_PendingRender);
			}
			if(Capture)
				PreparedFeatures = Operations.CaptureFeatures(Frame);
		}
		// 下面仅提交已经准备好的容量、快照和固定大小状态。
		if(NewWindow)
			ResetWindow();
		m_Tracker = NextTracker;
		m_WasEnabled = true;
		if(Decision.m_BelowTarget)
		{
			if(m_FrameSamples.Empty())
				m_WindowStartFrame = Frame.m_Frame;
			m_FrameSamples.Record(Frame.m_FrameMs, Frame.m_Frame);
			for(auto &Component : m_vComponents)
			{
				Component.m_Update.Record(Component.m_PendingUpdate, Frame.m_Frame);
				Component.m_Render.Record(Component.m_PendingRender, Frame.m_Frame);
			}
			if(Capture)
			{
				m_WorstFrameMs = Frame.m_FrameMs;
				m_WorstFrame = Frame.m_Frame;
				m_Features.Swap(PreparedFeatures);
			}
		}
		if(!Decision.m_FlushWindow)
			return true;
		return FinishWindow(Frame, Decision, false);
	}
	catch(...)
	{
		// 准备失败不推进事件或追加部分组件；本帧 pending 由 guard 丢弃。
		return false;
	}
}

bool CQmStutterCapture::FinishWindow(const SFrame &Frame, const SQmStutterFrameDecision &Decision, bool Force) noexcept
{
	bool Complete = true;
	try
	{
		Complete = EmitWindow(Frame, Decision, Force);
	}
	catch(...)
	{
		Complete = false;
	}
	ResetWindow();
	return Complete;
}

bool CQmStutterCapture::Finish(const SFrame &Frame, EQmStutterFlushReason Reason) noexcept
{
	const auto Decision = m_Tracker.Flush(Reason);
	const bool Complete = FinishWindow(Frame, Decision, true);
	ClearPending();
	m_WasEnabled = false;
	return Complete;
}
