// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "monitoring.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiTheme.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace
{
	static float BytesPerSecondDelta(int64_t CurrentBytes, int64_t PrevBytes, float DeltaSeconds)
	{
		if(!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f || CurrentBytes < PrevBytes)
			return -1.0f;
		return (float)(CurrentBytes - PrevBytes) / DeltaSeconds;
	}

	static EQmConnectionGrade DetermineConnectionGrade(const SQmNetworkMetrics &Net)
	{
		return QmDetermineConnectionGrade(Net);
	}

	static EQmDiagnosticCause DeterminePrimaryCause(const SQmNetworkMetrics &Net, const SQmPerformanceMetrics &Perf, EQmConnectionGrade Grade)
	{
		return QmDeterminePrimaryCause(Net, Perf, Grade);
	}

	static const char *LocalizeGradeSummary(EQmConnectionGrade Grade)
	{
		switch(Grade)
		{
		case EQmConnectionGrade::NORMAL: return Localize("Connection normal");
		case EQmConnectionGrade::ELEVATED: return Localize("Connection elevated");
		case EQmConnectionGrade::SEVERE: return Localize("Connection severely abnormal");
		case EQmConnectionGrade::DISCONNECTED: return Localize("Connection disconnected");
		}
		return Localize("Connection disconnected");
	}

	static const char *LocalizeCauseDetail(EQmDiagnosticCause Cause, EQmConnectionGrade Grade, const SQmNetworkMetrics &Net, const SQmPerformanceMetrics &Perf)
	{
		if(Grade == EQmConnectionGrade::DISCONNECTED)
			return Localize("Not connected to a game server");

		switch(Cause)
		{
		case EQmDiagnosticCause::SNAPSHOT_GAP: return Localize("Complete snapshots are arriving late or not arriving");
		case EQmDiagnosticCause::PREDICTION: return Localize("Prediction lead is high, check prediction settings and server timing");
		case EQmDiagnosticCause::PREDICTION_JITTER: return Localize("Prediction jitter is obvious, prediction timing is changing");
		case EQmDiagnosticCause::CLIENT_PERFORMANCE:
			if(Perf.m_FrameTimeP95Ms > 16.7f)
				return Localize("Client frame time is abnormal");
			if(Perf.m_CpuUsagePct >= 75.0f)
				return Localize("Client CPU usage is high");
			if((Net.m_PingMs >= 0.0f && Perf.m_PredictionTimeMs >= Net.m_PingMs + 12.0f) || Perf.m_PredictionStress >= 12.0f)
				return Localize("Client prediction time is high");
			return Localize("Client performance pressure is high");
		case EQmDiagnosticCause::NONE: return Localize("No obvious anomaly");
		}
		return Localize("No obvious anomaly");
	}

	static CQmAsyncDevicePerfSampler &ProcessPerfSamplerState()
	{
		// CPU/内存始终供监控使用，独立于仅在性能日志开启时运行的 GPU 查询。
		static CQmAsyncDevicePerfSampler s_Sampler(QmSampleProcessPerf, std::chrono::milliseconds(50));
		return s_Sampler;
	}

	static CQmAsyncDevicePerfSampler &DevicePerfSamplerState()
	{
		static CQmAsyncDevicePerfSampler s_Sampler;
		return s_Sampler;
	}

	static SQmDevicePerfSample CachedDevicePerfSample(bool Enabled, bool &NewSample)
	{
		static CQmDevicePerfVersionTracker s_VersionTracker;

		CQmAsyncDevicePerfSampler &Sampler = DevicePerfSamplerState();
		QmUpdateDevicePerfSamplerState(Sampler, Enabled);
		if(!Enabled)
		{
			s_VersionTracker.Reset();
			NewSample = false;
			return {};
		}

		const SQmDevicePerfSnapshot Snapshot = Sampler.Snapshot();
		NewSample = s_VersionTracker.Observe(Snapshot.m_Version);
		return Snapshot.m_Sample;
	}
}

