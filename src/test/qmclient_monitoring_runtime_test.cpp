// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1
#include <engine/client/game_ping.h>
#include <engine/client/gpu_upload_limiter.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/components/qmclient/monitoring/monitoring.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/settings_perf_windows.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/stutter_diagnostics.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/frame_scheduler.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace
{
	template<typename TPredicate>
	bool WaitUntil(TPredicate Predicate, std::chrono::milliseconds Timeout = std::chrono::milliseconds(200))
	{
		const auto Deadline = std::chrono::steady_clock::now() + Timeout;
		while(std::chrono::steady_clock::now() < Deadline)
		{
			if(Predicate())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}
}

TEST(QmMonitoringRuntimeContract, ConnectionGradeTracksDisconnectedState)
{
	SQmNetworkMetrics Net;
	Net.m_Connected = false;
	EXPECT_EQ(QmDetermineConnectionGrade(Net), EQmConnectionGrade::DISCONNECTED);
}

TEST(QmMonitoringRuntimeContract, ConnectionGradeUsesThresholdTable)
{
	SQmNetworkMetrics Net;
	Net.m_Connected = true;
	Net.m_PingMs = 40.0f;
	Net.m_PredictionLeadMs = 50.0f;
	Net.m_PredictionJitterMs = 5.0f;
	EXPECT_EQ(QmDetermineConnectionGrade(Net), EQmConnectionGrade::NORMAL);

	Net.m_PredictionLeadMs = 110.0f;
	EXPECT_EQ(QmDetermineConnectionGrade(Net), EQmConnectionGrade::ELEVATED);

	Net.m_PredictionLeadMs = 210.0f;
	EXPECT_EQ(QmDetermineConnectionGrade(Net), EQmConnectionGrade::SEVERE);
}

TEST(QmMonitoringRuntimeContract, PrimaryCausePrefersDominantMetric)
{
	SQmNetworkMetrics Net;
	SQmPerformanceMetrics Perf;

	Net.m_Connected = false;
	EXPECT_EQ(QmDeterminePrimaryCause(Net, Perf, EQmConnectionGrade::DISCONNECTED), EQmDiagnosticCause::NONE);

	Net.m_Connected = true;
	Net.m_SnapshotGapMs = 120.0f;
	Net.m_PredictionLeadMs = 40.0f;
	EXPECT_EQ(QmDeterminePrimaryCause(Net, Perf, EQmConnectionGrade::ELEVATED), EQmDiagnosticCause::SNAPSHOT_GAP);

	Net.m_SnapshotGapMs = 20.0f;
	Net.m_PredictionLeadMs = 95.0f;
	EXPECT_EQ(QmDeterminePrimaryCause(Net, Perf, EQmConnectionGrade::ELEVATED), EQmDiagnosticCause::PREDICTION);

	Net.m_PredictionLeadMs = 30.0f;
	Net.m_PredictionJitterMs = 28.0f;
	EXPECT_EQ(QmDeterminePrimaryCause(Net, Perf, EQmConnectionGrade::SEVERE), EQmDiagnosticCause::PREDICTION_JITTER);

	Net.m_PredictionJitterMs = 6.0f;
	Net.m_VitalResendCount = 8;
	EXPECT_EQ(QmDeterminePrimaryCause(Net, Perf, EQmConnectionGrade::SEVERE), EQmDiagnosticCause::NONE);

	Net.m_ConnectionProblems = true;
	EXPECT_EQ(QmDeterminePrimaryCause(Net, Perf, EQmConnectionGrade::SEVERE), EQmDiagnosticCause::SNAPSHOT_GAP);
}

TEST(QmMonitoringRuntimeContract, RollbackAmountUsesNegativeGameTimeMargin)
{
	EXPECT_FLOAT_EQ(QmComputeRollbackMs(-18.0f), 18.0f);
	EXPECT_FLOAT_EQ(QmComputeRollbackMs(6.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeRollbackMs(std::numeric_limits<float>::quiet_NaN()), -1.0f);
}

TEST(QmMonitoringRuntimeContract, PeakSelectionPrefersLatestMatchingPeak)
{
	std::array<float, 8> aHistory = {41.0f, 39.0f, 41.0f, 24.0f, 24.0f, 18.0f, 24.0f, 17.0f};
	EXPECT_EQ(QmFindLatestPeakIndex(aHistory, 0, 8), 2);
	EXPECT_EQ(QmFindLatestAbsolutePeakIndex(aHistory, 0, 8), 2);
}

TEST(QmMonitoringRuntimeContract, SignedPeakSelectionUsesLatestAbsolutePeak)
{
	std::array<float, 8> aHistory = {-7.0f, 5.0f, -9.0f, 3.0f, 9.0f, 4.0f, 8.0f, 2.0f};
	EXPECT_EQ(QmFindLatestAbsolutePeakIndex(aHistory, 0, 8), 4);
}

TEST(QmMonitoringLayoutContract, UiScaleGrowsOnHighResolutionScreens)
{
	EXPECT_FLOAT_EQ(QmComputeMonitoringUiScale(800.0f, 600.0f), 0.65f);
	const float Expected1600x900 = std::sqrt((1600.0f / 1920.0f) * (900.0f / 1080.0f));
	EXPECT_FLOAT_EQ(QmComputeMonitoringUiScale(1600.0f, 900.0f), Expected1600x900);
	EXPECT_FLOAT_EQ(QmComputeMonitoringUiScale(3840.0f, 2160.0f), 1.8f);
}

TEST(QmMonitoringLayoutContract, PanelOpacityClampsPercentToUnitRange)
{
	EXPECT_FLOAT_EQ(QmComputeMonitoringPanelOpacity(-20), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeMonitoringPanelOpacity(35), 0.35f);
	EXPECT_FLOAT_EQ(QmComputeMonitoringPanelOpacity(140), 1.0f);
}

TEST(QmMonitoringRuntimeContract, ProcessCpuUsageNormalizesAcrossLogicalCpus)
{
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(-1.0f, 8), -1.0f);
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(114.0f, 8), 14.25f);
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(1600.0f, 16), 100.0f);
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(114.0f, 0), 100.0f);
}

