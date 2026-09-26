// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MONITORING_MONITORING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MONITORING_MONITORING_H

#include <base/system.h>
#include <base/types.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

enum class EQmConnectionGrade
{
	NORMAL,
	ELEVATED,
	SEVERE,
	DISCONNECTED,
};

enum class EQmDiagnosticCause
{
	NONE,
	SNAPSHOT_GAP,
	PREDICTION,
	PREDICTION_JITTER,
	CLIENT_PERFORMANCE,
};

struct SQmNetworkMetrics
{
	struct STrafficStats
	{
		uint64_t m_Packets = 0;
		uint64_t m_PayloadBytes = 0;
		uint64_t m_OverheadBytes = 0;
		uint64_t m_TotalBytes = 0;
		uint64_t m_AveragePayloadBytes = 0;
		float m_RateKibPerSec = -1.0f;
	};

	float m_PingMs = -1.0f;
	float m_PredictionLeadMs = -1.0f;
	float m_PredictionMarginMs = -1.0f;
	float m_PredictionJitterMs = -1.0f;
	float m_SnapshotGapMs = -1.0f;
	int m_SnapshotTickGap = -1;
	float m_SnapshotRatePerSec = -1.0f;
	float m_SnapshotPayloadBytesPerSec = -1.0f;
	float m_SnapshotPartRatePerSec = -1.0f;
	float m_GameTimeMarginMs = std::numeric_limits<float>::quiet_NaN();
	float m_GameTimeCorrectionMs = -1.0f;
	float m_GameTimeAheadRatePct = -1.0f;
	float m_DownPayloadBytesPerSec = -1.0f;
	float m_UpPayloadBytesPerSec = -1.0f;
	int m_VitalResendCount = -1;
	STrafficStats m_Send;
	STrafficStats m_Recv;
	bool m_ConnectionProblems = false;
	bool m_Connected = false;
};

struct SQmPerformanceMetrics
{
	struct SGraphicsMemoryStats
	{
		uint64_t m_TextureKiB = 0;
		uint64_t m_BufferKiB = 0;
		uint64_t m_StreamedKiB = 0;
		uint64_t m_StagingKiB = 0;
	};

	float m_Fps = 0.0f;
	float m_FrameTimeMs = 0.0f;
	float m_FrameTimeP95Ms = -1.0f;
	float m_FrameTimeP99Ms = -1.0f;
	float m_FpsOnePctLow = -1.0f;
	int m_LongFrameCount = 0;
	float m_FrameTimeUs = 0.0f;
	float m_CpuUsagePct = -1.0f;
	float m_TotalCpuUsagePct = -1.0f;
	float m_MemoryUsageMb = -1.0f;
	float m_GpuUtilPct = -1.0f;
	float m_GpuDedicatedVramMb = -1.0f;
	float m_GpuDedicatedVramBudgetMb = -1.0f;
	float m_GpuSharedVramMb = -1.0f;
	float m_DiskReadMbPerSec = -1.0f;
	float m_PredictionTimeMs = 0.0f;
	float m_PredictionStress = 0.0f;
	int m_GameTick = 0;
	int m_PredictedTick = 0;
	bool m_DeviceSampleAvailable = false;
	SGraphicsMemoryStats m_GraphicsMemory;
};

struct SQmDevicePerfSample
{
	float m_CpuUsagePct = -1.0f;
	float m_TotalCpuUsagePct = -1.0f;
	float m_MemoryUsageMb = -1.0f;
	float m_GpuUtilPct = -1.0f;
	float m_GpuDedicatedVramMb = -1.0f;
	float m_GpuDedicatedVramBudgetMb = -1.0f;
	float m_GpuSharedVramMb = -1.0f;
	float m_DiskReadMbPerSec = -1.0f;
	bool m_Available = false;
};

// 后台采样回调，不访问客户端、配置或图形对象。
SQmDevicePerfSample QmSampleProcessPerf();

struct SQmDevicePerfSnapshot
{
	SQmDevicePerfSample m_Sample;
	uint64_t m_Version = 0;
};

class CQmDevicePerfVersionTracker
{
	uint64_t m_LastSeenVersion = 0;

public:
	void Reset() { m_LastSeenVersion = 0; }
	bool Observe(uint64_t Version)
	{
		if(Version == 0 || Version == m_LastSeenVersion)
			return false;
		m_LastSeenVersion = Version;
		return true;
	}
};