void CQmMonitoring::ResetHistory()
{
	m_Snapshot = {};
	const float InvalidMetric = std::numeric_limits<float>::quiet_NaN();
	m_aGameTimeMarginHistory.fill(InvalidMetric);
	m_aFrameTimeHistory.fill(InvalidMetric);
	m_HistoryHead = 0;
	m_HistoryCount = 0;
	m_LastSampleTick = 0;
	m_LastPercentileTick = 0;
	m_CachedFrameTimeP95Ms = -1.0f;
	m_CachedFrameTimeP99Ms = -1.0f;
	m_CachedFpsOnePctLow = -1.0f;
	m_CachedLongFrameCount = 0;
	m_LastSnapshotRateSampleTime = 0;
	m_LastSnapshotCount = 0;
	m_LastSnapshotPartCount = 0;
	m_LastSnapshotPayloadBytes = 0;
	m_SnapshotRatePerSec = -1.0f;
	m_SnapshotPartRatePerSec = -1.0f;
	m_SnapshotPayloadBytesPerSec = -1.0f;
	m_SnapshotRateConnection = -1;
	m_LastPredictionLeadMs = 0.0f;
	m_PredictionJitterMs = -1.0f;
	m_HasPredictionLeadSample = false;
}

void CQmMonitoring::OnInit()
{
	ResetHistory();
	QmUpdateDevicePerfSamplerState(ProcessPerfSamplerState(), true);
}

void CQmMonitoring::OnShutdown()
{
	ProcessPerfSamplerState().Stop();
	DevicePerfSamplerState().Stop();
}

void CQmMonitoring::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState && (NewState != IClient::STATE_ONLINE || OldState != IClient::STATE_ONLINE))
		ResetHistory();
}

void CQmMonitoring::OnRender()
{
	const int64_t Now = time_get();
	if(m_LastSampleTick != 0 && Now - m_LastSampleTick < time_freq() / 20)
		return;

	PushFrameTimeSample(Client()->RenderFrameTime() * 1000.0f);
	UpdateSnapshot();
	m_aGameTimeMarginHistory[m_HistoryHead] = m_Snapshot.m_Network.m_GameTimeMarginMs;
	m_HistoryHead = (m_HistoryHead + 1) % QM_MONITORING_HISTORY_CAPACITY;
	m_HistoryCount = std::min(m_HistoryCount + 1, QM_MONITORING_HISTORY_CAPACITY);
	m_LastSampleTick = Now;
}