TEST(QmMonitoringRuntimeContract, TotalCpuUsageComputesBusyDelta)
{
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 125, 1100), 75.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 200, 1100), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 100, 1100), 100.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 0, 125, 1100), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 90, 1100), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 125, 990), -1.0f);
}

TEST(QmMonitoringRuntimeContract, CpuRatioValueShowsProcessAndTotalCpu)
{
	char aBuf[32];
	FormatCpuRatioValue(aBuf, sizeof(aBuf), -1.0f, 35.0f);
	EXPECT_STREQ(aBuf, "--");
	FormatCpuRatioValue(aBuf, sizeof(aBuf), 12.4f, -1.0f);
	EXPECT_STREQ(aBuf, "12%");
	FormatCpuRatioValue(aBuf, sizeof(aBuf), 12.4f, 35.6f);
	EXPECT_STREQ(aBuf, "12%/36%");
}

TEST(QmMonitoringRuntimeContract, TrafficStatsMatchOfficialDebugMath)
{
	const auto Stats = QmComputeTrafficStats(10, 1000, 14, 1320, 1.0f);
	EXPECT_EQ(Stats.m_Packets, 4u);
	EXPECT_EQ(Stats.m_PayloadBytes, 320u);
	EXPECT_EQ(Stats.m_OverheadBytes, 168u);
	EXPECT_EQ(Stats.m_TotalBytes, 488u);
	EXPECT_EQ(Stats.m_AveragePayloadBytes, 80u);
	EXPECT_FLOAT_EQ(Stats.m_RateKibPerSec, 0.4765625f);
}

TEST(QmMonitoringRuntimeContract, CounterRateRejectsResetAndMissingWindow)
{
	EXPECT_FLOAT_EQ(QmComputeCounterRate(100, 160, 2.0f), 30.0f);
	EXPECT_FLOAT_EQ(QmComputeCounterRate(160, 100, 2.0f), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeCounterRate(100, 160, 0.0f), -1.0f);
}

TEST(QmMonitoringNetworkContract, GamePingProbeOnlyAcceptsCurrentUuid)
{
	const int64_t Frequency = 1000;
	const CUuid ProbeUuid = CalculateUuid("game-ping-current@test");
	const CUuid WrongUuid = CalculateUuid("game-ping-wrong@test");
	SGamePingProbe Probe;

	Probe.Begin(ProbeUuid, 10000, Frequency);
	EXPECT_FALSE(Probe.HandlePong(WrongUuid, 10025, Frequency));
	EXPECT_EQ(Probe.m_StartTime, 10000);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_Uuid, ProbeUuid);

	EXPECT_TRUE(Probe.HandlePong(ProbeUuid, 10025, Frequency));
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_EQ(Probe.m_NextTime, 11025);
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
}

TEST(QmMonitoringNetworkContract, GamePingProbeRejectsLatePongAfterTimeoutAndReplacement)
{
	const int64_t Frequency = 1000;
	const CUuid ProbeA = CalculateUuid("game-ping-a@test");
	const CUuid ProbeB = CalculateUuid("game-ping-b@test");
	SGamePingProbe Probe;

	Probe.Begin(ProbeA, 10000, Frequency);
	EXPECT_FALSE(Probe.HandleTimeout(11999, Frequency));
	EXPECT_TRUE(Probe.HandleTimeout(12000, Frequency));
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_NextTime, 13000);

	Probe.Begin(ProbeB, 13000, Frequency);
	EXPECT_FALSE(Probe.HandlePong(ProbeA, 13040, Frequency));
	EXPECT_EQ(Probe.m_StartTime, 13000);
	EXPECT_EQ(Probe.m_Uuid, ProbeB);
	EXPECT_TRUE(Probe.HandlePong(ProbeB, 13050, Frequency));
	EXPECT_EQ(Probe.m_RttMs, 50);

	Probe.Reset();
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_EQ(Probe.m_NextTime, -1);
	EXPECT_EQ(Probe.m_RttMs, -1);
	EXPECT_EQ(Probe.m_Uuid, UUID_ZEROED);
}

TEST(QmMonitoringNetworkContract, ManualPingProbeRejectsOverlappingRequests)
{
	SManualPingProbe Probe;
	EXPECT_TRUE(Probe.Begin(10000));
	EXPECT_FALSE(Probe.Begin(10010));
	EXPECT_EQ(Probe.m_StartTime, 10000);

	float RttMs = -1.0f;
	EXPECT_TRUE(Probe.HandlePong(10025, 1000, RttMs));
	EXPECT_FLOAT_EQ(RttMs, 25.0f);
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_FALSE(Probe.HandlePong(10030, 1000, RttMs));
}

TEST(QmMonitoringNetworkContract, ManualPingProbeRecoversAfterTimeout)
{
	SManualPingProbe Probe;
	EXPECT_TRUE(Probe.Begin(10000));
	EXPECT_FALSE(Probe.HandleTimeout(11999, 1000));
	EXPECT_TRUE(Probe.HandleTimeout(12000, 1000));
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_TRUE(Probe.Begin(13000));
}

TEST(QmMonitoringNetworkContract, LegacyGamePingProbeAcceptsLegacyReply)
{
	SGamePingProbe Probe;
	Probe.BeginLegacy(10000, 1000);
	EXPECT_TRUE(Probe.m_Legacy);
	EXPECT_TRUE(Probe.HandleLegacyPong(10025, 1000));
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_FALSE(Probe.m_Legacy);
}

TEST(QmMonitoringNetworkContract, LegacyManualPingSharesAutomaticProbeAndReply)
{
	constexpr int64_t Start = 10000;
	constexpr int64_t Frequency = 1000;
	SGamePingProbe Automatic;
	SManualPingProbe Manual;

	// Legacy 回复没有请求 ID，因此两个路径必须表示同一个线上请求。
	EXPECT_TRUE(Manual.Begin(Start));
	Automatic.BeginLegacy(Start, Frequency);
	EXPECT_TRUE(Automatic.HandleLegacyPong(Start + 25, Frequency));
	float RttMs = -1.0f;
	EXPECT_TRUE(Manual.HandlePong(Start + 25, Frequency, RttMs));
	EXPECT_FLOAT_EQ(RttMs, 25.0f);
	EXPECT_EQ(Automatic.m_StartTime, -1);
	EXPECT_EQ(Manual.m_StartTime, -1);
}