class CQmDevicePerfSnapshotCache
{
public:
	SQmDevicePerfSnapshot Publish(const SQmDevicePerfSample &Sample);
	void Reset();
	SQmDevicePerfSnapshot Snapshot() const;

private:
	mutable std::mutex m_Mutex;
	SQmDevicePerfSnapshot m_Snapshot;
};

class CQmAsyncDevicePerfSampler
{
public:
	using FSampleOverride = std::function<SQmDevicePerfSample()>;

	explicit CQmAsyncDevicePerfSampler(FSampleOverride SampleOverride = {}, std::chrono::milliseconds PollInterval = std::chrono::milliseconds(100));
	~CQmAsyncDevicePerfSampler();

	void EnsureStarted();
	void SetEnabled(bool Enabled);
	void Stop();
	SQmDevicePerfSnapshot Snapshot() const;

private:
	void Run();

	FSampleOverride m_SampleOverride;
	std::chrono::milliseconds m_PollInterval;
	CQmDevicePerfSnapshotCache m_Cache;
	std::thread m_Thread;
	mutable std::mutex m_StateMutex;
	std::condition_variable m_StateCv;
	bool m_Started = false;
	bool m_Enabled = false;
	bool m_StopRequested = false;
	uint64_t m_EnableGeneration = 0;
};

void QmUpdateDevicePerfSamplerState(CQmAsyncDevicePerfSampler &Sampler, bool Enabled);

struct SQmDiagnosticVerdict
{
	EQmConnectionGrade m_Grade = EQmConnectionGrade::DISCONNECTED;
	EQmDiagnosticCause m_PrimaryCause = EQmDiagnosticCause::NONE;
	const char *m_pSummary = "";
	const char *m_pDetail = "";
};

struct SQmMonitoringSnapshot
{
	SQmNetworkMetrics m_Network;
	SQmPerformanceMetrics m_Performance;
	SQmDiagnosticVerdict m_Verdict;
};

