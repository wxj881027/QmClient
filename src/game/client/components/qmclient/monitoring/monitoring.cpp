// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "monitoring.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#if defined(CONF_FAMILY_UNIX)
#include <sys/resource.h>
#endif

#if defined(CONF_PLATFORM_MACOS)
#include <mach/mach.h>
#endif

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#define IStorage IStorageCOM
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#undef IStorage

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace
{
	constexpr ColorRGBA PANEL_BG(0.025f, 0.04f, 0.07f, 0.98f);
	constexpr ColorRGBA SURFACE_BG(0.055f, 0.085f, 0.13f, 0.96f);
	constexpr ColorRGBA CARD_BG(0.09f, 0.13f, 0.19f, 0.98f);
	constexpr ColorRGBA GRID_COLOR(1.0f, 1.0f, 1.0f, 0.08f);
	constexpr ColorRGBA GRID_MAJOR_COLOR(1.0f, 1.0f, 1.0f, 0.14f);
	constexpr ColorRGBA DIVIDER_COLOR(1.0f, 1.0f, 1.0f, 0.18f);
	constexpr ColorRGBA PING_COLOR(0.40f, 0.66f, 1.0f, 0.95f);
	constexpr ColorRGBA PRED_COLOR(1.0f, 0.70f, 0.30f, 0.95f);
	constexpr ColorRGBA SNAPSHOT_GAP_COLOR(0.35f, 0.90f, 0.90f, 0.95f);
	constexpr ColorRGBA JITTER_COLOR(1.0f, 0.86f, 0.40f, 0.95f);
	constexpr ColorRGBA FPS_COLOR(0.50f, 0.90f, 0.70f, 0.95f);
	constexpr ColorRGBA GAME_MARGIN_COLOR(0.34f, 0.85f, 0.55f, 0.95f);

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

	static const char *GradeBadgeText(EQmConnectionGrade Grade)
	{
		switch(Grade)
		{
		case EQmConnectionGrade::NORMAL: return Localize("Normal");
		case EQmConnectionGrade::ELEVATED: return Localize("Elevated");
		case EQmConnectionGrade::SEVERE: return Localize("Severe");
		case EQmConnectionGrade::DISCONNECTED: return Localize("Disconnected");
		}
		return Localize("Disconnected");
	}

	static ColorRGBA GradeBadgeColor(EQmConnectionGrade Grade)
	{
		switch(Grade)
		{
		case EQmConnectionGrade::NORMAL: return ColorRGBA(0.18f, 0.70f, 0.42f, 0.95f);
		case EQmConnectionGrade::ELEVATED: return ColorRGBA(0.96f, 0.70f, 0.18f, 0.95f);
		case EQmConnectionGrade::SEVERE: return ColorRGBA(0.88f, 0.32f, 0.28f, 0.95f);
		case EQmConnectionGrade::DISCONNECTED: return ColorRGBA(0.45f, 0.48f, 0.56f, 0.95f);
		}
		return ColorRGBA(0.45f, 0.48f, 0.56f, 0.95f);
	}

	static float HudOpacity()
	{
		return QmComputeMonitoringPanelOpacity(g_Config.m_QmMonitoringHudOpacity);
	}

	static ColorRGBA ApplyHudOpacity(ColorRGBA Color)
	{
		Color.a *= HudOpacity();
		return Color;
	}

	static void DrawSurface(CUIRect Rect, ColorRGBA Color, float Radius)
	{
		Rect.Draw(ApplyHudOpacity(Color), IGraphics::CORNER_ALL, Radius);
	}

	static void DrawGraphGrid(IGraphics *pGraphics, CUIRect Rect, int HorizontalSegments, bool DrawZeroAxis = false)
	{
		std::array<IGraphics::CLineItem, 9> aLines = {};
		int NumLines = 0;
		for(int i = 1; i < HorizontalSegments; ++i)
		{
			const float Y = Rect.y + Rect.h * (float)i / (float)HorizontalSegments;
			aLines[NumLines++] = IGraphics::CLineItem(Rect.x, Y, Rect.x + Rect.w, Y);
		}
		for(int i = 1; i < 4; ++i)
		{
			const float X = Rect.x + Rect.w * (float)i / 4.0f;
			aLines[NumLines++] = IGraphics::CLineItem(X, Rect.y, X, Rect.y + Rect.h);
		}

		const int MidSegment = HorizontalSegments / 2;
		if(MidSegment > 0 && MidSegment < HorizontalSegments)
		{
			const float MidY = Rect.y + Rect.h * (float)MidSegment / (float)HorizontalSegments;
			aLines[NumLines++] = IGraphics::CLineItem(Rect.x, MidY, Rect.x + Rect.w, MidY);
		}
		pGraphics->TextureClear();
		pGraphics->LinesBegin();
		pGraphics->SetColor(GRID_COLOR);
		pGraphics->LinesDraw(aLines.data(), NumLines - 1);
		pGraphics->SetColor(GRID_MAJOR_COLOR);
		pGraphics->LinesDraw(&aLines[NumLines - 1], 1);
		if(DrawZeroAxis)
		{
			const IGraphics::CLineItem Axis(Rect.x, Rect.y + Rect.h * 0.5f, Rect.x + Rect.w, Rect.y + Rect.h * 0.5f);
			pGraphics->SetColor(DIVIDER_COLOR);
			pGraphics->LinesDraw(&Axis, 1);
		}
		pGraphics->LinesEnd();
	}

	template<size_t N, typename FMapY>
	static int BuildSmoothHistoryLines(const std::array<float, N> &aHistory, int HistoryHead, int HistoryCount, CUIRect Rect, FMapY MapY, IGraphics::CLineItem *pLines, int MaxLines)
	{
		if(HistoryCount < 2 || MaxLines <= 0)
			return 0;
		const int Start = (HistoryHead - HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
		const auto SampleAt = [&](int Index) {
			return aHistory[(Start + std::clamp(Index, 0, HistoryCount - 1)) % (int)aHistory.size()];
		};
		constexpr int SUBDIVISIONS = 4;
		int NumLines = 0;
		for(int i = 1; i < HistoryCount && NumLines < MaxLines; ++i)
		{
			const float P0 = SampleAt(i - 2);
			const float P1 = SampleAt(i - 1);
			const float P2 = SampleAt(i);
			const float P3 = SampleAt(i + 1);
			if(!std::isfinite(P1) || !std::isfinite(P2))
				continue;
			const float C0 = std::isfinite(P0) ? P0 : P1;
			const float C3 = std::isfinite(P3) ? P3 : P2;
			const float XBase = (float)(i - 1);
			float PrevX = Rect.x + Rect.w * XBase / (float)(HistoryCount - 1);
			float PrevY = MapY(P1);
			for(int Step = 1; Step <= SUBDIVISIONS && NumLines < MaxLines; ++Step)
			{
				const float T = (float)Step / (float)SUBDIVISIONS;
				const float T2 = T * T;
				const float T3 = T2 * T;
				const float Value = 0.5f * ((2.0f * P1) + (-C0 + P2) * T + (2.0f * C0 - 5.0f * P1 + 4.0f * P2 - C3) * T2 + (-C0 + 3.0f * P1 - 3.0f * P2 + C3) * T3);
				const float X = Rect.x + Rect.w * (XBase + T) / (float)(HistoryCount - 1);
				const float Y = MapY(Value);
				pLines[NumLines++] = IGraphics::CLineItem(PrevX, PrevY, X, Y);
				PrevX = X;
				PrevY = Y;
			}
		}
		return NumLines;
	}

	static void DrawPeakAnchor(IGraphics *pGraphics, float PeakX, float PeakY, CUIRect LabelRect, ColorRGBA Color)
	{
		const float LabelCenterX = LabelRect.x + LabelRect.w * 0.5f;
		const float LabelCenterY = LabelRect.y + LabelRect.h * 0.5f;
		const float AnchorRadius = 2.0f;
		const IGraphics::CLineItem Connector(PeakX, PeakY, LabelCenterX, LabelCenterY);
		CUIRect Anchor(PeakX - AnchorRadius, PeakY - AnchorRadius, AnchorRadius * 2.0f, AnchorRadius * 2.0f);

		pGraphics->TextureClear();
		pGraphics->LinesBegin();
		pGraphics->SetColor(Color);
		pGraphics->LinesDraw(&Connector, 1);
		pGraphics->LinesEnd();
		Anchor.Draw(Color, IGraphics::CORNER_ALL, AnchorRadius);
	}

	static void FormatGraphStats(char *pBuf, int BufSize, const SQmHistoryStats &Stats, const char *pUnit, int Precision = 0)
	{
		if(!Stats.m_HasData)
		{
			str_copy(pBuf, "--", BufSize);
			return;
		}

		if(Precision <= 0)
			str_format(pBuf, BufSize, "avg %.0f ↓%.0f ↑%.0f%s", Stats.m_Average, Stats.m_Min, Stats.m_Max, pUnit);
		else
			str_format(pBuf, BufSize, "avg %.*f ↓%.*f ↑%.*f%s", Precision, Stats.m_Average, Precision, Stats.m_Min, Precision, Stats.m_Max, pUnit);
	}

	static void FormatPercentValue(char *pBuf, int BufSize, float Value)
	{
		if(!std::isfinite(Value) || Value < 0.0f)
		{
			str_copy(pBuf, "--", BufSize);
			return;
		}
		str_format(pBuf, BufSize, "%.0f%%", Value);
	}

	static void FormatTickPairValue(char *pBuf, int BufSize, int GameTick, int PredictedTick)
	{
		str_format(pBuf, BufSize, "%d/%d", GameTick, PredictedTick);
	}

	static void FormatTrafficStatsValue(char *pBuf, int BufSize, const SQmNetworkMetrics::STrafficStats &Stats)
	{
		char aRateBuf[32];
		if(Stats.m_RateKibPerSec >= 0.0f)
			str_format(aRateBuf, sizeof(aRateBuf), "%.1fKiB/s", Stats.m_RateKibPerSec);
		else
			str_copy(aRateBuf, "--", sizeof(aRateBuf));

		str_format(
			pBuf,
			BufSize,
			"%" PRIu64 "p %" PRIu64 "+%" PRIu64 "=%" PRIu64 " %s avg %" PRIu64 "B",
			Stats.m_Packets,
			Stats.m_PayloadBytes,
			Stats.m_OverheadBytes,
			Stats.m_TotalBytes,
			aRateBuf,
			Stats.m_AveragePayloadBytes);
	}

	static bool RectsOverlap(const CUIRect &A, const CUIRect &B)
	{
		return A.x < B.x + B.w && A.x + A.w > B.x && A.y < B.y + B.h && A.y + A.h > B.y;
	}

	static CUIRect PlacePeakLabelRect(
		CUIRect PlotRect,
		float PeakX,
		float PeakY,
		float Width,
		float Height,
		float VerticalOffset,
		const std::array<CUIRect, 8> &aUsedRects,
		int UsedCount)
	{
		CUIRect LabelRect;
		LabelRect.w = Width;
		LabelRect.h = Height;

		const float XStep = Width * 0.55f;
		for(int Attempt = 0; Attempt < 7; ++Attempt)
		{
			const float Direction = Attempt % 2 == 0 ? 1.0f : -1.0f;
			const float Multiplier = (float)((Attempt + 1) / 2);
			LabelRect.x = std::clamp(PeakX - LabelRect.w * 0.5f + Direction * XStep * Multiplier, PlotRect.x, PlotRect.x + PlotRect.w - LabelRect.w);
			LabelRect.y = std::clamp(PeakY - LabelRect.h * 0.5f + VerticalOffset, PlotRect.y, PlotRect.y + PlotRect.h - LabelRect.h);

			bool Overlaps = false;
			for(int i = 0; i < UsedCount; ++i)
			{
				if(RectsOverlap(LabelRect, aUsedRects[i]))
				{
					Overlaps = true;
					break;
				}
			}
			if(!Overlaps)
				return LabelRect;
		}
		return LabelRect;
	}

	template<size_t N>
	static void DrawPeakLabel(
		IGraphics *pGraphics,
		ITextRender *pTextRender,
		const std::array<float, N> &aHistory,
		int HistoryHead,
		int HistoryCount,
		int PeakIndex,
		CUIRect PlotRect,
		float Denominator,
		ColorRGBA TextColor,
		float FontSize,
		const char *pUnit,
		float VerticalOffset,
		std::array<CUIRect, 8> &aUsedRects,
		int &UsedCount)
	{
		if(HistoryCount <= 0 || Denominator <= 0.0f)
			return;

		const int Start = (HistoryHead - HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
		if(PeakIndex < 0)
			return;
		const float PeakValue = aHistory[(Start + PeakIndex) % (int)aHistory.size()];

		const float PeakX = PlotRect.x + PlotRect.w * (float)PeakIndex / (float)std::max(HistoryCount - 1, 1);
		const float PeakY = PlotRect.y + PlotRect.h - (PlotRect.h * std::clamp(PeakValue / Denominator, 0.0f, 1.0f));
		char aBuf[32];
		FormatMetricValue(aBuf, sizeof(aBuf), pUnit, PeakValue, 0);
		const float Width = std::max(52.0f, pTextRender->TextWidth(FontSize, aBuf) + 8.0f);
		const float Height = FontSize + 4.0f;
		const CUIRect LabelRect = PlacePeakLabelRect(PlotRect, PeakX, PeakY, Width, Height, VerticalOffset, aUsedRects, UsedCount);
		DrawPeakAnchor(pGraphics, PeakX, PeakY, LabelRect, TextColor);
		pTextRender->TextColor(TextColor);
		const float TextWidth = pTextRender->TextWidth(FontSize, aBuf);
		pTextRender->Text(LabelRect.x + (LabelRect.w - TextWidth) * 0.5f, LabelRect.y, FontSize, aBuf);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		if(UsedCount < (int)aUsedRects.size())
			aUsedRects[UsedCount++] = LabelRect;
	}

	template<size_t N>
	static void DrawSignedPeakLabel(
		IGraphics *pGraphics,
		ITextRender *pTextRender,
		const std::array<float, N> &aHistory,
		int HistoryHead,
		int HistoryCount,
		int PeakIndex,
		CUIRect PlotRect,
		float Denominator,
		ColorRGBA TextColor,
		float FontSize,
		const char *pUnit,
		std::array<CUIRect, 8> &aUsedRects,
		int &UsedCount)
	{
		if(HistoryCount <= 0 || Denominator <= 0.0f)
			return;

		const int Start = (HistoryHead - HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
		if(PeakIndex < 0)
			return;
		const float PeakValue = aHistory[(Start + PeakIndex) % (int)aHistory.size()];

		const float PeakX = PlotRect.x + PlotRect.w * (float)PeakIndex / (float)std::max(HistoryCount - 1, 1);
		const float PeakY = PlotRect.y + PlotRect.h * 0.5f - (PlotRect.h * 0.5f * std::clamp(PeakValue / Denominator, -1.0f, 1.0f));
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%.0f%s", PeakValue, pUnit);
		const float Width = std::max(52.0f, pTextRender->TextWidth(FontSize, aBuf) + 8.0f);
		const float Height = FontSize + 4.0f;
		const CUIRect LabelRect = PlacePeakLabelRect(PlotRect, PeakX, PeakY, Width, Height, 0.0f, aUsedRects, UsedCount);
		DrawPeakAnchor(pGraphics, PeakX, PeakY, LabelRect, TextColor);
		pTextRender->TextColor(TextColor);
		const float TextWidth = pTextRender->TextWidth(FontSize, aBuf);
		pTextRender->Text(LabelRect.x + (LabelRect.w - TextWidth) * 0.5f, LabelRect.y, FontSize, aBuf);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
		if(UsedCount < (int)aUsedRects.size())
			aUsedRects[UsedCount++] = LabelRect;
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
	m_GraphCache = {};
	const float InvalidMetric = std::numeric_limits<float>::quiet_NaN();
	m_aRttHistory.fill(InvalidMetric);
	m_aPredHistory.fill(InvalidMetric);
	m_aSnapshotGapHistory.fill(InvalidMetric);
	m_aPredictionJitterHistory.fill(InvalidMetric);
	m_aGameTimeMarginHistory.fill(InvalidMetric);
	m_aFpsHistory.fill(InvalidMetric);
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
	PushHistorySample(
		m_Snapshot.m_Network.m_PingMs,
		m_Snapshot.m_Network.m_PredictionLeadMs,
		m_Snapshot.m_Network.m_SnapshotGapMs,
		m_Snapshot.m_Network.m_PredictionJitterMs,
		m_Snapshot.m_Network.m_GameTimeMarginMs,
		m_Snapshot.m_Performance.m_Fps);
	if(g_Config.m_DbgGraphs)
		UpdateGraphCache();
	else
		m_GraphCache.m_Valid = false;
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
		m_aRttHistory.fill(InvalidMetric);
		m_aPredHistory.fill(InvalidMetric);
		m_aSnapshotGapHistory.fill(InvalidMetric);
		m_aPredictionJitterHistory.fill(InvalidMetric);
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
	const SQmDevicePerfSample ProcessSample = ProcessPerfSamplerState().Snapshot().m_Sample;
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

void CQmMonitoring::PushHistorySample(float RttMs, float PredMs, float SnapshotGapMs, float PredictionJitterMs, float GameTimeMarginMs, float Fps)
{
	const float InvalidMetric = std::numeric_limits<float>::quiet_NaN();
	m_aRttHistory[m_HistoryHead] = RttMs >= 0.0f ? RttMs : InvalidMetric;
	m_aPredHistory[m_HistoryHead] = PredMs >= 0.0f ? PredMs : InvalidMetric;
	m_aSnapshotGapHistory[m_HistoryHead] = SnapshotGapMs >= 0.0f ? SnapshotGapMs : InvalidMetric;
	m_aPredictionJitterHistory[m_HistoryHead] = PredictionJitterMs >= 0.0f ? PredictionJitterMs : InvalidMetric;
	m_aGameTimeMarginHistory[m_HistoryHead] = std::isfinite(GameTimeMarginMs) ? GameTimeMarginMs : InvalidMetric;
	m_aFpsHistory[m_HistoryHead] = std::isfinite(Fps) && Fps >= 0.0f ? Fps : InvalidMetric;
	m_HistoryHead = (m_HistoryHead + 1) % (int)m_aRttHistory.size();
	m_HistoryCount = std::min(m_HistoryCount + 1, (int)m_aRttHistory.size());
}

void CQmMonitoring::UpdateGraphCache()
{
	m_GraphCache = {};
	m_GraphCache.m_Rtt.m_Stats = QmComputeHistoryStats(m_aRttHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_Prediction.m_Stats = QmComputeHistoryStats(m_aPredHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_SnapshotGap.m_Stats = QmComputeHistoryStats(m_aSnapshotGapHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_PredictionJitter.m_Stats = QmComputeHistoryStats(m_aPredictionJitterHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_Fps.m_Stats = QmComputeHistoryStats(m_aFpsHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_GameTimeMargin.m_Stats = QmComputeHistoryStats(m_aGameTimeMarginHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_Rtt.m_PeakIndex = QmFindLatestPeakIndex(m_aRttHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_Prediction.m_PeakIndex = QmFindLatestPeakIndex(m_aPredHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_SnapshotGap.m_PeakIndex = QmFindLatestPeakIndex(m_aSnapshotGapHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_PredictionJitter.m_PeakIndex = QmFindLatestPeakIndex(m_aPredictionJitterHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_Fps.m_PeakIndex = QmFindLatestPeakIndex(m_aFpsHistory, m_HistoryHead, m_HistoryCount);
	m_GraphCache.m_GameTimeMargin.m_PeakIndex = QmFindLatestAbsolutePeakIndex(m_aGameTimeMarginHistory, m_HistoryHead, m_HistoryCount);

	for(int i = 0; i < m_HistoryCount; ++i)
	{
		const int Index = (m_HistoryHead - m_HistoryCount + i + QM_MONITORING_HISTORY_CAPACITY) % QM_MONITORING_HISTORY_CAPACITY;
		const float Rtt = m_aRttHistory[Index];
		const float Prediction = m_aPredHistory[Index];
		const float SnapshotGap = m_aSnapshotGapHistory[Index];
		const float Jitter = m_aPredictionJitterHistory[Index];
		const float Fps = m_aFpsHistory[Index];
		const float GameMargin = m_aGameTimeMarginHistory[Index];
		if(std::isfinite(Rtt))
			m_GraphCache.m_MainGraphMaxValue = std::max(m_GraphCache.m_MainGraphMaxValue, Rtt);
		if(std::isfinite(Prediction))
			m_GraphCache.m_MainGraphMaxValue = std::max(m_GraphCache.m_MainGraphMaxValue, Prediction);
		if(std::isfinite(SnapshotGap))
			m_GraphCache.m_MainGraphMaxValue = std::max(m_GraphCache.m_MainGraphMaxValue, SnapshotGap);
		if(std::isfinite(Jitter))
			m_GraphCache.m_MainGraphMaxValue = std::max(m_GraphCache.m_MainGraphMaxValue, Jitter);
		if(std::isfinite(Fps))
			m_GraphCache.m_FpsGraphMaxValue = std::max(m_GraphCache.m_FpsGraphMaxValue, Fps);
		if(std::isfinite(GameMargin))
			m_GraphCache.m_GameTimeMarginMaxAbs = std::max(m_GraphCache.m_GameTimeMarginMaxAbs, std::abs(GameMargin));
	}
	m_GraphCache.m_Valid = true;
}

void CQmMonitoring::UpdateSnapshot()
{
	SQmMonitoringSnapshot Snapshot;
	UpdateNetworkMetrics(Snapshot.m_Network);
	UpdatePerformanceMetrics(Snapshot.m_Performance);
	UpdateDiagnosticVerdict(Snapshot.m_Verdict, Snapshot.m_Network, Snapshot.m_Performance);
	m_Snapshot = Snapshot;
}

void CQmMonitoring::RenderHeader(CUIRect Rect) const
{
	const float UiScale = QmComputeMonitoringUiScale(Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float SummaryFontSize = 22.0f * UiScale;
	const float DetailFontSize = 15.0f * UiScale;
	const float BadgeFontSize = 14.0f * UiScale;
	const float RightColumnWidth = 118.0f * UiScale;
	const float BadgeHeight = 24.0f * UiScale;

	CUIRect Left, Right, LineRect, SummaryRect, SeparatorRect, DetailRect, BadgeRect;
	Rect.VSplitRight(RightColumnWidth, &Left, &Right);
	Left.VSplitLeft(TextRender()->TextWidth(SummaryFontSize, Localize(m_Snapshot.m_Verdict.m_pSummary)) + 8.0f * UiScale, &SummaryRect, &LineRect);
	LineRect.VSplitLeft(12.0f * UiScale, &SeparatorRect, &LineRect);
	LineRect.VSplitLeft(TextRender()->TextWidth(DetailFontSize, Localize(m_Snapshot.m_Verdict.m_pDetail)) + 8.0f * UiScale, &DetailRect, &LineRect);
	Ui()->DoLabel(&SummaryRect, Localize(m_Snapshot.m_Verdict.m_pSummary), SummaryFontSize, TEXTALIGN_ML);
	Ui()->DoLabel(&SeparatorRect, "·", DetailFontSize, TEXTALIGN_MC);
	Ui()->DoLabel(&DetailRect, Localize(m_Snapshot.m_Verdict.m_pDetail), DetailFontSize, TEXTALIGN_ML);
	Right.HSplitTop(BadgeHeight, &BadgeRect, nullptr);
	BadgeRect.Draw(GradeBadgeColor(m_Snapshot.m_Verdict.m_Grade), IGraphics::CORNER_ALL, BadgeRect.h / 2.0f);
	Ui()->DoLabel(&BadgeRect, Localize(GradeBadgeText(m_Snapshot.m_Verdict.m_Grade)), BadgeFontSize, TEXTALIGN_MC);
}

void CQmMonitoring::RenderMainGraph(CUIRect Rect) const
{
	const float UiScale = QmComputeMonitoringUiScale(Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float Margin = 12.0f * UiScale;
	const float HeaderHeight = 82.0f * UiScale;
	const float ItemGap = 12.0f * UiScale;
	const float ColorWidth = 14.0f * UiScale;
	const float ColorRadius = 4.0f * UiScale;
	const float HeaderFontSize = 16.0f * UiScale;
	const float HeaderValueFontSize = 17.0f * UiScale;
	const float FooterFontSize = 13.0f * UiScale;
	const float PeakFontSize = 15.0f * UiScale;
	const float CornerRadius = 8.0f * UiScale;

	DrawSurface(Rect, SURFACE_BG, CornerRadius);

	if(m_HistoryCount < 2)
		return;

	CUIRect Inner, HeaderRect, PlotRect;
	Rect.Margin(Margin, &Inner);
	Inner.HSplitTop(HeaderHeight, &HeaderRect, &Inner);
	PlotRect = Inner;
	DrawGraphGrid(Graphics(), PlotRect, 4);

	const auto SampleAt = [this](const std::array<float, QM_MONITORING_HISTORY_CAPACITY> &aHistory, int Index) {
		const int Start = (m_HistoryHead - m_HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
		return aHistory[(Start + Index) % (int)aHistory.size()];
	};

	const float MaxValue = m_GraphCache.m_Valid ? m_GraphCache.m_MainGraphMaxValue : 30.0f;

	const auto DrawSeries = [&](const std::array<float, QM_MONITORING_HISTORY_CAPACITY> &aHistory, const ColorRGBA &Color) {
		std::array<IGraphics::CLineItem, (QM_MONITORING_HISTORY_CAPACITY - 1) * 4> aLines = {};
		const int NumLines = BuildSmoothHistoryLines(aHistory, m_HistoryHead, m_HistoryCount, PlotRect, [MaxValue, PlotRect](float Value) { return PlotRect.y + PlotRect.h - (PlotRect.h * std::clamp(Value / MaxValue, 0.0f, 1.0f)); }, aLines.data(), (int)aLines.size());

		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(Color);
		if(NumLines > 0)
			Graphics()->LinesDraw(aLines.data(), NumLines);
		Graphics()->LinesEnd();

		const float LastValue = SampleAt(aHistory, m_HistoryCount - 1);
		if(m_HistoryCount >= 2 && std::isfinite(LastValue))
		{
			const float LastX = PlotRect.x + PlotRect.w;
			const float LastY = PlotRect.y + PlotRect.h - (PlotRect.h * std::clamp(LastValue / MaxValue, 0.0f, 1.0f));
			const float DotR = 3.5f;
			CUIRect Dot(LastX - DotR, LastY - DotR, DotR * 2.0f, DotR * 2.0f);
			Dot.Draw(Color, IGraphics::CORNER_ALL, DotR);
		}
	};

	DrawSeries(m_aRttHistory, PING_COLOR);
	DrawSeries(m_aPredHistory, PRED_COLOR);
	DrawSeries(m_aSnapshotGapHistory, SNAPSHOT_GAP_COLOR);
	DrawSeries(m_aPredictionJitterHistory, JITTER_COLOR);
	std::array<CUIRect, 8> aPeakRects = {};
	int PeakRectCount = 0;
	DrawPeakLabel(Graphics(), TextRender(), m_aRttHistory, m_HistoryHead, m_HistoryCount, m_GraphCache.m_Rtt.m_PeakIndex, PlotRect, MaxValue, PING_COLOR, PeakFontSize, "ms", 0.0f, aPeakRects, PeakRectCount);
	DrawPeakLabel(Graphics(), TextRender(), m_aPredHistory, m_HistoryHead, m_HistoryCount, m_GraphCache.m_Prediction.m_PeakIndex, PlotRect, MaxValue, PRED_COLOR, PeakFontSize, "ms", 0.0f, aPeakRects, PeakRectCount);
	DrawPeakLabel(Graphics(), TextRender(), m_aSnapshotGapHistory, m_HistoryHead, m_HistoryCount, m_GraphCache.m_SnapshotGap.m_PeakIndex, PlotRect, MaxValue, SNAPSHOT_GAP_COLOR, PeakFontSize, "ms", 0.0f, aPeakRects, PeakRectCount);
	DrawPeakLabel(Graphics(), TextRender(), m_aPredictionJitterHistory, m_HistoryHead, m_HistoryCount, m_GraphCache.m_PredictionJitter.m_PeakIndex, PlotRect, MaxValue, JITTER_COLOR, PeakFontSize, "ms", 0.0f, aPeakRects, PeakRectCount);

	const struct SLegendItem
	{
		const char *m_pLabel;
		float m_Value;
		ColorRGBA m_Color;
		SQmHistoryStats m_Stats;
	} aLegend[] = {
		{Localize("RTT"), m_Snapshot.m_Network.m_PingMs, PING_COLOR, m_GraphCache.m_Rtt.m_Stats},
		{Localize("Prediction"), m_Snapshot.m_Network.m_PredictionLeadMs, PRED_COLOR, m_GraphCache.m_Prediction.m_Stats},
		{Localize("Snapshot age"), m_Snapshot.m_Network.m_SnapshotGapMs, SNAPSHOT_GAP_COLOR, m_GraphCache.m_SnapshotGap.m_Stats},
		{Localize("Jitter"), m_Snapshot.m_Network.m_PredictionJitterMs, JITTER_COLOR, m_GraphCache.m_PredictionJitter.m_Stats},
	};

	CUIRect TopRow, BottomRow;
	HeaderRect.HSplitMid(&TopRow, &BottomRow, 10.0f * UiScale);
	std::array<CUIRect, 4> aCells = {};
	CUIRect TopLeft, TopRight, BottomLeft, BottomRight;
	TopRow.VSplitMid(&TopLeft, &TopRight, ItemGap);
	BottomRow.VSplitMid(&BottomLeft, &BottomRight, ItemGap);
	aCells[0] = TopLeft;
	aCells[1] = TopRight;
	aCells[2] = BottomLeft;
	aCells[3] = BottomRight;
	for(int i = 0; i < 4; ++i)
	{
		CUIRect HeaderCell = aCells[i];
		CUIRect ColorRect, TextRect, LabelRect, ValueRect, StatsRect;

		HeaderCell.VSplitLeft(ColorWidth, &ColorRect, &HeaderCell);
		HeaderCell.VSplitLeft(5.0f * UiScale, nullptr, &HeaderCell);
		TextRect = HeaderCell;
		TextRect.HSplitTop(26.0f * UiScale, &TextRect, &StatsRect);
		TextRect.VSplitLeft(TextRect.w * 0.64f, &LabelRect, &ValueRect);
		ColorRect.HMargin(4.0f * UiScale, &ColorRect);
		ColorRect.Draw(aLegend[i].m_Color, IGraphics::CORNER_ALL, ColorRadius);

		char aValueBuf[32];
		FormatMetricValue(aValueBuf, sizeof(aValueBuf), "ms", aLegend[i].m_Value, 0);
		Ui()->DoLabel(&LabelRect, aLegend[i].m_pLabel, HeaderFontSize, TEXTALIGN_ML);
		Ui()->DoLabel(&ValueRect, aValueBuf, HeaderValueFontSize, TEXTALIGN_MR);

		char aStatsBuf[64];
		FormatGraphStats(aStatsBuf, sizeof(aStatsBuf), aLegend[i].m_Stats, "ms", 0);
		Ui()->DoLabel(&StatsRect, aStatsBuf, FooterFontSize, TEXTALIGN_ML);
	}
}

void CQmMonitoring::RenderFpsGraph(CUIRect Rect) const
{
	const float UiScale = QmComputeMonitoringUiScale(Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float Margin = 14.0f * UiScale;
	const float HeaderHeight = 44.0f * UiScale;
	const float HeaderFontSize = 15.0f * UiScale;
	const float StatsFontSize = 13.0f * UiScale;
	const float PeakFontSize = 14.0f * UiScale;
	const float CornerRadius = 8.0f * UiScale;

	DrawSurface(Rect, SURFACE_BG, CornerRadius);

	if(m_HistoryCount < 2)
		return;

	const auto SampleAt = [this](const std::array<float, QM_MONITORING_HISTORY_CAPACITY> &aHistory, int Index) {
		const int Start = (m_HistoryHead - m_HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
		return aHistory[(Start + Index) % (int)aHistory.size()];
	};

	CUIRect Inner, HeaderRect, PlotRect;
	Rect.Margin(Margin, &Inner);
	Inner.HSplitTop(HeaderHeight, &HeaderRect, &PlotRect);

	CUIRect FpsRect, GameMarginRect;
	PlotRect.HSplitMid(&FpsRect, &GameMarginRect, 8.0f * UiScale);
	DrawGraphGrid(Graphics(), FpsRect, 4);
	DrawGraphGrid(Graphics(), GameMarginRect, 4, true);

	const float MaxFpsValue = m_GraphCache.m_Valid ? m_GraphCache.m_FpsGraphMaxValue : 30.0f;
	const float MaxGameMarginAbs = m_GraphCache.m_Valid ? m_GraphCache.m_GameTimeMarginMaxAbs : 25.0f;

	std::array<IGraphics::CLineItem, (QM_MONITORING_HISTORY_CAPACITY - 1) * 4> aFpsLines = {};
	const int NumFpsLines = BuildSmoothHistoryLines(m_aFpsHistory, m_HistoryHead, m_HistoryCount, FpsRect, [MaxFpsValue, FpsRect](float Value) { return FpsRect.y + FpsRect.h - (FpsRect.h * std::clamp(Value / MaxFpsValue, 0.0f, 1.0f)); }, aFpsLines.data(), (int)aFpsLines.size());

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(FPS_COLOR);
	if(NumFpsLines > 0)
		Graphics()->LinesDraw(aFpsLines.data(), NumFpsLines);
	Graphics()->LinesEnd();
	if(m_HistoryCount >= 2 && std::isfinite(SampleAt(m_aFpsHistory, m_HistoryCount - 1)))
	{
		const float LastY = FpsRect.y + FpsRect.h - (FpsRect.h * std::clamp(SampleAt(m_aFpsHistory, m_HistoryCount - 1) / MaxFpsValue, 0.0f, 1.0f));
		const float DotR = 3.5f;
		CUIRect Dot(FpsRect.x + FpsRect.w - DotR, LastY - DotR, DotR * 2.0f, DotR * 2.0f);
		Dot.Draw(FPS_COLOR, IGraphics::CORNER_ALL, DotR);
	}
	std::array<CUIRect, 8> aPeakRects = {};
	int PeakRectCount = 0;
	DrawPeakLabel(Graphics(), TextRender(), m_aFpsHistory, m_HistoryHead, m_HistoryCount, m_GraphCache.m_Fps.m_PeakIndex, FpsRect, MaxFpsValue, FPS_COLOR, PeakFontSize, "", -8.0f * UiScale, aPeakRects, PeakRectCount);

	std::array<IGraphics::CLineItem, (QM_MONITORING_HISTORY_CAPACITY - 1) * 4> aGameMarginLines = {};
	const int NumGameMarginLines = BuildSmoothHistoryLines(m_aGameTimeMarginHistory, m_HistoryHead, m_HistoryCount, GameMarginRect, [MaxGameMarginAbs, GameMarginRect](float Value) { return GameMarginRect.y + GameMarginRect.h * 0.5f - (GameMarginRect.h * 0.5f * std::clamp(Value / MaxGameMarginAbs, -1.0f, 1.0f)); }, aGameMarginLines.data(), (int)aGameMarginLines.size());

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(GAME_MARGIN_COLOR);
	if(NumGameMarginLines > 0)
		Graphics()->LinesDraw(aGameMarginLines.data(), NumGameMarginLines);
	Graphics()->LinesEnd();
	if(m_HistoryCount >= 2 && std::isfinite(SampleAt(m_aGameTimeMarginHistory, m_HistoryCount - 1)))
	{
		const float LastY = GameMarginRect.y + GameMarginRect.h * 0.5f - (GameMarginRect.h * 0.5f * std::clamp(SampleAt(m_aGameTimeMarginHistory, m_HistoryCount - 1) / MaxGameMarginAbs, -1.0f, 1.0f));
		const float DotR = 3.5f;
		CUIRect Dot(GameMarginRect.x + GameMarginRect.w - DotR, LastY - DotR, DotR * 2.0f, DotR * 2.0f);
		Dot.Draw(GAME_MARGIN_COLOR, IGraphics::CORNER_ALL, DotR);
	}
	DrawSignedPeakLabel(Graphics(), TextRender(), m_aGameTimeMarginHistory, m_HistoryHead, m_HistoryCount, m_GraphCache.m_GameTimeMargin.m_PeakIndex, GameMarginRect, MaxGameMarginAbs, GAME_MARGIN_COLOR, PeakFontSize, "ms", aPeakRects, PeakRectCount);

	CUIRect LeftHeader, RightHeader;
	HeaderRect.VSplitMid(&LeftHeader, &RightHeader, 10.0f * UiScale);

	char aFpsValueBuf[32];
	FormatMetricValue(aFpsValueBuf, sizeof(aFpsValueBuf), "", m_Snapshot.m_Performance.m_Fps, 0);
	Ui()->DoLabel(&LeftHeader, Localize("Frame rate"), HeaderFontSize, TEXTALIGN_ML);
	Ui()->DoLabel(&LeftHeader, aFpsValueBuf, HeaderFontSize, TEXTALIGN_MR);
	CUIRect LeftStatsRect = LeftHeader;
	LeftStatsRect.y += 18.0f * UiScale;
	char aFpsStatsBuf[64];
	FormatGraphStats(aFpsStatsBuf, sizeof(aFpsStatsBuf), m_GraphCache.m_Fps.m_Stats, "", 0);
	Ui()->DoLabel(&LeftStatsRect, aFpsStatsBuf, StatsFontSize, TEXTALIGN_ML);

	char aGameMarginValueBuf[32];
	FormatMetricValue(aGameMarginValueBuf, sizeof(aGameMarginValueBuf), "ms", m_Snapshot.m_Network.m_GameTimeMarginMs, 0);
	Ui()->DoLabel(&RightHeader, Localize("Game time margin"), HeaderFontSize, TEXTALIGN_ML);
	Ui()->DoLabel(&RightHeader, aGameMarginValueBuf, HeaderFontSize, TEXTALIGN_MR);
	CUIRect RightStatsRect = RightHeader;
	RightStatsRect.y += 18.0f * UiScale;
	char aGameMarginStatsBuf[64];
	FormatGraphStats(aGameMarginStatsBuf, sizeof(aGameMarginStatsBuf), m_GraphCache.m_GameTimeMargin.m_Stats, "ms", 0);
	Ui()->DoLabel(&RightStatsRect, aGameMarginStatsBuf, StatsFontSize, TEXTALIGN_ML);
}

void CQmMonitoring::RenderPrimaryCards(CUIRect Rect) const
{
	const float UiScale = QmComputeMonitoringUiScale(Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float Gap = 10.0f * UiScale;
	const float CornerRadius = 8.0f * UiScale;
	DrawSurface(Rect, SURFACE_BG, CornerRadius);

	const struct SCard
	{
		const char *m_pLabel;
		float m_Value;
		const char *m_pUnit;
		int m_Precision;
		ColorRGBA m_Color;
		bool m_IsRate = false;
		bool m_IsPercent = false;
		bool m_IsCpu = false;
	} aCards[] = {
		{Localize("Frame rate"), m_Snapshot.m_Performance.m_Fps, "", 0, FPS_COLOR},
		{Localize("Frame time"), m_Snapshot.m_Performance.m_FrameTimeUs, "us", 0, FPS_COLOR},
		{Localize("DDNet/total CPU"), m_Snapshot.m_Performance.m_CpuUsagePct, "", 0, GAME_MARGIN_COLOR, false, false, true},
		{Localize("Memory"), m_Snapshot.m_Performance.m_MemoryUsageMb, "MB", 0, GAME_MARGIN_COLOR},
		{Localize("Process UDP RX (est.)"), m_Snapshot.m_Network.m_Recv.m_RateKibPerSec * 1024.0f, "", 0, PING_COLOR, true},
		{Localize("Process UDP TX (est.)"), m_Snapshot.m_Network.m_Send.m_RateKibPerSec * 1024.0f, "", 0, PRED_COLOR, true},
		{Localize("Snapshot payload"), m_Snapshot.m_Network.m_SnapshotPayloadBytesPerSec, "", 0, SNAPSHOT_GAP_COLOR, true},
		{Localize("Snapshot rate"), m_Snapshot.m_Network.m_SnapshotRatePerSec, "snap/s", 1, SNAPSHOT_GAP_COLOR},
	};

	const int CardCount = std::size(aCards);
	const int Columns = 4;
	const int Rows = (CardCount + Columns - 1) / Columns;
	const float CardWidth = std::max((Rect.w - Gap * (Columns - 1)) / (float)Columns, 0.0f);
	const float CardHeight = std::max((Rect.h - Gap * (Rows - 1)) / (float)Rows, 0.0f);
	const float Margin = std::clamp(CardHeight * 0.12f, 6.0f, 10.0f);
	const float LabelFontSize = std::clamp(CardHeight * 0.25f, 12.0f, 15.0f);
	const float ValueFontSize = std::clamp(CardHeight * 0.38f, 18.0f, 24.0f);
	const float AccentHeight = std::clamp(CardHeight * 0.055f, 2.0f, 4.0f);
	for(int i = 0; i < CardCount; ++i)
	{
		const int RowIndex = i / Columns;
		const int ColumnIndex = i % Columns;
		CUIRect CardRect(
			Rect.x + (CardWidth + Gap) * ColumnIndex,
			Rect.y + (CardHeight + Gap) * RowIndex,
			CardWidth,
			CardHeight);

		CUIRect Inner, LabelRect, ValueRect, AccentRect;
		DrawSurface(CardRect, CARD_BG, CornerRadius);
		CardRect.Margin(Margin, &Inner);
		Inner.HSplitTop(LabelFontSize + 3.0f * UiScale, &LabelRect, &ValueRect);
		AccentRect = CardRect;
		AccentRect.h = AccentHeight;
		AccentRect.x += Margin;
		AccentRect.w = std::min(28.0f * UiScale, std::max(CardRect.w - Margin * 2.0f, 0.0f));

		char aBuf[32];
		if(aCards[i].m_IsRate)
			FormatRateValue(aBuf, sizeof(aBuf), aCards[i].m_Value);
		else if(aCards[i].m_IsCpu)
			FormatCpuRatioValue(aBuf, sizeof(aBuf), aCards[i].m_Value, m_Snapshot.m_Performance.m_TotalCpuUsagePct);
		else if(aCards[i].m_IsPercent)
			FormatPercentValue(aBuf, sizeof(aBuf), aCards[i].m_Value);
		else
			FormatMetricValue(aBuf, sizeof(aBuf), aCards[i].m_pUnit, aCards[i].m_Value, aCards[i].m_Precision);
		Ui()->DoLabel(&LabelRect, aCards[i].m_pLabel, LabelFontSize, TEXTALIGN_ML);
		Ui()->DoLabel(&ValueRect, aBuf, ValueFontSize, TEXTALIGN_ML);
		AccentRect.Draw(aCards[i].m_Color, IGraphics::CORNER_ALL, AccentHeight / 2.0f);
	}
}

void CQmMonitoring::RenderDebugDetails(CUIRect Rect) const
{
	const float UiScale = QmComputeMonitoringUiScale(Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float Margin = 10.0f * UiScale;
	const float Gap = 14.0f * UiScale;
	const float RowGap = 4.0f * UiScale;
	const float LabelFontSize = 13.0f * UiScale;
	const float ValueFontSize = 13.0f * UiScale;
	const float CornerRadius = 8.0f * UiScale;

	DrawSurface(Rect, SURFACE_BG, CornerRadius);

	CUIRect Inner, Left, Right;
	Rect.Margin(Margin, &Inner);
	Inner.VSplitMid(&Left, &Right, Gap);
	const float RowHeight = std::clamp((Inner.h - RowGap * 5.0f) / 6.0f, 14.0f * UiScale, 20.0f * UiScale);

	struct SDetailRow
	{
		const char *m_pLabel;
		const char *m_pValue;
	};

	char aTickBuf[32];
	char aPingBuf[32];
	char aPredictionLeadBuf[32];
	char aPredictionJitterBuf[32];
	char aSnapshotGapBuf[32];
	char aSnapshotTickGapBuf[32];
	char aFrameP95Buf[32];
	char aFrameP99Buf[32];
	char aFpsLowBuf[32];
	char aLongFrameBuf[32];
	char aVitalResendBuf[32];

	FormatTickPairValue(aTickBuf, sizeof(aTickBuf), m_Snapshot.m_Performance.m_GameTick, m_Snapshot.m_Performance.m_PredictedTick);
	FormatMetricValue(aPingBuf, sizeof(aPingBuf), "ms", m_Snapshot.m_Network.m_PingMs, 0);
	FormatMetricValue(aPredictionLeadBuf, sizeof(aPredictionLeadBuf), "ms", m_Snapshot.m_Network.m_PredictionLeadMs, 0);
	FormatMetricValue(aPredictionJitterBuf, sizeof(aPredictionJitterBuf), "ms", m_Snapshot.m_Network.m_PredictionJitterMs, 0);
	FormatMetricValue(aSnapshotGapBuf, sizeof(aSnapshotGapBuf), "ms", m_Snapshot.m_Network.m_SnapshotGapMs, 0);
	FormatMetricValue(aSnapshotTickGapBuf, sizeof(aSnapshotTickGapBuf), "", (float)m_Snapshot.m_Network.m_SnapshotTickGap, 0);
	FormatMetricValue(aFrameP95Buf, sizeof(aFrameP95Buf), "ms", m_Snapshot.m_Performance.m_FrameTimeP95Ms, 1);
	FormatMetricValue(aFrameP99Buf, sizeof(aFrameP99Buf), "ms", m_Snapshot.m_Performance.m_FrameTimeP99Ms, 1);
	FormatMetricValue(aFpsLowBuf, sizeof(aFpsLowBuf), " FPS", m_Snapshot.m_Performance.m_FpsOnePctLow, 0);
	FormatMetricValue(aLongFrameBuf, sizeof(aLongFrameBuf), "", (float)m_Snapshot.m_Performance.m_LongFrameCount, 0);
	FormatMetricValue(aVitalResendBuf, sizeof(aVitalResendBuf), "", (float)m_Snapshot.m_Network.m_VitalResendCount, 0);

	const SDetailRow aLeftRows[] = {
		{Localize("Game/predicted tick"), aTickBuf},
		{Localize("RTT"), aPingBuf},
		{Localize("Snapshot age"), aSnapshotGapBuf},
		{Localize("Prediction lead"), aPredictionLeadBuf},
		{Localize("Prediction jitter"), aPredictionJitterBuf},
	};
	const SDetailRow aRightRows[] = {
		{Localize("Snapshot tick gap"), aSnapshotTickGapBuf},
		{Localize("Frame time p95"), aFrameP95Buf},
		{Localize("Frame time p99"), aFrameP99Buf},
		{Localize("1% low"), aFpsLowBuf},
		{Localize("Long frames"), aLongFrameBuf},
		{Localize("Vital resend queue"), aVitalResendBuf},
	};

	const auto RenderColumn = [&](CUIRect ColumnRect, const SDetailRow *pRows, int RowCount) {
		for(int i = 0; i < RowCount; ++i)
		{
			CUIRect RowRect, LabelRect, ValueRect;
			ColumnRect.HSplitTop(RowHeight, &RowRect, &ColumnRect);
			RowRect.VSplitLeft(RowRect.w * 0.48f, &LabelRect, &ValueRect);
			ValueRect.Margin(4.0f * UiScale, &ValueRect);
			Ui()->DoLabel(&LabelRect, pRows[i].m_pLabel, LabelFontSize, TEXTALIGN_ML);
			Ui()->DoLabel(&ValueRect, pRows[i].m_pValue, ValueFontSize, TEXTALIGN_ML);
			if(i + 1 < RowCount)
				ColumnRect.HSplitTop(RowGap, nullptr, &ColumnRect);
		}
	};

	RenderColumn(Left, aLeftRows, std::size(aLeftRows));
	RenderColumn(Right, aRightRows, std::size(aRightRows));
}

void CQmMonitoring::RenderHud(CUIRect View) const
{
	const float UiScale = QmComputeMonitoringUiScale(Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	const float Padding = std::round(QM_MONITORING_PANEL_PADDING * UiScale);
	const float HeaderHeight = QM_MONITORING_HEADER_HEIGHT * UiScale;
	const float SectionGap = QM_MONITORING_SECTION_GAP * UiScale;

	View.Draw(ApplyHudOpacity(PANEL_BG), IGraphics::CORNER_ALL, 8.0f * UiScale);
	CUIRect Content;
	View.Margin(Padding, &Content);

	CUIRect Header, MainGraph, FpsGraph, MetricsGrid, DebugDetails;
	const SQmMonitoringBodyLayout BodyLayout = QmComputeMonitoringBodyLayout(Content.h, UiScale);
	Content.HSplitTop(HeaderHeight, &Header, &Content);
	Content.HSplitTop(SectionGap, nullptr, &Content);
	Content.HSplitTop(BodyLayout.m_MainGraphHeight, &MainGraph, &Content);
	Content.HSplitTop(SectionGap, nullptr, &Content);
	Content.HSplitTop(BodyLayout.m_FpsGraphHeight, &FpsGraph, &Content);
	Content.HSplitTop(SectionGap, nullptr, &Content);
	Content.HSplitTop(BodyLayout.m_PrimaryCardsHeight, &MetricsGrid, &Content);
	Content.HSplitTop(SectionGap, nullptr, &Content);
	Content.HSplitTop(BodyLayout.m_MetricsExtraHeight, &DebugDetails, &Content);

	RenderHeader(Header);
	RenderMainGraph(MainGraph);
	RenderFpsGraph(FpsGraph);
	RenderPrimaryCards(MetricsGrid);
	if(DebugDetails.h > 0.0f)
		RenderDebugDetails(DebugDetails);
}