TEST(QmMonitoringRuntimeContract, FormattersRejectNonFiniteMetrics)
{
	const float InvalidMetric = std::numeric_limits<float>::quiet_NaN();
	char aBuf[32];
	FormatMetricValue(aBuf, sizeof(aBuf), "ms", InvalidMetric);
	EXPECT_STREQ(aBuf, "--");
	FormatRateValue(aBuf, sizeof(aBuf), InvalidMetric);
	EXPECT_STREQ(aBuf, "--");
	FormatCpuRatioValue(aBuf, sizeof(aBuf), InvalidMetric, 20.0f);
	EXPECT_STREQ(aBuf, "--");
}

TEST(QmMonitoringRuntimeContract, HistoryPercentileUsesRecentSamples)
{
	const std::array<float, 4> aHistory = {10.0f, 20.0f, 30.0f, 40.0f};
	EXPECT_FLOAT_EQ(QmComputeHistoryPercentile(aHistory, 0, 4, 50.0f), 30.0f);
	EXPECT_FLOAT_EQ(QmComputeHistoryPercentile(aHistory, 0, 4, 95.0f), 40.0f);
	EXPECT_FLOAT_EQ(QmComputeHistoryPercentile(aHistory, 0, 0, 95.0f), -1.0f);
}

TEST(QmMonitoringRuntimeContract, HistoryStatsIgnoreUnavailableSamples)
{
	const float InvalidMetric = std::numeric_limits<float>::quiet_NaN();
	const std::array<float, 4> aHistory = {InvalidMetric, 10.0f, InvalidMetric, 20.0f};
	const SQmHistoryStats Stats = QmComputeHistoryStats(aHistory, 0, 4);
	EXPECT_TRUE(Stats.m_HasData);
	EXPECT_FLOAT_EQ(Stats.m_Current, 20.0f);
	EXPECT_FLOAT_EQ(Stats.m_Min, 10.0f);
	EXPECT_FLOAT_EQ(Stats.m_Max, 20.0f);
	EXPECT_FLOAT_EQ(Stats.m_Average, 15.0f);
}

TEST(QmMonitoringLayoutContract, HudLayoutPlacesPanelLeftOfGraphColumn)
{
	// 紧凑化布局：1600x900 的 UiScale=5/6，面板宽受 760*UiScale 上限约束，
	// 高受内容高度(674*UiScale+2*Padding)约束；面板右缘对齐 GraphX-GraphSpacing。
	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(1600.0f, 900.0f, 1184.0f, 16.0f);
	EXPECT_NEAR(Layout.m_PanelRect.w, 633.333f, 0.01f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.h, 582.0f);
	EXPECT_NEAR(Layout.m_PanelRect.x, 534.667f, 0.01f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.y, 32.0f);
	EXPECT_NEAR(Layout.m_ContentRect.x, 544.667f, 0.01f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.y, 42.0f);
}

TEST(QmMonitoringLayoutContract, BodyLayoutPreservesMetricsBudgetOnCompactPanels)
{
	const SQmMonitoringBodyLayout Layout = QmComputeMonitoringBodyLayout(260.0f, 1.0f);
	const float AvailableBodyHeight = 260.0f - QM_MONITORING_HEADER_HEIGHT - QM_MONITORING_SECTION_GAP * 4.0f;
	EXPECT_GE(Layout.m_MainGraphHeight, 0.0f);
	EXPECT_GE(Layout.m_FpsGraphHeight, 0.0f);
	EXPECT_GE(Layout.m_PrimaryCardsHeight, 0.0f);
	EXPECT_GE(Layout.m_MetricsExtraHeight, 0.0f);
	EXPECT_LE(Layout.m_MainGraphHeight, 190.0f);
	EXPECT_LE(Layout.m_FpsGraphHeight, 120.0f);
	EXPECT_LE(Layout.m_PrimaryCardsHeight, QM_MONITORING_PRIMARY_CARDS_HEIGHT);
	EXPECT_NEAR(Layout.m_MainGraphHeight + Layout.m_FpsGraphHeight + Layout.m_PrimaryCardsHeight + Layout.m_MetricsExtraHeight, AvailableBodyHeight, 0.001f);
	EXPECT_GT(Layout.m_PrimaryCardsHeight, 0.0f);
}

TEST(QmMonitoringLayoutContract, HudLayoutUsesLargerPanelOn4kScreens)
{
	// 4K 的 UiScale 被 clamp 到 1.8：面板宽 760*1.8=1368，高受内容高度约束 1257，
	// 仍是 1080p(633x582) 的两倍以上，"更大面板"语义保持。
	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(3840.0f, 2160.0f, 2842.0f, 38.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.w, 1368.0f);
	EXPECT_GT(Layout.m_PanelRect.h, 1200.0f);
	EXPECT_LE(Layout.m_PanelRect.y + Layout.m_PanelRect.h, 2160.0f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.x, 1458.0f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.y, 98.0f);
}

TEST(QmMonitoringLayoutContract, HudLayoutClampsPanelInsideScreenBounds)
{
	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(360.0f, 240.0f, 120.0f, 16.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.x, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.y, 0.0f);
	EXPECT_LE(Layout.m_PanelRect.x + Layout.m_PanelRect.w, 360.0f);
	EXPECT_LE(Layout.m_PanelRect.y + Layout.m_PanelRect.h, 240.0f);
}

TEST(QmMonitoringRuntimeContract, DeviceMetricsDefaultToUnavailable)
{
	SQmPerformanceMetrics Perf;
	EXPECT_FALSE(Perf.m_DeviceSampleAvailable);
	EXPECT_FLOAT_EQ(Perf.m_GpuUtilPct, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_GpuDedicatedVramMb, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_GpuDedicatedVramBudgetMb, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_GpuSharedVramMb, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_DiskReadMbPerSec, -1.0f);
}