struct SQmMonitoringHudLayout
{
	CUIRect m_PanelRect = {0.0f, 0.0f, 0.0f, 0.0f};
	CUIRect m_ContentRect = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct SQmMonitoringBodyLayout
{
	float m_MainGraphHeight = 0.0f;
	float m_FpsGraphHeight = 0.0f;
	float m_PrimaryCardsHeight = 0.0f;
	float m_MetricsExtraHeight = 0.0f;
};

struct SQmHistoryStats
{
	float m_Current = 0.0f;
	float m_Average = 0.0f;
	float m_Min = 0.0f;
	float m_Max = 0.0f;
	bool m_HasData = false;
};

struct SQmGraphSeriesCache
{
	SQmHistoryStats m_Stats;
	int m_PeakIndex = -1;
};

struct SQmMonitoringGraphCache
{
	SQmGraphSeriesCache m_Rtt;
	SQmGraphSeriesCache m_Prediction;
	SQmGraphSeriesCache m_SnapshotGap;
	SQmGraphSeriesCache m_PredictionJitter;
	SQmGraphSeriesCache m_Fps;
	SQmGraphSeriesCache m_GameTimeMargin;
	float m_MainGraphMaxValue = 30.0f;
	float m_FpsGraphMaxValue = 30.0f;
	float m_GameTimeMarginMaxAbs = 25.0f;
	bool m_Valid = false;
};

inline constexpr float QM_MONITORING_PANEL_PADDING = 12.0f;
inline constexpr float QM_MONITORING_HEADER_HEIGHT = 50.0f;
inline constexpr float QM_MONITORING_SECTION_GAP = 8.0f;
inline constexpr float QM_MONITORING_MAIN_GRAPH_HEIGHT = 220.0f;
inline constexpr float QM_MONITORING_FPS_GRAPH_HEIGHT = 140.0f;
inline constexpr float QM_MONITORING_PRIMARY_CARDS_HEIGHT = 96.0f;
inline constexpr float QM_MONITORING_SECONDARY_CARDS_HEIGHT = 136.0f;
inline constexpr int QM_MONITORING_HISTORY_CAPACITY = 180;

inline float QmComputeMonitoringUiScale(float ScreenWidth, float ScreenHeight)
{
	const float WidthScale = ScreenWidth / 1920.0f;
	const float HeightScale = ScreenHeight / 1080.0f;
	const float AreaScale = std::sqrt(std::max(WidthScale * HeightScale, 0.0f));
	return std::clamp(AreaScale, 0.65f, 1.8f);
}

inline float QmComputeRateKibPerSec(float BytesPerSec)
{
	return !std::isfinite(BytesPerSec) || BytesPerSec <= 0.0f ? 0.0f : BytesPerSec / 1024.0f;
}

inline float QmComputeMonitoringPanelOpacity(int OpacityPercent)
{
	return std::clamp(OpacityPercent / 100.0f, 0.0f, 1.0f);
}

inline float QmNormalizeProcessCpuUsagePct(float RawCpuUsagePct, unsigned CpuCount)
{
	if(!std::isfinite(RawCpuUsagePct) || RawCpuUsagePct < 0.0f)
		return -1.0f;
	if(CpuCount == 0)
		return std::clamp(RawCpuUsagePct, 0.0f, 100.0f);
	return std::clamp(RawCpuUsagePct / (float)CpuCount, 0.0f, 100.0f);
}

inline float QmComputeTotalCpuUsagePct(uint64_t PrevIdle, uint64_t PrevTotal, uint64_t CurrentIdle, uint64_t CurrentTotal)
{
	if(PrevTotal == 0 || CurrentTotal <= PrevTotal || CurrentIdle < PrevIdle)
		return -1.0f;

	const uint64_t TotalDelta = CurrentTotal - PrevTotal;
	const uint64_t IdleDelta = CurrentIdle - PrevIdle;
	if(TotalDelta == 0)
		return -1.0f;
	return std::clamp((float)(TotalDelta - std::min(IdleDelta, TotalDelta)) * 100.0f / (float)TotalDelta, 0.0f, 100.0f);
}

inline float QmComputeRollbackMs(float GameTimeMarginMs)
{
	if(!std::isfinite(GameTimeMarginMs))
		return -1.0f;
	return GameTimeMarginMs < 0.0f ? -GameTimeMarginMs : 0.0f;
}

inline float QmComputeDiskReadMbPerSec(uint64_t PrevReadBytes, uint64_t PrevTickNs, uint64_t CurrentReadBytes, uint64_t CurrentTickNs)
{
	if(PrevTickNs == 0 || CurrentTickNs <= PrevTickNs || CurrentReadBytes < PrevReadBytes)
		return -1.0f;

	const uint64_t DeltaNs = CurrentTickNs - PrevTickNs;
	if(DeltaNs == 0)
		return -1.0f;

	const double DeltaBytes = (double)(CurrentReadBytes - PrevReadBytes);
	const double DeltaSeconds = (double)DeltaNs / 1000000000.0;
	return DeltaSeconds > 0.0 ? (float)(DeltaBytes / (1024.0 * 1024.0) / DeltaSeconds) : -1.0f;
}

inline SQmNetworkMetrics::STrafficStats QmComputeTrafficStats(const NETSTATS &Prev, const NETSTATS &Current, float SampleIntervalSec)
{
	SQmNetworkMetrics::STrafficStats Stats;
	constexpr uint64_t OverheadSize = 14 + 20 + 8;

	if(Current.sent_packets < Prev.sent_packets || Current.sent_bytes < Prev.sent_bytes)
		return Stats;

	Stats.m_Packets = Current.sent_packets - Prev.sent_packets;
	Stats.m_PayloadBytes = Current.sent_bytes - Prev.sent_bytes;
	Stats.m_OverheadBytes = Stats.m_Packets * OverheadSize;
	Stats.m_TotalBytes = Stats.m_PayloadBytes + Stats.m_OverheadBytes;
	Stats.m_AveragePayloadBytes = Stats.m_Packets == 0 ? 0 : Stats.m_PayloadBytes / Stats.m_Packets;
	if(std::isfinite(SampleIntervalSec) && SampleIntervalSec > 0.0f)
		Stats.m_RateKibPerSec = (float)Stats.m_TotalBytes / 1024.0f / SampleIntervalSec;
	return Stats;
}

inline float QmComputeCounterRate(uint64_t Previous, uint64_t Current, float SampleIntervalSec)
{
	if(Current < Previous || !std::isfinite(SampleIntervalSec) || SampleIntervalSec <= 0.0f)
		return -1.0f;
	return (float)(Current - Previous) / SampleIntervalSec;
}

inline SQmNetworkMetrics::STrafficStats QmComputeTrafficStats(uint64_t PrevPackets, uint64_t PrevBytes, uint64_t CurrentPackets, uint64_t CurrentBytes, float SampleIntervalSec)
{
	NETSTATS Prev = {};
	Prev.sent_packets = PrevPackets;
	Prev.sent_bytes = PrevBytes;
	NETSTATS Current = {};
	Current.sent_packets = CurrentPackets;
	Current.sent_bytes = CurrentBytes;
	return QmComputeTrafficStats(Prev, Current, SampleIntervalSec);
}

inline void FormatMetricValue(char *pBuf, int BufSize, const char *pUnit, float Value, int Precision = 0)
{
	if(!std::isfinite(Value) || Value < 0.0f)
	{
		str_copy(pBuf, "--", BufSize);
		return;
	}
	if(Precision <= 0)
		str_format(pBuf, BufSize, "%.0f%s", Value, pUnit);
	else
		str_format(pBuf, BufSize, "%.*f%s", Precision, Value, pUnit);
}

inline void FormatRateValue(char *pBuf, int BufSize, float BytesPerSec)
{
	if(!std::isfinite(BytesPerSec) || BytesPerSec < 0.0f)
	{
		str_copy(pBuf, "--", BufSize);
		return;
	}

	const float KibPerSec = QmComputeRateKibPerSec(BytesPerSec);
	if(KibPerSec >= 1024.0f)
		str_format(pBuf, BufSize, "%.1fMiB/s", KibPerSec / 1024.0f);
	else
		str_format(pBuf, BufSize, "%.1fKiB/s", KibPerSec);
}

inline void FormatCpuRatioValue(char *pBuf, int BufSize, float ProcessCpuPct, float TotalCpuPct)
{
	if(!std::isfinite(ProcessCpuPct) || ProcessCpuPct < 0.0f)
	{
		str_copy(pBuf, "--", BufSize);
		return;
	}
	if(!std::isfinite(TotalCpuPct) || TotalCpuPct < 0.0f)
	{
		str_format(pBuf, BufSize, "%.0f%%", ProcessCpuPct);
		return;
	}
	str_format(pBuf, BufSize, "%.0f%%/%.0f%%", ProcessCpuPct, TotalCpuPct);
}

template<size_t N>
inline SQmHistoryStats QmComputeHistoryStats(const std::array<float, N> &aHistory, int HistoryHead, int HistoryCount)
{
	SQmHistoryStats Stats;
	if(HistoryCount <= 0)
		return Stats;

	const int Start = (HistoryHead - HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
	float Sum = 0.0f;
	int ValidCount = 0;
	for(int i = 0; i < HistoryCount; ++i)
	{
		const float Value = aHistory[(Start + i) % (int)aHistory.size()];
		if(!std::isfinite(Value))
			continue;
		Stats.m_Current = Value;
		if(ValidCount == 0)
		{
			Stats.m_Min = Value;
			Stats.m_Max = Value;
		}
		Stats.m_Min = std::min(Stats.m_Min, Value);
		Stats.m_Max = std::max(Stats.m_Max, Value);
		Sum += Value;
		++ValidCount;
	}
	if(ValidCount > 0)
	{
		Stats.m_Average = Sum / (float)ValidCount;
		Stats.m_HasData = true;
	}
	return Stats;
}

template<size_t N>
inline float QmComputeHistoryPercentile(const std::array<float, N> &aHistory, int HistoryHead, int HistoryCount, float Percentile)
{
	if(HistoryCount <= 0)
		return -1.0f;

	const int Count = std::min(HistoryCount, (int)aHistory.size());
	const int Start = (HistoryHead - Count + (int)aHistory.size()) % (int)aHistory.size();
	std::array<float, N> aValues = {};
	int ValidCount = 0;
	for(int i = 0; i < Count; ++i)
	{
		const float Value = aHistory[(Start + i) % (int)aHistory.size()];
		if(std::isfinite(Value))
			aValues[ValidCount++] = Value;
	}
	if(ValidCount == 0)
		return -1.0f;
	std::sort(aValues.begin(), aValues.begin() + ValidCount);

	const float NormalizedPercentile = std::clamp(Percentile, 0.0f, 100.0f) / 100.0f;
	const int Index = std::clamp((int)std::ceil(NormalizedPercentile * (ValidCount - 1)), 0, ValidCount - 1);
	return aValues[Index];
}

template<size_t N>
inline int QmFindLatestPeakIndex(const std::array<float, N> &aHistory, int HistoryHead, int HistoryCount)
{
	if(HistoryCount <= 0)
		return 0;

	const int Start = (HistoryHead - HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
	float PeakValue = -std::numeric_limits<float>::infinity();
	for(int i = 0; i < HistoryCount; ++i)
	{
		const float Value = aHistory[(Start + i) % (int)aHistory.size()];
		if(std::isfinite(Value))
			PeakValue = std::max(PeakValue, Value);
	}
	if(!std::isfinite(PeakValue))
		return -1;

	const float Tolerance = std::max(0.05f, std::abs(PeakValue) * 0.005f);
	for(int i = HistoryCount - 1; i >= 0; --i)
	{
		const float Value = aHistory[(Start + i) % (int)aHistory.size()];
		if(std::isfinite(Value) && Value >= PeakValue - Tolerance)
			return i;
	}
	return 0;
}

template<size_t N>
inline int QmFindLatestAbsolutePeakIndex(const std::array<float, N> &aHistory, int HistoryHead, int HistoryCount)
{
	if(HistoryCount <= 0)
		return 0;

	const int Start = (HistoryHead - HistoryCount + (int)aHistory.size()) % (int)aHistory.size();
	float PeakValue = -std::numeric_limits<float>::infinity();
	for(int i = 0; i < HistoryCount; ++i)
	{
		const float Value = aHistory[(Start + i) % (int)aHistory.size()];
		if(std::isfinite(Value))
			PeakValue = std::max(PeakValue, std::abs(Value));
	}
	if(!std::isfinite(PeakValue))
		return -1;

	const float Tolerance = std::max(0.05f, PeakValue * 0.005f);
	for(int i = HistoryCount - 1; i >= 0; --i)
	{
		const float Value = std::abs(aHistory[(Start + i) % (int)aHistory.size()]);
		if(std::isfinite(Value) && Value >= PeakValue - Tolerance)
			return i;
	}
	return 0;
}

inline EQmConnectionGrade QmDetermineConnectionGrade(const SQmNetworkMetrics &Net)
{
	if(!Net.m_Connected)
		return EQmConnectionGrade::DISCONNECTED;
	if(Net.m_PredictionJitterMs >= 25.0f || Net.m_SnapshotGapMs >= 180.0f || Net.m_PingMs >= 180.0f || Net.m_PredictionLeadMs >= 180.0f || Net.m_ConnectionProblems)
		return EQmConnectionGrade::SEVERE;
	if(Net.m_PredictionJitterMs >= 10.0f || Net.m_SnapshotGapMs >= 90.0f || Net.m_PingMs >= 90.0f || Net.m_PredictionLeadMs >= 90.0f)
		return EQmConnectionGrade::ELEVATED;
	return EQmConnectionGrade::NORMAL;
}

inline EQmDiagnosticCause QmDeterminePrimaryCause(const SQmNetworkMetrics &Net, const SQmPerformanceMetrics &Perf, EQmConnectionGrade Grade)
{
	if(Grade == EQmConnectionGrade::DISCONNECTED)
		return EQmDiagnosticCause::NONE;
	if(Grade == EQmConnectionGrade::NORMAL &&
		(Perf.m_FrameTimeP95Ms > 16.7f || Perf.m_CpuUsagePct >= 75.0f || Perf.m_PredictionStress >= 12.0f))
		return EQmDiagnosticCause::CLIENT_PERFORMANCE;
	if(Net.m_SnapshotGapMs >= 90.0f || Net.m_ConnectionProblems)
		return EQmDiagnosticCause::SNAPSHOT_GAP;
	if(Net.m_PredictionJitterMs >= 10.0f)
		return EQmDiagnosticCause::PREDICTION_JITTER;
	if(Net.m_PredictionLeadMs >= 90.0f)
		return EQmDiagnosticCause::PREDICTION;
	return EQmDiagnosticCause::NONE;
}

inline SQmMonitoringHudLayout QmComputeMonitoringHudLayout(float ScreenWidth, float ScreenHeight, float GraphX, float GraphSpacing)
{
	SQmMonitoringHudLayout Layout;
	const float UiScale = QmComputeMonitoringUiScale(ScreenWidth, ScreenHeight);
	const float Padding = std::round(QM_MONITORING_PANEL_PADDING * UiScale);

	float PanelW = std::round(ScreenWidth * 0.42f);
	float PanelH = std::round(ScreenHeight * 0.78f);
	PanelW = std::max(PanelW, 620.0f * UiScale);
	PanelH = std::max(PanelH, 560.0f * UiScale);
	PanelW = std::min(PanelW, 760.0f * UiScale);
	PanelH = std::min(PanelH, 860.0f * UiScale);
	const float PreferredContentHeight = (QM_MONITORING_HEADER_HEIGHT +
						     QM_MONITORING_SECTION_GAP * 4.0f +
						     QM_MONITORING_MAIN_GRAPH_HEIGHT +
						     QM_MONITORING_FPS_GRAPH_HEIGHT +
						     QM_MONITORING_PRIMARY_CARDS_HEIGHT +
						     QM_MONITORING_SECONDARY_CARDS_HEIGHT) *
					     UiScale;
	PanelH = std::min(PanelH, std::round(PreferredContentHeight + Padding * 2.0f));
	PanelW = std::min(PanelW, ScreenWidth);
	PanelH = std::min(PanelH, ScreenHeight);

	Layout.m_PanelRect.w = PanelW;
	Layout.m_PanelRect.h = PanelH;
	Layout.m_PanelRect.x = std::clamp(GraphX - PanelW - GraphSpacing, 0.0f, std::max(ScreenWidth - PanelW, 0.0f));
	Layout.m_PanelRect.y = std::clamp(GraphSpacing * 2.0f, 0.0f, std::max(ScreenHeight - PanelH, 0.0f));

	Layout.m_ContentRect = Layout.m_PanelRect;
	Layout.m_ContentRect.x += Padding;
	Layout.m_ContentRect.y += Padding;
	Layout.m_ContentRect.w = std::max(0.0f, Layout.m_ContentRect.w - Padding * 2.0f);
	Layout.m_ContentRect.h = std::max(0.0f, Layout.m_ContentRect.h - Padding * 2.0f);
	return Layout;
}

inline SQmMonitoringBodyLayout QmComputeMonitoringBodyLayout(float ContentHeight, float UiScale)
{
	SQmMonitoringBodyLayout Layout;
	const float HeaderHeight = QM_MONITORING_HEADER_HEIGHT * UiScale;
	const float SectionGap = QM_MONITORING_SECTION_GAP * UiScale;
	const float MainGraphHeight = QM_MONITORING_MAIN_GRAPH_HEIGHT * UiScale;
	const float FpsGraphHeight = QM_MONITORING_FPS_GRAPH_HEIGHT * UiScale;
	const float PrimaryCardsHeight = QM_MONITORING_PRIMARY_CARDS_HEIGHT * UiScale;
	const float MetricsExtraHeight = QM_MONITORING_SECONDARY_CARDS_HEIGHT * UiScale;
	const float MainGraphMinHeight = 190.0f * UiScale;
	const float FpsGraphMinHeight = 120.0f * UiScale;
	const float AvailableBodyHeight = std::max(ContentHeight - HeaderHeight - SectionGap * 4.0f, 0.0f);
	if(AvailableBodyHeight <= 0.0f)
		return Layout;

	const float PreferredGraphTotal = MainGraphHeight + FpsGraphHeight;
	const float MinimumGraphTotal = MainGraphMinHeight + FpsGraphMinHeight;
	const float ReservedCardTotal = PrimaryCardsHeight + MetricsExtraHeight;

	Layout.m_PrimaryCardsHeight = PrimaryCardsHeight;
	Layout.m_MetricsExtraHeight = MetricsExtraHeight;

	if(AvailableBodyHeight >= ReservedCardTotal + PreferredGraphTotal)
	{
		Layout.m_MainGraphHeight = MainGraphHeight;
		Layout.m_FpsGraphHeight = FpsGraphHeight;
		return Layout;
	}

	const float AvailableGraphHeight = std::max(AvailableBodyHeight - ReservedCardTotal, 0.0f);
	if(AvailableGraphHeight >= MinimumGraphTotal && PreferredGraphTotal > MinimumGraphTotal)
	{
		const float GraphScale = (AvailableGraphHeight - MinimumGraphTotal) / (PreferredGraphTotal - MinimumGraphTotal);
		Layout.m_MainGraphHeight = MainGraphMinHeight + (MainGraphHeight - MainGraphMinHeight) * GraphScale;
		Layout.m_FpsGraphHeight = FpsGraphMinHeight + (FpsGraphHeight - FpsGraphMinHeight) * GraphScale;
		Layout.m_MetricsExtraHeight = std::max(AvailableBodyHeight - Layout.m_MainGraphHeight - Layout.m_FpsGraphHeight - Layout.m_PrimaryCardsHeight, 0.0f);
		return Layout;
	}

	const float FallbackTotal = MinimumGraphTotal + ReservedCardTotal;
	const float FallbackScale = FallbackTotal > 0.0f ? std::clamp(AvailableBodyHeight / FallbackTotal, 0.0f, 1.0f) : 0.0f;
	Layout.m_MainGraphHeight = MainGraphMinHeight * FallbackScale;
	Layout.m_FpsGraphHeight = FpsGraphMinHeight * FallbackScale;
	Layout.m_PrimaryCardsHeight = PrimaryCardsHeight * FallbackScale;
	Layout.m_MetricsExtraHeight = std::max(AvailableBodyHeight - Layout.m_MainGraphHeight - Layout.m_FpsGraphHeight - Layout.m_PrimaryCardsHeight, 0.0f);
	return Layout;
}

class CQmMonitoring : public CComponent
{
	SQmMonitoringSnapshot m_Snapshot;
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aRttHistory = {};
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aPredHistory = {};
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aSnapshotGapHistory = {};
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aPredictionJitterHistory = {};
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aGameTimeMarginHistory = {};
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aFpsHistory = {};
	std::array<float, QM_MONITORING_HISTORY_CAPACITY> m_aFrameTimeHistory = {};
	SQmMonitoringGraphCache m_GraphCache;
	int m_HistoryHead = 0;
	int m_HistoryCount = 0;
	int64_t m_LastSampleTick = 0;
	int64_t m_LastPercentileTick = 0;
	float m_CachedFrameTimeP95Ms = -1.0f;
	float m_CachedFrameTimeP99Ms = -1.0f;
	float m_CachedFpsOnePctLow = -1.0f;
	int m_CachedLongFrameCount = 0;
	int64_t m_LastSnapshotRateSampleTime = 0;
	uint64_t m_LastSnapshotCount = 0;
	uint64_t m_LastSnapshotPartCount = 0;
	uint64_t m_LastSnapshotPayloadBytes = 0;
	float m_SnapshotRatePerSec = -1.0f;
	float m_SnapshotPartRatePerSec = -1.0f;
	float m_SnapshotPayloadBytesPerSec = -1.0f;
	int m_SnapshotRateConnection = -1;
	float m_LastPredictionLeadMs = 0.0f;
	float m_PredictionJitterMs = -1.0f;
	bool m_HasPredictionLeadSample = false;

	void ResetHistory();
	void UpdateNetworkMetrics(SQmNetworkMetrics &Net);
	void UpdatePerformanceMetrics(SQmPerformanceMetrics &Perf);
	void UpdateDiagnosticVerdict(SQmDiagnosticVerdict &Verdict, const SQmNetworkMetrics &Net, const SQmPerformanceMetrics &Perf);
	void PushFrameTimeSample(float FrameTimeMs);
	void PushHistorySample(float RttMs, float PredMs, float SnapshotGapMs, float PredictionJitterMs, float GameTimeMarginMs, float Fps);
	void UpdateGraphCache();
	void RenderHeader(CUIRect Rect) const;
	void RenderMainGraph(CUIRect Rect) const;
	void RenderFpsGraph(CUIRect Rect) const;
	void RenderPrimaryCards(CUIRect Rect) const;
	void RenderDebugDetails(CUIRect Rect) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;

	void UpdateSnapshot();
	void RenderHud(CUIRect View) const;

	const SQmMonitoringSnapshot &Snapshot() const { return m_Snapshot; }
};

#endif