void CQmMonitoring::UpdateNetworkMetrics(SQmNetworkMetrics &Net)
{
	IClient *pClient = Client();
	Net.m_Connected = pClient->IsGameConnectionAlive();
	const int SnapshotConnection = g_Config.m_ClDummy;
	if(m_SnapshotRateConnection != -1 && m_SnapshotRateConnection != SnapshotConnection)
	{
		const float InvalidMetric = std::numeric_limits<float>::quiet_NaN();
		m_aGameTimeMarginHistory.fill(InvalidMetric);
		m_LastPredictionLeadMs = 0.0f;
		m_PredictionJitterMs = -1.0f;
		m_HasPredictionLeadSample = false;
	}
	Net.m_ConnectionProblems = pClient->ConnectionProblems();
	Net.m_PingMs = pClient->PingMs();
	Net.m_PredictionLeadMs = pClient->PredictionLeadMs();
	Net.m_PredictionMarginMs = pClient->PredictionMarginMs();
	if(Net.m_PredictionLeadMs >= 0.0f)
	{
		if(m_HasPredictionLeadSample)
		{
			const float LeadDelta = std::abs(Net.m_PredictionLeadMs - m_LastPredictionLeadMs);
			m_PredictionJitterMs = m_PredictionJitterMs < 0.0f ? LeadDelta : m_PredictionJitterMs + (LeadDelta - m_PredictionJitterMs) * 0.15f;
		}
		m_LastPredictionLeadMs = Net.m_PredictionLeadMs;
		m_HasPredictionLeadSample = true;
	}
	else
	{
		m_LastPredictionLeadMs = 0.0f;
		m_HasPredictionLeadSample = false;
		m_PredictionJitterMs = -1.0f;
	}
	Net.m_PredictionJitterMs = m_PredictionJitterMs;
	Net.m_VitalResendCount = Net.m_Connected ? std::max(pClient->PendingResendCount(), 0) : -1;

	SClientSnapshotStats SnapshotStats;
	pClient->SnapshotStats(SnapshotStats);
	const bool HasGameTimeMargin = Net.m_Connected && SnapshotStats.m_SnapshotCount > 2;
	Net.m_GameTimeMarginMs = HasGameTimeMargin ? pClient->GameTimeMarginMs() : std::numeric_limits<float>::quiet_NaN();
	Net.m_GameTimeCorrectionMs = QmComputeRollbackMs(Net.m_GameTimeMarginMs);
	Net.m_SnapshotGapMs = SnapshotStats.m_CurrentGapMs;
	Net.m_SnapshotTickGap = SnapshotStats.m_LastTickGap;
	Net.m_SnapshotRatePerSec = m_SnapshotRatePerSec;
	Net.m_SnapshotPartRatePerSec = m_SnapshotPartRatePerSec;
	Net.m_SnapshotPayloadBytesPerSec = m_SnapshotPayloadBytesPerSec;
	const int64_t Now = time_get();
	if(m_LastSnapshotRateSampleTime == 0 || m_SnapshotRateConnection != SnapshotConnection)
	{
		m_SnapshotRateConnection = SnapshotConnection;
		m_LastSnapshotRateSampleTime = Now;
		m_LastSnapshotCount = SnapshotStats.m_SnapshotCount;
		m_LastSnapshotPartCount = SnapshotStats.m_PartCount;
		m_LastSnapshotPayloadBytes = SnapshotStats.m_PayloadBytes;
		m_SnapshotRatePerSec = -1.0f;
		m_SnapshotPartRatePerSec = -1.0f;
		m_SnapshotPayloadBytesPerSec = -1.0f;
	}
	else if(SnapshotStats.m_SnapshotCount < m_LastSnapshotCount ||
		SnapshotStats.m_PartCount < m_LastSnapshotPartCount ||
		SnapshotStats.m_PayloadBytes < m_LastSnapshotPayloadBytes)
	{
		m_LastSnapshotRateSampleTime = Now;
		m_LastSnapshotCount = SnapshotStats.m_SnapshotCount;
		m_LastSnapshotPartCount = SnapshotStats.m_PartCount;
		m_LastSnapshotPayloadBytes = SnapshotStats.m_PayloadBytes;
		m_SnapshotRatePerSec = -1.0f;
		m_SnapshotPartRatePerSec = -1.0f;
		m_SnapshotPayloadBytesPerSec = -1.0f;
	}
	else if(Now - m_LastSnapshotRateSampleTime >= time_freq())
	{
		const float SampleSeconds = (float)(Now - m_LastSnapshotRateSampleTime) / (float)time_freq();
		m_SnapshotRatePerSec = QmComputeCounterRate(m_LastSnapshotCount, SnapshotStats.m_SnapshotCount, SampleSeconds);
		m_SnapshotPartRatePerSec = QmComputeCounterRate(m_LastSnapshotPartCount, SnapshotStats.m_PartCount, SampleSeconds);
		m_SnapshotPayloadBytesPerSec = QmComputeCounterRate(m_LastSnapshotPayloadBytes, SnapshotStats.m_PayloadBytes, SampleSeconds);
		m_LastSnapshotRateSampleTime = Now;
		m_LastSnapshotCount = SnapshotStats.m_SnapshotCount;
		m_LastSnapshotPartCount = SnapshotStats.m_PartCount;
		m_LastSnapshotPayloadBytes = SnapshotStats.m_PayloadBytes;
	}
	Net.m_SnapshotRatePerSec = m_SnapshotRatePerSec;
	Net.m_SnapshotPartRatePerSec = m_SnapshotPartRatePerSec;
	Net.m_SnapshotPayloadBytesPerSec = m_SnapshotPayloadBytesPerSec;

	NETSTATS Prev = {};
	NETSTATS Current = {};
	std::chrono::nanoseconds SampleInterval = std::chrono::nanoseconds::zero();
	pClient->NetStatsSnapshot(Prev, Current, SampleInterval);

	const float DeltaSeconds = SampleInterval.count() > 0 ? (float)SampleInterval.count() / 1000000000.0f : 0.0f;

	Net.m_DownPayloadBytesPerSec = BytesPerSecondDelta((int64_t)Current.recv_bytes, (int64_t)Prev.recv_bytes, DeltaSeconds);
	Net.m_UpPayloadBytesPerSec = BytesPerSecondDelta((int64_t)Current.sent_bytes, (int64_t)Prev.sent_bytes, DeltaSeconds);
	Net.m_Send = QmComputeTrafficStats(Prev.sent_packets, Prev.sent_bytes, Current.sent_packets, Current.sent_bytes, DeltaSeconds);
	Net.m_Recv = QmComputeTrafficStats(Prev.recv_packets, Prev.recv_bytes, Current.recv_packets, Current.recv_bytes, DeltaSeconds);

	int NegativeSamples = 0;
	int ValidSamples = 0;
	const int Start = (m_HistoryHead - m_HistoryCount + (int)m_aGameTimeMarginHistory.size()) % (int)m_aGameTimeMarginHistory.size();
	for(int i = 0; i < m_HistoryCount; ++i)
	{
		const float Margin = m_aGameTimeMarginHistory[(Start + i) % (int)m_aGameTimeMarginHistory.size()];
		if(!std::isfinite(Margin))
			continue;
		++ValidSamples;
		if(Margin < 0.0f)
			++NegativeSamples;
	}
	Net.m_GameTimeAheadRatePct = ValidSamples > 0 ? (float)NegativeSamples * 100.0f / (float)ValidSamples : -1.0f;
}