TEST(QmMonitoringRuntimeContract, UiQuadBatchFlushGuardsInvalidRectsAndKeepsSubmissionPlan)
{
	EXPECT_FALSE(UiBatchableRectHasPositiveSize(0.0f, 10.0f));
	EXPECT_FALSE(UiBatchableRectHasPositiveSize(10.0f, 0.0f));
	EXPECT_FALSE(UiBatchableRectHasPositiveSize(-1.0f, 10.0f));
	EXPECT_FALSE(UiBatchableRectHasPositiveSize(10.0f, -1.0f));
	EXPECT_TRUE(UiBatchableRectHasPositiveSize(1.0f, 1.0f));
	EXPECT_TRUE(UiBatchableRectHasPositiveSize(200.0f, 24.0f));

	EXPECT_FALSE(UiQuadBatchHasPendingSubmission(-1, 0));
	EXPECT_FALSE(UiQuadBatchHasPendingSubmission(-1, 3));
	EXPECT_FALSE(UiQuadBatchHasPendingSubmission(2, 0));
	EXPECT_TRUE(UiQuadBatchHasPendingSubmission(0, 1));
	EXPECT_TRUE(UiQuadBatchHasPendingSubmission(5, 4));

	const ColorRGBA Red(1.0f, 0.0f, 0.0f, 1.0f);
	const ColorRGBA Blue(0.0f, 0.0f, 1.0f, 1.0f);

	{
		const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(false, -1, Red, -1, Blue);
		EXPECT_TRUE(Plan.m_LeavesBatchUntouched);
		EXPECT_FALSE(Plan.m_RenderImmediately);
		EXPECT_FALSE(Plan.m_FlushBeforeQueue);
		EXPECT_FALSE(Plan.m_QueueSprite);
	}

	{
		const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(false, -1, Red, 7, Blue);
		EXPECT_FALSE(Plan.m_LeavesBatchUntouched);
		EXPECT_TRUE(Plan.m_RenderImmediately);
		EXPECT_FALSE(Plan.m_FlushBeforeQueue);
		EXPECT_FALSE(Plan.m_QueueSprite);
	}

	{
		const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(true, -1, Red, 7, Blue);
		EXPECT_FALSE(Plan.m_LeavesBatchUntouched);
		EXPECT_FALSE(Plan.m_RenderImmediately);
		EXPECT_FALSE(Plan.m_FlushBeforeQueue);
		EXPECT_TRUE(Plan.m_QueueSprite);
	}

	{
		const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(true, 7, Red, 7, Red);
		EXPECT_FALSE(Plan.m_LeavesBatchUntouched);
		EXPECT_FALSE(Plan.m_RenderImmediately);
		EXPECT_FALSE(Plan.m_FlushBeforeQueue);
		EXPECT_TRUE(Plan.m_QueueSprite);
	}

	{
		const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(true, 7, Red, 8, Red);
		EXPECT_FALSE(Plan.m_LeavesBatchUntouched);
		EXPECT_FALSE(Plan.m_RenderImmediately);
		EXPECT_TRUE(Plan.m_FlushBeforeQueue);
		EXPECT_TRUE(Plan.m_QueueSprite);
	}

	{
		const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(true, 7, Red, 7, Blue);
		EXPECT_FALSE(Plan.m_LeavesBatchUntouched);
		EXPECT_FALSE(Plan.m_RenderImmediately);
		EXPECT_TRUE(Plan.m_FlushBeforeQueue);
		EXPECT_TRUE(Plan.m_QueueSprite);
	}
}

TEST(QmMonitoringPerfContract, SettingsPerfWindowAccumulatesFpsAndFrameStats)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("settings_open", "online", "settings:tee", "none", 30, false);

	Tracker.RecordFrame(0.010f, 12.0, false);
	Tracker.RecordFrame(0.020f, 30.0, false);
	Tracker.RecordFrame(0.0f, 99.0, false);
	Tracker.RecordFrame(-1.0f, 99.0, false);

	EXPECT_TRUE(Tracker.HasActiveWindow());
	const SQmSettingsPerfWindowSummary Summary = Tracker.FinishActiveWindow();

	EXPECT_STREQ(Summary.m_aOperation, "settings_open");
	EXPECT_STREQ(Summary.m_aContext, "online");
	EXPECT_STREQ(Summary.m_aPage, "settings:tee");
	EXPECT_EQ(Summary.m_SampleFrames, 2);
	EXPECT_NEAR(Summary.m_SampleSeconds, 0.030f, 0.0001f);
	EXPECT_NEAR(Summary.m_FpsAvg, 66.666f, 0.01f);
	EXPECT_NEAR(Summary.m_FpsMin, 50.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FpsMax, 100.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FpsOnePctLow, 50.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FrameMsAvg, 15.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FrameMsP95, 20.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FrameMsP99, 20.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FrameMsMax, 20.0f, 0.01f);
	EXPECT_NEAR(Summary.m_MenuMsMax, 30.0f, 0.01f);
	EXPECT_FALSE(Summary.m_CapLimited);
	EXPECT_FALSE(Tracker.HasActiveWindow());
}

TEST(QmMonitoringPerfContract, RealOnePctLowUsesFrameSamples)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("settings_tab_switch", "offline", "settings:assets", "entity_bg", 100, false, 1000);
	SQmSettingsPerfWindowFrameResult Result;
	for(int i = 0; i < 99; ++i)
		Result = Tracker.RecordFrame(0.001f, 1.0, false, 1001 + i);
	Result = Tracker.RecordFrame(0.100f, 100.0, false, 1100);
	ASSERT_TRUE(Result.m_ShouldFlush);
	const SQmSettingsPerfWindowSummary Summary = Result.m_Summary;

	EXPECT_EQ(Summary.m_WindowStartFrame, 1000);
	EXPECT_EQ(Summary.m_WindowEndFrame, 1100);
	EXPECT_NEAR(Summary.m_FrameMsP99, 1.0f, 0.01f);
	EXPECT_NEAR(Summary.m_FpsOnePctLow, 10.0f, 0.01f);
	EXPECT_NE(Summary.m_FpsOnePctLow, Summary.m_FrameMsP99 > 0.0f ? 1000.0f / Summary.m_FrameMsP99 : 0.0f);
}

TEST(QmMonitoringPerfContract, SettingsPerfWindowEndsAfterFixedFrameBudget)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("settings_tab_switch", "offline", "settings:tclient", "0", 3, true);

	EXPECT_FALSE(Tracker.RecordFrame(0.010f, 2.0, false).m_ShouldFlush);
	EXPECT_FALSE(Tracker.RecordFrame(0.010f, 3.0, false).m_ShouldFlush);
	const SQmSettingsPerfWindowFrameResult Result = Tracker.RecordFrame(0.010f, 4.0, false);

	ASSERT_TRUE(Result.m_ShouldFlush);
	EXPECT_EQ(Result.m_Summary.m_SampleFrames, 3);
	EXPECT_STREQ(Result.m_Summary.m_aOperation, "settings_tab_switch");
	EXPECT_STREQ(Result.m_Summary.m_aContext, "offline");
	EXPECT_STREQ(Result.m_Summary.m_aPage, "settings:tclient");
	EXPECT_STREQ(Result.m_Summary.m_aTab, "0");
	EXPECT_TRUE(Result.m_Summary.m_CapLimited);
	EXPECT_FALSE(Tracker.HasActiveWindow());
}

TEST(QmMonitoringPerfContract, SettingsPerfScrollWindowEndsAfterIdleTimeout)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartScrollWindow("settings_tee_scroll", "offline", "settings:tee", "none", 0.250f, false);

	EXPECT_FALSE(Tracker.RecordFrame(0.016f, 5.0, true).m_ShouldFlush);
	EXPECT_FALSE(Tracker.RecordFrame(0.100f, 6.0, false).m_ShouldFlush);
	EXPECT_FALSE(Tracker.RecordFrame(0.149f, 7.0, false).m_ShouldFlush);
	const SQmSettingsPerfWindowFrameResult Result = Tracker.RecordFrame(0.001f, 8.0, false);

	ASSERT_TRUE(Result.m_ShouldFlush);
	EXPECT_EQ(Result.m_Summary.m_SampleFrames, 4);
	EXPECT_STREQ(Result.m_Summary.m_aOperation, "settings_tee_scroll");
	EXPECT_NEAR(Result.m_Summary.m_MenuMsMax, 8.0f, 0.01f);
	EXPECT_FALSE(Tracker.HasActiveWindow());
}

TEST(QmMonitoringPerfContract, SettingsPerfWindowStartFlushesInterruptedWindow)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.StartFixedFrameWindow("settings_open", "offline", "settings:tee", "none", 30, false);
	Tracker.RecordFrame(0.016f, 7.0, false);
	Tracker.RecordFrame(0.017f, 8.0, false);

	const SQmSettingsPerfWindowFrameResult Interrupted = Tracker.StartScrollWindow("settings_tee_scroll", "offline", "settings:tee", "none", 0.250f, false);

	ASSERT_TRUE(Interrupted.m_ShouldFlush);
	EXPECT_STREQ(Interrupted.m_Summary.m_aOperation, "settings_open");
	EXPECT_EQ(Interrupted.m_Summary.m_SampleFrames, 2);
	EXPECT_TRUE(Tracker.HasActiveWindow());
	EXPECT_STREQ(Tracker.ActiveOperation(), "settings_tee_scroll");
}

TEST(QmMonitoringPerfContract, SettingsPerfWindowEnsureDoesNotRestartMatchingScrollOperation)
{
	CQmSettingsPerfWindowTracker Tracker;
	Tracker.EnsureScrollWindow("server_browser_scroll", "offline", "server_browser", "none", 0.250f, false, 100);
	Tracker.RecordFrame(0.010f, 3.0, true, 100);

	const SQmSettingsPerfWindowFrameResult Reused = Tracker.EnsureScrollWindow("server_browser_scroll", "offline", "server_browser", "none", 0.250f, false, 101);
	EXPECT_FALSE(Reused.m_ShouldFlush);
	const SQmSettingsPerfWindowSummary Summary = Tracker.FinishActiveWindow();
	EXPECT_EQ(Summary.m_SampleFrames, 1);
	EXPECT_EQ(Summary.m_WindowStartFrame, 100u);
	EXPECT_EQ(Summary.m_WindowEndFrame, 100u);
}

TEST(QmMonitoringPerfContract, ReenabledDeviceSamplerReportsFirstVersionAgain)
{
	CQmDevicePerfVersionTracker Tracker;
	EXPECT_FALSE(Tracker.Observe(0));
	EXPECT_TRUE(Tracker.Observe(1));
	EXPECT_FALSE(Tracker.Observe(1));
	EXPECT_TRUE(Tracker.Observe(2));

	Tracker.Reset();
	EXPECT_FALSE(Tracker.Observe(0));
	EXPECT_TRUE(Tracker.Observe(1));
	EXPECT_FALSE(Tracker.Observe(1));
}