void CQmMonitoring::UpdatePerformanceMetrics(SQmPerformanceMetrics &Perf)
{
	Perf.m_FrameTimeMs = Client()->FrameTimeAverage() * 1000.0f;
	const int64_t Now = time_get();
	if(g_Config.m_DbgGraphs || m_LastPercentileTick == 0 || Now - m_LastPercentileTick >= time_freq() / 2)
	{
		const int FrameHistoryHead = (m_HistoryHead + 1) % (int)m_aFrameTimeHistory.size();
		const int FrameHistoryCount = std::min(m_HistoryCount + 1, (int)m_aFrameTimeHistory.size());
		m_CachedFrameTimeP95Ms = QmComputeHistoryPercentile(m_aFrameTimeHistory, FrameHistoryHead, FrameHistoryCount, 95.0f);
		m_CachedFrameTimeP99Ms = QmComputeHistoryPercentile(m_aFrameTimeHistory, FrameHistoryHead, FrameHistoryCount, 99.0f);
		m_CachedFpsOnePctLow = m_CachedFrameTimeP99Ms > 0.0f ? 1000.0f / m_CachedFrameTimeP99Ms : -1.0f;
		m_CachedLongFrameCount = 0;
		const int Start = (FrameHistoryHead - FrameHistoryCount + (int)m_aFrameTimeHistory.size()) % (int)m_aFrameTimeHistory.size();
		for(int i = 0; i < FrameHistoryCount; ++i)
		{
			const float FrameMs = m_aFrameTimeHistory[(Start + i) % (int)m_aFrameTimeHistory.size()];
			if(std::isfinite(FrameMs) && FrameMs > 16.7f)
				++m_CachedLongFrameCount;
		}
		m_LastPercentileTick = Now;
	}
	Perf.m_FrameTimeP95Ms = m_CachedFrameTimeP95Ms;
	Perf.m_FrameTimeP99Ms = m_CachedFrameTimeP99Ms;
	Perf.m_FpsOnePctLow = m_CachedFpsOnePctLow;
	Perf.m_LongFrameCount = m_CachedLongFrameCount;
	Perf.m_FrameTimeUs = Client()->FrameTimeAverage() * 1000000.0f;
	Perf.m_Fps = Perf.m_FrameTimeMs > 0.0f ? 1000.0f / Perf.m_FrameTimeMs : 0.0f;
	Perf.m_PredictionTimeMs = (float)Client()->GetPredictionTime();
	auto &ProcessSampler = ProcessPerfSamplerState();
	QmUpdateDevicePerfSamplerState(ProcessSampler, true);
	const SQmDevicePerfSample ProcessSample = ProcessSampler.Snapshot().m_Sample;
	Perf.m_CpuUsagePct = ProcessSample.m_CpuUsagePct;
	Perf.m_TotalCpuUsagePct = ProcessSample.m_TotalCpuUsagePct;
	Perf.m_MemoryUsageMb = ProcessSample.m_MemoryUsageMb;
	Perf.m_GameTick = Client()->GameTick(g_Config.m_ClDummy);
	Perf.m_PredictedTick = Client()->PredGameTick(g_Config.m_ClDummy);
	Perf.m_GraphicsMemory.m_TextureKiB = Graphics()->TextureMemoryUsage() / 1024;
	Perf.m_GraphicsMemory.m_BufferKiB = Graphics()->BufferMemoryUsage() / 1024;
	Perf.m_GraphicsMemory.m_StreamedKiB = Graphics()->StreamedMemoryUsage() / 1024;
	Perf.m_GraphicsMemory.m_StagingKiB = Graphics()->StagingMemoryUsage() / 1024;
	if(QmPerfEnabled())
	{
		bool NewDeviceSample = false;
		const SQmDevicePerfSample DeviceSample = CachedDevicePerfSample(true, NewDeviceSample);
		Perf.m_GpuUtilPct = DeviceSample.m_GpuUtilPct;
		Perf.m_GpuDedicatedVramMb = DeviceSample.m_GpuDedicatedVramMb;
		Perf.m_GpuDedicatedVramBudgetMb = DeviceSample.m_GpuDedicatedVramBudgetMb;
		Perf.m_GpuSharedVramMb = DeviceSample.m_GpuSharedVramMb;
		Perf.m_DiskReadMbPerSec = DeviceSample.m_DiskReadMbPerSec;
		Perf.m_DeviceSampleAvailable = DeviceSample.m_Available;

		if(NewDeviceSample)
		{
			char aPayload[384];
			str_format(aPayload, sizeof(aPayload),
				"event=sample gpu_util_percent=%.3f gpu_dedicated_vram_mb=%.3f gpu_dedicated_vram_budget_mb=%.3f gpu_shared_vram_mb=%.3f cpu_process_percent=%.3f cpu_total_percent=%.3f memory_process_mb=%.3f disk_read_mb_s=%.3f sample_available=%d",
				Perf.m_GpuUtilPct,
				Perf.m_GpuDedicatedVramMb,
				Perf.m_GpuDedicatedVramBudgetMb,
				Perf.m_GpuSharedVramMb,
				Perf.m_CpuUsagePct,
				Perf.m_TotalCpuUsagePct,
				Perf.m_MemoryUsageMb,
				Perf.m_DiskReadMbPerSec,
				Perf.m_DeviceSampleAvailable ? 1 : 0);
			QmPerfLogPayload("perf/device", aPayload, Client());
		}
	}
	else
	{
		bool NewDeviceSample = false;
		(void)CachedDevicePerfSample(false, NewDeviceSample);
		Perf.m_GpuUtilPct = -1.0f;
		Perf.m_GpuDedicatedVramMb = -1.0f;
		Perf.m_GpuDedicatedVramBudgetMb = -1.0f;
		Perf.m_GpuSharedVramMb = -1.0f;
		Perf.m_DiskReadMbPerSec = -1.0f;
		Perf.m_DeviceSampleAvailable = false;
	}
	const float PingMs = Client()->PingMs();
	const float ReferencePingMs = PingMs >= 0.0f ? PingMs : 0.0f;
	Perf.m_PredictionStress =
		std::max(Perf.m_PredictionTimeMs - ReferencePingMs, 0.0f) +
		std::max(Perf.m_FrameTimeMs - 16.7f, 0.0f);
}