TEST(QmMonitoringPerfContract, DevicePerfSnapshotCacheReturnsConsistentVersionedSnapshot)
{
	CQmDevicePerfSnapshotCache Cache;

	SQmDevicePerfSample First;
	First.m_GpuUtilPct = 11.0f;
	First.m_GpuDedicatedVramMb = 101.0f;
	First.m_GpuDedicatedVramBudgetMb = 2048.0f;
	First.m_Available = true;
	const SQmDevicePerfSnapshot FirstSnapshot = Cache.Publish(First);

	SQmDevicePerfSample Second;
	Second.m_GpuUtilPct = 27.5f;
	Second.m_GpuDedicatedVramMb = 205.0f;
	Second.m_GpuDedicatedVramBudgetMb = 4096.0f;
	Second.m_GpuSharedVramMb = 17.0f;
	Second.m_DiskReadMbPerSec = 3.5f;
	Second.m_Available = true;
	const SQmDevicePerfSnapshot Published = Cache.Publish(Second);

	const SQmDevicePerfSnapshot Read = Cache.Snapshot();
	EXPECT_GT(Published.m_Version, FirstSnapshot.m_Version);
	EXPECT_EQ(Read.m_Version, Published.m_Version);
	EXPECT_FLOAT_EQ(Read.m_Sample.m_GpuUtilPct, Second.m_GpuUtilPct);
	EXPECT_FLOAT_EQ(Read.m_Sample.m_GpuDedicatedVramMb, Second.m_GpuDedicatedVramMb);
	EXPECT_FLOAT_EQ(Read.m_Sample.m_GpuDedicatedVramBudgetMb, Second.m_GpuDedicatedVramBudgetMb);
	EXPECT_FLOAT_EQ(Read.m_Sample.m_GpuSharedVramMb, Second.m_GpuSharedVramMb);
	EXPECT_FLOAT_EQ(Read.m_Sample.m_DiskReadMbPerSec, Second.m_DiskReadMbPerSec);
	EXPECT_EQ(Read.m_Sample.m_Available, Second.m_Available);
}

TEST(QmMonitoringPerfContract, DevicePerfSnapshotCacheKeepsSampleAndVersionConsistentAcrossThreads)
{
	CQmDevicePerfSnapshotCache Cache;
	std::atomic<bool> Stop{false};
	std::atomic<int> MismatchCount{0};

	std::thread Writer([&]() {
		for(uint64_t Version = 1; Version <= 2000; ++Version)
		{
			SQmDevicePerfSample Sample;
			Sample.m_GpuUtilPct = (float)Version;
			Sample.m_GpuDedicatedVramMb = (float)Version * 2.0f;
			Sample.m_GpuDedicatedVramBudgetMb = (float)Version * 4.0f;
			Sample.m_Available = true;
			Cache.Publish(Sample);
		}
		Stop.store(true, std::memory_order_release);
	});

	std::thread Reader([&]() {
		while(!Stop.load(std::memory_order_acquire))
		{
			const SQmDevicePerfSnapshot Snapshot = Cache.Snapshot();
			if(Snapshot.m_Version == 0)
				continue;
			if(Snapshot.m_Sample.m_GpuUtilPct != (float)Snapshot.m_Version ||
				Snapshot.m_Sample.m_GpuDedicatedVramMb != (float)Snapshot.m_Version * 2.0f ||
				Snapshot.m_Sample.m_GpuDedicatedVramBudgetMb != (float)Snapshot.m_Version * 4.0f)
			{
				MismatchCount.fetch_add(1, std::memory_order_relaxed);
			}
		}
	});

	Writer.join();
	Reader.join();
	EXPECT_EQ(MismatchCount.load(std::memory_order_relaxed), 0);
}

TEST(QmMonitoringPerfContract, DevicePerfSamplerStateStopsWorkerOnDisableAndCanRestart)
{
	std::atomic<int> SampleCalls{0};
	auto SampleFn = [&SampleCalls]() {
		SQmDevicePerfSample Sample;
		Sample.m_GpuUtilPct = (float)SampleCalls.fetch_add(1, std::memory_order_relaxed) + 1.0f;
		Sample.m_Available = true;
		return Sample;
	};

	CQmAsyncDevicePerfSampler Sampler(SampleFn, std::chrono::milliseconds(5));
	QmUpdateDevicePerfSamplerState(Sampler, false);
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	EXPECT_EQ(SampleCalls.load(std::memory_order_relaxed), 0);

	QmUpdateDevicePerfSamplerState(Sampler, true);
	ASSERT_TRUE(WaitUntil([&]() { return SampleCalls.load(std::memory_order_relaxed) >= 2; }));

	QmUpdateDevicePerfSamplerState(Sampler, false);
	const int CallsAfterDisable = SampleCalls.load(std::memory_order_relaxed);
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	EXPECT_EQ(SampleCalls.load(std::memory_order_relaxed), CallsAfterDisable);
	const SQmDevicePerfSnapshot ClearedSnapshot = Sampler.Snapshot();
	EXPECT_EQ(ClearedSnapshot.m_Version, 0u);
	EXPECT_FALSE(ClearedSnapshot.m_Sample.m_Available);
	EXPECT_FLOAT_EQ(ClearedSnapshot.m_Sample.m_GpuUtilPct, -1.0f);

	QmUpdateDevicePerfSamplerState(Sampler, true);
	ASSERT_TRUE(WaitUntil([&]() { return SampleCalls.load(std::memory_order_relaxed) > CallsAfterDisable; }));
	Sampler.Stop();
}

TEST(QmMonitoringPerfContract, DiskReadRateUsesMegabytesPerSecond)
{
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(0, 0, 1024 * 1024, 1000000000ull), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(0, 1000000000ull, 1024 * 1024, 2000000000ull), 1.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(1024, 2000000000ull, 1024, 3000000000ull), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(2048, 5000000000ull, 1024, 4000000000ull), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(1024, 1000000000ull, 2048, 1000000000ull), -1.0f);
}

TEST(QmMonitoringPerfContract, PerfDurationGateUsesConfiguredThreshold)
{
	const int OldThreshold = g_Config.m_QmPerfDebugThresholdMs;
	const int OldStutterDiagnostics = g_Config.m_QmPerfStutterDiagnostics;
	g_Config.m_QmPerfDebugThresholdMs = 4;
	g_Config.m_QmPerfStutterDiagnostics = 0;

	EXPECT_FALSE(QmPerfShouldLogDuration(3.999));
	EXPECT_TRUE(QmPerfShouldLogDuration(4.0));
	EXPECT_TRUE(QmPerfShouldLogDuration(0.0, true));

	g_Config.m_QmPerfDebugThresholdMs = OldThreshold;
	g_Config.m_QmPerfStutterDiagnostics = OldStutterDiagnostics;
}

TEST(QmStutterDiagnostics, PerfDurationGateUsesThreeHundredFpsBudgetWhileEnabled)
{
	const int OldThreshold = g_Config.m_QmPerfDebugThresholdMs;
	const int OldStutterDiagnostics = g_Config.m_QmPerfStutterDiagnostics;
	g_Config.m_QmPerfDebugThresholdMs = 4;
	g_Config.m_QmPerfStutterDiagnostics = 1;

	EXPECT_FALSE(QmPerfShouldLogDuration(QmStutterFrameBudgetMs() - 0.001));
	EXPECT_TRUE(QmPerfShouldLogDuration(QmStutterFrameBudgetMs()));

	g_Config.m_QmPerfDebugThresholdMs = OldThreshold;
	g_Config.m_QmPerfStutterDiagnostics = OldStutterDiagnostics;
}

TEST(QmMonitoringPerfContract, PerfPayloadJsonFieldsPreserveSpaceContainingValues)
{
	char aJson[1024];
	bool First = true;
	str_copy(aJson, "{", sizeof(aJson));
	QmPerfAppendPayloadJsonFields(aJson, sizeof(aJson), First, "event=source_request skin=My Skin Name priority=visible first_visible_skin=Another Skin");
	str_append(aJson, "}", sizeof(aJson));

	EXPECT_NE(str_find(aJson, "\"skin\":\"My Skin Name\""), nullptr);
	EXPECT_NE(str_find(aJson, "\"priority\":\"visible\""), nullptr);
	EXPECT_NE(str_find(aJson, "\"first_visible_skin\":\"Another Skin\""), nullptr);
}

TEST(QmMonitoringPerfContract, PerfPayloadJsonFieldsAppendAfterExistingFields)
{
	char aJson[256];
	bool First = true;
	str_copy(aJson, "{", sizeof(aJson));
	QmPerfAppendJsonField(aJson, sizeof(aJson), First, "system", "perf/test");
	QmPerfAppendPayloadJsonFields(aJson, sizeof(aJson), First, "event=source_request count=12 skin=\"My Skin\"");
	str_append(aJson, "}", sizeof(aJson));

	EXPECT_STREQ(aJson, "{\"system\":\"perf/test\",\"event\":\"source_request\",\"count\":12,\"skin\":\"My Skin\"}");
}

TEST(QmMonitoringPerfContract, PerfPayloadJsonFieldsDoNotWritePastFullBuffer)
{
	char aJson[sizeof("{\"a\":\"123456\"}")];
	bool First = true;
	str_copy(aJson, "{\"a\":\"123456\"}", sizeof(aJson));
	QmPerfAppendPayloadJsonFields(aJson, sizeof(aJson), First, "event=long_value next=1");

	EXPECT_STREQ(aJson, "{\"a\":\"123456\"}");
}

TEST(QmMonitoringRuntimeContract, SettingsResourcePreviewSchedulerZeroBudgetDoesNotAdmitWork)
{
	CSettingsResourcePreviewScheduler Scheduler;
	Scheduler.BeginFrame(0, 0, 0, 0);

	EXPECT_FALSE(Scheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::VISIBLE));
	EXPECT_FALSE(Scheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::NEAR_VISIBLE));
	EXPECT_FALSE(Scheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::BACKGROUND));
	EXPECT_FALSE(Scheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::VISIBLE));
	EXPECT_FALSE(Scheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::NEAR_VISIBLE));
	EXPECT_FALSE(Scheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::BACKGROUND));
	EXPECT_FALSE(Scheduler.CanUploadPreview());
}

TEST(QmMonitoringRuntimeContract, SettingsResourcePreviewShellOnlyBlocksPreviewJobsAndUploads)
{
	CSettingsResourcePreviewScheduler Scheduler;
	Scheduler.BeginFrame(2, 2, 2, 2);
	Scheduler.SetShellOnlyFrame(true);

	EXPECT_TRUE(Scheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::VISIBLE));
	EXPECT_FALSE(Scheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::NEAR_VISIBLE));
	EXPECT_FALSE(Scheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::BACKGROUND));
	EXPECT_FALSE(Scheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::VISIBLE));
	EXPECT_FALSE(Scheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::NEAR_VISIBLE));
	EXPECT_FALSE(Scheduler.CanStartPreviewJob(ESettingsResourcePreviewPriority::BACKGROUND));
	EXPECT_FALSE(Scheduler.CanUploadPreview());
}

TEST(QmMonitoringRuntimeContract, SettingsResourcePreviewUploadBudgetUsesSharedLimiterBeforeLocalBudget)
{
	CGpuUploadLimiter Limiter;
	Limiter.OnFrameStart(0);
	SSettingsResourceMergeBudget MergeBudget;
	MergeBudget.m_MaxGpuUploads = 1;
	SResourcePreviewUploadBudget PreviewBudget;
	PreviewBudget.m_MaxUploads = 1;
	PreviewBudget.m_pMergeBudget = &MergeBudget;
	PreviewBudget.m_pGpuUploadLimiter = &Limiter;

	EXPECT_FALSE(SettingsResourcePreviewConsumeUploadBudget(PreviewBudget));
	EXPECT_EQ(PreviewBudget.m_UploadsUsed, 0);
	EXPECT_EQ(MergeBudget.m_MaxGpuUploads, 1);
	EXPECT_EQ(Limiter.UploadsThisFrame(), 0);

	Limiter.OnFrameStart(1);
	EXPECT_TRUE(SettingsResourcePreviewConsumeUploadBudget(PreviewBudget));
	EXPECT_EQ(PreviewBudget.m_UploadsUsed, 1);
	EXPECT_EQ(MergeBudget.m_MaxGpuUploads, 0);
	SettingsResourcePreviewCommitUploadBudget(PreviewBudget);
	EXPECT_EQ(Limiter.UploadsThisFrame(), 1);
}