void CQmMonitoring::UpdateDiagnosticVerdict(SQmDiagnosticVerdict &Verdict, const SQmNetworkMetrics &Net, const SQmPerformanceMetrics &Perf)
{
	Verdict.m_Grade = DetermineConnectionGrade(Net);
	Verdict.m_PrimaryCause = DeterminePrimaryCause(Net, Perf, Verdict.m_Grade);
	Verdict.m_pSummary = LocalizeGradeSummary(Verdict.m_Grade);
	Verdict.m_pDetail = LocalizeCauseDetail(Verdict.m_PrimaryCause, Verdict.m_Grade, Net, Perf);
}

void CQmMonitoring::PushFrameTimeSample(float FrameTimeMs)
{
	m_aFrameTimeHistory[m_HistoryHead] = std::isfinite(FrameTimeMs) && FrameTimeMs >= 0.0f ? FrameTimeMs : std::numeric_limits<float>::quiet_NaN();
}

void CQmMonitoring::UpdateSnapshot()
{
	SQmMonitoringSnapshot Snapshot;
	UpdateNetworkMetrics(Snapshot.m_Network);
	UpdatePerformanceMetrics(Snapshot.m_Performance);
	UpdateDiagnosticVerdict(Snapshot.m_Verdict, Snapshot.m_Network, Snapshot.m_Performance);
	m_Snapshot = Snapshot;
}