TEST(QmMonitoringRuntimeContract, SettingsResourcePreviewRejectsInvalidUploadImagesBeforeBudget)
{
	uint8_t aPixel[4] = {255, 255, 255, 255};

	CImageInfo MissingData;
	MissingData.m_Width = 1;
	MissingData.m_Height = 1;
	MissingData.m_Format = CImageInfo::FORMAT_RGBA;
	EXPECT_FALSE(SettingsResourcePreviewImageValidForUpload(MissingData));

	CImageInfo ZeroWidth;
	ZeroWidth.m_Width = 0;
	ZeroWidth.m_Height = 1;
	ZeroWidth.m_Format = CImageInfo::FORMAT_RGBA;
	ZeroWidth.m_pData = aPixel;
	EXPECT_FALSE(SettingsResourcePreviewImageValidForUpload(ZeroWidth));

	CImageInfo ZeroHeight;
	ZeroHeight.m_Width = 1;
	ZeroHeight.m_Height = 0;
	ZeroHeight.m_Format = CImageInfo::FORMAT_RGBA;
	ZeroHeight.m_pData = aPixel;
	EXPECT_FALSE(SettingsResourcePreviewImageValidForUpload(ZeroHeight));

	CImageInfo ValidImage;
	ValidImage.m_Width = 1;
	ValidImage.m_Height = 1;
	ValidImage.m_Format = CImageInfo::FORMAT_RGBA;
	ValidImage.m_pData = aPixel;
	EXPECT_TRUE(SettingsResourcePreviewImageValidForUpload(ValidImage));
}

TEST(QmMonitoringLayoutContract, ScrollRegionNoScrollSliderReleasesActiveAfterMouseUp)
{
	EXPECT_FALSE(ScrollRegionShouldKeepNoScrollSliderActive(false, false));
	EXPECT_FALSE(ScrollRegionShouldKeepNoScrollSliderActive(false, true));
	EXPECT_FALSE(ScrollRegionShouldKeepNoScrollSliderActive(true, false));
	EXPECT_TRUE(ScrollRegionShouldKeepNoScrollSliderActive(true, true));
}

TEST(QmMonitoringRuntimeContract, FrameSchedulerServiceProducesRealTokensAndIsolatesConsumers)
{
	std::unique_ptr<IFrameScheduler, void (*)(IFrameScheduler *)> Scheduler(CreateFrameScheduler(), [](IFrameScheduler *p) { delete p; });
	ASSERT_NE(Scheduler, nullptr);

	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 5.0f;
	Input.m_FrameMsP95 = 6.0f;

	const SSettingsAdaptiveBudgetOutput IngameOutput = Scheduler->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, Input);
	EXPECT_GE(IngameOutput.m_VisibleTokens, 1);
	EXPECT_GE(IngameOutput.m_TextContainerTokens, 1);
	EXPECT_EQ(IngameOutput.m_Mode, ESettingsAdaptiveBudgetMode::IDLE);

	const SSettingsAdaptiveBudgetOutput &IngameLast = Scheduler->LastOutput(EFrameSchedulerConsumer::IngameServerInfo);
	EXPECT_EQ(IngameLast.m_VisibleTokens, IngameOutput.m_VisibleTokens);
	EXPECT_EQ(IngameLast.m_TextContainerTokens, IngameOutput.m_TextContainerTokens);

	const SSettingsAdaptiveBudgetOutput SettingsLast = Scheduler->LastOutput(EFrameSchedulerConsumer::SettingsText);
	EXPECT_EQ(SettingsLast.m_VisibleTokens, 0);
	EXPECT_EQ(SettingsLast.m_TextContainerTokens, 0);

	Input.m_FrameMsAverage = 30.0f;
	Input.m_FrameMsP95 = 40.0f;
	const SSettingsAdaptiveBudgetOutput PressuredOutput = Scheduler->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, Input);
	EXPECT_EQ(PressuredOutput.m_Mode, ESettingsAdaptiveBudgetMode::FRAME_PRESSURE);
}

TEST(QmMonitoringRuntimeContract, FrameSchedulerResetClearsConsumerStateAndFrameScope)
{
	std::unique_ptr<IFrameScheduler, void (*)(IFrameScheduler *)> Scheduler(CreateFrameScheduler(), [](IFrameScheduler *p) { delete p; });
	ASSERT_NE(Scheduler, nullptr);

	Scheduler->BeginFrame(42);

	SSettingsAdaptiveBudgetInput Input;
	Input.m_WindowActive = true;
	Input.m_TargetFrameMs = 8.333f;
	Input.m_FrameMsAverage = 5.0f;
	Input.m_FrameMsP95 = 6.0f;

	const SSettingsAdaptiveBudgetOutput Output = Scheduler->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, Input);
	ASSERT_GT(Output.m_TextContainerTokens, 0);
	ASSERT_GT(Scheduler->LastOutput(EFrameSchedulerConsumer::IngameServerInfo).m_TextContainerTokens, 0);
	ASSERT_EQ(Scheduler->CurrentFrameId(), 42);

	Scheduler->Reset();

	EXPECT_EQ(Scheduler->CurrentFrameId(), 0);
	for(size_t i = 0; i < FRAME_SCHEDULER_CONSUMER_COUNT; ++i)
	{
		const EFrameSchedulerConsumer Consumer = static_cast<EFrameSchedulerConsumer>(i);
		EXPECT_FALSE(Scheduler->State(Consumer).m_Initialized);
		EXPECT_EQ(Scheduler->State(Consumer).m_HealthyFrames, 0);
		EXPECT_EQ(Scheduler->State(Consumer).m_BackgroundWindow, 1);
		EXPECT_EQ(Scheduler->LastOutput(Consumer).m_VisibleTokens, 0);
		EXPECT_EQ(Scheduler->LastOutput(Consumer).m_TextContainerTokens, 0);
	}
}

TEST(QmMonitoringRuntimeContract, MenuUiPerfTreatsImmediateWheelConsumptionAsActiveScroll)
{
	EXPECT_TRUE(QmMenuUiScrollPerfActive(true, false, false));
	EXPECT_TRUE(QmMenuUiScrollPerfActive(false, true, false));
	EXPECT_TRUE(QmMenuUiScrollPerfActive(false, false, true));
	EXPECT_FALSE(QmMenuUiScrollPerfActive(false, false, false));
}