void CQmMonitoring::RenderHud(const SQmMonitoringHudLayout &Layout) const
{
	using namespace ui_token::hud::debug_graph;
	const float Scale = Layout.m_Scale;
	if(Scale <= 0.0f)
		return;

	const SUiTheme Theme = ResolveInputFallbackTheme();
	Layout.m_PanelRect.Draw(Theme.m_Surface.WithMultipliedAlpha(QmComputeMonitoringPanelOpacity(g_Config.m_QmMonitoringHudOpacity)), IGraphics::CORNER_ALL, ui_token::radius::BASE * Scale);

	CUIRect Content, Summary, Network, Graph;
	Layout.m_PanelRect.Margin(PADDING * Scale, &Content);
	Content.HSplitTop(SUMMARY_HEIGHT * Scale, &Summary, &Content);
	Content.HSplitTop(NETWORK_HEIGHT * Scale, &Network, &Content);
	Content.HSplitTop(ui_token::spacing::XS * Scale, nullptr, &Graph);

	char aFps[32], aFrame[32], aRtt[32], aPrediction[32], aLine[128];
	const auto &Perf = m_Snapshot.m_Performance;
	const auto &Net = m_Snapshot.m_Network;
	FormatMetricValue(aFps, sizeof(aFps), " FPS", Perf.m_Fps);
	FormatMetricValue(aFrame, sizeof(aFrame), " ms", Perf.m_FrameTimeMs, 1);
	FormatMetricValue(aRtt, sizeof(aRtt), " ms", Net.m_Connected ? Net.m_PingMs : -1.0f);
	FormatMetricValue(aPrediction, sizeof(aPrediction), " ms", Net.m_Connected ? Net.m_PredictionLeadMs : -1.0f);

	const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
	TextRender()->TextColor(Theme.m_TextTitle);
	CUIRect RateRect, TimeRect;
	Summary.VSplitLeft(Summary.w / 3.0f, &RateRect, &TimeRect);
	Ui()->DoLabel(&RateRect, aFps, ui_token::font::TITLE * Scale, TEXTALIGN_ML);
	str_format(aLine, sizeof(aLine), "%s  %s", Localize("Frame time"), aFrame);
	Ui()->DoLabel(&TimeRect, aLine, ui_token::font::BODY * Scale, TEXTALIGN_MR);
	TextRender()->TextColor(Theme.m_TextBody.WithMultipliedAlpha(ui_token::color::TEXT_SECONDARY.a));
	str_format(aLine, sizeof(aLine), "%s  %s     %s  %s", Localize("RTT"), aRtt, Localize("Prediction lead"), aPrediction);
	Ui()->DoLabel(&Network, aLine, ui_token::font::BODY * Scale, TEXTALIGN_ML);
	TextRender()->TextColor(PreviousTextColor);

	const float BarWidth = Graph.w / BAR_COUNT;
	const int Visible = std::min(m_HistoryCount, BAR_COUNT);
	std::array<IGraphics::CQuadItem, BAR_COUNT> aBars;
	int BarCount = 0;
	for(int Column = BAR_COUNT - Visible; Column < BAR_COUNT; ++Column)
	{
		const float Height = Graph.h * QmMonitoringBarFraction(m_aFrameTimeHistory, m_HistoryHead, m_HistoryCount, Column);
		if(Height <= 0.0f)
			continue;
		aBars[BarCount++] = {Graph.x + Column * BarWidth, Graph.y + Graph.h - Height, BarWidth, Height};
	}

	const IGraphics::CQuadItem Guide(Graph.x, Graph.y + Graph.h * (1.0f - FRAME_BUDGET_MS / GRAPH_MAX_MS), Graph.w, GUIDE_HEIGHT * Scale);
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Theme.m_TextSmall.WithMultipliedAlpha(GUIDE_ALPHA));
	Graphics()->QuadsDrawTL(&Guide, 1);
	Graphics()->SetColor(Theme.m_Accent);
	if(BarCount > 0)
		Graphics()->QuadsDrawTL(aBars.data(), BarCount);
	Graphics()->QuadsEnd();
}
