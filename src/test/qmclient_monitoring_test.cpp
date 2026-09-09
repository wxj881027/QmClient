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
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace
{

	std::string ReadRepoFile(const char *pPath)
	{
		return ReadTestSourceFile(pPath);
	}

	bool ContainsAll(const std::string &Source, std::initializer_list<const char *> Needles)
	{
		for(const char *pNeedle : Needles)
		{
			if(Source.find(pNeedle) == std::string::npos)
				return false;
		}
		return true;
	}

	bool ContainsAny(const std::string &Source, std::initializer_list<const char *> Needles)
	{
		for(const char *pNeedle : Needles)
		{
			if(Source.find(pNeedle) != std::string::npos)
				return true;
		}
		return false;
	}

	std::string Trim(const std::string &Text)
	{
		const size_t Begin = Text.find_first_not_of(" \t\r\n");
		if(Begin == std::string::npos)
			return {};

		const size_t End = Text.find_last_not_of(" \t\r\n");
		return Text.substr(Begin, End - Begin + 1);
	}

	std::vector<std::string> SplitLines(const std::string &Source)
	{
		std::vector<std::string> vLines;
		std::stringstream Stream(Source);
		std::string Line;
		while(std::getline(Stream, Line))
			vLines.push_back(Line);
		return vLines;
	}

	void AppendUniqueCandidate(std::vector<std::string> &vCandidates, const std::string &Candidate)
	{
		if(Candidate.empty())
			return;

		for(const std::string &Existing : vCandidates)
		{
			if(Existing == Candidate)
				return;
		}
		vCandidates.push_back(Candidate);
	}

	std::vector<std::string> ExtractQuotedCandidates(const std::string &Line)
	{
		std::vector<std::string> vFound;
		size_t Pos = 0;
		while((Pos = Line.find('"', Pos)) != std::string::npos)
		{
			const size_t End = Line.find('"', Pos + 1);
			if(End == std::string::npos)
				break;
			AppendUniqueCandidate(vFound, Line.substr(Pos + 1, End - Pos - 1));
			Pos = End + 1;
		}
		return vFound;
	}

	std::string ExtractVariableAssignment(const std::string &Line)
	{
		for(const char *pVar : {"pTitle", "pText", "pLabel"})
		{
			const size_t VarPos = Line.find(pVar);
			if(VarPos == std::string::npos)
				continue;

			const size_t EqualPos = Line.find('=', VarPos + 1);
			if(EqualPos == std::string::npos)
				continue;

			return Trim(Line.substr(EqualPos + 1));
		}
		return {};
	}

	[[maybe_unused]] std::vector<std::string> CollectStableTextCandidates(const std::string &Source)
	{
		const std::vector<std::string> vLines = SplitLines(Source);
		std::vector<std::string> vCandidates;
		for(size_t LineIndex = 0; LineIndex < vLines.size(); ++LineIndex)
		{
			const std::string &Line = vLines[LineIndex];
			if(!ContainsAny(Line,
				   {
					   "Ui()->DoLabel(",
					   "DoButton_Menu(",
					   "DoButton_CheckBox",
					   "DoButton_CheckBoxAutoVMarginAndSet",
					   "Ui()->DoScrollbarOption",
					   "DoSettingsLabel",
					   "DoSettingsMenuLabel",
					   "DoSettingsButton_",
					   "DoSettingsScrollbarOption",
					   "SettingsTextElement",
					   "QmNewFeatureLabel",
					   "RainbowColor",
					   "DoModuleHeadline",
					   "pTitle",
					   "pText",
					   "pLabel",
				   }))
			{
				continue;
			}

			for(const std::string &Quoted : ExtractQuotedCandidates(Line))
				AppendUniqueCandidate(vCandidates, Quoted);

			const size_t BlockBegin = LineIndex > 2 ? LineIndex - 2 : 0;
			const size_t BlockEnd = std::min(LineIndex + 3, vLines.size());
			for(size_t NearbyLine = BlockBegin; NearbyLine < BlockEnd; ++NearbyLine)
			{
				for(const std::string &Quoted : ExtractQuotedCandidates(vLines[NearbyLine]))
					AppendUniqueCandidate(vCandidates, Quoted);

				const std::string Assignment = ExtractVariableAssignment(vLines[NearbyLine]);
				if(!Assignment.empty())
					AppendUniqueCandidate(vCandidates, Assignment);
			}

			if(Line.find("QmNewFeatureLabel") != std::string::npos)
				AppendUniqueCandidate(vCandidates, "QmNewFeatureLabel");
			if(Line.find("RainbowColor") != std::string::npos)
				AppendUniqueCandidate(vCandidates, "RainbowColor");
			if(Line.find("DoModuleHeadline") != std::string::npos)
				AppendUniqueCandidate(vCandidates, "DoModuleHeadline");
		}
		return vCandidates;
	}

	struct SStableTextCandidate
	{
		int m_Line = 0;
		std::string m_Text;
		std::string m_SourceLine;
	};

	struct SStableTextRawAllow
	{
		const char *m_pFile;
		int m_Line;
		const char *m_pReason;
	};

	bool IsStableTextAllowedReason(const char *pReason)
	{
		for(const char *pAllowed : {
			    "dynamic-value",
			    "user-generated",
			    "localized-list-data",
			    "localized-setting-label",
			    "stateful-new-label",
			    "animated-style",
			    "icon-only",
			    "status-message",
			    "search-result",
			    "input-text",
		    })
		{
			if(str_comp(pReason, pAllowed) == 0)
				return true;
		}
		return false;
	}

	bool IsDynamicStableTextCandidateLine(const std::string &Line)
	{
		return ContainsAny(Line, {
						 "aBuf",
						 "pSkinContainer->Name()",
						 "Language.m_Name",
						 "m_aStatusMessage",
						 "FONT_ICON_",
						 "pValue",
						 "DoEditBox",
						 "Input",
						 "Search",
						 "m_aName",
						 "m_aClan",
						 "Profile.",
						 "Client.",
						 "pEntry->",
						 "pType->",
						 "pVar->",
						 "SourceName(",
						 "pText,",
						 "pLabel,",
						 "pTitle,",
					 });
	}

	bool IsStableTextCandidatePayloadIgnored(const std::string &Text)
	{
		return Text == "%" || Text == "ms" || Text == "ms (off)" || Text == "s" || Text == " min" || Text == " seconds" || Text == " seconds (never)" || Text == "X" || Text == "RainbowColor";
	}

	bool IsPooledStableTextLine(const std::string &Line)
	{
		return ContainsAny(Line, {
						 "DoSettingsMenuLabel(",
						 "DoSettingsButton_Menu(",
						 "DoSettingsButton_CheckBox(",
						 "DoSettingsButton_CheckBoxAutoVMarginAndSet(",
						 "DoButton_Menu(",
						 "DoButton_CheckBox(",
						 "DoButton_CheckBox_Common(",
						 "DoButton_CheckBoxAutoVMarginAndSet(",
						 "DoSettingsScrollbarOption(",
						 "SettingsTextElement(",
						 "DoSettingsLabel(",
						 "DoQmSettingsLabel(",
						 "DoQmSettingsCheckbox(",
						 "DoQmSettingsMenuButton(",
					 });
	}

	std::vector<SStableTextCandidate> CollectRawStableTextCandidatesWithLines(const std::string &Source)
	{
		const std::vector<std::string> vLines = SplitLines(Source);
		std::vector<SStableTextCandidate> vCandidates;
		for(size_t LineIndex = 0; LineIndex < vLines.size(); ++LineIndex)
		{
			const std::string TrimmedLine = Trim(vLines[LineIndex]);
			if(TrimmedLine.empty() || TrimmedLine.rfind("//", 0) == 0)
				continue;
			if(TrimmedLine.find("TextRender()->TextColor(") != std::string::npos)
				continue;
			if(IsPooledStableTextLine(TrimmedLine) || IsDynamicStableTextCandidateLine(TrimmedLine))
				continue;
			if(!ContainsAny(TrimmedLine, {
							     "Ui()->DoLabel(",
							     "DoButton_Menu(",
							     "DoButton_CheckBox",
							     "Ui()->DoScrollbarOption",
							     "QmNewFeatureLabel",
							     "RainbowColor",
							     "DoModuleHeadline",
						     }))
			{
				continue;
			}

			std::vector<std::string> vTexts = ExtractQuotedCandidates(TrimmedLine);
			if(TrimmedLine.find("QmNewFeatureLabel") != std::string::npos)
				AppendUniqueCandidate(vTexts, "QmNewFeatureLabel");
			if(TrimmedLine.find("RainbowColor") != std::string::npos)
				AppendUniqueCandidate(vTexts, "RainbowColor");
			if(TrimmedLine.find("DoModuleHeadline") != std::string::npos)
				AppendUniqueCandidate(vTexts, "DoModuleHeadline");
			if(vTexts.empty() && ContainsAny(TrimmedLine, {"pTitle", "pText", "pLabel"}))
				AppendUniqueCandidate(vTexts, TrimmedLine);

			for(const std::string &Text : vTexts)
			{
				if(Text.empty() || IsStableTextCandidatePayloadIgnored(Text))
					continue;
				vCandidates.push_back({(int)LineIndex + 1, Text, TrimmedLine});
			}
		}
		return vCandidates;
	}

	bool IsStableTextCandidateAllowed(const SStableTextCandidate &Candidate, const char *pFile, const std::vector<SStableTextRawAllow> &vAllowlist)
	{
		for(const SStableTextRawAllow &Allow : vAllowlist)
		{
			EXPECT_TRUE(IsStableTextAllowedReason(Allow.m_pReason)) << Allow.m_pReason;
			if(str_comp(Allow.m_pFile, pFile) == 0 && Allow.m_Line == Candidate.m_Line)
				return true;
		}
		return false;
	}

	std::vector<SStableTextCandidate> FilterCandidatesNotCoveredByMenuPoolOrAllowlist(const char *pFile, const std::vector<SStableTextCandidate> &vCandidates, const std::vector<SStableTextRawAllow> &vAllowlist)
	{
		std::vector<SStableTextCandidate> vUnexpected;
		for(const SStableTextCandidate &Candidate : vCandidates)
		{
			if(!IsStableTextCandidateAllowed(Candidate, pFile, vAllowlist))
				vUnexpected.push_back(Candidate);
		}
		return vUnexpected;
	}

	std::string JoinCandidates(const std::vector<SStableTextCandidate> &vCandidates)
	{
		std::ostringstream Out;
		for(const SStableTextCandidate &Candidate : vCandidates)
			Out << "\nline " << Candidate.m_Line << ": " << Candidate.m_Text << " :: " << Candidate.m_SourceLine;
		return Out.str();
	}

	std::string ExtractSourceFunctionBody(const std::string &Source, const char *pSignature)
	{
		const size_t SignaturePos = Source.find(pSignature);
		if(SignaturePos == std::string::npos)
			return {};

		const size_t BodyStart = Source.find('{', SignaturePos);
		if(BodyStart == std::string::npos)
			return {};

		int Depth = 0;
		for(size_t i = BodyStart; i < Source.size(); ++i)
		{
			if(Source[i] == '{')
				++Depth;
			else if(Source[i] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, i - BodyStart + 1);
			}
		}
		return {};
	}

	std::string ExtractSourceBlock(const std::string &Source, const char *pBeginMarker, const char *pEndMarker)
	{
		const size_t Begin = Source.find(pBeginMarker);
		if(Begin == std::string::npos)
			return {};
		const size_t End = Source.find(pEndMarker, Begin);
		if(End == std::string::npos)
			return Source.substr(Begin);
		return Source.substr(Begin, End - Begin);
	}

	size_t CountSubstring(const std::string &Haystack, const std::string &Needle)
	{
		if(Needle.empty())
			return 0;

		size_t Count = 0;
		size_t Pos = 0;
		while((Pos = Haystack.find(Needle, Pos)) != std::string::npos)
		{
			++Count;
			Pos += Needle.size();
		}
		return Count;
	}

} // namespace

TEST(QmMonitoringHelpers, ConnectionGradeTracksDisconnectedState)
{
	SQmNetworkMetrics Net;
	Net.m_Connected = false;
	EXPECT_EQ(QmDetermineConnectionGrade(Net), EQmConnectionGrade::DISCONNECTED);
}

TEST(QmMonitoringHelpers, ConnectionGradeUsesThresholdTable)
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

TEST(QmMonitoringHelpers, PrimaryCausePrefersDominantMetric)
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

TEST(QmMonitoringHelpers, RollbackAmountUsesNegativeGameTimeMargin)
{
	EXPECT_FLOAT_EQ(QmComputeRollbackMs(-18.0f), 18.0f);
	EXPECT_FLOAT_EQ(QmComputeRollbackMs(6.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeRollbackMs(std::numeric_limits<float>::quiet_NaN()), -1.0f);
}

TEST(QmMonitoringHelpers, PeakSelectionPrefersLatestMatchingPeak)
{
	std::array<float, 8> aHistory = {41.0f, 39.0f, 41.0f, 24.0f, 24.0f, 18.0f, 24.0f, 17.0f};
	EXPECT_EQ(QmFindLatestPeakIndex(aHistory, 0, 8), 2);
	EXPECT_EQ(QmFindLatestAbsolutePeakIndex(aHistory, 0, 8), 2);
}

TEST(QmMonitoringHelpers, SignedPeakSelectionUsesLatestAbsolutePeak)
{
	std::array<float, 8> aHistory = {-7.0f, 5.0f, -9.0f, 3.0f, 9.0f, 4.0f, 8.0f, 2.0f};
	EXPECT_EQ(QmFindLatestAbsolutePeakIndex(aHistory, 0, 8), 4);
}

TEST(QmMonitoringHelpers, UiScaleGrowsOnHighResolutionScreens)
{
	EXPECT_FLOAT_EQ(QmComputeMonitoringUiScale(800.0f, 600.0f), 0.65f);
	const float Expected1600x900 = std::sqrt((1600.0f / 1920.0f) * (900.0f / 1080.0f));
	EXPECT_FLOAT_EQ(QmComputeMonitoringUiScale(1600.0f, 900.0f), Expected1600x900);
	EXPECT_FLOAT_EQ(QmComputeMonitoringUiScale(3840.0f, 2160.0f), 1.8f);
}

TEST(QmMonitoringHelpers, PanelOpacityClampsPercentToUnitRange)
{
	EXPECT_FLOAT_EQ(QmComputeMonitoringPanelOpacity(-20), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeMonitoringPanelOpacity(35), 0.35f);
	EXPECT_FLOAT_EQ(QmComputeMonitoringPanelOpacity(140), 1.0f);
}

TEST(QmMonitoringHelpers, ProcessCpuUsageNormalizesAcrossLogicalCpus)
{
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(-1.0f, 8), -1.0f);
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(114.0f, 8), 14.25f);
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(1600.0f, 16), 100.0f);
	EXPECT_FLOAT_EQ(QmNormalizeProcessCpuUsagePct(114.0f, 0), 100.0f);
}

TEST(QmMonitoringHelpers, TotalCpuUsageComputesBusyDelta)
{
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 125, 1100), 75.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 200, 1100), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 100, 1100), 100.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 0, 125, 1100), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 90, 1100), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeTotalCpuUsagePct(100, 1000, 125, 990), -1.0f);
}

TEST(QmMonitoringHelpers, CpuRatioValueShowsProcessAndTotalCpu)
{
	char aBuf[32];
	FormatCpuRatioValue(aBuf, sizeof(aBuf), -1.0f, 35.0f);
	EXPECT_STREQ(aBuf, "--");
	FormatCpuRatioValue(aBuf, sizeof(aBuf), 12.4f, -1.0f);
	EXPECT_STREQ(aBuf, "12%");
	FormatCpuRatioValue(aBuf, sizeof(aBuf), 12.4f, 35.6f);
	EXPECT_STREQ(aBuf, "12%/36%");
}

TEST(QmMonitoringHelpers, TrafficStatsMatchOfficialDebugMath)
{
	const auto Stats = QmComputeTrafficStats(10, 1000, 14, 1320, 1.0f);
	EXPECT_EQ(Stats.m_Packets, 4u);
	EXPECT_EQ(Stats.m_PayloadBytes, 320u);
	EXPECT_EQ(Stats.m_OverheadBytes, 168u);
	EXPECT_EQ(Stats.m_TotalBytes, 488u);
	EXPECT_EQ(Stats.m_AveragePayloadBytes, 80u);
	EXPECT_FLOAT_EQ(Stats.m_RateKibPerSec, 0.4765625f);
}

TEST(QmMonitoringHelpers, CounterRateRejectsResetAndMissingWindow)
{
	EXPECT_FLOAT_EQ(QmComputeCounterRate(100, 160, 2.0f), 30.0f);
	EXPECT_FLOAT_EQ(QmComputeCounterRate(160, 100, 2.0f), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeCounterRate(100, 160, 0.0f), -1.0f);
}

TEST(QmMonitoringHelpers, GamePingProbeOnlyAcceptsCurrentUuid)
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

TEST(QmMonitoringHelpers, GamePingProbeRejectsLatePongAfterTimeoutAndReplacement)
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

TEST(QmMonitoringHelpers, ManualPingProbeRejectsOverlappingRequests)
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

TEST(QmMonitoringHelpers, ManualPingProbeRecoversAfterTimeout)
{
	SManualPingProbe Probe;
	EXPECT_TRUE(Probe.Begin(10000));
	EXPECT_FALSE(Probe.HandleTimeout(11999, 1000));
	EXPECT_TRUE(Probe.HandleTimeout(12000, 1000));
	EXPECT_EQ(Probe.m_StartTime, -1);
	EXPECT_TRUE(Probe.Begin(13000));
}

TEST(QmMonitoringHelpers, LegacyGamePingProbeAcceptsLegacyReply)
{
	SGamePingProbe Probe;
	Probe.BeginLegacy(10000, 1000);
	EXPECT_TRUE(Probe.m_Legacy);
	EXPECT_TRUE(Probe.HandleLegacyPong(10025, 1000));
	EXPECT_EQ(Probe.m_RttMs, 25);
	EXPECT_FALSE(Probe.m_Legacy);
}

TEST(QmMonitoringHelpers, LegacyManualPingSharesAutomaticProbeAndReply)
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

TEST(QmMonitoringHelpers, AutomaticAndManualPingPathsCoordinateWithExplicitLegacySharing)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	const std::string PingMs = ExtractSourceFunctionBody(Client, "float CClient::PingMs() const");
	const std::string ManualPing = ExtractSourceFunctionBody(Client, "void CClient::Con_Ping(IConsole::IResult *pResult, void *pUserData)");
	const std::string ProcessPacket = ExtractSourceFunctionBody(Client, "void CClient::ProcessServerPacket(CNetChunk *pPacket, int Conn, bool Dummy)");
	const std::string PingReply = ExtractSourceBlock(ProcessPacket, "else if(Msg == NETMSG_PING_REPLY)", "else if(Msg == NETMSG_INPUTTIMING)");

	ASSERT_FALSE(AutomaticPing.empty());
	ASSERT_FALSE(PingMs.empty());
	ASSERT_FALSE(ManualPing.empty());
	ASSERT_FALSE(PingReply.empty());
	EXPECT_NE(AutomaticPing.find("NETMSG_PINGEX"), std::string::npos);
	EXPECT_NE(AutomaticPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_EQ(PingMs.find("!m_ServerCapabilities.m_PingEx"), std::string::npos);
	EXPECT_NE(ManualPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_NE(ManualPing.find("m_aGamePingProbes"), std::string::npos);
	EXPECT_NE(ManualPing.find("m_ManualPingProbe.Begin"), std::string::npos);
	EXPECT_NE(PingReply.find("m_ManualPingProbe.HandlePong"), std::string::npos);
	EXPECT_NE(PingReply.find("m_aGamePingProbes"), std::string::npos);
	EXPECT_EQ(Client.find("m_aGamePingIgnoreNextReply"), std::string::npos);
	EXPECT_EQ(Client.find("m_PingStartTime"), std::string::npos);
}

TEST(QmMonitoringHelpers, LegacyPingAndRespawnCancelPathsRemainPresent)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string Controls = ReadRepoFile("src/game/client/components/controls.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	const std::string Respawn = ExtractSourceFunctionBody(Controls, "void CControls::OnRender()");
	ASSERT_FALSE(AutomaticPing.empty());
	ASSERT_FALSE(Respawn.empty());
	EXPECT_NE(AutomaticPing.find("BeginLegacy"), std::string::npos);
	EXPECT_NE(AutomaticPing.find("NETMSG_PING, true"), std::string::npos);
	EXPECT_NE(Respawn.find("用户主动选择了其他武器"), std::string::npos);
	EXPECT_NE(Respawn.find("else"), std::string::npos);
}

TEST(QmMonitoringHelpers, ManualPingTimeoutIsCheckedBeforeKcpEarlyContinue)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string AutomaticPing = ExtractSourceFunctionBody(Client, "void CClient::UpdateGamePing()");
	ASSERT_FALSE(AutomaticPing.empty());
	const size_t Timeout = AutomaticPing.find("m_ManualPingProbe.HandleTimeout");
	const size_t KcpBranch = AutomaticPing.find("m_aNetClient[Conn].IsKcpActive()");
	ASSERT_NE(Timeout, std::string::npos);
	ASSERT_NE(KcpBranch, std::string::npos);
	EXPECT_LT(Timeout, KcpBranch);
}

TEST(QmMonitoringHelpers, DefaultExtrasFailureClearsPublishedState)
{
	const std::string GameClient = ReadRepoFile("src/game/client/gameclient.cpp");
	const std::string LoadExtras = ExtractSourceFunctionBody(GameClient, "void CGameClient::LoadExtrasSkin(const char *pPath, bool AsDir)");
	ASSERT_FALSE(LoadExtras.empty());
	const size_t CandidateFailure = LoadExtras.find("UnloadExtrasSkin(Candidate);");
	const size_t DefaultFailure = LoadExtras.find("if(IsDefault)", CandidateFailure);
	ASSERT_NE(CandidateFailure, std::string::npos);
	ASSERT_NE(DefaultFailure, std::string::npos);
	EXPECT_NE(LoadExtras.find("m_ExtrasSkin = {};", DefaultFailure), std::string::npos);
	EXPECT_NE(LoadExtras.find("m_ExtrasSkinLoaded = false;", DefaultFailure), std::string::npos);
}

TEST(QmMonitoringHelpers, FormattersRejectNonFiniteMetrics)
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

TEST(QmMonitoringHelpers, HistoryPercentileUsesRecentSamples)
{
	const std::array<float, 4> aHistory = {10.0f, 20.0f, 30.0f, 40.0f};
	EXPECT_FLOAT_EQ(QmComputeHistoryPercentile(aHistory, 0, 4, 50.0f), 30.0f);
	EXPECT_FLOAT_EQ(QmComputeHistoryPercentile(aHistory, 0, 4, 95.0f), 40.0f);
	EXPECT_FLOAT_EQ(QmComputeHistoryPercentile(aHistory, 0, 0, 95.0f), -1.0f);
}

TEST(QmMonitoringHelpers, HistoryStatsIgnoreUnavailableSamples)
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

TEST(QmMonitoringHelpers, SnapshotCountRequiresValidatedSnapshotStorageInsert)
{
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string Body = ExtractSourceFunctionBody(Client, "void CClient::ProcessServerPacket(CNetChunk *pPacket, int Conn, bool Dummy)");
	ASSERT_FALSE(Body.empty());

	const size_t CrcValidation = Body.find("TmpBuffer3.AsSnapshot()->Crc() != Crc");
	const size_t AltSnapshotValidation = Body.find("if(AltSnapSize < 0)");
	const size_t PartCount = Body.find("m_aSnapshotStats[Conn].m_PartCount++");
	const size_t StorageInsert = Body.find("m_aSnapshotStorage[Conn].Add(");
	const size_t SnapshotCount = Body.find("SnapshotStats.m_SnapshotCount++");
	ASSERT_NE(CrcValidation, std::string::npos);
	ASSERT_NE(AltSnapshotValidation, std::string::npos);
	ASSERT_NE(PartCount, std::string::npos);
	ASSERT_NE(StorageInsert, std::string::npos);
	ASSERT_NE(SnapshotCount, std::string::npos);

	EXPECT_LT(Body.find("if(Unpacker.Error() || NumParts < 1"), PartCount);
	EXPECT_LT(PartCount, Body.find("// Check m_aAckGameTick"));
	EXPECT_LT(CrcValidation, StorageInsert);
	EXPECT_LT(AltSnapshotValidation, StorageInsert);
	EXPECT_LT(StorageInsert, SnapshotCount);
}

TEST(QmMonitoringHelpers, HudLayoutPlacesPanelLeftOfGraphColumn)
{
	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(1600.0f, 900.0f, 1184.0f, 16.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.w, 768.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.h, 594.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.x, 400.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.y, 32.0f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.x, 410.0f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.y, 42.0f);
}

TEST(QmMonitoringHelpers, BodyLayoutPreservesMetricsBudgetOnCompactPanels)
{
	const SQmMonitoringBodyLayout Layout = QmComputeMonitoringBodyLayout(260.0f, 1.0f);
	EXPECT_NEAR(Layout.m_MainGraphHeight, 63.1f, 0.1f);
	EXPECT_NEAR(Layout.m_FpsGraphHeight, 39.8f, 0.1f);
	EXPECT_NEAR(Layout.m_PrimaryCardsHeight, 37.2f, 0.1f);
	EXPECT_NEAR(Layout.m_MetricsExtraHeight, 31.9f, 0.1f);
	EXPECT_GT(Layout.m_PrimaryCardsHeight, 30.0f);
}

TEST(QmMonitoringHelpers, HudLayoutUsesLargerPanelOn4kScreens)
{
	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(3840.0f, 2160.0f, 2842.0f, 38.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.w, 1843.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.h, 1405.0f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.x, 983.0f);
	EXPECT_FLOAT_EQ(Layout.m_ContentRect.y, 98.0f);
}

TEST(QmMonitoringHelpers, HudLayoutClampsPanelInsideScreenBounds)
{
	const SQmMonitoringHudLayout Layout = QmComputeMonitoringHudLayout(360.0f, 240.0f, 120.0f, 16.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.x, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_PanelRect.y, 0.0f);
	EXPECT_LE(Layout.m_PanelRect.x + Layout.m_PanelRect.w, 360.0f);
	EXPECT_LE(Layout.m_PanelRect.y + Layout.m_PanelRect.h, 240.0f);
}

TEST(QmMonitoringHelpers, DeviceMetricsDefaultToUnavailable)
{
	SQmPerformanceMetrics Perf;
	EXPECT_FALSE(Perf.m_DeviceSampleAvailable);
	EXPECT_FLOAT_EQ(Perf.m_GpuUtilPct, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_GpuDedicatedVramMb, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_GpuDedicatedVramBudgetMb, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_GpuSharedVramMb, -1.0f);
	EXPECT_FLOAT_EQ(Perf.m_DiskReadMbPerSec, -1.0f);
}

TEST(QmMonitoringHelpers, TeeSkinListFrameTelemetryExposesRowsFields)
{
	std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	EXPECT_NE(Source.find("event=list_frame page=settings:tee"), std::string::npos);
	EXPECT_NE(Source.find("rows_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d"), std::string::npos);
	EXPECT_NE(Source.find("first_visible_index=%d last_visible_index=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, JumpHintLegacyConfigMigratesAllFieldsToQmConfig)
{
	std::ifstream TClientFile(TestSourcePath("src/engine/shared/config_variables_tclient.h"));
	ASSERT_TRUE(TClientFile.good());
	std::stringstream TClientBuffer;
	TClientBuffer << TClientFile.rdbuf();
	const std::string TClientSource = TClientBuffer.str();

	std::ifstream QmFile(TestSourcePath("src/engine/shared/config_variables_qmclient.h"));
	ASSERT_TRUE(QmFile.good());
	std::stringstream QmBuffer;
	QmBuffer << QmFile.rdbuf();
	const std::string QmSource = QmBuffer.str();

	std::ifstream GameClientFile(TestSourcePath("src/game/client/gameclient.cpp"));
	ASSERT_TRUE(GameClientFile.good());
	std::stringstream GameClientBuffer;
	GameClientBuffer << GameClientFile.rdbuf();
	const std::string GameClientSource = GameClientBuffer.str();

	// The jump-hint UI moved from tc_jump_hint* to qm_jump_hint*. This test
	// guards the migration shape so we do not silently drop old users' enabled
	// state, color, position, or font size when only the text key is migrated.
	EXPECT_NE(TClientSource.find("MACRO_CONFIG_INT(TcJumpHintLegacy, tc_jump_hint,"), std::string::npos);
	EXPECT_NE(TClientSource.find("MACRO_CONFIG_STR(TcJumpHintTextLegacy, tc_jump_hint_text,"), std::string::npos);
	EXPECT_NE(TClientSource.find("MACRO_CONFIG_COL(TcJumpHintColorLegacy, tc_jump_hint_color,"), std::string::npos);
	EXPECT_NE(TClientSource.find("MACRO_CONFIG_INT(TcJumpHintXLegacy, tc_jump_hint_x,"), std::string::npos);
	EXPECT_NE(TClientSource.find("MACRO_CONFIG_INT(TcJumpHintYLegacy, tc_jump_hint_y,"), std::string::npos);
	EXPECT_NE(TClientSource.find("MACRO_CONFIG_INT(TcJumpHintSizeLegacy, tc_jump_hint_size,"), std::string::npos);
	// Legacy keys are read-only migration inputs. Keeping CFGFLAG_SAVE on them
	// would re-save old tc_jump_hint* values and let them overwrite qm_jump_hint*
	// again whenever the user resets the new value back to its default.
	EXPECT_EQ(TClientSource.find("TcJumpHintLegacy, tc_jump_hint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TcJumpHintTextLegacy, tc_jump_hint_text, 512, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TcJumpHintColorLegacy, tc_jump_hint_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TcJumpHintXLegacy, tc_jump_hint_x, 20, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TcJumpHintYLegacy, tc_jump_hint_y, 5, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TcJumpHintSizeLegacy, tc_jump_hint_size, 10, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmSource.find("MACRO_CONFIG_INT(QmJumpHint, qm_jump_hint,"), std::string::npos);
	EXPECT_NE(QmSource.find("MACRO_CONFIG_STR(QmJumpHintText, qm_jump_hint_text,"), std::string::npos);
	EXPECT_NE(QmSource.find("MACRO_CONFIG_COL(QmJumpHintColor, qm_jump_hint_color,"), std::string::npos);
	EXPECT_NE(QmSource.find("MACRO_CONFIG_INT(QmJumpHintX, qm_jump_hint_x,"), std::string::npos);
	EXPECT_NE(QmSource.find("MACRO_CONFIG_INT(QmJumpHintY, qm_jump_hint_y,"), std::string::npos);
	EXPECT_NE(QmSource.find("MACRO_CONFIG_INT(QmJumpHintSize, qm_jump_hint_size,"), std::string::npos);
	EXPECT_NE(GameClientSource.find("MigrateInt(g_Config.m_QmJumpHint, g_Config.m_TcJumpHintLegacy"), std::string::npos);
	EXPECT_NE(GameClientSource.find("MigrateStr(g_Config.m_QmJumpHintText, sizeof(g_Config.m_QmJumpHintText), g_Config.m_TcJumpHintTextLegacy"), std::string::npos);
	EXPECT_NE(GameClientSource.find("MigrateCol(g_Config.m_QmJumpHintColor, g_Config.m_TcJumpHintColorLegacy"), std::string::npos);
	EXPECT_NE(GameClientSource.find("MigrateInt(g_Config.m_QmJumpHintX, g_Config.m_TcJumpHintXLegacy"), std::string::npos);
	EXPECT_NE(GameClientSource.find("MigrateInt(g_Config.m_QmJumpHintY, g_Config.m_TcJumpHintYLegacy"), std::string::npos);
	EXPECT_NE(GameClientSource.find("MigrateInt(g_Config.m_QmJumpHintSize, g_Config.m_TcJumpHintSizeLegacy"), std::string::npos);
}

TEST(QmMonitoringHelpers, JumpHintSettingsCardExposesAllQmFields)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionJumpHintContent(");
	ASSERT_FALSE(Body.empty());

	// Jump hint migrated from tc_jump_hint* to qm_jump_hint*. The settings page
	// must expose the whole qm_* surface, not just the enable toggle, otherwise
	// users can render the HUD element but cannot adjust its color, position, or
	// size without using console commands.
	EXPECT_NE(Body.find("RenderQmFunctionCheckbox(&g_Config.m_QmJumpHint"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintColor"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintX"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintY"), std::string::npos);
	EXPECT_NE(Body.find("&g_Config.m_QmJumpHintSize"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmSettingsSliderWithValueInput(pInputId, ControlColumn, pValue"), std::string::npos);
	EXPECT_NE(Body.find("&s_QmJumpHintXInputId, &g_Config.m_QmJumpHintX"), std::string::npos);
	EXPECT_NE(Body.find("&s_QmJumpHintYInputId, &g_Config.m_QmJumpHintY"), std::string::npos);
	EXPECT_NE(Body.find("&s_QmJumpHintSizeInputId, &g_Config.m_QmJumpHintSize"), std::string::npos);
	EXPECT_EQ(Body.find("m_TcJumpHint"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmUiRuntimeTelemetryExposesSettingsContext)
{
	std::ifstream HeaderFile(TestSourcePath("src/game/client/QmUi/QmRt.h"));
	ASSERT_TRUE(HeaderFile.good());
	std::stringstream HeaderBuffer;
	HeaderBuffer << HeaderFile.rdbuf();
	const std::string Header = HeaderBuffer.str();

	std::ifstream SourceFile(TestSourcePath("src/game/client/QmUi/QmRt.cpp"));
	ASSERT_TRUE(SourceFile.good());
	std::stringstream SourceBuffer;
	SourceBuffer << SourceFile.rdbuf();
	const std::string Source = SourceBuffer.str();

	EXPECT_NE(Header.find("void SetPerfContext(const char *pPage, const char *pOperation);"), std::string::npos);
	EXPECT_EQ(Header.find("float m_LayoutMs = 0.0f;"), std::string::npos);
	EXPECT_NE(Header.find("int m_ActiveAnimCount = 0;"), std::string::npos);
	EXPECT_NE(Header.find("int m_QueuedAnimCount = 0;"), std::string::npos);
	EXPECT_NE(Source.find("active_anims=%d queued_anims=%d"), std::string::npos);
	EXPECT_EQ(Source.find("layout_ms=%.3f"), std::string::npos);
	EXPECT_NE(Source.find("QmPerfLogPayload(\"perf/ui_runtime\""), std::string::npos);
	EXPECT_NE(Source.find("QmPerfLogStage(\"perf/ui_runtime\", pStage, DurationMs, Force, pClient, pPage, nullptr, pExtra);"), std::string::npos);
	EXPECT_NE(Source.find("LogPerfStage(m_pGameClient->Client(), m_aPerfPage[0] != '\\0' ? m_aPerfPage : nullptr, \"ui_runtime_total\", RenderTimer.ElapsedMs(), false, aExtra);"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientPerfTelemetryUsesLiveClientContext)
{
	{
		std::ifstream File(TestSourcePath("src/game/client/components/qmclient/menus_qmclient.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("void LogQmPerfStage(IClient *pClient, const char *pStage, double DurationMs, bool Force = false, const char *pExtra = nullptr)"), std::string::npos);
		EXPECT_NE(Source.find("QmPerfLogStage(\"perf/qmclient\", pStage, DurationMs, Force, pClient, nullptr, nullptr, pExtra);"), std::string::npos);
		EXPECT_EQ(CountSubstring(Source, "LogQmPerfStage("), CountSubstring(Source, "LogQmPerfStage(Client(),") + 1);
		EXPECT_NE(Source.find("QmPerfLogPayload(\"perf/qmclient\", aPayload, Client(), CurrentQmUiPerfPage());"), std::string::npos);
		EXPECT_EQ(Source.find("QmPerfLogPayload(\"perf/qmclient\", aPayload);"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/gameclient.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("m_UiRuntimeV2.SetPerfContext(pPerfPage, m_Menus.CurrentQmUiPerfOperation());"), std::string::npos);
	}
}

TEST(QmMonitoringHelpers, SectionQuadBatchingDoesNotCrossTextOrClipBoundaries)
{
	{
		std::ifstream File(TestSourcePath("src/game/client/ui.h"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("class CUiScopedQuadBatch"), std::string::npos);
		EXPECT_NE(Source.find("void BeginQuadBatch() const;"), std::string::npos);
		EXPECT_NE(Source.find("void FlushQuadBatch() const;"), std::string::npos);
		EXPECT_NE(Source.find("void EndQuadBatch() const;"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/ui.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("CUiScopedQuadBatch::CUiScopedQuadBatch"), std::string::npos);
		EXPECT_NE(Source.find("CUiScopedQuadBatch::~CUiScopedQuadBatch"), std::string::npos);
		EXPECT_NE(Source.find("void CUi::FlushQuadBatch() const"), std::string::npos);
		EXPECT_EQ(Source.find("RenderQuadContainerAsSpriteMultiple(m_QuadBatchContainerIndex"), std::string::npos);
		EXPECT_NE(Source.find("TextRender()->RenderTextContainer"), std::string::npos);
		EXPECT_NE(Source.find("void CUi::RenderLabelTextContainerAligned"), std::string::npos);
		EXPECT_NE(Source.find("FlushQuadBatch();\n\tTextRender()->RenderTextContainer"), std::string::npos);
		EXPECT_NE(Source.find("void CUi::ClipEnable(const CUIRect *pRect)\n{\n\tFlushQuadBatch();"), std::string::npos);
		EXPECT_NE(Source.find("void CUi::ClipDisable()\n{\n\tFlushQuadBatch();"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/tclient/menus_tclient.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_EQ(Source.find("CUiScopedQuadBatch QuadBatchScope(Ui());"), std::string::npos);
	}
}

TEST(UiQuadBatch, PureColorFlushUsesUntexturedQuadContainerPath)
{
	std::ifstream File(TestSourcePath("src/game/client/ui.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();
	const std::string Body = ExtractSourceFunctionBody(Source, "void CUi::FlushQuadBatch() const");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("UiQuadBatchHasPendingSubmission"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->TextureClear();"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->SetColor(m_QuadBatchColor);"), std::string::npos);
	EXPECT_EQ(Body.find("RenderQuadContainerAsSpriteMultiple"), std::string::npos);
	EXPECT_NE(Body.find("m_vQuadBatchSprites"), std::string::npos);
	EXPECT_NE(Body.find("RenderQuadContainerEx(m_QuadBatchContainerIndex, 0, -1"), std::string::npos);
	EXPECT_NE(Body.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);"), std::string::npos);
	EXPECT_NE(Body.find("m_vQuadBatchSprites.clear();"), std::string::npos);
	EXPECT_NE(Body.find("m_QuadBatchContainerIndex = -1;"), std::string::npos);
}

TEST(QmMonitoringHelpers, UiQuadBatchFlushGuardsInvalidRectsAndKeepsSubmissionPlan)
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

TEST(QmMonitoringHelpers, SettingsPerfWindowAccumulatesFpsAndFrameStats)
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

TEST(QmMonitoringHelpers, SettingsPerfWindowLogsOnePercentLowFps)
{
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_perf_windows.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	EXPECT_NE(Header.find("m_FpsOnePctLow"), std::string::npos);
	EXPECT_EQ(Header.find("m_FpsOnePctLow = m_Summary.m_FrameMsP99 > 0.0f ? 1000.0f / m_Summary.m_FrameMsP99 : 0.0f;"), std::string::npos);
	EXPECT_NE(Header.find("OnePercentLowFpsFromFrameMs"), std::string::npos);
	EXPECT_NE(Menus.find("fps_1pct_low=%.3f"), std::string::npos);
	EXPECT_NE(Menus.find("fps_1pct_source=real_sampled"), std::string::npos);
	EXPECT_NE(Menus.find("window_start_frame=%"), std::string::npos);
	EXPECT_NE(Menus.find("window_end_frame=%"), std::string::npos);
	EXPECT_NE(Menus.find("Summary.m_FpsOnePctLow"), std::string::npos);
}

// Intent: catch regression of the stable-text key mismatch root cause.
// Bug: plan-collection replay uses MenuTextSettingsContentView(Screen), visible render uses real MainView;
// sub-pixel Rect.w differences × 0.1-pixel bucket granularity (round_to_int(width * 10)) produced 8424
// key mismatches in the 2026-06-19 fresh log (qm_perf_2026-06-19_21-51-10_summary.json).
// Fix: MaxWidthBucket uses 4-pixel granularity (round_to_int(width / 4.0f) * 4) to tolerate sub-pixel drift;
// FontSize and UiScaleBucket keep 0.1 granularity (those values are stable across plan vs visible paths).
TEST(QmMonitoringHelpers, SettingsTextStyleKeyUsesFourPixelMaxWidthBucket)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	const size_t AssignPos = Menus.find("m_MaxWidthBucket =");
	ASSERT_NE(AssignPos, std::string::npos) << "BuildMenuTextStyleKey must assign m_MaxWidthBucket";
	const std::string Tail = Menus.substr(AssignPos, 200);

	EXPECT_NE(Tail.find("round_to_int(MaxWidth / 4.0f) * 4"), std::string::npos)
		<< "MaxWidthBucket must use 4-pixel granularity";
	EXPECT_EQ(Tail.find("MenuTextBucket(MaxWidth)"), std::string::npos)
		<< "MaxWidthBucket must not use 0.1-pixel MenuTextBucket anymore";
}

// Intent: catch regression of the stable-text double-bookkeeping bug.
// Bug: CScopedMenuTextVisibleGuard constructor unconditionally cleared per-frame counters; outer shell guard
// (menus.cpp RenderMenuShell) and inner content guard (menus_settings.cpp RenderSettings) each constructed
// one. Inner constructor wiped outer's accumulation; both destructors logged when candidates > 0, producing
// 2x duplicate settings_text_usage payloads (6/18 hit-rate audit found 2:269 duplication histogram).
// Fix: guard uses stack semantics — only stack bottom (m_Previous == false) clears counters on construct
// and flushes log on destruct; nested guards inherit parent's counters and add their own on top.
TEST(QmMonitoringHelpers, SettingsTextVisibleGuardUsesStackSemantics)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	const size_t CtorPos = Menus.find("CScopedMenuTextVisibleGuard::CScopedMenuTextVisibleGuard");
	ASSERT_NE(CtorPos, std::string::npos);
	const std::string CtorTail = Menus.substr(CtorPos, 1500);

	EXPECT_NE(CtorTail.find("if(!m_Previous)"), std::string::npos)
		<< "Constructor must gate counter reset on stack-bottom (m_Previous == false)";

	const size_t DtorPos = Menus.find("CScopedMenuTextVisibleGuard::~CScopedMenuTextVisibleGuard");
	ASSERT_NE(DtorPos, std::string::npos);
	const std::string DtorTail = Menus.substr(DtorPos, 800);

	EXPECT_NE(DtorTail.find("if(!m_Previous && m_pMenus->m_MenuTextStableCandidatesThisFrame > 0)"), std::string::npos)
		<< "Destructor must gate log on stack-bottom (m_Previous == false)";
}

TEST(QmMonitoringHelpers, RealOnePctLowUsesFrameSamples)
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

TEST(QmMonitoringHelpers, SettingsPerfWindowEndsAfterFixedFrameBudget)
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

TEST(QmMonitoringHelpers, SettingsPerfScrollWindowEndsAfterIdleTimeout)
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

TEST(QmMonitoringHelpers, SettingsPerfWindowStartFlushesInterruptedWindow)
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

TEST(QmMonitoringHelpers, SettingsPerfWindowEnsureDoesNotRestartMatchingScrollOperation)
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

TEST(QmMonitoringHelpers, SettingsOpenWindowIsProtectedFromStalePreviousSettingsState)
{
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(MenusSource.find("OldPage != NewPage && NewPage == PAGE_SETTINGS"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_SettingsPerfLastPage = -1;"), std::string::npos);
	EXPECT_NE(SettingsSource.find("if(m_SettingsPerfLastPage != -1)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("StartSettingsPerfFixedWindow(\"settings_tab_switch\""), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsPerfWindowFlushesActiveSamplesOnShutdown)
{
	const std::string MenusSource = ReadTestSourceFile("src/game/client/components/menus.cpp");

	EXPECT_NE(MenusSource.find("void CMenus::OnShutdown()"), std::string::npos);
	EXPECT_NE(MenusSource.find("if(m_SettingsPerfWindowTracker.HasActiveWindow())"), std::string::npos);
	EXPECT_NE(MenusSource.find("const SQmSettingsPerfWindowSummary Summary = m_SettingsPerfWindowTracker.FinishActiveWindow();"), std::string::npos);
	EXPECT_NE(MenusSource.find("LogSettingsPerfWindowSummary(Summary);"), std::string::npos);
}

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
		return Predicate();
	}

} // namespace

TEST(QmMonitoringHelpers, DevicePerfSnapshotCacheReturnsConsistentVersionedSnapshot)
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

TEST(QmMonitoringHelpers, DevicePerfSnapshotCacheKeepsSampleAndVersionConsistentAcrossThreads)
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

TEST(QmMonitoringHelpers, DevicePerfSamplerStateStopsWorkerOnDisableAndCanRestart)
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

TEST(QmMonitoringHelpers, DiskReadRateUsesMegabytesPerSecond)
{
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(0, 0, 1024 * 1024, 1000000000ull), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(0, 1000000000ull, 1024 * 1024, 2000000000ull), 1.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(1024, 2000000000ull, 1024, 3000000000ull), 0.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(2048, 5000000000ull, 1024, 4000000000ull), -1.0f);
	EXPECT_FLOAT_EQ(QmComputeDiskReadMbPerSec(1024, 1000000000ull, 2048, 1000000000ull), -1.0f);
}

TEST(QmMonitoringHelpers, PerfConfigDefaultsUseLowThresholdWithoutJsonToggle)
{
	std::ifstream File(TestSourcePath("src/engine/shared/config_variables_qmclient.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("MACRO_CONFIG_INT(QmPerfDebugThresholdMs, qm_perf_debug_threshold_ms, 4, 1, 1000"), std::string::npos);
	EXPECT_EQ(Source.find("MACRO_CONFIG_INT(QmPerfJson, qm_perf_json, 0, 0, 1"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfDurationGateUsesConfiguredThreshold)
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

TEST(QmMonitoringHelpers, ProcessHighPriorityConfigExistsAndDefaultsOff)
{
	std::ifstream File(TestSourcePath("src/engine/shared/config_variables_qmclient.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("MACRO_CONFIG_INT(QmProcessHighPriority, qm_process_high_priority, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Source.find("MACRO_CONFIG_INT(QmAssetsPreviewBudgetMbOverride, qm_assets_preview_budget_mb_override, 0, 0, 16384"), std::string::npos);
	EXPECT_NE(Source.find("MACRO_CONFIG_INT(QmAssetsPreviewBudgetPercent, qm_assets_preview_budget_percent, 8, 0, 100"), std::string::npos);
}

TEST(QmMonitoringHelpers, WindowsStartupPriorityHookIsOptionalAndGuarded)
{
	std::ifstream File(TestSourcePath("src/engine/client/client.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("#if defined(CONF_FAMILY_WINDOWS)"), std::string::npos);
	EXPECT_NE(Source.find("if(g_Config.m_QmProcessHighPriority)"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmProcessHighPriority ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS"), std::string::npos);
	EXPECT_NE(Source.find("SetPriorityClass(GetCurrentProcess(), PriorityClass)"), std::string::npos);
	EXPECT_NE(Source.find("m_pConsole->Chain(\"qm_process_high_priority\", ConchainProcessHighPriority, this);"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfLoggingAlwaysEmitsJsonPayload)
{
	std::ifstream File(TestSourcePath("src/game/client/components/qmclient/perf_logging.h"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_EQ(Source.find("if(g_Config.m_QmPerfJson == 0)"), std::string::npos);
	EXPECT_NE(Source.find("str_copy(aJson, \"{\", sizeof(aJson));"), std::string::npos);
	EXPECT_NE(Source.find("dbg_msg(pSystem, \"%s\", aJson);"), std::string::npos);
	EXPECT_NE(Source.find("if(!QmPerfShouldLogDuration(DurationMs, Force))"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfPayloadJsonFieldsPreserveSpaceContainingValues)
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

TEST(QmMonitoringHelpers, RuntimePerfCallsitesUseSharedLoggingHelpers)
{
	for(const char *pPath : {
		    "src/game/client/components/countryflags.cpp",
		    "src/game/client/components/menus.cpp",
		    "src/game/client/components/menus_settings_assets.cpp",
		    "src/game/client/components/section_loader.cpp",
		    "src/game/client/components/qmclient/menus_qmclient.cpp",
		    "src/game/client/components/tclient/menus_tclient.cpp",
	    })
	{
		std::ifstream File(TestSourcePath(pPath));
		ASSERT_TRUE(File.good()) << pPath;
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		EXPECT_EQ(Source.find("dbg_msg(\"perf/"), std::string::npos) << pPath;
	}
}

TEST(QmMonitoringHelpers, MenuTextPoolReplacesSettingsOnlyBoundary)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	EXPECT_TRUE(ContainsAll(Header, {"m_MenuTextPool", "SMenuTextPoolEntry", "SMenuTextStyleKey", "MenuTextElement(", "DoMenuLabelStreamed(", "PrebuildSettingsMenuTextPool("}));
	EXPECT_TRUE(ContainsAll(Source, {"SettingsTextElement", "MenuTextElement", "MENU_TEXT_SCOPE_SETTINGS"}));
	EXPECT_EQ(Header.find("std::unordered_map<std::string, SSettingsTextPoolEntry> m_SettingsTextPool"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsStableTextMissAndStaleBlockVisibleBuild)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	EXPECT_TRUE(ContainsAll(Source, {"event=settings_text_miss", "event=settings_text_stale", "m_MenuTextPoolVisibleGuard", "StableTextMiss", "StableTextStale"}));
	EXPECT_TRUE(ContainsAll(Source, {"event=settings_text_usage", "m_MenuTextStableCandidatesThisFrame", "m_MenuTextStableHitsThisFrame", "m_MenuTextStableReusedThisFrame"}));
	EXPECT_TRUE(ContainsAll(Source, {"MenuTextPoolSizeForTesting", "CScopedMenuTextVisibleGuard"}));
	EXPECT_EQ(Source.find("context-checkbox-common-"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Source, {
						"scope=%s page=%s tab=%d subtab=%d key=%s reason=%s plan_status=%s operation=%s frame=%",
						"%s:%d:%d:%d:%s:fs%d:al%d:mw%d:us%d:hd%d:cm%d",
						"StyleKey.m_Align",
						"StyleKey.m_MaxWidthBucket",
						"StyleKey.m_HiDpiScaleBucket",
						"StyleKey.m_CompactMode",
					}));
	EXPECT_TRUE(ContainsAll(Source, {
						"const char *CMenus::SettingsPerfStableTextScope(int Page) const",
						"str_comp(SettingsPerfActiveOperation(), \"ingame_esc_open\") == 0",
						"(void)Page;",
						"return str_comp(pActivePage, aPage) == 0 ? \"target_settings\" : \"settings\";",
						"SettingsPerfStableTextScope(Page)",
					}));
	EXPECT_TRUE(ContainsAll(Source, {"LogSettingsTextPoolCoverageGap(Client(), \"settings_text_miss\", Scope, SettingsPerfStableTextScope(Page), Page, Tab, Subtab", "LogSettingsTextPoolCoverageGap(Client(), \"settings_text_stale\", Scope, SettingsPerfStableTextScope(Page), Page, Tab, Subtab"}));
	EXPECT_TRUE(ContainsAll(Source, {"case ESettingsInvalidationReason::DPI_CHANGED: return \"dpi\";", "case ESettingsInvalidationReason::UI_SCALE_CHANGED: return \"ui_scale\";"}));

	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	EXPECT_TRUE(ContainsAll(Header, {
						"void DoSettingsMenuLabel(int Page, int Tab, int Subtab",
						"int DoSettingsButton_Menu(int Page, int Tab, int Subtab, CButtonContainer *pBC",
						"int DoSettingsButton_CheckBox(int Page, int Tab, int Subtab",
						"bool DoSettingsScrollbarOption(int Page, int Tab, int Subtab",
					}));
	EXPECT_NE(Source.find("CUIElement &TextElement = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);"), std::string::npos);
	EXPECT_NE(Source.find("dbg_assert(pBC != nullptr, \"settings menu button requires a stable button container\")"), std::string::npos);
	EXPECT_EQ(Header.find("CButtonContainer *pBC = nullptr"), std::string::npos);
	EXPECT_EQ(Source.find("s_FallbackButton"), std::string::npos);
	EXPECT_NE(Source.find("DoButton_Menu(pBC, pText, Checked, pRect, Flags, nullptr, Corners, Rounding, FontFactor, Color, &TextElement, ResolvedBodySize)"), std::string::npos);
	EXPECT_EQ(Source.find("reason=%s\", pReason != nullptr ? pReason : \"unknown\""), std::string::npos);

	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	EXPECT_TRUE(ContainsAll(TClient, {
						 "DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_OverrideButton",
						 "DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_AddButton",
						 "DoSettingsButton_Menu(SETTINGS_TCLIENT, TCLIENT_TAB_BINDWHEEL, TCLIENT_TAB_BINDWHEEL, &s_RemoveButton",
					 }));
}

TEST(QmMonitoringHelpers, MenuTextPoolStaleRefreshDoesNotReinitRegisteredElement)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const size_t StaleBranch = Source.find("else if(It->second.m_Generation != m_MenuTextPoolGeneration)");
	ASSERT_NE(StaleBranch, std::string::npos);
	const size_t ReturnElement = Source.find("return It->second.m_Element;", StaleBranch);
	ASSERT_NE(ReturnElement, std::string::npos);
	const std::string Body = Source.substr(StaleBranch, ReturnElement - StaleBranch);

	EXPECT_NE(Body.find("Ui()->ResetUIElement(It->second.m_Element);"), std::string::npos);
	EXPECT_EQ(Body.find("It->second.m_Element.Init(Ui(), 1);"), std::string::npos);
}

TEST(QmMonitoringHelpers, StableSettingsHelpersRequireExplicitTextIds)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	EXPECT_EQ(Source.find("ctx-scrollbar-"), std::string::npos);
	EXPECT_EQ(Source.find("ctx-checkbox-"), std::string::npos);
	EXPECT_EQ(Source.find("ctx-menu-label-"), std::string::npos);
	EXPECT_EQ(Source.find("ctx-menu-button-"), std::string::npos);
	EXPECT_EQ(Source.find("ctx-label-"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientStableTextCandidateAuditIsEmptyExceptAllowlist)
{
	const char *pFile = "src/game/client/components/tclient/menus_tclient.cpp";
	const std::string Source = ReadRepoFile(pFile);
	const std::vector<SStableTextCandidate> vCandidates = CollectRawStableTextCandidatesWithLines(Source);
	const std::vector<SStableTextRawAllow> vAllowlist = {
		{pFile, 3464, "user-generated"},
		{pFile, 3803, "dynamic-value"},
		{pFile, 3805, "localized-list-data"},
		{pFile, 3933, "localized-list-data"},
		{pFile, 3994, "localized-list-data"},
		{pFile, 4106, "user-generated"},
		{pFile, 4118, "user-generated"},
		{pFile, 4180, "localized-list-data"},
		{pFile, 4190, "dynamic-value"},
		{pFile, 4222, "localized-list-data"},
		{pFile, 4229, "dynamic-value"},
		{pFile, 4283, "localized-list-data"},
		{pFile, 4744, "dynamic-value"},
		{pFile, 4747, "dynamic-value"},
		{pFile, 4750, "dynamic-value"},
		{pFile, 4755, "user-generated"},
		{pFile, 4757, "user-generated"},
		{pFile, 5126, "dynamic-value"},
		{pFile, 5459, "localized-list-data"},
		{pFile, 5496, "localized-list-data"},
		{pFile, 5512, "localized-list-data"},
	};
	const std::vector<SStableTextCandidate> vUnexpected = FilterCandidatesNotCoveredByMenuPoolOrAllowlist(pFile, vCandidates, vAllowlist);
	EXPECT_TRUE(vUnexpected.empty()) << JoinCandidates(vUnexpected);
}

TEST(QmMonitoringHelpers, QmClientStableTextCandidateAuditIsEmptyExceptAllowlist)
{
	const char *pFile = "src/game/client/components/qmclient/menus_qmclient.cpp";
	const std::string Source = ReadRepoFile(pFile);
	const std::vector<SStableTextCandidate> vCandidates = CollectRawStableTextCandidatesWithLines(Source);
	const std::vector<SStableTextRawAllow> vAllowlist = {
		{pFile, 765, "stateful-new-label"},
		{pFile, 757, "stateful-new-label"},
		{pFile, 942, "animated-style"},
		{pFile, 943, "animated-style"},
		{pFile, 950, "animated-style"},
		{pFile, 951, "animated-style"},
		{pFile, 954, "animated-style"},
		{pFile, 958, "animated-style"},
		{pFile, 959, "animated-style"},
		{pFile, 961, "animated-style"},
		{pFile, 962, "animated-style"},
		{pFile, 963, "animated-style"},
		{pFile, 966, "animated-style"},
		{pFile, 967, "animated-style"},
		{pFile, 968, "animated-style"},
		{pFile, 969, "animated-style"},
		{pFile, 970, "animated-style"},
		{pFile, 976, "animated-style"},
		{pFile, 985, "animated-style"},
		{pFile, 993, "animated-style"},
		{pFile, 1989, "dynamic-value"},
		{pFile, 2155, "icon-only"},
		{pFile, 2482, "animated-style"},
		{pFile, 2483, "status-message"},
		{pFile, 2490, "animated-style"},
		{pFile, 2491, "status-message"},
		{pFile, 3461, "localized-list-data"},
		{pFile, 4315, "localized-list-data"},
		{pFile, 4677, "status-message"},
		{pFile, 4681, "user-generated"},
		{pFile, 4689, "user-generated"},
		{pFile, 4954, "status-message"},
		{pFile, 5185, "stateful-new-label"},
		{pFile, 5181, "stateful-new-label"},
		{pFile, 5193, "stateful-new-label"},
		{pFile, 5771, "status-message"},
		{pFile, 5767, "status-message"},
		{pFile, 5779, "status-message"},
		{pFile, 6259, "status-message"},
		{pFile, 6260, "status-message"},
		{pFile, 952, "animated-style"},
		{pFile, 953, "animated-style"},
		{pFile, 960, "animated-style"},
		{pFile, 964, "animated-style"},
		{pFile, 971, "animated-style"},
		{pFile, 972, "animated-style"},
		{pFile, 974, "animated-style"},
		{pFile, 978, "animated-style"},
		{pFile, 981, "animated-style"},
		{pFile, 982, "animated-style"},
		{pFile, 995, "animated-style"},
		{pFile, 2501, "animated-style"},
		{pFile, 2502, "animated-style"},
		{pFile, 4700, "localized-list-data"},
		{pFile, 5204, "stateful-new-label"},
		{pFile, 5790, "status-message"},
		{pFile, 949, "animated-style"},
		{pFile, 957, "animated-style"},
		{pFile, 965, "animated-style"},
		{pFile, 975, "animated-style"},
		{pFile, 977, "animated-style"},
		{pFile, 979, "animated-style"},
		{pFile, 980, "animated-style"},
		{pFile, 992, "animated-style"},
		{pFile, 2611, "animated-style"},
		{pFile, 2612, "user-generated"},
		{pFile, 7204, "stateful-new-label"},
		{pFile, 983, "dynamic-value"},
		{pFile, 990, "dynamic-value"},
		{pFile, 994, "dynamic-value"},
		{pFile, 998, "dynamic-value"},
		{pFile, 999, "dynamic-value"},
		{pFile, 1001, "dynamic-value"},
		{pFile, 1002, "dynamic-value"},
		{pFile, 984, "dynamic-value"},
		{pFile, 986, "dynamic-value"},
		{pFile, 987, "dynamic-value"},
		{pFile, 989, "dynamic-value"},
		{pFile, 996, "dynamic-value"},
		{pFile, 1000, "dynamic-value"},
		{pFile, 1003, "dynamic-value"},
		{pFile, 1004, "dynamic-value"},
		{pFile, 1045, "dynamic-value"},
		{pFile, 1046, "dynamic-value"},
		{pFile, 1053, "dynamic-value"},
		{pFile, 1057, "dynamic-value"},
		{pFile, 1061, "dynamic-value"},
		{pFile, 1062, "dynamic-value"},
		{pFile, 1064, "dynamic-value"},
		{pFile, 1065, "dynamic-value"},
		{pFile, 1047, "dynamic-value"},
		{pFile, 1048, "dynamic-value"},
		{pFile, 1049, "dynamic-value"},
		{pFile, 1050, "dynamic-value"},
		{pFile, 1054, "dynamic-value"},
		{pFile, 1056, "dynamic-value"},
		{pFile, 1058, "dynamic-value"},
		{pFile, 1060, "dynamic-value"},
		{pFile, 1063, "dynamic-value"},
		{pFile, 1066, "dynamic-value"},
		{pFile, 1067, "dynamic-value"},
		{pFile, 1068, "dynamic-value"},
		{pFile, 1069, "dynamic-value"},
		{pFile, 1306, "dynamic-value"},
		{pFile, 1307, "dynamic-value"},
		{pFile, 1313, "dynamic-value"},
		{pFile, 1317, "dynamic-value"},
		{pFile, 1321, "dynamic-value"},
		{pFile, 1322, "dynamic-value"},
		{pFile, 1324, "dynamic-value"},
		{pFile, 1325, "dynamic-value"},
		{pFile, 1308, "dynamic-value"},
		{pFile, 1309, "dynamic-value"},
		{pFile, 1315, "dynamic-value"},
		{pFile, 1319, "dynamic-value"},
		{pFile, 1323, "dynamic-value"},
		{pFile, 1326, "dynamic-value"},
		{pFile, 1327, "dynamic-value"},
		{pFile, 1331, "dynamic-value"},
		{pFile, 1332, "dynamic-value"},
		{pFile, 1333, "dynamic-value"},
		{pFile, 1337, "dynamic-value"},
		{pFile, 1338, "dynamic-value"},
		{pFile, 1344, "dynamic-value"},
		{pFile, 1345, "dynamic-value"},
		{pFile, 1346, "dynamic-value"},
		{pFile, 1347, "dynamic-value"},
		{pFile, 1341, "dynamic-value"},
		{pFile, 1342, "dynamic-value"},
		{pFile, 1348, "dynamic-value"},
		{pFile, 1349, "dynamic-value"},
		{pFile, 1350, "dynamic-value"},
		{pFile, 1352, "dynamic-value"},
		{pFile, 1356, "dynamic-value"},
		{pFile, 1357, "dynamic-value"},
		{pFile, 1359, "dynamic-value"},
		{pFile, 1360, "dynamic-value"},
	};
	const std::vector<SStableTextCandidate> vUnexpected = FilterCandidatesNotCoveredByMenuPoolOrAllowlist(pFile, vCandidates, vAllowlist);
	EXPECT_TRUE(vUnexpected.empty()) << JoinCandidates(vUnexpected);
	EXPECT_FALSE(IsPooledStableTextLine("Ui()->DoLabel(&TitleRect, QmNewFeatureLabel(pTitle, pNewFeatureId, aTitle, sizeof(aTitle)), LgHeadlineSizeNew, TEXTALIGN_ML);"));
	EXPECT_FALSE(IsPooledStableTextLine("Ui()->DoLabel(&Label, RainbowColor(), LgBodySize, TEXTALIGN_ML);"));
	EXPECT_FALSE(IsPooledStableTextLine("RenderQmModuleHeadline(View, pTitle, pTip, true);"));
}

TEST(QmMonitoringHelpers, BaseSettingsStableTextCandidateAuditIsEmptyExceptAllowlist)
{
	const char *pFile = "src/game/client/components/menus_settings.cpp";
	const std::string Source = ReadRepoFile(pFile);
	const std::vector<SStableTextCandidate> vCandidates = CollectRawStableTextCandidatesWithLines(Source);
	const std::vector<SStableTextRawAllow> vAllowlist = {
		{pFile, 472, "animated-style"},
		{pFile, 1446, "input-text"},
		{pFile, 1759, "dynamic-value"},
		{pFile, 1790, "localized-list-data"},
		{pFile, 1935, "localized-list-data"},
		{pFile, 1971, "localized-list-data"},
		{pFile, 1980, "localized-list-data"},
		{pFile, 1989, "localized-list-data"},
		{pFile, 2680, "search-result"},
		{pFile, 3590, "localized-list-data"},
		{pFile, 3644, "input-text"},
		{pFile, 3648, "input-text"},
		{pFile, 3838, "input-text"},
		{pFile, 4135, "localized-list-data"},
		{pFile, 4804, "status-message"},
		{pFile, 4809, "status-message"},
		{pFile, 6427, "input-text"},
		{pFile, 474, "localized-list-data"},
		{pFile, 1761, "dynamic-value"},
		{pFile, 1973, "localized-list-data"},
		{pFile, 1982, "localized-list-data"},
		{pFile, 1991, "localized-list-data"},
		{pFile, 4890, "status-message"},
		{pFile, 4895, "status-message"},
		{pFile, 6513, "input-text"},
	};
	const std::vector<SStableTextCandidate> vUnexpected = FilterCandidatesNotCoveredByMenuPoolOrAllowlist(pFile, vCandidates, vAllowlist);
	EXPECT_TRUE(vUnexpected.empty()) << JoinCandidates(vUnexpected);
}

TEST(QmMonitoringHelpers, TClientSettingsCardsUseSharedBoxAndAlignedFirstSection)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("auto DrawSectionBox = "), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->RenderBatchableRect(&Section, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);"), std::string::npos);
	EXPECT_NE(Body.find("m_SettingsCardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(Body.find("AppendDeckCards(vLeftSections);"), std::string::npos);
	EXPECT_NE(Body.find("AppendDeckCards(vRightSections);"), std::string::npos);
	EXPECT_NE(Body.find("VisualFontLoader.Process(false);"), std::string::npos);
	EXPECT_NE(Body.find("RightSectionLoader.Process(false);"), std::string::npos);
	EXPECT_EQ(Body.find("CachedHeightForStableCardId("), std::string::npos);
	EXPECT_NE(Source.find("m_SectionMeasure = std::move(Section.m_MeasureFn);"), std::string::npos);
	EXPECT_NE(Source.find("m_SectionRender = std::move(Section.m_RenderFullFn);"), std::string::npos);
	EXPECT_NE(Source.find("Section.m_MeasureFn = [this](CUIRect &MeasureColumn)"), std::string::npos);
	EXPECT_EQ(Source.find("m_SectionMeasure = Section.m_MeasureFn;"), std::string::npos);
	EXPECT_NE(Body.find("s_aDeckCardBindings[DeckCardBindingIndex++].BindSection(Section);"), std::string::npos);
	EXPECT_NE(Body.find("Section.m_MeasureFn = [LayoutSection]"), std::string::npos);
	EXPECT_NE(Body.find("Section.m_RenderCompactFn = [this, LayoutSection,"), std::string::npos);
	EXPECT_EQ(Body.find("Section.m_MeasureFn = [&LayoutSection]"), std::string::npos);
	EXPECT_EQ(Body.find("Section.m_RenderCompactFn = [this, &LayoutSection,"), std::string::npos);
	EXPECT_EQ(Body.find("s_VisualFontLoader.m_ScrollY"), std::string::npos);
	EXPECT_EQ(Body.find("s_RightSectionLoader.m_ScrollY"), std::string::npos);
	EXPECT_EQ(Body.find("DrawTClientCacheSectionBox("), std::string::npos);
	EXPECT_EQ(Body.find("InsetTClientCacheSectionContent("), std::string::npos);
	EXPECT_EQ(Source.find("ConfigureSplitCachedStaticLayer"), std::string::npos);
	EXPECT_EQ(Source.find("RenderSettingsCardSection"), std::string::npos);
	EXPECT_NE(Source.find("ConfigureSettingsCardSection"), std::string::npos);
	EXPECT_NE(Source.find("Section.m_pStableCardId = pStableCardId;"), std::string::npos);
	EXPECT_NE(Header.find("void ConfigureSettingsCardSection(SSettingsSection &Section, const char *pTitle, const char *pStableCardId"), std::string::npos);

	const size_t ThemeSection = Source.find("SSettingsSection CMenus::BuildTClientThemeCacheSection()");
	ASSERT_NE(ThemeSection, std::string::npos);
	const size_t ThemeSectionEnd = Source.find("SSettingsSection CMenus::BuildTClientAutoReplyCacheSection()", ThemeSection);
	ASSERT_NE(ThemeSectionEnd, std::string::npos);
	const std::string ThemeBody = Source.substr(ThemeSection, ThemeSectionEnd - ThemeSection);
	EXPECT_NE(ThemeBody.find("ConfigureSettingsCardSection(S, Localizable(\"Visual: Font & Cursor\"), \"tclient:visual-font-cursor\", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientThemeCacheSection(Col, Render); }, Margin);"), std::string::npos);

	const size_t AutoReplySection = Source.find("SSettingsSection CMenus::BuildTClientAutoReplyCacheSection()");
	ASSERT_NE(AutoReplySection, std::string::npos);
	const size_t AutoReplySectionEnd = Source.find("SSettingsSection CMenus::BuildTClientPetCacheSection()", AutoReplySection);
	ASSERT_NE(AutoReplySectionEnd, std::string::npos);
	const std::string AutoReplyBody = Source.substr(AutoReplySection, AutoReplySectionEnd - AutoReplySection);
	EXPECT_NE(AutoReplyBody.find("ConfigureSettingsCardSection(S, Localizable(\"Auto reply\"), \"tclient:auto-reply\", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientAutoReplyCacheSection(Col, Render); }, MarginBetweenSections);"), std::string::npos);

	const size_t PetSection = Source.find("SSettingsSection CMenus::BuildTClientPetCacheSection()");
	ASSERT_NE(PetSection, std::string::npos);
	const size_t PetSectionEnd = Source.find("SSettingsSection CMenus::BuildTClientHudCacheSection()", PetSection);
	ASSERT_NE(PetSectionEnd, std::string::npos);
	const std::string PetBody = Source.substr(PetSection, PetSectionEnd - PetSection);
	EXPECT_NE(PetBody.find("ConfigureSettingsCardSection(S, \"Pet\", \"tclient:pet\", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientPetCacheSection(Col, Render); }, MarginBetweenSections);"), std::string::npos);

	const size_t HudSection = Source.find("SSettingsSection CMenus::BuildTClientHudCacheSection()");
	ASSERT_NE(HudSection, std::string::npos);
	const size_t HudSectionEnd = Source.find("void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)", HudSection);
	ASSERT_NE(HudSectionEnd, std::string::npos);
	const std::string HudBody = Source.substr(HudSection, HudSectionEnd - HudSection);
	EXPECT_NE(HudBody.find("ConfigureSettingsCardSection(S, \"HUD\", \"tclient:hud\", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientHudCacheSection(Col, Render); }, Margin);"), std::string::npos);

	const size_t ThemeLayout = Source.find("float CMenus::LayoutTClientThemeCacheSection(CUIRect &CurrentColumn, bool Render)");
	ASSERT_NE(ThemeLayout, std::string::npos);
	const size_t ThemeLayoutEnd = Source.find("float CMenus::LayoutTClientAutoReplyCacheSection", ThemeLayout);
	ASSERT_NE(ThemeLayoutEnd, std::string::npos);
	const std::string ThemeLayoutBody = Source.substr(ThemeLayout, ThemeLayoutEnd - ThemeLayout);
	EXPECT_NE(ThemeLayoutBody.find("CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);"), std::string::npos);
	EXPECT_EQ(ThemeLayoutBody.find("CurrentColumn.HSplitTop(MarginBetweenSections, nullptr, &CurrentColumn);"), std::string::npos);

	const size_t AutoReplyLayout = Source.find("float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)");
	ASSERT_NE(AutoReplyLayout, std::string::npos);
	const size_t AutoReplyLayoutEnd = Source.find("float CMenus::LayoutTClientPetCacheSection", AutoReplyLayout);
	ASSERT_NE(AutoReplyLayoutEnd, std::string::npos);
	const std::string AutoReplyLayoutBody = Source.substr(AutoReplyLayout, AutoReplyLayoutEnd - AutoReplyLayout);
	EXPECT_NE(AutoReplyLayoutBody.find("const float SavedY = CurrentColumn.y;"), std::string::npos);
	EXPECT_NE(AutoReplyLayoutBody.find("return CurrentColumn.y - SavedY;"), std::string::npos);
	EXPECT_EQ(AutoReplyLayoutBody.find("return CurrentColumn.y - BoxRect.y;"), std::string::npos);

	const size_t PetLayout = Source.find("float CMenus::LayoutTClientPetCacheSection(CUIRect &CurrentColumn, bool Render)");
	ASSERT_NE(PetLayout, std::string::npos);
	const size_t PetLayoutEnd = Source.find("float CMenus::LayoutTClientHudCacheSection", PetLayout);
	ASSERT_NE(PetLayoutEnd, std::string::npos);
	const std::string PetLayoutBody = Source.substr(PetLayout, PetLayoutEnd - PetLayout);
	EXPECT_NE(PetLayoutBody.find("const float SavedY = CurrentColumn.y;"), std::string::npos);
	EXPECT_NE(PetLayoutBody.find("return CurrentColumn.y - SavedY;"), std::string::npos);
	EXPECT_EQ(PetLayoutBody.find("return CurrentColumn.y - BoxRect.y;"), std::string::npos);

	const size_t HudLayout = Source.find("float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)");
	ASSERT_NE(HudLayout, std::string::npos);
	const size_t HudLayoutEnd = Source.find("SSettingsSection CMenus::BuildTClientThemeCacheSection", HudLayout);
	ASSERT_NE(HudLayoutEnd, std::string::npos);
	const std::string HudLayoutBody = Source.substr(HudLayout, HudLayoutEnd - HudLayout);
	EXPECT_NE(HudLayoutBody.find("const float SavedY = CurrentColumn.y;"), std::string::npos);
	EXPECT_NE(HudLayoutBody.find("return CurrentColumn.y - SavedY;"), std::string::npos);
	EXPECT_EQ(HudLayoutBody.find("return CurrentColumn.y - BoxRect.y;"), std::string::npos);

	EXPECT_EQ(Source.find("TClientCacheSectionBoxRect("), std::string::npos);
	EXPECT_EQ(Source.find("InsetTClientCacheSectionContent("), std::string::npos);
	EXPECT_EQ(Source.find("DrawTClientCacheSectionBox("), std::string::npos);
	EXPECT_EQ(Source.find("RenderSettingsCardSection("), std::string::npos);

	const std::string BindChatBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(BindChatBody.empty());
	EXPECT_NE(BindChatBody.find("SettingsPageLayout(MainView, UiScale);"), std::string::npos);
	EXPECT_NE(BindChatBody.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_ChatBindsPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(BindChatBody.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_EQ(BindChatBody.find("DrawTClientCacheSectionBox(Section);"), std::string::npos);
	EXPECT_EQ(BindChatBody.find("s_ScrollRegion.AddRect(TClientCacheSectionBoxRect(Section))"), std::string::npos);
	EXPECT_EQ(BindChatBody.find("InsetTClientCacheSectionContent(ContentColumn);"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientSettingsCardDeckUsesPublicRuntimeOnly)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");

	EXPECT_EQ(Header.find("std::vector<std::string> m_vTClientLeftCardOrder;"), std::string::npos);
	EXPECT_EQ(Header.find("std::vector<std::string> m_vTClientRightCardOrder;"), std::string::npos);
	EXPECT_EQ(Header.find("SSettingsCardDeckDragState m_TClientSettingsCardDragState;"), std::string::npos);
	EXPECT_EQ(Source.find("RegisterSettingsCardDeckItem(Item);"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckCanStartDrag({&Item, false, HitRegion})"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckBeginPress(DragState, Item);"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckTryPromotePress(DragState);"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckClearPress(DragState);"), std::string::npos);
	EXPECT_EQ(Source.find("DragState.m_DropColumn = Column;"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckDropColumnForMouseX(LeftView, RightView, Ui()->MouseX(), m_TClientSettingsCardDragState.m_DropColumn)"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckMoveBetweenColumns(vSourceOrder, vTargetOrder, DragState.m_Item.m_pStableId, DropIndex)"), std::string::npos);
	EXPECT_EQ(Source.find("CommitSettingsCardDeckDragDrop(pOrder, Column, DropIndex, pDeckCoordinator);"), std::string::npos);
	EXPECT_EQ(Header.find("bool CommitSettingsCardDeckDragDrop(std::vector<std::string> *pOrder, ESettingsCardDeckColumn DropColumn, int DropIndex, settings_card_deck::CDeck *pDeckCoordinator = nullptr);"), std::string::npos);
	EXPECT_EQ(Source.find("m_TClientSettingsCardDeckOrderDirty = true;"), std::string::npos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/QmCardRegistry.h>"), std::string::npos);
	EXPECT_EQ(Source.find("LoadTClientOrderFromGlobalCardModel(g_Config.m_QmGlobalCardOrder, m_vTClientLeftCardOrder, m_vTClientRightCardOrder)"), std::string::npos);
	EXPECT_NE(Source.find("Model.LoadExplicit(pConfig, qm_card_registry::BuildDefaultEntries())"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pEntry = pConfig;\n\t\tchar aToken[160];\n\t\twhile((pEntry = str_next_token(pEntry, \";\", aToken"), std::string::npos);
	EXPECT_EQ(Source.find("LoadTClientOrderFromLegacyCardOrder(g_Config.m_QmSettingsCardOrder, m_vTClientLeftCardOrder, m_vTClientRightCardOrder);"), std::string::npos);
	EXPECT_EQ(Source.find("const bool HasGlobalTClientOrder = LoadTClientOrderFromGlobalCardModel(g_Config.m_QmGlobalCardOrder, m_vTClientLeftCardOrder, m_vTClientRightCardOrder);"), std::string::npos);
	EXPECT_EQ(Source.find("if(!HasGlobalTClientOrder)"), std::string::npos);
	EXPECT_EQ(Source.find("if(g_Config.m_QmGlobalCardOrder[0] == '\\0')"), std::string::npos);
	EXPECT_EQ(Source.find("else\n\t\t\t\t{\n\t\t\t\t\tm_vTClientLeftCardOrder.clear();\n\t\t\t\t\tm_vTClientRightCardOrder.clear();\n\t\t\t\t}"), std::string::npos);
	EXPECT_EQ(Source.find("const bool IsTClientMainOrder = pOrder == &m_vTClientLeftCardOrder || pOrder == &m_vTClientRightCardOrder;"), std::string::npos);
	EXPECT_EQ(Source.find("if(IsTClientMainOrder)"), std::string::npos);
	EXPECT_EQ(Source.find("SerializeMergedTClientGlobalCardOrder(g_Config.m_QmGlobalCardOrder, m_vTClientLeftCardOrder, m_vTClientRightCardOrder"), std::string::npos);
	EXPECT_EQ(Source.find("str_copy(g_Config.m_QmGlobalCardOrder, aMergedGlobalOrder, sizeof(g_Config.m_QmGlobalCardOrder));"), std::string::npos);
	EXPECT_EQ(Source.find("str_copy(g_Config.m_QmSettingsCardOrder, aSerialized"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckApplyOrder(vSections, vOrder);"), std::string::npos);
	EXPECT_EQ(Source.find("WrapSettingsCardDeckSections(vLeftSections, ESettingsCardDeckColumn::LEFT, m_vTClientLeftCardOrder);"), std::string::npos);
	EXPECT_EQ(Source.find("WrapSettingsCardDeckSections(vRightSections, ESettingsCardDeckColumn::RIGHT, m_vTClientRightCardOrder);"), std::string::npos);
	EXPECT_EQ(Source.find("const bool TClientSettingsCardDeckOrderDirtyAtFrameStart = !PrewarmOnly && m_TClientSettingsCardDeckOrderDirty;"), std::string::npos);
	EXPECT_EQ(Source.find("if(TClientSettingsCardDeckOrderDirtyAtFrameStart)\n\t\tm_TClientSettingsCardDeckOrderDirty = false;"), std::string::npos);
	EXPECT_EQ(Source.find("if(TClientSettingsCardDeckOrderDirtyAtFrameStart)\n\t\ts_VisualFontLoader.InvalidateCache(ESettingsCacheDirtyReason::CONFIG);"), std::string::npos);
	EXPECT_EQ(Source.find("if(TClientSettingsCardDeckOrderDirtyAtFrameStart)\n\t\t\ts_RightSectionLoader.InvalidateCache(ESettingsCacheDirtyReason::CONFIG);"), std::string::npos);
	EXPECT_EQ(Source.find("if(!PrewarmOnly && m_TClientSettingsCardDeckOrderDirty)\n\t\tm_TClientSettingsCardDeckOrderDirty = false;"), std::string::npos);
	EXPECT_NE(Source.find("VisualFontLoader.Register(std::move(vLeftSections));"), std::string::npos);
	EXPECT_NE(Source.find("RightSectionLoader.Register(std::move(vRightSections));"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckIsDraggingItem(m_TClientSettingsCardDragState, Item)"), std::string::npos);
	EXPECT_EQ(Source.find("m_TClientSettingsCardDragState.m_DropColumn == ColumnId"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckDropIndicatorRect(Item, m_TClientSettingsCardDragState.m_DropIndex"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckDropIndexForColumnItems("), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckProxyRect(m_TClientSettingsCardDragState.m_Item, Ui()->MouseX(), Ui()->MouseY())"), std::string::npos);
	EXPECT_EQ(Source.find("if(m_TClientSettingsCardDragState.m_Active && !Ui()->MouseButton(0) && Ui()->LastMouseButton(0))"), std::string::npos);
	EXPECT_EQ(Source.find("std::vector<std::string> *pOrder = m_TClientSettingsCardDragState.m_Item.m_Column == ESettingsCardDeckColumn::LEFT ? &m_vTClientLeftCardOrder : &m_vTClientRightCardOrder;"), std::string::npos);
	EXPECT_EQ(Source.find("if(m_TClientSettingsCardDragState.m_Active && !Ui()->MouseButton(0) && !Ui()->LastMouseButton(0))"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckAutoScrollDelta(Ui()->MouseY(), Viewport.y, Viewport.y + Viewport.h"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardDeckCanStartDrag({&Item, true"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientSettingsCardDeckCoversEveryBoxedSection)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::vector<const char *> vStableIds = {
		"tclient:visual-font-cursor",
		"tclient:visual-nameplates",
		"tclient:visual-effects",
		"tclient:input",
		"tclient:anti-latency-tools",
		"tclient:improved-anti-ping",
		"tclient:execute-on-join",
		"tclient:voting",
		"tclient:auto-reply",
		"tclient:player-indicator",
		"tclient:pet",
		"tclient:hud",
		"tclient:tee-status-bar",
		"tclient:tile-outlines",
		"tclient:ghost-tools",
		"tclient:rainbow",
		"tclient:tee-trails",
		"tclient:background-draw",
		"tclient:finish-name",
	};
	for(const char *pStableId : vStableIds)
	{
		EXPECT_NE(Source.find(pStableId), std::string::npos) << pStableId;
	}
	EXPECT_EQ(Source.find("if(SectionMeta.m_pStableCardId == nullptr || SectionMeta.m_pStableCardId[0] == '\\0')\n\t\t\t\tcontinue;"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientFocusModeSectionLabelsUseDisplayTextNotTranslationKeys)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmVisualFocusModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth)");
	ASSERT_FALSE(Body.empty());
	const std::string &FocusModeBody = Body;

	EXPECT_NE(FocusModeBody.find("auto RenderSection = [&](CUIRect &Target, const char *pTextId, const char *pLabel)"), std::string::npos);
	EXPECT_EQ(FocusModeBody.find("Localize(pTextId)"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("RenderQmVisualLabel(pTextId, &Row, Localize(pLabel), SmallSize);"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("RenderSection(LeftColumn, \"qmclient-focus-section-interface\", \"Interface\");"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("RenderSection(LeftColumn, \"qmclient-focus-section-players\", \"Players\");"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("RenderSection(LeftColumn, \"qmclient-focus-section-visuals\", \"Visuals\");"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("RenderSection(RightColumn, \"qmclient-focus-section-audio\", \"Audio\");"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("RenderSection(RightColumn, \"qmclient-focus-section-chat\", \"Chat\");"), std::string::npos);
	EXPECT_NE(Source.find("RenderQmVisualFocusModeContent(Content, LineHeight, BodySize, LineSpacing, LineSpacing, LabelWidth);"), std::string::npos);
	EXPECT_EQ(Source.find("RenderQmVisualFocusModeContent(Content, LineHeight, BodySize, LineSpacing, LabelWidth, LabelWidth);"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsTextColdStartAvoidsGlobalLanguageCacheAndCachesCheckboxLabels)
{
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_EQ(Source.find("PrepareLanguagePageCache(MainView.w);"), std::string::npos);
		EXPECT_EQ(Source.find("if(g_Config.m_UiSettingsPage == SETTINGS_LANGUAGE)"), std::string::npos);
		EXPECT_NE(Source.find("PrepareLanguagePageCache(List.w, true);"), std::string::npos);
		EXPECT_EQ(Source.find("PrepareLanguagePageCache(MainView.w, false)"), std::string::npos);
		EXPECT_NE(Source.find("PrepareLanguagePageCache(Content.w, false);"), std::string::npos);
		EXPECT_NE(Source.find("SettingsWarmupConsumeBudget(m_SettingsFrameBudget, ESettingsWarmupCost::TEXT_CONTAINER)"), std::string::npos);
		EXPECT_NE(Source.find("const bool TextChanged = RectEl.m_Text != Language.m_Name.c_str();"), std::string::npos);
		EXPECT_NE(Source.find("const bool SizeChanged = RectEl.m_Width != Label.w || RectEl.m_Height != Label.h;"), std::string::npos);
		EXPECT_NE(Source.find("const bool NeedsTextContainer = !RectEl.m_UITextContainer.Valid() || ColorChanged || TextChanged || SizeChanged;"), std::string::npos);
		EXPECT_NE(Source.find("DoSettingsButton_CheckBox(SETTINGS_GENERAL"), std::string::npos);
		EXPECT_NE(Source.find("DoSettingsButton_CheckBox(SETTINGS_GRAPHICS"), std::string::npos);
		EXPECT_NE(Source.find("DoSettingsButton_CheckBox(SETTINGS_SOUND"), std::string::npos);
		EXPECT_NE(Source.find("DoSettingsButton_CheckBox(SETTINGS_DDNET"), std::string::npos);
		const size_t NewShellMetrics = Source.find("m_SettingsContentMetrics = ResolveSettingsContentMetrics(Shell.m_ContentRect.w);");
		const size_t LegacyShellSplit = Source.find("MainView.VSplitRight(TabBarWidth, &MainView, &TabBar);");
		const size_t LegacyShellMargin = Source.find("MainView.Margin(std::clamp(MainView.w * 0.02f, 12.0f, 20.0f), &MainView);");
		const size_t LegacyShellMetrics = Source.find("m_SettingsContentMetrics = ResolveSettingsContentMetrics(MainView.w);");
		const size_t FirstPageRender = Source.find("if(g_Config.m_UiSettingsPage == SETTINGS_GENERAL)");
		ASSERT_NE(NewShellMetrics, std::string::npos);
		ASSERT_NE(LegacyShellSplit, std::string::npos);
		ASSERT_NE(LegacyShellMargin, std::string::npos);
		ASSERT_NE(LegacyShellMetrics, std::string::npos);
		ASSERT_NE(FirstPageRender, std::string::npos);
		EXPECT_LT(NewShellMetrics, LegacyShellSplit);
		EXPECT_LT(LegacyShellSplit, LegacyShellMargin);
		EXPECT_LT(LegacyShellMargin, LegacyShellMetrics);
		EXPECT_LT(LegacyShellMetrics, FirstPageRender);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect)"), std::string::npos);
		EXPECT_NE(Source.find("int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect)"), std::string::npos);
		EXPECT_NE(Source.find("const float BodySize = RequestedFontSize > 0.0f ? RequestedFontSize : CurrentSettingsContentMetrics().m_BodySize;"), std::string::npos);
		EXPECT_EQ(Source.find("SettingsPageUiScale(0.0f)"), std::string::npos);
		EXPECT_EQ(Source.find("SettingsPageUiScale(pRect->w)"), std::string::npos);
		EXPECT_NE(Source.find("ResolveSettingsCheckboxFontSize(BodySize, RequestedFontSize, pRect->h, Box.h, CUi::ms_FontmodHeight)"), std::string::npos);
		EXPECT_NE(Source.find("Props.m_MinimumFontSize = FontSize * 0.7f;"), std::string::npos);
		EXPECT_NE(Source.find("return m_SettingsContentMetrics;"), std::string::npos);
		EXPECT_NE(Source.find("const SMenuTextStyleKey StyleKey = BuildMenuTextStyleKey(&Label, FontSize, TEXTALIGN_ML, Props);"), std::string::npos);
		EXPECT_NE(Source.find("CUIElement &LabelElement = MenuTextElement(MENU_TEXT_SCOPE_SETTINGS, Page, Tab, Subtab, pTextId, StyleKey);"), std::string::npos);
		EXPECT_NE(Source.find("DoButton_CheckBox_Common_WithLabelElement(pId, pText, Checked ? \"X\" : \"\", pRect, BUTTONFLAG_LEFT, &LabelElement, ProcessInput, FontSize);"), std::string::npos);
		EXPECT_NE(Source.find("DoSettingsLabelStreamed(*pLabelElement, &Label, pText, FontSize, TEXTALIGN_ML, Props);"), std::string::npos);
		EXPECT_NE(Source.find("int CMenus::DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, const unsigned Flags, const bool ProcessInput)"), std::string::npos);
		EXPECT_EQ(Source.find("context-checkbox-common-"), std::string::npos);
		EXPECT_EQ(Source.find("m_SettingsTextContextPage >= 0 && pText != nullptr && pText[0] != '\\0'"), std::string::npos);
		EXPECT_NE(Source.find("void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText"), std::string::npos);
		EXPECT_NE(Source.find("if(pText == nullptr)"), std::string::npos);
		EXPECT_NE(Source.find("Ui()->DoLabel(&Label, pText, FontSize, TEXTALIGN_ML, Props);"), std::string::npos);
	}
}

TEST(QmMonitoringHelpers, SettingsTextPlanPrebuildSeparatesInvisibleWarmupFromVisibleRender)
{
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus.h"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("struct SMenuTextPlanItem"), std::string::npos);
		EXPECT_NE(Source.find("std::string m_TextId;"), std::string::npos);
		EXPECT_NE(Source.find("std::string m_Text;"), std::string::npos);
		EXPECT_NE(Source.find("enum EMenuTextStyleMode"), std::string::npos);
		EXPECT_NE(Source.find("MENU_TEXT_STYLE_DEFAULT"), std::string::npos);
		EXPECT_NE(Source.find("MENU_TEXT_STYLE_RECT"), std::string::npos);
		EXPECT_NE(Source.find("MENU_TEXT_STYLE_EXACT"), std::string::npos);
		EXPECT_NE(Source.find("MENU_TEXT_STYLE_ALLOWLIST_DYNAMIC"), std::string::npos);
		EXPECT_NE(Source.find("EMenuTextStyleMode m_StyleMode"), std::string::npos);
		EXPECT_NE(Source.find("std::string m_AllowlistReason;"), std::string::npos);
		EXPECT_NE(Source.find("std::string m_SourceTag;"), std::string::npos);
		EXPECT_EQ(Source.find("bool m_UseExplicitStyleKey = false;"), std::string::npos);
		EXPECT_NE(Source.find("MENU_TEXT_SCOPE_INGAME"), std::string::npos);
		EXPECT_NE(Source.find("SMenuTextPlanItem AddStableTextDefault("), std::string::npos);
		EXPECT_NE(Source.find("SMenuTextPlanItem AddStableTextLabel("), std::string::npos);
		EXPECT_NE(Source.find("SMenuTextPlanItem AddStableTextCheckbox("), std::string::npos);
		EXPECT_NE(Source.find("SMenuTextPlanItem AddStableTextScrollbar("), std::string::npos);
		EXPECT_NE(Source.find("SMenuTextPlanItem AddStableTextButton("), std::string::npos);
		EXPECT_NE(Source.find("void BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems"), std::string::npos);
		EXPECT_NE(Source.find("void BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems"), std::string::npos);
		EXPECT_NE(Source.find("void BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);"), std::string::npos);
		EXPECT_NE(Source.find("void BuildBaseSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);"), std::string::npos);
		EXPECT_NE(Source.find("void BuildSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);"), std::string::npos);
		EXPECT_NE(Source.find("bool PrebuildSettingsTextPlanItem(const SMenuTextPlanItem &Item, int &RemainingBudget);"), std::string::npos);
		EXPECT_NE(Source.find("int CountMissingSettingsMenuTextPlanItems() const;"), std::string::npos);
		EXPECT_NE(Source.find("int PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride = nullptr);"), std::string::npos);
		EXPECT_EQ(Source.find("void PrebuildVisibleSettingsTextPool(const CUIRect &MainView, int Budget);"), std::string::npos);
		EXPECT_TRUE(ContainsAll(Source, {
							"std::vector<SMenuTextPlanItem> m_vSettingsMenuTextPrebuildPlan;",
							"std::unordered_set<std::string> m_SettingsMenuTextPlannedKeys;",
							"size_t m_SettingsMenuTextPlanCursor",
							"uint64_t m_SettingsMenuTextPlanGeneration",
							"SSettingsMenuTextPrebuildStats m_SettingsMenuTextLastPrebuildStats",
							"m_MenuTextStablePlannedThisFrame",
							"m_MenuTextStableUnplannedThisFrame",
							"std::unordered_set<std::string> m_SettingsMenuTextPlannedDescriptors;",
						}));
		EXPECT_NE(Source.find("bool DoSettingsScrollbarOption(int Page, int Tab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale = &CUi::ms_LinearScrollbarScale, unsigned Flags = 0u, const char *pSuffix = \"\", const char *pMaxText = nullptr);"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		const std::string LoadingBody = ExtractSourceFunctionBody(Source, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");

		EXPECT_NE(Source.find("bool CMenus::PrebuildSettingsTextPlanItem(const SMenuTextPlanItem &Item, int &RemainingBudget)"), std::string::npos);
		const std::string PrebuildItemBody = ExtractSourceFunctionBody(Source, "bool CMenus::PrebuildSettingsTextPlanItem(const SMenuTextPlanItem &Item, int &RemainingBudget)");
		EXPECT_NE(Source.find("void CMenus::BuildBaseSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)"), std::string::npos);
		EXPECT_EQ(Source.find("AddDefaultStyleItem("), std::string::npos);
		EXPECT_NE(Source.find("static constexpr int s_aBaseSettingsPages[]"), std::string::npos);
		EXPECT_NE(Source.find("RenderSettings(MainView);"), std::string::npos);
		EXPECT_NE(Source.find("SETTINGS_GENERAL"), std::string::npos);
		EXPECT_NE(Source.find("SETTINGS_DDNET"), std::string::npos);
		EXPECT_EQ(Source.find("AddGeneralItem(\""), std::string::npos);
		EXPECT_EQ(Source.find("AddGeneralCheckbox(\""), std::string::npos);
		EXPECT_EQ(Source.find("AddStableTextDefault(SETTINGS_TEE"), std::string::npos);
		EXPECT_NE(Source.find("void CMenus::BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)"), std::string::npos);
		EXPECT_NE(Source.find("\"ingame-tab-server-info\""), std::string::npos);
		EXPECT_NE(Source.find("RenderMenubar(TabBar, IClient::STATE_ONLINE);"), std::string::npos);
		EXPECT_NE(Source.find("RenderServerInfo(ContentView);"), std::string::npos);
		EXPECT_NE(Source.find("plan_status=%s"), std::string::npos);
		EXPECT_NE(Source.find("planned=%d unplanned=%d"), std::string::npos);
		EXPECT_NE(Source.find("MenuTextDescriptorKey("), std::string::npos);
		EXPECT_NE(Source.find("const bool HasDescriptor = m_SettingsMenuTextPlannedDescriptors.find(MenuTextDescriptorKey("), std::string::npos);
		EXPECT_NE(Source.find("HasDescriptor ? (KeyPlanned ? \"not_built\" : \"key_mismatch\") : \"missing_descriptor\""), std::string::npos);
		EXPECT_NE(Source.find("Box.Margin(2.0f, &Box);\n\tSLabelProperties Props;"), std::string::npos);
		EXPECT_NE(Source.find("void CMenus::BuildSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)"), std::string::npos);
		EXPECT_NE(Source.find("CUIRect SettingsMainView = MenuTextSettingsContentView(Screen);"), std::string::npos);
		EXPECT_NE(Source.find("BuildVisibleSettingsMenuTextPlan(vVisibleItems, SettingsMainView);"), std::string::npos);
		EXPECT_NE(Source.find("BuildIngameMenuTextPlan(vItems, Screen);"), std::string::npos);
		EXPECT_NE(Source.find("int CMenus::CountMissingSettingsMenuTextPlanItems()"), std::string::npos);
		EXPECT_NE(Source.find("It->second.m_Generation != m_MenuTextPoolGeneration"), std::string::npos);
		EXPECT_NE(Source.find("int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)"), std::string::npos);
		EXPECT_EQ(Source.find("void CMenus::PrebuildVisibleSettingsTextPool(const CUIRect &MainView, int Budget)"), std::string::npos);
		ASSERT_FALSE(PrebuildItemBody.empty());
		EXPECT_NE(PrebuildItemBody.find("pRect->m_UITextContainer.Valid()"), std::string::npos);
		EXPECT_NE(PrebuildItemBody.find("Entry.m_Built = true;"), std::string::npos);
		EXPECT_NE(PrebuildItemBody.find("Entry.m_Generation = m_MenuTextPoolGeneration;"), std::string::npos);
		EXPECT_NE(Source.find("bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)"), std::string::npos);
		EXPECT_EQ(Source.find("RenderSettingsTClient(ContentView, true);"), std::string::npos);
		EXPECT_EQ(Source.find("RenderSettingsQmClient(ContentView, false, true);"), std::string::npos);
		EXPECT_EQ(Source.find("PrebuildVisibleSettingsTextPool(ContentView"), std::string::npos);
		ASSERT_FALSE(LoadingBody.empty());
		EXPECT_NE(LoadingBody.find("m_vSettingsMenuTextPrebuildPlan"), std::string::npos);
		EXPECT_NE(LoadingBody.find("m_SettingsMenuTextPlanCursor"), std::string::npos);
		EXPECT_EQ(LoadingBody.find("BuildSettingsMenuTextPlan(vItems);"), std::string::npos);
		EXPECT_EQ(LoadingBody.find("PrebuildSettingsTClientTextPool("), std::string::npos);
		EXPECT_EQ(LoadingBody.find("PrebuildSettingsQmClientTextPool("), std::string::npos);
		EXPECT_EQ(LoadingBody.find("const int LastPage = SettingsCanonicalPage(m_SettingsRuntimeMetadata.m_LastPage);"), std::string::npos);
	}
	{
		const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
		const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
		const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
		const std::string ScrollRegionHeader = ReadRepoFile("src/game/client/ui_scrollregion.h");
		const std::string TClientPlanBody = ExtractSourceFunctionBody(TClient, "void CMenus::BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)");
		const std::string QmClientPlanBody = ExtractSourceFunctionBody(QmClient, "void CMenus::BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)");
		const std::string TClientSettingsBody = ExtractSourceFunctionBody(TClient, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
		EXPECT_NE(TClient.find("void CMenus::BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems"), std::string::npos);
		EXPECT_NE(QmClient.find("void CMenus::BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems"), std::string::npos);
		ASSERT_FALSE(TClientPlanBody.empty());
		ASSERT_FALSE(QmClientPlanBody.empty());
		EXPECT_NE(TClientPlanBody.find("g_Config.m_UiSettingsPage = SETTINGS_TCLIENT;"), std::string::npos);
		EXPECT_NE(QmClientPlanBody.find("g_Config.m_UiSettingsPage = SETTINGS_QMCLIENT;"), std::string::npos);
		EXPECT_NE(TClientPlanBody.find("RenderSettings(MainView);"), std::string::npos);
		EXPECT_NE(QmClientPlanBody.find("RenderSettings(MainView);"), std::string::npos);
		EXPECT_EQ(TClientPlanBody.find("RenderSettingsTClient(MainView, true);"), std::string::npos);
		EXPECT_EQ(QmClientPlanBody.find("RenderSettingsQmClient(MainView, false, true);"), std::string::npos);
		EXPECT_NE(TClient.find("m_MenuTextPlanCollecting = true;"), std::string::npos);
		EXPECT_NE(QmClient.find("m_MenuTextPlanCollecting = true;"), std::string::npos);
		EXPECT_NE(TClient.find("m_pMenuTextPlanCollection = &vItems;"), std::string::npos);
		EXPECT_NE(QmClient.find("m_pMenuTextPlanCollection = &vItems;"), std::string::npos);
		EXPECT_NE(TClient.find("m_MenuTextPlanCollecting = PreviousCollecting;"), std::string::npos);
		EXPECT_NE(QmClient.find("m_MenuTextPlanCollecting = PreviousCollecting;"), std::string::npos);
		ASSERT_FALSE(TClientSettingsBody.empty());
		EXPECT_NE(ScrollRegionHeader.find("void SetContentHeightForNextFrame(float ContentHeight);"), std::string::npos);
		EXPECT_EQ(TClientSettingsBody.find("LogSettingsStage(\"tclient_settings_right_prewarm\", RightColumnTimer);\n\t\t\treturn;"), std::string::npos);
		EXPECT_NE(TClientSettingsBody.find("m_SettingsCardDeck.RenderCached("), std::string::npos);
		EXPECT_NE(TClientSettingsBody.find("s_TClientSettingsScrollRegion"), std::string::npos);
		EXPECT_EQ(TClientSettingsBody.find("BeginSettingsScrollRegion("), std::string::npos);
		EXPECT_EQ(Settings.find("DoButton_CheckBox(&g_Config.m_ClAutoDemoRecord"), std::string::npos);
		EXPECT_EQ(Settings.find("DoButton_CheckBox(&g_Config.m_ClAutoScreenshot"), std::string::npos);
		EXPECT_EQ(Settings.find("DoButton_CheckBox(&g_Config.m_ClAutoStatboardScreenshot"), std::string::npos);
		EXPECT_EQ(Settings.find("DoButton_CheckBox(&g_Config.m_ClAutoCSV"), std::string::npos);
		EXPECT_NE(Settings.find("const bool CollectingMenuTextPlan = m_MenuTextPlanCollecting;"), std::string::npos);
		EXPECT_NE(Settings.find("const bool SettingsPerfEnabled = PerfDebugEnabled() && !CollectingMenuTextPlan;"), std::string::npos);
		EXPECT_NE(Settings.find("if(!CollectingMenuTextPlan && g_Config.m_UiSettingsPage != SETTINGS_ASSETS"), std::string::npos);
		EXPECT_NE(Settings.find("if(!CollectingMenuTextPlan)"), std::string::npos);
		EXPECT_NE(Settings.find("if(!s_SettingsTransitionInitialized)"), std::string::npos);
		EXPECT_NE(Settings.find("if(!CollectingMenuTextPlan && m_SettingsPerfLastPage == g_Config.m_UiSettingsPage)"), std::string::npos);
		EXPECT_NE(Settings.find("m_SettingsPerfLastPage = g_Config.m_UiSettingsPage;"), std::string::npos);
		EXPECT_NE(Settings.find("\"general-auto-demo-record\""), std::string::npos);
		EXPECT_NE(Settings.find("\"general-auto-screenshot\""), std::string::npos);
		EXPECT_NE(Settings.find("\"general-auto-statboard-screenshot\""), std::string::npos);
		EXPECT_NE(Settings.find("\"general-auto-csv\""), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/gameclient.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		const std::string Body = ExtractSourceFunctionBody(Source, "void CGameClient::PrewarmSettingsRuntimeCachesDuringLoading(const char *pLoadingCaption, const char *pLoadingMessage)");

		ASSERT_FALSE(Body.empty());
		EXPECT_NE(Body.find("if(g_Config.m_QmSettingsPrewarm == 0)"), std::string::npos);
		EXPECT_LT(Body.find("if(g_Config.m_QmSettingsPrewarm == 0)"), Body.find("m_Menus.PrewarmSettingsPages();"));
		EXPECT_NE(Body.find("m_Menus.PrewarmSettingsPages();"), std::string::npos);
		EXPECT_NE(Body.find("m_Menus.PrewarmSettingsTextPoolForLoading("), std::string::npos);
		EXPECT_NE(Body.find("SettingsLoadingPrewarmAdvance("), std::string::npos);
		EXPECT_EQ(Body.find("while(SettingsLoadingPrewarmShouldKeepPumping"), std::string::npos);
	}
}

TEST(QmMonitoringHelpers, SettingsStableTextRegistryCoversVisibleWrappers)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Menus.find("AddStableTextLabel("), std::string::npos);
	EXPECT_NE(Menus.find("AddStableTextCheckbox("), std::string::npos);
	EXPECT_NE(Menus.find("AddStableTextScrollbar("), std::string::npos);
	EXPECT_NE(Menus.find("AddStableTextButton("), std::string::npos);
	EXPECT_NE(Menus.find("CollectMenuTextPlanItem("), std::string::npos);
	EXPECT_NE(Menus.find("m_MenuTextPlanCollecting"), std::string::npos);
	const std::string TClientPlanBody = ExtractSourceFunctionBody(TClient, "void CMenus::BuildTClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)");
	const std::string QmClientPlanBody = ExtractSourceFunctionBody(QmClient, "void CMenus::BuildQmClientSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView, int Tab)");
	ASSERT_FALSE(TClientPlanBody.empty());
	ASSERT_FALSE(QmClientPlanBody.empty());
	EXPECT_NE(TClientPlanBody.find("RenderSettings(MainView);"), std::string::npos);
	EXPECT_NE(QmClientPlanBody.find("RenderSettings(MainView);"), std::string::npos);
	EXPECT_EQ(TClientPlanBody.find("RenderSettingsTClient(MainView, true);"), std::string::npos);
	EXPECT_EQ(QmClientPlanBody.find("RenderSettingsQmClient(MainView, false, true);"), std::string::npos);
	EXPECT_NE(TClient.find("const bool PreviousCollecting = m_MenuTextPlanCollecting;"), std::string::npos);
	EXPECT_NE(QmClient.find("const bool PreviousCollecting = m_MenuTextPlanCollecting;"), std::string::npos);
	EXPECT_NE(TClient.find("m_MenuTextPlanCollecting = true;"), std::string::npos);
	EXPECT_NE(QmClient.find("m_MenuTextPlanCollecting = true;"), std::string::npos);
	EXPECT_NE(TClient.find("m_MenuTextPlanCollecting = PreviousCollecting;"), std::string::npos);
	EXPECT_NE(QmClient.find("m_MenuTextPlanCollecting = PreviousCollecting;"), std::string::npos);
	EXPECT_EQ(TClient.find("auto AddItem = [&]"), std::string::npos);
	EXPECT_EQ(QmClient.find("auto AddItem = [&]"), std::string::npos);
	EXPECT_EQ(Menus.find("Item.m_UseExplicitStyleKey"), std::string::npos);
	EXPECT_NE(Menus.find("CMenus::SMenuTextStyleKey CMenus::SettingsMenuTextPlanStyleKey(const SMenuTextPlanItem &Item) const"), std::string::npos);
	EXPECT_NE(Menus.find("switch(Item.m_StyleMode)"), std::string::npos);

	const std::vector<std::string> vRequiredBaseIds = {
		"\"deck:general-game\"",
		"\"deck:general-language\"",
		"\"deck:general-client\"",
		"\"tee-name-label\"",
		"\"tee-clan-label\"",
		"\"ddnet-run-on-join-label\"",
		"\"Save the best demo of each race\"",
		"\"Enable replays\"",
		"\"Enable ghost\"",
		"\"Show text entities\"",
		"\"Show others\"",
		"\"Show others (own team only)\"",
		"\"Show background quads\"",
		"\"Predict events (experimental)\"",
		"\"AntiPing (latency compensation)\"",
		"\"Use current map as background\"",
		"\"Show tiles layers from BG map\"",
	};
	for(const std::string &Id : vRequiredBaseIds)
	{
		EXPECT_NE(Settings.find(Id), std::string::npos) << Id;
	}
	EXPECT_NE(Menus.find("static constexpr int s_aBaseSettingsPages[]"), std::string::npos);
	EXPECT_NE(Menus.find("RenderSettings(MainView);"), std::string::npos);
	EXPECT_EQ(Menus.find("AddStableTextDefault(SETTINGS_TEE"), std::string::npos);
	EXPECT_EQ(Menus.find("AddGeneralCheckbox(\""), std::string::npos);

	const std::vector<std::string> vRequiredTClientIds = {
		"\"tclient-outline-width\"",
		"\"tclient-statusbar-local-time-title\"",
		"\"tclient-statusbar-colors-title\"",
		"\"tclient-statusbar-empty-preview\"",
		"\"tclient-statusbar-scheme-label\"",
		"\"tclient-statusbar-show\"",
		"\"tclient-statusbar-show-labels\"",
		"\"tclient-statusbar-height\"",
		"\"tclient-statusbar-12-hour-clock\"",
		"\"tclient-statusbar-seconds\"",
		"\"tclient-statusbar-alpha\"",
		"\"tclient-statusbar-text-alpha\"",
		"\"tclient-statusbar-apply-scheme\"",
		"\"tclient-statusbar-add-item\"",
		"\"tclient-statusbar-remove-item\"",
	};
	for(const std::string &Id : vRequiredTClientIds)
	{
		EXPECT_NE(TClient.find(Id), std::string::npos) << Id;
	}
	EXPECT_EQ(TClient.find("AddStableTextScrollbar(SETTINGS_TCLIENT"), std::string::npos);
	EXPECT_EQ(TClient.find("AddStableTextCheckbox(SETTINGS_TCLIENT"), std::string::npos);
	EXPECT_EQ(TClient.find("AddStableTextButton(SETTINGS_TCLIENT"), std::string::npos);

	const std::vector<std::string> vRequiredQmClientIds = {
		"\"qmclient-chat-bubble-duration\"",
		"\"qmclient-chat-bubble-opacity\"",
		"\"qmclient-display-mode\"",
		"\"qmclient-translation-service\"",
		"\"qmclient-target-language\"",
		"\"qmclient-llm-provider\"",
	};
	for(const std::string &Id : vRequiredQmClientIds)
	{
		EXPECT_NE(QmClient.find(Id), std::string::npos) << Id;
	}
	EXPECT_EQ(QmClient.find("AddStableText"), std::string::npos);

	const std::vector<std::string> vRequiredIngameIds = {
		"\"ingame-tab-game\"",
		"\"ingame-tab-players\"",
		"\"ingame-tab-server-info\"",
		"\"ingame-tab-browser\"",
		"\"ingame-tab-ghost\"",
		"\"ingame-tab-call-vote\"",
		"\"ingame-server-info-title\"",
		"\"ingame-server-info-address-label\"",
		"\"ingame-server-info-ping-label\"",
		"\"ingame-server-info-version-label\"",
		"\"ingame-server-info-password-label\"",
		"\"ingame-server-info-community-label\"",
		"\"ingame-game-info-title\"",
		"\"ingame-game-info-type-label\"",
		"\"ingame-game-info-map-label\"",
		"\"ingame-game-info-players-label\"",
		"\"ingame-server-info-motd-title\"",
	};
	for(const std::string &Id : vRequiredIngameIds)
	{
		EXPECT_TRUE(Menus.find(Id) != std::string::npos || Ingame.find(Id) != std::string::npos) << Id;
	}
	EXPECT_NE(Menus.find("DoIngameMenuTab("), std::string::npos);
	EXPECT_NE(Menus.find("BuildIngameMenuTextPlan(vItems, Screen);"), std::string::npos);
	EXPECT_NE(Menus.find("RenderMenubar(TabBar, IClient::STATE_ONLINE);"), std::string::npos);
	EXPECT_NE(Menus.find("RenderServerInfo(ContentView);"), std::string::npos);
	EXPECT_EQ(Menus.find("AddIngameTab("), std::string::npos);
	EXPECT_NE(Menus.find("return DoMenuTabV2(pButtonContainer, pText, Checked != 0, pRect, Corners, nullptr, nullptr, nullptr, nullptr, &TextElement, ContentScale);"), std::string::npos);
	EXPECT_EQ(Menus.find("DoMenuTabV2(&s_ServerInfoButton, Localize(\"Server info\")"), std::string::npos);
	EXPECT_NE(Ingame.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-server-info-title\""), std::string::npos);
	EXPECT_NE(Ingame.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-game-info-title\""), std::string::npos);
	EXPECT_NE(Ingame.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-server-info-motd-title\""), std::string::npos);
	EXPECT_EQ(Ingame.find("DoIngameMenuLabel(PAGE_SERVER_INFO, \"ingame-server-info-name\""), std::string::npos);
	EXPECT_EQ(Ingame.find("DoIngameMenuLabel(PAGE_SERVER_INFO, \"ingame-game-info-map\", &Label, aBuf"), std::string::npos);
	const size_t OnlineBranch = Menus.find("case IClient::STATE_ONLINE:");
	ASSERT_NE(OnlineBranch, std::string::npos);
	const size_t IngameGuard = Menus.find("TextVisibleGuard.emplace(this);", OnlineBranch);
	const size_t IngameContent = Menus.find("if(m_GamePage == PAGE_GAME)", OnlineBranch);
	const size_t IngameSettings = Menus.find("RenderSettings(MainView);", IngameContent);
	const size_t IngameMenubar = Menus.find("RenderMenubar(TabBar, ClientState);", OnlineBranch);
	ASSERT_NE(IngameGuard, std::string::npos);
	ASSERT_NE(IngameContent, std::string::npos);
	ASSERT_NE(IngameSettings, std::string::npos);
	ASSERT_NE(IngameMenubar, std::string::npos);
	EXPECT_LT(IngameGuard, IngameContent);
	EXPECT_LT(IngameGuard, IngameSettings);
	EXPECT_LT(IngameGuard, IngameMenubar);
	const size_t OfflineBranch = Menus.find("case IClient::STATE_OFFLINE:");
	ASSERT_NE(OfflineBranch, std::string::npos);
	const size_t OfflineGuard = Menus.find("std::optional<CScopedMenuTextVisibleGuard> TextVisibleGuard;", OfflineBranch);
	ASSERT_NE(OfflineGuard, std::string::npos);
	const size_t OfflineContent = Menus.find("CPerfTimer ContentTimer;", OfflineGuard);
	ASSERT_NE(OfflineContent, std::string::npos);
	const std::string OfflineGuardBody = Menus.substr(OfflineGuard, OfflineContent - OfflineGuard);
	EXPECT_NE(OfflineGuardBody.find("if(m_MenuPage == PAGE_SETTINGS)"), std::string::npos);
	EXPECT_EQ(OfflineGuardBody.find("if(m_GamePage != PAGE_SETTINGS)"), std::string::npos);
	EXPECT_EQ(Menus.find("Props.m_MaxWidth = Width;"), std::string::npos);
	EXPECT_EQ(Menus.find("CUIRect{0.0f, 0.0f, Width, Height - 4.0f}"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsStableTextPlanKeysMatchVisibleWrappers)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");

	const std::string ScrollbarBody = ExtractSourceFunctionBody(Menus, "CMenus::SMenuTextPlanItem CMenus::AddStableTextScrollbar(int Page, int Tab, int Subtab, const char *pTextId, const char *pText, const CUIRect &Rect, unsigned Flags, const char *pSourceTag) const");
	ASSERT_FALSE(ScrollbarBody.empty());
	EXPECT_NE(ScrollbarBody.find("ui_widget::SliderInputFieldLabelRect("), std::string::npos);
	EXPECT_NE(ScrollbarBody.find("BuildMenuTextStyleKey("), std::string::npos);

	const std::string ScrollbarOptionBody = ExtractSourceFunctionBody(Menus, "bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, int Subtab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)");
	const std::string NumericLabelBridge = ExtractSourceFunctionBody(Menus, "bool CMenus::PrepareSettingsNumericFieldLabel(");
	ASSERT_FALSE(ScrollbarOptionBody.empty());
	ASSERT_FALSE(NumericLabelBridge.empty());
	EXPECT_NE(ScrollbarOptionBody.find("PrepareSettingsNumericFieldLabel("), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("ui_widget::SliderInputFieldLabelRect("), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("BuildMenuTextStyleKey("), std::string::npos);
	EXPECT_NE(NumericLabelBridge.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_SETTINGS"), std::string::npos);
	EXPECT_EQ(ScrollbarOptionBody.find("DoSettingsMenuLabel(Page, Tab, Subtab, pTextId, &Label, pStr, FontSize, TEXTALIGN_ML, {}, (int)Label.w);"), std::string::npos);
	const std::string SplitScrollbarBody = ExtractSourceFunctionBody(Menus, "void CMenus::SplitSettingsScrollbarRects(const CUIRect &Rect, unsigned Flags, CUIRect *pLabelRect, CUIRect *pValueRect, CUIRect *pScrollBarRect) const");
	ASSERT_FALSE(SplitScrollbarBody.empty());
	EXPECT_NE(SplitScrollbarBody.find("const float ValueWidth = std::clamp(Rect.w * 0.12f, 42.0f, 68.0f);"), std::string::npos);
	EXPECT_NE(SplitScrollbarBody.find("const float LabelWidth = std::clamp(Rect.w * 0.25f, 108.0f, 180.0f);"), std::string::npos);
	EXPECT_NE(SplitScrollbarBody.find("Controls.VSplitRight(ValueWidth, &ScrollBar, &ValueText);"), std::string::npos);
	EXPECT_NE(SplitScrollbarBody.find("ScrollBar.VMargin(minimum(10.0f, Rect.w * 0.025f), &ScrollBar);"), std::string::npos);

	EXPECT_EQ(Header.find("BuildSettingsScrollbarTextStyle("), std::string::npos);
	EXPECT_NE(Settings.find("DoAppearanceNumericField(APPEARANCE_TAB_HUD, \"appearance-freeze-bars-alpha-inside-freeze\""), std::string::npos);
	EXPECT_NE(Header.find("SMenuTextStyleKey BuildSettingsShellTitleTextStyle(const CUIRect &Rect, CUIRect *pOutLabel = nullptr) const;"), std::string::npos);
	EXPECT_NE(Menus.find("BuildSettingsShellTitleTextStyle("), std::string::npos);
	EXPECT_NE(Menus.find("settings-shell-title"), std::string::npos);
	EXPECT_NE(Settings.find("DoDDNetNumericField(\"ddnet-default-zoom\""), std::string::npos);
	EXPECT_NE(Settings.find("DoSettingsButton_CheckBox(SETTINGS_DDNET, -1, &g_Config.m_ClRaceGhost, \"Enable ghost\""), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsStableTextPrebuildCompletesTargetPlan)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string CountBody = ExtractSourceFunctionBody(Menus, "int CMenus::CountMissingSettingsMenuTextPlanItems()");
	const std::string LogBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrebuildSettingsMenuTextPool(int Budget, const char *pScopeOverride, const char *pOperationOverride)");

	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(CountBody.empty());
	ASSERT_FALSE(LogBody.empty());
	EXPECT_NE(Header.find("int CountMissingSettingsMenuTextPlanItems() const;"), std::string::npos);
	EXPECT_NE(CountBody.find("m_vSettingsMenuTextPrebuildPlan"), std::string::npos);
	EXPECT_EQ(CountBody.find("BuildSettingsMenuTextPlan(vItems);"), std::string::npos);
	EXPECT_NE(PrebuildBody.find("m_SettingsMenuTextLastPrebuildStats.m_Remaining = CountMissingSettingsMenuTextPlanItems();"), std::string::npos);
	EXPECT_NE(LogBody.find("const int RemainingMissing = m_SettingsMenuTextLastPrebuildStats.m_Remaining;"), std::string::npos);
	EXPECT_EQ(LogBody.find("const int RemainingMissing = CountMissingSettingsMenuTextPlanItems();"), std::string::npos);
	EXPECT_EQ(CountBody.find("for(const SMenuTextPlanItem &Item : m_vSettingsMenuTextPrebuildPlan)"), std::string::npos);
	EXPECT_NE(CountBody.find("m_SettingsMenuTextPlanCollectionComplete"), std::string::npos);
	EXPECT_NE(CountBody.find("m_vSettingsMenuTextPrebuildPlan.size() - (int)m_SettingsMenuTextPlanCursor"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameEscStableTextRegistryCoversMenubar)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string PlanBody = ExtractSourceFunctionBody(Menus, "void CMenus::BuildIngameMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView)");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string CollectionBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	ASSERT_FALSE(PlanBody.empty());
	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(CollectionBody.empty());

	EXPECT_NE(Header.find("void PrebuildIngameEscTextPoolBeforeOpen(int Budget);"), std::string::npos);
	EXPECT_NE(Menus.find("PrebuildIngameEscTextPoolBeforeOpen("), std::string::npos);
	EXPECT_EQ(Menus.find("PrebuildIngameEscTextPoolBeforeOpen(96);"), std::string::npos);
	EXPECT_NE(CollectionBody.find("const bool IngameEscOperation = str_comp(pOperation, \"ingame_esc_open\") == 0;"), std::string::npos);
	EXPECT_NE(CollectionBody.find("if(IngameEscOperation)"), std::string::npos);
	EXPECT_NE(Menus.find("BuildIngameMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, Screen);"), std::string::npos);
	EXPECT_NE(PrebuildBody.find("AdvanceSettingsMenuTextPlanCollection("), std::string::npos);
	for(const char *pId : {
		    "\"ingame-tab-game\"",
		    "\"ingame-tab-players\"",
		    "\"ingame-tab-server-info\"",
		    "\"ingame-tab-browser\"",
		    "\"ingame-tab-ghost\"",
		    "\"ingame-tab-call-vote\"",
	    })
	{
		EXPECT_NE(PlanBody.find(pId), std::string::npos) << pId;
	}
	EXPECT_EQ(Menus.find("plan_status\":\"missing_descriptor\""), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsOpenSkipsIngamePlanWhileEscCollectsIncrementally)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string CollectionBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(CollectionBody.empty());

	EXPECT_NE(CollectionBody.find("const bool IngameEscOperation = str_comp(pOperation, \"ingame_esc_open\") == 0;"), std::string::npos);
	EXPECT_NE(CollectionBody.find("if(IngameEscOperation)"), std::string::npos);
	EXPECT_NE(CollectionBody.find("MENU_TEXT_PLAN_UNIT_INGAME_ESC"), std::string::npos);
	EXPECT_NE(PrebuildBody.find("AdvanceSettingsMenuTextPlanCollection("), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("BuildSettingsMenuTextPlan(vItems, SettingsMainView);"), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("MENU_TEXT_PLAN_UNIT_INGAME_ESC"), std::string::npos);
}

TEST(QmMonitoringHelpers, LoadingAndEscPrebuildDoNotSynchronouslyBuildFullSettingsPlan)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string EscBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrebuildIngameEscTextPoolBeforeOpen(int Budget)");
	const std::string EnsureBody = ExtractSourceFunctionBody(Menus, "void CMenus::EnsureSettingsMenuTextPlanReadyForVisible()");
	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(EscBody.empty());
	ASSERT_FALSE(EnsureBody.empty());

	EXPECT_NE(Header.find("void BuildVisibleSettingsMenuTextPlan(std::vector<SMenuTextPlanItem> &vItems, CUIRect MainView);"), std::string::npos);
	EXPECT_NE(PrebuildBody.find("AdvanceSettingsMenuTextPlanCollection("), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("BuildSettingsMenuTextPlan(vItems, SettingsMainView);"), std::string::npos);
	EXPECT_EQ(EscBody.find("BuildSettingsMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, SettingsMainView);"), std::string::npos);
	EXPECT_NE(EscBody.find("PrebuildSettingsMenuTextPool(minimum(Budget, maximum(1, AdaptiveBudget.m_TextPrebuildTokens)), \"target_settings\", \"ingame_esc_open\");"), std::string::npos);
	EXPECT_NE(EnsureBody.find("BuildVisibleSettingsMenuTextPlan(vVisibleItems, SettingsMainView);"), std::string::npos);
	EXPECT_EQ(EnsureBody.find("BuildSettingsMenuTextPlan(m_vSettingsMenuTextPrebuildPlan);"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameEscPrewarmsStableTextBeforeVisibleFrame)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string CollectionBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)");
	const std::string CollectBody = ExtractSourceFunctionBody(Menus, "void CMenus::CollectSettingsMenuTextPlanUnit(const SSettingsMenuTextPlanCollectionUnit &Unit, CUIRect Screen, CUIRect SettingsMainView)");
	ASSERT_FALSE(OnRenderBody.empty());
	ASSERT_FALSE(CollectionBody.empty());
	ASSERT_FALSE(CollectBody.empty());

	const size_t StartWindowPos = OnRenderBody.find("StartSettingsPerfFixedWindow(\"ingame_esc_open\"");
	const size_t SetActivePos = OnRenderBody.find("SetActive(true);", StartWindowPos);
	const size_t RenderPos = OnRenderBody.find("Render();");
	ASSERT_NE(StartWindowPos, std::string::npos);
	ASSERT_NE(SetActivePos, std::string::npos);
	ASSERT_NE(RenderPos, std::string::npos);
	EXPECT_LT(StartWindowPos, SetActivePos);
	EXPECT_LT(SetActivePos, RenderPos);

	EXPECT_NE(Header.find("uint64_t m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(Header.find("bool m_IngameServerInfoBackgroundPrepareRequested"), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("PrebuildIngameEscTextPoolBeforeOpen("), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("PrebuildSettingsMenuTextPool("), std::string::npos);
	EXPECT_NE(OnRenderBody.find("Client()->PerfFrame() > m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("PrepareIngameServerInfoTextRuntime();"), std::string::npos);
	EXPECT_NE(CollectionBody.find("m_vSettingsMenuTextPlanCollectionUnits.push_back({MENU_TEXT_PLAN_UNIT_INGAME_ESC, -1, -1});"), std::string::npos);
	EXPECT_NE(CollectBody.find("case MENU_TEXT_PLAN_UNIT_INGAME_ESC:"), std::string::npos);
	EXPECT_NE(CollectBody.find("BuildIngameMenuTextPlan(m_vSettingsMenuTextPrebuildPlan, Screen);"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsMenuTextPlanCollectionUsesIncrementalCursor)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string EscBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrebuildIngameEscTextPoolBeforeOpen(int Budget)");
	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(EscBody.empty());

	EXPECT_NE(Header.find("struct SSettingsMenuTextPlanCollectionUnit"), std::string::npos);
	EXPECT_NE(Header.find("struct SSettingsMenuTextPlanCollectionStats"), std::string::npos);
	EXPECT_NE(Header.find("std::vector<SSettingsMenuTextPlanCollectionUnit> m_vSettingsMenuTextPlanCollectionUnits;"), std::string::npos);
	EXPECT_NE(Header.find("size_t m_SettingsMenuTextPlanCollectionCursor"), std::string::npos);
	EXPECT_NE(Header.find("uint64_t m_SettingsMenuTextPlanCollectionGeneration"), std::string::npos);
	EXPECT_NE(Header.find("bool m_SettingsMenuTextPlanCollectionDirty"), std::string::npos);
	EXPECT_NE(Header.find("bool m_SettingsMenuTextPlanCollectionComplete"), std::string::npos);
	EXPECT_NE(Header.find("SSettingsMenuTextPlanCollectionStats m_SettingsMenuTextLastCollectionStats"), std::string::npos);
	EXPECT_NE(Header.find("int SettingsTextPlanCollectionRemaining() const"), std::string::npos);

	EXPECT_NE(Menus.find("void CMenus::PrepareSettingsMenuTextPlanCollectionUnits(const char *pOperationOverride)"), std::string::npos);
	EXPECT_NE(Menus.find("bool CMenus::AdvanceSettingsMenuTextPlanCollection(int Budget, const char *pOperationOverride)"), std::string::npos);
	EXPECT_NE(Menus.find("void CMenus::CollectSettingsMenuTextPlanUnit(const SSettingsMenuTextPlanCollectionUnit &Unit, CUIRect Screen, CUIRect SettingsMainView)"), std::string::npos);
	EXPECT_NE(Menus.find("event=settings_text_plan_collection"), std::string::npos);
	EXPECT_NE(Menus.find("units_done=%d units_total=%d remaining=%d budget=%d complete=%d dirty=%d phase=%s scope=%s operation=%s"), std::string::npos);

	EXPECT_NE(PrebuildBody.find("AdvanceSettingsMenuTextPlanCollection("), std::string::npos);
	EXPECT_LT(PrebuildBody.find("AdvanceSettingsMenuTextPlanCollection("), PrebuildBody.find("while(m_SettingsMenuTextPlanCursor < m_vSettingsMenuTextPrebuildPlan.size())"));
	EXPECT_EQ(PrebuildBody.find("BuildSettingsMenuTextPlan(vItems, SettingsMainView);"), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("BuildSettingsMenuTextPlan(vItems);"), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("BuildBaseSettingsMenuTextPlan(vItems, SettingsMainView);"), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("for(int Tab = 0; Tab < NumTClientTextPlanTabs; ++Tab)"), std::string::npos);
	EXPECT_EQ(PrebuildBody.find("for(int Tab = 0; Tab < NUMBER_OF_QMCLIENT_SETTINGS_TABS; ++Tab)"), std::string::npos);

	EXPECT_EQ(EscBody.find("BuildSettingsMenuTextPlan("), std::string::npos);
	EXPECT_EQ(EscBody.find("BuildSettingsMenuTextPlan("), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsPlanCollectionDoesNotSynchronouslyPumpStartupOrEsc)
{
	const std::string GameClient = ReadRepoFile("src/game/client/gameclient.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrewarmBody = ExtractSourceFunctionBody(GameClient, "void CGameClient::PrewarmSettingsRuntimeCachesDuringLoading(const char *pLoadingCaption, const char *pLoadingMessage)");
	const std::string RenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string EscBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrebuildIngameEscTextPoolBeforeOpen(int Budget)");
	ASSERT_FALSE(PrewarmBody.empty());
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(EscBody.empty());

	EXPECT_NE(PrewarmBody.find("constexpr int TEXT_PREWARM_BUDGET_PER_STEP = 8;"), std::string::npos);
	EXPECT_NE(PrewarmBody.find("m_Menus.PrewarmSettingsTextPoolForLoading(TEXT_PREWARM_BUDGET_PER_STEP);"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("while(SettingsLoadingPrewarmShouldKeepPumping"), std::string::npos);
	EXPECT_EQ(PrewarmBody.find("MAX_PREWARM_ATTEMPTS = 128"), std::string::npos);

	EXPECT_EQ(RenderBody.find("PrebuildIngameEscTextPoolBeforeOpen(96);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("PrebuildIngameEscTextPoolBeforeOpen(4);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("PrebuildIngameEscTextPoolBeforeOpen(3);"), std::string::npos);
	EXPECT_NE(RenderBody.find("StartSettingsPerfFixedWindow(\"ingame_esc_open\""), std::string::npos);
	EXPECT_EQ(EscBody.find("BuildSettingsMenuTextPlan("), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsPlanCollectionDoesNotEnterVisibleGuard)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsBody = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(RenderSettingsBody.empty());

	EXPECT_NE(RenderSettingsBody.find("std::optional<CScopedMenuTextVisibleGuard> TextVisibleGuard;"), std::string::npos);
	const size_t GuardCondition = RenderSettingsBody.find("if(!CollectingMenuTextPlan)");
	const size_t GuardEmplace = RenderSettingsBody.find("TextVisibleGuard.emplace(this);");
	ASSERT_NE(GuardCondition, std::string::npos);
	ASSERT_NE(GuardEmplace, std::string::npos);
	EXPECT_LT(GuardCondition, GuardEmplace);
	EXPECT_EQ(RenderSettingsBody.find("const CScopedMenuTextVisibleGuard TextVisibleGuard(this);"), std::string::npos);
}

TEST(QmMonitoringHelpers, VisibleGuardKeepsPlanMetadataAfterInvalidation)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string InvalidateBody = ExtractSourceFunctionBody(Menus, "void CMenus::InvalidateMenuTextPool(const char *pReason)");
	const std::string EnsureBody = ExtractSourceFunctionBody(Menus, "void CMenus::EnsureSettingsMenuTextPlanReadyForVisible()");
	ASSERT_FALSE(InvalidateBody.empty());
	ASSERT_FALSE(EnsureBody.empty());

	EXPECT_NE(Header.find("void EnsureSettingsMenuTextPlanReadyForVisible();"), std::string::npos);
	EXPECT_NE(InvalidateBody.find("if(!m_MenuTextPoolVisibleGuard)"), std::string::npos);
	EXPECT_NE(InvalidateBody.find("m_SettingsMenuTextPlanMetadataDirty = true;"), std::string::npos);
	EXPECT_NE(EnsureBody.find("std::vector<SMenuTextPlanItem> vVisibleItems;"), std::string::npos);
	EXPECT_NE(EnsureBody.find("BuildVisibleSettingsMenuTextPlan(vVisibleItems, SettingsMainView);"), std::string::npos);
	EXPECT_NE(EnsureBody.find("m_SettingsMenuTextPlannedDescriptors.insert"), std::string::npos);
	EXPECT_NE(EnsureBody.find("m_SettingsMenuTextPlannedKeys.insert"), std::string::npos);
	EXPECT_NE(EnsureBody.find("m_vSettingsMenuTextPrebuildPlan.clear();"), std::string::npos);
	EXPECT_EQ(EnsureBody.find("m_vSettingsMenuTextPlanCollectionUnits.clear();"), std::string::npos);
	EXPECT_EQ(EnsureBody.find("m_SettingsMenuTextPlanCollectionDirty = true;"), std::string::npos);
	EXPECT_NE(Menus.find("EnsureSettingsMenuTextPlanReadyForVisible();\n\tm_pMenus->m_MenuTextPoolVisibleGuard = true;"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoBrowserStartupDoesNotSynchronouslyFetchAllHeaders)
{
	const std::string MenusDemo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string RenderListBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	const std::string PopulateBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::DemolistPopulate()");
	ASSERT_FALSE(RenderListBody.empty());
	ASSERT_FALSE(PopulateBody.empty());

	EXPECT_NE(RenderListBody.find("DemolistPopulate();"), std::string::npos);
	EXPECT_NE(RenderListBody.find("DemolistOnUpdate(true);"), std::string::npos);
	EXPECT_EQ(PopulateBody.find("FetchAllHeaders();"), std::string::npos);
	EXPECT_EQ(PopulateBody.find("EnsureAllDemoDates();"), std::string::npos);
	EXPECT_NE(MenusDemo.find("AdvanceDemoBrowserMetadata("), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_startup"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoBrowserFetchInfoUsesBoundedProgress)
{
	const std::string MenusDemo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string ButtonsBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated)");
	const std::string SortListBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	const std::string DetailsBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserDetails(CUIRect DetailsView)");
	const std::string FetchAllBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::FetchAllHeaders()");
	const std::string EnsureDatesBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::EnsureAllDemoDates()");
	ASSERT_FALSE(ButtonsBody.empty());
	ASSERT_FALSE(SortListBody.empty());
	ASSERT_FALSE(DetailsBody.empty());
	ASSERT_FALSE(FetchAllBody.empty());
	ASSERT_FALSE(EnsureDatesBody.empty());

	EXPECT_NE(ButtonsBody.find("g_Config.m_BrDemoFetchInfo"), std::string::npos);
	EXPECT_EQ(ButtonsBody.find("FetchAllHeaders();"), std::string::npos);
	EXPECT_EQ(SortListBody.find("EnsureAllDemoDates();"), std::string::npos);
	EXPECT_EQ(FetchAllBody.find("for(auto &Item : m_vDemos)"), std::string::npos);
	EXPECT_EQ(EnsureDatesBody.find("for(auto &Item : m_vDemos)"), std::string::npos);
	EXPECT_NE(FetchAllBody.find("AdvanceDemoBrowserMetadata(2, g_Config.m_BrDemoSort == SORT_DATE ? 4 : 0, \"fetch_info\");"), std::string::npos);
	EXPECT_NE(EnsureDatesBody.find("AdvanceDemoBrowserMetadata(0, maximum(1, AdaptiveBudget.m_DemoMetadataTokens), \"ensure_dates\");"), std::string::npos);
	EXPECT_EQ(SortListBody.find("if(EnsureDemoDate(*pItem))"), std::string::npos);
	EXPECT_NE(SortListBody.find("if(pItem->m_DateLoaded && pItem->m_DateValid)"), std::string::npos);
	EXPECT_EQ(DetailsBody.find("!FetchHeader(*pItem)"), std::string::npos);
	EXPECT_NE(DetailsBody.find("!pItem->m_InfosLoaded"), std::string::npos);
	EXPECT_NE(Header.find("size_t m_DemoHeaderFetchCursor"), std::string::npos);
	EXPECT_NE(Header.find("size_t m_DemoDateFetchCursor"), std::string::npos);
	EXPECT_NE(Header.find("bool m_DemoHeaderFetchComplete"), std::string::npos);
	EXPECT_NE(Header.find("bool m_DemoDateFetchComplete"), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_header_fetch"), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_date_fetch"), std::string::npos);
	EXPECT_NE(MenusDemo.find("event=demo_browser_preview_load"), std::string::npos);
	EXPECT_NE(MenusDemo.find("metadata_remaining=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoBrowserMetadataPrioritizesVisibleWindow)
{
	const std::string MenusDemo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string AdvanceBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::AdvanceDemoBrowserMetadata(int HeaderBudget, int DateBudget, const char *pTrigger, int VisibleFirst, int VisibleEnd)");
	const std::string RenderListBody = ExtractSourceFunctionBody(MenusDemo, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	ASSERT_FALSE(AdvanceBody.empty());
	ASSERT_FALSE(RenderListBody.empty());

	EXPECT_NE(Header.find("void AdvanceDemoBrowserMetadata(int HeaderBudget, int DateBudget, const char *pTrigger, int VisibleFirst = -1, int VisibleEnd = -1);"), std::string::npos);
	EXPECT_NE(RenderListBody.find("int FirstVisibleIndex = -1;"), std::string::npos);
	EXPECT_NE(RenderListBody.find("EndVisibleIndex = ItemIndex + 1;"), std::string::npos);
	EXPECT_NE(RenderListBody.find("\"list_frame\",\n\t\tFirstVisibleIndex,\n\t\tEndVisibleIndex);"), std::string::npos);
	EXPECT_LT(AdvanceBody.find("for(int i = VisibleFirst; i < VisibleEnd && RemainingBudget > 0; ++i)"), AdvanceBody.find("while(m_DemoHeaderFetchCursor < m_vDemos.size() && RemainingBudget > 0)"));
	EXPECT_LT(AdvanceBody.find("for(int i = VisibleFirst; i < VisibleEnd && RemainingBudget > 0; ++i)", AdvanceBody.find("if(!m_DemoDateFetchComplete")), AdvanceBody.find("while(m_DemoDateFetchCursor < m_vDemos.size() && RemainingBudget > 0)"));
	EXPECT_NE(AdvanceBody.find("std::stable_sort(m_vDemos.begin(), m_vDemos.end());\n\t\t\tDemolistOnUpdate(false);"), std::string::npos);
	EXPECT_NE(AdvanceBody.find("visible_scanned=%d"), std::string::npos);
	EXPECT_NE(AdvanceBody.find("background_scanned=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsAssetsPreviewAdmissionPrefersCombinedVisibleWindow)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("struct SSettingsAssetsVisibleAdmission"), std::string::npos);
	EXPECT_NE(Body.find("SSettingsAssetsVisibleAdmission CombinedVisibleAdmission"), std::string::npos);
	EXPECT_NE(Body.find("visible_first=1"), std::string::npos);
	EXPECT_NE(Body.find("visible_starts=%d prefetch_starts=%d background_starts=%d"), std::string::npos);
	EXPECT_LT(Body.find("SSettingsAssetsVisibleAdmission CombinedVisibleAdmission"), Body.find("StartWorkshopThumb("));
	EXPECT_LT(Body.find("SSettingsAssetsVisibleAdmission CombinedVisibleAdmission"), Body.find("SchedulePreviewRange("));
	EXPECT_EQ(Body.find("StartWorkshopThumb(Asset, SettingsWorkshopThumbShouldStartHighPriority(VisibleDownloadableIndex, FirstVisibleDownloadableIndex, LastVisibleDownloadableIndex));"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsAssetsVisibleReadyPreflightPrecedesDraw)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("enum class EAssetsVisiblePreflightState"), std::string::npos);
	EXPECT_NE(Source.find("struct SSettingsAssetsVisiblePreflight"), std::string::npos);
	EXPECT_NE(Body.find("PrepareAssetsVisibleContentBudgeted("), std::string::npos);
	EXPECT_EQ(Body.find("RenderAssetsVisibleReadySkeleton("), std::string::npos);
	EXPECT_NE(Body.find("stage=assets_visible_preflight"), std::string::npos);
	EXPECT_NE(Body.find("stage=assets_card_geometry"), std::string::npos);
	EXPECT_NE(Body.find("visible_ready=%d geometry_stable=%d thumb_starts_before_visible=%d thumb_starts_during_draw=%d"), std::string::npos);
	EXPECT_NE(Body.find("MaxWorkshopThumbJumpStartsPerFrame"), std::string::npos);
	EXPECT_NE(Body.find("const int WorkshopThumbStartLimitThisFrame = WorkshopListJumpScrollActive ? MaxWorkshopThumbJumpStartsPerFrame : MaxWorkshopThumbStartsPerFrameAdaptive;"), std::string::npos);
	EXPECT_LT(Body.find("PrepareAssetsVisibleContentBudgeted("), Body.find("for(size_t ListIndex = WorkshopVisibleRange.m_FirstItem"));
	const size_t DrawLoop = Body.find("for(size_t ListIndex = WorkshopVisibleRange.m_FirstItem");
	ASSERT_NE(DrawLoop, std::string::npos);
	const size_t DrawLog = Body.find("assets_preview_draw_workshop_cards", DrawLoop);
	ASSERT_NE(DrawLog, std::string::npos);
	const std::string DrawLoopBody = Body.substr(DrawLoop, DrawLog - DrawLoop);
	EXPECT_EQ(DrawLoopBody.find("StartWorkshopThumb("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsUsesAdaptiveBudgetForPreviewAndThumbWork)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("BeginSettingsUiFrameScheduler("), std::string::npos);
	EXPECT_NE(Body.find("AdaptiveBudget.m_VisibleTokens"), std::string::npos);
	EXPECT_NE(Body.find("AdaptiveBudget.m_PrefetchTokens"), std::string::npos);
	EXPECT_NE(Body.find("AdaptiveBudget.m_GpuUploadTokens"), std::string::npos);
	EXPECT_NE(Body.find("const bool AssetsScrollPressure = ResourceFrameContext.m_ScrollActive || ResourceFrameContext.m_JumpScrollActive;"), std::string::npos);
	EXPECT_NE(Body.find("const int AdaptivePrefetchTokens = AssetsScrollPressure ? 0 : AdaptiveBudget.m_PrefetchTokens;"), std::string::npos);
	EXPECT_NE(Body.find("const int AdaptiveBackgroundTokens = AssetsScrollPressure ? 0 : AdaptiveBudget.m_BackgroundTokens;"), std::string::npos);
	EXPECT_NE(Body.find("WorkshopVisibleRange.m_EndItem + (AssetsScrollPressure ? 0 : Columns)"), std::string::npos);
	EXPECT_NE(Menus.find("event=settings_adaptive_budget"), std::string::npos);
	EXPECT_EQ(Body.find("constexpr int MaxWorkshopThumbStartsPerFrame = 16;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsPageSwitchDefersPreviewGpuUploads)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool AssetsPageSwitchActive = m_SettingsPageSwitchActive;"), std::string::npos);
	EXPECT_NE(Body.find("const bool AssetsShellOnlyFrame = AssetsTabSwitchFirstFrame || AssetsPageSwitchActive;"), std::string::npos);
	EXPECT_NE(Body.find("const bool AssetsContentWarmupBlocked = AssetsShellOnlyFrame || AssetsScrollPressure;"), std::string::npos);
	EXPECT_NE(Body.find("AssetsUploadBlocked = AssetsContentWarmupBlocked || AssetsDirectScrollUploadBlocked"), std::string::npos);
	EXPECT_NE(Body.find("MaxPreviewUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;"), std::string::npos);
	EXPECT_NE(Body.find("MaxWorkshopThumbUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;"), std::string::npos);
	EXPECT_EQ(Body.find("const int MaxPreviewUploadsPerFrame = maximum(1, AdaptiveBudget.m_GpuUploadTokens);"), std::string::npos);
	EXPECT_EQ(Body.find("const int MaxWorkshopThumbUploadsPerFrame = maximum(1, AdaptiveBudget.m_GpuUploadTokens);"), std::string::npos);
	EXPECT_EQ(Body.find("m_SettingsFrameBudget.m_MaxGpuUploads = maximum(m_SettingsFrameBudget.m_MaxGpuUploads, AdaptiveBudget.m_GpuUploadTokens);"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsDirectScrollDefersPreviewGpuUploads)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("constexpr int AssetsScrollUploadCooldownFrames = 6;"), std::string::npos);
	EXPECT_NE(Body.find("AssetsDirectScrollUploadBlocked"), std::string::npos);
	EXPECT_NE(Body.find("AssetsUploadBlocked = AssetsContentWarmupBlocked || AssetsDirectScrollUploadBlocked"), std::string::npos);
	EXPECT_NE(Body.find("MaxPreviewUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;"), std::string::npos);
	EXPECT_NE(Body.find("const char *pAssetsUploadBlockFrameContext = AssetsDirectScrollUploadBlocked ? \"scroll_cooldown\""), std::string::npos);
	EXPECT_NE(Body.find("pAssetsUploadBlockFrameContext"), std::string::npos);
	EXPECT_NE(Body.find("scroll_upload_cooldown=%d frame_context=%s upload_block=%s"), std::string::npos);
	EXPECT_NE(Body.find("RefreshAssetsScrollUploadCooldownForOffset"), std::string::npos);
	EXPECT_NE(Body.find("RefreshAssetsScrollUploadCooldownForOffset(ListScrollActive, s_ListBox.ScrollOffsetY()"), std::string::npos);
	EXPECT_NE(Body.find("RefreshAssetsScrollUploadCooldownForOffset(WorkshopListScrollActive, s_WorkshopAssetsListBox.ScrollOffsetY()"), std::string::npos);
	EXPECT_NE(Body.find("RefreshAssetsUploadBudget();"), std::string::npos);
	EXPECT_EQ(Body.find("const int MaxPreviewUploadsPerFrame = AssetsPageSwitchActive ? 0 : AdaptiveBudget.m_GpuUploadTokens;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsDirectScrollDefersWorkshopFinalize)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("MaxWorkshopThumbDecodeFinalizesThisFrame = AssetsUploadBlocked ? 0 : MaxWorkshopThumbDecodeFinalizesPerFrame;"), std::string::npos);
	EXPECT_NE(Body.find("MaxWorkshopThumbUploadsPerFrame = AssetsUploadBlocked ? 0 : AdaptiveBudget.m_GpuUploadTokens;"), std::string::npos);
	EXPECT_NE(Body.find("SettingsResourceFrameStageBudget(FinalizeFrameContext, FinalizePriority, MaxWorkshopThumbDecodeFinalizesThisFrame, 0)"), std::string::npos);
	EXPECT_NE(Body.find("scroll_upload_cooldown=%d"), std::string::npos);
	EXPECT_EQ(Body.find("const int MaxWorkshopThumbUploadsPerFrame = AssetsPageSwitchActive ? 0 : AdaptiveBudget.m_GpuUploadTokens;"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsUiBudgetTelemetryExists)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_NE(Header.find("struct SSettingsUiBudgetFrame"), std::string::npos);
	EXPECT_NE(Header.find("LogSettingsUiBudget("), std::string::npos);
	const size_t UiBudgetEvent = Menus.find("event=settings_ui_budget");
	ASSERT_NE(UiBudgetEvent, std::string::npos);
	const std::string UiBudgetFormat = Menus.substr(UiBudgetEvent, 512);
	EXPECT_TRUE(ContainsAll(UiBudgetFormat, {"layout_ms=", "text_ms=", "text_new=", "text_reused=", "draw_calls=", "vertices=", "indices=", "heap_allocs=", "visible_widgets="}));
	EXPECT_NE(TClient.find("LogSettingsUiBudget(\"settings:tclient\""), std::string::npos);
	EXPECT_NE(Assets.find("LogSettingsUiBudget(\"settings:assets\""), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsInternalTabSwitchStartsFpsWindow)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("StartSettingsPerfFixedWindow(\"settings_assets_tab_switch\""), std::string::npos);
	EXPECT_NE(Body.find("CurrentQmUiPerfPage()"), std::string::npos);
	EXPECT_NE(Body.find("str_format(aAssetsPerfTab"), std::string::npos);
	EXPECT_NE(Body.find("s_AssetsTabSwitchFirstFrame = 1;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsPerfStagesUseRealFrameId)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_NE(Source.find("LogAssetsPerfStageForClient(Client(), pStage, DurationMs, Force, pExtra);"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Source, {
						"LogAssetsFramePerfStage(\"assets_preview_draw_workshop_cards\"",
						"LogAssetsFramePerfStage(\"assets_card_geometry\"",
						"LogAssetsPerfStageForClient(Client(), \"assets_window_focus\"",
						"LogAssetsPerfStageForClient(Client(), \"assets_preview_upload_queue_push\"",
						"LogAssetsPerfStageForClient(Client(), \"assets_workshop_thumb_start_local\"",
						"LogAssetsPerfStageForClient(Client(), \"assets_workshop_thumb_start_remote\"",
					}));
	EXPECT_EQ(Source.find("QmPerfLogStage(\"perf/assets\", pStage, DurationMs, Force, nullptr"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfAnalyzerReportsUiBudgetFields)
{
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");
	const std::string Quality = ReadRepoFile("qmclient_scripts/perf/lib/quality.ts");
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	EXPECT_NE(Stats.find("export interface SettingsUiBudgetSummary"), std::string::npos);
	EXPECT_NE(Stats.find("settingsUiBudgetSummary(entries"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Stats, {"layout_ms", "text_ms", "draw_calls", "heap_allocs"}));
	EXPECT_NE(Quality.find("settingsUiBudget"), std::string::npos);
	EXPECT_NE(Report.find("Settings UI Budget"), std::string::npos);
	EXPECT_FALSE(ContainsAny(Report, {"APPROXIMATE", "REPORT ONLY", "Draw Calls Est.", "Vertices Est.", "Indices Est.", "占位观测", "不代表本轮已做通用文本渲染优化"}));
	EXPECT_NE(Tests.find("testSettingsUiBudgetFieldsAppearInSummaryAndReport"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfTelemetryOverheadIsAttributedByFrameWindow)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");
	const std::string Body = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	ASSERT_FALSE(Body.empty());

	// Text/glyph telemetry is useful only if its own frame-tail flush can be
	// blamed when it becomes the problem. This prevents low-FPS windows from
	// falling back to attribution=none or hiding profiler overhead inside UI total.
	EXPECT_NE(Body.find("TextRender()->FlushQmTextRuntimeBudgetLog();"), std::string::npos);
	EXPECT_NE(Body.find("LogPerfStage(Client(), \"telemetry_flush\", StageTimer.ElapsedMs());"), std::string::npos);
	EXPECT_NE(Stats.find("telemetry_overhead"), std::string::npos);
	EXPECT_NE(Stats.find("telemetry_flush"), std::string::npos);
	EXPECT_NE(Report.find("Telemetry Flush"), std::string::npos);
	EXPECT_NE(Tests.find("testPerfOverheadIsReportedAsCulprit"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientSettingsTab0StableTextKeysMatchPlan)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	EXPECT_NE(Menus.find("BuildTClientSettingsMenuTextPlan(vItems, MainView, m_TClientSettingsTab);"), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-prediction-margin\""), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-outline-opacity\""), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-outline-solid-opacity\""), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-outline-width\""), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-player-indicator-title\""), std::string::npos);

	EXPECT_EQ(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, \"tclient-prediction-margin\""), std::string::npos);
	EXPECT_EQ(Source.find("SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, \"tclient-player-indicator-title\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientTab0StableTextKeysMatchLatestLogSamples)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_NE(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-antiping-uncertainty-scale\""), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-auto-vote-minimum-time\""), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, \"tclient-auto-reply-title\""), std::string::npos);
	EXPECT_NE(Source.find("\"tclient-antiping-uncertainty-scale\""), std::string::npos);
	EXPECT_NE(Source.find("\"tclient-auto-vote-minimum-time\""), std::string::npos);
	EXPECT_NE(Source.find("\"tclient-auto-reply-title\""), std::string::npos);
	EXPECT_EQ(Source.find("SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, \"tclient-auto-reply-title\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientPrewarmDoesNotRunUnboundedInVisibleTargetFrame)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool TClientVisibleTargetFrame = !ReadOnly"), std::string::npos);
	EXPECT_NE(Body.find("SetProgressiveEnabled(TClientVisibleTargetFrame)"), std::string::npos);
	EXPECT_NE(Body.find("SetMaxSectionsPerFrame(TClientVisibleTargetFrame ?"), std::string::npos);
	EXPECT_NE(Body.find("tclient_settings_left_prewarm_budgeted"), std::string::npos);
	EXPECT_NE(Body.find("tclient_settings_right_prewarm_budgeted"), std::string::npos);
	EXPECT_EQ(Body.find("VisualFontLoader.SetProgressiveEnabled(false);"), std::string::npos);
	EXPECT_EQ(Body.find("RightSectionLoader.SetProgressiveEnabled(false);"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientSectionMeasuredHeightMatchesRenderedHeight)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("LogTClientSectionHeightConsistency"), std::string::npos);
	EXPECT_NE(Body.find("section_height_measured=%.3f section_height_rendered=%.3f height_delta=%.3f"), std::string::npos);
	EXPECT_NE(Body.find("MeasuredHeight = "), std::string::npos);
	EXPECT_NE(Body.find("RenderedHeight = "), std::string::npos);
	EXPECT_NE(Body.find("absolute(HeightDelta) <= 0.01f"), std::string::npos);
	EXPECT_NE(Body.find("RenderBoxedFullSection"), std::string::npos);
	EXPECT_NE(Body.find("FillCachedStaticLayer"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsWorkshopCardDrawHasSubstageTelemetry)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("assets_preview_draw_workshop_cards_layout_text"), std::string::npos);
	EXPECT_NE(Body.find("assets_preview_draw_workshop_cards_preview_draw"), std::string::npos);
	EXPECT_NE(Body.find("assets_preview_draw_workshop_cards_thumb_scheduling"), std::string::npos);
	EXPECT_NE(Body.find("layout_text_ms=%.3f preview_draw_ms=%.3f thumb_scheduling_ms=%.3f"), std::string::npos);
	EXPECT_NE(Body.find("char aExtra[512];"), std::string::npos);
	EXPECT_NE(Body.find("CardLayoutTextTimer"), std::string::npos);
	EXPECT_NE(Body.find("CardPreviewDrawTimer"), std::string::npos);
	EXPECT_NE(Body.find("ThumbSchedulingTimer"), std::string::npos);
	EXPECT_NE(Body.find("WorkshopCardLayoutTextMs += CardLayoutTextTimer.ElapsedMs();"), std::string::npos);
	EXPECT_NE(Body.find("WorkshopCardPreviewDrawMs += CardPreviewDrawTimer.ElapsedMs();"), std::string::npos);
	EXPECT_NE(Body.find("const double PreflightMs = ThumbSchedulingTimer.ElapsedMs();"), std::string::npos);
	EXPECT_NE(Body.find("WorkshopCardThumbSchedulingMs += PreflightMs;"), std::string::npos);
	const size_t PreparePos = Body.find("auto PrepareAssetsVisibleContentBudgeted =");
	const size_t DrawLoopPos = Body.find("s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem)");
	ASSERT_NE(PreparePos, std::string::npos);
	ASSERT_NE(DrawLoopPos, std::string::npos);
	EXPECT_LT(PreparePos, DrawLoopPos);
	EXPECT_NE(Body.find("UiBudget.m_LayoutMs = WorkshopCardsTimer.ElapsedMs();"), std::string::npos);
	EXPECT_NE(Body.find("UiBudget.m_TextReused = 0;"), std::string::npos);
	EXPECT_EQ(Body.find("WorkshopCardLoopMs * 0.5"), std::string::npos);
	EXPECT_EQ(Body.find("WorkshopCardLoopMs - WorkshopCardLayoutTextMs"), std::string::npos);
	EXPECT_EQ(Body.find("UiBudget.m_LayoutMs = AssetsUiBudgetTimer.ElapsedMs();"), std::string::npos);
	EXPECT_EQ(Body.find("UiBudget.m_TextReused = WorkshopVisibleRange.m_RenderedItems;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabSwitchLimitsFirstFrameVisibleThumbStarts)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("constexpr int MaxAssetsTabSwitchVisibleThumbStartsFirstFrame = 2;"), std::string::npos);
	EXPECT_NE(Body.find("AssetsTabSwitchFirstFrame"), std::string::npos);
	EXPECT_NE(Body.find("MaxAssetsTabSwitchVisibleThumbStartsFirstFrame"), std::string::npos);
	EXPECT_NE(Body.find("tab_switch_first_frame=%d"), std::string::npos);
	EXPECT_NE(Body.find("visible_thumb_start_limit=%d"), std::string::npos);
	EXPECT_NE(Body.find("CombinedVisibleAdmission.m_VisibleStarts >= VisibleThumbStartLimitThisFrame"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabSwitchKeepsVisibleThumbStartsCappedForCooldown)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("constexpr int AssetsTabSwitchCooldownFrames"), std::string::npos);
	EXPECT_NE(Body.find("s_AssetsTabSwitchCooldownFrames = AssetsTabSwitchCooldownFrames;"), std::string::npos);
	EXPECT_NE(Body.find("const bool AssetsTabSwitchCooldownActive = s_AssetsTabSwitchCooldownFrames > 0;"), std::string::npos);
	EXPECT_NE(Body.find("AssetsTabSwitchCooldownActive ? MaxAssetsTabSwitchVisibleThumbStartsPerFrame : MaxWorkshopThumbStartsPerFrameAdaptive"), std::string::npos);
	EXPECT_NE(Body.find("tab_switch_cooldown_frames=%d"), std::string::npos);
	EXPECT_NE(Body.find("--s_AssetsTabSwitchCooldownFrames;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEntityBgInstalledThumbsRespectTabSwitchVisibleCap)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("int &VisibleStartsThisFrame"), std::string::npos);
	EXPECT_NE(Source.find("int MaxVisibleStartsPerFrame"), std::string::npos);
	EXPECT_NE(Source.find("if(HighPriority && VisibleStartsThisFrame >= MaxVisibleStartsPerFrame)"), std::string::npos);
	EXPECT_NE(Source.find("++VisibleStartsThisFrame;"), std::string::npos);
	EXPECT_NE(Body.find("CombinedVisibleAdmission.m_VisibleStarts"), std::string::npos);
	EXPECT_NE(Body.find("VisibleThumbStartLimitThisFrame"), std::string::npos);
	EXPECT_NE(Body.find("CombinedVisibleAdmission.m_VisibleStarts,\n\t\t\t\t\t   VisibleThumbStartLimitThisFrame"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabSwitchDoesNotTransformCardGeometry)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("TriggerUiSwitchAnimation(AssetsTabSwitchNode"), std::string::npos);
	EXPECT_NE(Body.find("AssetsTabSwitchFirstFrame"), std::string::npos);
	EXPECT_EQ(Body.find("ApplyUiSwitchOffset(MainView, TransitionStrength, s_AssetsTransitionDirection"), std::string::npos);
	EXPECT_EQ(Body.find("ApplyUiSwitchOffset(MainView"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardShellFirstRenderingExists)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("enum class ESettingsAssetsCardHydrationLayer"), std::string::npos);
	EXPECT_NE(Source.find("SSettingsAssetsCardShell"), std::string::npos);
	EXPECT_NE(Source.find("SSettingsAssetsCardMetadataCacheEntry"), std::string::npos);
	EXPECT_NE(Source.find("SSettingsAssetsCardPreviewState"), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardShell("), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardMetadataCached("), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardPreview("), std::string::npos);
	EXPECT_LT(Body.find("RenderAssetsCardShell("), Body.find("RenderAssetsCardMetadataCached("));
	EXPECT_LT(Body.find("RenderAssetsCardShell("), Body.find("RenderAssetsCardPreview("));
}

TEST(QmMonitoringHelpers, AssetsCardMetadataUsesDedicatedCache)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("struct SSettingsAssetsCardCacheKey"), std::string::npos);
	EXPECT_NE(Source.find("m_AssetId"), std::string::npos);
	EXPECT_NE(Source.find("m_Tab"), std::string::npos);
	EXPECT_NE(Source.find("m_LocaleHash"), std::string::npos);
	EXPECT_NE(Source.find("m_UiScale"), std::string::npos);
	EXPECT_NE(Source.find("m_CardWidth"), std::string::npos);
	EXPECT_NE(Source.find("m_StatusHash"), std::string::npos);
	EXPECT_NE(Source.find("m_Installed"), std::string::npos);
	EXPECT_NE(Source.find("m_DownloadFailed"), std::string::npos);
	EXPECT_NE(Source.find("m_LocalOnly"), std::string::npos);
	EXPECT_NE(Source.find("QM_ASSET_METADATA_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(Source.find("TrimAssetsCardMetadataCacheForInsert("), std::string::npos);
	EXPECT_NE(Source.find("static std::unordered_map<SSettingsAssetsCardCacheKey, SSettingsAssetsCardMetadataCacheEntry"), std::string::npos);
	EXPECT_NE(Source.find("BuildAssetsCardCacheKey("), std::string::npos);
	EXPECT_NE(Source.find("Key.m_StatusHash = str_quickhash"), std::string::npos);
	EXPECT_NE(Source.find("FindAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(Source.find("HydrateAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(Source.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
	EXPECT_EQ(Source.find("RefreshAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(Body.find("SSettingsAssetsCardMetadataCacheEntry *pMetadata = FindAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(Body.find("RequestAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_EQ(Body.find("pMetadata = HydrateAssetsCardMetadataTimed("), std::string::npos);
	EXPECT_EQ(Body.find("RefreshAssetsCardMetadata(*pMetadata"), std::string::npos);
	EXPECT_EQ(Source.find("RegisterMenuTextPlanItem(\"assets-card"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsMetadataRequestDoesNotHydrateInRenderPath)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string RequestBody = ExtractSourceFunctionBody(Source, "static void RequestAssetsCardMetadataHydration");
	ASSERT_FALSE(RequestBody.empty());

	EXPECT_NE(Source.find("struct SSettingsAssetsCardMetadataRequest"), std::string::npos);
	EXPECT_NE(Source.find("QueueAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_NE(RequestBody.find("QueueAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_EQ(RequestBody.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsMetadataHydratesOnlyThroughBudgetDrain)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "static int DrainAssetsCardMetadataHydrationRequests");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Body.find("const int AssetsInitialMetadataLayoutTokens = maximum(1, minimum(AdaptiveBudget.m_VisibleTokens, 4));"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsMetadataLayoutTokensThisFrame = AssetsShellOnlyFrame ? AssetsInitialMetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Body.find("DrainAssetsCardMetadataHydrationRequests(AssetsMetadataLayoutTokensThisFrame"), std::string::npos);
	EXPECT_NE(DrainBody.find("while(MetadataLayoutTokens > 0"), std::string::npos);
	EXPECT_NE(DrainBody.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsScrollPressureBlocksContentHydrationWork)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Fast scroll/jump scroll drops frames when preview/decode/upload work is
	// allowed in the same render frame. Metadata gets a tiny visible-only budget
	// so labels do not disappear while holding the scrollbar.
	EXPECT_NE(Body.find("const bool AssetsContentWarmupBlocked = AssetsShellOnlyFrame || AssetsScrollPressure;"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsMetadataLayoutTokensThisFrame = AssetsShellOnlyFrame ? AssetsInitialMetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsPreviewArtifactTokensThisFrame = AssetsContentWarmupBlocked ? 0 : AdaptivePreviewArtifactTokens;"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsTextureUploadTokensThisFrame = AssetsContentWarmupBlocked ? 0 : AdaptiveTextureUploadTokens;"), std::string::npos);
	EXPECT_NE(Body.find("const int MaxPreviewDecodeStartsPerFrame = AssetsContentWarmupBlocked ? 0 : maximum(1, AdaptiveBudget.m_VisibleTokens + AdaptivePrefetchTokens + AdaptiveBackgroundTokens);"), std::string::npos);
	EXPECT_NE(Body.find("const int MaxWorkshopThumbStartsPerFrameAdaptive = AssetsContentWarmupBlocked ? 0 : maximum(1, AdaptiveBudget.m_VisibleTokens + AdaptivePrefetchTokens + AdaptiveBackgroundTokens);"), std::string::npos);
	EXPECT_NE(Body.find("const int VisibleThumbStartLimitThisFrame = AssetsContentWarmupBlocked ? 0 :"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsScrollPressureStillRendersMetadataFallback)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Regression guard: holding the scrollbar must not hide card titles. The
	// immediate fallback uses fixed card geometry, so it remains readable without
	// waiting for streamed text containers to hydrate.
	EXPECT_NE(Body.find("const bool AssetsRenderCardMetadataFallback = !AssetsShellOnlyFrame;"), std::string::npos);
	EXPECT_NE(Body.find("else if(AssetsRenderCardMetadataFallback)"), std::string::npos);
	EXPECT_NE(Body.find("pMetadata != nullptr && AssetsContentWarmupBlocked"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsScrollPressureSkipsPreviewSchedulingAndUploads)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Scroll pressure is the path the user stress-tested: scheduling/decode/upload
	// work must not run in the same frame as a fast list movement.
	EXPECT_NE(Body.find("if(!AssetsContentWarmupBlocked)"), std::string::npos);
	EXPECT_NE(Body.find("PrepareAssetsLocalVisibleContentBudgeted();"), std::string::npos);
	EXPECT_NE(Body.find("PrepareAssetsVisibleContentBudgeted();"), std::string::npos);
	EXPECT_NE(Body.find("ProcessAssetsResourcePreviewJobs(Graphics(), ResourcePreviewTelemetry, ResourcePreviewUploadBudget);"), std::string::npos);
	EXPECT_NE(Body.find("if(!AssetsContentWarmupBlocked && FirstVisibleIndex >= 0)"), std::string::npos);
	EXPECT_LT(Body.find("if(!AssetsContentWarmupBlocked)"), Body.find("ProcessAssetsResourcePreviewJobs(Graphics(), ResourcePreviewTelemetry, ResourcePreviewUploadBudget);"));
}

TEST(QmMonitoringHelpers, AssetsTabSwitchFirstFrameDoesNotStartThumbsOrUploads)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool AssetsShellOnlyFrame = AssetsTabSwitchFirstFrame || AssetsPageSwitchActive;"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsTextureUploadTokensThisFrame = AssetsContentWarmupBlocked ? 0 : AdaptiveTextureUploadTokens;"), std::string::npos);
	EXPECT_NE(Body.find("if(!AssetsContentWarmupBlocked)\n\t\t\t\t\t\t\tSchedulePreviewRange("), std::string::npos);
	EXPECT_NE(Body.find("if(!AssetsContentWarmupBlocked && StartWorkshopThumb(Asset, Visible))"), std::string::npos);
	EXPECT_NE(Body.find("if(!AssetsContentWarmupBlocked)"), std::string::npos);
	EXPECT_NE(Body.find("PrepareAssetsVisibleContentBudgeted();"), std::string::npos);
	EXPECT_NE(Body.find("ProcessAssetsResourcePreviewJobs(Graphics(), ResourcePreviewTelemetry, ResourcePreviewUploadBudget);"), std::string::npos);
	EXPECT_LT(Body.find("if(!AssetsContentWarmupBlocked)"), Body.find("ProcessAssetsResourcePreviewJobs(Graphics(), ResourcePreviewTelemetry, ResourcePreviewUploadBudget);"));
	EXPECT_NE(Body.find("metadata_hydrated=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsDrawLoopOnlyUsesReadyContent)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());
	const size_t DrawLoopStart = Body.find("s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem)");
	ASSERT_NE(DrawLoopStart, std::string::npos);
	const size_t DrawLoopEnd = Body.find("s_WorkshopAssetsListBox.SkipItems((int)CombinedCount - WorkshopVisibleRange.m_EndItem)", DrawLoopStart);
	ASSERT_NE(DrawLoopEnd, std::string::npos);
	const std::string DrawLoopBody = Body.substr(DrawLoopStart, DrawLoopEnd - DrawLoopStart);

	EXPECT_NE(DrawLoopBody.find("FindAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(DrawLoopBody.find("RenderAssetsCardMetadataCached("), std::string::npos);
	EXPECT_NE(DrawLoopBody.find("RenderAssetsCardPreview("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("RequestAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("StartWorkshopThumb("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("SchedulePreviewRange("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("ProcessAssetsResourcePreviewJobs("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("DrainSharedResourcePreviewUploadQueue("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsVisiblePreflightRunsOutsideCardDrawLoop)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());
	const size_t PreparePos = Body.find("PrepareAssetsVisibleContentBudgeted(");
	const size_t DrawLoopStart = Body.find("s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem)");
	ASSERT_NE(PreparePos, std::string::npos);
	ASSERT_NE(DrawLoopStart, std::string::npos);

	// The preflight is the only place that may schedule visible/near-visible
	// content. If this work drifts back into the card loop, tab switches still
	// stall even though the shell-first layer exists.
	EXPECT_LT(PreparePos, DrawLoopStart);
	EXPECT_EQ(Body.find("RunAssetsVisibleReadyPreflight();"), std::string::npos);
	EXPECT_NE(Body.find("LogAssetsFramePerfStage(\"assets_visible_preflight\""), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardDrawLoopDoesNotStartThumbsPreviewJobsOrUploads)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());
	const size_t DrawLoopStart = Body.find("s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem)");
	ASSERT_NE(DrawLoopStart, std::string::npos);
	const size_t DrawLoopEnd = Body.find("s_WorkshopAssetsListBox.SkipItems((int)CombinedCount - WorkshopVisibleRange.m_EndItem)", DrawLoopStart);
	ASSERT_NE(DrawLoopEnd, std::string::npos);
	const std::string DrawLoopBody = Body.substr(DrawLoopStart, DrawLoopEnd - DrawLoopStart);

	// Regression guard for the broken previews/default tee issue: the draw loop
	// may only consume ready state. It must not start jobs, uploads, or thumbs.
	EXPECT_EQ(DrawLoopBody.find("StartWorkshopThumb("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("SchedulePreviewRange("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("StartAssetsEntityBgPreviewArtifactJob("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("ProcessAssetsResourcePreviewJobs("), std::string::npos);
	EXPECT_EQ(DrawLoopBody.find("SettingsResourcePreviewConsumeUploadBudget("), std::string::npos);
}

TEST(QmMonitoringHelpers, EntityBgPreviewArtifactBackfillsReadyTexture)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string PreviewSource = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Entity BG needs its own tile/artifact preview, not a permanent placeholder.
	// The artifact job must mark CPU artifact ready and the upload drain must
	// backfill a texture that the card renderer can draw in later frames.
	EXPECT_NE(Source.find("StartAssetsEntityBgPreviewArtifactJob("), std::string::npos);
	EXPECT_NE(Source.find("gs_SettingsAssetsResourcePreviewCache.MarkArtifactReady("), std::string::npos);
	EXPECT_NE(PreviewSource.find("m_TextureReady = true"), std::string::npos);
	EXPECT_NE(Body.find("if(pPipelineState != nullptr && pPipelineState->m_Texture.IsValid())"), std::string::npos);
	const size_t FirstPrepare = std::min(Body.find("auto PrepareAssetsLocalVisibleContentBudgeted ="), Body.find("auto PrepareAssetsVisibleContentBudgeted ="));
	ASSERT_NE(FirstPrepare, std::string::npos);
	EXPECT_LT(FirstPrepare, Body.find("StartAssetsEntityBgPreviewArtifactJob("));
	EXPECT_EQ(Body.substr(Body.find("s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem)"), Body.find("s_WorkshopAssetsListBox.SkipItems((int)CombinedCount - WorkshopVisibleRange.m_EndItem)") - Body.find("s_WorkshopAssetsListBox.SkipItems(WorkshopVisibleRange.m_FirstItem)")).find("StartAssetsEntityBgPreviewArtifactJob("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEntityBgPreviewJobUsesStableKeyNotItemPointer)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string StartBody = ExtractSourceFunctionBody(Source, "static bool StartAssetsEntityBgPreviewArtifactJob");
	ASSERT_FALSE(StartBody.empty());

	// Preview jobs may finish after a fast scroll, list rebuild, or tab switch.
	// They must be keyed by stable resource identity rather than retaining a
	// pointer to a card/list item whose storage can move or disappear.
	EXPECT_EQ(Source.find("StartAssetsEntityBgPreviewArtifactJob(const SResourcePreviewKey &PreviewKey, CMenus::SCustomItem *pItem"), std::string::npos);
	EXPECT_NE(Source.find("StartAssetsEntityBgPreviewArtifactJob(const SResourcePreviewKey &PreviewKey, const char *pAssetName"), std::string::npos);
	EXPECT_EQ(StartBody.find("pItem->"), std::string::npos);
	EXPECT_NE(StartBody.find("ResolveEntityBgPreviewArtifactSource(pStorage, pAssetName"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardHydrationSchedulerDefersContentAfterTabSwitch)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("struct SSettingsAssetsCardHydrationScheduler"), std::string::npos);
	EXPECT_NE(Source.find("BeginAssetsCardHydrationFrame("), std::string::npos);
	EXPECT_NE(Source.find("CanHydrateMetadata("), std::string::npos);
	EXPECT_NE(Source.find("CanRenderMetadata("), std::string::npos);
	EXPECT_NE(Source.find("CanHydratePreview("), std::string::npos);
	EXPECT_NE(Source.find("CanRenderPreview("), std::string::npos);
	EXPECT_NE(Source.find("m_TabSwitchShellOnlyFrame"), std::string::npos);
	EXPECT_NE(Body.find("SSettingsAssetsCardHydrationScheduler CardHydrationScheduler = BeginAssetsCardHydrationFrame("), std::string::npos);
	EXPECT_NE(Body.find("RequestAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_NE(Body.find("CardHydrationScheduler.CanRenderPreview("), std::string::npos);
	EXPECT_EQ(Body.find("RenderAssetsCardPreview(Shell, PreviewState, true, CardHydrationScheduler.CanHydratePreview("), std::string::npos);
	EXPECT_EQ(Body.find("const bool RenderMetadata = CardHydrationScheduler.CanRenderMetadata(CombinedVisible, MetadataCached) &&"), std::string::npos);
	EXPECT_EQ(Body.find("(MetadataCached || PreviewPipelineScheduler.CanHydrateMetadata(ESettingsResourcePreviewPriority::VISIBLE));"), std::string::npos);
	EXPECT_NE(Body.find("tab_switch_shell_only=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardPreviewDrawDoesNotStarveAfterBudget)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("bool CanRenderPreview(bool Visible, bool HasPreviewContent, bool HeavyPreviewDeferred = false)"), std::string::npos);
	EXPECT_NE(Source.find("if(!HasPreviewContent)"), std::string::npos);
	EXPECT_NE(Body.find("CardHydrationScheduler.CanRenderPreview(CombinedVisible, PreviewReady, PreviewState.m_EntityBgHeavyPreviewDeferred)"), std::string::npos);
	EXPECT_NE(Body.find("CardHydrationScheduler.CanRenderPreview(CombinedVisible, PreviewReady)"), std::string::npos);
	EXPECT_EQ(Body.find("CardHydrationScheduler.CanHydratePreview(CombinedVisible, PreviewState.m_Texture.IsValid() || PreviewState.m_DrawEntityTileArtifact)"), std::string::npos);
	EXPECT_EQ(Body.find("CardHydrationScheduler.CanHydratePreview(CombinedVisible, PreviewReady)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardPreviewHeavyPathLeavesDrawLoop)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("EnsureAssetsCardPreviewArtifact("), std::string::npos);
	EXPECT_NE(Source.find("RenderAssetsEntityTilePreviewArtifact("), std::string::npos);
	EXPECT_NE(Body.find("EnsureAssetsCardPreviewArtifact("), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardPreview("), std::string::npos);
	EXPECT_NE(Source.find("auto RenderAssetsCardShell = [&](const SSettingsAssetsCardShell &Shell)"), std::string::npos);
	EXPECT_NE(Source.find("DrawRoundedSurface(Ui(), ShellRect, ColorRGBA(0.03f, 0.04f, 0.06f, 0.16f), ColorRGBA(), AssetCardRadius);"), std::string::npos);
	const size_t ShellStart = Source.find("auto RenderAssetsCardShell = [&](const SSettingsAssetsCardShell &Shell)");
	ASSERT_NE(ShellStart, std::string::npos);
	const size_t ShellEnd = Source.find("};", ShellStart);
	ASSERT_NE(ShellEnd, std::string::npos);
	const std::string ShellBody = Source.substr(ShellStart, ShellEnd - ShellStart);
	EXPECT_EQ(ShellBody.find("DrawPreviewFrame(Shell.m_TextureRect);"), std::string::npos);
	EXPECT_EQ(Body.find("static const int COLS = 7, ROWS = 7;"), std::string::npos);
	EXPECT_EQ(Body.find("for(int r = 0; r < ROWS; r++)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEntityBgPreviewHeavyPathDeferredDuringTabSwitchCooldown)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("m_EntityBgHeavyPreviewDeferred"), std::string::npos);
	EXPECT_NE(Body.find("const bool DeferEntityBgHeavyPreview = s_CurCustomTab == ASSETS_TAB_ENTITY_BG && AssetsTabSwitchCooldownActive"), std::string::npos);
	EXPECT_NE(Body.find("PreviewState.m_EntityBgHeavyPreviewDeferred = DeferEntityBgHeavyPreview"), std::string::npos);
	EXPECT_NE(Body.find("++EntityBgHeavyPreviewDeferredCount;"), std::string::npos);
	EXPECT_NE(Body.find("CardHydrationScheduler.CanRenderPreview(CombinedVisible, PreviewReady, PreviewState.m_EntityBgHeavyPreviewDeferred)"), std::string::npos);
	EXPECT_NE(Body.find("entity_bg_preview_deferred=%d"), std::string::npos);
	EXPECT_NE(Body.find("entity_bg_preview_deferred_count=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsResourcePreviewPipelineExists)
{
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string CMake = ReadRepoFile("CMakeLists.txt");

	EXPECT_NE(Header.find("struct SResourcePreviewKey"), std::string::npos);
	EXPECT_NE(Header.find("struct SResourcePreviewState"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewCache"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewScheduler"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewJob"), std::string::npos);
	EXPECT_NE(Header.find("enum class ESettingsResourcePreviewPriority"), std::string::npos);
	EXPECT_NE(Header.find("VISIBLE"), std::string::npos);
	EXPECT_NE(Header.find("NEAR_VISIBLE"), std::string::npos);
	EXPECT_NE(Header.find("BACKGROUND"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewScheduler::BeginFrame"), std::string::npos);
	const std::string BeginFrameBody = ExtractSourceFunctionBody(Source, "void CSettingsResourcePreviewScheduler::BeginFrame(int VisibleBudget, int NearVisibleBudget, int BackgroundBudget, int UploadBudget)");
	ASSERT_FALSE(BeginFrameBody.empty());
	EXPECT_EQ(BeginFrameBody.find("CanStartPreviewJob("), std::string::npos);
	EXPECT_NE(CMake.find("components/qmclient/settings_resource_preview.cpp"), std::string::npos);
	EXPECT_NE(CMake.find("src/game/client/components/qmclient/settings_resource_preview.cpp"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsResourcePreviewPipelineHasRealArtifactAndUploadScheduler)
{
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");

	EXPECT_NE(Header.find("struct SResourcePreviewArtifact"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsResourcePreviewUploadScheduler"), std::string::npos);
	EXPECT_NE(Header.find("EnqueueUpload(const SResourcePreviewKey &Key, CImageInfo &&Image"), std::string::npos);
	EXPECT_NE(Header.find("Drain(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry"), std::string::npos);
	EXPECT_NE(Header.find("m_vUploadQueue"), std::string::npos);
	EXPECT_NE(Header.find("m_ArtifactReady"), std::string::npos);
	EXPECT_NE(Header.find("m_UploadQueueDepth"), std::string::npos);
	EXPECT_NE(Header.find("m_UploadBudgetExhausted"), std::string::npos);

	EXPECT_NE(Source.find("BuildPreviewArtifact("), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewJob::CSettingsResourcePreviewJob(std::string Name, CImageInfo &&Image, int TargetSize)"), std::string::npos);
	EXPECT_NE(Source.find("Result.m_Artifact = BuildPreviewArtifact(std::move(m_InputImage), m_TargetSize);"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewUploadScheduler::Drain"), std::string::npos);
	EXPECT_NE(Source.find("SettingsResourcePreviewConsumeUploadBudget(Budget"), std::string::npos);
	EXPECT_NE(Source.find("SettingsResourcePreviewCommitUploadBudget(Budget"), std::string::npos);
	EXPECT_NE(Source.find("if(pGraphics == nullptr)"), std::string::npos);
	EXPECT_NE(Source.find("Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();"), std::string::npos);
	EXPECT_NE(Source.find("if(Texture.IsValid())"), std::string::npos);
	EXPECT_NE(Source.find("Cache.MarkUploadFailed(Item.m_Key);"), std::string::npos);
	EXPECT_EQ(Source.find("Result.m_Image = std::move(m_InputImage);"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsResourcePreviewSchedulerZeroBudgetDoesNotAdmitWork)
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

TEST(QmMonitoringHelpers, SettingsResourcePreviewShellOnlyBlocksPreviewJobsAndUploads)
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

TEST(QmMonitoringHelpers, SettingsResourcePreviewUploadBudgetUsesSharedLimiterBeforeLocalBudget)
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

TEST(QmMonitoringHelpers, SettingsResourcePreviewRejectsInvalidUploadImagesBeforeBudget)
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

TEST(QmMonitoringHelpers, SettingsResourcePreviewDrainRejectsInvalidImagesBeforeConsumingBudget)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "int CSettingsResourcePreviewUploadScheduler::Drain(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	const std::string DrainOneBody = ExtractSourceFunctionBody(Source, "bool CSettingsResourcePreviewUploadScheduler::DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	ASSERT_FALSE(DrainBody.empty());
	ASSERT_FALSE(DrainOneBody.empty());

	EXPECT_NE(DrainBody.find("DrainOne(Budget, Telemetry, Cache, pGraphics)"), std::string::npos);
	const size_t ValidatePos = DrainOneBody.find("!SettingsResourcePreviewImageValidForUpload(Item.m_Image)");
	const size_t ConsumePos = DrainOneBody.find("SettingsResourcePreviewConsumeUploadBudget(Budget");
	ASSERT_NE(ValidatePos, std::string::npos);
	ASSERT_NE(ConsumePos, std::string::npos);
	EXPECT_LT(ValidatePos, ConsumePos);
	EXPECT_NE(DrainOneBody.find("Cache.MarkUploadFailed(Item.m_Key);"), std::string::npos);
	EXPECT_NE(DrainOneBody.find("Item.m_Finalize(false, IGraphics::CTextureHandle())"), std::string::npos);
	EXPECT_NE(DrainOneBody.find("m_vUploadQueue.push_front(std::move(Item));"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsResourcePreviewUploadBudgetExhaustionRequeuesWithoutFailure)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string DrainOneBody = ExtractSourceFunctionBody(Source, "bool CSettingsResourcePreviewUploadScheduler::DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	ASSERT_FALSE(DrainOneBody.empty());

	const size_t BudgetExhaustedPos = DrainOneBody.find("!SettingsResourcePreviewConsumeUploadBudget(Budget)");
	const size_t UploadPos = DrainOneBody.find("pGraphics->LoadTextureRawMove");
	ASSERT_NE(BudgetExhaustedPos, std::string::npos);
	ASSERT_NE(UploadPos, std::string::npos);
	ASSERT_LT(BudgetExhaustedPos, UploadPos);
	const std::string BudgetExhaustedBody = DrainOneBody.substr(BudgetExhaustedPos, UploadPos - BudgetExhaustedPos);
	EXPECT_NE(BudgetExhaustedBody.find("m_vUploadQueue.push_front(std::move(Item));"), std::string::npos);
	EXPECT_EQ(BudgetExhaustedBody.find("Item.m_Image.Free()"), std::string::npos);
	EXPECT_EQ(BudgetExhaustedBody.find("Item.m_Finalize(false)"), std::string::npos);
	EXPECT_EQ(BudgetExhaustedBody.find("Cache.MarkPreviewJobDone(Item.m_Key, false)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsSharedUploadSchedulerDoesNotRequeueMovedPreviewImages)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	EXPECT_NE(Source.find("DrainSharedResourcePreviewUploadQueue"), std::string::npos);
	EXPECT_NE(Source.find("while(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0 && !vReadyQueue.empty()"), std::string::npos);
	EXPECT_NE(Source.find("while(gs_SettingsAssetsResourcePreviewUploadScheduler.QueueDepth() == 0 && !WorkshopState.m_vReadyThumbQueue.empty()"), std::string::npos);
	const size_t LocalEnqueue = Source.find("gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUploadToTarget(\n\t\t\t\tPreviewKey");
	ASSERT_NE(LocalEnqueue, std::string::npos);
	const size_t LocalFailure = Source.find("if(!gs_SettingsAssetsResourcePreviewUploadScheduler.DrainOne(PreviewUploadBudget", LocalEnqueue);
	ASSERT_NE(LocalFailure, std::string::npos);
	const size_t LocalEnd = Source.find("char aFinalizeUploadExtra", LocalFailure);
	ASSERT_NE(LocalEnd, std::string::npos);
	const std::string LocalFailureBody = Source.substr(LocalFailure, LocalEnd - LocalFailure);
	EXPECT_EQ(LocalFailureBody.find("vReadyQueue.push_front(Handle)"), std::string::npos);
	EXPECT_EQ(LocalFailureBody.find("ResetCustomItemPreviewState(*pItem)"), std::string::npos);
	EXPECT_NE(LocalFailureBody.find("GPU_UPLOAD_BUDGET"), std::string::npos);

	const size_t WorkshopEnqueue = Source.find("gs_SettingsAssetsResourcePreviewUploadScheduler.EnqueueUploadToTarget(\n\t\t\t\t\tPreviewKey", LocalEnd);
	ASSERT_NE(WorkshopEnqueue, std::string::npos);
	const size_t WorkshopFailure = Source.find("if(!gs_SettingsAssetsResourcePreviewUploadScheduler.DrainOne(WorkshopPreviewUploadBudget", WorkshopEnqueue);
	ASSERT_NE(WorkshopFailure, std::string::npos);
	const size_t WorkshopEnd = Source.find("WorkshopThumbUploadedBytesThisFrame", WorkshopFailure);
	ASSERT_NE(WorkshopEnd, std::string::npos);
	const std::string WorkshopFailureBody = Source.substr(WorkshopFailure, WorkshopEnd - WorkshopFailure);
	EXPECT_EQ(WorkshopFailureBody.find("WorkshopState.m_vReadyThumbQueue.push_front"), std::string::npos);
	EXPECT_EQ(WorkshopFailureBody.find("ResetWorkshopThumbReadyState(*pAsset)"), std::string::npos);
	EXPECT_NE(WorkshopFailureBody.find("GPU_UPLOAD_BUDGET"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsLocalEntityBgStartsPreviewPipelineAndKeepsFallbackVisible)
{
	// Entity Background Image cards are separate from the Entities tab. The artifact
	// pipeline may be pending, but the old map/video fallback must stay visible.
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());
	const size_t LocalBranch = Body.find("if(!UsesCombinedAssetList(pCurrentCategory))");
	ASSERT_NE(LocalBranch, std::string::npos);
	const size_t LocalBranchEnd = Body.find("if(const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab); UsesCombinedAssetList(pCategory)", LocalBranch);
	ASSERT_NE(LocalBranchEnd, std::string::npos);
	const std::string LocalBody = Body.substr(LocalBranch, LocalBranchEnd - LocalBranch);

	EXPECT_NE(LocalBody.find("BuildAssetsResourcePreviewKey("), std::string::npos);
	EXPECT_NE(LocalBody.find("SettingsResourcePreviewDrawResult("), std::string::npos);
	EXPECT_NE(LocalBody.find("StartAssetsEntityBgPreviewArtifactJob("), std::string::npos);
	EXPECT_NE(LocalBody.find("RenderAssetsCardPreview(Shell, PreviewState"), std::string::npos);
	EXPECT_NE(LocalBody.find("if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !PreviewReady)"), std::string::npos);
	EXPECT_EQ(LocalBody.find("if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !PreviewReady && !AssetsContentWarmupBlocked)"), std::string::npos);
	EXPECT_NE(LocalBody.find("RenderEntityBgFallback(Shell.m_TextureRect)"), std::string::npos);
	EXPECT_NE(LocalBody.find("RenderEntityBgVideoFallback(Shell.m_TextureRect)"), std::string::npos);
}

TEST(QmMonitoringHelpers, TeeListDoesNotExposePartialSkinPreviewUploads)
{
	// The tee list must not render a CSkin while only some sprites have uploaded.
	// Partially uploaded skins looked like broken/default tees in the settings list.
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const CSkin *pSkin = State == CSkins::CSkinContainer::EState::LOADED ? pSkinContainer->Skin().get() : pDefaultSkin;"), std::string::npos);
	EXPECT_EQ(Body.find("const CSkin *pSkin = pSkinContainer->Skin() != nullptr ? pSkinContainer->Skin().get() : pDefaultSkin;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEntitiesPreviewUsesTileArtifactRenderer)
{
	// This is the Entities asset tab, not Entity Background Image. Entities need
	// the tile-layout preview renderer; drawing the source texture as one quad
	// makes the card preview look missing or wrong.
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("bool m_DrawEntityTileArtifact = false;"), std::string::npos);
	EXPECT_NE(Body.find("auto RenderAssetsEntityTilePreviewArtifact = "), std::string::npos);
	EXPECT_NE(Body.find("PreviewState.m_DrawEntityTileArtifact = PreviewState.m_Texture.IsValid();"), std::string::npos);
	EXPECT_NE(Body.find("PreviewState.m_DrawEntityTileArtifact = s_CurCustomTab == ASSETS_TAB_ENTITIES && gs_SettingsAssetsEntityGamePreview && Asset.m_ThumbTexture.IsValid();"), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsEntityTilePreviewArtifact(PreviewFrameRect, PreviewState.m_Texture);"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsFrameSchedulerExposesResourceAndTextBudgets)
{
	const std::string Header = ReadRepoFile("src/game/client/components/settings_resource_jobs.h");
	const std::string Source = ReadRepoFile("src/game/client/components/settings_resource_jobs.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");

	EXPECT_NE(Header.find("m_FrameId"), std::string::npos);
	EXPECT_NE(Header.find("m_aOperation"), std::string::npos);
	EXPECT_NE(Header.find("m_aPage"), std::string::npos);
	EXPECT_NE(Header.find("m_aTab"), std::string::npos);
	EXPECT_NE(Header.find("m_Subtab"), std::string::npos);
	EXPECT_NE(Header.find("m_aContext"), std::string::npos);
	EXPECT_NE(Header.find("m_TabSwitchFirstFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_FramePressure"), std::string::npos);
	EXPECT_NE(Header.find("m_ResourceUploadTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_TextContainerTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_GlyphRasterizeTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_GlyphUploadTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_MetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_PreviewArtifactTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_TextureUploadTokens"), std::string::npos);
	EXPECT_NE(Header.find("m_CardDrawTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_ResourceUploadTokens = Output.m_GpuUploadTokens;"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_TextContainerTokens = Output.m_TextPrebuildTokens;"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_GlyphRasterizeTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_GlyphUploadTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_MetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_PreviewArtifactTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_TextureUploadTokens"), std::string::npos);
	EXPECT_NE(Source.find("Output.m_CardDrawTokens"), std::string::npos);
	EXPECT_NE(Menus.find("resource_upload_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("text_container_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("glyph_rasterize_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("glyph_upload_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("paragraph_layout_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("metadata_layout_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("preview_artifact_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("texture_upload_tokens=%d"), std::string::npos);
	EXPECT_NE(Menus.find("card_draw_tokens=%d"), std::string::npos);

	SSettingsAdaptiveBudgetState State;
	SSettingsAdaptiveBudgetInput Input;
	Input.m_FrameMsAverage = 2.0f;
	Input.m_FrameMsP95 = 3.0f;
	Input.m_BackgroundBacklog = 4;
	const SSettingsAdaptiveBudgetOutput Output = SettingsAdaptiveBudgetStep(Input, State);
	EXPECT_EQ(Output.m_ResourceUploadTokens, Output.m_GpuUploadTokens);
	EXPECT_EQ(Output.m_TextContainerTokens, Output.m_TextPrebuildTokens);
	EXPECT_GE(Output.m_GlyphRasterizeTokens, 1);
	EXPECT_GE(Output.m_GlyphUploadTokens, 1);
	EXPECT_GE(Output.m_ParagraphLayoutTokens, 1);
	EXPECT_GE(Output.m_MetadataLayoutTokens, 1);
	EXPECT_GE(Output.m_PreviewArtifactTokens, 1);
	EXPECT_GE(Output.m_TextureUploadTokens, 1);
	EXPECT_GE(Output.m_CardDrawTokens, 1);

	SSettingsAdaptiveBudgetInput PressureInput;
	PressureInput.m_FrameMsAverage = 20.0f;
	PressureInput.m_FrameMsP95 = 20.0f;
	PressureInput.m_TargetFrameMs = 8.333f;
	PressureInput.m_BackgroundBacklog = 4;
	const SSettingsAdaptiveBudgetOutput PressureOutput = SettingsAdaptiveBudgetStep(PressureInput, State);
	EXPECT_EQ(PressureOutput.m_ParagraphLayoutTokens, 0);
	EXPECT_LE(PressureOutput.m_PreviewArtifactTokens, 1);
}

TEST(QmMonitoringHelpers, SettingsSchedulerFeedsTextRuntimeCostIntoAdaptiveBudget)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string PrepareBody = ExtractSourceFunctionBody(Source, "void CMenus::PrepareSettingsAdaptiveBudgetInput(SSettingsAdaptiveBudgetInput &Input)");
	const std::string LogBody = ExtractSourceFunctionBody(Source, "void CMenus::LogSettingsAdaptiveBudget(const char *pSource, const SSettingsAdaptiveBudgetInput &Input, const SSettingsAdaptiveBudgetOutput &Output) const");
	ASSERT_FALSE(PrepareBody.empty());
	ASSERT_FALSE(LogBody.empty());

	EXPECT_NE(Header.find("m_TextContainerCreateMsEwma"), std::string::npos);
	EXPECT_NE(PrepareBody.find("TextRender()->QmTextRuntimeBudgetSnapshot()"), std::string::npos);
	EXPECT_NE(PrepareBody.find("Input.m_TextContainerCreateMsEwma"), std::string::npos);
	EXPECT_NE(LogBody.find("text_create_ewma_ms=%.3f"), std::string::npos);
	EXPECT_NE(LogBody.find("text_scroll_cap=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, UiFrameSchedulerOwnsTextAndResourceBudgets)
{
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");

	EXPECT_NE(Menus.find("BeginSettingsUiFrameScheduler"), std::string::npos);
	EXPECT_NE(Menus.find("CurrentSettingsUiFrameBudget"), std::string::npos);
	EXPECT_NE(Assets.find("CurrentSettingsUiFrameBudget()"), std::string::npos);
	EXPECT_NE(Assets.find("AdaptiveBudget.m_MetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Assets.find("AdaptiveBudget.m_PreviewArtifactTokens"), std::string::npos);
	EXPECT_NE(Assets.find("AdaptiveBudget.m_TextureUploadTokens"), std::string::npos);
	EXPECT_NE(Ingame.find("GameClient()->FrameScheduler()->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, TextBudgetInput)"), std::string::npos);
	EXPECT_NE(Ingame.find("m_IngameTextFrameBudget.m_TextContainerTokens"), std::string::npos);
	EXPECT_NE(Ingame.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Skins.find("SettingsGpuUploadFrameBudgetForFrame()"), std::string::npos);
	EXPECT_NE(Skins.find("SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsAndSkinsUseSharedTextureUploadDrain)
{
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");
	const std::string Preview = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string AssetsBody = ExtractSourceFunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	const std::string SkinProcessBody = ExtractSourceFunctionBody(Skins, "CSkins::ESkinProcessResult CSkins::ProcessSkinContainer(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,\n\tint &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,\n\tstd::chrono::nanoseconds MaxTime)");
	const std::string SkinDrainBody = ExtractSourceFunctionBody(Skins, "CSkins::ESkinProcessResult CSkins::DrainSettingsSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,\n\tint &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,\n\tstd::chrono::nanoseconds MaxTime)");
	ASSERT_FALSE(AssetsBody.empty());
	ASSERT_FALSE(SkinProcessBody.empty());
	ASSERT_FALSE(SkinDrainBody.empty());

	EXPECT_NE(Preview.find("CSettingsResourcePreviewUploadScheduler"), std::string::npos);
	EXPECT_NE(Preview.find("SettingsResourcePreviewImageValidForUpload"), std::string::npos);
	EXPECT_NE(Preview.find("EnqueueUploadToTarget"), std::string::npos);
	EXPECT_NE(Assets.find("gs_SettingsAssetsResourcePreviewUploadScheduler.Drain("), std::string::npos);
	EXPECT_NE(AssetsBody.find("EnqueueUploadToTarget("), std::string::npos);
	EXPECT_EQ(AssetsBody.find("ReplaceCustomItemPreviewTexture("), std::string::npos);
	EXPECT_EQ(AssetsBody.find("ReplaceWorkshopThumbTexture("), std::string::npos);
	EXPECT_NE(Assets.find("ResourcePreviewUploadBudget.m_MaxUploads = ResourcePreviewUploadMergeBudget.m_MaxGpuUploads;"), std::string::npos);
	EXPECT_NE(Assets.find("ResourcePreviewUploadBudget.m_pFrameBudget = SettingsFrameBudget();"), std::string::npos);
	EXPECT_NE(Skins.find("SResourcePreviewUploadBudget SkinPreviewUploadBudget"), std::string::npos);
	EXPECT_NE(SkinDrainBody.find("LoadSkinFinish(pSkinContainer"), std::string::npos);
	EXPECT_NE(SkinDrainBody.find("SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget, SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)"), std::string::npos);
	EXPECT_EQ(SkinDrainBody.find("BeginSkinPreviewUpload(pSkinContainer"), std::string::npos);
	EXPECT_EQ(SkinDrainBody.find("UploadNextSkinPreviewSprite(pSkinContainer, SkinPreviewUploadBudget)"), std::string::npos);
	EXPECT_EQ(SkinDrainBody.find("FinishSkinPreviewUpload(pSkinContainer)"), std::string::npos);
	EXPECT_NE(Skins.find("preview_uploads"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameServerInfoUsesStableTextAndSnapshotCache)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfo(CUIRect MainView)");
	const std::string SetGamePageBody = ExtractSourceFunctionBody(Menus, "void CMenus::SetGamePage(int NewPage)");
	const std::string RenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::Render()");
	const std::string ValueBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderIngameServerInfoValueCached(const char *pTextId, unsigned &TextHash, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	const std::string SnapshotDrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainSnapshotTextContainers()");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(SetGamePageBody.empty());
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(ValueBody.empty());
	ASSERT_FALSE(SnapshotDrainBody.empty());

	EXPECT_NE(Header.find("struct SIngameServerInfoTextSnapshot"), std::string::npos);
	EXPECT_NE(Header.find("struct SMenuSnapshotTextKey"), std::string::npos);
	EXPECT_NE(Header.find("m_SnapshotTextCache"), std::string::npos);
	EXPECT_NE(Header.find("m_SnapshotTextPending"), std::string::npos);
	EXPECT_NE(Source.find("RenderIngameServerInfoValueCached("), std::string::npos);
	EXPECT_NE(Source.find("RequestSnapshotTextContainer("), std::string::npos);
	EXPECT_NE(Source.find("DrainSnapshotTextContainers()"), std::string::npos);
	EXPECT_NE(Body.find("DoServerInfoField(\"ingame-server-info-address-label\""), std::string::npos);
	EXPECT_NE(Body.find("RenderIngameServerInfoValueCached(\"ingame-server-info-name-value\""), std::string::npos);
	EXPECT_NE(Body.find("RenderIngameServerInfoValueCached(pValueTextId, ValueHash"), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-server-info-address-value\""), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-server-info-ping-value\""), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-server-info-version-value\""), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-server-info-password-value\""), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-game-info-type-value\""), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-game-info-map-value\""), std::string::npos);
	EXPECT_NE(Body.find("\"ingame-game-info-players-value\""), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel(&Label, CurrentServerInfo.m_aName"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel(&ValueRect"), std::string::npos);
	EXPECT_NE(Body.find("m_IngameServerInfoTextSnapshot"), std::string::npos);
	EXPECT_NE(SetGamePageBody.find("NewPage == PAGE_SERVER_INFO"), std::string::npos);
	EXPECT_NE(SetGamePageBody.find("StartSettingsPerfFixedWindow(\"ingame_server_info\", \"online\", \"game\", \"server_info\", 30)"), std::string::npos);
	EXPECT_NE(RenderBody.find("if(m_GamePage == PAGE_SETTINGS)\n\t\t\t\tTextVisibleGuard.emplace(this);"), std::string::npos);
	EXPECT_NE(ValueBody.find("RenderSnapshotTextContainer(*pReadyElement"), std::string::npos);
	EXPECT_EQ(ValueBody.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, *pReadyElement"), std::string::npos);
	EXPECT_NE(SnapshotDrainBody.find("m_IngameTextFrameBudget.m_TextContainerTokens"), std::string::npos);
	EXPECT_EQ(SnapshotDrainBody.find("m_CurrentSettingsUiFrameBudget.m_TextContainerTokens"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameEscOpenHasConcreteSectionTelemetry)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string RenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::Render()");
	const std::string GameBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderGame(CUIRect MainView)");
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(GameBody.empty());

	EXPECT_EQ(Menus.find("ingame_esc_button_column"), std::string::npos);
	EXPECT_NE(GameBody.find("ingame_esc_button_column"), std::string::npos);
	EXPECT_NE(GameBody.find("MainView.HSplitTop(45.0f + (HasSecondaryButtonBar ? 35.0f : 0.0f), &ButtonBars, &MainView);"), std::string::npos);
	EXPECT_LT(GameBody.find("MainView.HSplitTop(45.0f + (HasSecondaryButtonBar ? 35.0f : 0.0f), &ButtonBars, &MainView);"), GameBody.find("ingame_esc_button_column"));
	const std::string ButtonColumnLog = "LogIngamePerfStage(Client(), \"ingame_esc_button_column\", ButtonColumnTimer.ElapsedMs(), false, aButtonColumnPerfExtra);";
	const size_t ButtonColumnTimerPos = GameBody.find("CPerfTimer ButtonColumnTimer;");
	const size_t ButtonColumnLogPos = GameBody.find(ButtonColumnLog);
	const size_t LastButtonControlPos = GameBody.find("Console()->ExecuteLine(\"toggle_local_console\", IConsole::CLIENT_ID_UNSPECIFIED);");
	const size_t TouchEditingBranchPos = GameBody.find("if(GameClient()->m_TouchControls.IsEditingActive())", LastButtonControlPos);
	const size_t NormalButtonColumnFlushPos = GameBody.find("\n\tLogButtonColumnPerf();", LastButtonControlPos);
	ASSERT_NE(ButtonColumnTimerPos, std::string::npos);
	ASSERT_NE(ButtonColumnLogPos, std::string::npos);
	ASSERT_NE(LastButtonControlPos, std::string::npos);
	ASSERT_NE(TouchEditingBranchPos, std::string::npos);
	ASSERT_NE(NormalButtonColumnFlushPos, std::string::npos);
	EXPECT_EQ(CountSubstring(GameBody, ButtonColumnLog), 1u);
	EXPECT_LT(ButtonColumnTimerPos, ButtonColumnLogPos);
	EXPECT_LT(LastButtonControlPos, NormalButtonColumnFlushPos);
	EXPECT_LT(NormalButtonColumnFlushPos, TouchEditingBranchPos);
	EXPECT_LT(NormalButtonColumnFlushPos, GameBody.find("RenderTouchControlsEditor"));
	EXPECT_LT(NormalButtonColumnFlushPos, GameBody.find("RenderTouchButtonEditor"));
	EXPECT_LT(NormalButtonColumnFlushPos, GameBody.find("RenderConfigSettings"));
	EXPECT_LT(NormalButtonColumnFlushPos, GameBody.find("RenderPreviewSettings"));

	const std::string ButtonColumnRegion = GameBody.substr(ButtonColumnTimerPos, TouchEditingBranchPos - ButtonColumnTimerPos);
	std::istringstream RegionStream(ButtonColumnRegion);
	std::string Line;
	std::string PreviousNonEmptyLine;
	while(std::getline(RegionStream, Line))
	{
		const std::string TrimmedLine = Trim(Line);
		if(TrimmedLine.empty())
			continue;
		if(TrimmedLine.find("return;") != std::string::npos)
		{
			if(PreviousNonEmptyLine != "if(ButtonColumnPerfLogged)")
				EXPECT_EQ(PreviousNonEmptyLine, "LogButtonColumnPerf();");
		}
		PreviousNonEmptyLine = TrimmedLine;
	}

	EXPECT_NE(RenderBody.find("ingame_esc_menu_shell"), std::string::npos);
	EXPECT_NE(RenderBody.find("ingame_esc_tab_content"), std::string::npos);
	EXPECT_NE(RenderBody.find("ingame_server_info_layout"), std::string::npos);
	EXPECT_NE(RenderBody.find("const char *pOperationName = SettingsPerfActiveOperation();"), std::string::npos);
	EXPECT_NE(RenderBody.find("operation=%s context=online page=%s tab=none frame=%"), std::string::npos);
	EXPECT_NE(RenderBody.find("context=online"), std::string::npos);
	EXPECT_NE(RenderBody.find("tab=none"), std::string::npos);
	EXPECT_NE(RenderBody.find("frame=%\" PRIu64"), std::string::npos);
	EXPECT_NE(RenderBody.find("LogPerfStage(Client(), \"ingame_esc_menu_shell\", ShellTimer.ElapsedMs(), false, aEscPerfExtra);"), std::string::npos);
	EXPECT_NE(RenderBody.find("LogPerfStage(Client(), \"ingame_esc_tab_content\", StageTimer.ElapsedMs(), TransitionActive, aEscPerfExtra);"), std::string::npos);
	EXPECT_NE(RenderBody.find("LogPerfStage(Client(), \"ingame_server_info_layout\", StageDurationMs, TransitionActive, aEscPerfExtra);"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerInfoTextPreparedOnlyWhenOpeningServerInfoPage)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string SetGamePageBody = ExtractSourceFunctionBody(Menus, "void CMenus::SetGamePage(int NewPage)");
	const std::string RenderBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderServerInfo(CUIRect MainView)");
	ASSERT_FALSE(OnRenderBody.empty());
	ASSERT_FALSE(SetGamePageBody.empty());
	ASSERT_FALSE(RenderBody.empty());

	// Esc-opening PAGE_GAME and switching to PAGE_SERVER_INFO must not do the
	// dynamic server-info snapshot/MOTD prepare synchronously in the input path.
	// The prepare is allowed only from the OnRender frame-end background path.
	EXPECT_NE(Header.find("void PrepareIngameServerInfoTextRuntime("), std::string::npos);
	EXPECT_NE(Ingame.find("void CMenus::PrepareIngameServerInfoTextRuntime("), std::string::npos);
	EXPECT_NE(OnRenderBody.find("PrepareIngameServerInfoTextRuntime("), std::string::npos);
	EXPECT_EQ(SetGamePageBody.find("PrepareIngameServerInfoTextRuntime("), std::string::npos);
	EXPECT_NE(SetGamePageBody.find("NewPage == PAGE_SERVER_INFO"), std::string::npos);
	EXPECT_NE(SetGamePageBody.find("m_IngameServerInfoBackgroundPrepareRequested = false;"), std::string::npos);
	EXPECT_EQ(RenderBody.find("PrepareIngameServerInfoTextRuntime("), std::string::npos);
	EXPECT_NE(Ingame.find("event=server_info_text_prepare"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameServerInfoBackgroundPrepareDoesNotDrainMotdParagraph)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string PrepareBody = ExtractSourceFunctionBody(Ingame, "void CMenus::PrepareIngameServerInfoTextRuntime(const CUIRect *pMainView)");
	ASSERT_FALSE(OnRenderBody.empty());
	ASSERT_FALSE(PrepareBody.empty());

	// Esc-open background prewarm may prepare the small server-info value
	// snapshots and enqueue the MOTD paragraph, but it must not synchronously
	// drain the paragraph. Visible server-info frames must not run paragraph
	// drain either; that work is only allowed through the non-visible background
	// scheduler path.
	EXPECT_NE(OnRenderBody.find("PrepareIngameServerInfoTextRuntime();"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("DrainIngameUiSnapshotTextRuntime();"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("m_IngameServerInfoBackgroundPrepareRequested = !m_SnapshotTextPending.empty() || m_IngameMotdParagraphCache.m_Pending;"), std::string::npos);
	EXPECT_NE(PrepareBody.find("RequestIngameMotdParagraphCache("), std::string::npos);
	EXPECT_EQ(PrepareBody.find("DrainIngameUiTextRuntime("), std::string::npos);
	EXPECT_NE(PrepareBody.find("RequestSnapshotTextContainer("), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerInfoDoesNotShowVisiblePlaceholderOnCacheMiss)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string ValueBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderIngameServerInfoValueCached(const char *pTextId, unsigned &TextHash, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	const std::string SnapshotBody = ExtractSourceFunctionBody(Source, "bool CMenus::RequestSnapshotTextContainer(const char *pScope, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, CUIElement **ppReadyElement)");
	ASSERT_FALSE(ValueBody.empty());
	ASSERT_FALSE(SnapshotBody.empty());

	// Dynamic values may be budgeted, but the visible server-info card should
	// reuse the previous ready snapshot instead of showing a user-visible
	// placeholder/loading state on a cache miss.
	EXPECT_NE(Header.find("CUIElement *m_pLastReadyElement"), std::string::npos);
	EXPECT_NE(SnapshotBody.find("m_pLastReadyElement"), std::string::npos);
	EXPECT_NE(ValueBody.find("RenderSnapshotTextContainer(*pReadyElement"), std::string::npos);
	EXPECT_NE(ValueBody.find("RenderSnapshotTextContainer(*pLastReadyElement"), std::string::npos);
	EXPECT_EQ(ValueBody.find("Loading"), std::string::npos);
	// Dynamic values are content, not fixed UI chrome. A miss may enqueue a
	// snapshot text request, but must not synchronously draw through Ui()->DoLabel
	// because that recreates text containers in the visible server-info frame.
	EXPECT_EQ(ValueBody.find("Ui()->DoLabel"), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdParagraphMissDoesNotRecreateInRenderPath)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	ASSERT_FALSE(Body.empty());
	const std::string RequestBody = ExtractSourceFunctionBody(Source, "bool CMenus::RequestIngameMotdParagraphCache(CUIRect Motd, float FontSize)");
	ASSERT_FALSE(RequestBody.empty());

	EXPECT_NE(Header.find("struct SIngameMotdParagraphCache"), std::string::npos);
	EXPECT_NE(Header.find("m_IngameMotdParagraphCache"), std::string::npos);
	EXPECT_NE(Source.find("RequestIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(Body.find("RequestIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_EQ(Source.find("EnsureIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(RequestBody.find("if(!m_IngameMotdParagraphCache.m_Pending ||"), std::string::npos);
	EXPECT_EQ(RequestBody.find("SettingsAdaptiveBudgetStep("), std::string::npos);
	EXPECT_EQ(RequestBody.find("TextRender()->RecreateTextContainer("), std::string::npos);
	EXPECT_EQ(Body.find("TextRender()->RecreateTextContainer("), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdParagraphHydratesOnlyThroughBudgetDrain)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Header.find("m_Pending"), std::string::npos);
	EXPECT_NE(Header.find("m_PendingFrame"), std::string::npos);
	EXPECT_NE(Source.find("GameClient()->FrameScheduler()->ComputeBudget(EFrameSchedulerConsumer::IngameServerInfo, TextBudgetInput)"), std::string::npos);
	EXPECT_EQ(Source.find("BeginSettingsUiFrameScheduler(\"ingame_server_info_snapshot_text\""), std::string::npos);
	EXPECT_EQ(DrainBody.find("BeginSettingsUiFrameScheduler("), std::string::npos);
	EXPECT_NE(DrainBody.find("ParagraphLayoutTokens <= 0"), std::string::npos);
	EXPECT_NE(DrainBody.find("--m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(Source.find("paragraph_budget_blocked"), std::string::npos);
	EXPECT_NE(Source.find("paragraph_cache_hit"), std::string::npos);
	EXPECT_NE(Source.find("paragraph_layout_ms"), std::string::npos);
	EXPECT_NE(DrainBody.find("TextRender()->CreateOrAppendTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_IngameMotdParagraphCache.m_PendingFrame"), std::string::npos);
	EXPECT_NE(Body.find("RequestIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_EQ(Body.find("DrainIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_NE(Source.find("void CMenus::DrainIngameUiTextRuntime(bool AllowCurrentFrame)"), std::string::npos);
	EXPECT_NE(Header.find("m_PendingRect"), std::string::npos);
	EXPECT_NE(Header.find("m_BuildByteOffset"), std::string::npos);
	EXPECT_NE(Header.find("m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(Header.find("m_IngameTextAdaptiveBudgetState"), std::string::npos);
	EXPECT_NE(Header.find("SSettingsAdaptiveBudgetOutput m_IngameTextFrameBudget"), std::string::npos);
	EXPECT_NE(Source.find("DrainIngameMotdParagraphCache(m_IngameMotdParagraphCache.m_PendingRect, m_IngameMotdParagraphCache.m_PendingFontSize, AllowCurrentFrame);"), std::string::npos);
	EXPECT_EQ(Source.find("m_IngameTextAdaptiveBudgetState"), std::string::npos);
	EXPECT_EQ(Source.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens = maximum(1, m_IngameTextFrameBudget.m_ParagraphLayoutTokens)"), std::string::npos);
	EXPECT_EQ(Source.find("m_CurrentSettingsUiFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_EQ(Body.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdParagraphDrainBuildsInChunks)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Header.find("static constexpr int INGAME_MOTD_PARAGRAPH_CHUNK_BYTES"), std::string::npos);
	EXPECT_NE(Header.find("INGAME_MOTD_PARAGRAPH_CHUNK_BYTES = 24"), std::string::npos);
	EXPECT_NE(Header.find("CTextCursor m_BuildCursor"), std::string::npos);
	EXPECT_NE(Header.find("int m_BuildByteOffset"), std::string::npos);
	EXPECT_NE(DrainBody.find("INGAME_MOTD_PARAGRAPH_CHUNK_BYTES"), std::string::npos);
	EXPECT_NE(DrainBody.find("str_utf8_isstart"), std::string::npos);
	EXPECT_NE(DrainBody.find("TextRender()->CreateOrAppendTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_IngameMotdParagraphCache.m_BuildByteOffset += ChunkLength"), std::string::npos);
	EXPECT_NE(DrainBody.find("std::swap(m_MotdTextContainerIndex, m_IngameMotdParagraphCache.m_BuildTextContainerIndex)"), std::string::npos);
	EXPECT_EQ(DrainBody.find("m_MotdTextContainerIndex = m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_IngameMotdParagraphCache.m_PreviousTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
}

TEST(QmMonitoringHelpers, VulkanFrameSubmitFailureRecordsFrameContext)
{
	const std::string VulkanSource = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const size_t QueueSubmitPos = VulkanSource.find("VkResult QueueSubmitRes = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, m_vQueueSubmitFences[m_CurImageIndex]);");
	const size_t DiagnosticPos = VulkanSource.find("frame submit failed: result=%d image=%u/%u command_buffers=%u", QueueSubmitPos);
	const size_t ErrorPos = VulkanSource.find("SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, \"Submitting to graphics queue failed.\"", QueueSubmitPos);
	const size_t SubmitEnd = VulkanSource.find("MarkFrameTimestampQueryPending(m_CurImageIndex);", QueueSubmitPos);

	EXPECT_NE(QueueSubmitPos, std::string::npos);
	EXPECT_NE(DiagnosticPos, std::string::npos);
	EXPECT_NE(ErrorPos, std::string::npos);
	ASSERT_NE(SubmitEnd, std::string::npos);
	const std::string SubmitBlock = VulkanSource.substr(QueueSubmitPos, SubmitEnd - QueueSubmitPos);
	EXPECT_LT(QueueSubmitPos, DiagnosticPos);
	EXPECT_LT(DiagnosticPos, ErrorPos);
	EXPECT_NE(SubmitBlock.find("if(pCritErrorMsg != nullptr)"), std::string::npos);
	EXPECT_NE(SubmitBlock.find("return false;"), std::string::npos);
	EXPECT_EQ(SubmitBlock.find("else"), std::string::npos);
}

TEST(QmMonitoringHelpers, VulkanNoVsyncPrefersImmediateAndKeepsNormalSwapchainDepth)
{
	const std::string VulkanSource = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const size_t FunctionStart = VulkanSource.find("[[nodiscard]] bool GetPresentationMode(VkPresentModeKHR &VKIOMode)");
	const size_t FunctionEnd = VulkanSource.find("\n\t[[nodiscard]] bool GetSurfaceProperties", FunctionStart);
	ASSERT_NE(FunctionStart, std::string::npos);
	ASSERT_NE(FunctionEnd, std::string::npos);
	const std::string FunctionBody = VulkanSource.substr(FunctionStart, FunctionEnd - FunctionStart);

	EXPECT_EQ(FunctionBody.find("#if defined(CONF_PLATFORM_MACOS)"), std::string::npos);
	const size_t Immediate = FunctionBody.find("VK_PRESENT_MODE_IMMEDIATE_KHR");
	const size_t Mailbox = FunctionBody.find("VK_PRESENT_MODE_MAILBOX_KHR", Immediate);
	ASSERT_NE(Immediate, std::string::npos);
	ASSERT_NE(Mailbox, std::string::npos);
	EXPECT_LT(Immediate, Mailbox);

	const std::string SwapImageBody = ExtractSourceFunctionBody(VulkanSource, "uint32_t GetNumberOfSwapImages(");
	ASSERT_FALSE(SwapImageBody.empty());
	EXPECT_NE(SwapImageBody.find("uint32_t ImgNumber = VKCapabilities.minImageCount + 1;"), std::string::npos);
	EXPECT_EQ(SwapImageBody.find("if(!g_Config.m_GfxVsync)"), std::string::npos);
	EXPECT_EQ(SwapImageBody.find("return VKCapabilities.minImageCount;"), std::string::npos);
}

TEST(QmMonitoringHelpers, RenderLoopKeepsConfiguredAsyncPolicyAndDisablesPerfHotPath)
{
	const std::string ClientSource = ReadRepoFile("src/engine/client/client.cpp");
	const size_t RunStart = ClientSource.find("void CClient::Run()");
	const size_t RunEnd = ClientSource.find("GameClient()->RenderShutdownMessage();", RunStart);
	ASSERT_NE(RunStart, std::string::npos);
	ASSERT_NE(RunEnd, std::string::npos);
	const std::string RunBody = ClientSource.substr(RunStart, RunEnd - RunStart);

	const size_t AsyncRenderPolicy = RunBody.find("bool AsyncRenderOld = g_Config.m_GfxAsyncRenderOld;");
	const size_t GfxRefreshRate = RunBody.find("int GfxRefreshRate = g_Config.m_GfxRefreshRate;", AsyncRenderPolicy);
	const size_t PerfPolicy = RunBody.find("const bool PerfEnabled = QmPerfEnabled();");
	const size_t OptionalLoopTimer = RunBody.find("std::optional<CPerfTimer> LoopTimer;");
	ASSERT_NE(AsyncRenderPolicy, std::string::npos);
	ASSERT_NE(GfxRefreshRate, std::string::npos);
	const std::string AsyncRenderPolicyBlock = RunBody.substr(AsyncRenderPolicy, GfxRefreshRate - AsyncRenderPolicy);
	EXPECT_EQ(RunBody.find("MacosVulkanBackend"), std::string::npos);
	EXPECT_EQ(AsyncRenderPolicyBlock.find("#if defined(CONF_PLATFORM_MACOS)"), std::string::npos);
	EXPECT_EQ(AsyncRenderPolicyBlock.find("AsyncRenderOld = false;"), std::string::npos);
	EXPECT_NE(PerfPolicy, std::string::npos);
	EXPECT_NE(OptionalLoopTimer, std::string::npos);
	EXPECT_LT(PerfPolicy, OptionalLoopTimer);
	EXPECT_EQ(RunBody.find("CPerfTimer LoopTimer;"), std::string::npos);
	EXPECT_NE(RunBody.find("int64_t AdditionalTime = GfxRefreshRate ? ((Now - LastRenderTime) - RenderFrameTicks) : 0;"), std::string::npos);
	EXPECT_NE(RunBody.find("AdditionalTime > (time_freq() / 60)"), std::string::npos);
}

TEST(QmMonitoringHelpers, DedicatedDrawCommandsDoNotConsumeRetainedVertices)
{
	const std::string GraphicsSource = ReadRepoFile("src/engine/client/graphics_threaded.cpp");
	const std::string Header = ReadRepoFile("src/engine/client/graphics_threaded.h");
	const std::string TextBody = ExtractSourceFunctionBody(GraphicsSource, "void CGraphics_Threaded::RenderText(int BufferContainerIndex");
	const std::string QuadBody = ExtractSourceFunctionBody(GraphicsSource, "void CGraphics_Threaded::RenderQuadContainer(int ContainerIndex, int QuadOffset");
	const std::string QuadExBody = ExtractSourceFunctionBody(GraphicsSource, "void CGraphics_Threaded::RenderQuadContainerEx(int ContainerIndex");

	ASSERT_FALSE(TextBody.empty());
	ASSERT_FALSE(QuadBody.empty());
	ASSERT_FALSE(QuadExBody.empty());
	EXPECT_EQ(Header.find("FlushPendingVerticesForDrawCommand"), std::string::npos);
	EXPECT_EQ(GraphicsSource.find("void CGraphics_Threaded::FlushPendingVerticesForDrawCommand()"), std::string::npos);
	EXPECT_EQ(TextBody.find("FlushVertices"), std::string::npos);
	EXPECT_EQ(QuadBody.find("FlushPendingVerticesForDrawCommand"), std::string::npos);
	EXPECT_EQ(QuadExBody.find("FlushPendingVerticesForDrawCommand"), std::string::npos);
}

TEST(QmMonitoringHelpers, VulkanKeepsBufferedR8TextPath)
{
	const std::string GraphicsSource = ReadRepoFile("src/engine/client/graphics_threaded.cpp");
	const std::string VulkanSource = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const size_t InitStart = GraphicsSource.find("int CGraphics_Threaded::IssueInit()");
	const size_t InitEnd = GraphicsSource.find("// TClient", InitStart);
	ASSERT_NE(InitStart, std::string::npos);
	ASSERT_NE(InitEnd, std::string::npos);
	const std::string InitBody = GraphicsSource.substr(InitStart, InitEnd - InitStart);

	EXPECT_NE(InitBody.find("m_GLTextBufferingEnabled = (m_GLQuadContainerBufferingEnabled && m_pBackend->HasTextBuffering());"), std::string::npos);
	EXPECT_NE(VulkanSource.find("pCommand->m_pCapabilities->m_TextBuffering = true;"), std::string::npos);
	EXPECT_EQ(VulkanSource.find("pCommand->m_pCapabilities->m_TextBuffering = false;"), std::string::npos);
	EXPECT_NE(VulkanSource.find("VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM"), std::string::npos);

	EXPECT_EQ(VulkanSource.find("Skipping text command with unavailable Vulkan resources"), std::string::npos);
}

TEST(QmMonitoringHelpers, VulkanProfilingTimersStayOutOfDisabledHotPath)
{
	const std::string VulkanSource = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string PrepareFrame = ExtractSourceFunctionBody(VulkanSource, "[[nodiscard]] bool PrepareFrame()");
	const std::string RunCommand = ExtractSourceFunctionBody(VulkanSource, "[[nodiscard]] ERunCommandReturnTypes RunCommand(");
	const std::string QueueSubmit = ExtractSourceFunctionBody(VulkanSource, "VkResult QueueSubmit(");

	ASSERT_FALSE(PrepareFrame.empty());
	ASSERT_FALSE(RunCommand.empty());
	ASSERT_FALSE(QueueSubmit.empty());
	EXPECT_NE(PrepareFrame.find("m_FrameProfilingActive = FrameProfilingEnabled();"), std::string::npos);
	EXPECT_NE(RunCommand.find("if(m_FrameProfilingActive)"), std::string::npos);
	EXPECT_NE(RunCommand.find("m_FrameProfilingActive ? time_get_nanoseconds() : 0ns"), std::string::npos);
	EXPECT_NE(QueueSubmit.find("m_FrameProfilingActive ? time_get_nanoseconds() : 0ns"), std::string::npos);
}

TEST(QmMonitoringHelpers, MacosVulkanGraphicsErrorDialogKeepsWindowAlive)
{
	const std::string BackendSource = ReadRepoFile("src/engine/client/backend_sdl.cpp");
	const std::string Body = ExtractSourceFunctionBody(BackendSource, "std::optional<int> CGraphicsBackend_SDL_GL::ShowMessageBox(const IGraphics::CMessageBox &MessageBox)");
	ASSERT_FALSE(Body.empty());

	const size_t MacosGuard = Body.find("#if defined(CONF_PLATFORM_MACOS)");
	const size_t VulkanBranch = Body.find("if(m_BackendType == EBackendType::BACKEND_TYPE_VULKAN)", MacosGuard);
	const size_t ParentlessMessageBox = Body.find("return ShowMessageBoxImpl(MessageBox, nullptr);", VulkanBranch);
	const size_t Cleanup = Body.find("m_pProcessor->ErroneousCleanup();");
	const size_t DestroyWindow = Body.find("SDL_DestroyWindow(m_pWindow);");

	ASSERT_NE(MacosGuard, std::string::npos);
	ASSERT_NE(VulkanBranch, std::string::npos);
	ASSERT_NE(ParentlessMessageBox, std::string::npos);
	ASSERT_NE(Cleanup, std::string::npos);
	ASSERT_NE(DestroyWindow, std::string::npos);
	EXPECT_LT(MacosGuard, VulkanBranch);
	EXPECT_LT(VulkanBranch, ParentlessMessageBox);
	EXPECT_LT(ParentlessMessageBox, Cleanup);
	EXPECT_LT(ParentlessMessageBox, DestroyWindow);
}

TEST(QmMonitoringHelpers, MotdUsesReadyOrStableParagraphOnly)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	const std::string ResizeBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnWindowResize()");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(ResizeBody.empty());

	// MOTD can be prepared asynchronously, but visible rendering must not show a
	// loading placeholder or blank range. The current stable paragraph remains
	// the sole owner until a completed build replaces it.
	EXPECT_NE(Header.find("m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(Header.find("m_PreviousTextContainerIndex"), std::string::npos);
	EXPECT_NE(Source.find("RenderIngameMotdStableParagraphCache("), std::string::npos);
	EXPECT_NE(Body.find("const bool RenderedMotdParagraph ="), std::string::npos);
	EXPECT_NE(Body.find("RenderIngameMotdStableParagraphCache("), std::string::npos);
	EXPECT_NE(Body.find("RenderIngameMotdFallbackText("), std::string::npos);
	EXPECT_NE(Body.find("server_info_not_ready=1"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Loading"), std::string::npos);
	EXPECT_NE(ResizeBody.find("TextRender()->DeleteTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex);"), std::string::npos);
}

TEST(QmMonitoringHelpers, ValueSelectorDisplayUsesSingleLineShrink)
{
	const std::string Header = ReadRepoFile("src/game/client/ui.h");
	const std::string Source = ReadRepoFile("src/game/client/ui.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "SEditResult<int64_t> CUi::DoValueSelectorWithState(const void *pId, const CUIRect *pRect, const char *pLabel, int64_t Current, int64_t Min, int64_t Max, const SValueSelectorProperties &Props)");
	const std::string FlagsBody = ExtractSourceFunctionBody(Source, "static int GetFlagsForLabelProperties(const SLabelProperties &LabelProps, const CTextCursor *pReadCursor)");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(FlagsBody.empty());

	EXPECT_NE(Header.find("struct SLabelProperties"), std::string::npos);
	EXPECT_NE(Header.find("bool m_DisallowNewline"), std::string::npos);
	EXPECT_NE(FlagsBody.find("LabelProps.m_DisallowNewline ? TEXTFLAG_DISALLOW_NEWLINE : 0"), std::string::npos);
	EXPECT_NE(Body.find("SLabelProperties ValueLabelProps"), std::string::npos);
	EXPECT_NE(Body.find("pRect->VMargin(2.0f, &Textbox);"), std::string::npos);
	EXPECT_NE(Body.find("ValueLabelProps.m_MaxWidth = Textbox.w"), std::string::npos);
	EXPECT_NE(Body.find("ValueLabelProps.m_DisallowNewline = true"), std::string::npos);
	EXPECT_NE(Body.find("ValueLabelProps.m_StopAtEnd = true"), std::string::npos);
	EXPECT_NE(Body.find("ValueLabelProps.m_MinimumFontSize"), std::string::npos);
	EXPECT_NE(Body.find("const char *pDisplayText = m_ActiveValueSelectorState.m_pLastTextId == pId ? m_ActiveValueSelectorState.m_NumberInput.GetDisplayedString() : aBuf;"), std::string::npos);
	EXPECT_NE(Body.find("const float ValueFontSize = QmFitSingleLineFontSize("), std::string::npos);
	EXPECT_NE(Body.find("DoLabel(&Textbox, pDisplayText, ValueFontSize, Props.m_TextAlign, ValueLabelProps);"), std::string::npos);
	EXPECT_NE(Body.find("auto RenderValueSelectorDisplay = [&](bool RenderText = true)"), std::string::npos);
	EXPECT_NE(Body.find("RenderValueSelectorDisplay();"), std::string::npos);
	EXPECT_NE(Body.find("RenderValueSelectorDisplay(false);"), std::string::npos);
	EXPECT_NE(Body.find("const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();"), std::string::npos);
	EXPECT_NE(Body.find("TextRender()->TextColor(PreviousTextColor);"), std::string::npos);
	EXPECT_LT(Body.find("RenderValueSelectorDisplay();"), Body.find("if(Inside && !MouseButton(0) && !MouseButton(1))"));
	EXPECT_EQ(Body.find("DoEditBox(&m_ActiveValueSelectorState.m_NumberInput"), std::string::npos);
	EXPECT_EQ(Body.find("DoLabel(pRect, aBuf, 10.0f, TEXTALIGN_MC);\n"), std::string::npos);
}

TEST(QmMonitoringHelpers, ValueSelectorCanFormatAndParseSpecialTextValues)
{
	const std::string Header = ReadRepoFile("src/game/client/ui.h");
	const std::string Source = ReadRepoFile("src/game/client/ui.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "SEditResult<int64_t> CUi::DoValueSelectorWithState(const void *pId, const CUIRect *pRect, const char *pLabel, int64_t Current, int64_t Min, int64_t Max, const SValueSelectorProperties &Props)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Header.find("FValueSelectorFormatCallback"), std::string::npos);
	EXPECT_NE(Header.find("FValueSelectorParseCallback"), std::string::npos);
	EXPECT_NE(Header.find("m_pfnFormatValue"), std::string::npos);
	EXPECT_NE(Header.find("m_pfnParseValue"), std::string::npos);
	EXPECT_NE(Body.find("Props.m_pfnFormatValue"), std::string::npos);
	EXPECT_NE(Body.find("Props.m_pfnParseValue"), std::string::npos);
	EXPECT_NE(Body.find("m_ActiveValueSelectorState.m_NumberInput.Set(aEditBuf);"), std::string::npos);
	EXPECT_NE(Body.find("if(Props.m_pfnParseValue != nullptr)\n\t\t\t{"), std::string::npos);
	EXPECT_NE(Body.find("if(Props.m_pfnParseValue(m_ActiveValueSelectorState.m_NumberInput.GetString(), ParsedValue, Base))"), std::string::npos);
	EXPECT_NE(Body.find("else\n\t\t\t\tCurrent = std::clamp(m_ActiveValueSelectorState.m_NumberInput.GetInteger64(Base), Min, Max);"), std::string::npos);
}

TEST(QmMonitoringHelpers, GraphicsRefreshRateInputAcceptsInfinitySymbol)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string UiFormsSource = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string NumericFieldBody = ExtractSourceFunctionBody(UiFormsSource, "bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options)");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(NumericFieldBody.empty());

	EXPECT_NE(Body.find("int InputMin = -1, int InputMax = -1"), std::string::npos);
	EXPECT_NE(Body.find("Options.m_InputMin = InputMin;"), std::string::npos);
	EXPECT_NE(Body.find("Options.m_InputMax = InputMax;"), std::string::npos);
	EXPECT_NE(Body.find("CUi::SCROLLBAR_OPTION_INFINITE | CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, 0, 10000"), std::string::npos);
	EXPECT_NE(NumericFieldBody.find("NumericFieldTextIsInfinite(pInput->GetString())"), std::string::npos);
	EXPECT_NE(NumericFieldBody.find("if(!ParsedInfinite && (Options.m_InputMin >= 0 || Options.m_InputMax >= 0))"), std::string::npos);
	EXPECT_NE(NumericFieldBody.find("Parsed = NumericFieldTextInputStoredValue("), std::string::npos);
	EXPECT_EQ(Body.find("FormatInfiniteValueSelector"), std::string::npos);
	EXPECT_EQ(Body.find("ParseInfiniteValueSelector"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientSliderValueInputReservesReadableValueWidth)
{
	const std::string Source = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const bool HasSuffix = Options.m_pSuffix != nullptr && Options.m_pSuffix[0] != '\\0';"), std::string::npos);
	EXPECT_NE(Body.find("const float MinimumValueWidth = 52.0f + SuffixWidth + 22.0f;"), std::string::npos);
	EXPECT_NE(Body.find("const float ValueWidth = std::clamp((MultiLine ? ValueRect.w : Controls.w) * 0.26f, MinimumValueWidth, 128.0f);"), std::string::npos);
	EXPECT_NE(Body.find("const bool HasSlider = MultiLine || Controls.w > ValueWidth + 42.0f;"), std::string::npos);
	EXPECT_NE(Body.find("InputField.VMargin(std::min(5.0f, ValueWidth * 0.1f), &InputField);"), std::string::npos);
	EXPECT_NE(Body.find("FieldOptions.m_pTrailingText = HasSuffix ? Options.m_pSuffix : nullptr;"), std::string::npos);
}

TEST(QmMonitoringHelpers, NumericFieldSharesScrollbarStyleAndReservesInfiniteEndpoint)
{
	const std::string FormsHeader = ReadRepoFile("src/game/client/QmUi/UiForms.h");
	const std::string FormsSource = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string UiHeader = ReadRepoFile("src/game/client/ui.h");
	const std::string UiSource = ReadRepoFile("src/game/client/ui.cpp");
	const std::string NumericBody = ExtractSourceFunctionBody(FormsSource, "bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options)");
	ASSERT_FALSE(NumericBody.empty());

	EXPECT_NE(FormsHeader.find("NumericFieldInfiniteEndpointStart"), std::string::npos);
	EXPECT_NE(NumericBody.find("const float InfiniteEndpointStart = Infinite ? NumericFieldInfiniteEndpointStart(ScrollBar.w, Ctx.m_UiScale) : 1.0f;"), std::string::npos);
	EXPECT_NE(NumericBody.find("const bool NewInfiniteValue = Infinite && NewNormalized >= (1.0f + InfiniteEndpointStart) * 0.5f;"), std::string::npos);
	EXPECT_NE(NumericBody.find("Ctx.m_pUi->RenderScrollbarH(pId, &ScrollBar, Normalized);"), std::string::npos);
	EXPECT_NE(UiHeader.find("void RenderScrollbarH(const void *pId"), std::string::npos);
	EXPECT_NE(UiSource.find("void CUi::RenderScrollbarH(const void *pId"), std::string::npos);
}

TEST(QmMonitoringHelpers, SkinTransitionDurationLabelUsesSingleLineShrink)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmVisualSkinTransitionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("SLabelProperties DurationLabelProps"), std::string::npos);
	EXPECT_NE(Body.find("DurationLabelProps.m_DisallowNewline = true"), std::string::npos);
	EXPECT_NE(Body.find("DurationLabelProps.m_StopAtEnd = true"), std::string::npos);
	EXPECT_NE(Body.find("DurationLabelProps.m_MinimumFontSize = 6.0f"), std::string::npos);
	EXPECT_NE(Body.find("RenderQmVisualLabel(\"qmclient-skin-transition-duration\", &LabelColumn, Localize(\"Skin transition duration\"), BodySize, TEXTALIGN_ML, DurationLabelProps);"), std::string::npos);
}

TEST(QmMonitoringHelpers, TeeSkinQueueOmitsCapacityAndUsesSharedIntervalNumericField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("const bool CompactQueueCapacityControls"), std::string::npos);
	EXPECT_EQ(Body.find("CUIRect QueueEnabledRect;"), std::string::npos);
	EXPECT_EQ(Body.find("QueueDirty ? \"● \""), std::string::npos);
	EXPECT_NE(Body.find("str_format(aQueueLabel, sizeof(aQueueLabel), \"%s (%d)\", Localize(\"Skin queue\"), (int)SkinQueue.size());"), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsButton_CheckBox(SETTINGS_TEE, -1, -1, &QueueEnabled, QueueDummy ? \"tee-dummy-skin-queue-enabled\" : \"tee-player-skin-queue-enabled\", aQueueLabel, QueueEnabled, &QueueHeader"), std::string::npos);
	EXPECT_NE(Body.find("Ui()->DoLabel(&QueueListHeaderLabel, aCurrentQueueLabel"), std::string::npos);
	EXPECT_EQ(Body.find("DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, \"tee_queue_list_label\", &QueueListHeaderLabel, Localize(\"Skin queue\")"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Enable skin queue\"), QueueEnabled"), std::string::npos);
	EXPECT_NE(Body.find("CUIRect IntervalRow, IntervalLabel, IntervalControls;"), std::string::npos);
	EXPECT_NE(Body.find("const bool StackQueueInterval = QueueSection.w <"), std::string::npos);
	EXPECT_NE(Body.find("QueueSection.HSplitTop(QueueIntervalRowHeight, &IntervalRow, &QueueSection);"), std::string::npos);
	EXPECT_NE(Body.find("if(StackQueueInterval)"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Enabled\")"), std::string::npos);
	EXPECT_EQ(Body.find("tee-skin-queue-enabled-heading"), std::string::npos);
	EXPECT_NE(Body.find("IntervalRow.VSplitLeft(minimum(IntervalRow.w, QueueIntervalLabelWidth), &IntervalLabel, &IntervalControls);"), std::string::npos);
	EXPECT_NE(Body.find("const float QueueValueInputWidth = 58.0f * UiScale"), std::string::npos);
	EXPECT_NE(Body.find("TextRender()->TextWidth(TeeMetrics.m_SmallSize, \"ms\")"), std::string::npos);
	EXPECT_NE(Body.find("SLabelProperties QueueControlLabelProps;"), std::string::npos);
	EXPECT_NE(Body.find("QueueControlLabelProps.m_DisallowNewline = true"), std::string::npos);
	EXPECT_NE(Body.find("QueueControlLabelProps.m_MinimumFontSize = 6.0f"), std::string::npos);
	EXPECT_NE(Body.find("static ui_widget::SNumericFieldState s_aQueueIntervalStates[NUM_DUMMIES];"), std::string::npos);
	const size_t InputCtxPos = Body.find("IUiContext TeeSkinQueueIntervalCtx;");
	const size_t InputCtxUiPos = Body.find("TeeSkinQueueIntervalCtx.m_pUi = Ui();", InputCtxPos);
	const size_t InputCtxAnimPos = Body.find("TeeSkinQueueIntervalCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", InputCtxUiPos);
	const size_t InputCtxTreePos = Body.find("TeeSkinQueueIntervalCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", InputCtxAnimPos);
	const size_t InputCtxScopePos = Body.find("TeeSkinQueueIntervalCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tee_skin_queue_interval_text_input\");", InputCtxTreePos);
	const size_t InputCtxFrameDtPos = Body.find("TeeSkinQueueIntervalCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", InputCtxScopePos);
	const size_t NumericFieldPos = Body.find("ui_widget::NumericField(TeeSkinQueueIntervalCtx, &s_aQueueIntervalStates[QueueDummy], &QueueInterval, &QueueInterval, 1, 120000, IntervalInputGroup, QueueIntervalOptions);", InputCtxFrameDtPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(InputCtxPos, std::string::npos);
	EXPECT_NE(InputCtxUiPos, std::string::npos);
	EXPECT_NE(InputCtxAnimPos, std::string::npos);
	EXPECT_NE(InputCtxTreePos, std::string::npos);
	EXPECT_NE(InputCtxScopePos, std::string::npos);
	EXPECT_NE(InputCtxFrameDtPos, std::string::npos);
	EXPECT_NE(NumericFieldPos, std::string::npos);
	EXPECT_LT(InputCtxPos, InputCtxUiPos);
	EXPECT_LT(InputCtxUiPos, InputCtxAnimPos);
	EXPECT_LT(InputCtxAnimPos, InputCtxTreePos);
	EXPECT_LT(InputCtxTreePos, InputCtxScopePos);
	EXPECT_LT(InputCtxScopePos, InputCtxFrameDtPos);
	EXPECT_LT(InputCtxFrameDtPos, NumericFieldPos);
	EXPECT_NE(Body.find("QueueIntervalOptions.m_pSuffix = \"ms\";"), std::string::npos);
	EXPECT_NE(Body.find("QueueIntervalOptions.m_TrailingWidth = QueueValueUnitWidth;"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&QueueIntervalInput, &IntervalInput, 10.0f, IGraphics::CORNER_ALL, {}, TEXTALIGN_MC)"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel(&IntervalUnit, \"ms\""), std::string::npos);
	EXPECT_EQ(Body.find("DoValueSelectorWithState(&s_aQueueIntervalInputIds[QueueDummy]"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoScrollbarH(&QueueInterval"), std::string::npos);
	EXPECT_EQ(Body.find("IntervalScrollbar"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Queue capacity\")"), std::string::npos);
	EXPECT_EQ(Body.find("static char s_aQueueLengthInputIds[NUM_DUMMIES];"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Length\")"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Queue limit\")"), std::string::npos);
	EXPECT_EQ(Body.find("DoSettingsScrollbarOption(SETTINGS_TEE, -1, -1, \"tee-skin-queue-length\""), std::string::npos);
}

TEST(QmMonitoringHelpers, TeeSkinQueueDragShowsDropLineAndGhostRow)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("static vec2 s_QueueDragGrabOffset = vec2(0.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(Body.find("static CUIRect s_QueueDraggedRect;"), std::string::npos);
	EXPECT_NE(Body.find("CUIRect QueueDropLine;"), std::string::npos);
	EXPECT_NE(Body.find("HasQueueDropLine = true;"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), QueueDropLine, ColorRGBA(0.45f, 0.7f, 1.0f, 0.9f), ColorRGBA(), 1.0f);"), std::string::npos);
	EXPECT_NE(Body.find("CUIRect QueueDragGhost = s_QueueDraggedRect;"), std::string::npos);
	EXPECT_NE(Body.find("QueueDragGhost.x = Ui()->MouseX() - s_QueueDragGrabOffset.x;"), std::string::npos);
	EXPECT_NE(Body.find("DrawRoundedSurface(Ui(), QueueDragGhost, ColorRGBA(0.18f, 0.2f, 0.24f, 0.92f), ColorRGBA(), 4.0f);"), std::string::npos);
	EXPECT_NE(Body.find("QueueDragGhostLabel"), std::string::npos);
	EXPECT_NE(Body.find("s_QueueDragGrabOffset = Ui()->MousePos() - vec2(Item.m_Rect.x, Item.m_Rect.y);"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameTextRuntimeSkipsIdleLogLines)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	ASSERT_FALSE(Body.empty());

	// The perf log can run for millions of frames. Avoid 0-cost text runtime rows
	// that only inflate logs and reports; keep rows when miss/block/layout happens.
	EXPECT_NE(Body.find("if(CacheHit || CacheMiss || BudgetBlocked || ParagraphLayoutMs >= QmPerfThresholdMs())"), std::string::npos);
	EXPECT_NE(Body.find("QmPerfLogPayload(\"perf/text\", aPayload, Client(), \"game\");"), std::string::npos);
}

TEST(QmMonitoringHelpers, RenderPathsDoNotCreateTextContainersSynchronously)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string LabelBody = ExtractSourceFunctionBody(Menus, "void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)");
	const std::string DrainBody = ExtractSourceFunctionBody(Menus, "void CMenus::DrainMenuTextContainerBuildRequests()");
	const std::string MotdBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	const std::string AssetsBody = ExtractSourceFunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(LabelBody.empty());
	ASSERT_FALSE(DrainBody.empty());
	ASSERT_FALSE(MotdBody.empty());
	ASSERT_FALSE(AssetsBody.empty());

	const size_t QueueBranch = LabelBody.find("if(NeedsBuild && m_pSettingsTextPrebuildBudget == nullptr)");
	const size_t PrebuildBranch = LabelBody.find("if(m_pSettingsTextPrebuildBudget != nullptr)", QueueBranch);
	ASSERT_NE(QueueBranch, std::string::npos);
	ASSERT_NE(PrebuildBranch, std::string::npos);
	const std::string RenderRequestBranch = LabelBody.substr(QueueBranch, PrebuildBranch - QueueBranch);

	EXPECT_NE(LabelBody.find("QueueMenuTextContainerBuild"), std::string::npos);
	EXPECT_EQ(RenderRequestBranch.find("DrainMenuTextContainerBuild("), std::string::npos);
	EXPECT_NE(Menus.find("if(m_pSettingsTextPrebuildBudget != nullptr)"), std::string::npos);
	EXPECT_NE(Menus.find("DrainMenuTextContainerBuild(Element, pRect, pText, Size, Align, LabelProps, StrLen, pReadCursor, Render, &TextContainerRecreated);"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_CurrentSettingsUiFrameBudget.m_TextContainerTokens > 0"), std::string::npos);
	EXPECT_NE(DrainBody.find("--m_CurrentSettingsUiFrameBudget.m_TextContainerTokens"), std::string::npos);
	EXPECT_NE(DrainBody.find("DrainMenuTextContainerBuild("), std::string::npos);
	EXPECT_EQ(LabelBody.find("Ui()->DoLabelStreamed(*Element.Rect(0)"), std::string::npos);
	EXPECT_EQ(MotdBody.find("TextRender()->RecreateTextContainer("), std::string::npos);
	EXPECT_EQ(AssetsBody.find("TextRender()->RecreateTextContainer("), std::string::npos);
	EXPECT_NE(AssetsBody.find("RequestAssetsCardMetadataHydration"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameServerInfoCardTitlesHaveImmediateFallback)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfo(CUIRect MainView)");
	const std::string MotdBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(MotdBody.empty());

	// Screenshot regression: budgeted ingame stable labels can be queued on cache
	// miss, but server-info card headers must still be visible in the current
	// frame instead of disappearing until the text container drain catches up.
	EXPECT_NE(Header.find("void DoIngameMenuTitleLabel("), std::string::npos);
	EXPECT_NE(Menus.find("void CMenus::DoIngameMenuTitleLabel("), std::string::npos);
	EXPECT_NE(Body.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-server-info-title\""), std::string::npos);
	EXPECT_NE(Body.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-game-info-title\""), std::string::npos);
	EXPECT_NE(MotdBody.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-server-info-motd-title\""), std::string::npos);
	EXPECT_EQ(Body.find("DoIngameMenuTitleLabel(PAGE_SERVER_INFO, \"ingame-server-info-address-label\""), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameMenuTabsHaveImmediateTextFallback)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "int CMenus::DoMenuTabV2(CButtonContainer *pButtonContainer, const char *pText, bool Active, const CUIRect *pRect, int Corners, const ColorRGBA *pCustomDefault, const ColorRGBA *pCustomActive, const ColorRGBA *pCustomHover, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float ContentScale)");
	ASSERT_FALSE(Body.empty());

	// Screenshot regression: the Ghost / Call vote ingame tabs are critical
	// navigation labels. A budgeted streamed-text cache miss may enqueue the
	// real container, but the current frame must still draw readable text.
	EXPECT_NE(Source.find("\"ingame-tab-ghost\""), std::string::npos);
	EXPECT_NE(Source.find("\"ingame-tab-call-vote\""), std::string::npos);
	EXPECT_NE(Body.find("CUIElement::SUIElementRect *pElementRect = pTextUiElement->Rect(0);"), std::string::npos);
	EXPECT_NE(Body.find("const bool HadReadyContainer = pElementRect->m_UITextContainer.Valid();"), std::string::npos);
	EXPECT_NE(Body.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, *pTextUiElement, &Label, pText, LabelFontSize, TEXTALIGN_MC);"), std::string::npos);
	EXPECT_NE(Body.find("if(pTextUiElement != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid())"), std::string::npos);
	EXPECT_NE(Body.find("Ui()->DoLabel(&Label, pText, LabelFontSize, TEXTALIGN_MC);"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameStableLabelsHaveImmediateTextFallback)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::DoIngameMenuLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	ASSERT_FALSE(Body.empty());

	// Server-info fixed labels such as Address/Ping/Version are visible UI
	// chrome, not optional content. They still use the ingame text pool, but a
	// current-frame cache miss must not make the label disappear.
	EXPECT_NE(Ingame.find("DoIngameMenuLabel(PAGE_SERVER_INFO, pLabelTextId"), std::string::npos);
	EXPECT_NE(Body.find("CUIElement::SUIElementRect *pElementRect = Element.Rect(0);"), std::string::npos);
	EXPECT_NE(Body.find("const bool HadReadyContainer = pElementRect->m_UITextContainer.Valid();"), std::string::npos);
	EXPECT_NE(Body.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, Element, pRect, pText, Size, Align, LabelProps);"), std::string::npos);
	EXPECT_NE(Body.find("if(&Element != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid() && pRect != nullptr)"), std::string::npos);
	EXPECT_NE(Body.find("Ui()->DoLabel(pRect, pText, Size, Align, LabelProps);"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameServerInfoButtonsUseBudgetedTextPipeline)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderServerInfo(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Server-info chrome includes action buttons as well as labels. Fixed
	// buttons must use the ingame text pool so opening a text-heavy server-info
	// page does not create button text containers synchronously.
	EXPECT_NE(Header.find("DoIngameMenuButton("), std::string::npos);
	EXPECT_NE(Source.find("int CMenus::DoIngameMenuButton("), std::string::npos);
	EXPECT_NE(Body.find("DoIngameMenuButton(PAGE_SERVER_INFO, \"ingame-server-info-copy-button\""), std::string::npos);
	EXPECT_EQ(Body.find("DoButton_Menu(&s_CopyButton, Localize(\"Copy info\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameMenuButtonKeepsNativeCenteredButtonTextLayout)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "int CMenus::DoIngameMenuButton(int Page, const char *pTextId, CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding)");
	ASSERT_FALSE(Body.empty());

	// Regression guard for ingame ESC buttons: the budgeted text helper must
	// use the same text rect calculation as DoButton_Menu, while keeping the
	// ingame scope and current-frame fallback. A hand-written rect diverges from
	// native centered labels and made Chinese button text appear off-center.
	EXPECT_NE(Source.find("CUIRect MenuButtonTextRect("), std::string::npos);
	EXPECT_NE(Body.find("CUIRect Text = MenuButtonTextRect(pRect, 0.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(Body.find("DoButton_Menu(pButtonContainer, \"\", Checked"), std::string::npos);
	EXPECT_NE(Body.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_INGAME, TextElement"), std::string::npos);
	EXPECT_NE(Body.find("Ui()->DoLabel(&Text, pText"), std::string::npos);
	EXPECT_EQ(Body.find("CUIRect Text = *pRect;"), std::string::npos);
	EXPECT_EQ(Body.find("Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f"), std::string::npos);
	EXPECT_EQ(Body.find("HoverLift"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameMenuButtonDoesNotDependOnUiRuntimeAnimation)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "int CMenus::DoIngameMenuButton(int Page, const char *pTextId, CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding)");
	ASSERT_FALSE(Body.empty());

	// Regression guard for Esc-open crashes: ingame buttons can render before
	// the QmUi animation runtime is in a stable menu-frame context. Their
	// cached text path should use the native static button text rect and avoid
	// per-button animation runtime lookups.
	EXPECT_EQ(Body.find("GameClient()->UiRuntimeV2()->AnimRuntime()"), std::string::npos);
	EXPECT_EQ(Body.find("ResolveUiAnimValue("), std::string::npos);
	EXPECT_EQ(Body.find("HoverLift"), std::string::npos);
	EXPECT_NE(Body.find("CUIRect Text = MenuButtonTextRect(pRect, 0.0f, 0.0f);"), std::string::npos);
	EXPECT_NE(Body.find("DoButton_Menu(pButtonContainer, \"\", Checked"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardRightControlsStayCompact)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Screenshot regression: resource cards need the title/description to keep
	// priority. The right-side status pill and action icon are secondary
	// controls, so their reserved widths should stay compact.
	EXPECT_NE(Source.find("constexpr float AssetsCardStatusTagFontSize = 6.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetsCardStatusTagMinWidth = 24.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetsCardStatusTagMaxWidth = 36.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetsCardStatusTagHorizontalPadding = 2.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetCardHeaderControlHeight = 18.0f;"), std::string::npos);
	EXPECT_NE(Body.find("TitleRect.VSplitRight(16.0f, &TitleRect, &Shell.m_ActionButtonRect);"), std::string::npos);
	EXPECT_EQ(Source.find("constexpr float AssetsCardStatusTagMaxWidth = 52.0f;"), std::string::npos);
	EXPECT_EQ(Source.find("constexpr float AssetsCardStatusTagMaxWidth = 46.0f;"), std::string::npos);
	EXPECT_EQ(Body.find("TitleRect.VSplitRight(24.0f, &TitleRect, &Shell.m_ActionButtonRect);"), std::string::npos);
	EXPECT_EQ(Body.find("TitleRect.VSplitRight(20.0f, &TitleRect, &Shell.m_ActionButtonRect);"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsToolbarWrapsWithoutShrinkingButtons)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const int ToolbarRowCount = ToolbarUsesOwnRows ? ComputeToolbarRowCount(MainView.w) : 1;"), std::string::npos);
	EXPECT_NE(Body.find("const float FooterHeight = ToolbarUsesOwnRows ? ms_ButtonHeight * (1.0f + ToolbarRowCount) + ToolbarRowGap * ToolbarRowCount : ms_ButtonHeight;"), std::string::npos);
	EXPECT_NE(Body.find("auto PlaceToolbarButton ="), std::string::npos);
	EXPECT_EQ(Body.find("ButtonScale"), std::string::npos);
	EXPECT_EQ(Body.find("AssetsDirButtonWidth *= "), std::string::npos);
}

TEST(QmMonitoringHelpers, StreamedLabelCachesSingleLineMiddleAlignMetrics)
{
	const std::string Header = ReadRepoFile("src/game/client/ui.h");
	const std::string Source = ReadRepoFile("src/game/client/ui.cpp");
	const std::string CachedBody = ExtractSourceFunctionBody(Source, "void CUi::DoLabel(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor) const");
	const std::string StreamedBody = ExtractSourceFunctionBody(Source, "void CUi::DoLabelStreamed(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated) const");
	const std::string HelperBody = ExtractSourceFunctionBody(Source, "void CUi::RenderLabelTextContainerAligned(const CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, int Align) const");
	ASSERT_FALSE(CachedBody.empty());
	ASSERT_FALSE(StreamedBody.empty());
	ASSERT_FALSE(HelperBody.empty());

	// Streamed single-line labels still need the same vertical centering metrics
	// as the immediate DoLabel path. Otherwise centered settings buttons and
	// centered preview placeholders drift upward after the text-pool rewrite.
	EXPECT_NE(Header.find("float m_BiggestCharacterHeight;"), std::string::npos);
	EXPECT_NE(Header.find("int m_LineCount;"), std::string::npos);
	EXPECT_NE(CachedBody.find("TextBounds.m_LineCount == 1 ? &TextBounds.m_BiggestCharacterHeight : nullptr"), std::string::npos);
	EXPECT_NE(CachedBody.find("RectEl.m_BiggestCharacterHeight = TextBounds.m_BiggestCharacterHeight;"), std::string::npos);
	EXPECT_NE(CachedBody.find("RectEl.m_LineCount = TextBounds.m_LineCount;"), std::string::npos);
	EXPECT_NE(HelperBody.find("RectEl.m_LineCount == 1 ? &RectEl.m_BiggestCharacterHeight : nullptr"), std::string::npos);
	EXPECT_NE(StreamedBody.find("RectEl.m_FontSize != Size"), std::string::npos);
	EXPECT_NE(StreamedBody.find("RectEl.m_TextAlign != Align"), std::string::npos);
	EXPECT_NE(StreamedBody.find("RectEl.m_LabelMaxWidth != LabelProps.m_MaxWidth"), std::string::npos);
	EXPECT_NE(StreamedBody.find("RectEl.m_LabelFlags != Flags"), std::string::npos);
	EXPECT_EQ(StreamedBody.find("CalcAlignedCursorPos(pRect, vec2(RectEl.m_Cursor.m_LongestLineWidth, RectEl.m_Cursor.Height()), Align);"), std::string::npos);
}

TEST(QmMonitoringHelpers, StreamedLabelRenderUsesCanonicalAlignmentHelper)
{
	const std::string Source = ReadRepoFile("src/game/client/ui.cpp");
	const std::string Header = ReadRepoFile("src/game/client/ui.h");
	const std::string StreamedBody = ExtractSourceFunctionBody(Source, "void CUi::DoLabelStreamed(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated) const");
	ASSERT_FALSE(StreamedBody.empty());

	EXPECT_NE(Header.find("RenderLabelTextContainerAligned"), std::string::npos);
	EXPECT_NE(Source.find("void CUi::RenderLabelTextContainerAligned"), std::string::npos);
	EXPECT_NE(StreamedBody.find("RenderLabelTextContainerAligned(RectEl, pRect, Align)"), std::string::npos);
	EXPECT_EQ(StreamedBody.find("TextRender()->RenderTextContainer(RectEl.m_UITextContainer"), std::string::npos);
	EXPECT_EQ(StreamedBody.find("pRect->x, pRect->y"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuLabelStreamedDoesNotBypassCachedLabelAlignment)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Menus, "void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("Ui()->RenderLabelTextContainerAligned(*pElementRect, pRect, Align);"), std::string::npos);
	EXPECT_EQ(Body.find("TextRender()->RenderTextContainer(pElementRect->m_UITextContainer"), std::string::npos);
	EXPECT_EQ(Body.find("pRect->x, pRect->y"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuTextStyleKeyIncludesScaleButIgnoresAnimatedColorState)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string BuildBody = ExtractSourceFunctionBody(Source, "CMenus::SMenuTextStyleKey CMenus::BuildMenuTextStyleKey(const CUIRect *pRect, float FontSize, int Align, const SLabelProperties &LabelProps) const");
	const std::string CacheKeyBody = ExtractSourceFunctionBody(Source, "std::string MenuTextCacheKey(CMenus::EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const CMenus::SMenuTextStyleKey &StyleKey)");
	const std::string MenuNeedsBuildBody = ExtractSourceFunctionBody(Source, "bool CMenus::MenuTextContainerNeedsBuild(CUIElement &Element, const CUIRect *pRect, const char *pText, int StrLen, const CTextCursor *pReadCursor)");
	const std::string MenuStreamedBody = ExtractSourceFunctionBody(Source, "void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)");
	const std::string UiSource = ReadRepoFile("src/game/client/ui.cpp");
	const std::string StreamedBody = ExtractSourceFunctionBody(UiSource, "void CUi::DoLabelStreamed(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated) const");

	EXPECT_NE(Header.find("int m_HiDpiScaleBucket"), std::string::npos);
	EXPECT_EQ(Header.find("int m_TextColorHash"), std::string::npos);
	EXPECT_EQ(Header.find("int m_OutlineColorHash"), std::string::npos);
	EXPECT_NE(Header.find("SMenuTextStyleKey BuildMenuTextStyleKey"), std::string::npos);
	EXPECT_FALSE(BuildBody.empty());
	EXPECT_FALSE(CacheKeyBody.empty());
	EXPECT_FALSE(MenuNeedsBuildBody.empty());
	EXPECT_FALSE(MenuStreamedBody.empty());
	EXPECT_FALSE(StreamedBody.empty());
	EXPECT_NE(BuildBody.find("Graphics()->ScreenHiDPIScale()"), std::string::npos);
	EXPECT_EQ(BuildBody.find("TextRender()->GetTextColor()"), std::string::npos);
	EXPECT_EQ(BuildBody.find("TextRender()->GetTextOutlineColor()"), std::string::npos);
	EXPECT_EQ(CacheKeyBody.find(":tc"), std::string::npos);
	EXPECT_EQ(CacheKeyBody.find(":oc"), std::string::npos);
	EXPECT_EQ(MenuNeedsBuildBody.find("ColorChanged"), std::string::npos);
	EXPECT_NE(MenuStreamedBody.find("pElementRect->m_TextColor = TextRender()->GetTextColor();"), std::string::npos);
	EXPECT_NE(MenuStreamedBody.find("pElementRect->m_TextOutlineColor = TextRender()->GetTextOutlineColor();"), std::string::npos);
	EXPECT_NE(StreamedBody.find("if(ColorChanged)"), std::string::npos);
	EXPECT_NE(StreamedBody.find("RectEl.m_TextColor = TextRender()->GetTextColor();"), std::string::npos);
	EXPECT_EQ(StreamedBody.find("|| ColorChanged ||"), std::string::npos);
	EXPECT_EQ(Source.find("StyleKey.m_UiScaleBucket = 100"), std::string::npos);
	EXPECT_EQ(Source.find("str_quickhash(\"default-text-style\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsTextUsageSeparatesPoolHitFromRenderReadyHit)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string MenuTextElementBody = ExtractSourceFunctionBody(Source, "CUIElement &CMenus::MenuTextElement(EMenuTextScope Scope, int Page, int Tab, int Subtab, const char *pTextId, const SMenuTextStyleKey &StyleKey)");
	const std::string StreamedBody = ExtractSourceFunctionBody(Source, "void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)");
	ASSERT_FALSE(MenuTextElementBody.empty());
	ASSERT_FALSE(StreamedBody.empty());

	EXPECT_NE(Header.find("m_MenuTextStablePoolHitsThisFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_MenuTextStableRenderReadyHitsThisFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_MenuTextStableBuildQueuedThisFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_MenuTextStableFallbackImmediateThisFrame"), std::string::npos);
	EXPECT_NE(Source.find("pool_hit=%d render_ready_hit=%d"), std::string::npos);
	EXPECT_NE(Source.find("build_queued=%d fallback_immediate=%d"), std::string::npos);
	EXPECT_NE(MenuTextElementBody.find("++m_MenuTextStablePoolHitsThisFrame"), std::string::npos);
	EXPECT_NE(StreamedBody.find("++m_MenuTextStableRenderReadyHitsThisFrame"), std::string::npos);
	EXPECT_NE(StreamedBody.find("++m_MenuTextStableBuildQueuedThisFrame"), std::string::npos);
	EXPECT_NE(StreamedBody.find("++m_MenuTextStableFallbackImmediateThisFrame"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameImmediateTextFallbackIsCountedForSchedulerCoverage)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string TabBody = ExtractSourceFunctionBody(Source, "int CMenus::DoMenuTabV2(CButtonContainer *pButtonContainer, const char *pText, bool Active, const CUIRect *pRect, int Corners, const ColorRGBA *pCustomDefault, const ColorRGBA *pCustomActive, const ColorRGBA *pCustomHover, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float ContentScale)");
	const std::string ButtonBody = ExtractSourceFunctionBody(Source, "int CMenus::DoIngameMenuButton(int Page, const char *pTextId, CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Flags, int Corners, float Rounding)");
	const std::string LabelBody = ExtractSourceFunctionBody(Source, "void CMenus::DoIngameMenuLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	const std::string TitleBody = ExtractSourceFunctionBody(Source, "void CMenus::DoIngameMenuTitleLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	ASSERT_FALSE(TabBody.empty());
	ASSERT_FALSE(ButtonBody.empty());
	ASSERT_FALSE(LabelBody.empty());
	ASSERT_FALSE(TitleBody.empty());

	EXPECT_NE(Header.find("CountMenuTextImmediateFallback()"), std::string::npos);
	EXPECT_NE(Source.find("void CMenus::CountMenuTextImmediateFallback()"), std::string::npos);
	EXPECT_NE(Source.find("scheduler_coverage=%s"), std::string::npos);
	EXPECT_NE(Source.find("FallbackImmediate > 0 ? \"uncovered\" : \"budgeted\""), std::string::npos);
	EXPECT_NE(TabBody.find("if(pTextUiElement != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid())"), std::string::npos);
	EXPECT_NE(ButtonBody.find("if(&TextElement != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid())"), std::string::npos);
	EXPECT_NE(LabelBody.find("if(&Element != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid() && pRect != nullptr)"), std::string::npos);
	EXPECT_NE(TitleBody.find("if(&Element != &m_MenuTextFallbackElement && !HadReadyContainer && !pElementRect->m_UITextContainer.Valid() && pRect != nullptr)"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameFixedChromeUsesBudgetedTextPipeline)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string GameBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderGame(CUIRect MainView)");
	const std::string CallVoteBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderServerControl(CUIRect MainView)");
	const std::string UnfinishedBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderUnfinishedMaps(CUIRect MainView)");
	const std::string GhostBody = ExtractSourceFunctionBody(Ingame, "void CMenus::RenderGhost(CUIRect MainView)");
	ASSERT_FALSE(GameBody.empty());
	ASSERT_FALSE(CallVoteBody.empty());
	ASSERT_FALSE(UnfinishedBody.empty());
	ASSERT_FALSE(GhostBody.empty());

	// Fixed ingame chrome is visible immediately when opening ESC, so it needs
	// the ingame text pool and its current-frame fallback. Leaving these labels
	// on raw DoButton/DoLabel paths recreates containers during the first visible
	// frame and caused server-info/title regressions in earlier iterations.
	EXPECT_NE(Header.find("DoIngameMenuCheckBox("), std::string::npos);
	EXPECT_NE(Source.find("int CMenus::DoIngameMenuCheckBox("), std::string::npos);
	EXPECT_NE(GameBody.find("DoIngameMenuButton(PAGE_GAME, \"ingame-game-disconnect\""), std::string::npos);
	EXPECT_NE(GameBody.find("DoIngameMenuButton(PAGE_GAME, \"ingame-game-edit-hud\""), std::string::npos);
	EXPECT_NE(GameBody.find("DoIngameMenuCheckBox(PAGE_GAME, \"ingame-game-edit-touch-controls\""), std::string::npos);
	EXPECT_EQ(GameBody.find("DoButton_Menu(&s_DisconnectButton, Localize(\"Disconnect\")"), std::string::npos);
	EXPECT_EQ(GameBody.find("DoButton_CheckBox(&s_TouchControlsEditCheckbox, Localize(\"Edit touch controls\")"), std::string::npos);

	EXPECT_NE(CallVoteBody.find("DoIngameMenuButton(PAGE_CALLVOTE, \"ingame-call-vote-call\""), std::string::npos);
	EXPECT_NE(CallVoteBody.find("DoIngameMenuLabel(PAGE_CALLVOTE, \"ingame-call-vote-reason-label\""), std::string::npos);
	EXPECT_NE(CallVoteBody.find("DoIngameMenuButton(PAGE_CALLVOTE, \"ingame-call-vote-force\""), std::string::npos);
	EXPECT_EQ(CallVoteBody.find("DoButton_Menu(&s_CallVoteButton, Localize(\"Call vote\")"), std::string::npos);
	EXPECT_EQ(CallVoteBody.find("Ui()->DoLabel(&Reason, pLabel, 14.0f"), std::string::npos);

	EXPECT_NE(UnfinishedBody.find("DoIngameMenuTitleLabel(PAGE_CALLVOTE, \"ingame-unfinished-maps-title\""), std::string::npos);
	EXPECT_NE(UnfinishedBody.find("DoIngameMenuCheckBox(PAGE_CALLVOTE, \"ingame-unfinished-auto-start-vote\""), std::string::npos);
	EXPECT_NE(UnfinishedBody.find("DoIngameMenuButton(PAGE_CALLVOTE, \"ingame-unfinished-random-pick\""), std::string::npos);
	EXPECT_EQ(UnfinishedBody.find("DoButton_CheckBox(&s_UnfinishedMapAutoVote, Localize(\"Auto start vote\")"), std::string::npos);

	EXPECT_NE(GhostBody.find("DoIngameMenuButton(PAGE_GHOST, \"ingame-ghost-directory\""), std::string::npos);
	EXPECT_NE(GhostBody.find("DoIngameMenuButton(PAGE_GHOST, ActivateAll ? \"ingame-ghost-activate-all\" : \"ingame-ghost-deactivate-all\""), std::string::npos);
	EXPECT_NE(GhostBody.find("DoIngameMenuButton(PAGE_GHOST, \"ingame-ghost-delete\""), std::string::npos);
	EXPECT_EQ(GhostBody.find("DoButton_Menu(&s_DirectoryButton, Localize(\"Ghosts directory\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsToolbarAndPlaceholdersUseBudgetedTextPipeline)
{
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Assets, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Assets tab switches should not spend the first visible frame creating
	// toolbar or per-card placeholder text containers. Toolbar text uses the
	// settings text pool, while card placeholders stay visual-only until preview
	// content is ready.
	EXPECT_NE(Body.find("DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_AssetsEditorButton"), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_ShowWorkshopAssetsId"), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_WorkshopSyncId"), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsButton_Menu(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, &s_AssetsDirId"), std::string::npos);
	EXPECT_EQ(Body.find("DoButton_Menu(&s_AssetsEditorButton, Localize(\"Assets editor\")"), std::string::npos);
	EXPECT_EQ(Body.find("DoButton_Menu(&s_ShowWorkshopAssetsId, Localize(\"Show Workshop Assets\")"), std::string::npos);
	EXPECT_EQ(Body.find("DoButton_Menu(&s_WorkshopSyncId, Localize(\"Sync Workshop Assets\")"), std::string::npos);
	EXPECT_EQ(Body.find("DoButton_Menu(&s_AssetsDirId, Localize(\"Assets directory\")"), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsMenuLabel(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, \"assets-loading-list\""), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsMenuLabel(SETTINGS_ASSETS, s_CurCustomTab, s_CurCustomTab, \"assets-workshop-no-assets\""), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel(&LoadingRect, Localize(\"Loading"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsWorkshopVisibilityDefaultsToHidden)
{
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");

	EXPECT_NE(MenusHeader.find("bool m_ShowWorkshopAssets = false;"), std::string::npos);
	EXPECT_EQ(MenusHeader.find("bool m_ShowWorkshopAssets = true;"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameCriticalTextFallbacksAreLimited)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string StreamedBody = ExtractSourceFunctionBody(Source, "void CMenus::DoMenuLabelStreamed(EMenuTextScope Scope, CUIElement &Element, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render)");
	const std::string TabBody = ExtractSourceFunctionBody(Source, "int CMenus::DoMenuTabV2(CButtonContainer *pButtonContainer, const char *pText, bool Active, const CUIRect *pRect, int Corners, const ColorRGBA *pCustomDefault, const ColorRGBA *pCustomActive, const ColorRGBA *pCustomHover, const CCommunityIcon *pCommunityIcon, CUIElement *pTextUiElement, float ContentScale)");
	const std::string TitleBody = ExtractSourceFunctionBody(Source, "void CMenus::DoIngameMenuTitleLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	const std::string LabelBody = ExtractSourceFunctionBody(Source, "void CMenus::DoIngameMenuLabel(int Page, const char *pTextId, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	ASSERT_FALSE(StreamedBody.empty());
	ASSERT_FALSE(TabBody.empty());
	ASSERT_FALSE(TitleBody.empty());
	ASSERT_FALSE(LabelBody.empty());

	// Keep immediate fallback scoped to ingame stable UI chrome. Expanding it
	// inside DoMenuLabelStreamed would make ordinary settings text misses
	// synchronous again and undo the text-budget work.
	EXPECT_EQ(StreamedBody.find("!HadReadyContainer"), std::string::npos);
	EXPECT_NE(TabBody.find("!HadReadyContainer"), std::string::npos);
	EXPECT_NE(TitleBody.find("!HadReadyContainer"), std::string::npos);
	EXPECT_NE(LabelBody.find("!HadReadyContainer"), std::string::npos);
}

TEST(QmMonitoringHelpers, StableTextUsageTelemetryIsClassifiedAsStaticStable)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void LogSettingsTextPoolUsage(IClient *pClient, CMenus::EMenuTextScope Scope, const char *pScopeName, int Page, int Tab, int Subtab, const char *pOperation, uint64_t Frame, int Candidates, int Hits, int Reused, int Misses, int Stales, int TextNew, int TextReused, int Planned, int Unplanned, int PoolHits, int RenderReadyHits, int BuildQueued, int FallbackImmediate)");
	ASSERT_FALSE(Body.empty());

	// Static stable text is the only class allowed in the stable-text
	// denominator. Dynamic snapshot values, paragraphs, and card metadata have
	// separate hit-rate counters so a bad server name or resource title cache
	// cannot make the stable descriptor plan look broken.
	EXPECT_NE(Body.find("text_class=static_stable"), std::string::npos);
	EXPECT_EQ(Body.find("dynamic_snapshot"), std::string::npos);
	EXPECT_EQ(Body.find("paragraph"), std::string::npos);
	EXPECT_EQ(Body.find("card_metadata"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameServerInfoDoesNotTouchUninitializedUiElementRects)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderIngameServerInfoValueCached(const char *pTextId, unsigned &TextHash, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Header.find("CUIElement m_IngameServerNameTextElement"), std::string::npos);
	EXPECT_EQ(Body.find("Element.Rect(0)"), std::string::npos);
	EXPECT_EQ(Body.find("DeleteTextContainer"), std::string::npos);
	EXPECT_NE(Body.find("(void)TextHash;"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameTextRuntimeDrainsOutsideRenderFunctions)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string RenderBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfo(CUIRect MainView)");
	const std::string MotdBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	const std::string SnapshotDrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameUiSnapshotTextRuntime()");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameUiTextRuntime(bool AllowCurrentFrame)");
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(MotdBody.empty());
	ASSERT_FALSE(SnapshotDrainBody.empty());
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(Header.find("void DrainIngameUiSnapshotTextRuntime();"), std::string::npos);
	EXPECT_NE(Header.find("void DrainIngameUiTextRuntime(bool AllowCurrentFrame = false);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("DrainSnapshotTextContainers()"), std::string::npos);
	EXPECT_EQ(MotdBody.find("DrainIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(SnapshotDrainBody.find("DrainSnapshotTextContainers()"), std::string::npos);
	EXPECT_NE(DrainBody.find("DrainIngameUiSnapshotTextRuntime()"), std::string::npos);
	EXPECT_NE(DrainBody.find("DrainIngameMotdParagraphCache("), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameEscOpenDefersServerInfoRuntimeDrain)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	ASSERT_FALSE(OnRenderBody.empty());

	EXPECT_NE(OnRenderBody.find("StartSettingsPerfFixedWindow(\"ingame_esc_open\""), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("PrebuildIngameEscTextPoolBeforeOpen("), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("PrebuildSettingsMenuTextPool("), std::string::npos);
	EXPECT_NE(Header.find("uint64_t m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(Header.find("bool m_IngameServerInfoBackgroundPrepareRequested"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("m_IngameEscOpenFrame = Client()->PerfFrame();"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("m_IngameServerInfoBackgroundPrepareRequested = true;"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("Client()->PerfFrame() > m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("PrepareIngameServerInfoTextRuntime();"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("if(m_GamePage == PAGE_SERVER_INFO)"), std::string::npos);
	const size_t VisibleServerInfoBranch = OnRenderBody.find("if(m_GamePage == PAGE_SERVER_INFO)");
	const size_t BackgroundPrepareBranch = OnRenderBody.find("else if(m_IngameServerInfoBackgroundPrepareRequested", VisibleServerInfoBranch);
	ASSERT_NE(VisibleServerInfoBranch, std::string::npos);
	ASSERT_NE(BackgroundPrepareBranch, std::string::npos);
	const std::string VisibleBranch = OnRenderBody.substr(VisibleServerInfoBranch, BackgroundPrepareBranch - VisibleServerInfoBranch);
	EXPECT_NE(VisibleBranch.find("DrainIngameUiSnapshotTextRuntime();"), std::string::npos);
	EXPECT_EQ(VisibleBranch.find("DrainIngameUiTextRuntime(false);"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("DrainIngameUiTextRuntime(false);"), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("else if(GameClient()->m_Motd.ServerMotd()[0])"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("DrainIngameUiSnapshotTextRuntime();"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerInfoLayoutAvoidsTextWidthInRenderPath)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfo(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("TextRender()->TextWidth"), std::string::npos);
	EXPECT_NE(Body.find("const float ServerInfoLabelWidth"), std::string::npos);
	EXPECT_NE(Body.find("pRow->VSplitLeft(ServerInfoLabelWidth"), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdRenderUsesOnlyCurrentMatchingParagraphCache)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Header.find("m_LastStableHeight"), std::string::npos);
	EXPECT_NE(Source.find("bool CMenus::IngameMotdParagraphCacheMatches"), std::string::npos);
	EXPECT_NE(Body.find("const bool CacheReady = IngameMotdParagraphCacheMatches(Motd, MotdFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("if(m_IngameMotdParagraphCache.m_Valid && m_MotdTextContainerIndex.Valid())"), std::string::npos);
	EXPECT_NE(Body.find("if(CacheReady && m_MotdTextContainerIndex.Valid())"), std::string::npos);
}

TEST(QmMonitoringHelpers, DynamicSnapshotTextUsesBudgetedDrain)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string SnapshotDrainBody = ExtractSourceFunctionBody(Ingame, "void CMenus::DrainSnapshotTextContainers()");

	EXPECT_NE(Header.find("struct SMenuSnapshotTextKey"), std::string::npos);
	EXPECT_NE(Header.find("RequestSnapshotTextContainer"), std::string::npos);
	EXPECT_NE(Header.find("DrainSnapshotTextContainers"), std::string::npos);
	EXPECT_NE(Header.find("m_SnapshotTextPending"), std::string::npos);
	EXPECT_NE(Menus.find("m_TextContainerTokens"), std::string::npos);
	EXPECT_NE(Menus.find("m_SnapshotTextCache.clear()"), std::string::npos);
	EXPECT_NE(Menus.find("m_SnapshotTextPending.clear()"), std::string::npos);
	EXPECT_NE(Menus.find("PrebuildSettingsTextPlanItem"), std::string::npos);
	EXPECT_NE(Menus.find("DrainMenuTextContainerBuild(Element, &Item.m_Rect"), std::string::npos);
	EXPECT_NE(Ingame.find("while(m_IngameTextFrameBudget.m_TextContainerTokens > 0 && !m_SnapshotTextPending.empty())"), std::string::npos);
	EXPECT_NE(Ingame.find("--m_IngameTextFrameBudget.m_TextContainerTokens"), std::string::npos);
	EXPECT_EQ(Ingame.find("m_CurrentSettingsUiFrameBudget.m_TextContainerTokens > 0 && !m_SnapshotTextPending.empty()"), std::string::npos);
	EXPECT_NE(Ingame.find("RenderIngameServerInfoValueCached"), std::string::npos);
	EXPECT_NE(Ingame.find("RequestSnapshotTextContainer("), std::string::npos);
	ASSERT_FALSE(SnapshotDrainBody.empty());
	// Snapshot cache miss/container creation is reported by the drain as a
	// frame aggregate. It must not be inferred from paragraph misses, otherwise
	// MOTD misses pollute dynamic short-text hit rate.
	EXPECT_NE(SnapshotDrainBody.find("snapshot_cache_miss=%d"), std::string::npos);
	EXPECT_NE(SnapshotDrainBody.find("text_container_new=%d"), std::string::npos);
	EXPECT_NE(SnapshotDrainBody.find("text_container_uploads=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdParagraphUsesBudgetedDrain)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(DrainBody.find("m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(DrainBody.find("m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(DrainBody.find("--m_IngameTextFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_EQ(DrainBody.find("m_CurrentSettingsUiFrameBudget.m_ParagraphLayoutTokens"), std::string::npos);
	EXPECT_NE(DrainBody.find("TextRender()->CreateOrAppendTextContainer(m_IngameMotdParagraphCache.m_BuildTextContainerIndex"), std::string::npos);
	EXPECT_EQ(DrainBody.find("TextRender()->RecreateTextContainer(m_MotdTextContainerIndex"), std::string::npos);
	EXPECT_NE(DrainBody.find("paragraph_cache_hit=%d"), std::string::npos);
	EXPECT_NE(DrainBody.find("paragraph_cache_miss=%d"), std::string::npos);
	EXPECT_EQ(DrainBody.find("static_stable_hit="), std::string::npos);
	EXPECT_EQ(DrainBody.find("snapshot_cache_miss="), std::string::npos);
}

TEST(QmMonitoringHelpers, VisibleIngameRenderDrainsParagraphOnlyAfterPendingFrame)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Ingame = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	const std::string PrepareBody = ExtractSourceFunctionBody(Ingame, "void CMenus::PrepareIngameServerInfoTextRuntime(const CUIRect *pMainView)");
	ASSERT_FALSE(OnRenderBody.empty());
	ASSERT_FALSE(PrepareBody.empty());

	// Long MOTD paragraph layout can be enqueued by prepare, but the visible
	// frame must not use the same-frame force path or drain paragraph work.
	EXPECT_NE(OnRenderBody.find("DrainIngameUiTextRuntime(false);"), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("DrainIngameUiTextRuntime(true);"), std::string::npos);
	EXPECT_EQ(PrepareBody.find("DrainIngameUiTextRuntime(true);"), std::string::npos);
	EXPECT_EQ(PrepareBody.find("DrainIngameUiTextRuntime(false);"), std::string::npos);
	EXPECT_NE(PrepareBody.find("RequestIngameMotdParagraphCache("), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdPrepareHydratesParagraphWithoutSameFrameStarvation)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string PrepareBody = ExtractSourceFunctionBody(Source, "void CMenus::PrepareIngameServerInfoTextRuntime(const CUIRect *pMainView)");
	const std::string DrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame)");
	const std::string RuntimeDrainBody = ExtractSourceFunctionBody(Source, "void CMenus::DrainIngameUiTextRuntime(bool AllowCurrentFrame)");
	ASSERT_FALSE(PrepareBody.empty());
	ASSERT_FALSE(DrainBody.empty());
	ASSERT_FALSE(RuntimeDrainBody.empty());

	// Server-info preparation may enqueue work, but it must respect the normal
	// budgeted drain path. Same-frame force would move long paragraph layout
	// back into the page-open frame.
	EXPECT_NE(Header.find("void DrainIngameMotdParagraphCache(CUIRect Motd, float FontSize, bool AllowCurrentFrame = false);"), std::string::npos);
	EXPECT_NE(Header.find("void DrainIngameUiTextRuntime(bool AllowCurrentFrame = false);"), std::string::npos);
	EXPECT_EQ(PrepareBody.find("DrainIngameUiTextRuntime(true);"), std::string::npos);
	EXPECT_EQ(PrepareBody.find("DrainIngameUiTextRuntime(false);"), std::string::npos);
	EXPECT_NE(PrepareBody.find("RequestIngameMotdParagraphCache("), std::string::npos);
	EXPECT_NE(DrainBody.find("!AllowCurrentFrame && Frame <= m_IngameMotdParagraphCache.m_PendingFrame"), std::string::npos);
	EXPECT_NE(RuntimeDrainBody.find("DrainIngameMotdParagraphCache(m_IngameMotdParagraphCache.m_PendingRect, m_IngameMotdParagraphCache.m_PendingFontSize, AllowCurrentFrame);"), std::string::npos);
}

TEST(QmMonitoringHelpers, MotdParagraphPendingDrainsAfterVisibleRequest)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	ASSERT_FALSE(OnRenderBody.empty());

	// If MOTD changes while the server-info page is already open, render can
	// only enqueue a paragraph request. The frame-end drain must continue the
	// budgeted paragraph pipeline on later frames, otherwise the announcement
	// area stays blank until the page is reopened.
	EXPECT_NE(OnRenderBody.find("if(IsActive() && Client()->State() == IClient::STATE_ONLINE)"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("if(m_GamePage == PAGE_SERVER_INFO)"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("DrainIngameUiTextRuntime(false);"), std::string::npos);
	EXPECT_EQ(OnRenderBody.find("else if(GameClient()->m_Motd.ServerMotd()[0])"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("Client()->PerfFrame() > m_IngameEscOpenFrame"), std::string::npos);
	EXPECT_NE(OnRenderBody.find("PrepareIngameServerInfoTextRuntime();"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameTextPlanCollectionDoesNotTouchMotdRuntimeCache)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("if(m_MenuTextPlanCollecting)"), std::string::npos);
	EXPECT_LT(Body.find("if(m_MenuTextPlanCollecting)"), Body.find("GameClient()->m_Motd.ServerMotd()"));
	EXPECT_LT(Body.find("if(m_MenuTextPlanCollecting)"), Body.find("CScrollRegion"));
	EXPECT_LT(Body.find("if(m_MenuTextPlanCollecting)"), Body.find("RequestIngameMotdParagraphCache(Motd"));
	EXPECT_EQ(Body.find("DrainIngameMotdParagraphCache(Motd"), std::string::npos);
	EXPECT_NE(Body.find("return;"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameDynamicTextPlanCollectionDoesNotTouchRuntimeElements)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderIngameServerInfoValueCached(const char *pTextId, unsigned &TextHash, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps)");
	ASSERT_FALSE(Body.empty());

	const size_t CollectingCheck = Body.find("if(m_MenuTextPlanCollecting)");
	const size_t TextHashWrite = Body.find("TextHash = NewHash;");
	const size_t SnapshotRequest = Body.find("RequestSnapshotTextContainer(");
	ASSERT_NE(CollectingCheck, std::string::npos);
	ASSERT_NE(TextHashWrite, std::string::npos);
	ASSERT_NE(SnapshotRequest, std::string::npos);
	EXPECT_LT(CollectingCheck, TextHashWrite);
	EXPECT_LT(CollectingCheck, SnapshotRequest);
	EXPECT_NE(Body.find("CollectMenuTextPlanItem(MENU_TEXT_SCOPE_INGAME"), std::string::npos);
	EXPECT_EQ(Body.find("Element.Rect(0)"), std::string::npos);
}

TEST(QmMonitoringHelpers, BackgroundTextureRenderSkipsBeforeInterfacesAreReady)
{
	const std::string ComponentHeader = ReadRepoFile("src/game/client/component.h");
	const std::string BackgroundSource = ReadRepoFile("src/game/client/components/background.cpp");
	const std::string Body = ExtractSourceFunctionBody(BackgroundSource, "bool CBackground::RenderBackgroundTexture()");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(ComponentHeader.find("bool InterfacesInitialized() const { return m_pClient != nullptr; }"), std::string::npos);
	EXPECT_NE(Body.find("if(!InterfacesInitialized())"), std::string::npos);
	EXPECT_LT(Body.find("if(!InterfacesInitialized())"), Body.find("Graphics()->GetScreen("));
	EXPECT_NE(Body.find("return false;"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuBackgroundRenderSkipsBeforeInterfacesAreReady)
{
	const std::string MenuBackgroundSource = ReadRepoFile("src/game/client/components/menu_background.cpp");
	const std::string Body = ExtractSourceFunctionBody(MenuBackgroundSource, "bool CMenuBackground::Render()");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("if(!InterfacesInitialized())"), std::string::npos);
	EXPECT_LT(Body.find("if(!InterfacesInitialized())"), Body.find("RenderBackgroundTexture()"));
	EXPECT_LT(Body.find("if(!InterfacesInitialized())"), Body.find("Client()->RenderFrameTime()"));
	EXPECT_NE(Body.find("return false;"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuBackgroundKeepsPositionAcrossIdleFrames)
{
	const std::string MenuBackgroundSource = ReadRepoFile("src/game/client/components/menu_background.cpp");
	const std::string RenderBody = ExtractSourceFunctionBody(MenuBackgroundSource, "bool CMenuBackground::Render()");
	const std::string ChangePositionBody = ExtractSourceFunctionBody(MenuBackgroundSource, "void CMenuBackground::ChangePosition(int PositionNumber)");
	const std::string LoadBody = ExtractSourceFunctionBody(MenuBackgroundSource, "void CMenuBackground::LoadMenuBackground(bool HasDayHint, bool HasNightHint)");
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(ChangePositionBody.empty());
	ASSERT_FALSE(LoadBody.empty());

	EXPECT_EQ(RenderBody.find("m_CurrentPosition = -1;"), std::string::npos);
	EXPECT_NE(ChangePositionBody.find("if(NewPosition == m_CurrentPosition)"), std::string::npos);
	EXPECT_NE(ChangePositionBody.find("return;"), std::string::npos);
	EXPECT_NE(LoadBody.find("InvalidateCurrentPosition();"), std::string::npos);
}

TEST(QmMonitoringHelpers, StartMenuHasConcretePerfStagesAndNoPerFrameV2LayoutAllocation)
{
	const std::string StartMenuSource = ReadRepoFile("src/game/client/components/menus_start.cpp");
	const std::string Body = ExtractSourceFunctionBody(StartMenuSource, "void CMenusStart::RenderStartMenuImpl(CUIRect MainView, bool UseV2Layout)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(StartMenuSource.find("LogStartMenuPerfStage"), std::string::npos);
	EXPECT_NE(Body.find("const bool TrackPerf = QmPerfEnabled();"), std::string::npos);
	EXPECT_NE(Body.find("start_menu_logo"), std::string::npos);
	EXPECT_NE(Body.find("start_menu_external_layout"), std::string::npos);
	EXPECT_NE(Body.find("start_menu_external_buttons"), std::string::npos);
	EXPECT_NE(Body.find("start_menu_main_buttons"), std::string::npos);
	EXPECT_NE(Body.find("start_menu_version_console"), std::string::npos);
	EXPECT_NE(Body.find("start_menu_total"), std::string::npos);
	EXPECT_NE(StartMenuSource.find("static std::vector<SUiLayoutChild> s_vExternalButtonChildren(6);"), std::string::npos);
	EXPECT_NE(StartMenuSource.find("static std::vector<SUiLayoutChild> s_vMainButtonChildren(5);"), std::string::npos);
	EXPECT_EQ(StartMenuSource.find("std::vector<SUiLayoutChild> vChildren("), std::string::npos);
	EXPECT_EQ(StartMenuSource.find("CPerfTimer"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuIdleRenderThrottleOnlySkipsSettingsDuringPerfSampling)
{
	const std::string MenusSource = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(MenusSource, "int CMenus::IdleRenderFrameRate() const");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(MenusHeader.find("std::chrono::nanoseconds m_LastMenuInteractionTime"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr int MENU_IDLE_REFRESH_RATE = 60;"), std::string::npos);
	EXPECT_NE(MenusSource.find("constexpr auto MENU_IDLE_INTERACTION_GRACE_TIME = 450ms;"), std::string::npos);
	EXPECT_NE(Body.find("!InterfacesInitialized()"), std::string::npos);
	EXPECT_NE(Body.find("Ui()->ActiveItem() != nullptr"), std::string::npos);
	EXPECT_NE(Body.find("CLineInput::GetActiveInput() != nullptr"), std::string::npos);
	EXPECT_NE(Body.find("GameClient()->m_KeyBinder.IsActive()"), std::string::npos);
	const size_t SettingsGuard = Body.find("if(IsSettingsPageActive() && (g_Config.m_QmPerfDebug != 0 || m_SettingsPerfWindowTracker.HasActiveWindow()))");
	const size_t SettingsReturn = Body.find("return 0;", SettingsGuard);
	ASSERT_NE(SettingsGuard, std::string::npos);
	ASSERT_NE(SettingsReturn, std::string::npos);
	EXPECT_NE(Body.find("GameClient()->m_MenuBackground.IsLoading()"), std::string::npos);
	EXPECT_NE(Body.find("UiRuntimeStats.m_ActiveAnimCount > 0"), std::string::npos);
	EXPECT_NE(Body.find("time_get_nanoseconds() - m_LastMenuInteractionTime < MENU_IDLE_INTERACTION_GRACE_TIME"), std::string::npos);
	EXPECT_NE(Body.find("return maximum(MENU_IDLE_REFRESH_RATE, g_Config.m_GfxScreenRefreshRate);"), std::string::npos);
	EXPECT_LT(SettingsReturn, Body.find("return maximum(MENU_IDLE_REFRESH_RATE, g_Config.m_GfxScreenRefreshRate);"));
}

TEST(QmMonitoringHelpers, ClientRenderLoopUsesGameClientIdleThrottleWithOneFrameRatePath)
{
	const std::string ClientSource = ReadRepoFile("src/engine/client/client.cpp");
	const std::string ClientInterface = ReadRepoFile("src/engine/client.h");
	const std::string GameClientHeader = ReadRepoFile("src/game/client/gameclient.h");
	const std::string GameClientSource = ReadRepoFile("src/game/client/gameclient.cpp");
	const std::string Forwarder = ExtractSourceFunctionBody(GameClientSource, "int CGameClient::RenderThrottleRefreshRate() const");
	ASSERT_FALSE(Forwarder.empty());

	EXPECT_NE(ClientInterface.find("virtual int RenderThrottleRefreshRate() const = 0;"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("int RenderThrottleRefreshRate() const override;"), std::string::npos);
	EXPECT_NE(Forwarder.find("return m_Menus.IdleRenderFrameRate();"), std::string::npos);
	EXPECT_NE(ClientSource.find("RequestedRenderThrottleRate = GameClient()->RenderThrottleRefreshRate();"), std::string::npos);
	EXPECT_NE(ClientSource.find("GfxRefreshRate = std::clamp(RequestedRenderThrottleRate, 10, 10000);"), std::string::npos);
	EXPECT_NE(ClientSource.find("IdleRenderThrottleRate = GfxRefreshRate;"), std::string::npos);
	EXPECT_NE(ClientSource.find("int LastIdleRenderThrottleRate = -1;"), std::string::npos);
	EXPECT_NE(ClientSource.find("event=idle_render_throttle rate=%d requested=%d configured=%d vsync=%d"), std::string::npos);
	EXPECT_NE(ClientSource.find("LastIdleRenderThrottleRate = IdleRenderThrottleRate;"), std::string::npos);
	EXPECT_NE(ClientSource.find("fs_makedir_rec_for(aPerfLogCompletePath);"), std::string::npos);
	EXPECT_NE(ClientSource.find("PerfLogfile = io_open(aPerfLogCompletePath, IOFLAG_WRITE);"), std::string::npos);
	EXPECT_NE(ClientSource.find("char aWorkingDir[IO_MAX_PATH_LENGTH];"), std::string::npos);
	EXPECT_NE(ClientSource.find("if(fs_getcwd(aWorkingDir, sizeof(aWorkingDir)))"), std::string::npos);
	EXPECT_NE(ClientSource.find("str_format(aPerfLogCompletePath, sizeof(aPerfLogCompletePath), \"%s/%s\", aWorkingDir, aPerfLogPath);"), std::string::npos);
	EXPECT_NE(ClientSource.find("const int64_t RenderFrameTicks = GfxRefreshRate > 0 ? time_freq() / (int64_t)GfxRefreshRate : 0;"), std::string::npos);
	EXPECT_NE(ClientSource.find("(!GfxRefreshRate || RenderFrameTicks <= Now - LastRenderTime)"), std::string::npos);
	EXPECT_NE(ClientSource.find("int64_t AdditionalTime = GfxRefreshRate ? ((Now - LastRenderTime) - RenderFrameTicks) : 0;"), std::string::npos);
	EXPECT_NE(ClientSource.find("state=%d render_rate=%d throttle=%d"), std::string::npos);
	EXPECT_NE(ClientSource.find("const auto WaitWithNetwork = [&](std::chrono::nanoseconds WaitTime)"), std::string::npos);
	EXPECT_NE(ClientSource.find("else if(IdleRenderThrottleRate > 0)"), std::string::npos);
	EXPECT_NE(ClientSource.find("SleepTimeInNanoSeconds = (std::chrono::nanoseconds(1s) / (int64_t)IdleRenderThrottleRate) - (Now - LastTime);"), std::string::npos);
	EXPECT_EQ(ClientSource.find("time_freq() / (int64_t)g_Config.m_GfxRefreshRate"), std::string::npos);
}

TEST(QmMonitoringHelpers, ConsoleQueuedResultCopyPreservesExternalArguments)
{
	const std::string ConsoleSource = ReadRepoFile("src/engine/shared/console.cpp");
	const std::string Body = ExtractSourceFunctionBody(ConsoleSource, "CConsole::CResult::CResult(const CResult &Other)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(ConsoleSource.find("const char *CConsole::CResult::CopyArgumentPointer"), std::string::npos);
	EXPECT_NE(Body.find("CopyArgumentPointer(Other.m_pArgsStart, Other)"), std::string::npos);
	EXPECT_NE(Body.find("CopyArgumentPointer(Other.m_pCommand, Other)"), std::string::npos);
	EXPECT_NE(Body.find("CopyArgumentPointer(Other.m_apArgs[i], Other)"), std::string::npos);
	EXPECT_EQ(Body.find("m_aStringStorage + (Other.m_apArgs[i] - Other.m_aStringStorage)"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsLoadingPrewarmApiIsPublic)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const size_t StartLoading = Header.find("void StartLoading(int Total);");
	const size_t PrewarmSettingsPages = Header.find("void PrewarmSettingsPages();");
	const size_t IsInit = Header.find("bool IsInit() const");
	ASSERT_NE(StartLoading, std::string::npos);
	ASSERT_NE(PrewarmSettingsPages, std::string::npos);
	ASSERT_NE(IsInit, std::string::npos);

	EXPECT_GT(PrewarmSettingsPages, StartLoading);
	EXPECT_LT(PrewarmSettingsPages, IsInit);
}

TEST(QmMonitoringHelpers, TextRuntimeTelemetryReportsGlyphContainerAndParagraphCosts)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");

	EXPECT_NE(Text.find("glyph_rasterize_ms"), std::string::npos);
	EXPECT_NE(Text.find("text_container_create_ms"), std::string::npos);
	EXPECT_NE(Text.find("text_container_upload_ms"), std::string::npos);
	EXPECT_NE(Stats.find("glyphRasterizeMs"), std::string::npos);
	EXPECT_NE(Stats.find("paragraphLayoutMs"), std::string::npos);
	EXPECT_NE(Stats.find("paragraphBudgetBlocked"), std::string::npos);
	EXPECT_NE(Stats.find("paragraphCacheHit"), std::string::npos);
	EXPECT_NE(Stats.find("paragraphCacheMiss"), std::string::npos);
}

TEST(QmMonitoringHelpers, TextRuntimeCountersDoNotAccumulateWhilePerfDisabled)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Body = ExtractSourceFunctionBody(Text, "void FlushQmTextRuntimeBudgetLog() override");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Text.find("void ResetQmTextRuntimeBudgetCounters(bool ConsumeGlyphStats)"), std::string::npos);
	EXPECT_NE(Body.find("m_pGlyphMap->ConsumeQmPerfGlyphStats"), std::string::npos);
	EXPECT_NE(Body.find("if(!QmPerfEnabled()"), std::string::npos);
	EXPECT_LT(Body.find("m_pGlyphMap->ConsumeQmPerfGlyphStats"), Body.find("if(!QmPerfEnabled()"));
	EXPECT_GT(Body.find("ResetQmTextRuntimeBudgetCounters(false);", Body.find("if(!QmPerfEnabled()")), Body.find("if(!QmPerfEnabled()"));
}

TEST(QmMonitoringHelpers, TextRuntimeSnapshotUpdatesBeforePerfLoggingGate)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Body = ExtractSourceFunctionBody(Text, "void FlushQmTextRuntimeBudgetLog() override");
	ASSERT_FALSE(Body.empty());

	// The adaptive settings scheduler consumes this snapshot during normal
	// gameplay, so it must not depend on the perf log/debug switch being on.
	EXPECT_NE(Body.find("UpdateQmTextRuntimeBudgetSnapshot(GlyphNew, GlyphUploads, GlyphRasterizeMs, GlyphUploadMs);"), std::string::npos);
	EXPECT_NE(Body.find("if(!QmPerfEnabled()"), std::string::npos);
	EXPECT_LT(Body.find("UpdateQmTextRuntimeBudgetSnapshot(GlyphNew, GlyphUploads, GlyphRasterizeMs, GlyphUploadMs);"), Body.find("if(!QmPerfEnabled()"));
}

TEST(QmMonitoringHelpers, TextRuntimeTelemetrySkipsLowCostSingleContainerNoise)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Body = ExtractSourceFunctionBody(Text, "void FlushQmTextRuntimeBudgetLog() override");
	ASSERT_FALSE(Body.empty());

	// A long perf run can create millions of cheap one-off text containers.
	// Keep high-cost/glyph/upload rows for attribution, but do not log every
	// sub-threshold single-container create as its own perf/text line.
	EXPECT_NE(Text.find("bool ShouldLogTextRuntimeBudget("), std::string::npos);
	EXPECT_NE(Text.find("TextContainerWork >= 8"), std::string::npos);
	EXPECT_NE(Body.find("if(!ShouldLogTextRuntimeBudget("), std::string::npos);
	EXPECT_NE(Body.find("ResetQmTextRuntimeBudgetCounters(false);"), std::string::npos);
}

TEST(QmMonitoringHelpers, TextRuntimeTelemetryFlushesOncePerUiFrame)
{
	const std::string Header = ReadRepoFile("src/engine/textrender.h");
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string CreateBody = ExtractSourceFunctionBody(Text, "bool CreateTextContainer(STextContainerIndex &TextContainerIndex, CTextCursor *pCursor, const char *pText, int Length = -1) override");
	const std::string UploadBody = ExtractSourceFunctionBody(Text, "void UploadTextContainer(STextContainerIndex TextContainerIndex) override");
	const std::string OnRenderBody = ExtractSourceFunctionBody(Menus, "void CMenus::OnRender()");
	ASSERT_FALSE(CreateBody.empty());
	ASSERT_FALSE(UploadBody.empty());
	ASSERT_FALSE(OnRenderBody.empty());

	// Regression guard for 2GB perf logs: text render hot paths may only
	// accumulate counters. The UI frame owns the single aggregated flush.
	EXPECT_NE(Header.find("FlushQmTextRuntimeBudgetLog"), std::string::npos);
	EXPECT_EQ(CreateBody.find("LogQmTextRuntimeBudget("), std::string::npos);
	EXPECT_EQ(UploadBody.find("LogQmTextRuntimeBudget("), std::string::npos);
	EXPECT_NE(OnRenderBody.find("TextRender()->FlushQmTextRuntimeBudgetLog()"), std::string::npos);
}

TEST(QmMonitoringHelpers, TextRenderExposesRuntimeBudgetSnapshotForScheduler)
{
	const std::string Header = ReadRepoFile("src/engine/textrender.h");
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");

	EXPECT_NE(Header.find("struct SQmTextRuntimeBudgetSnapshot"), std::string::npos);
	EXPECT_NE(Header.find("virtual SQmTextRuntimeBudgetSnapshot QmTextRuntimeBudgetSnapshot() const"), std::string::npos);
	EXPECT_NE(Text.find("SQmTextRuntimeBudgetSnapshot m_QmLastTextRuntimeBudgetSnapshot"), std::string::npos);
	EXPECT_NE(Text.find("m_QmLastTextRuntimeBudgetSnapshot.m_TextContainerCreateMs"), std::string::npos);
}

TEST(QmMonitoringHelpers, HudSettingsTextHydratesUnderBudget)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string HudBranch = ExtractSourceBlock(Settings, "if(m_AppearanceSettingsTab == APPEARANCE_TAB_HUD)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_CHAT)");
	ASSERT_FALSE(HudBranch.empty());

	EXPECT_NE(Settings.find("APPEARANCE_TAB_HUD"), std::string::npos);
	EXPECT_NE(Settings.find("DoAppearanceNumericField(APPEARANCE_TAB_HUD"), std::string::npos);
	EXPECT_NE(HudBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD"), std::string::npos);
	EXPECT_EQ(HudBranch.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_NE(HudBranch.find("DoSettingsLabelStreamed"), std::string::npos);
	EXPECT_EQ(HudBranch.find("Ui()->DoLabel_AutoLineSize"), std::string::npos);
	EXPECT_NE(Menus.find("m_pSettingsTextPrebuildBudget"), std::string::npos);
	EXPECT_NE(Menus.find("BeginSettingsUiFrameScheduler("), std::string::npos);
	EXPECT_NE(Menus.find("DrainMenuTextContainerBuildRequests()"), std::string::npos);
	EXPECT_NE(Menus.find("AdaptiveBudget.m_TextPrebuildTokens"), std::string::npos);
}

TEST(QmMonitoringHelpers, AppearanceHudChatNamePlateCheckboxesUseBudgetedTextPipeline)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string HudBranch = ExtractSourceBlock(Settings, "if(m_AppearanceSettingsTab == APPEARANCE_TAB_HUD)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_CHAT)");
	const std::string ChatBranch = ExtractSourceBlock(Settings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_CHAT)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)");
	const std::string NamePlateBranch = ExtractSourceBlock(Settings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION)");
	const std::string HookCollisionBranch = ExtractSourceBlock(Settings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_INFO_MESSAGES)");
	const std::string InfoMessagesBranch = ExtractSourceBlock(Settings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_INFO_MESSAGES)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_LASER)");
	ASSERT_FALSE(HudBranch.empty());
	ASSERT_FALSE(ChatBranch.empty());
	ASSERT_FALSE(NamePlateBranch.empty());
	ASSERT_FALSE(HookCollisionBranch.empty());
	ASSERT_FALSE(InfoMessagesBranch.empty());

	// These visible Appearance subtabs contain many fixed checkbox labels.
	// They must use the settings text cache/drain wrappers so first entry does
	// not synchronously create text containers for every visible checkbox.
	EXPECT_NE(HudBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_HUD"), std::string::npos);
	EXPECT_NE(ChatBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_CHAT"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_APPEARANCE, APPEARANCE_TAB_NAME_PLATE"), std::string::npos);
	EXPECT_EQ(HudBranch.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_EQ(ChatBranch.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_EQ(ChatBranch.find("DoButton_CheckBox("), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("DoButton_CheckBox("), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("DoButton_Menu(&s_NameplateResetLayoutButton"), std::string::npos);
	EXPECT_NE(HookCollisionBranch.find("DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_HOOK_COLLISION"), std::string::npos);
	EXPECT_NE(InfoMessagesBranch.find("DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, APPEARANCE_TAB_INFO_MESSAGES"), std::string::npos);
	EXPECT_EQ(HookCollisionBranch.find("DoButton_CheckBox("), std::string::npos);
	EXPECT_EQ(InfoMessagesBranch.find("DoButton_CheckBox("), std::string::npos);
}

TEST(QmMonitoringHelpers, AppearanceNamePlateHookStrengthSizeUsesSingleLineSlider)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string NamePlateBranch = ExtractSourceBlock(Settings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION)");
	ASSERT_FALSE(NamePlateBranch.empty());

	EXPECT_NE(NamePlateBranch.find("DoAppearanceNumericField(APPEARANCE_TAB_NAME_PLATE, \"appearance-hook-strength-size\""), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("SCROLLBAR_OPTION_MULTILINE"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/components/menus_settings.cpp").find("Localize(\"Glow\")"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/components/menus_settings.cpp").find("Localize(\"Glow range\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, AppearanceNamePlateTabUsesCardBackedScrollRegion)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string NamePlateBranch = ExtractSourceBlock(Settings, "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_NAME_PLATE)", "else if(m_AppearanceSettingsTab == APPEARANCE_TAB_HOOK_COLLISION)");
	ASSERT_FALSE(NamePlateBranch.empty());

	EXPECT_EQ(NamePlateBranch.find("DrawTClientCacheSectionBox("), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("s_NamePlateSettingsCardHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("s_NamePlateMeasuredCardHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("s_NamePlateMeasuredSettingsCardHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("s_NamePlateMeasuredPreviewCardHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("const float QmClientSettingsScrollbarWidth"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("const float QmClientSettingsScrollbarMargin = std::clamp(8.0f * UiScale, 6.0f, 8.0f);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("const float NamePlateContentPaddingY = std::clamp(14.0f * UiScale, 10.0f, 14.0f);"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("AddMeasuredCard(5,"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("ResolveSettingsRadioRowLayout"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("AddCard(6, NamePlatePreviewMinCardHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("const float NamePlateCardHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlateSettingsCard.h = NamePlateSettingsCardHeight;"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlateSettingsShadow.Draw(NamePlateSettingsShadowColor, IGraphics::CORNER_ALL, NamePlateCardCornerRadius);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlateSettingsCard.Draw(NamePlateSettingsGlassColor, IGraphics::CORNER_ALL, NamePlateCardCornerRadius);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlateSettingsTopHighlight.Draw(NamePlateSettingsHighlightColor, IGraphics::CORNER_NONE, 0.0f);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("LeftView.HSplitTop(NamePlateContentPaddingY, nullptr, &LeftView);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("LeftView.HSplitBottom(NamePlateContentPaddingY, &LeftView, nullptr);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewCard.h = NamePlateCardHeight;"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewCard.h = NamePlatePreviewCardHeight;"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewShadow.Draw(NamePlateSettingsShadowColor, IGraphics::CORNER_ALL, NamePlateCardCornerRadius);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewCard.Draw(NamePlateSettingsGlassColor, IGraphics::CORNER_ALL, NamePlateCardCornerRadius);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewTopHighlight.Draw(NamePlateSettingsHighlightColor, IGraphics::CORNER_NONE, 0.0f);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("RightView.HSplitTop(NamePlateContentPaddingY, nullptr, &RightView);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("RightView.HSplitBottom(NamePlateContentPaddingY, &RightView, nullptr);"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("GeneralContentHeight"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("NamePlateTextContentHeight"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("HookContentHeight"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("KeysContentHeight"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("BeginSettingsScrollRegion(s_NamePlateSettingsScrollRegion, &NamePlateSettingsView"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("NamePlatePreviewAreaHeight = 60.0f"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("ScrollParams.m_ScrollbarWidth ="), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("ScrollParams.m_ScrollbarMargin = QmClientSettingsScrollbarMargin;"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("ScrollParams.m_ScrollbarWidth = std::clamp(20.0f * UiScale, 18.0f, 20.0f);"), std::string::npos);
	EXPECT_EQ(NamePlateBranch.find("ScrollParams.m_ScrollbarMargin = std::clamp(5.0f * UiScale, 4.0f, 5.0f);"), std::string::npos);
	EXPECT_NE(NamePlateBranch.find("RenderNamePlatePreview"), std::string::npos);
	EXPECT_NE(Settings.find("const char *pAppearanceDeckTab ="), std::string::npos);
	EXPECT_NE(Settings.find("\"appearance-hud\""), std::string::npos);
	EXPECT_NE(Settings.find("std::array<CScrollRegion, NUMBER_OF_APPEARANCE_TABS> s_AppearanceSettingsCardScrollRegions"), std::string::npos);
	EXPECT_NE(Settings.find("SettingsCardDeckForRenderPass().RenderCached(AppearanceCardCtx, AppearancePage, pAppearanceDeckTab"), std::string::npos);
	EXPECT_NE(Settings.find("s_AppearanceSettingsCardScrollRegions[m_AppearanceSettingsTab]"), std::string::npos);
}

TEST(QmMonitoringHelpers, AppearanceSettingsHeadingsUseBudgetedTextPipeline)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Appearance subtab headings are visible settings chrome. They must go
	// through the settings text cache/drain path instead of direct UI labels,
	// otherwise first-entry HUD/Appearance tabs can create containers in render.
	EXPECT_NE(Body.find("auto DoAppearanceHeading"), std::string::npos);
	EXPECT_NE(Body.find("SettingsTextElement(SETTINGS_APPEARANCE, m_AppearanceSettingsTab"), std::string::npos);
	EXPECT_NE(Body.find("DoSettingsLabelStreamed(HeadingElement"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel_AutoLineSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, ControlsSettingsChromeUsesBudgetedTextPipeline)
{
	const std::string Controls = ReadRepoFile("src/game/client/components/menus_settings_controls.cpp");

	// The Controls settings page has a dense first frame: block headings,
	// option labels, controller labels and bind labels all sit above the fold.
	// They must use the shared settings text cache/drain path so first entry
	// cannot synchronously create a large batch of text containers in render.
	EXPECT_NE(Controls.find("DoSettingsControlsLabel("), std::string::npos);
	EXPECT_NE(Controls.find("DoSettingsControlsCheckBox("), std::string::npos);
	EXPECT_NE(Controls.find("ui_widget::NumericField("), std::string::npos);
	EXPECT_NE(Controls.find("DoSettingsButton_Menu(CMenus::SETTINGS_CONTROLS"), std::string::npos);
	EXPECT_EQ(Controls.find("Ui()->DoLabel("), std::string::npos);
	EXPECT_EQ(Controls.find("Ui()->DoLabel_AutoLineSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TeeSkinFilterCheckboxesUseBudgetedTextPipeline)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string TeeBody = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(TeeBody.empty());

	// The Tee page is list-heavy, but the fixed skin filter checkboxes are
	// still settings chrome. Keep them on the shared settings text cache/drain
	// path instead of direct checkbox labels.
	EXPECT_NE(TeeBody.find("DoSettingsButton_CheckBox(SETTINGS_TEE"), std::string::npos);
	EXPECT_EQ(TeeBody.find("DoButton_CheckBox(&g_Config.m_ClDownloadSkins"), std::string::npos);
	EXPECT_EQ(TeeBody.find("DoButton_CheckBox(&g_Config.m_ClDownloadCommunitySkins"), std::string::npos);
	EXPECT_EQ(TeeBody.find("DoButton_CheckBox(&g_Config.m_ClVanillaSkinsOnly"), std::string::npos);
	EXPECT_EQ(TeeBody.find("DoButton_CheckBox(&g_Config.m_ClFatSkins"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsRadioMenusUseBudgetedTextPipeline)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Controls = ReadRepoFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	// Settings radio rows are fixed UI chrome: one label plus a small set of
	// fixed buttons. They should use the same text cache/drain helpers as
	// checkboxes and scrollbars instead of the generic direct-label helper.
	EXPECT_NE(Header.find("DoSettingsLine_RadioMenu("), std::string::npos);
	EXPECT_NE(Source.find("bool CMenus::DoSettingsLine_RadioMenu("), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsLabel(Page, Tab, pLabelTextId"), std::string::npos);
	EXPECT_NE(Source.find("DoSettingsButton_Menu(Page, Tab, Subtab"), std::string::npos);
	EXPECT_EQ(Controls.find("DoLine_RadioMenu("), std::string::npos);
	EXPECT_EQ(Settings.find("DoLine_RadioMenu("), std::string::npos);
	EXPECT_EQ(TClient.find("DoLine_RadioMenu("), std::string::npos);
	EXPECT_EQ(QmClient.find("DoLine_RadioMenu("), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientVisibleCheckboxesUseBudgetedTextPipeline)
{
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::vector<std::string> vFunctionBodies = {
		ExtractSourceFunctionBody(TClient, "float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)"),
		ExtractSourceFunctionBody(TClient, "float CMenus::LayoutTClientPetCacheSection(CUIRect &CurrentColumn, bool Render)"),
		ExtractSourceFunctionBody(TClient, "float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)")};
	const std::vector<std::string> vRenderBlocks = {
		ExtractSourceBlock(TClient, "auto LayoutVisualNameplateSection", "auto LayoutVisualEffectsSection"),
		ExtractSourceBlock(TClient, "auto LayoutVisualEffectsSection", "auto LayoutInputSection"),
		ExtractSourceBlock(TClient, "auto LayoutInputSection", "auto LayoutAntiLatencyToolsSection"),
		ExtractSourceBlock(TClient, "auto LayoutAntiLatencyToolsSection", "auto LayoutAntiPingSmoothingSection"),
		ExtractSourceBlock(TClient, "auto LayoutAntiPingSmoothingSection", "auto LayoutAutoExecuteSection"),
		ExtractSourceBlock(TClient, "auto LayoutPetSection", "auto MeasurePetSection"),
		ExtractSourceBlock(TClient, "auto RenderPetInteractiveSection", "auto LayoutAutoReplySection"),
		ExtractSourceBlock(TClient, "auto LayoutAutoReplySection", "auto MeasureAutoReplySection"),
		ExtractSourceBlock(TClient, "auto RenderAutoReplyInteractiveSection", "auto LayoutPlayerIndicatorSection"),
		ExtractSourceBlock(TClient, "auto LayoutPlayerIndicatorSection", "// ---- CSectionLoader")};

	EXPECT_NE(Header.find("DoTClientSettingsButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_NE(Header.find("DoTClientSettingsButton_CheckBox("), std::string::npos);
	EXPECT_NE(Header.find("DoTClientSettingsButton_Menu("), std::string::npos);
	EXPECT_NE(TClient.find("int CMenus::DoTClientSettingsButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_NE(TClient.find("int CMenus::DoTClientSettingsButton_CheckBox("), std::string::npos);
	EXPECT_NE(TClient.find("int CMenus::DoTClientSettingsButton_Menu("), std::string::npos);
	EXPECT_NE(TClient.find("return DoSettingsButton_CheckBoxAutoVMarginAndSet(SETTINGS_TCLIENT, m_TClientSettingsTab"), std::string::npos);
	EXPECT_NE(TClient.find("return DoSettingsButton_CheckBox(SETTINGS_TCLIENT, m_TClientSettingsTab"), std::string::npos);
	EXPECT_NE(TClient.find("return DoSettingsButton_Menu(SETTINGS_TCLIENT, m_TClientSettingsTab"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_CheckBox(&g_Config"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_CheckBox(&m_Dummy"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_CheckBox(&s_CustomColorId"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_CheckBox(&s_TcUiTag"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_LoadButton, Localize(\"Load\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_SaveButton, Localize(\"Save\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_DeleteButton, Localize(\"Delete\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_OverrideButton, Localize(\"Override\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_ProfilesFile, Localize(\"Profiles file\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_ApplyBtn, Localize(\"Apply Changes\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&s_ClearBtn, Localize(\"Clear Changes\")"), std::string::npos);
	EXPECT_EQ(TClient.find("DoButton_Menu(&ResetBtn, Localize(\"Reset\")"), std::string::npos);
	for(const auto &Body : vFunctionBodies)
	{
		ASSERT_FALSE(Body.empty());
		EXPECT_NE(Body.find("DoTClientSettingsButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
		EXPECT_EQ(Body.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	}
	for(const auto &Block : vRenderBlocks)
	{
		ASSERT_FALSE(Block.empty());
		EXPECT_NE(Block.find("DoTClientSettingsButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
		EXPECT_EQ(Block.find("DoButton_CheckBoxAutoVMarginAndSet("), std::string::npos);
	}
}

TEST(QmMonitoringHelpers, AssetsEntityBgPreviewUsesArtifactJob)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Preview = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/components/qmclient/settings_resource_preview.h>"), std::string::npos);
	EXPECT_NE(Preview.find("CSettingsResourcePreviewJob"), std::string::npos);
	EXPECT_NE(Source.find("SResourcePreviewKey"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewCache"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewScheduler"), std::string::npos);
	EXPECT_NE(Body.find("SettingsResourcePreviewDrawResult"), std::string::npos);
	EXPECT_NE(Body.find("ESettingsResourcePreviewDrawResult::PLACEHOLDER"), std::string::npos);
	EXPECT_NE(Body.find("preview_jobs_started=%d preview_jobs_done=%d preview_uploads=%d preview_admissions=%d preview_artifact_ms=%.3f metadata_hydrate_ms=%.3f metadata_hydrated=%d placeholder_count=%d ready_texture_count=%d visible_ready_ratio=%.3f"), std::string::npos);
	EXPECT_NE(Body.find("CanStartPreviewJob(ESettingsResourcePreviewPriority::VISIBLE"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsResourcePreviewUploadScheduler gs_SettingsAssetsResourcePreviewUploadScheduler"), std::string::npos);
	EXPECT_NE(Source.find("ProcessAssetsResourcePreviewJobs("), std::string::npos);
	EXPECT_NE(Source.find("StartAssetsEntityBgPreviewArtifactJob("), std::string::npos);
	EXPECT_NE(Source.find("m_vEntityBgPreviewJobs"), std::string::npos);
	EXPECT_NE(Body.find("ProcessAssetsResourcePreviewJobs(Graphics(), ResourcePreviewTelemetry"), std::string::npos);
	EXPECT_NE(Body.find("StartAssetsEntityBgPreviewArtifactJob(PreviewKey"), std::string::npos);
	EXPECT_EQ(Body.find("gs_SettingsAssetsResourcePreviewCache.MarkPreviewJobStarted(PreviewKey);"), std::string::npos);
	EXPECT_NE(Body.find("SettingsResourcePreviewDrawResult(ResourcePreviewState)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEntityBgPreviewArtifactJobDoesNotDependOnExistingPreviewImage)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string PreviewHeader = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string PreviewSource = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string StartBody = ExtractSourceFunctionBody(Source, "static bool StartAssetsEntityBgPreviewArtifactJob");
	ASSERT_FALSE(StartBody.empty());

	EXPECT_EQ(StartBody.find("pItem->m_PreviewImage.m_pData == nullptr"), std::string::npos);
	EXPECT_EQ(StartBody.find("m_PreviewImage.DeepCopy()"), std::string::npos);
	EXPECT_NE(StartBody.find("ResolveEntityBgPreviewArtifactSource("), std::string::npos);
	EXPECT_NE(StartBody.find("CSettingsResourcePreviewJob::FromPath("), std::string::npos);
	EXPECT_NE(PreviewHeader.find("static std::shared_ptr<CSettingsResourcePreviewJob> FromPath("), std::string::npos);
	EXPECT_NE(PreviewSource.find("LoadPng("), std::string::npos);
	EXPECT_NE(PreviewSource.find("BuildPreviewArtifactFromPath("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardDrawLoopDoesNotRunHeavyPreview)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t RenderPreviewStart = Source.find("auto RenderAssetsCardPreview = ");
	ASSERT_NE(RenderPreviewStart, std::string::npos);
	const size_t RenderPreviewEnd = Source.find("auto RenderAssetsCardLoadingShells", RenderPreviewStart);
	ASSERT_NE(RenderPreviewEnd, std::string::npos);
	const std::string RenderPreviewBody = Source.substr(RenderPreviewStart, RenderPreviewEnd - RenderPreviewStart);

	EXPECT_NE(RenderPreviewBody.find("SettingsResourcePreviewDrawResult"), std::string::npos);
	EXPECT_NE(RenderPreviewBody.find("DrawResourcePreviewPlaceholder"), std::string::npos);
	EXPECT_EQ(RenderPreviewBody.find("Localize(\"Video Background\")"), std::string::npos);
	EXPECT_EQ(RenderPreviewBody.find("Localize(\"Map Preview\")"), std::string::npos);
	EXPECT_EQ(RenderPreviewBody.find("FONT_ICON_PLAY"), std::string::npos);
	EXPECT_EQ(RenderPreviewBody.find("m_DrawEntityBgVideoFallback"), std::string::npos);
	EXPECT_EQ(RenderPreviewBody.find("m_DrawEntityBgFallback"), std::string::npos);
	EXPECT_EQ(Body.find("PreviewState.m_DrawEntityBgVideoFallback ="), std::string::npos);
	EXPECT_EQ(Body.find("PreviewState.m_DrawEntityBgFallback ="), std::string::npos);
	const size_t MapFallback = Source.find("auto RenderEntityBgFallback = ");
	ASSERT_NE(MapFallback, std::string::npos);
	const size_t VideoFallback = Source.find("auto RenderEntityBgVideoFallback = ", MapFallback);
	ASSERT_NE(VideoFallback, std::string::npos);
	const size_t RenderLoop = Source.find("s_WorkshopAssetsListBox.SkipItems", VideoFallback);
	ASSERT_NE(RenderLoop, std::string::npos);
	const std::string FallbackBody = Source.substr(MapFallback, RenderLoop - MapFallback);
	EXPECT_EQ(FallbackBody.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_SETTINGS"), std::string::npos);
	EXPECT_NE(FallbackBody.find("Ui()->DoLabel(&LabelRect"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardDrawLoopDoesNotRunPreviewOrTextLayout)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("RequestAssetsCardMetadataHydration"), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardMetadataCached"), std::string::npos);
	EXPECT_NE(Body.find("SettingsResourcePreviewDrawResult(ResourcePreviewState)"), std::string::npos);
	EXPECT_EQ(Body.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
	EXPECT_EQ(Body.find("RefreshAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(Body.find("PreviewState.m_DrawEntityTileArtifact ="), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardMetadataRenderingUsesBudgetedMenuText)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const size_t MetadataStart = Source.find("auto RenderAssetsCardMetadata = ");
	ASSERT_NE(MetadataStart, std::string::npos);
	const size_t MetadataEnd = Source.find("auto RenderEntityBgFallback = ", MetadataStart);
	ASSERT_NE(MetadataEnd, std::string::npos);
	const std::string MetadataBody = Source.substr(MetadataStart, MetadataEnd - MetadataStart);

	EXPECT_NE(MetadataBody.find("DoMenuLabelStreamed(MENU_TEXT_SCOPE_SETTINGS"), std::string::npos);
	EXPECT_EQ(MetadataBody.find("Ui()->DoLabelStreamed("), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsLocalCardsUseSharedMetadataRenderer)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());
	const size_t LocalBranch = Body.find("if(!UsesCombinedAssetList(pCurrentCategory))");
	ASSERT_NE(LocalBranch, std::string::npos);
	const size_t LocalBranchEnd = Body.find("if(const SAssetResourceCategory *pCategory = AssetResourceCategoryByTab(s_CurCustomTab); UsesCombinedAssetList(pCategory)", LocalBranch);
	ASSERT_NE(LocalBranchEnd, std::string::npos);
	const std::string LocalBody = Body.substr(LocalBranch, LocalBranchEnd - LocalBranch);

	EXPECT_NE(LocalBody.find("RenderAssetsCardMetadataCached("), std::string::npos);
	EXPECT_NE(LocalBody.find("RequestAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_EQ(LocalBody.find("Ui()->DoLabel(&TitleRect"), std::string::npos);
	EXPECT_EQ(LocalBody.find("Ui()->DoLabel(&HeaderLayout.m_AuthorRect"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsMetadataCacheMissUsesVisibleFallbackOutsideShellOnlyFrame)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// The card shell can be frame-0 only, but the first readable non-shell frame
	// should still paint the current title/author/status instead of staying blank.
	EXPECT_NE(Body.find("RenderAssetsCardMetadataFallback(Ui(), Shell"), std::string::npos);
	EXPECT_NE(Body.find("else if(AssetsRenderCardMetadataFallback)"), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardMetadataCached(Shell, pMetadata, RenderAssetsCardMetadata);"), std::string::npos);
}

TEST(QmMonitoringHelpers, SkinsAndTeeDoNotExposePartialPreviewUploads)
{
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Skins, "CSkins::ESkinProcessResult CSkins::DrainSettingsSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,\n\tint &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,\n\tstd::chrono::nanoseconds MaxTime)");
	ASSERT_FALSE(DrainBody.empty());

	EXPECT_NE(DrainBody.find("LoadSkinFinish(pSkinContainer"), std::string::npos);
	EXPECT_EQ(DrainBody.find("BeginSkinPreviewUpload(pSkinContainer"), std::string::npos);
	EXPECT_EQ(DrainBody.find("UploadNextSkinPreviewSprite(pSkinContainer, SkinPreviewUploadBudget)"), std::string::npos);
	EXPECT_EQ(DrainBody.find("FinishSkinPreviewUpload(pSkinContainer)"), std::string::npos);
	EXPECT_NE(DrainBody.find("SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget, SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)"), std::string::npos);
	EXPECT_NE(Settings.find("tee_preview_pipeline"), std::string::npos);
	EXPECT_NE(Settings.find("BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, \"tee\""), std::string::npos);
}

TEST(QmMonitoringHelpers, SkinsTeeUploadBudgetRequeuesInsteadOfFailing)
{
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");
	const std::string DrainBody = ExtractSourceFunctionBody(Skins, "CSkins::ESkinProcessResult CSkins::DrainSettingsSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,\n\tint &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,\n\tstd::chrono::nanoseconds MaxTime)");
	ASSERT_FALSE(DrainBody.empty());

	// When the shared upload budget is exhausted, Tee/skin preview completion must
	// stay queued. Marking it failed here caused default yellow tees/question marks
	// to leak into the list during fast scrolling.
	const size_t BudgetCheck = DrainBody.find("!SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget, SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)");
	const size_t LoadFinish = DrainBody.find("LoadSkinFinish(pSkinContainer");
	ASSERT_NE(BudgetCheck, std::string::npos);
	ASSERT_NE(LoadFinish, std::string::npos);
	EXPECT_LT(BudgetCheck, LoadFinish);
	const std::string BudgetBlockedBody = DrainBody.substr(BudgetCheck, LoadFinish - BudgetCheck);
	EXPECT_NE(BudgetBlockedBody.find("return ESkinProcessResult::BREAK_GPU_LIMIT;"), std::string::npos);
	EXPECT_EQ(BudgetBlockedBody.find("SetState(CSkinContainer::EState::ERROR"), std::string::npos);
	EXPECT_EQ(BudgetBlockedBody.find("LoadSkinFinish("), std::string::npos);
}

TEST(QmMonitoringHelpers, SharedPreviewUploadSchedulerRejectsPartialCommit)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");
	const std::string DrainOneBody = ExtractSourceFunctionBody(Source, "bool CSettingsResourcePreviewUploadScheduler::DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)");
	ASSERT_FALSE(DrainOneBody.empty());

	// Commit/finalize must be reserve -> valid texture -> commit. A failed or
	// budget-blocked upload must not consume budget or expose a half-ready texture.
	EXPECT_EQ(Header.find("m_pTargetTexture"), std::string::npos);
	const size_t ConsumePos = DrainOneBody.find("!SettingsResourcePreviewConsumeUploadBudget(Budget)");
	const size_t UploadPos = DrainOneBody.find("pGraphics->LoadTextureRawMove");
	const size_t ValidPos = DrainOneBody.find("if(Texture.IsValid())");
	const size_t CommitPos = DrainOneBody.find("SettingsResourcePreviewCommitUploadBudget(Budget)");
	const size_t FinalizeTruePos = DrainOneBody.find("Item.m_Finalize(true, Texture)");
	ASSERT_NE(ConsumePos, std::string::npos);
	ASSERT_NE(UploadPos, std::string::npos);
	ASSERT_NE(ValidPos, std::string::npos);
	ASSERT_NE(CommitPos, std::string::npos);
	ASSERT_NE(FinalizeTruePos, std::string::npos);
	EXPECT_LT(ConsumePos, UploadPos);
	EXPECT_LT(UploadPos, ValidPos);
	EXPECT_LT(ValidPos, CommitPos);
	EXPECT_LT(CommitPos, FinalizeTruePos);
	const std::string BudgetBlockedBody = DrainOneBody.substr(ConsumePos, UploadPos - ConsumePos);
	EXPECT_EQ(BudgetBlockedBody.find("Item.m_Finalize(false)"), std::string::npos);
	EXPECT_EQ(BudgetBlockedBody.find("Cache.MarkPreviewJobDone(Item.m_Key, false)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabSwitchFirstFrameShellOnly)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("m_TabSwitchShellOnlyFrame"), std::string::npos);
	EXPECT_NE(Body.find("AssetsTabSwitchFirstFrame"), std::string::npos);
	EXPECT_NE(Body.find("CardHydrationScheduler.m_TabSwitchShellOnlyFrame"), std::string::npos);
	EXPECT_NE(Body.find("PreviewPipelineScheduler.BeginFrame("), std::string::npos);
	EXPECT_NE(Body.find("PreviewPipelineScheduler.SetShellOnlyFrame(AssetsShellOnlyFrame)"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsPreviewArtifactTokensThisFrame = AssetsContentWarmupBlocked ? 0 : AdaptivePreviewArtifactTokens;"), std::string::npos);
	EXPECT_NE(Body.find("tab_switch_shell_only=%d"), std::string::npos);
	EXPECT_EQ(Body.find("if(AssetsTabSwitchFirstFrame)\n\t\t\t\t\t\t++ResourcePreviewTelemetry.m_PlaceholderCount;"), std::string::npos);
	EXPECT_EQ(Body.find("StartWorkshopThumb(Asset, SettingsWorkshopThumbShouldStartHighPriority"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabSwitchUsesShellFirstFrame)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string BeginFrameBody = ExtractSourceFunctionBody(Source, "static SSettingsAssetsCardHydrationScheduler BeginAssetsCardHydrationFrame");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(BeginFrameBody.empty());
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("constexpr int AssetsTabSwitchCooldownFrames = 8;"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("Scheduler.m_TabSwitchShellOnlyFrame = AssetsTabSwitchFirstFrame;"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("if(AssetsTabSwitchFirstFrame)"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("Scheduler.m_MetadataBudget = maximum(1, minimum(VisibleCardCount, MetadataLayoutTokens));"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("Scheduler.m_PreviewBudget = 0;"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("return Scheduler;"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("Scheduler.m_PreviewBudget = AssetsTabSwitchCooldownActive ? 0"), std::string::npos);
	EXPECT_NE(Body.find("LogAssetsFramePerfStage(\"assets_tab_switch_shell_first\""), std::string::npos);
	EXPECT_NE(Body.find("operation=settings_assets_tab_switch"), std::string::npos);
	EXPECT_NE(Body.find("tab_switch_shell_only=1"), std::string::npos);
	EXPECT_NE(Body.find("CardHydrationScheduler.m_TabSwitchShellOnlyFrame ? 1 : 0"), std::string::npos);
}

TEST(QmMonitoringHelpers, SkinsUseSharedPreviewUploadBudget)
{
	const std::string Skins = ReadRepoFile("src/game/client/components/skins.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/qmclient/settings_resource_preview.h");

	EXPECT_NE(Header.find("CSettingsResourcePreviewScheduler"), std::string::npos);
	EXPECT_NE(Header.find("CSettingsResourcePreviewUploadScheduler"), std::string::npos);
	EXPECT_NE(Skins.find("#include <game/client/components/qmclient/settings_resource_preview.h>"), std::string::npos);
	EXPECT_NE(Skins.find("SResourcePreviewUploadBudget SkinPreviewUploadBudget"), std::string::npos);
	const std::string DrainBody = ExtractSourceFunctionBody(Skins, "CSkins::ESkinProcessResult CSkins::DrainSettingsSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,\n\tint &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,\n\tstd::chrono::nanoseconds MaxTime)");
	ASSERT_FALSE(DrainBody.empty());
	EXPECT_NE(DrainBody.find("SkinPreviewUploadBudget.m_MaxUploads = GameClient()->GpuUploadLimiter()->RemainingUploads();"), std::string::npos);
	EXPECT_EQ(DrainBody.find("UploadNextSkinPreviewSprite(pSkinContainer, SkinPreviewUploadBudget)"), std::string::npos);
	EXPECT_NE(DrainBody.find("SettingsResourcePreviewCommitUploadBudget(SkinPreviewUploadBudget, SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)"), std::string::npos);
	EXPECT_NE(Skins.find("preview_uploads"), std::string::npos);
	EXPECT_NE(DrainBody.find("SettingsResourcePreviewConsumeUploadBudget(SkinPreviewUploadBudget, SETTINGS_SKIN_SOURCE_TEXTURE_UPLOADS)"), std::string::npos);
	EXPECT_NE(DrainBody.find("LoadSkinFinish(pSkinContainer"), std::string::npos);
	EXPECT_NE(Menus.find("SResourcePreviewTelemetry TeePreviewTelemetry"), std::string::npos);
	EXPECT_NE(Menus.find("SettingsResourcePreviewDrawResult(TeeResourcePreviewState)"), std::string::npos);
	EXPECT_NE(Menus.find("tee_preview_admissions=%d tee_ready_textures=%d tee_placeholders=%d"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsMetadataHydrationIsBudgetedAndTimed)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
	EXPECT_NE(Source.find("ResourcePreviewTelemetry.m_MetadataHydrateMs += HydrateTimer.ElapsedMs();"), std::string::npos);
	EXPECT_NE(Source.find("static int DrainAssetsCardMetadataHydrationRequests"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsInitialMetadataLayoutTokens = maximum(1, minimum(AdaptiveBudget.m_VisibleTokens, 4));"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsMetadataLayoutTokensThisFrame = AssetsShellOnlyFrame ? AssetsInitialMetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Body.find("DrainAssetsCardMetadataHydrationRequests(AssetsMetadataLayoutTokensThisFrame"), std::string::npos);
	EXPECT_NE(Body.find("RequestAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_EQ(Body.find("HydrateAssetsCardMetadataTimed("), std::string::npos);
	EXPECT_EQ(Body.find("HydrateAssetsCardMetadata(\n"), std::string::npos);
}

TEST(QmMonitoringHelpers, TextGlyphContainerTelemetryExists)
{
	const std::string Text = ReadRepoFile("src/engine/client/text.cpp");
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	EXPECT_NE(Text.find("perf/text"), std::string::npos);
	EXPECT_NE(Text.find("event=text_runtime_budget"), std::string::npos);
	EXPECT_NE(Text.find("glyph_new=%d glyph_uploads=%d glyph_rasterize_ms=%.3f glyph_upload_ms=%.3f text_container_new=%d text_container_uploads=%d text_container_create_ms=%.3f text_container_upload_ms=%.3f"), std::string::npos);
	EXPECT_NE(Text.find("m_QmPerfGlyphNew"), std::string::npos);
	EXPECT_NE(Text.find("m_QmPerfTextContainerUploads"), std::string::npos);
	EXPECT_NE(Stats.find("export interface TextRuntimeBudgetSummary"), std::string::npos);
	EXPECT_NE(Stats.find("textRuntimeBudgetSummary(entries"), std::string::npos);
	EXPECT_NE(Report.find("Text Pipeline"), std::string::npos);
	EXPECT_NE(Tests.find("testTextRuntimeBudgetSummary"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfAnalyzerReportsOnePctLowAndPreviewBudget)
{
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Quality = ReadRepoFile("qmclient_scripts/perf/lib/quality.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	EXPECT_NE(Stats.find("export interface PreviewBudgetSummary"), std::string::npos);
	EXPECT_NE(Stats.find("previewBudgetSummary(entries"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Stats, {
					       "preview_jobs_started",
					       "preview_jobs_done",
					       "preview_uploads",
					       "preview_admissions",
					       "visible_ready_ratio",
					       "fpsOnePctLowAvailable",
					       "coldTabSwitchFpsSummaries",
					       "warmTabSwitchFpsSummaries",
				       }));
	EXPECT_TRUE(ContainsAll(Report, {"Cold/Warm Tab Switch", "Preview Budget", "1% Low Target"}));
	EXPECT_NE(Quality.find("previewBudget"), std::string::npos);
	EXPECT_NE(Tests.find("testPreviewBudgetSummaryAndColdWarmTabSwitches"), std::string::npos);
}

TEST(QmMonitoringHelpers, PerfAnalyzerCorrelatesOnePctLowWithTextAndResourceBudgets)
{
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Quality = ReadRepoFile("qmclient_scripts/perf/lib/quality.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	EXPECT_NE(Stats.find("export interface BudgetCorrelationWindow"), std::string::npos);
	EXPECT_NE(Stats.find("export function budgetCorrelationSummary(entries: PerfEntry[]): BudgetCorrelationSummary"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Stats, {"dominantAttribution", "culpritRank"}));
	EXPECT_NE(Quality.find("budgetCorrelation: budgetCorrelationSummary(entries)"), std::string::npos);
	EXPECT_TRUE(ContainsAll(Report, {"Budget Attribution by Window", "Top Culprit", "Text Pipeline", "Preview Budget", "UI Frame Scheduler", "1% Low Target"}));
	EXPECT_TRUE(ContainsAll(Tests, {
					       "testUnifiedFrameSchedulerAndTextPipelineBudgetSummary",
					       "testPerfAnalyzerCorrelatesOnePctLowWithTextAndResourceBudgets",
					       "testBudgetCorrelationSummaryByFpsWindow",
					       "testBudgetCorrelationRanksCulprits",
				       }));
}

TEST(QmMonitoringHelpers, PerfReportDefaultsToStatisticalSections)
{
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	// The perf report should default to statistical summaries and sampled charts
	// instead of dumping the full raw log stream.
	EXPECT_NE(Report.find("Assets Draw Distribution"), std::string::npos);
	EXPECT_NE(Report.find("Text Pipeline"), std::string::npos);
	EXPECT_NE(Report.find("Budget Attribution by Window"), std::string::npos);
	EXPECT_NE(Report.find("statisticalSummary("), std::string::npos);
	EXPECT_NE(Report.find("sampleArrayEvenly("), std::string::npos);
	EXPECT_NE(Tests.find("testReportUsesStatisticalBudgetReportInsteadOfRawBudgetDump"), std::string::npos);
	EXPECT_NE(Tests.find("testReportSamplesLargeEmbeddedChartData"), std::string::npos);
	EXPECT_EQ(Report.find("raw event stream"), std::string::npos);
}

TEST(QmMonitoringHelpers, LowFpsWindowRequiresRealSampledAndTopCulprit)
{
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Quality = ReadRepoFile("qmclient_scripts/perf/lib/quality.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	// A 240Hz gate cannot be satisfied with P99-derived placeholders or an
	// unattributed low-fps window.
	EXPECT_NE(Report.find("Top Culprit"), std::string::npos);
	EXPECT_NE(Report.find("unattributed_spike"), std::string::npos);
	EXPECT_NE(Quality.find("fps_1pct_low_missing_real_sampled"), std::string::npos);
	EXPECT_NE(Quality.find("unattributed_spike"), std::string::npos);
	EXPECT_NE(Quality.find("quality.failed ? 'FAIL'"), std::string::npos);
	EXPECT_NE(Tests.find("testPerfAnalyzerFailsUnattributedLowFpsWindow"), std::string::npos);
	EXPECT_NE(Tests.find("testMissingOnePctLowIsMarkedP99DerivedAndNotTargetPass"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardShellUsesCompactCurrentLabelBadges)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const size_t LayoutStart = Source.find("auto LayoutAssetsCardShell = ");
	ASSERT_NE(LayoutStart, std::string::npos);
	const size_t LayoutEnd = Source.find("auto ComputeAssetPreviewContentSize = ", LayoutStart);
	ASSERT_NE(LayoutEnd, std::string::npos);
	const std::string LayoutBody = Source.substr(LayoutStart, LayoutEnd - LayoutStart);

	// Small resource cards should size the right-side chip from the current
	// label with tight padding instead of reserving the old wide English badge
	// widths. The left title/id lane keeps the remaining width.
	EXPECT_NE(Source.find("AssetsCardStatusTagHorizontalPadding"), std::string::npos);
	EXPECT_NE(Source.find("AssetsCardStatusTagMaxWidth"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetCardHeaderMargin = 3.0f;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr float AssetCardHeaderControlMargin = 1.0f;"), std::string::npos);
	EXPECT_EQ(Source.find("auto LayoutAssetCardHeader = "), std::string::npos);
	EXPECT_EQ(Source.find("const float AssetsCardDownloadedTagWidth = TextRender()->TextWidth"), std::string::npos);
	EXPECT_EQ(Source.find("const float AssetsCardLocalOnlyBadgeWidth = TextRender()->TextWidth"), std::string::npos);
	EXPECT_NE(LayoutBody.find("auto ComputeBadgeWidth = [&](const char *pLabel)"), std::string::npos);
	EXPECT_NE(LayoutBody.find("TextRender()->TextWidth(AssetsCardStatusTagFontSize, pLabel"), std::string::npos);
	EXPECT_NE(LayoutBody.find("const float TitleMinWidth = 12.0f;"), std::string::npos);
	EXPECT_NE(LayoutBody.find("ReserveTrailingRect(TitleRect, ComputeBadgeWidth(pStatusLabel)"), std::string::npos);
	EXPECT_NE(LayoutBody.find("ReserveTrailingRect(TitleRect, ComputeBadgeWidth(Localize(\"Local-only\"))"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardMetadataFallbackUsesStableTextGeometry)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string FallbackBody = ExtractSourceFunctionBody(Source, "static void RenderAssetsCardMetadataFallback");
	const std::string MetadataBody = ExtractSourceFunctionBody(Source, "auto RenderAssetsCardMetadata");
	ASSERT_FALSE(FallbackBody.empty());
	ASSERT_FALSE(MetadataBody.empty());

	// Cache-miss frames must look like cache-hit frames. Larger fallback text
	// caused title/status overlap and visible size changes while metadata hydrated.
	EXPECT_NE(FallbackBody.find("AssetsCardTitleFontSize"), std::string::npos);
	EXPECT_NE(FallbackBody.find("AssetsCardAuthorFontSize"), std::string::npos);
	EXPECT_NE(MetadataBody.find("AssetsCardTitleFontSize"), std::string::npos);
	EXPECT_NE(MetadataBody.find("AssetsCardAuthorFontSize"), std::string::npos);
	EXPECT_NE(FallbackBody.find("SLabelProperties TitleProps"), std::string::npos);
	EXPECT_NE(FallbackBody.find("TitleProps.m_EllipsisAtEnd = true;"), std::string::npos);
	EXPECT_NE(FallbackBody.find("AuthorProps.m_EllipsisAtEnd = true;"), std::string::npos);
	EXPECT_EQ(FallbackBody.find("Shell.m_ActionButtonRect : Shell.m_TitleRect"), std::string::npos);
	EXPECT_NE(FallbackBody.find("Shell.m_StatusTagRect"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardMetadataFallbackRendersStatusTags)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string FallbackBody = ExtractSourceFunctionBody(Source, "static void RenderAssetsCardMetadataFallback");
	const std::string MetadataBody = ExtractSourceFunctionBody(Source, "auto RenderAssetsCardMetadata");
	ASSERT_FALSE(FallbackBody.empty());
	ASSERT_FALSE(MetadataBody.empty());

	// Status tags are part of the fixed card shell. A cache miss must not hide
	// Local/Network/Downloaded badges while metadata hydration catches up.
	EXPECT_NE(FallbackBody.find("Shell.m_HasStatusTag"), std::string::npos);
	EXPECT_NE(FallbackBody.find("Shell.m_StatusTagRect"), std::string::npos);
	EXPECT_NE(FallbackBody.find("pStatusLabel"), std::string::npos);
	EXPECT_NE(FallbackBody.find("pUi->DoLabel(&StatusRect"), std::string::npos);
	EXPECT_NE(MetadataBody.find("Metadata.m_StatusLabel.c_str()"), std::string::npos);
	EXPECT_EQ(FallbackBody.find("(void)pStatusLabel"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsScrollRendersCachedMetadata)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Scroll pressure blocks preview/upload work, not visible metadata drawing.
	// A ready cache entry is cheap O(visible) drawing and must stay visible.
	EXPECT_NE(Body.find("const bool AssetsRenderCardMetadataFallback = !AssetsShellOnlyFrame;"), std::string::npos);
	EXPECT_NE(Body.find("if(pMetadata != nullptr)\n\t\t\t\tRenderAssetsCardMetadataCached"), std::string::npos);
	EXPECT_NE(Body.find("if(pMetadata != nullptr)\n\t\t\t\t\t\tRenderAssetsCardMetadataCached"), std::string::npos);
	EXPECT_EQ(Body.find("if(pMetadata != nullptr && !AssetsContentWarmupBlocked)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsScrollRendersCachedMetadataWithoutStreamedContainerWait)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Holding the scrollbar disables text-container creation. Cached metadata
	// must still be readable by drawing the cached strings through fixed-geometry
	// fallback instead of waiting for DoMenuLabelStreamed containers to hydrate.
	EXPECT_NE(Body.find("pMetadata != nullptr && AssetsContentWarmupBlocked"), std::string::npos);
	EXPECT_NE(Body.find("pMetadata->m_Title.c_str()"), std::string::npos);
	EXPECT_NE(Body.find("pMetadata->m_Author.c_str()"), std::string::npos);
	EXPECT_NE(Body.find("pMetadata->m_StatusLabel.c_str()"), std::string::npos);
	EXPECT_LT(Body.find("pMetadata != nullptr && AssetsContentWarmupBlocked"), Body.find("RenderAssetsCardMetadataCached(Shell, pMetadata, RenderAssetsCardMetadata);"));
}

TEST(QmMonitoringHelpers, AssetsScrollMissStillShowsImmediateMetadataFallback)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	// Fast scrollbar drags block hydration work, but visible card titles must
	// remain readable even when the metadata cache missed for a newly exposed
	// range. Missing metadata may use immediate fixed-geometry fallback; it must
	// not be hidden behind AssetsRenderCardMetadataFallback.
	EXPECT_NE(Body.find("const bool AssetsRenderCardMetadataFallback = !AssetsShellOnlyFrame;"), std::string::npos);
	EXPECT_EQ(Body.find("const bool AssetsRenderCardMetadataFallback = !AssetsContentWarmupBlocked;"), std::string::npos);
	EXPECT_NE(Body.find("else if(AssetsRenderCardMetadataFallback)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsFirstVisibleFrameHasMetadataWarmupBudget)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string BeginFrameBody = ExtractSourceFunctionBody(Source, "static SSettingsAssetsCardHydrationScheduler BeginAssetsCardHydrationFrame");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(BeginFrameBody.empty());
	ASSERT_FALSE(Body.empty());

	// First visible frame should have ready metadata for the first screen, not a
	// long shell-only phase. Preview/upload can stay blocked; metadata gets a
	// small first-frame budget.
	EXPECT_NE(BeginFrameBody.find("if(AssetsTabSwitchFirstFrame)"), std::string::npos);
	EXPECT_NE(BeginFrameBody.find("Scheduler.m_MetadataBudget = maximum(1, minimum(VisibleCardCount, MetadataLayoutTokens));"), std::string::npos);
	EXPECT_NE(Body.find("const int AssetsMetadataLayoutTokensThisFrame = AssetsShellOnlyFrame ? AssetsInitialMetadataLayoutTokens"), std::string::npos);
	EXPECT_NE(Body.find("DrainAssetsCardMetadataHydrationRequests(AssetsMetadataLayoutTokensThisFrame"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabSwitchResetsListScrollToTop)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("s_AssetsResetListScrollOnTabSwitch = true;"), std::string::npos);
	EXPECT_NE(Body.find("if(s_AssetsResetListScrollOnTabSwitch)\n\t\t{\n\t\t\ts_ListBox.ResetScroll();"), std::string::npos);
	EXPECT_NE(Body.find("if(s_AssetsResetListScrollOnTabSwitch)\n\t\t\t{\n\t\t\t\ts_WorkshopAssetsListBox.ResetScroll();"), std::string::npos);
	EXPECT_LT(Body.find("s_AssetsResetListScrollOnTabSwitch = true;"), Body.find("s_WorkshopAssetsListBox.DoStart("));
}

TEST(QmMonitoringHelpers, AssetsStatusLabelsDistinguishLocalNetworkWorkshopDownloaded)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("auto ResolveLocalAssetStatusLabel = "), std::string::npos);
	EXPECT_NE(Source.find("pWorkshopAsset != nullptr ? Localize(\"Downloaded\") : Localize(\"Local\")"), std::string::npos);
	EXPECT_NE(Body.find("ResolveLocalAssetStatusLabel(pLocalItem, ShowLocalOnlyBadge)"), std::string::npos);
	EXPECT_NE(Body.find("ResolveLocalAssetStatusLabel(pItem, ShowLocalOnlyBadge)"), std::string::npos);
	EXPECT_NE(Body.find("Localize(Asset.m_Installed ? \"Downloaded\" : \"Network\")"), std::string::npos);
	EXPECT_EQ(Body.find("Asset.m_Installed ? \"Downloaded\" : \"Not downloaded\""), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsStatusTagKeepsMinimumReadableWidth)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const size_t LayoutStart = Source.find("auto LayoutAssetsCardShell = ");
	ASSERT_NE(LayoutStart, std::string::npos);
	const size_t LayoutEnd = Source.find("auto ComputeAssetPreviewContentSize = ", LayoutStart);
	ASSERT_NE(LayoutEnd, std::string::npos);
	const std::string LayoutBody = Source.substr(LayoutStart, LayoutEnd - LayoutStart);

	// Narrow cards must not squeeze the status tag into the delete icon area.
	// If the tag cannot keep a readable minimum width, it must not reserve a
	// header slot.
	EXPECT_NE(LayoutBody.find("AssetsCardStatusTagMinWidth"), std::string::npos);
	EXPECT_NE(LayoutBody.find("if(MaxWidth < DesiredMinWidth)"), std::string::npos);
	EXPECT_NE(LayoutBody.find("DesiredMinWidth"), std::string::npos);
	EXPECT_NE(LayoutBody.find("ReserveTrailingRect(TitleRect, ComputeBadgeWidth(pStatusLabel), AssetsCardStatusTagMinWidth"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsStatusTagColorsDistinguishReadyFromNetwork)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const size_t RenderStart = Source.find("auto RenderAssetStatusTag = ");
	ASSERT_NE(RenderStart, std::string::npos);
	const size_t RenderEnd = Source.find("auto ComputePreviewDrawRect = ", RenderStart);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = Source.substr(RenderStart, RenderEnd - RenderStart);

	// Ready-to-use local/downloaded assets stay green. Network-only assets use
	// the neutral label color so the availability state remains visible.
	EXPECT_NE(RenderBody.find("AssetsCardStatusReadyColor"), std::string::npos);
	EXPECT_NE(RenderBody.find("AssetsCardStatusNetworkColor"), std::string::npos);
	EXPECT_NE(RenderBody.find("Positive ? AssetsCardStatusReadyColor : AssetsCardStatusNetworkColor"), std::string::npos);
	EXPECT_NE(Source.find("RenderAssetsCardMetadataFallback(Ui(), Shell, pMetadata->m_Title.c_str(), pMetadata->m_Author.c_str(), pMetadata->m_StatusLabel.c_str(), pMetadata->m_Installed || pMetadata->m_LocalOnly"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsStatusTagsUseTightHorizontalPadding)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string FallbackBody = ExtractSourceFunctionBody(Source, "static void RenderAssetsCardMetadataFallback");
	const size_t RenderStart = Source.find("auto RenderAssetStatusTag = ");
	ASSERT_FALSE(FallbackBody.empty());
	ASSERT_NE(RenderStart, std::string::npos);
	const size_t RenderEnd = Source.find("auto ComputePreviewDrawRect = ", RenderStart);
	ASSERT_NE(RenderEnd, std::string::npos);
	const std::string RenderBody = Source.substr(RenderStart, RenderEnd - RenderStart);

	EXPECT_NE(Source.find("AssetsCardStatusTagHorizontalPadding"), std::string::npos);
	EXPECT_NE(FallbackBody.find("StatusLabelProps.m_MaxWidth = static_cast<int>(StatusRect.w - AssetsCardStatusTagHorizontalPadding * 2.0f);"), std::string::npos);
	EXPECT_NE(RenderBody.find("StatusLabelProps.m_MaxWidth = static_cast<int>(StatusRect.w - AssetsCardStatusTagHorizontalPadding * 2.0f);"), std::string::npos);
	EXPECT_EQ(FallbackBody.find("StatusRect.w - 2.0f"), std::string::npos);
	EXPECT_EQ(RenderBody.find("StatusRect.w - 2.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEntityBgFallbackTextIsCenteredTodo)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const size_t FallbackStart = Source.find("auto RenderEntityBgFallback = ");
	ASSERT_NE(FallbackStart, std::string::npos);
	const size_t FallbackEnd = Source.find("auto RenderEntityBgVideoFallback = ", FallbackStart);
	ASSERT_NE(FallbackEnd, std::string::npos);
	const std::string FallbackBody = Source.substr(FallbackStart, FallbackEnd - FallbackStart);

	EXPECT_NE(FallbackBody.find("Localize(\"Map Preview TODO\")"), std::string::npos);
	EXPECT_NE(FallbackBody.find("TEXTALIGN_MC"), std::string::npos);
	EXPECT_NE(FallbackBody.find("LabelRect = FallbackRect;"), std::string::npos);
	EXPECT_NE(FallbackBody.find("Ui()->DoLabel(&LabelRect"), std::string::npos);
	EXPECT_EQ(FallbackBody.find("DoMenuLabelStreamed"), std::string::npos);
	EXPECT_NE(Source.find("if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !PreviewReady)"), std::string::npos);
	EXPECT_NE(Source.find("if(s_CurCustomTab == ASSETS_TAB_ENTITY_BG && !PreviewReady && !PreviewState.m_DrawFolderIcon)"), std::string::npos);
	EXPECT_EQ(FallbackBody.find("Localize(\"Map Preview\")"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsPreviewFinalizeBudgetsAreNotHardCappedToOnePerFrame)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");

	// Ready previews/thumbs should appear as soon as each one finishes; the
	// pipeline must not serialize visible finalization to 1 item per frame.
	// Raising the steady-state budget is fine, but scroll-active frames still
	// keep a hard visible-stage cap through SettingsResourceFrameStageBudget(..., 0).
	EXPECT_NE(Source.find("constexpr int MaxPreviewDecodeFinalizesPerFrame = 16;"), std::string::npos);
	EXPECT_NE(Source.find("constexpr int MaxWorkshopThumbDecodeFinalizesPerFrame = 16;"), std::string::npos);
	EXPECT_EQ(Source.find("constexpr int MaxPreviewDecodeFinalizesPerFrame = 1;"), std::string::npos);
	EXPECT_EQ(Source.find("constexpr int MaxWorkshopThumbDecodeFinalizesPerFrame = 1;"), std::string::npos);
	EXPECT_NE(Source.find("SettingsResourceFrameStageBudget(FinalizeFrameContext, FinalizePriority, MaxPreviewDecodeFinalizesPerFrame, 0)"), std::string::npos);
	EXPECT_NE(Source.find("SettingsResourceFrameStageBudget(FinalizeFrameContext, FinalizePriority, MaxWorkshopThumbDecodeFinalizesThisFrame, 0)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardMetadataDoesNotHydrateWithoutBudget)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("FindAssetsCardMetadata("), std::string::npos);
	EXPECT_NE(Source.find("HydrateAssetsCardMetadata("), std::string::npos);
	EXPECT_EQ(Source.find("RefreshAssetsCardMetadata("), std::string::npos);
	EXPECT_EQ(Body.find("if(pMetadata == nullptr && CardHydrationScheduler.CanHydrateMetadata(CombinedVisible))"), std::string::npos);
	EXPECT_NE(Body.find("RequestAssetsCardMetadataHydration("), std::string::npos);
	EXPECT_EQ(Body.find("if(pMetadata == nullptr && RenderMetadata)"), std::string::npos);
	EXPECT_EQ(Body.find("if(pMetadata != nullptr && RenderMetadata)"), std::string::npos);
	EXPECT_EQ(Body.find("GetOrHydrateAssetsCardMetadata("), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel(&ErrorRect, Localize(\"Download failed\"), 9.0f, TEXTALIGN_MC);"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsCardInitialEntryUsesStableShellGeometry)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("AssetsCardShellShowsAuthorRow("), std::string::npos);
	EXPECT_NE(Source.find("AssetsCardListAreaWithStableScrollbar("), std::string::npos);
	EXPECT_NE(Source.find("RenderAssetsCardLoadingShells("), std::string::npos);
	EXPECT_NE(Body.find("const bool ShowAuthorRow = AssetsCardShellShowsAuthorRow(s_CurCustomTab, true"), std::string::npos);
	EXPECT_NE(Body.find("const bool ShowAuthorRow = AssetsCardShellShowsAuthorRow(s_CurCustomTab, false"), std::string::npos);
	EXPECT_NE(Body.find("AssetsCardListAreaWithStableScrollbar(WorkshopListArea, s_WorkshopAssetsListBox.ScrollbarWidthMax(), s_WorkshopAssetsListBox.ScrollbarMargin())"), std::string::npos);
	EXPECT_NE(Body.find("s_WorkshopAssetsListBox.DoStart(WorkshopRowHeight, CombinedCount, Columns, 1, OldCombinedSelected, &WorkshopListArea, false, IGraphics::CORNER_ALL);"), std::string::npos);
	EXPECT_NE(Body.find("RenderAssetsCardLoadingShells("), std::string::npos);
	EXPECT_NE(Body.find("const bool AssetsInitialEntryLoading = m_aAssetLoadStates[s_CurCustomTab] == ASSET_LOAD_STATE_LOADING || WorkshopState.m_pListTask != nullptr;"), std::string::npos);
	EXPECT_NE(Body.find("stage=assets_card_geometry"), std::string::npos);
	EXPECT_NE(Body.find("geometry_changed=%d"), std::string::npos);
	EXPECT_LT(Body.find("if(AssetsInitialEntryLoading)"), Body.find("Ui()->DoLabel(&WorkshopListArea, Localize(\"No assets\"), 12.0f, TEXTALIGN_MC);"));
	EXPECT_EQ(Source.find("ShouldShowAssetCardAuthorRow(!Asset.m_Author.empty(), false)"), std::string::npos);
	EXPECT_EQ(Source.find("ShowAuthorRow = !Asset.m_Author.empty()"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoBrowserUsesAdaptiveMetadataBudget)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Header.find("SSettingsAdaptiveBudgetState m_DemoBrowserAdaptiveBudgetState"), std::string::npos);
	EXPECT_NE(Body.find("BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::DemoBrowser, \"demo_browser\""), std::string::npos);
	EXPECT_NE(Body.find("AdaptiveBudget.m_DemoMetadataTokens"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/components/menus.cpp").find("event=settings_adaptive_budget"), std::string::npos);
	EXPECT_EQ(Body.find("AdvanceDemoBrowserMetadata(g_Config.m_BrDemoFetchInfo && !BrowsingScreenshots ? 2 : 0, g_Config.m_BrDemoSort == SORT_DATE ? 4 : 0"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsTextPrebuildUsesAdaptiveBudget)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string PrebuildBody = ExtractSourceFunctionBody(Menus, "int CMenus::PrebuildSettingsTextPoolForLoading(int Budget, const char *pOperationOverride)");
	const std::string EscBody = ExtractSourceFunctionBody(Menus, "void CMenus::PrebuildIngameEscTextPoolBeforeOpen(int Budget)");
	ASSERT_FALSE(PrebuildBody.empty());
	ASSERT_FALSE(EscBody.empty());

	EXPECT_EQ(Header.find("SSettingsAdaptiveBudgetState m_SettingsTextAdaptiveBudgetState"), std::string::npos);
	EXPECT_NE(PrebuildBody.find("BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, \"stable_text_prebuild\""), std::string::npos);
	EXPECT_NE(PrebuildBody.find("AdaptiveBudget.m_TextPrebuildTokens"), std::string::npos);
	EXPECT_NE(Menus.find("event=settings_adaptive_budget"), std::string::npos);
	EXPECT_NE(EscBody.find("BeginSettingsUiFrameScheduler(EFrameSchedulerConsumer::SettingsText, \"stable_text_ingame_esc\""), std::string::npos);
	EXPECT_EQ(EscBody.find("PrebuildSettingsMenuTextPool(minimum(Budget, 4), \"target_settings\", \"ingame_esc_open\");"), std::string::npos);
}

TEST(QmMonitoringHelpers, ReportStableTextSamplesDoNotWrapKeyVertically)
{
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Tests = ReadRepoFile("qmclient_scripts/perf/test.ts");

	EXPECT_NE(Report.find("sample-key-cell"), std::string::npos);
	EXPECT_NE(Report.find("white-space:nowrap"), std::string::npos);
	EXPECT_NE(Report.find("table-layout:fixed"), std::string::npos);
	EXPECT_NE(Report.find("truncateMiddle(sampleField(sample, 'key')"), std::string::npos);
	EXPECT_NE(Tests.find("must-not-wrap-vertically"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsStableTextTargetAcceptanceRequiresFullCoverage)
{
	const std::string Stats = ReadRepoFile("qmclient_scripts/perf/lib/stats.ts");
	const std::string Report = ReadRepoFile("qmclient_scripts/perf/lib/report.ts");
	const std::string Quality = ReadRepoFile("qmclient_scripts/perf/lib/quality.ts");

	EXPECT_NE(Stats.find("acceptanceBlocked: missCount > 0 || staleCount > 0 || prebuildRemainingBeforeTarget > 0 || !planCollectionAvailable || !planCollectionComplete || !utilizationAvailable || !planCoverageAvailable || unplannedVisibleCount > 0 || keyMismatchCount > 0 || textNew > 0"), std::string::npos);
	EXPECT_NE(Stats.find("unplanned visible stable text candidates"), std::string::npos);
	EXPECT_NE(Stats.find("stable text key mismatches"), std::string::npos);
	EXPECT_NE(Stats.find("settings_text_plan_collection"), std::string::npos);
	EXPECT_NE(Stats.find("planCollectionRemainingBeforeTarget"), std::string::npos);
	EXPECT_NE(Stats.find("!planCollectionAvailable || !planCollectionComplete"), std::string::npos);
	EXPECT_NE(Report.find("collection remaining=0 只表示计划收集完成"), std::string::npos);
	EXPECT_NE(Report.find("Plan Collection"), std::string::npos);
	EXPECT_NE(Report.find("Container Remaining"), std::string::npos);
	EXPECT_NE(Report.find("Visible Coverage"), std::string::npos);
	EXPECT_NE(Report.find("Assets Visible-First Admission"), std::string::npos);
	EXPECT_NE(Stats.find("assetsVisibleReadySummary"), std::string::npos);
	EXPECT_NE(Stats.find("thumbStartsDuringDraw > 0"), std::string::npos);
	EXPECT_NE(Stats.find("geometryStable === false"), std::string::npos);
	EXPECT_NE(Report.find("Visible Ready"), std::string::npos);
	EXPECT_NE(Quality.find("stable text coverage blocked settings acceptance"), std::string::npos);
	EXPECT_NE(Quality.find("assets visible-ready preflight missing"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsTextMissLogsAreSampledPerFrameBucket)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Menus, "void LogSettingsTextPoolCoverageGap(IClient *pClient, const char *pEvent, CMenus::EMenuTextScope Scope, const char *pScopeName, int Page, int Tab, int Subtab, const char *pKey, const char *pReason, const char *pPlanStatus, const char *pOperation, uint64_t Frame)");
	ASSERT_FALSE(Body.empty());

	// This guard keeps target coverage useful without returning to multi-GB
	// logs when thousands of visible labels share the same key-mismatch cause.
	EXPECT_NE(Body.find("MaxGapSamplesPerFrameBucket"), std::string::npos);
	EXPECT_NE(Body.find("s_SamplesThisBucket"), std::string::npos);
	EXPECT_NE(Body.find("log_sample_limit"), std::string::npos);
}

TEST(QmMonitoringHelpers, DefaultGateRunsFullAutomatedTests)
{
	const std::string Gate = ReadRepoFile("qmclient_scripts/gate/check_gate.py");
	const std::string ScriptsOverview = ReadRepoFile("qmclient_scripts/scripts_overview.md");
	ASSERT_FALSE(Gate.empty());

	const size_t DefaultMode = Gate.find("\"default\": {");
	ASSERT_NE(DefaultMode, std::string::npos);
	const size_t FullMode = Gate.find("\"full\": {", DefaultMode);
	ASSERT_NE(FullMode, std::string::npos);
	const std::string DefaultSpec = Gate.substr(DefaultMode, FullMode - DefaultMode);

	// The default gate is the normal pre-submit gate, so it must run the full
	// automated test set. Full mode is reserved for extra heavyweight/noisy
	// checks, not for merely getting Rust tests.
	EXPECT_NE(DefaultSpec.find("\"tests\": {\"cxx\": True, \"rust\": True, \"all\": False}"), std::string::npos);
	EXPECT_EQ(DefaultSpec.find("\"strict_build\""), std::string::npos);
	EXPECT_EQ(DefaultSpec.find("\"dilate\""), std::string::npos);
	EXPECT_NE(DefaultSpec.find("C++ 全量测试和 Rust 全量测试"), std::string::npos);
	EXPECT_NE(Gate.substr(FullMode).find("\"strict_build\""), std::string::npos);
	EXPECT_NE(Gate.substr(FullMode).find("\"dilate\""), std::string::npos);
	EXPECT_NE(ScriptsOverview.find("C++ 全量测试和 Rust 全量测试"), std::string::npos);
	EXPECT_NE(ScriptsOverview.find("严格构建与静态分析只属于 full gate"), std::string::npos);
	EXPECT_NE(ScriptsOverview.find("不作为“全量测试”的默认入口"), std::string::npos);
}

TEST(QmMonitoringHelpers, WindowsCmakeWrapperDoesNotPreconfigureBeforeBuild)
{
	const std::string Wrapper = ReadRepoFile("qmclient_scripts/cmake-windows.cmd");
	const std::string Repair = ReadRepoFile("qmclient_scripts/repair_ninja_msvc_prefix.py");
	ASSERT_FALSE(Wrapper.empty());
	ASSERT_FALSE(Repair.empty());

	EXPECT_EQ(Wrapper.find("--prepare-build"), std::string::npos);
	EXPECT_NE(Wrapper.find("if not \"%CMRC%\"==\"0\""), std::string::npos);
	EXPECT_NE(Wrapper.find("type \"%CMOUT%\""), std::string::npos);
	EXPECT_FALSE(ContainsAny(Repair, {
						 "--prepare-build",
						 "def _prepare_build_rules",
						 "subprocess.run(\n        [\"cmake\", \"-S\"",
					 }));
}

TEST(QmMonitoringHelpers, NightlyWorkflowPublishesPdbFreePrerelease)
{
	const std::string Nightly = ReadRepoFile(".github/workflows/nightly.yml");
	ASSERT_FALSE(Nightly.empty());

	EXPECT_NE(Nightly.find("workflow_dispatch:"), std::string::npos);
	EXPECT_NE(Nightly.find("schedule:"), std::string::npos);
	EXPECT_NE(Nightly.find("Check Nightly Changes"), std::string::npos);
	EXPECT_NE(Nightly.find("git ls-remote --tags origin refs/tags/nightly"), std::string::npos);
	EXPECT_NE(Nightly.find("should_build=false"), std::string::npos);
	EXPECT_NE(Nightly.find("github.event_name"), std::string::npos);
	EXPECT_NE(Nightly.find("needs.changes.outputs.should_build == 'true'"), std::string::npos);
	EXPECT_NE(Nightly.find("cmake --build build --target package_default --parallel"), std::string::npos);
	EXPECT_NE(Nightly.find("QmClient-windows.zip"), std::string::npos);
	EXPECT_NE(Nightly.find("QmClient-windows.7z"), std::string::npos);
	EXPECT_NE(Nightly.find("QmClient-ubuntu.tar.xz"), std::string::npos);
	EXPECT_NE(Nightly.find("QmClient-macOS.dmg"), std::string::npos);
	EXPECT_NE(Nightly.find("PDB files must not be published"), std::string::npos);
	EXPECT_NE(Nightly.find("\\.pdb$"), std::string::npos);
	EXPECT_NE(Nightly.find("gh release delete nightly --cleanup-tag --yes || true"), std::string::npos);
	EXPECT_NE(Nightly.find("gh release create nightly"), std::string::npos);
	EXPECT_NE(Nightly.find("--prerelease"), std::string::npos);
	EXPECT_NE(Nightly.find("contents: write"), std::string::npos);
	EXPECT_EQ(Nightly.find("startsWith(github.ref, 'refs/tags/')"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsScrollRegionHelperExists)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string ScrollRegionHeader = ReadRepoFile("src/game/client/ui_scrollregion.h");
	const std::string ScrollRegion = ReadRepoFile("src/game/client/ui_scrollregion.cpp");
	const std::string UiHeader = ReadRepoFile("src/game/client/ui.h");

	EXPECT_NE(Header.find("struct SSettingsScrollRegionFrame"), std::string::npos);
	EXPECT_NE(Header.find("BeginSettingsScrollRegion(CScrollRegion &ScrollRegion"), std::string::npos);
	EXPECT_NE(Header.find("FinishSettingsScrollRegion(CScrollRegion &ScrollRegion"), std::string::npos);
	EXPECT_NE(Header.find("const CUIRect *pEndRect = nullptr"), std::string::npos);
	EXPECT_NE(Menus.find("CMenus::SSettingsScrollRegionFrame CMenus::BeginSettingsScrollRegion(CScrollRegion &ScrollRegion"), std::string::npos);
	EXPECT_NE(Menus.find("void CMenus::FinishSettingsScrollRegion(CScrollRegion &ScrollRegion"), std::string::npos);
	EXPECT_NE(Menus.find("if(pEndRect != nullptr)\n\t\tScrollRegion.AddRect(*pEndRect);"), std::string::npos);
	EXPECT_NE(Menus.find("Frame.m_FinalOffsetY = ScrollRegion.ScrollbarShown() ? ScrollRegion.ContentScrollOffsetY() : 0.0f;"), std::string::npos);
	EXPECT_NE(Menus.find("m_SettingsScrollActive = m_SettingsScrollActive ||"), std::string::npos);
	EXPECT_NE(Menus.find("m_SettingsRuntimeMetadata.m_LastScrollY = Frame.m_FinalOffsetY;"), std::string::npos);
	EXPECT_NE(ScrollRegionHeader.find("float ContentScrollOffsetY() const { return m_Params.m_ScrollHorizontal ? 0.0f : -m_ScrollState.Offset(); }"), std::string::npos);
	const std::string ScrollRegionEnd = ExtractSourceFunctionBody(ScrollRegion, "void CScrollRegion::End()");
	const std::string NoScrollBody = ExtractSourceFunctionBody(ScrollRegion, "void CScrollRegion::MaintainNoScrollSliderActive()");
	const std::string ScrollRegionSlider = ExtractSourceFunctionBody(ScrollRegion, "void CScrollRegion::DoSlider()");
	ASSERT_FALSE(ScrollRegionEnd.empty());
	ASSERT_FALSE(NoScrollBody.empty());
	ASSERT_FALSE(ScrollRegionSlider.empty());
	EXPECT_NE(ScrollRegion.find("bool CScrollRegion::ContentOverflows() const"), std::string::npos);
	EXPECT_NE(ScrollRegionHeader.find("constexpr bool QmScrollRegionContentOverflows(float ContentSize, float ViewportSize, float PixelSize)"), std::string::npos);
	EXPECT_NE(ScrollRegion.find("return !m_Params.m_HideScrollbar && ContentOverflows();"), std::string::npos);
	EXPECT_NE(ScrollRegion.find("m_ContentSize 来自上一帧 End/AddRect 的测量结果"), std::string::npos);
	EXPECT_NE(ScrollRegionSlider.find("const float ScrollMax = Metrics.MaxOffset();"), std::string::npos);
	EXPECT_NE(ScrollRegionSlider.find("const bool CanScroll = ContentOverflows() && ScrollMax > 0.0f && RailSize > 0.0f;"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool IsActiveItem(const void *pId) const"), std::string::npos);
	EXPECT_NE(ScrollRegionEnd.find("MaintainNoScrollSliderActive();"), std::string::npos);
	EXPECT_NE(NoScrollBody.find("m_ScrollState.ResetForNonScrollableContent(Active);"), std::string::npos);
	EXPECT_EQ(NoScrollBody.find("m_ScrollState.EndThumbDrag();"), std::string::npos);
	EXPECT_NE(NoScrollBody.find("Ui()->IsActiveItem(pId)"), std::string::npos);
	EXPECT_NE(NoScrollBody.find("ScrollRegionShouldKeepNoScrollSliderActive(WasActive, Ui()->MouseButton(0))"), std::string::npos);
	EXPECT_NE(NoScrollBody.find("Ui()->SetActiveItem(pId);"), std::string::npos);
	EXPECT_NE(NoScrollBody.find("Ui()->SetActiveItem(nullptr);"), std::string::npos);
	EXPECT_NE(ScrollRegionSlider.find("MaintainNoScrollSliderActive();"), std::string::npos);
}

TEST(QmMonitoringHelpers, ScrollRegionNoScrollSliderReleasesActiveAfterMouseUp)
{
	EXPECT_FALSE(ScrollRegionShouldKeepNoScrollSliderActive(false, false));
	EXPECT_FALSE(ScrollRegionShouldKeepNoScrollSliderActive(false, true));
	EXPECT_FALSE(ScrollRegionShouldKeepNoScrollSliderActive(true, false));
	EXPECT_TRUE(ScrollRegionShouldKeepNoScrollSliderActive(true, true));
}

TEST(QmMonitoringHelpers, GlobalCardOrderConfigHasMigrationLandingZone)
{
	const std::string QmConfig = ReadRepoFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ConfigManager = ReadRepoFile("src/engine/shared/config.cpp");

	EXPECT_NE(QmConfig.find("MACRO_CONFIG_STR(QmGlobalCardOrder, qm_global_card_order, 8000, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmConfig.find("MACRO_CONFIG_INT(QmCardOrderMigrated, qm_card_order_migrated, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmConfig.find("MACRO_CONFIG_STR(QmSettingsCardOrder, qm_settings_card_order, 2048, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(QmConfig.find("MACRO_CONFIG_STR(QmSidebarCardOrder, qm_sidebar_card_order, 2048, \"\", CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_EQ(QmConfig.find("qm_global_card_order, 2048"), std::string::npos);
	EXPECT_NE(ConfigManager.find("std::vector<char> vLineBuf(pVariable->MaxSerializedSize());"), std::string::npos);
}

TEST(QmMonitoringHelpers, CardOrderModelUsesStableIdIndexForFind)
{
	const std::string Model = ReadRepoFile("src/game/client/QmUi/QmCardOrderModel.cpp");
	const std::string FindBody = ExtractSourceFunctionBody(Model, "int CModel::FindByStableId(const char *pStableId) const");
	ASSERT_FALSE(FindBody.empty());

	EXPECT_NE(FindBody.find("return StateIndexForStableId(pStableId);"), std::string::npos);
	EXPECT_EQ(FindBody.find("for(size_t i = 0; i < m_vEntries.size(); ++i)"), std::string::npos);
	EXPECT_NE(Model.find("BuildStateIndex(); // 维护 stableId→index 注册表"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsCardOrderPersistenceCommitsGlobalConfigAtomically)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string LoadBody = ExtractSourceFunctionBody(Menus, "void CMenus::LoadSettingsCardOrderModel()");
	const std::string SaveBody = ExtractSourceFunctionBody(Menus, "bool CMenus::SaveSettingsCardOrderModel()");
	ASSERT_FALSE(LoadBody.empty());
	ASSERT_FALSE(SaveBody.empty());

	const char *pTempBuffer = "char aSerialized[sizeof(g_Config.m_QmGlobalCardOrder)]";
	const char *pSerialize = "m_SettingsCardOrderModel.Serialize(aSerialized, sizeof(aSerialized))";
	const char *pCopy = "str_copy(g_Config.m_QmGlobalCardOrder, aSerialized, sizeof(g_Config.m_QmGlobalCardOrder));";
	const size_t CandidateTempPos = LoadBody.find(pTempBuffer);
	const size_t CandidateSerializePos = LoadBody.find("Candidate.Serialize(aSerialized, sizeof(aSerialized))", CandidateTempPos);
	const size_t CandidateCopyPos = LoadBody.find(pCopy, CandidateSerializePos);
	const size_t CandidateCommitPos = LoadBody.find("m_SettingsCardOrderModel.SetEntries(CopyModelEntries(Candidate));", CandidateCopyPos);
	const size_t CandidateDirtyClearPos = LoadBody.find("m_SettingsCardOrderModel.ClearDirty();", CandidateCommitPos);
	const size_t LoadTempPos = LoadBody.find(pTempBuffer, CandidateDirtyClearPos);
	const size_t LoadSerializePos = LoadBody.find(pSerialize, LoadTempPos);
	const size_t LoadCopyPos = LoadBody.find(pCopy, LoadSerializePos);
	const size_t LoadDirtyClearPos = LoadBody.find("m_SettingsCardOrderModel.ClearDirty();", LoadCopyPos);
	const size_t LoadMigratedPos = LoadBody.find("g_Config.m_QmCardOrderMigrated = 1;");
	EXPECT_NE(CandidateTempPos, std::string::npos);
	EXPECT_NE(CandidateSerializePos, std::string::npos);
	EXPECT_NE(CandidateCopyPos, std::string::npos);
	EXPECT_NE(CandidateCommitPos, std::string::npos);
	EXPECT_NE(CandidateDirtyClearPos, std::string::npos);
	EXPECT_LT(CandidateTempPos, CandidateSerializePos);
	EXPECT_LT(CandidateSerializePos, CandidateCopyPos);
	EXPECT_LT(CandidateCopyPos, CandidateCommitPos);
	EXPECT_LT(CandidateCommitPos, CandidateDirtyClearPos);
	EXPECT_NE(LoadTempPos, std::string::npos);
	EXPECT_NE(LoadSerializePos, std::string::npos);
	EXPECT_NE(LoadCopyPos, std::string::npos);
	EXPECT_NE(LoadDirtyClearPos, std::string::npos);
	EXPECT_NE(LoadMigratedPos, std::string::npos);
	EXPECT_LT(LoadTempPos, LoadSerializePos);
	EXPECT_LT(LoadSerializePos, LoadCopyPos);
	EXPECT_LT(LoadCopyPos, LoadDirtyClearPos);
	EXPECT_LT(LoadDirtyClearPos, LoadMigratedPos);

	const size_t SaveTempPos = SaveBody.find(pTempBuffer);
	const size_t SaveSerializePos = SaveBody.find(pSerialize);
	const size_t SaveCopyPos = SaveBody.find(pCopy);
	const size_t SaveDirtyClearPos = SaveBody.find("m_SettingsCardOrderModel.ClearDirty();");
	EXPECT_NE(SaveTempPos, std::string::npos);
	EXPECT_NE(SaveSerializePos, std::string::npos);
	EXPECT_NE(SaveCopyPos, std::string::npos);
	EXPECT_NE(SaveDirtyClearPos, std::string::npos);
	EXPECT_LT(SaveTempPos, SaveSerializePos);
	EXPECT_LT(SaveSerializePos, SaveCopyPos);
	EXPECT_LT(SaveCopyPos, SaveDirtyClearPos);
}

TEST(QmMonitoringHelpers, SettingsCardLayoutVersionMigrationRequiresWholeLegacyGroups)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string LoadBody = ExtractSourceFunctionBody(Menus, "void CMenus::LoadSettingsCardOrderModel()");
	ASSERT_FALSE(LoadBody.empty());

	EXPECT_NE(LoadBody.find("const bool ContributorsStillOldDefault"), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:qmclient-contributors-community\""), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:qmclient-contributors-sponsors\""), std::string::npos);
	EXPECT_NE(LoadBody.find("const bool BindWheelStillOldDefault"), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:tclient-bind-wheel-editor\""), std::string::npos);
	EXPECT_NE(LoadBody.find("IsAtOldDefault(\"deck:tclient-bind-wheel-preview\""), std::string::npos);
	EXPECT_NE(LoadBody.find("MakeCandidate(Candidate);"), std::string::npos);
	EXPECT_NE(LoadBody.find("TabContainsOnlyStableIds(Candidate, \"qmclient-contributors\""), std::string::npos);
	EXPECT_NE(LoadBody.find("TabContainsOnlyStableIds(Candidate, \"tclient-bind-wheel\""), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateExactLayout(Candidate, \"tclient-profiles\", vLegacyProfileLayout, vTargetProfileLayout, vProfileIds)"), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateExactLayout(Candidate, \"tclient-status-bar\", vLegacyStatusBarDefaults, vTargetStatusBarLayout, vStatusBarIds)"), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateExactLayout(Candidate, \"tee\", vLegacyTeeDefaults, vTargetTeeLayout, vTeeIds)"), std::string::npos);
	EXPECT_EQ(LoadBody.find("MoveIfStillAtOldDefault"), std::string::npos);
	EXPECT_NE(LoadBody.find("MigrateTClientMainCardsToAlternatingColumns"), std::string::npos);
	EXPECT_NE(LoadBody.find("TClientMainCardsMigrationCommitPlan(MigrationResult)"), std::string::npos);
	EXPECT_NE(LoadBody.find("PersistCandidate(Candidate, CandidateChanged)"), std::string::npos);
	EXPECT_NE(LoadBody.find("g_Config.m_QmCardLayoutVersion = 1;"), std::string::npos);
	EXPECT_NE(LoadBody.find("PersistAndAdvanceLayoutVersion(5, true)"), std::string::npos);
	EXPECT_NE(LoadBody.find("if(CommitPlan.m_PersistSerialized)"), std::string::npos);
	EXPECT_NE(LoadBody.find("if(CommitPlan.m_AdvanceVersion)"), std::string::npos);
	EXPECT_NE(LoadBody.find("PersistAndAdvanceLayoutVersion(7, true)"), std::string::npos);
}
TEST(QmMonitoringHelpers, GlobalSearchUsesDedicatedSettingsPage)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string RuntimeCache = ReadRepoFile("src/game/client/components/settings_runtime_cache.cpp");
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string QmWrapperBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsQmClient(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	const std::string QmMainBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	const std::string SearchBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsGlobalSearch(CUIRect MainView, bool PrewarmOnly)");
	const std::string SearchContentBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)");
	const std::string SharedBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	ASSERT_FALSE(QmWrapperBody.empty());
	ASSERT_FALSE(QmMainBody.empty());
	ASSERT_FALSE(SearchBody.empty());
	ASSERT_FALSE(SearchContentBody.empty());
	ASSERT_FALSE(SharedBody.empty());

	EXPECT_NE(Header.find("SETTINGS_SEARCH"), std::string::npos);
	EXPECT_EQ(Header.find("QMCLIENT_SETTINGS_TAB_SEARCH"), std::string::npos);
	EXPECT_EQ(Header.find("CLineInputBuffered<128> m_QmClientModuleSearchInput;"), std::string::npos);
	EXPECT_NE(Header.find("CLineInputBuffered<128> m_GlobalCardSearchInput;"), std::string::npos);
	EXPECT_EQ(Header.find("m_aQmClientModuleSearchInputs"), std::string::npos);
	EXPECT_NE(Settings.find("case CMenus::SETTINGS_SEARCH: return \"search\";"), std::string::npos);
	EXPECT_NE(Menus.find("m_apSettingsTabs[SETTINGS_SEARCH] = Localize(\"Search\");"), std::string::npos);
	EXPECT_NE(Settings.find("SETTINGS_SEARCH,"), std::string::npos);
	EXPECT_NE(RuntimeCache.find("case CMenus::SETTINGS_SEARCH: return \"search\";"), std::string::npos);
	EXPECT_NE(Settings.find("RenderSettingsGlobalSearch(ContentView, CollectingMenuTextPlan);"), std::string::npos);
	EXPECT_EQ(QmClient.find("case CMenus::QMCLIENT_SETTINGS_TAB_SEARCH: return \"search\";"), std::string::npos);
	EXPECT_EQ(QmClient.find("s_apQmTabNames[QMCLIENT_SETTINGS_TAB_SEARCH] = Localize(\"Search\");"), std::string::npos);
	EXPECT_NE(Header.find("RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly = false);"), std::string::npos);
	EXPECT_NE(Header.find("RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly);"), std::string::npos);
	EXPECT_NE(QmWrapperBody.find("RenderSettingsQmClientContent(MainView, ContributorsPage, PrewarmOnly);"), std::string::npos);
	EXPECT_NE(SearchBody.find("RenderSettingsGlobalSearchContent(MainView, PrewarmOnly);"), std::string::npos);
	EXPECT_EQ(SearchBody.find("RenderSettingsQmClientContent("), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SETTINGS_SEARCH"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SearchTabActive"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("CLineInputBuffered<128> &ModuleSearchInput = m_GlobalCardSearchInput;"), std::string::npos);
	EXPECT_EQ(SearchContentBody.find("m_QmClientModuleSearchInput"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("IUiContext SearchCtx = SettingsUiContext(\"settings_global_search\", UiScale);"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("ui_widget::InputField(InputCtx, &m_GlobalCardSearchInput, Row, BodySize"), std::string::npos);
	EXPECT_EQ(SearchContentBody.find("Ui()->DoEditBox_Search(&ModuleSearchInput"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("InputCard.m_Spec = {\"deck:global-search-input\""), std::string::npos);
	EXPECT_NE(SearchContentBody.find("ResultsCard.m_Spec = {\"deck:global-search-results\""), std::string::npos);
	EXPECT_NE(SearchContentBody.find("DoSettingsMenuLabel(SETTINGS_SEARCH, -1, -1, \"qmclient-search-no-matching-features\""), std::string::npos);
	EXPECT_EQ(SearchContentBody.find("DoSettingsMenuLabel(SETTINGS_QMCLIENT"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("CollectGlobalSearchResults(pModuleSearch, s_GlobalSearchCache.m_Sixup, CardOrderModel, s_GlobalSearchCache.m_Results);"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("CardDeck.RenderCached(SearchCtx, Page, \"global-search\""), std::string::npos);
	EXPECT_NE(SearchContentBody.find("static CScrollRegion s_GlobalSearchScrollRegion;"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("s_GlobalSearchPrewarmOrderModel.LoadMerged(g_Config.m_QmGlobalCardOrder, qm_card_registry::BuildDefaultEntries());"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("ReadOnly ? s_GlobalSearchPrewarmOrderModel : SettingsCardOrderModel();"), std::string::npos);
	const size_t ReadOnlyOrderSelection = SearchContentBody.find("qm_card_order::CModel &CardOrderModel = ReadOnly ?");
	const size_t SearchLayoutRevision = SearchContentBody.find("const uint64_t LayoutRevision = CardOrderModel.LayoutRevision();");
	ASSERT_NE(ReadOnlyOrderSelection, std::string::npos);
	ASSERT_NE(SearchLayoutRevision, std::string::npos);
	EXPECT_LT(ReadOnlyOrderSelection, SearchLayoutRevision);
	EXPECT_EQ(SearchContentBody.find("SettingsCardOrderModel().LayoutRevision()"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("static CSettingsCardDeck s_GlobalSearchPrewarmDeck;"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("ReadOnly ? s_GlobalSearchPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("if(ReadOnly)\n\t{\n\t\tSearchCtx.m_pAnim = nullptr;"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("InputState.m_pScrollParams = ReadOnly ? nullptr : &ScrollParams;"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("ReadOnly ? nullptr : &s_GlobalSearchScrollRegion"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("const bool Clicked = !ReadOnly && Ui()->DoButtonLogic"), std::string::npos);
	EXPECT_EQ(SearchContentBody.find("BeginSettingsQmScrollContainer"), std::string::npos);
	EXPECT_EQ(SearchContentBody.find("RenderQmSettingsGlassCard"), std::string::npos);
	EXPECT_EQ(SharedBody.find("GlobalSearchPage"), std::string::npos);
	EXPECT_EQ(SharedBody.find("SearchTabActive"), std::string::npos);
	EXPECT_EQ(SharedBody.find("ShowSearchModuleControls"), std::string::npos);
	EXPECT_EQ(SearchContentBody.find("const std::vector<const SQmGlobalSearchCard *> &SearchVisibleGlobalCards = GlobalSearchResults.m_vVisibleCards;"), std::string::npos);
	EXPECT_EQ(SharedBody.find("SearchVisibleGlobalCards"), std::string::npos);
	EXPECT_EQ(SharedBody.find("SearchVisibleExternalCards"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;"), std::string::npos);
	EXPECT_NE(SearchContentBody.find("Localize(\"Found %d global cards\")"), std::string::npos);
	EXPECT_EQ(SharedBody.find("g_Config.m_UiSettingsPage == SETTINGS_SEARCH"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("m_aQmClientModuleSearchInputs["), std::string::npos);
	EXPECT_EQ(QmMainBody.find("m_GlobalCardSearchInput"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientSearchTabUsesGlobalCardRegistry)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string SharedBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(SharedBody.empty());

	EXPECT_NE(QmClient.find("#include <game/client/QmUi/QmCardRegistry.h>"), std::string::npos);
	EXPECT_NE(QmClient.find("using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;"), std::string::npos);
	EXPECT_NE(QmClient.find("struct SQmGlobalSearchNavigation"), std::string::npos);
	EXPECT_EQ(QmClient.find("BuildGlobalSearchCards("), std::string::npos);
	EXPECT_NE(QmClient.find("qm_card_registry::SearchCards(pSearch, Model)"), std::string::npos);
	EXPECT_NE(QmClient.find("CollectGlobalSearchResults("), std::string::npos);
	EXPECT_NE(QmClient.find("ResolveGlobalSearchNavigation("), std::string::npos);
	EXPECT_NE(QmClient.find("Card.m_Target = qm_card_registry::ResolveCardNavigationTarget(Default, Model);"), std::string::npos);
	EXPECT_NE(SharedBody.find("struct SGlobalSearchCache"), std::string::npos);
	EXPECT_NE(SharedBody.find("CollectGlobalSearchResults(pModuleSearch, s_GlobalSearchCache.m_Sixup, CardOrderModel, s_GlobalSearchCache.m_Results);"), std::string::npos);
	EXPECT_NE(SharedBody.find("s_GlobalSearchCache.m_Results.m_vAllVisibleCards"), std::string::npos);
	EXPECT_NE(QmClient.find("for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())"), std::string::npos);
	EXPECT_EQ(SharedBody.find("qm_card_registry::Defaults()"), std::string::npos);
	EXPECT_EQ(SharedBody.find("EQmModuleId::Info"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientMainPageRemovesLegacyModuleSearchPipeline)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string QmMainBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsQmClientContent(CUIRect MainView, bool ContributorsPage, bool PrewarmOnly)");
	ASSERT_FALSE(QmMainBody.empty());

	EXPECT_EQ(QmMainBody.find("HasModuleSearch"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("pModuleSearch"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("ModuleSearchKeywords"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("ModuleMatchesSearch"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("CQmFunctionSnapshotJob"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SQmFunctionSnapshot"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SearchVisibleModules"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SearchLeftModules"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SearchRightModules"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SearchSingleColumnMode"), std::string::npos);
	EXPECT_EQ(QmMainBody.find("SearchDragBlocked"), std::string::npos);
}

TEST(QmMonitoringHelpers, P6GlobalSearchUsesPublicDeckOnly)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string SharedBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(SharedBody.empty());

	EXPECT_EQ(QmClient.find("std::vector<SQmGlobalSearchCard> m_vCards;"), std::string::npos);
	EXPECT_NE(QmClient.find("using SQmGlobalSearchCard = qm_card_registry::SCardSearchResult;"), std::string::npos);
	EXPECT_NE(QmClient.find("std::vector<SQmGlobalSearchCard> m_vAllVisibleCards;"), std::string::npos);
	EXPECT_NE(QmClient.find("vCards = qm_card_registry::SearchCards(pSearch, Model);"), std::string::npos);
	EXPECT_NE(QmClient.find("Card.m_Target = qm_card_registry::ResolveCardNavigationTarget(Default, Model);"), std::string::npos);
	EXPECT_NE(SharedBody.find("const std::vector<SQmGlobalSearchCard> &SearchVisibleGlobalCards = s_GlobalSearchCache.m_Results.m_vAllVisibleCards;"), std::string::npos);
	EXPECT_NE(QmClient.find("Out.m_vAllVisibleCards.push_back(std::move(Card));"), std::string::npos);
	EXPECT_NE(SharedBody.find("ResultsCard.m_MeasureRevision = SearchMatchedGlobalCardCount;"), std::string::npos);
	EXPECT_NE(SharedBody.find("ResolveGlobalSearchNavigation(Card)"), std::string::npos);
	EXPECT_NE(SharedBody.find("Ui()->DoButtonLogic(Card.m_pStableId, 0, &ResultRect, BUTTONFLAG_LEFT)"), std::string::npos);
	EXPECT_NE(SharedBody.find("GlobalSearchNavigationLabel(Navigation)"), std::string::npos);
	EXPECT_NE(SharedBody.find("NavigateToSettingsCard(Card.m_Target);"), std::string::npos);
	EXPECT_NE(QmClient.find("CollectGlobalSearchResults(const char *pSearch, bool Sixup, const qm_card_order::CModel &Model"), std::string::npos);
	EXPECT_NE(QmClient.find("Sixup && str_comp(pTab, \"tee\") == 0"), std::string::npos);
	EXPECT_NE(QmClient.find("!Sixup && str_comp(pTab, \"tee7\") == 0"), std::string::npos);
	EXPECT_EQ(QmClient.find("void CMenus::RenderGlobalSearchResultCard("), std::string::npos);
	EXPECT_EQ(QmClient.find("void CMenus::RenderGlobalSearchResults("), std::string::npos);
	EXPECT_EQ(QmClient.find("s_GlobalSearchCardButtonIndex"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientSearchNavigationTargetsSettingsPages)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const size_t NavigationPos = QmClient.find("SQmGlobalSearchNavigation ResolveGlobalSearchNavigation");
	ASSERT_NE(NavigationPos, std::string::npos);
	const std::string NavigationBody = QmClient.substr(NavigationPos, 4200);
	const std::string CardBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(CardBody.empty());

	EXPECT_NE(NavigationBody.find("str_startswith(pStableId, \"qm:\")"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_SettingsPage = CMenus::SETTINGS_QMCLIENT;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_QmClientTab = CMenus::QMCLIENT_SETTINGS_TAB_FUNCTION;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_QmClientTab = CMenus::QMCLIENT_SETTINGS_TAB_HUD;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_SettingsPage = CMenus::SETTINGS_TCLIENT;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_TClientTab = 0;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_SettingsPage = Route.m_SettingsPage;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_TClientTab = Route.m_TClientTab;"), std::string::npos);
	EXPECT_NE(NavigationBody.find("Navigation.m_AppearanceTab = Route.m_AppearanceTab;"), std::string::npos);
	EXPECT_NE(CardBody.find("NavigateToSettingsCard(Card.m_Target);"), std::string::npos);
	EXPECT_NE(MenusHeader.find("std::string m_SettingsCardFocusStableId;"), std::string::npos);
	EXPECT_NE(MenusHeader.find("int m_AppearanceSettingsTab = APPEARANCE_TAB_HUD;"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientSearchNavigationUsesTabRouteTable)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const size_t NavigationPos = QmClient.find("SQmGlobalSearchNavigation ResolveGlobalSearchNavigation");
	ASSERT_NE(NavigationPos, std::string::npos);
	const std::string NavigationBody = QmClient.substr(NavigationPos, 3600);

	EXPECT_NE(QmClient.find("struct SQmGlobalSearchTabRoute"), std::string::npos);
	EXPECT_NE(QmClient.find("s_aGlobalSearchTabRoutes[]"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"graphics\", CMenus::SETTINGS_GRAPHICS"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"sound\", CMenus::SETTINGS_SOUND"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"ddnet\", CMenus::SETTINGS_DDNET"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"tclient-bind-wheel\", CMenus::SETTINGS_TCLIENT"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"tclient-chat-binds\", CMenus::SETTINGS_TCLIENT"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"tclient-status-bar\", CMenus::SETTINGS_TCLIENT"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"tclient-info\", CMenus::SETTINGS_TCLIENT"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"tclient-profiles\", CMenus::SETTINGS_PROFILES"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"tclient-configs\", CMenus::SETTINGS_QMCLIENT, -1, -1, CMenus::QMCLIENT_SETTINGS_TAB_CONFIG}"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"appearance-hud\", CMenus::SETTINGS_APPEARANCE"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"appearance-chat\", CMenus::SETTINGS_APPEARANCE"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"appearance-name-plate\", CMenus::SETTINGS_APPEARANCE"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"appearance-hook-collision\", CMenus::SETTINGS_APPEARANCE"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"appearance-info-messages\", CMenus::SETTINGS_APPEARANCE"), std::string::npos);
	EXPECT_NE(QmClient.find("{\"appearance-laser\", CMenus::SETTINGS_APPEARANCE"), std::string::npos);
	EXPECT_NE(NavigationBody.find("for(const SQmGlobalSearchTabRoute &Route : s_aGlobalSearchTabRoutes)"), std::string::npos);
	EXPECT_EQ(NavigationBody.find("str_comp(pTab, \"graphics\")"), std::string::npos);
	EXPECT_EQ(NavigationBody.find("str_comp(pTab, \"sound\")"), std::string::npos);
	EXPECT_EQ(NavigationBody.find("str_comp(pTab, \"ddnet\")"), std::string::npos);
	EXPECT_EQ(NavigationBody.find("str_comp(pTab, \"tclient-bind-wheel\")"), std::string::npos);
	EXPECT_EQ(NavigationBody.find("str_comp(pTab, \"tclient-status-bar\")"), std::string::npos);
	EXPECT_EQ(NavigationBody.find("str_comp(pTab, \"appearance-"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientSearchNavigationRouteTableCoversRegistryDeckTabs)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const size_t RouteTablePos = QmClient.find("static constexpr SQmGlobalSearchTabRoute s_aGlobalSearchTabRoutes[]");
	ASSERT_NE(RouteTablePos, std::string::npos);
	const size_t RouteTableEnd = QmClient.find("};", RouteTablePos);
	ASSERT_NE(RouteTableEnd, std::string::npos);
	const std::string RouteTable = QmClient.substr(RouteTablePos, RouteTableEnd - RouteTablePos);

	std::set<std::string> DeckTabs;
	for(const qm_card_registry::SCardDefault &Default : qm_card_registry::Defaults())
	{
		if(Default.m_pStableId == nullptr || Default.m_pDefaultTab == nullptr)
			continue;
		if(str_startswith(Default.m_pStableId, "deck:") == nullptr)
			continue;
		DeckTabs.insert(Default.m_pDefaultTab);
	}
	ASSERT_FALSE(DeckTabs.empty());
	for(const std::string &Tab : DeckTabs)
	{
		if(Tab == "qmclient-contributors")
		{
			EXPECT_NE(QmClient.find("if(str_comp(pTab, \"qmclient-contributors\") == 0)"), std::string::npos);
			continue;
		}
		if(Tab == "global-search")
			continue;
		const std::string Needle = "{\"" + Tab + "\",";
		EXPECT_NE(RouteTable.find(Needle), std::string::npos) << Tab;
	}
}

TEST(QmMonitoringHelpers, QmClientFriendEnterTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionFriendNotifyContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("SettingsUiContext(\"settings_qmclient_friend_enter_text_inputs\", BodySize / ui_token::font::BODY)"), std::string::npos);
	EXPECT_NE(Body.find("pInput->SetEmptyText(Localize(pPlaceholder));"), std::string::npos);
	EXPECT_NE(Body.find("RenderText(\"qmclient-friend-notifications-large-text-content\", \"Large text content\", &s_FriendEnterBroadcastText, \"Please use %s as friend name\");"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, pInput, ControlColumn, Localize(pPlaceholder), BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("s_FriendEnterGreetText"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_FriendEnterBroadcastText"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_FriendEnterGreetText"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientGoresActorChatInputUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionGoresActorContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("SettingsUiContext(\"settings_qmclient_gores_actor_text_inputs\", BodySize / ui_token::font::BODY)"), std::string::npos);
	EXPECT_NE(Body.find("s_FreezeChatMessageQmClient.SetEmptyText(Localize(\"Leave empty to disable\"));"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_FreezeChatMessageQmClient, ControlColumn, Localize(\"Leave empty to disable\"), BodySize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_FreezeChatMessageQmClient"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientPieMenuRenameQueueUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionPieMenuContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("SettingsUiContext(\"settings_qmclient_pie_menu_text_inputs\", UiScale)"), std::string::npos);
	EXPECT_NE(Body.find("s_PieMenuRenameQueue.SetEmptyText(Localize(\"Example: name1|name2|name3\"));"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_PieMenuRenameQueue, ControlColumn, Localize(\"Example: name1|name2|name3\"), BodySize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_PieMenuRenameQueue"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientTranslateSettingsInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionTranslateContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("SettingsUiContext(\"settings_qmclient_translate_text_inputs\", BodySize / ui_token::font::BODY)"), std::string::npos);

	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_TranslateEndpoint, ControlCol, \"https://tmt.tencentcloudapi.com/\", BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_TranslateEndpoint, ControlCol, \"http://localhost:5000\", BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_TranslateRegion, ControlCol, \"ap-guangzhou\", BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_TranslateSecretId, ControlCol, Localize(\"Tencent Cloud SecretId\"), BodySize);"), std::string::npos);
	const size_t TcSecretKeyHiddenPos = Body.find("s_TranslateSecretKey.SetHidden(true);");
	const size_t TcSecretKeyTextFieldPos = Body.find("ui_widget::InputField(TextInputCtx, &s_TranslateSecretKey, ControlCol, Localize(\"Tencent Cloud SecretKey\"), BodySize);", TcSecretKeyHiddenPos);
	EXPECT_NE(TcSecretKeyHiddenPos, std::string::npos);
	EXPECT_NE(TcSecretKeyTextFieldPos, std::string::npos);
	EXPECT_LT(TcSecretKeyHiddenPos, TcSecretKeyTextFieldPos);
	const size_t LibreKeyHiddenPos = Body.find("s_TranslateKey.SetHidden(true);");
	const size_t LibreKeyTextFieldPos = Body.find("ui_widget::InputField(TextInputCtx, &s_TranslateKey, ControlCol, \"\", BodySize);", LibreKeyHiddenPos);
	EXPECT_NE(LibreKeyHiddenPos, std::string::npos);
	EXPECT_NE(LibreKeyTextFieldPos, std::string::npos);
	EXPECT_LT(LibreKeyHiddenPos, LibreKeyTextFieldPos);

	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, pActiveKeyInput, ControlCol, pActiveKeyInput->GetEmptyText(), BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, pActiveEndpointInput, ControlCol, pActiveEndpointInput->GetEmptyText(), BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, pActiveModelInput, ControlCol, pActiveModelInput->GetEmptyText(), BodySize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TextInputCtx, &s_CustomPrompt, ControlCol, Localize(\"Leave empty to use default prompt\"), BodySize);"), std::string::npos);
	const size_t LanguageOptionsPos = Body.find("ui_widget::STextFieldOptions LanguageInputOptions;");
	const size_t LanguagePlaceholderPos = Body.find("LanguageInputOptions.m_pPlaceholder = pEmptyText;", LanguageOptionsPos);
	const size_t LanguageFontPos = Body.find("LanguageInputOptions.m_FontSize = BodySize;", LanguagePlaceholderPos);
	const size_t LanguageCornersPos = Body.find("LanguageInputOptions.m_Corners = IGraphics::CORNER_ALL;", LanguageFontPos);
	const size_t LanguageAlignPos = Body.find("LanguageInputOptions.m_TextAlign = TEXTALIGN_MC;", LanguageCornersPos);
	const size_t LanguageTextFieldPos = Body.find("ui_widget::InputField(TextInputCtx, &LineInput, EditRect, LanguageInputOptions);", LanguageAlignPos);
	const size_t LanguageWriteBackPos = Body.find("str_copy(pConfigValue, LineInput.GetString(), ConfigValueSize);", LanguageTextFieldPos);
	EXPECT_NE(LanguageOptionsPos, std::string::npos);
	EXPECT_NE(LanguagePlaceholderPos, std::string::npos);
	EXPECT_NE(LanguageFontPos, std::string::npos);
	EXPECT_NE(LanguageCornersPos, std::string::npos);
	EXPECT_NE(LanguageAlignPos, std::string::npos);
	EXPECT_NE(LanguageTextFieldPos, std::string::npos);
	EXPECT_NE(LanguageWriteBackPos, std::string::npos);
	EXPECT_LT(LanguageOptionsPos, LanguagePlaceholderPos);
	EXPECT_LT(LanguagePlaceholderPos, LanguageFontPos);
	EXPECT_LT(LanguageFontPos, LanguageCornersPos);
	EXPECT_LT(LanguageCornersPos, LanguageAlignPos);
	EXPECT_LT(LanguageAlignPos, LanguageTextFieldPos);
	EXPECT_LT(LanguageTextFieldPos, LanguageWriteBackPos);

	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_TranslateEndpoint"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_TranslateRegion"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_TranslateSecretId"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_TranslateSecretKey"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_TranslateKey"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(pActiveKeyInput"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(pActiveEndpointInput"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(pActiveModelInput"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_CustomPrompt"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&LineInput"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientVoiceTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmHudVoiceContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext QmClientVoiceTextInputCtx;");
	const size_t UiPos = Body.find("QmClientVoiceTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t AnimPos = Body.find("QmClientVoiceTextInputCtx.m_pAnim = PrewarmOnly || Ui()->RenderOnly() ? nullptr : &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
	const size_t TreePos = Body.find("QmClientVoiceTextInputCtx.m_pTree = PrewarmOnly || Ui()->RenderOnly() ? nullptr : &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
	const size_t ScopePos = Body.find("QmClientVoiceTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_qmclient_voice_text_inputs\");", TreePos);
	const size_t FrameDtPos = Body.find("QmClientVoiceTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(AnimPos, std::string::npos);
	EXPECT_NE(TreePos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(FrameDtPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, AnimPos);
	EXPECT_LT(AnimPos, TreePos);
	EXPECT_LT(TreePos, ScopePos);
	EXPECT_LT(ScopePos, FrameDtPos);

	const size_t TokenInputPos = Body.find("static CLineInput s_VoiceToken(g_Config.m_QmVoiceToken, sizeof(g_Config.m_QmVoiceToken));");
	const size_t TokenEmptyTextPos = Body.find("s_VoiceToken.SetEmptyText(Localize(\"Leave empty to join public room\"));", TokenInputPos);
	const size_t TokenHiddenPos = Body.find("s_VoiceToken.SetHidden(true);", TokenEmptyTextPos);
	const size_t TokenTextFieldPos = Body.find("ui_widget::InputField(QmClientVoiceTextInputCtx, &s_VoiceToken, ControlCol, Localize(\"Leave empty to join public room\"), BodySize);", TokenHiddenPos);
	EXPECT_NE(TokenInputPos, std::string::npos);
	EXPECT_NE(TokenEmptyTextPos, std::string::npos);
	EXPECT_NE(TokenHiddenPos, std::string::npos);
	EXPECT_NE(TokenTextFieldPos, std::string::npos);
	EXPECT_LT(TokenInputPos, TokenEmptyTextPos);
	EXPECT_LT(TokenEmptyTextPos, TokenHiddenPos);
	EXPECT_LT(TokenHiddenPos, TokenTextFieldPos);

	const size_t ServerInputPos = Body.find("static CLineInput s_VoiceServer(g_Config.m_QmVoiceServer, sizeof(g_Config.m_QmVoiceServer));");
	const size_t ServerEmptyTextPos = Body.find("s_VoiceServer.SetEmptyText(\"42.194.185.210:9987\");", ServerInputPos);
	const size_t ServerTextFieldPos = Body.find("ui_widget::InputField(QmClientVoiceTextInputCtx, &s_VoiceServer, ControlCol, \"42.194.185.210:9987\", BodySize);", ServerEmptyTextPos);
	EXPECT_NE(ServerInputPos, std::string::npos);
	EXPECT_NE(ServerEmptyTextPos, std::string::npos);
	EXPECT_NE(ServerTextFieldPos, std::string::npos);
	EXPECT_LT(ServerInputPos, ServerEmptyTextPos);
	EXPECT_LT(ServerEmptyTextPos, ServerTextFieldPos);

	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_VoiceToken, &ControlCol, LgBodySize"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_VoiceServer, &ControlCol, LgBodySize"), std::string::npos);
}

TEST(QmMonitoringHelpers, NeteaseLyricsSettingsAreIntegratedWithSystemMediaControls)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmHudSystemMediaControlsContent");
	ASSERT_FALSE(Body.empty());
	EXPECT_NE(Body.find("g_Config.m_QmLyrics"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_QmLyricsInMediaIsland"), std::string::npos);
	EXPECT_EQ(Source.find("RenderQmHudLyricsContent"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsRenderOnlyTraversalHasNoInputAnimationDeviceOrConfigSideEffects)
{
	const std::string FormsSource = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string InputFieldBody = ExtractSourceFunctionBody(FormsSource, "SInputFieldResult InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const SInputFieldOptions &Options)");
	ASSERT_FALSE(InputFieldBody.empty());
	const size_t RenderOnlyPos = InputFieldBody.find("if(Ctx.m_pUi->RenderOnly())");
	const size_t EmptyTextMutationPos = InputFieldBody.find("pInput->SetEmptyText(");
	EXPECT_NE(RenderOnlyPos, std::string::npos);
	EXPECT_NE(EmptyTextMutationPos, std::string::npos);
	EXPECT_LT(RenderOnlyPos, EmptyTextMutationPos);
	EXPECT_NE(InputFieldBody.find("return {};", RenderOnlyPos), std::string::npos);

	const std::string QmSource = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string VoiceBody = ExtractSourceFunctionBody(QmSource, "void CMenus::RenderQmHudVoiceContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)");
	const std::string BackgroundBody = ExtractSourceFunctionBody(QmSource, "void CMenus::RenderQmHudBackground3DContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(VoiceBody.empty());
	ASSERT_FALSE(BackgroundBody.empty());
	EXPECT_NE(VoiceBody.find("if(!ReadOnly && !s_VoiceInputDevicesInitialized)"), std::string::npos);
	EXPECT_NE(VoiceBody.find("if(!ReadOnly && !s_VoiceOutputDevicesInitialized)"), std::string::npos);
	EXPECT_NE(BackgroundBody.find("if(!PrewarmOnly && !Ui()->RenderOnly() && g_Config.m_Qm3DParticlesSizeMax < g_Config.m_Qm3DParticlesSizeMin)"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsRenderOnlyTraversalDoesNotConsumeDeckAnimationOrSkinRefreshState)
{
	const std::string MenusSource = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Tee7Source = ReadRepoFile("src/game/client/components/menus_settings7.cpp");
	const std::string ControlsSource = ReadRepoFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string TClientSource = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");

	const std::string ResolveTab = ExtractSourceFunctionBody(MenusSource, "float CMenus::ResolveMenuTabAnimationValue(");
	const std::string TriggerSwitch = ExtractSourceFunctionBody(MenusSource, "void CMenus::TriggerUiSwitchAnimation(");
	const std::string ReadSwitch = ExtractSourceFunctionBody(MenusSource, "float CMenus::ReadUiSwitchAnimation(");
	const std::string Toggle = ExtractSourceFunctionBody(MenusSource, "int CMenus::DoButton_Toggle(");
	const std::string MenuButton = ExtractSourceFunctionBody(MenusSource, "int CMenus::DoButton_Menu(");
	for(const std::string *pBody : {&ResolveTab, &TriggerSwitch, &ReadSwitch, &Toggle, &MenuButton})
	{
		ASSERT_FALSE(pBody->empty());
		const size_t Guard = pBody->find("RenderOnly()");
		const size_t Runtime = pBody->find("AnimRuntime");
		ASSERT_NE(Guard, std::string::npos);
		ASSERT_NE(Runtime, std::string::npos);
		EXPECT_LT(Guard, Runtime);
	}

	const std::string Player = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettingsPlayer(");
	const std::string Tee = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettingsTee(");
	const std::string Tee7 = ExtractSourceFunctionBody(Tee7Source, "void CMenus::RenderSettingsTee7Content(");
	const std::string Tee7Custom = ExtractSourceFunctionBody(Tee7Source, "void CMenus::RenderSettingsTeeCustom7(");
	ASSERT_FALSE(Player.empty());
	ASSERT_FALSE(Tee.empty());
	ASSERT_FALSE(Tee7.empty());
	ASSERT_FALSE(Tee7Custom.empty());
	EXPECT_EQ(Player.find("TriggerUiSwitchAnimation"), std::string::npos);
	EXPECT_EQ(Tee.find("TriggerUiSwitchAnimation"), std::string::npos);
	EXPECT_NE(Tee.find("if(!Ui()->RenderOnly() && (DoButton_Menu(&s_SkinRefreshButton"), std::string::npos);
	EXPECT_NE(Tee.find("if(!RenderOnly && ShouldRefresh)"), std::string::npos);
	EXPECT_NE(Tee7.find("if(!Ui()->RenderOnly())"), std::string::npos);
	EXPECT_NE(Tee7.find("if(!Ui()->RenderOnly() && (DoButton_Menu(&s_SkinRefreshButton"), std::string::npos);
	EXPECT_NE(Tee7Custom.find("if(!Ui()->RenderOnly())"), std::string::npos);

	const std::string Controls = ExtractSourceFunctionBody(ControlsSource, "void CMenusSettingsControls::Render(");
	ASSERT_FALSE(Controls.empty());
	EXPECT_NE(Controls.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(Controls.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(Controls.find("ReadOnly ? nullptr : &m_SettingsScrollRegion"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && ui_widget::InputField("), std::string::npos);
	EXPECT_NE(Controls.find("else if(!ReadOnly && !m_vSearchMatches.empty()"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && m_SearchMatchReveal"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && DeckResult.m_OrderChanged)"), std::string::npos);
	EXPECT_NE(Controls.find("if(!ReadOnly && (m_BindOptionsDirty || GameClient()->m_KeyBinder.IsActive()))"), std::string::npos);
	const std::string BindRows = ExtractSourceFunctionBody(ControlsSource, "void CMenusSettingsControls::RenderSettingsBinds(");
	ASSERT_FALSE(BindRows.empty());
	EXPECT_NE(BindRows.find("if(!ReadOnly && !m_SettingsScrollRegion.AddRect(KeyReaders)"), std::string::npos);
	const size_t ReadOnlyKeyGuard = BindRows.find("if(ReadOnly)");
	const size_t KeyReaderCall = BindRows.find("GameClient()->m_KeyBinder.DoKeyReader(");
	ASSERT_NE(ReadOnlyKeyGuard, std::string::npos);
	ASSERT_NE(KeyReaderCall, std::string::npos);
	EXPECT_LT(ReadOnlyKeyGuard, KeyReaderCall);
	EXPECT_NE(BindRows.find("if(ReadOnly)\n\t\t\t\tcontinue;", ReadOnlyKeyGuard), std::string::npos);

	const std::string TClientMain = ExtractSourceFunctionBody(TClientSource, "void CMenus::RenderSettingsTClient(CUIRect MainView, bool PrewarmOnly)");
	const std::string TClientSettings = ExtractSourceFunctionBody(TClientSource, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	const std::string TClientWarList = ExtractSourceFunctionBody(TClientSource, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	const std::string TClientProfiles = ExtractSourceFunctionBody(TClientSource, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	const std::string TClientConfigs = ExtractSourceFunctionBody(TClientSource, "void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(TClientMain.empty());
	ASSERT_FALSE(TClientSettings.empty());
	ASSERT_FALSE(TClientWarList.empty());
	ASSERT_FALSE(TClientProfiles.empty());
	ASSERT_FALSE(TClientConfigs.empty());
	EXPECT_NE(TClientMain.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(TClientMain.find("if(!ReadOnly)\n\t\tEnsureSettingsBindCache();"), std::string::npos);
	EXPECT_NE(TClientMain.find("RenderSettingsTClientSettings(ContentView, ReadOnly);"), std::string::npos);
	EXPECT_NE(TClientSettings.find("CSectionLoader &VisualFontLoader = ReadOnly ? s_VisualFontReadOnlyLoader : s_VisualFontLoader;"), std::string::npos);
	EXPECT_NE(TClientSettings.find("CSectionLoader &RightSectionLoader = ReadOnly ? s_RightSectionReadOnlyLoader : s_RightSectionLoader;"), std::string::npos);
	EXPECT_NE(TClientWarList.find("CListBox &EntriesListBox = ReadOnly ? s_EntriesReadOnlyListBox : s_EntriesListBox;"), std::string::npos);
	EXPECT_NE(TClientWarList.find("CListBox &WarTypeListBox = ReadOnly ? s_WarTypeReadOnlyListBox : s_WarTypeListBox;"), std::string::npos);
	EXPECT_NE(TClientWarList.find("CListBox &PlayerListBox = ReadOnly ? s_PlayerReadOnlyListBox : s_PlayerListBox;"), std::string::npos);
	const auto ExpectInactiveBeforeStart = [](const std::string &Body, const char *pListName) {
		const std::string SetActive = std::string(pListName) + ".SetActive(!ReadOnly);";
		const std::string DoStart = std::string(pListName) + ".DoStart(";
		const size_t SetActivePos = Body.find(SetActive);
		const size_t DoStartPos = Body.find(DoStart);
		EXPECT_NE(SetActivePos, std::string::npos) << pListName;
		EXPECT_NE(DoStartPos, std::string::npos) << pListName;
		EXPECT_LT(SetActivePos, DoStartPos) << pListName;
	};
	ExpectInactiveBeforeStart(TClientWarList, "EntriesListBox");
	ExpectInactiveBeforeStart(TClientWarList, "WarTypeListBox");
	ExpectInactiveBeforeStart(TClientWarList, "PlayerListBox");
	EXPECT_NE(TClientWarList.find("if(!ReadOnly)\n\t{\n\t\ts_pSelectedEntry = pSelectedEntry;\n\t\ts_pSelectedType = pSelectedType;\n\t}"), std::string::npos);
	EXPECT_NE(TClientProfiles.find("CListBox &ProfilesListBox = ReadOnly ? s_ProfilesReadOnlyListBox : s_ProfilesListBox;"), std::string::npos);
	ExpectInactiveBeforeStart(TClientProfiles, "ProfilesListBox");
	EXPECT_NE(TClientProfiles.find("if(!ReadOnly)\n\t\ts_SelectedProfile = SelectedProfile;"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("CScrollRegion &ConfigListScrollRegion = ReadOnly ? s_ConfigListReadOnlyScrollRegion : s_ConfigListScrollRegion;"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("ConfigListScrollRegion.AddRect(Header)"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("ConfigListScrollRegion.AddRect(RowItem)"), std::string::npos);
	EXPECT_EQ(TClientConfigs.find("s_ConfigListScrollRegion.AddRect("), std::string::npos);
	EXPECT_NE(TClientConfigs.find("float &PrevConfigsScrollY = ReadOnly ? s_PrevConfigsReadOnlyScrollY : s_PrevConfigsScrollY;"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("PrevConfigsScrollY = ScrollFrame.m_FinalOffsetY;"), std::string::npos);
	EXPECT_EQ(TClientConfigs.find("s_PrevConfigsScrollY = ScrollFrame.m_FinalOffsetY;"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("auto &IntInputs = ReadOnly ? s_ReadOnlyIntInputs : s_IntInputs;"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("auto &StrInputs = ReadOnly ? s_ReadOnlyStrInputs : s_StrInputs;"), std::string::npos);
	EXPECT_NE(TClientConfigs.find("auto &ColInputs = ReadOnly ? s_ReadOnlyColInputs : s_ColInputs;"), std::string::npos);
	EXPECT_EQ(TClientConfigs.find("SIntState &State = s_IntInputs[pVar];"), std::string::npos);
	EXPECT_EQ(TClientConfigs.find("SStrState &State = s_StrInputs[pVar];"), std::string::npos);
	EXPECT_EQ(TClientConfigs.find("SColState &ColState = s_ColInputs[pVar];"), std::string::npos);

	const std::string RenderPassOrderModel = ExtractSourceFunctionBody(MenusSource, "qm_card_order::CModel &CMenus::SettingsCardOrderModelForRenderPass()");
	ASSERT_FALSE(RenderPassOrderModel.empty());
	EXPECT_NE(RenderPassOrderModel.find("m_SettingsCardRenderOnlyOrderInitialized"), std::string::npos);
	EXPECT_NE(RenderPassOrderModel.find("m_SettingsCardRenderOnlyOrderSource != g_Config.m_QmGlobalCardOrder"), std::string::npos);
}

TEST(QmMonitoringHelpers, LaserPreviewDrawsWeaponBodiesBeforePreviewLaser)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)");
	ASSERT_FALSE(Body.empty());

	const size_t PreviewLaserPos = Body.find("GameClient()->m_Items.RenderLaser(From, Pos, OuterColor, InnerColor, 4.0f, TicksHead, LaserType, g_Config.m_QmLaserGlowIntensity);");
	const size_t RifleBodyPos = Body.find("Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);");
	const size_t ShotgunBodyPos = Body.find("Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);", RifleBodyPos + 1);

	ASSERT_NE(PreviewLaserPos, std::string::npos);
	ASSERT_NE(RifleBodyPos, std::string::npos);
	ASSERT_NE(ShotgunBodyPos, std::string::npos);
	EXPECT_LT(RifleBodyPos, PreviewLaserPos);
	EXPECT_LT(ShotgunBodyPos, PreviewLaserPos);
}

TEST(QmMonitoringHelpers, LaserRoundCapsRenderedInBothEnhancedAndPlainPaths)
{
	const std::string Source = ReadRepoFile("src/game/client/components/items.cpp");
	ASSERT_FALSE(Source.empty());

	// RenderLaser splits into an enhanced-glow branch and a plain else branch.
	// Round caps must be drawn in BOTH paths so toggling QmLaserRoundCaps is
	// visible even without QmLaserEnhanced (the original bug: the toggle had
	// no visible effect unless enhancement was also enabled).
	const std::string RoundCapGuard = "if(g_Config.m_QmLaserRoundCaps)";
	size_t Pos = 0;
	int Count = 0;
	while((Pos = Source.find(RoundCapGuard, Pos)) != std::string::npos)
	{
		++Count;
		Pos += RoundCapGuard.size();
	}
	EXPECT_GE(Count, 2);
}

TEST(QmMonitoringHelpers, QmLayoutTransitionCacheIsOwnedByTree)
{
	const std::string AnimHeader = ReadRepoFile("src/game/client/QmUi/QmAnim.h");
	const std::string TreeHeader = ReadRepoFile("src/game/client/QmUi/QmTree.h");
	const std::string TreeSource = ReadRepoFile("src/game/client/QmUi/QmTree.cpp");
	EXPECT_NE(AnimHeader.find("ResolveTargetValue(uint64_t NodeKey, EUiAnimProperty Property, float Target, const SUiAnimTransition &Transition);"), std::string::npos);
	EXPECT_NE(AnimHeader.find("SResolveTargetState"), std::string::npos);
	EXPECT_NE(AnimHeader.find("m_LastTargets"), std::string::npos);
	EXPECT_NE(AnimHeader.find("m_ResolveUseCounter"), std::string::npos);
	EXPECT_EQ(AnimHeader.find("SFlipRectState"), std::string::npos);
	EXPECT_EQ(AnimHeader.find("m_FlipRects"), std::string::npos);
	EXPECT_EQ(AnimHeader.find("m_FlipUseCounter"), std::string::npos);
	EXPECT_NE(TreeHeader.find("ResolveLayoutTransition(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target, const struct SUiSpringConfig &Spring, int Priority = 1, bool Animate = true);"), std::string::npos);
	EXPECT_NE(TreeHeader.find("m_LayoutTransitions"), std::string::npos);
	EXPECT_NE(TreeHeader.find("m_LayoutUseCounter"), std::string::npos);
	EXPECT_NE(TreeHeader.find("PruneLayoutTransitionCache"), std::string::npos);
	EXPECT_NE(TreeSource.find("ResolveLayoutTransition(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const CUIRect &Target, const SUiSpringConfig &Spring, int Priority, bool Animate)"), std::string::npos);
	EXPECT_NE(TreeSource.find("|| !Animate"), std::string::npos);
	EXPECT_NE(TreeSource.find("AnimRuntime.ResolveTargetValue("), std::string::npos);
	EXPECT_NE(TreeSource.find("m_LayoutTransitions"), std::string::npos);
	EXPECT_NE(TreeSource.find("m_LayoutUseCounter"), std::string::npos);
	EXPECT_NE(TreeSource.find("PruneLayoutTransitionCache"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmUiAnimatePresenceIsGenericWidgetHelper)
{
	const std::string Context = ReadRepoFile("src/game/client/QmUi/UiContext.h");
	const std::string Overlays = ReadRepoFile("src/game/client/QmUi/UiOverlays.h");
	const std::string TreeHeader = ReadRepoFile("src/game/client/QmUi/QmTree.h");
	const std::string TreeSource = ReadRepoFile("src/game/client/QmUi/QmTree.cpp");
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(Context.find("CUiV2Tree *m_pTree = nullptr;"), std::string::npos);
	EXPECT_NE(Overlays.find("SAnimatePresenceResult"), std::string::npos);
	EXPECT_NE(Overlays.find("AnimatePresence(const IUiContext &Ctx, const void *pId, bool Visible"), std::string::npos);
	EXPECT_NE(Overlays.find("Ctx.m_pTree->ResolvePresence(*Ctx.m_pAnim"), std::string::npos);
	EXPECT_NE(Overlays.find("const SAnimatePresenceResult Presence = AnimatePresence(Ctx, pId, Visible, ui_token::motion::TOAST_SLIDE);"), std::string::npos);
	EXPECT_NE(Overlays.find("const SAnimatePresenceResult Presence = AnimatePresence(Ctx, pId, *pOpen, ui_token::motion::MODAL_IN);"), std::string::npos);
	EXPECT_NE(TreeHeader.find("struct SUiPresenceResult"), std::string::npos);
	EXPECT_NE(TreeHeader.find("ResolvePresence(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, bool Visible"), std::string::npos);
	EXPECT_NE(TreeSource.find("ResolvePresence(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, bool Visible"), std::string::npos);
	EXPECT_NE(QmClient.find("Ctx.m_pTree = PrewarmOnly ? nullptr : &GameClient()->UiRuntimeV2()->Tree();"), std::string::npos);
}

TEST(QmMonitoringHelpers, InputFieldsConsumeSharedLayoutHelper)
{
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Header = ReadRepoFile("src/game/client/QmUi/UiForms.h");
	const std::string InputBody = ExtractSourceFunctionBody(Forms, "SInputFieldResult InputField(const IUiContext &Ctx, CLineInput *pInput, const CUIRect &Rect, const SInputFieldOptions &Options)");
	ASSERT_FALSE(InputBody.empty());

	EXPECT_NE(Header.find("struct SInputFieldLayout"), std::string::npos);
	EXPECT_NE(Header.find("ResolveInputFieldLayout("), std::string::npos);
	EXPECT_NE(Header.find("struct SInputFieldOptions"), std::string::npos);
	EXPECT_NE(InputBody.find("const bool InlineTrailingText = Options.m_InlineTrailingText"), std::string::npos);
	EXPECT_NE(InputBody.find("const float TrailingWidth = HasTrailingAction ? std::max(Options.m_TrailingWidth, Rect.h) : Options.m_TrailingWidth;"), std::string::npos);
	EXPECT_NE(InputBody.find("ResolveInputFieldLayout(Rect, HasIcon, Options.m_Clearable, Ctx.m_UiScale, InlineTrailingText ? 0.0f : TrailingWidth)"), std::string::npos);
	EXPECT_NE(InputBody.find("ResolveInlineTrailingTextLayout(Layout.m_ContentRect"), std::string::npos);
	EXPECT_NE(InputBody.find("Layout.m_ContentRect.h * CUi::ms_FontmodHeight * 0.8f"), std::string::npos);
	EXPECT_NE(InputBody.find("Layout.m_ContentRect"), std::string::npos);
	EXPECT_NE(InputBody.find("Layout.m_ClearRect"), std::string::npos);
	EXPECT_NE(InputBody.find("Layout.m_TrailingRect"), std::string::npos);
	EXPECT_NE(InputBody.find("RenderOptions.m_pHitRect = &InputHitRect;"), std::string::npos);
	EXPECT_NE(InputBody.find("Options.m_Mode == EInputFieldMode::MULTILINE"), std::string::npos);
	EXPECT_NE(InputBody.find("DrawTextFieldFocusBorder(Ctx, pInput, Layout.m_FocusRingRect, Options.m_Mode == EInputFieldMode::MULTILINE);"), std::string::npos);
	EXPECT_LT(InputBody.find("if(Options.m_SearchHotkeyEnabled"), InputBody.find("DrawTextFieldFocusBorder(Ctx, pInput, Layout.m_FocusRingRect, Options.m_Mode == EInputFieldMode::MULTILINE);"));
}
TEST(QmMonitoringHelpers, SettingsCardShellConsumesCanonicalVisualContract)
{
	const std::string Source = ReadRepoFile("src/game/client/QmUi/SettingsCard.cpp");
	const std::string CardHeader = ReadRepoFile("src/game/client/QmUi/SettingsCard.h");
	const std::string DeckHeader = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.h");
	const std::string MenusSource = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string QmClientSource = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(Source.find("State.m_DrawOffsetY"), std::string::npos);
	EXPECT_NE(Source.find("DrawState.m_DrawAlpha"), std::string::npos);
	EXPECT_NE(Source.find("DrawState.m_DropFeedback"), std::string::npos);
	EXPECT_EQ(Source.find("DrawState.m_ReflowCompleteFeedback"), std::string::npos);
	EXPECT_NE(Source.find("const bool InteractionComplete = DrawState.m_DropFeedback;"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardSubtitleVisible(DrawState.m_Hovered, DrawState.m_SubtitleVisibleDuringMotion, DrawState.m_Focused)"), std::string::npos);
	EXPECT_EQ(Source.find("PointInRect(DrawRect"), std::string::npos);
	EXPECT_NE(Source.find("ColorRGBA Surface = ResolveSettingsCardSurfaceColor(VisualOptions.m_UseSurfaceColor ? VisualOptions.m_SurfaceColor : Theme.m_Surface, DrawState);"), std::string::npos);
	EXPECT_NE(CardHeader.find("bool m_UseSurfaceColor = false;"), std::string::npos);
	EXPECT_EQ(Source.find("ResolveSettingsCardLinkedSurfaceColor"), std::string::npos);
	EXPECT_EQ(Source.find("DrawState.m_Hovered ? Theme.m_SurfaceHovered : Theme.m_Surface"), std::string::npos);
	EXPECT_NE(Source.find("VisualOptions.m_RainbowTitles"), std::string::npos);
	EXPECT_NE(Source.find("const bool DrawNormalBorder = VisualOptions.m_AlwaysShowBorders;"), std::string::npos);
	EXPECT_NE(Source.find("const bool DrawAttentionBorder = DrawState.m_Focused || DrawState.m_Dragged || InteractionComplete;"), std::string::npos);
	EXPECT_EQ(Source.find("DrawState.m_Hovered ? Theme.m_BorderHovered"), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsSmallFontSize(UiScale)"), std::string::npos);
	EXPECT_EQ(Source.find("ui_token::font::SMALL * UiScale"), std::string::npos);
	EXPECT_EQ(Source.find("RenderCanonicalSettingsCardHandle("), std::string::npos);
	EXPECT_NE(Source.find("DrawRoundedSurface(Ctx, ChromeRect"), std::string::npos);
	EXPECT_EQ(Source.find("ChromeRect.Draw(Surface, IGraphics::CORNER_ALL, CardRadius);"), std::string::npos);
	EXPECT_EQ(Source.find("ExecuteSettingsCardChromeDraw("), std::string::npos);
	EXPECT_EQ(Source.find("ResolveSettingsCardBorderRingClipRects"), std::string::npos);
	EXPECT_NE(Source.find("DrawNormalBorder || DrawAttentionBorder ? BorderWidth : 0.0f"), std::string::npos);
	EXPECT_EQ(Source.find("InnerSurface.Margin(BorderWidth, &InnerSurface);"), std::string::npos);
	EXPECT_EQ(Source.find("ChromeRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius);"), std::string::npos);
	EXPECT_EQ(Source.find("BorderRect.Draw(Border, IGraphics::CORNER_ALL, CardRadius);"), std::string::npos);
	EXPECT_EQ(Source.find("FocusRect.Draw(FocusRing"), std::string::npos);
	EXPECT_NE(DeckHeader.find("bool m_AllowHeaderDrag = true;"), std::string::npos);
	EXPECT_NE(MenusSource.find("Options.m_RainbowTitles = g_Config.m_QmUiCardRainbowTitles != 0;"), std::string::npos);
	EXPECT_NE(MenusSource.find("Options.m_AlwaysShowBorders = g_Config.m_QmUiCardBorders != 0;"), std::string::npos);
	EXPECT_NE(MenusSource.find("Options.m_SurfaceColor = CardColor.WithAlpha(std::clamp(g_Config.m_QmUiCardOpacity / 100.0f"), std::string::npos);
	EXPECT_EQ(MenusSource.find("Options.m_SurfaceColor = CardColor.WithAlpha(std::clamp(g_Config.m_QmUiOpacity / 100.0f"), std::string::npos);
	EXPECT_EQ(MenusSource.find("Options.m_RainbowTitles = g_Config.m_QmUiCardRainbowTitles != 0 &&"), std::string::npos);
	EXPECT_NE(QmClientSource.find("RenderSettingsCardCollapseButton(CardCtx, Frame.m_HandleRect, Collapsed)"), std::string::npos);
	EXPECT_EQ(QmClientSource.find("Collapsed ? \"+\" : \"-\""), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsCheckboxAndTClientConditionalRowsUseTheCanonicalSurfaceAndLayout)
{
	const std::string MenusSource = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string TClientSource = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string SettingsDeckSource = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string AutoReply = ExtractSourceFunctionBody(TClientSource, "float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)");
	const std::string Hud = ExtractSourceFunctionBody(TClientSource, "float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)");

	ASSERT_FALSE(AutoReply.empty());
	ASSERT_FALSE(Hud.empty());
	EXPECT_NE(MenusSource.find("const ColorRGBA BoxColor(1.0f, 1.0f, 1.0f, BoxAlpha);"), std::string::npos);
	EXPECT_NE(MenusSource.find("DrawRoundedSurface(Ui(), Box, BoxColor, BoxColor, 3.0f);"), std::string::npos);
	EXPECT_NE(AutoReply.find("if(g_Config.m_TcAutoReplyMuted)"), std::string::npos);
	EXPECT_NE(AutoReply.find("if(g_Config.m_TcAutoReplyMinimized)"), std::string::npos);
	EXPECT_EQ(AutoReply.find("ReplyRect = Rows.Next();\n\tif(Render && g_Config.m_TcAutoReplyMuted)"), std::string::npos);
	EXPECT_EQ(AutoReply.find("ReplyRect = Rows.Next();\n\tif(Render && g_Config.m_TcAutoReplyMinimized)"), std::string::npos);
	EXPECT_NE(Hud.find("if(g_Config.m_TcRenderCursorSpec)"), std::string::npos);
	EXPECT_NE(Hud.find("if(g_Config.m_TcNotifyWhenLast)"), std::string::npos);
	EXPECT_NE(Hud.find("if(g_Config.m_TcShowCenter)"), std::string::npos);
	EXPECT_EQ(Hud.find("else\n\t{\n\t\tRows.Next();\n\t\tRows.Next();\n\t\tRows.Next();\n\t}"), std::string::npos);
	EXPECT_NE(TClientSource.find("Hash = HashValueFnv1a64(Hash, g_Config.m_TcTinyTees > 0);"), std::string::npos);
	EXPECT_NE(TClientSource.find("return HashValueFnv1a64(Hash, g_Config.m_TcPredMarginInFreeze);"), std::string::npos);
	EXPECT_NE(TClientSource.find("if(g_Config.m_TcTinyTees > 0)\n\t\t\t{\n\t\t\t\tconst bool RenderTinyTeeSize"), std::string::npos);
	EXPECT_NE(TClientSource.find("static std::vector<CButtonContainer> s_vTinyTeeModeButtons = {{}, {}, {}};"), std::string::npos);
	EXPECT_NE(TClientSource.find("s_vTinyTeeModeButtons, {\"tclient-smaller-tees-none\""), std::string::npos);
	EXPECT_NE(TClientSource.find("Ui()->DoButtonLogic(&s_vTinyTeeModeButtons[Index]"), std::string::npos);
	EXPECT_EQ(TClientSource.find("s_vTinyTeePreLayoutButtons"), std::string::npos);
	EXPECT_NE(TClientSource.find("if(g_Config.m_TcRemoveAnti)\n\t\t\t{\n\t\t\t\tCUIRect AmountButton = Rows.Next();"), std::string::npos);
	EXPECT_NE(TClientSource.find("if(g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID)\n\t\t\t{\n\t\t\t\tCUIRect ColorRow = Rows.Next();"), std::string::npos);
	EXPECT_NE(TClientSource.find("BuildTClientConditionalRowsPreLayoutInput"), std::string::npos);
	EXPECT_NE(TClientSource.find("Definition.m_PreLayoutInput = BuildTClientConditionalRowsPreLayoutInput(s_aDeckCardSpecs[Index].first);"), std::string::npos);
	EXPECT_NE(TClientSource.find("static CUi::SDropDownState s_TrailDropDownState;"), std::string::npos);
	EXPECT_NE(TClientSource.find("if(str_comp(pStableCardId, \"tclient:tee-trails\") == 0)"), std::string::npos);
	EXPECT_NE(TClientSource.find("const int Selected = s_TrailDropDownState.m_SelectionPopupContext.m_SelectionIndex;"), std::string::npos);
	EXPECT_NE(TClientSource.find("const int NewColorMode = Selected + 1;"), std::string::npos);
	EXPECT_NE(TClientSource.find("g_Config.m_TcTeeTrailColorMode = NewColorMode;"), std::string::npos);
	EXPECT_NE(TClientSource.find("Definition.m_HasPendingPreLayoutInput = []"), std::string::npos);
	EXPECT_NE(TClientSource.find("return Selected >= 0 && Selected < 4;"), std::string::npos);
	const size_t PendingInput = SettingsDeckSource.find("const bool HasPendingPreLayoutInput = Card.m_pDefinition->m_HasPendingPreLayoutInput");
	const size_t PreLayoutInput = SettingsDeckSource.find("Card.m_pDefinition->m_PreLayoutInput(PreLayoutFrame.m_ContentRect)");
	const size_t InvalidateCardHeight = SettingsDeckSource.find("m_vContentHeights[Card.m_StateIndex] = -1.0f;", PreLayoutInput);
	const size_t InvalidateAllHeights = SettingsDeckSource.find("std::fill(m_vContentHeights.begin(), m_vContentHeights.end(), -1.0f);", InvalidateCardHeight);
	const size_t RebuildPreparedCards = SettingsDeckSource.find("BuildPreparedCards(*pColumns);", InvalidateAllHeights);
	ASSERT_NE(PendingInput, std::string::npos);
	ASSERT_NE(PreLayoutInput, std::string::npos);
	ASSERT_NE(InvalidateCardHeight, std::string::npos);
	ASSERT_NE(InvalidateAllHeights, std::string::npos);
	ASSERT_NE(RebuildPreparedCards, std::string::npos);
	EXPECT_LT(PendingInput, PreLayoutInput);
	EXPECT_LT(PreLayoutInput, InvalidateCardHeight);
	EXPECT_LT(InvalidateCardHeight, InvalidateAllHeights);
	EXPECT_LT(InvalidateAllHeights, RebuildPreparedCards);
	EXPECT_NE(TClientSource.find("tclient:visual-nameplates"), std::string::npos);
	EXPECT_NE(TClientSource.find("tclient:visual-effects"), std::string::npos);
	EXPECT_NE(TClientSource.find("tclient:anti-latency-tools"), std::string::npos);
	EXPECT_NE(TClientSource.find("tclient:auto-reply"), std::string::npos);
	EXPECT_NE(TClientSource.find("tclient:hud"), std::string::npos);
	EXPECT_NE(TClientSource.find("return HashValueFnv1a64(Hash, g_Config.m_TcWhiteFeet);"), std::string::npos);
}

TEST(QmMonitoringHelpers, MigratedQmClientCardTitlesRemainLocalized)
{
	const std::string Language = ReadRepoFile("data/languages/simplified_chinese.txt");

	EXPECT_NE(Language.find("Weapon animation\n== 武器动画"), std::string::npos);
	EXPECT_NE(Language.find("Hitbox mode\n== 碰撞箱模式"), std::string::npos);
	EXPECT_NE(Language.find("Chat Bubble\n== 消息气泡"), std::string::npos);
	EXPECT_NE(Language.find("Streamer Mode\n== 主播模式"), std::string::npos);
	EXPECT_NE(Language.find("Camera & FOV\n== 镜头与视野"), std::string::npos);
}

TEST(QmMonitoringHelpers, GlobalSearchTargetsGraphicsCanonicalCard)
{
	const std::string SearchSource = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string SettingsSource = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string RegistrySource = ReadRepoFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string SearchCardBody = ExtractSourceFunctionBody(SearchSource, "void CMenus::RenderSettingsGlobalSearchContent(CUIRect MainView, bool PrewarmOnly)");
	const std::string GraphicsBody = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(SearchCardBody.empty());
	ASSERT_FALSE(GraphicsBody.empty());

	EXPECT_NE(SearchCardBody.find("NavigateToSettingsCard(Card.m_Target);"), std::string::npos);
	EXPECT_NE(GraphicsBody.find("m_SettingsCardDeck.RequestReveal(m_SettingsCardFocusStableId.c_str());"), std::string::npos);
	EXPECT_NE(GraphicsBody.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(RegistrySource.find("graphics visual rendering card appearance settings card border corner segments rainbow title"), std::string::npos);
}
TEST(QmMonitoringHelpers, RegistryNavigationBridgeOwnsSettingsTarget)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string SetPageBody = ExtractSourceFunctionBody(Source, "bool CMenus::SetSettingsPageFromCardTab(const char *pTab)");
	const std::string NavigateBody = ExtractSourceFunctionBody(Source, "void CMenus::NavigateToSettingsCard(const qm_card_registry::SCardNavigationTarget &Target)");
	ASSERT_FALSE(SetPageBody.empty());
	ASSERT_FALSE(NavigateBody.empty());

	EXPECT_NE(Header.find("bool SetSettingsPageFromCardTab(const char *pTab);"), std::string::npos);
	EXPECT_NE(Header.find("void NavigateToSettingsCard(const qm_card_registry::SCardNavigationTarget &Target);"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"graphics\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"player\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tee\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-chat-binds\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-warlist\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-info\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-profiles\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"tclient-configs\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("g_Config.m_UiSettingsPage = SETTINGS_GRAPHICS;"), std::string::npos);
	EXPECT_NE(SetPageBody.find("str_comp(pTab, \"appearance-hud\") == 0"), std::string::npos);
	EXPECT_NE(SetPageBody.find("return false;"), std::string::npos);
	EXPECT_NE(NavigateBody.find("SetSettingsPageFromCardTab(Target.m_pTab)"), std::string::npos);
	EXPECT_NE(NavigateBody.find("m_SettingsCardDeck.RequestReveal(Target.m_pStableId);"), std::string::npos);
}

// 意图：registry 的动态 Localize 标题也必须显式进入提取器，避免生成语言文件后 Search 回退英文。
TEST(QmMonitoringHelpers, RegistrySearchTitlesRemainLocalizationSourceKeys)
{
	const std::string Registry = ReadRepoFile("src/game/client/QmUi/QmCardRegistry.cpp");
	const std::string SettingsSource = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string GraphicsBody = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(GraphicsBody.empty());
	EXPECT_NE(Registry.find("Localizable(\"Graphics display\")"), std::string::npos);
	EXPECT_NE(Registry.find("Localizable(\"Visual\")"), std::string::npos);
	EXPECT_NE(Registry.find("Localizable(\"Display modes\")"), std::string::npos);
	EXPECT_EQ(Registry.find("Localizable(\"Graphics backend\")"), std::string::npos);
	EXPECT_NE(GraphicsBody.find("Localize(\"Graphics backend\")"), std::string::npos);
}
TEST(QmMonitoringHelpers, GraphicsDeckUsesPublicCoordinator)
{
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string MenusSource = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string SettingsBody = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettings(CUIRect MainView)");
	const std::string GraphicsBody = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(SettingsBody.empty());
	ASSERT_FALSE(GraphicsBody.empty());

	EXPECT_NE(MenusHeader.find("CSettingsCardDeck m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(MenusHeader.find("m_SettingsCardDeckDisplayCycle"), std::string::npos);
	EXPECT_EQ(MenusHeader.find("m_GraphicsSettingsCardDeck"), std::string::npos);
	const size_t CycleGuard = SettingsBody.find("m_SettingsCardDeckDisplayState.EnterView(SettingsDisplayViewKey)");
	const size_t BeginDisplayCycle = SettingsBody.find("m_SettingsCardDeck.BeginDisplayCycle", CycleGuard);
	ASSERT_NE(CycleGuard, std::string::npos);
	ASSERT_NE(BeginDisplayCycle, std::string::npos);
	EXPECT_LT(CycleGuard, BeginDisplayCycle);
	EXPECT_NE(SettingsBody.find("ResolveSettingsCardDisplayViewKey"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_SettingsCardDeckDisplayState.LeaveSettings();"), std::string::npos);
	EXPECT_NE(SettingsBody.find("m_SettingsCardDeck.BeginDisplayCycle(++m_SettingsCardDeckDisplayCycle, true);"), std::string::npos);
	EXPECT_EQ(SettingsBody.find("m_SettingsCardDeck.BeginDisplayCycle(++m_SettingsCardDeckDisplayCycle, false);"), std::string::npos);
	EXPECT_EQ(GraphicsBody.find("m_SettingsCardDeck.BeginDisplayCycle"), std::string::npos);
	EXPECT_NE(GraphicsBody.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(GraphicsBody.find("SettingsCardOrderModelForRenderPass()"), std::string::npos);
	EXPECT_NE(GraphicsBody.find("SaveSettingsCardOrderModel()"), std::string::npos);
}

TEST(QmMonitoringHelpers, GraphicsDeckRemovesOnlyThePublicBridge)
{
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Header.find("m_GraphicsSettingsCardDeck"), std::string::npos);
	EXPECT_EQ(Body.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Body.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(Body.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(Body.find("RenderSettingsCardDeckDragOverlay("), std::string::npos);
}

TEST(QmMonitoringHelpers, P6TClientBindWheelMigrationUsesThePublicDeckOnly)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string BindWheelBody = ExtractSourceFunctionBody(TClient, "void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)");
	EXPECT_NE(QmClient.find("CSettingsCardDeck &CardDeck = ReadOnly ?"), std::string::npos);
	ASSERT_FALSE(BindWheelBody.empty());
	EXPECT_NE(BindWheelBody.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(BindWheelBody.find("CSettingsCardDeck &CardDeck = ReadOnly ? s_BindWheelPrewarmDeck : m_SettingsCardDeck;"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("CardDeck.RenderCached("), std::string::npos);
	EXPECT_NE(BindWheelBody.find("SettingsCardOrderModel()"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("CScrollRegion s_BindWheelSettingsScrollRegion"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("deck:tclient-bind-wheel-editor"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("deck:tclient-bind-wheel-preview"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("const bool ReadOnly = PrewarmOnly || Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("s_BindWheelPrewarmOrderModel.LoadMerged(\"\", qm_card_registry::BuildDefaultEntries())"), std::string::npos);
	EXPECT_NE(BindWheelBody.find("InputState.m_AllowHeaderDrag = !ReadOnly;"), std::string::npos);
	EXPECT_EQ(BindWheelBody.find("BeginSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(BindWheelBody.find("BeginSettingsCardDeckCard("), std::string::npos);
	EXPECT_EQ(BindWheelBody.find("EndSettingsCardDeck("), std::string::npos);
	EXPECT_EQ(BindWheelBody.find("s_BindWheelSettingsScrollY"), std::string::npos);
	EXPECT_EQ(BindWheelBody.find("s_BindWheelEditorCardHeight"), std::string::npos);
	EXPECT_EQ(BindWheelBody.find("s_BindWheelPreviewCardHeight"), std::string::npos);
}

TEST(QmMonitoringHelpers, P6VisualContentOwnersRemainShellFree)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string StreamerBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualStreamerContent(CUIRect &Content, float LineHeight, float LineSpacing)");
	const std::string TranslateUiBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualTranslateUiContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing)");
	const std::string EntityOverlayBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualEntityOverlayContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	const std::string CollisionHitboxBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualCollisionHitboxContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	const std::string WeaponAnimationBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualWeaponAnimationContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, float ContentGap, bool PrewarmOnly)");
	const std::string ChatBubbleBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualChatBubbleContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	const std::string SkinTransitionBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualSkinTransitionContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	const std::string FocusModeBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualFocusModeContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float ColumnGap, float LabelWidth)");
	const std::string CameraViewBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmVisualCameraViewContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(StreamerBody.empty());
	ASSERT_FALSE(TranslateUiBody.empty());
	ASSERT_FALSE(EntityOverlayBody.empty());
	ASSERT_FALSE(CollisionHitboxBody.empty());
	ASSERT_FALSE(WeaponAnimationBody.empty());
	ASSERT_FALSE(ChatBubbleBody.empty());
	ASSERT_FALSE(SkinTransitionBody.empty());
	ASSERT_FALSE(FocusModeBody.empty());
	ASSERT_FALSE(CameraViewBody.empty());

	EXPECT_NE(Header.find("RenderQmVisualStreamerContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualTranslateUiContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualEntityOverlayContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualCollisionHitboxContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualWeaponAnimationContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualChatBubbleContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualSkinTransitionContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualFocusModeContent"), std::string::npos);
	EXPECT_NE(Header.find("RenderQmVisualCameraViewContent"), std::string::npos);
	EXPECT_NE(Header.find("IsQmNewFeatureRead"), std::string::npos);
	EXPECT_NE(StreamerBody.find("RenderQmVisualCheckbox"), std::string::npos);
	EXPECT_NE(TranslateUiBody.find("NTranslateUiSettings::RenderTranslateUiModule"), std::string::npos);
	EXPECT_NE(EntityOverlayBody.find("RenderQmSettingsSliderWithValueInput"), std::string::npos);
	EXPECT_NE(CollisionHitboxBody.find("DoLine_ColorPicker"), std::string::npos);
	EXPECT_NE(WeaponAnimationBody.find("MarkQmNewFeatureHovered"), std::string::npos);
	EXPECT_NE(ChatBubbleBody.find("DoLine_ColorPicker"), std::string::npos);
	EXPECT_NE(SkinTransitionBody.find("MarkQmNewFeatureHovered"), std::string::npos);
	EXPECT_NE(SkinTransitionBody.find("RenderQmSettingsSliderWithValueInput"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("toggle qm_focus_mode 0 1"), std::string::npos);
	EXPECT_NE(FocusModeBody.find("g_CommandBindCache"), std::string::npos);
	EXPECT_NE(CameraViewBody.find("QueueAspectApply"), std::string::npos);
	EXPECT_NE(CameraViewBody.find("RenderQmSettingsSliderWithValueInput"), std::string::npos);
	EXPECT_EQ(StreamerBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(TranslateUiBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(QmClient.find("RenderQmVisualCardAppearanceContent"), std::string::npos);
	EXPECT_EQ(Header.find("RenderQmVisualCardAppearanceContent"), std::string::npos);
	EXPECT_EQ(EntityOverlayBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(CollisionHitboxBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(WeaponAnimationBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(ChatBubbleBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(SkinTransitionBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(FocusModeBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(CameraViewBody.find("RegisterModuleCard"), std::string::npos);
}

TEST(QmMonitoringHelpers, P6FunctionGoresContentExtractionKeepsStableHeightAndInputState)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string GoresBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmFunctionGoresContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(GoresBody.empty());

	EXPECT_NE(Header.find("RenderQmFunctionGoresContent"), std::string::npos);
	EXPECT_EQ(GoresBody.find("LightFirstFrame"), std::string::npos);
	EXPECT_NE(GoresBody.find("settings_qmclient_gores_text_inputs"), std::string::npos);
	EXPECT_NE(GoresBody.find("s_AxiomLoginPassword"), std::string::npos);
	EXPECT_NE(GoresBody.find("s_AxiomDummyLoginPassword"), std::string::npos);
	EXPECT_NE(GoresBody.find("RenderQmFunctionCheckbox"), std::string::npos);
	EXPECT_NE(GoresBody.find("toggle qm_gores 0 1"), std::string::npos);
	EXPECT_NE(GoresBody.find("Content.HSplitTop(LineSpacing, nullptr, &Content);"), std::string::npos);
	EXPECT_NE(GoresBody.find("InputField(TextInputCtx, &Input, ControlColumn, Options)"), std::string::npos);
	EXPECT_NE(GoresBody.find("DoKeyReader(&s_ReaderButtonGoresToggle, &s_ClearButtonGoresToggle, &BindKey"), std::string::npos);
	EXPECT_EQ(GoresBody.find("CenterControl"), std::string::npos);
	EXPECT_EQ(GoresBody.find("0.35f"), std::string::npos);
	EXPECT_EQ(GoresBody.find("RegisterModuleCard"), std::string::npos);
}

TEST(QmMonitoringHelpers, P6FunctionBlockWordsContentExtractionKeepsStableHeightAndInputContracts)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string BlockWordsBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmFunctionBlockWordsContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(BlockWordsBody.empty());

	EXPECT_NE(Header.find("RenderQmFunctionBlockWordsContent"), std::string::npos);
	EXPECT_EQ(BlockWordsBody.find("LightFirstFrame"), std::string::npos);
	EXPECT_NE(BlockWordsBody.find("str_utf8_truncate"), std::string::npos);
	EXPECT_NE(BlockWordsBody.find("EInputFieldMode::MULTILINE"), std::string::npos);
	EXPECT_NE(BlockWordsBody.find("CalcQiaFenInputHeight"), std::string::npos);
	EXPECT_NE(BlockWordsBody.find("RenderQmFunctionCheckbox"), std::string::npos);
	EXPECT_EQ(BlockWordsBody.find("RegisterModuleCard"), std::string::npos);
}

TEST(QmMonitoringHelpers, P6FunctionKeywordReplyContentExtractionKeepsRuleRowsAndConfigCodec)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Header = ReadRepoFile("src/game/client/components/menus.h");
	const std::string KeywordReplyBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmFunctionKeywordReplyContent(CUIRect &Content, float UiScale, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	ASSERT_FALSE(KeywordReplyBody.empty());

	EXPECT_NE(Header.find("RenderQmFunctionKeywordReplyContent"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("ParseAutoReplyRules"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("AutoReplyRowsMatchRules"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("QmKeywordReplyRules::DecodeFromConfig"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("QmKeywordReplyRules::EncodeForConfig"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("Changes.ShouldCommit(Ui()->RenderOnly())"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("RenderQmFunctionCheckbox"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("RenderQmSettingsSliderWithValueInput"), std::string::npos);
	EXPECT_NE(KeywordReplyBody.find("IsAutoReplyRuleRowHalfFilled"), std::string::npos);
	EXPECT_EQ(KeywordReplyBody.find("RegisterModuleCard"), std::string::npos);
}

TEST(QmMonitoringHelpers, P6HudPlayerStatsContentExtractionKeepsProgressBranchesMeasured)
{
	const std::string QmClient = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string PlayerStatsBody = ExtractSourceFunctionBody(QmClient, "void CMenus::RenderQmHudPlayerStatsContent(CUIRect &Content, float LineHeight, float BodySize, float LineSpacing, float LabelWidth, bool PrewarmOnly)");
	const size_t CaseStart = QmClient.rfind("case EQmModuleId::PlayerStats:");
	const size_t CaseEnd = QmClient.find("case EQmModuleId::CollisionHitbox:", CaseStart);
	const std::string DeckCase = CaseStart != std::string::npos && CaseEnd != std::string::npos ? QmClient.substr(CaseStart, CaseEnd - CaseStart) : "";
	ASSERT_FALSE(PlayerStatsBody.empty());
	ASSERT_FALSE(DeckCase.empty());

	EXPECT_NE(PlayerStatsBody.find("m_QmPlayerStatsMapProgress"), std::string::npos);
	EXPECT_NE(PlayerStatsBody.find("m_QmPlayerStatsMapProgressStyle"), std::string::npos);
	EXPECT_NE(PlayerStatsBody.find("DoLine_ColorPicker"), std::string::npos);
	EXPECT_NE(PlayerStatsBody.find("RenderQmSettingsSliderWithValueInput"), std::string::npos);
	EXPECT_EQ(PlayerStatsBody.find("RegisterModuleCard"), std::string::npos);
	EXPECT_EQ(PlayerStatsBody.find("HandleModuleDragState"), std::string::npos);
	EXPECT_NE(DeckCase.find("RenderQmHudPlayerStatsContent"), std::string::npos);
	EXPECT_EQ(DeckCase.find("RenderSliderWithValueInput"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientContentOwnersPreserveInteractiveContracts)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string KeyBinderSource = ReadRepoFile("src/game/client/components/key_binder.cpp");
	const std::string KeyBinds = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionKeyBindsContent(");
	const std::string FriendNotify = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionFriendNotifyContent(");
	const std::string FavoriteMaps = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionFavoriteMapsContent(");
	const std::string PieMenu = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionPieMenuContent(");
	const std::string Translate = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionTranslateContent(");
	const std::string NotificationsBasic = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmHudNotificationsBasicContent(");
	const std::string NotificationsAdvanced = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmHudNotificationsAdvancedContent(");
	const std::string FunctionDeck = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsQmClientFunctionDeck(");
	const std::string HudDeck = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsQmClientHudDeck(");
	ASSERT_FALSE(KeyBinds.empty());
	ASSERT_FALSE(FriendNotify.empty());
	ASSERT_FALSE(FavoriteMaps.empty());
	ASSERT_FALSE(PieMenu.empty());
	ASSERT_FALSE(Translate.empty());
	ASSERT_FALSE(NotificationsBasic.empty());
	ASSERT_FALSE(NotificationsAdvanced.empty());
	ASSERT_FALSE(FunctionDeck.empty());
	ASSERT_FALSE(HudDeck.empty());
	EXPECT_NE(KeyBinderSource.find("const bool ReadOnly = Ui()->RenderOnly();"), std::string::npos);
	EXPECT_NE(KeyBinderSource.find("if(!ReadOnly && m_pKeyReaderId == pReaderButton && m_Key.has_value())"), std::string::npos);

	EXPECT_NE(KeyBinds.find("+toggle cl_dummy_hammer 1 0"), std::string::npos);
	EXPECT_NE(KeyBinds.find("qm_timeout_disconnect"), std::string::npos);
	EXPECT_NE(FriendNotify.find("ui_widget::InputField"), std::string::npos);
	EXPECT_NE(FriendNotify.find("m_QmFriendOnlineAutoRefresh"), std::string::npos);
	EXPECT_NE(FriendNotify.find("m_QmFriendEnterAutoGreet"), std::string::npos);
	EXPECT_NE(FriendNotify.find("m_QmFriendEnterBroadcast"), std::string::npos);
	EXPECT_NE(FavoriteMaps.find("UpdateMapCategoryCache"), std::string::npos);
	EXPECT_NE(FavoriteMaps.find("RemoveFavoriteMap"), std::string::npos);
	EXPECT_NE(PieMenu.find("ShowPopupColorPicker"), std::string::npos);
	EXPECT_NE(PieMenu.find("qmclient-pie-menu-reset-colors"), std::string::npos);
	EXPECT_NE(Translate.find("RenderLanguageDropDownWithCustomInput"), std::string::npos);
	EXPECT_NE(Translate.find("m_QmTranslateLlmEnableThinking"), std::string::npos);
	EXPECT_NE(NotificationsBasic.find("m_QmHudNotificationsShowAdvanced"), std::string::npos);
	EXPECT_NE(NotificationsAdvanced.find("m_QmHudNotificationsUseCategoryFilters"), std::string::npos);
	EXPECT_NE(NotificationsAdvanced.find("RenderQmSettingsSliderWithValueInput"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("RenderQmFunctionKeyBindsContent"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("RenderQmFunctionFriendNotifyContent"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("RenderQmFunctionFavoriteMapsContent"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("RenderQmFunctionPieMenuContent"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("RenderQmFunctionTranslateContent"), std::string::npos);
	EXPECT_NE(HudDeck.find("RenderQmHudNotificationsBasicContent"), std::string::npos);
	EXPECT_NE(HudDeck.find("RenderQmHudNotificationsAdvancedContent"), std::string::npos);
}

TEST(QmMonitoringHelpers, TimeoutDisconnectReconnectAdvertisesDDNetVersionBeforeSystemInfo)
{
	const std::string Source = ReadRepoFile("src/engine/client/client.cpp");
	const std::string SendInfoBody = ExtractSourceFunctionBody(Source, "void CClient::SendInfo(int Conn)");
	ASSERT_FALSE(SendInfoBody.empty());

	const size_t LegacyVersion = SendInfoBody.find("CMsgPacker MsgLegacyVersion(NETMSGTYPE_CL_ISDDNETLEGACY, false);");
	const size_t TClientInfo = SendInfoBody.find("SendTClientInfo(Conn);");
	const size_t ClientVersion = SendInfoBody.find("CMsgPacker MsgVer(NETMSG_CLIENTVER, true);");
	const size_t SystemInfo = SendInfoBody.find("CMsgPacker Msg(NETMSG_INFO, true);");
	ASSERT_NE(LegacyVersion, std::string::npos);
	ASSERT_NE(TClientInfo, std::string::npos);
	ASSERT_NE(ClientVersion, std::string::npos);
	ASSERT_NE(SystemInfo, std::string::npos);

	EXPECT_LT(LegacyVersion, TClientInfo);
	EXPECT_LT(LegacyVersion, ClientVersion);
	EXPECT_LT(LegacyVersion, SystemInfo);
	EXPECT_NE(SendInfoBody.find("MsgLegacyVersion.AddInt(GameClient()->DDNetVersion());"), std::string::npos);
	EXPECT_NE(SendInfoBody.find("SendMsg(Conn, &MsgLegacyVersion, MSGFLAG_VITAL);"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmClientDeckMeasureRevisionsDoNotPreMeasureContent)
{
	const std::string Source = ReadRepoFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string HudDeck = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsQmClientHudDeck(");
	const std::string FunctionDeck = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsQmClientFunctionDeck(");
	const std::string VisualDeck = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsQmClientVisualDeck(");
	const std::string BlockWords = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionBlockWordsContent(");
	const std::string KeywordReply = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionKeywordReplyContent(");
	const std::string FavoriteMaps = ExtractSourceFunctionBody(Source, "void CMenus::RenderQmFunctionFavoriteMapsContent(");
	ASSERT_FALSE(HudDeck.empty());
	ASSERT_FALSE(FunctionDeck.empty());
	ASSERT_FALSE(VisualDeck.empty());
	ASSERT_FALSE(BlockWords.empty());
	ASSERT_FALSE(KeywordReply.empty());
	ASSERT_FALSE(FavoriteMaps.empty());

	EXPECT_EQ(HudDeck.find("m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, EstimateContentHeight"), std::string::npos);
	EXPECT_EQ(FunctionDeck.find("m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, MeasureContentHeight"), std::string::npos);
	EXPECT_EQ(VisualDeck.find("m_MeasureRevision = static_cast<uint64_t>(maximum(0.0f, EstimateContentHeight"), std::string::npos);
	EXPECT_EQ(HudDeck.find("EstimateContentHeight(Id)) * 1000.0f"), std::string::npos);
	EXPECT_EQ(FunctionDeck.find("MeasureContentHeight(Id, Page.m_ContentViewport.w)"), std::string::npos);
	EXPECT_EQ(VisualDeck.find("EstimateContentHeight(Id)) * 1000.0f"), std::string::npos);
	EXPECT_NE(HudDeck.find("MeasureContentRevision"), std::string::npos);
	EXPECT_NE(FunctionDeck.find("MeasureContentRevision"), std::string::npos);
	EXPECT_NE(VisualDeck.find("MeasureContentRevision"), std::string::npos);
	for(const std::string *pDeck : {&HudDeck, &FunctionDeck, &VisualDeck})
	{
		EXPECT_NE(pDeck->find("auto BuildDefinitions ="), std::string::npos);
		EXPECT_NE(pDeck->find("ResolveSettingsCardDefinitionsRevision("), std::string::npos);
		EXPECT_NE(pDeck->find("CardDeck.RenderCached("), std::string::npos);
		EXPECT_NE(pDeck->find("Definition.m_MeasureRevision = MeasureContentRevision(Id);"), std::string::npos);
	}
	const size_t FunctionDeckDefinitions = FunctionDeck.find("auto BuildDefinitions =");
	const size_t FunctionDeckMeasure = FunctionDeck.find("auto MeasureContentHeight");
	ASSERT_NE(FunctionDeckDefinitions, std::string::npos);
	ASSERT_NE(FunctionDeckMeasure, std::string::npos);
	for(const char *pSync : {"str_comp(s_aBlockWordsLayoutConfigCache, g_Config.m_QmBlockWordsList)", "QmKeywordReplyRules::DecodeFromConfig", "UpdateKeywordRulesLayoutState(CountAutoReplyRules(aDecodedRules), false)", "s_FavoriteMapsLayoutCount != FavoriteMapCount"})
	{
		const size_t SyncPosition = FunctionDeck.find(pSync);
		ASSERT_NE(SyncPosition, std::string::npos) << pSync;
		EXPECT_LT(SyncPosition, FunctionDeckDefinitions) << pSync;
	}
	EXPECT_EQ(FunctionDeck.find("QmKeywordReplyRules::DecodeFromConfig", FunctionDeckMeasure + 1), std::string::npos);

	for(const char *pState : {"DummyMiniViewExpanded", "g_Config.m_QmPlayerStatsMapProgress", "g_Config.m_QmSpeedrunTimer", "g_Config.m_QmInputOverlay", "g_Config.m_QmHudNotificationsShowAdvanced", "g_Config.m_QmHudNotificationsUseCategoryFilters", "g_Config.m_QmVoiceEnable", "g_Config.m_QmVoiceShowAdvanced", "DynamicIslandOriginalStyle", "g_Config.m_QmSmtcEnable", "g_Config.m_QmNeteaseHookEnable", "g_Config.m_Qm3DParticles"})
		EXPECT_NE(HudDeck.find(pState), std::string::npos) << pState;
	for(const char *pState : {"g_Config.m_TcFreezeChatEnabled", "g_Config.m_TcFreezeChatEmoticon", "g_Config.m_QmAxiomAutoLogin", "g_Config.m_QmGores", "g_Config.m_QmGoresAutoEnable", "g_Config.m_QmWeaponTrajectory", "g_Config.m_QmFriendOnlineAutoRefresh", "g_Config.m_QmFriendEnterBroadcast", "g_Config.m_QmFriendEnterAutoGreet", "s_BlockWordsLayoutRevision", "g_Config.m_QmTranslateBackend", "g_Config.m_QmTranslateLlmEnableThinking", "g_Config.m_QmTranslateLlmProvider", "s_KeywordRulesLayoutRevision", "g_Config.m_QmPieMenuEnabled", "s_FavoriteMapsLayoutRevision", "g_Config.m_QmAutoTeamLock"})
		EXPECT_NE(FunctionDeck.find(pState), std::string::npos) << pState;
	for(const char *pState : {"g_Config.m_QmChatBubble", "g_Config.m_QmCameraDrift", "g_Config.m_QmDynamicFov", "g_Config.m_QmAspectPreset", "g_Config.m_QmSkinChangeTransition", "g_Config.m_QmWeaponSwitchAnim", "g_Config.m_QmHitboxMode", "g_Config.m_QmShowCollisionHitbox"})
		EXPECT_NE(VisualDeck.find(pState), std::string::npos) << pState;
	for(const char *pState : {"g_Config.m_QmHitboxShowMap", "g_Config.m_QmHitboxShowTeeCollision", "g_Config.m_QmHitboxShowTeeFreeze", "g_Config.m_QmHitboxShowTeeDeath", "g_Config.m_QmHitboxShowHammer", "g_Config.m_QmHitboxShowProjectiles", "g_Config.m_QmHitboxShowLasers", "g_Config.m_QmHitboxShowFreezeLasers", "g_Config.m_QmHitboxShowHook"})
		EXPECT_NE(VisualDeck.find(pState), std::string::npos) << pState;

	EXPECT_NE(BlockWords.find("str_copy(s_aBlockWordsLayoutConfigCache"), std::string::npos);
	EXPECT_NE(BlockWords.find("++s_BlockWordsLayoutRevision"), std::string::npos);
	EXPECT_NE(KeywordReply.find("UpdateKeywordRulesLayoutState"), std::string::npos);
	EXPECT_NE(KeywordReply.find("s_aKeywordRulesConfigCache"), std::string::npos);
	EXPECT_NE(FavoriteMaps.find("++s_FavoriteMapsLayoutRevision"), std::string::npos);
}

TEST(QmMonitoringHelpers, PublicSettingsCardDeckCoordinatesCanonicalDefinitions)
{
	const std::string Header = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.h");
	const std::string Source = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string LogicHeader = ReadRepoFile("src/game/client/QmUi/SettingsCardDeckLogic.h");

	EXPECT_NE(Header.find("struct SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(Header.find("struct SSettingsCardDeckInput"), std::string::npos);
	EXPECT_NE(Header.find("struct SSettingsCardDeckResult"), std::string::npos);
	EXPECT_NE(Header.find("class CSettingsCardDeck"), std::string::npos);
	EXPECT_NE(Header.find("SSettingsCardDeckResult Render("), std::string::npos);
	EXPECT_NE(Header.find("void RequestReveal("), std::string::npos);
	EXPECT_NE(Header.find("void BeginDisplayCycle(uint64_t DisplayCycle, bool AnimateEntry)"), std::string::npos);
	EXPECT_NE(Header.find("CProjectionCache m_ProjectionCache"), std::string::npos);
	EXPECT_NE(Source.find("m_ProjectionCache.Resolve("), std::string::npos);
	EXPECT_NE(Source.find("ApplySettingsCardDeckDragPlacement("), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsCardDeckDropOrder("), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckAutoScrollDelta("), std::string::npos);
	EXPECT_NE(Source.find("CommitSettingsCardDeckDrop("), std::string::npos);
	EXPECT_NE(Source.find("SettingsCard("), std::string::npos);
	EXPECT_NE(Source.find("bool PointerInsideDrawFrame = false;"), std::string::npos);
	EXPECT_NE(Header.find("std::string m_PendingRevealStableId"), std::string::npos);
	EXPECT_EQ(Header.find("m_ReflowCompleteFeedbackRemaining"), std::string::npos);
	EXPECT_NE(Header.find("m_CollapsedInitialized"), std::string::npos);
	EXPECT_NE(Header.find("m_vContentHeights"), std::string::npos);
	EXPECT_NE(Header.find("m_MeasureEachFrame"), std::string::npos);
	EXPECT_NE(Header.find("m_MeasureRevision"), std::string::npos);
	EXPECT_NE(Header.find("m_RenderMeasured"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardEntryNodeKey"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardEntryNodeKey(pTab)"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardEntryNodeKey(pTab, pStableId)"), std::string::npos);
	EXPECT_EQ(Source.find("SettingsCardEntryNodeKey(pTab, pStableId, m_DisplayCycle)"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardHeightNodeKey"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardReflowNodeKey"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckAllowsDragStart(EntryPending, EntryPositionActive, ReflowTargetChanged, ReflowPositionActive || ContentHeightAnimationActive)"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsCardColumnFramePlan"), std::string::npos);
	EXPECT_EQ(Source.find("CursorY = ResolveSettingsCardColumnFrame"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckShouldSnapReflow(GeometryStateChanged, m_Drag.Active())"), std::string::npos);
	EXPECT_NE(Source.find("if(SnapReflow)"), std::string::npos);
	EXPECT_EQ(Source.find("|| m_Drag.Active() || ContentHeightTargetChanged"), std::string::npos);
	EXPECT_EQ(Source.find("SnapReflow = true;"), std::string::npos);
	EXPECT_EQ(Source.find("EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_NE(Header.find("CSettingsCardDeckFrameRuntime m_FrameRuntime"), std::string::npos);
	EXPECT_NE(LogicHeader.find("uint64_t m_DisplayCycle"), std::string::npos);
	EXPECT_NE(Source.find("m_FrameRuntime.AnimateEntry() ? Motion.m_EntryDistance : 0.0f"), std::string::npos);
	EXPECT_NE(Source.find("m_SuppressHoverFeedbackOnce = true"), std::string::npos);
	EXPECT_NE(Source.find("State.m_HoverFeedbackEnabled = !m_SuppressHoverFeedbackOnce"), std::string::npos);
	EXPECT_NE(Source.find("std::abs(Input.m_MouseX - m_LastPointerX)"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCard(Ctx, Card.m_Frame"), std::string::npos);
	EXPECT_NE(Source.find("Card.m_pDefinition->m_RenderMeasured"), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsPageLayoutForScrollViewport"), std::string::npos);
	EXPECT_NE(Source.find("MouseInScrollViewport"), std::string::npos);
	EXPECT_NE(Source.find("Input.m_MouseX >= DrawLayout.m_aColumns[0].x"), std::string::npos);
	EXPECT_NE(Source.find("m_vContentHeights[StateIndex]"), std::string::npos);
	EXPECT_EQ(Source.find("m_vContentHeights.assign(Model.Count(), -1.0f)"), std::string::npos);
	EXPECT_NE(Header.find("m_vContentWidths"), std::string::npos);
	EXPECT_NE(Source.find("std::abs(CachedContentWidth - ContentWidth)"), std::string::npos);
	EXPECT_NE(Source.find("pDefinition->m_MeasureEachFrame"), std::string::npos);
	EXPECT_NE(Source.find("CachedMeasureRevision != pDefinition->m_MeasureRevision"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckNeedsContentMeasure(Collapsed, pDefinition->m_MeasureEachFrame, CachedContentHeight)"), std::string::npos);
	EXPECT_NE(Source.find("for(const int StateIndex : m_vBoundDefinitionStateIndices)"), std::string::npos);
	const size_t ActiveSetStart = Source.find("auto RebuildActiveStateIndices = [&]() {");
	const size_t ActiveSetEnd = Source.find("};", ActiveSetStart);
	ASSERT_NE(ActiveSetStart, std::string::npos);
	ASSERT_NE(ActiveSetEnd, std::string::npos);
	EXPECT_EQ(Source.substr(ActiveSetStart, ActiveSetEnd - ActiveSetStart).find("StateIndexForStableId"), std::string::npos);
	EXPECT_NE(Source.find("SettingsCardDeckRendersContent(Collapsed) ? Card.m_pDefinition->m_Render : FSettingsCardRender{}"), std::string::npos);
	EXPECT_NE(Source.find("Model.Entry(Card.m_StateIndex).m_pStableId"), std::string::npos);
	EXPECT_EQ(Source.find("m_Drag.m_pStableId"), std::string::npos);

	const size_t AddRect = Source.find("pScrollRegion->AddRect(Card.m_Frame.m_Rect, Reveal)");
	const size_t Render = Source.find("SettingsCard(Ctx, Card.m_Frame");
	ASSERT_NE(AddRect, std::string::npos);
	ASSERT_NE(Render, std::string::npos);
	EXPECT_LT(AddRect, Render);

	const std::string CardSource = ReadRepoFile("src/game/client/QmUi/SettingsCard.cpp");
	EXPECT_NE(CardSource.find("HeaderAction(DrawFrame, DrawState.m_Collapsed)"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsCardsKeepStableBackgroundPositionDuringPageSwitches)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettings = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettings(CUIRect MainView)");
	const std::string RenderAppearance = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");

	EXPECT_NE(Menus.find("const bool ContentTransitionActive = TransitionActive && m_MenuPage != PAGE_SETTINGS;"), std::string::npos);
	EXPECT_NE(Menus.find("const bool ContentTransitionActive = TransitionActive && m_GamePage != PAGE_SETTINGS;"), std::string::npos);
	EXPECT_EQ(RenderSettings.find("settings_main_page_switch"), std::string::npos);
	EXPECT_EQ(RenderSettings.find("ApplyUiSwitchOffset(ContentView"), std::string::npos);
	EXPECT_NE(RenderSettings.find("m_SettingsPageSwitchActive = false;"), std::string::npos);
	EXPECT_EQ(RenderAppearance.find("settings_appearance_tab_switch"), std::string::npos);
	EXPECT_EQ(RenderAppearance.find("ApplyUiSwitchOffset(ContentView"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsCardDeckSkipsAnimationRuntimeOnStableFrames)
{
	const std::string Source = ReadRepoFile("src/game/client/QmUi/SettingsCardDeck.cpp");
	const std::string CardSource = ReadRepoFile("src/game/client/QmUi/SettingsCard.cpp");

	EXPECT_NE(Source.find("ResolveSettingsCardAnimationWork("), std::string::npos);
	EXPECT_NE(Source.find("ResolveSettingsCardHeightAnimationWork("), std::string::npos);
	EXPECT_NE(Source.find("if(m_FrameRuntime.EntryWasActive() && Motion.m_EntryDuration > 0.0f)"), std::string::npos);
	EXPECT_NE(Source.find("State.m_DrawOffsetY = DeckEntryOffsetY;"), std::string::npos);
	EXPECT_NE(Source.find("else if(AnimationWork.m_ResolveReflow)"), std::string::npos);
	EXPECT_NE(Source.find("State.m_ClipContent = SettingsCardDeckShouldClipContent(Card.m_Frame.m_ContentRect.w > 0.0f && Card.m_Frame.m_ContentRect.h > 0.0f, Card.m_ContentHeightAnimationActive);"), std::string::npos);
	EXPECT_EQ(Source.find("State.m_ClipContent = ContentHeightAnimationActive;"), std::string::npos);
	EXPECT_NE(CardSource.find("const CUIRect ClipRect = ResolveSettingsCardContentClipRect(DrawFrame.m_ContentRect, DrawFrame.m_Rect, UiScale);"), std::string::npos);
	EXPECT_NE(CardSource.find("Ctx.m_pUi->ClipEnable(&ClipRect);"), std::string::npos);
}

TEST(QmMonitoringHelpers, RenderOnlyNumericFieldsAndDropDownsDoNotMutateControlState)
{
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp");
	const std::string IntegerField = ExtractSourceFunctionBody(Forms, "SInputFieldResult IntegerField(");
	const std::string NumericField = ExtractSourceFunctionBody(Forms, "bool NumericField(");
	const std::string DropDown = ExtractSourceFunctionBody(Ui, "int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)");
	ASSERT_FALSE(IntegerField.empty());
	ASSERT_FALSE(NumericField.empty());
	ASSERT_FALSE(DropDown.empty());
	EXPECT_NE(DropDown.find("m_DropDownFontSize > 0.0f ? m_DropDownFontSize"), std::string::npos);
	EXPECT_NE(DropDown.find("State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;"), std::string::npos);
	EXPECT_NE(DropDown.find("ButtonProps.m_FontSize = ResolvedFontSize;"), std::string::npos);

	const size_t IntegerRenderOnly = IntegerField.find("if(Ctx.m_pUi->RenderOnly())");
	const size_t IntegerWrite = IntegerField.find("*pValue = ClampedValue;");
	ASSERT_NE(IntegerRenderOnly, std::string::npos);
	ASSERT_NE(IntegerWrite, std::string::npos);
	EXPECT_LT(IntegerRenderOnly, IntegerWrite);

	const size_t NumericRenderOnly = NumericField.find("if(RenderOnly)");
	const size_t NumericStateWrite = NumericField.find("pState->m_LastSyncedStoredValue = *pValue;");
	const size_t NumericInputWrite = NumericField.find("pInput->SetInteger(DisplayValue);");
	ASSERT_NE(NumericRenderOnly, std::string::npos);
	ASSERT_NE(NumericStateWrite, std::string::npos);
	ASSERT_NE(NumericInputWrite, std::string::npos);
	EXPECT_LT(NumericRenderOnly, NumericStateWrite);
	EXPECT_LT(NumericRenderOnly, NumericInputWrite);

	const size_t DropDownRenderOnly = DropDown.find("if(RenderOnly())");
	const size_t DropDownStateInit = DropDown.find("if(!State.m_Init)");
	ASSERT_NE(DropDownRenderOnly, std::string::npos);
	ASSERT_NE(DropDownStateInit, std::string::npos);
	EXPECT_LT(DropDownRenderOnly, DropDownStateInit);
}
TEST(QmMonitoringHelpers, GraphicsUsesCanonicalSettingsCardShell)
{
	const std::string SettingsSource = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string GraphicsBody = ExtractSourceFunctionBody(SettingsSource, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(GraphicsBody.empty());

	EXPECT_NE(GraphicsBody.find("SettingsPageLayout("), std::string::npos);
	EXPECT_NE(GraphicsBody.find("SSettingsCardDefinition"), std::string::npos);
	EXPECT_NE(GraphicsBody.find("SettingsCardDeckForRenderPass().RenderCached("), std::string::npos);
	EXPECT_NE(GraphicsBody.find("deck:graphics-display"), std::string::npos);
	EXPECT_EQ(GraphicsBody.find("RenderQmSettingsGlassCard("), std::string::npos);
}
TEST(QmMonitoringHelpers, DelayUpdateStaysOnUnifiedSliderInputPath)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string ScrollbarBody = ExtractSourceFunctionBody(Menus, "bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, int Subtab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)");
	const std::string FormsHeader = ReadRepoFile("src/game/client/QmUi/UiForms.h");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string NumericBody = ExtractSourceFunctionBody(Forms, "bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState, const void *pId, int *pValue, int Min, int Max, const CUIRect &Rect, const SNumericFieldOptions &Options)");
	ASSERT_FALSE(ScrollbarBody.empty());
	ASSERT_FALSE(NumericBody.empty());

	EXPECT_EQ(ScrollbarBody.find("Ui()->DoScrollbarOption("), std::string::npos);
	EXPECT_NE(ScrollbarBody.find("ui_widget::NumericField(InputCtx, pState"), std::string::npos);
	EXPECT_NE(ScrollbarBody.find("Options.m_CommitPolicy = (Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE) != 0 ?"), std::string::npos);
	EXPECT_EQ(Menus.find("DoSettingsSliderInputField"), std::string::npos);
	EXPECT_NE(FormsHeader.find("enum class EInputCommitPolicy"), std::string::npos);
	EXPECT_NE(FormsHeader.find("struct SNumericFieldOptions"), std::string::npos);
	EXPECT_NE(FormsHeader.find("struct SNumericFieldState"), std::string::npos);
	EXPECT_NE(FormsHeader.find("bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState"), std::string::npos);
	EXPECT_NE(FormsHeader.find("CLineInputNumber m_Input;"), std::string::npos);
	EXPECT_NE(FormsHeader.find("EInputCommitPolicy m_CommitPolicy = EInputCommitPolicy::LIVE;"), std::string::npos);
	EXPECT_EQ(FormsHeader.find("SSliderInputFieldOptions"), std::string::npos);
	EXPECT_EQ(FormsHeader.find("bool SliderInputField("), std::string::npos);
	EXPECT_NE(MenusHeader.find("std::unordered_map<const void *, std::unique_ptr<ui_widget::SNumericFieldState>> m_vpSettingsNumericFieldStates;"), std::string::npos);
	EXPECT_EQ(MenusHeader.find("DoSettingsSliderInputField"), std::string::npos);
	EXPECT_EQ(MenusHeader.find("m_vpSettingsSliderInputs"), std::string::npos);
	EXPECT_EQ(ScrollbarBody.find("GetSettingsSliderInput("), std::string::npos);
	EXPECT_NE(NumericBody.find("const bool SliderWasActive = pState->m_SliderWasActive;"), std::string::npos);
	EXPECT_NE(NumericBody.find("UpdateNumericFieldSliderCommit(*pState, Options.m_CommitPolicy, SliderActive, SliderReleased, CandidateStored, pValue)"), std::string::npos);
	EXPECT_NE(NumericBody.find("const int VisibleStoredValue = pState->m_HasPendingValue ? pState->m_PendingStoredValue : *pValue;"), std::string::npos);
}
TEST(QmMonitoringHelpers, SettingsNumericFieldsRemoveLegacySliderForwarding)
{
	const std::string FormsHeader = ReadRepoFile("src/game/client/QmUi/UiForms.h");
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string ScrollbarBody = ExtractSourceFunctionBody(Menus, "bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, int Subtab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)");
	ASSERT_FALSE(ScrollbarBody.empty());

	EXPECT_NE(FormsHeader.find("struct SNumericFieldOptions"), std::string::npos);
	EXPECT_EQ(FormsHeader.find("SSliderInputFieldOptions"), std::string::npos);
	EXPECT_EQ(FormsHeader.find("bool SliderInputField("), std::string::npos);
	EXPECT_NE(Forms.find("bool NumericField(const IUiContext &Ctx, SNumericFieldState *pState"), std::string::npos);
	EXPECT_EQ(Forms.find("bool SliderInputField("), std::string::npos);
	EXPECT_EQ(MenusHeader.find("DoSettingsSliderInputField"), std::string::npos);
	EXPECT_EQ(Menus.find("DoSettingsSliderInputField"), std::string::npos);
	EXPECT_NE(ScrollbarBody.find("ui_widget::NumericField(InputCtx, pState"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsUiThemeIsInjectedIntoSharedInputPrimitives)
{
	const std::string Theme = ReadRepoFile("src/game/client/QmUi/UiTheme.h");
	const std::string Context = ReadRepoFile("src/game/client/QmUi/UiContext.h");
	const std::string Forms = ReadRepoFile("src/game/client/QmUi/UiForms.cpp");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string ContextBody = ExtractSourceFunctionBody(Menus, "IUiContext CMenus::SettingsUiContext(const char *pScope, const float UiScale)");
	const std::string SliderBody = ExtractSourceFunctionBody(Menus, "bool CMenus::DoSettingsScrollbarOption(int Page, int Tab, int Subtab, const char *pTextId, const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)");
	ASSERT_FALSE(ContextBody.empty());
	ASSERT_FALSE(SliderBody.empty());

	EXPECT_NE(Theme.find("struct SUiTheme"), std::string::npos);
	EXPECT_NE(Theme.find("SUiTheme ResolveUiTheme"), std::string::npos);
	EXPECT_NE(Context.find("const SUiTheme *m_pTheme = nullptr;"), std::string::npos);
	EXPECT_NE(Context.find("float m_UiScale = 1.0f;"), std::string::npos);
	EXPECT_NE(Forms.find("SUiTheme ThemeFor(const IUiContext &Ctx)"), std::string::npos);
	EXPECT_NE(Forms.find("ResolveInputFallbackTheme(g_Config.m_QmUiFocusColor)"), std::string::npos);
	EXPECT_EQ(Forms.find("CUi::ms_LightButtonColorFunction"), std::string::npos);
	EXPECT_EQ(Forms.find("ui_token::color::BORDER_FOCUS"), std::string::npos);
	EXPECT_NE(ContextBody.find("m_SettingsUiTheme = ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiFocusColor), ColorHSLA(g_Config.m_QmUiAccentColor), ColorHSLA(g_Config.m_QmUiSelectedColor));"), std::string::npos);
	EXPECT_NE(ContextBody.find("Context.m_pIconManager = GameClient()->QmIconManager();"), std::string::npos);
	EXPECT_NE(ContextBody.find("Context.m_pTooltips = &GameClient()->m_Tooltips;"), std::string::npos);
	EXPECT_NE(ContextBody.find("Context.m_pTheme = &m_SettingsUiTheme;"), std::string::npos);
	EXPECT_NE(SliderBody.find("IUiContext InputCtx = SettingsUiContext(\"settings_slider_input\", Options.m_FontSize / ui_token::font::BODY);"), std::string::npos);
	EXPECT_NE(MenusHeader.find("SUiTheme m_SettingsUiTheme;"), std::string::npos);
}
TEST(QmMonitoringHelpers, TClientConfigSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientConfigs(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientConfigSearchCtx;"), std::string::npos);
	EXPECT_NE(Body.find("TClientConfigSearchCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_config_search\");"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientConfigSearchCtx, &s_SearchInput, SearchEdit, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientWarListSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	const size_t EntriesSearchPos = Body.find("ui_widget::InputField(TClientWarListEntriesSearchCtx, &s_EntriesFilterInput, EntriesSearch, FontSize");
	const size_t EntriesFilterPos = Body.find("if(str_comp(s_aCachedEntriesFilter, s_EntriesFilterInput.GetString()) != 0", EntriesSearchPos);
	const size_t PlayerSearchPos = Body.find("ui_widget::InputField(TClientWarListPlayerSearchCtx, &s_PlayerSearchInput, PlayerSearch, FontSize");
	const size_t PlayerFilterPos = Body.find("if(str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) ||", PlayerSearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientWarListEntriesSearchCtx = TClientWarListTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("TClientWarListEntriesSearchCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_warlist_entries_search\");"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientWarListPlayerSearchCtx = TClientWarListTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("TClientWarListPlayerSearchCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_warlist_player_search\");"), std::string::npos);
	EXPECT_NE(EntriesSearchPos, std::string::npos);
	EXPECT_NE(EntriesFilterPos, std::string::npos);
	EXPECT_LT(EntriesSearchPos, EntriesFilterPos);
	EXPECT_NE(PlayerSearchPos, std::string::npos);
	EXPECT_NE(PlayerFilterPos, std::string::npos);
	EXPECT_LT(PlayerSearchPos, PlayerFilterPos);
	EXPECT_NE(Body.find("s_vFilteredPlayerIds.push_back(ClientId);", PlayerFilterPos), std::string::npos);
	EXPECT_NE(Body.find("PlayerListBox.DoStart(ListRowHeight, s_vFilteredPlayerIds.size()", PlayerFilterPos), std::string::npos);
	EXPECT_EQ(Body.find("s_PlayerListBox.DoStart(ListRowHeight, MAX_CLIENTS"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&s_EntriesFilterInput, &EntriesSearch"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&s_PlayerSearchInput, &PlayerSearch"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientWarListTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientWarList(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientWarListTextInputCtx = SettingsUiContext(\"settings_tclient_warlist_text_inputs\", UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientWarListTextInputCtx, &s_NameInput, ButtonL, Localize(\"Name\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientWarListTextInputCtx, &s_ClanInput, ButtonR, Localize(\"Clan\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientWarListTextInputCtx, &s_ReasonInput, Button, Localize(\"Reason\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientWarListTextInputCtx, &s_TypeNameInput, Button, Localize(\"Group name\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_NameInput, &ButtonL, 12.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_ClanInput, &ButtonR, 12.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_ReasonInput, &Button, 12.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_TypeNameInput, &Button, 12.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientAutoReplyTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string CacheBody = ExtractSourceFunctionBody(Source, "float CMenus::LayoutTClientAutoReplyCacheSection(CUIRect &CurrentColumn, bool Render)");
	const std::string LegacyLayoutBody = ExtractSourceBlock(Source, "auto LayoutAutoReplySection", "auto MeasureAutoReplySection");
	const std::string LegacyInteractiveBody = ExtractSourceBlock(Source, "auto RenderAutoReplyInteractiveSection", "auto LayoutPlayerIndicatorSection");
	ASSERT_FALSE(CacheBody.empty());
	ASSERT_FALSE(LegacyLayoutBody.empty());
	ASSERT_FALSE(LegacyInteractiveBody.empty());

	const auto ExpectAutoReplyCtx = [](const std::string &Body) {
		const size_t CtxPos = Body.find("IUiContext TClientAutoReplyTextInputCtx;");
		const size_t UiPos = Body.find("TClientAutoReplyTextInputCtx.m_pUi = Ui();", CtxPos);
		const size_t AnimPos = Body.find("TClientAutoReplyTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
		const size_t TreePos = Body.find("TClientAutoReplyTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
		const size_t ScopePos = Body.find("TClientAutoReplyTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_auto_reply_text_inputs\");", TreePos);
		const size_t FrameDtPos = Body.find("TClientAutoReplyTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
		EXPECT_NE(CtxPos, std::string::npos);
		EXPECT_NE(UiPos, std::string::npos);
		EXPECT_NE(AnimPos, std::string::npos);
		EXPECT_NE(TreePos, std::string::npos);
		EXPECT_NE(ScopePos, std::string::npos);
		EXPECT_NE(FrameDtPos, std::string::npos);
		EXPECT_LT(CtxPos, UiPos);
		EXPECT_LT(UiPos, AnimPos);
		EXPECT_LT(AnimPos, TreePos);
		EXPECT_LT(TreePos, ScopePos);
		EXPECT_LT(ScopePos, FrameDtPos);
	};

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	ExpectAutoReplyCtx(CacheBody);
	EXPECT_NE(CacheBody.find("s_MutedReply.SetEmptyText(Localize(\"I muted you\"));"), std::string::npos);
	EXPECT_NE(CacheBody.find("s_MinimizedReply.SetEmptyText(Localize(\"I am away from the game window\"));"), std::string::npos);
	EXPECT_NE(CacheBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(CacheBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);"), std::string::npos);
	ExpectAutoReplyCtx(LegacyLayoutBody);
	EXPECT_NE(LegacyLayoutBody.find("s_MutedReply.SetEmptyText(Localize(\"I muted you\"));"), std::string::npos);
	EXPECT_NE(LegacyLayoutBody.find("s_MinimizedReply.SetEmptyText(Localize(\"I am away from the game window\"));"), std::string::npos);
	EXPECT_NE(LegacyLayoutBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(LegacyLayoutBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);"), std::string::npos);
	ExpectAutoReplyCtx(LegacyInteractiveBody);
	EXPECT_NE(LegacyInteractiveBody.find("s_MutedReply.SetEmptyText(Localize(\"I muted you\"));"), std::string::npos);
	EXPECT_NE(LegacyInteractiveBody.find("s_MinimizedReply.SetEmptyText(Localize(\"I am away from the game window\"));"), std::string::npos);
	EXPECT_NE(LegacyInteractiveBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(LegacyInteractiveBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(CacheBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, Localize(\"I muted you\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(CacheBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, Localize(\"I am away from the game window\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, Localize(\"I muted you\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, Localize(\"I am away from the game window\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MutedReply, ReplyRect, Localize(\"I muted you\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("ui_widget::InputField(TClientAutoReplyTextInputCtx, &s_MinimizedReply, ReplyRect, Localize(\"I am away from the game window\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(CacheBody.find("Ui()->DoEditBox(&s_MutedReply, &ReplyRect, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(CacheBody.find("Ui()->DoEditBox(&s_MinimizedReply, &ReplyRect, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("Ui()->DoEditBox(&s_MutedReply, &ReplyRect, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("Ui()->DoEditBox(&s_MinimizedReply, &ReplyRect, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("Ui()->DoEditBox(&s_MutedReply, &ReplyRect, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("Ui()->DoEditBox(&s_MinimizedReply, &ReplyRect, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientPetTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string CacheBody = ExtractSourceFunctionBody(Source, "float CMenus::LayoutTClientPetCacheSection(CUIRect &CurrentColumn, bool Render)");
	const std::string LegacyLayoutBody = ExtractSourceBlock(Source, "auto LayoutPetSection", "auto MeasurePetSection");
	const std::string LegacyInteractiveBody = ExtractSourceBlock(Source, "auto RenderPetInteractiveSection", "auto LayoutAutoReplySection");
	ASSERT_FALSE(CacheBody.empty());
	ASSERT_FALSE(LegacyLayoutBody.empty());
	ASSERT_FALSE(LegacyInteractiveBody.empty());

	const auto ExpectPetCtx = [](const std::string &Body) {
		const size_t CtxPos = Body.find("IUiContext TClientPetTextInputCtx;");
		const size_t UiPos = Body.find("TClientPetTextInputCtx.m_pUi = Ui();", CtxPos);
		const size_t AnimPos = Body.find("TClientPetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
		const size_t TreePos = Body.find("TClientPetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
		const size_t ScopePos = Body.find("TClientPetTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_pet_text_inputs\");", TreePos);
		const size_t FrameDtPos = Body.find("TClientPetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
		EXPECT_NE(CtxPos, std::string::npos);
		EXPECT_NE(UiPos, std::string::npos);
		EXPECT_NE(AnimPos, std::string::npos);
		EXPECT_NE(TreePos, std::string::npos);
		EXPECT_NE(ScopePos, std::string::npos);
		EXPECT_NE(FrameDtPos, std::string::npos);
		EXPECT_LT(CtxPos, UiPos);
		EXPECT_LT(UiPos, AnimPos);
		EXPECT_LT(AnimPos, TreePos);
		EXPECT_LT(TreePos, ScopePos);
		EXPECT_LT(ScopePos, FrameDtPos);
	};

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	ExpectPetCtx(CacheBody);
	EXPECT_NE(CacheBody.find("ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);"), std::string::npos);
	ExpectPetCtx(LegacyLayoutBody);
	EXPECT_NE(LegacyLayoutBody.find("ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);"), std::string::npos);
	ExpectPetCtx(LegacyInteractiveBody);
	EXPECT_NE(LegacyInteractiveBody.find("ui_widget::InputField(TClientPetTextInputCtx, &s_PetSkin, Button, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(CacheBody.find("Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientHudTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string CacheBody = ExtractSourceFunctionBody(Source, "float CMenus::LayoutTClientHudCacheSection(CUIRect &CurrentColumn, bool Render)");
	const std::string LegacyLayoutBody = ExtractSourceBlock(Source, "auto LayoutHudSection", "auto MeasureHudSection");
	const std::string LegacyInteractiveBody = ExtractSourceBlock(Source, "auto RenderHudInteractiveSection", "auto RenderHudSettingsMap");
	ASSERT_FALSE(CacheBody.empty());
	ASSERT_FALSE(LegacyLayoutBody.empty());
	ASSERT_FALSE(LegacyInteractiveBody.empty());

	const auto ExpectHudCtx = [](const std::string &Body) {
		const size_t CtxPos = Body.find("IUiContext TClientHudTextInputCtx;");
		const size_t UiPos = Body.find("TClientHudTextInputCtx.m_pUi = Ui();", CtxPos);
		const size_t AnimPos = Body.find("TClientHudTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
		const size_t TreePos = Body.find("TClientHudTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
		const size_t ScopePos = Body.find("TClientHudTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_hud_text_inputs\");", TreePos);
		const size_t FrameDtPos = Body.find("TClientHudTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
		EXPECT_NE(CtxPos, std::string::npos);
		EXPECT_NE(UiPos, std::string::npos);
		EXPECT_NE(AnimPos, std::string::npos);
		EXPECT_NE(TreePos, std::string::npos);
		EXPECT_NE(ScopePos, std::string::npos);
		EXPECT_NE(FrameDtPos, std::string::npos);
		EXPECT_LT(CtxPos, UiPos);
		EXPECT_LT(UiPos, AnimPos);
		EXPECT_LT(AnimPos, TreePos);
		EXPECT_LT(TreePos, ScopePos);
		EXPECT_LT(ScopePos, FrameDtPos);
	};

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	ExpectHudCtx(CacheBody);
	EXPECT_NE(CacheBody.find("s_LastInput.SetEmptyText(Localize(\"You're the last one!\"));"), std::string::npos);
	EXPECT_NE(CacheBody.find("ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);"), std::string::npos);
	ExpectHudCtx(LegacyLayoutBody);
	EXPECT_NE(LegacyLayoutBody.find("s_LastInput.SetEmptyText(Localize(\"You're the last one!\"));"), std::string::npos);
	EXPECT_NE(LegacyLayoutBody.find("ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);"), std::string::npos);
	ExpectHudCtx(LegacyInteractiveBody);
	EXPECT_NE(LegacyInteractiveBody.find("s_LastInput.SetEmptyText(Localize(\"You're the last one!\"));"), std::string::npos);
	EXPECT_NE(LegacyInteractiveBody.find("ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(CacheBody.find("ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, Localize(\"You're the last one!\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, Localize(\"You're the last one!\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("ui_widget::InputField(TClientHudTextInputCtx, &s_LastInput, Button, Localize(\"You're the last one!\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(CacheBody.find("Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyLayoutBody.find("Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(LegacyInteractiveBody.find("Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientWhiteFeetTextInputUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceBlock(Source, "auto LayoutVisualNameplateSection", "auto LayoutVisualEffectsSection");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TClientWhiteFeetTextInputCtx;");
	const size_t UiPos = Body.find("TClientWhiteFeetTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t AnimPos = Body.find("TClientWhiteFeetTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
	const size_t TreePos = Body.find("TClientWhiteFeetTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
	const size_t ScopePos = Body.find("TClientWhiteFeetTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_white_feet_text_inputs\");", TreePos);
	const size_t FrameDtPos = Body.find("TClientWhiteFeetTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(AnimPos, std::string::npos);
	EXPECT_NE(TreePos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(FrameDtPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, AnimPos);
	EXPECT_LT(AnimPos, TreePos);
	EXPECT_LT(TreePos, ScopePos);
	EXPECT_LT(ScopePos, FrameDtPos);
	EXPECT_NE(Body.find("s_WhiteFeet.SetEmptyText(\"x_ninja\");"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientWhiteFeetTextInputCtx, &s_WhiteFeet, FeetBox, nullptr, EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("ui_widget::InputField(TClientWhiteFeetTextInputCtx, &s_WhiteFeet, FeetBox, \"x_ninja\", EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_WhiteFeet, &FeetBox, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientAutoExecuteTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceBlock(Source, "auto LayoutAutoExecuteSection", "auto LayoutVotingSection");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TClientAutoExecuteTextInputCtx;");
	const size_t UiPos = Body.find("TClientAutoExecuteTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t AnimPos = Body.find("TClientAutoExecuteTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
	const size_t TreePos = Body.find("TClientAutoExecuteTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
	const size_t ScopePos = Body.find("TClientAutoExecuteTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_auto_execute_text_inputs\");", TreePos);
	const size_t FrameDtPos = Body.find("TClientAutoExecuteTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(AnimPos, std::string::npos);
	EXPECT_NE(TreePos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(FrameDtPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, AnimPos);
	EXPECT_LT(AnimPos, TreePos);
	EXPECT_LT(TreePos, ScopePos);
	EXPECT_LT(ScopePos, FrameDtPos);
	const size_t BeforeConnectInputPos = Body.find("static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));");
	const size_t BeforeConnectTextFieldPos = Body.find("ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);", BeforeConnectInputPos);
	const size_t OnConnectInputPos = Body.find("static CLineInput s_LineInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));", BeforeConnectTextFieldPos);
	const size_t OnConnectTextFieldPos = Body.find("ui_widget::InputField(TClientAutoExecuteTextInputCtx, &s_LineInput, Button, nullptr, EditBoxFontSize);", OnConnectInputPos);
	EXPECT_NE(BeforeConnectInputPos, std::string::npos);
	EXPECT_NE(BeforeConnectTextFieldPos, std::string::npos);
	EXPECT_NE(OnConnectInputPos, std::string::npos);
	EXPECT_NE(OnConnectTextFieldPos, std::string::npos);
	EXPECT_LT(BeforeConnectInputPos, BeforeConnectTextFieldPos);
	EXPECT_LT(BeforeConnectTextFieldPos, OnConnectInputPos);
	EXPECT_LT(OnConnectInputPos, OnConnectTextFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientVotingTextInputUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceBlock(Source, "auto LayoutVotingSection", "auto LayoutPetSection");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TClientVotingTextInputCtx;");
	const size_t UiPos = Body.find("TClientVotingTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t AnimPos = Body.find("TClientVotingTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
	const size_t TreePos = Body.find("TClientVotingTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
	const size_t ScopePos = Body.find("TClientVotingTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_voting_text_inputs\");", TreePos);
	const size_t FrameDtPos = Body.find("TClientVotingTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(AnimPos, std::string::npos);
	EXPECT_NE(TreePos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(FrameDtPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, AnimPos);
	EXPECT_LT(AnimPos, TreePos);
	EXPECT_LT(TreePos, ScopePos);
	EXPECT_LT(ScopePos, FrameDtPos);
	const size_t RowAllocatorPos = Body.find("CTClientSettingsRowAllocator Rows(CurrentColumn);");
	const size_t MessageRectPos = Body.find("VoteMessage = Rows.Next();", RowAllocatorPos);
	const size_t RenderGuardPos = Body.find("if(Render)", MessageRectPos);
	const size_t LabelPos = Body.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize(\"Message to send in chat:\"), FontSize, TEXTALIGN_ML);", RenderGuardPos);
	const size_t EmptyTextPos = Body.find("s_VoteMessage.SetEmptyText(Localize(\"Leave empty to disable\"));", LabelPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(TClientVotingTextInputCtx, &s_VoteMessage, VoteMessage, nullptr, EditBoxFontSize);", EmptyTextPos);
	EXPECT_NE(RowAllocatorPos, std::string::npos);
	EXPECT_NE(MessageRectPos, std::string::npos);
	EXPECT_NE(RenderGuardPos, std::string::npos);
	EXPECT_NE(LabelPos, std::string::npos);
	EXPECT_NE(EmptyTextPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_LT(RowAllocatorPos, MessageRectPos);
	EXPECT_LT(MessageRectPos, RenderGuardPos);
	EXPECT_LT(RenderGuardPos, LabelPos);
	EXPECT_LT(LabelPos, EmptyTextPos);
	EXPECT_LT(EmptyTextPos, TextFieldPos);
	EXPECT_EQ(Body.find("CurrentColumn.HSplitTop(LineSize + MarginExtraSmall, Render ? &VoteMessage"), std::string::npos);
	EXPECT_EQ(Body.find("ui_widget::InputField(TClientVotingTextInputCtx, &s_VoteMessage, VoteMessage, Localize(\"Leave empty to disable\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_VoteMessage, &VoteMessage, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientFinishNameTextInputUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceBlock(Source, "auto LayoutFinishNameSection", "std::vector<SSettingsSection> vRightSections");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TClientFinishNameTextInputCtx;");
	const size_t UiPos = Body.find("TClientFinishNameTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t AnimPos = Body.find("TClientFinishNameTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
	const size_t TreePos = Body.find("TClientFinishNameTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
	const size_t ScopePos = Body.find("TClientFinishNameTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_finish_name_text_inputs\");", TreePos);
	const size_t FrameDtPos = Body.find("TClientFinishNameTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(AnimPos, std::string::npos);
	EXPECT_NE(TreePos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(FrameDtPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, AnimPos);
	EXPECT_LT(AnimPos, TreePos);
	EXPECT_LT(TreePos, ScopePos);
	EXPECT_LT(ScopePos, FrameDtPos);
	const size_t RowAllocatorPos = Body.find("CTClientSettingsRowAllocator Rows(CurrentColumn);");
	const size_t ToggleRowPos = Body.find("CUIRect ToggleRow = Rows.Next();", RowAllocatorPos);
	const size_t InputBoxPos = Body.find("FinishNameBox = Rows.Next();", ToggleRowPos);
	const size_t RenderGuardPos = Body.find("if(Render)", InputBoxPos);
	const size_t LabelPos = Body.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, nullptr, &Label, Localize(\"Finish Name:\"), FontSize, TEXTALIGN_ML);", RenderGuardPos);
	const size_t InputPos = Body.find("static CLineInput s_FinishName(g_Config.m_TcFinishName, sizeof(g_Config.m_TcFinishName));", LabelPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(TClientFinishNameTextInputCtx, &s_FinishName, Button, nullptr, EditBoxFontSize);", InputPos);
	EXPECT_NE(RowAllocatorPos, std::string::npos);
	EXPECT_NE(ToggleRowPos, std::string::npos);
	EXPECT_NE(InputBoxPos, std::string::npos);
	EXPECT_NE(RenderGuardPos, std::string::npos);
	EXPECT_NE(LabelPos, std::string::npos);
	EXPECT_NE(InputPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_LT(RowAllocatorPos, ToggleRowPos);
	EXPECT_LT(ToggleRowPos, InputBoxPos);
	EXPECT_LT(InputBoxPos, RenderGuardPos);
	EXPECT_LT(RenderGuardPos, LabelPos);
	EXPECT_LT(LabelPos, InputPos);
	EXPECT_LT(InputPos, TextFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_FinishName, &Button, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientStatusSchemeTextInputUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientStatusSchemeTextInputCtx = SettingsUiContext(\"settings_tclient_status_scheme_text_inputs\", UiScale);"), std::string::npos);
	const size_t LabelPos = Body.find("DoSettingsMenuLabel(SETTINGS_TCLIENT, TCLIENT_TAB_STATUSBAR, TCLIENT_TAB_STATUSBAR, \"tclient-statusbar-scheme-label\", &SchemeLabel, Localize(\"Status Scheme:\"), FontSize, TEXTALIGN_MR);");
	const size_t InputPos = Body.find("static CLineInput s_StatusScheme(g_Config.m_TcStatusBarScheme, sizeof(g_Config.m_TcStatusBarScheme));");
	const size_t EmptyTextPos = Body.find("s_StatusScheme.SetEmptyText(\"\");", InputPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(TClientStatusSchemeTextInputCtx, &s_StatusScheme, SchemeInput, nullptr, EditBoxFontSize);", EmptyTextPos);
	EXPECT_NE(LabelPos, std::string::npos);
	EXPECT_NE(InputPos, std::string::npos);
	EXPECT_NE(EmptyTextPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_LT(InputPos, LabelPos);
	EXPECT_LT(InputPos, EmptyTextPos);
	EXPECT_LT(EmptyTextPos, TextFieldPos);
	EXPECT_EQ(Body.find("ui_widget::InputField(TClientStatusSchemeTextInputCtx, &s_StatusScheme, StatusScheme, \"\", EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_StatusScheme, &StatusScheme, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientConfigEditorTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceBlock(Source, "CUIRect TopLine, Below;", "else if(pVar->m_Type == SConfigVariable::VAR_COLOR)");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TClientConfigTextInputCtx;");
	const size_t UiPos = Body.find("TClientConfigTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t AnimPos = Body.find("TClientConfigTextInputCtx.m_pAnim = &GameClient()->UiRuntimeV2()->AnimRuntime();", UiPos);
	const size_t TreePos = Body.find("TClientConfigTextInputCtx.m_pTree = &GameClient()->UiRuntimeV2()->Tree();", AnimPos);
	const size_t ScopePos = Body.find("TClientConfigTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tclient_config_text_inputs\");", TreePos);
	const size_t FrameDtPos = Body.find("TClientConfigTextInputCtx.m_FrameDt = GameClient()->UiRuntimeV2()->FrameDt();", ScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(AnimPos, std::string::npos);
	EXPECT_NE(TreePos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(FrameDtPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, AnimPos);
	EXPECT_LT(AnimPos, TreePos);
	EXPECT_LT(TreePos, ScopePos);
	EXPECT_LT(ScopePos, FrameDtPos);

	const size_t IntInputBoxPos = Body.find("Controls.VSplitLeft(60.0f, &InputBox, &Dummy);");
	const size_t IntTextFieldPos = Body.find("if(ui_widget::InputField(TClientConfigTextInputCtx, &State.m_Input, InputBox, nullptr, EditBoxFontSize))", IntInputBoxPos);
	const size_t IntReadPos = Body.find("int NewVal = State.m_Input.GetInteger();", IntTextFieldPos);
	EXPECT_NE(IntInputBoxPos, std::string::npos);
	EXPECT_NE(IntTextFieldPos, std::string::npos);
	EXPECT_NE(IntReadPos, std::string::npos);
	EXPECT_LT(IntInputBoxPos, IntTextFieldPos);
	EXPECT_LT(IntTextFieldPos, IntReadPos);

	const size_t StrTypePos = Body.find("else if(pVar->m_Type == SConfigVariable::VAR_STRING)");
	const size_t StrTextFieldPos = Body.find("if(ui_widget::InputField(TClientConfigTextInputCtx, &State.m_Input, Controls, nullptr, EditBoxFontSize))", StrTypePos);
	const size_t StrReadPos = Body.find("const char *NewVal = State.m_Input.GetString();", StrTextFieldPos);
	EXPECT_NE(StrTypePos, std::string::npos);
	EXPECT_NE(StrTextFieldPos, std::string::npos);
	EXPECT_NE(StrReadPos, std::string::npos);
	EXPECT_LT(StrTypePos, StrTextFieldPos);
	EXPECT_LT(StrTextFieldPos, StrReadPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&State.m_Input, &InputBox, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&State.m_Input, &Controls, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientBindWheelTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientBindWheelTextInputCtx = SettingsUiContext(\"settings_tclient_bindwheel_text_inputs\", UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientBindWheelTextInputCtx, &s_NameInput, Button, Localize(\"Name\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(TClientBindWheelTextInputCtx, &s_BindInput, Button, Localize(\"Command\"), EditBoxFontSize);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientChatBindsTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	const size_t TextFieldPos = Body.find("ui_widget::InputField(TClientChatBindsTextInputCtx, &BindDefault.m_LineInput, Input, BindDefault.m_Bind.m_aName, EditBoxFontSize)");
	const size_t ActiveGuardPos = Body.find("&& BindDefault.m_LineInput.IsActive()", TextFieldPos);
	const size_t ReadOnlyGuardPos = Source.find("if(!ReadOnly && ui_widget::InputField(TClientChatBindsTextInputCtx");
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext TClientChatBindsTextInputCtx = SettingsUiContext(\"settings_tclient_chatbinds_text_inputs\", UiScale);"), std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_NE(ActiveGuardPos, std::string::npos);
	EXPECT_NE(ReadOnlyGuardPos, std::string::npos);
	EXPECT_LT(TextFieldPos, ActiveGuardPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&BindDefault.m_LineInput, &Input, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, ControlsQuickSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenusSettingsControls::Render(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext ControlsSearchCtx = GameClient()->m_Menus.SettingsUiContext(\"settings_controls_search\", FONT_SIZE / ui_token::font::BODY);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(ControlsSearchCtx, &m_FilterInput, QuickSearch, FONT_SIZE"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&m_FilterInput, &QuickSearch, FONT_SIZE"), std::string::npos);
}

TEST(QmMonitoringHelpers, TeeSkinSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t SearchPos = Body.find("ui_widget::InputField(TeeSkinSearchCtx, &s_SkinFilterInput, QuickSearch, SkinSearchOptions).m_Changed");
	const size_t RefreshPos = Body.find("SkinList.ForceRefresh();", SearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext TeeSkinSearchCtx = SettingsUiContext(\"settings_tee_skin_search\", UiScale);"), std::string::npos);
	EXPECT_NE(SearchPos, std::string::npos);
	EXPECT_NE(RefreshPos, std::string::npos);
	EXPECT_LT(SearchPos, RefreshPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, SkinFlagLanguageBusinessItemsStayNonCardAndUseSharedRuntime)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string PlayerBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	const std::string TeeBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	const std::string LanguageBody = ExtractSourceFunctionBody(Source, "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)");
	ASSERT_FALSE(PlayerBody.empty());
	ASSERT_FALSE(TeeBody.empty());
	ASSERT_FALSE(LanguageBody.empty());

	EXPECT_NE(PlayerBody.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);"), std::string::npos);
	EXPECT_NE(PlayerBody.find("s_ListBox.DoStart(48.0f * UiScale, s_vpFilteredFlags.size(), 10, 2"), std::string::npos);
	EXPECT_NE(TeeBody.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_GRID);"), std::string::npos);
	EXPECT_NE(TeeBody.find("ui_widget::InputField(TeeSkinSearchCtx"), std::string::npos);
	EXPECT_NE(LanguageBody.find("ScrollRequest.m_Profile = EQmScrollProfile::SETTINGS_INNER;"), std::string::npos);
	EXPECT_NE(LanguageBody.find("ScrollRequest.m_RowExtent = Metrics.m_ListRowHeight;"), std::string::npos);
	EXPECT_NE(LanguageBody.find("ScrollRequest.m_RowsPerStep = 3;"), std::string::npos);
	EXPECT_EQ(LanguageBody.find("QmSettingsScrollRegionParams("), std::string::npos);
	EXPECT_EQ(PlayerBody.find("SettingsCard("), std::string::npos);
	EXPECT_EQ(LanguageBody.find("SettingsCard("), std::string::npos);
	EXPECT_NE(Source.find("QM_TEE_PREVIEW_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(Source.find("QM_LANGUAGE_ROW_CACHE_CAPACITY"), std::string::npos);
}

TEST(QmMonitoringHelpers, TeeSkinClearableInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t PrefixInputPos = Body.find("static CLineInput s_SkinPrefixInput(g_Config.m_ClSkinPrefix, sizeof(g_Config.m_ClSkinPrefix));");
	const size_t PrefixCtxPos = Body.find("IUiContext TeeSkinPrefixTextInputCtx;", PrefixInputPos);
	const size_t PrefixUiPos = Body.find("TeeSkinPrefixTextInputCtx.m_pUi = Ui();", PrefixCtxPos);
	const size_t PrefixScopePos = Body.find("TeeSkinPrefixTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tee_skin_prefix_text_input\");", PrefixUiPos);
	const size_t PrefixOptionsPos = Body.find("ui_widget::SInputFieldOptions SkinPrefixInputOptions;", PrefixScopePos);
	const size_t PrefixClearablePos = Body.find("SkinPrefixInputOptions.m_Clearable = true;", PrefixOptionsPos);
	const size_t PrefixFieldPos = Body.find("ui_widget::InputField(TeeSkinPrefixTextInputCtx, &s_SkinPrefixInput, Button, SkinPrefixInputOptions).m_Changed", PrefixClearablePos);
	const size_t PrefixRefreshPos = Body.find("ShouldRefresh = true;", PrefixFieldPos);
	const size_t SkinInputPos = Body.find("static CLineInput s_SkinInput;", PrefixRefreshPos);
	const size_t SkinBufferPos = Body.find("s_SkinInput.SetBuffer(pSkinName, SkinNameSize);", SkinInputPos);
	const size_t SkinEmptyTextPos = Body.find("s_SkinInput.SetEmptyText(\"default\");", SkinBufferPos);
	const size_t SkinCtxPos = Body.find("IUiContext TeeSkinNameTextInputCtx;", SkinEmptyTextPos);
	const size_t SkinUiPos = Body.find("TeeSkinNameTextInputCtx.m_pUi = Ui();", SkinCtxPos);
	const size_t SkinScopePos = Body.find("TeeSkinNameTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tee_skin_name_text_input\");", SkinUiPos);
	const size_t SkinOptionsPos = Body.find("ui_widget::SInputFieldOptions SkinNameInputOptions;", SkinScopePos);
	const size_t SkinClearablePos = Body.find("SkinNameInputOptions.m_Clearable = true;", SkinOptionsPos);
	const size_t SkinFieldPos = Body.find("ui_widget::InputField(TeeSkinNameTextInputCtx, &s_SkinInput, Button, SkinNameInputOptions).m_Changed", SkinClearablePos);
	const size_t NeedSendInfoPos = Body.find("SetNeedSendInfo();", SkinFieldPos);
	const size_t ScrollSelectedPos = Body.find("m_SkinListScrollToSelected = true;", NeedSendInfoPos);
	const size_t ForceRefreshPos = Body.find("SkinList.ForceRefresh();", ScrollSelectedPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(PrefixInputPos, std::string::npos);
	EXPECT_NE(PrefixCtxPos, std::string::npos);
	EXPECT_NE(PrefixUiPos, std::string::npos);
	EXPECT_NE(PrefixScopePos, std::string::npos);
	EXPECT_NE(PrefixOptionsPos, std::string::npos);
	EXPECT_NE(PrefixClearablePos, std::string::npos);
	EXPECT_NE(PrefixFieldPos, std::string::npos);
	EXPECT_NE(PrefixRefreshPos, std::string::npos);
	EXPECT_LT(PrefixInputPos, PrefixCtxPos);
	EXPECT_LT(PrefixCtxPos, PrefixUiPos);
	EXPECT_LT(PrefixUiPos, PrefixScopePos);
	EXPECT_LT(PrefixScopePos, PrefixFieldPos);
	EXPECT_LT(PrefixFieldPos, PrefixRefreshPos);
	EXPECT_NE(SkinInputPos, std::string::npos);
	EXPECT_NE(SkinBufferPos, std::string::npos);
	EXPECT_NE(SkinEmptyTextPos, std::string::npos);
	EXPECT_NE(SkinCtxPos, std::string::npos);
	EXPECT_NE(SkinUiPos, std::string::npos);
	EXPECT_NE(SkinScopePos, std::string::npos);
	EXPECT_NE(SkinOptionsPos, std::string::npos);
	EXPECT_NE(SkinClearablePos, std::string::npos);
	EXPECT_NE(SkinFieldPos, std::string::npos);
	EXPECT_NE(NeedSendInfoPos, std::string::npos);
	EXPECT_NE(ScrollSelectedPos, std::string::npos);
	EXPECT_NE(ForceRefreshPos, std::string::npos);
	EXPECT_LT(SkinInputPos, SkinBufferPos);
	EXPECT_LT(SkinBufferPos, SkinEmptyTextPos);
	EXPECT_LT(SkinEmptyTextPos, SkinCtxPos);
	EXPECT_LT(SkinCtxPos, SkinUiPos);
	EXPECT_LT(SkinUiPos, SkinScopePos);
	EXPECT_LT(SkinScopePos, SkinFieldPos);
	EXPECT_LT(SkinFieldPos, NeedSendInfoPos);
	EXPECT_LT(NeedSendInfoPos, ScrollSelectedPos);
	EXPECT_LT(ScrollSelectedPos, ForceRefreshPos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_SkinPrefixInput, &Button, 14.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_SkinInput, &Button, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, SkinQueuePresetRenamePopupUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupSkinQueuePresetRename(void *pContext, CUIRect View, bool Active)");
	ASSERT_FALSE(Body.empty());

	const size_t LabelPos = Body.find("pMenus->DoSettingsMenuLabel(SETTINGS_TEE, -1, -1, \"tee-skin-queue-new-preset-name\", &Label, Localize(\"New preset name\"), FontSize, TEXTALIGN_ML);");
	const size_t TextInputCtxPos = Body.find("IUiContext SkinQueuePresetRenameTextInputCtx;", LabelPos);
	const size_t TextInputUiPos = Body.find("SkinQueuePresetRenameTextInputCtx.m_pUi = pMenus->Ui();", TextInputCtxPos);
	const size_t TextInputScopePos = Body.find("SkinQueuePresetRenameTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_skin_queue_preset_rename_text_input\");", TextInputUiPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(SkinQueuePresetRenameTextInputCtx, &pPopupContext->m_NameInput, Input, nullptr, FontSize + 1.0f);", TextInputScopePos);
	const size_t CancelPressedPos = Body.find("const bool CancelPressed", TextFieldPos);
	const size_t EscapeHotkeyPos = Body.find("pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)", CancelPressedPos);
	const size_t ConfirmPressedPos = Body.find("const bool ConfirmPressed", TextFieldPos);
	const size_t EnterHotkeyPos = Body.find("pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)", ConfirmPressedPos);
	const size_t RenamePos = Body.find("pMenus->GameClient()->m_Skins.RenameSkinQueuePreset((size_t)pPopupContext->m_PresetIndex, pPopupContext->m_NameInput.GetString(), pPopupContext->m_Dummy)", ConfirmPressedPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(LabelPos, std::string::npos);
	EXPECT_NE(TextInputCtxPos, std::string::npos);
	EXPECT_NE(TextInputUiPos, std::string::npos);
	EXPECT_NE(TextInputScopePos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_NE(CancelPressedPos, std::string::npos);
	EXPECT_NE(EscapeHotkeyPos, std::string::npos);
	EXPECT_NE(ConfirmPressedPos, std::string::npos);
	EXPECT_NE(EnterHotkeyPos, std::string::npos);
	EXPECT_NE(RenamePos, std::string::npos);
	EXPECT_LT(LabelPos, TextInputCtxPos);
	EXPECT_LT(TextInputCtxPos, TextInputUiPos);
	EXPECT_LT(TextInputUiPos, TextInputScopePos);
	EXPECT_LT(TextInputScopePos, TextFieldPos);
	EXPECT_LT(TextFieldPos, CancelPressedPos);
	EXPECT_LT(CancelPressedPos, EscapeHotkeyPos);
	EXPECT_LT(TextFieldPos, ConfirmPressedPos);
	EXPECT_LT(ConfirmPressedPos, EnterHotkeyPos);
	EXPECT_LT(ConfirmPressedPos, RenamePos);
	EXPECT_EQ(Body.find("pMenus->Ui()->DoEditBox(&pPopupContext->m_NameInput, &Input, FontSize + 1.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, DDNetSettingsTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t BackgroundInputPos = Body.find("static CLineInput s_BackgroundEntitiesInput(g_Config.m_ClBackgroundEntities, sizeof(g_Config.m_ClBackgroundEntities));");
	const size_t BackgroundWasActivePos = Body.find("const bool WasInputActive = s_BackgroundEntitiesInput.IsActive();", BackgroundInputPos);
	const size_t BackgroundCtxPos = Body.find("IUiContext DDNetBackgroundEntitiesTextInputCtx;", BackgroundWasActivePos);
	const size_t BackgroundUiPos = Body.find("DDNetBackgroundEntitiesTextInputCtx.m_pUi = Ui();", BackgroundCtxPos);
	const size_t BackgroundScopePos = Body.find("DDNetBackgroundEntitiesTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_ddnet_background_entities_text_input\");", BackgroundUiPos);
	const size_t BackgroundFieldPos = Body.find("const bool InputCommitted = ui_widget::InputField(DDNetBackgroundEntitiesTextInputCtx, &s_BackgroundEntitiesInput, EditBox, nullptr, BodySize);", BackgroundScopePos);
	const size_t BackgroundApplyPos = Body.find("BackgroundChanged = ApplyBackgroundEntitiesInputValue(s_BackgroundEntitiesInput);", BackgroundFieldPos);
	const size_t BackgroundBlurPos = Body.find("ShouldCommitBackgroundEntitiesInputOnBlur(WasInputActive, s_BackgroundEntitiesInput.IsActive(), s_BackgroundEntitiesInput.GetString(), s_aBackgroundEntitiesSync)", BackgroundApplyPos);
	const size_t RunOnJoinInputPos = Body.find("static CLineInput s_RunOnJoinInput(g_Config.m_ClRunOnJoin, sizeof(g_Config.m_ClRunOnJoin));");
	const size_t RunOnJoinEmptyTextPos = Body.find("s_RunOnJoinInput.SetEmptyText(Localize(\"Chat command (e.g. showall 1)\"));", RunOnJoinInputPos);
	const size_t RunOnJoinCtxPos = Body.find("IUiContext DDNetRunOnJoinTextInputCtx;", RunOnJoinEmptyTextPos);
	const size_t RunOnJoinUiPos = Body.find("DDNetRunOnJoinTextInputCtx.m_pUi = Ui();", RunOnJoinCtxPos);
	const size_t RunOnJoinScopePos = Body.find("DDNetRunOnJoinTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_ddnet_run_on_join_text_input\");", RunOnJoinUiPos);
	const size_t RunOnJoinFieldPos = Body.find("ui_widget::InputField(DDNetRunOnJoinTextInputCtx, &s_RunOnJoinInput, Button, nullptr, BodySize);", RunOnJoinScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(BackgroundInputPos, std::string::npos);
	EXPECT_NE(BackgroundWasActivePos, std::string::npos);
	EXPECT_NE(BackgroundCtxPos, std::string::npos);
	EXPECT_NE(BackgroundUiPos, std::string::npos);
	EXPECT_NE(BackgroundScopePos, std::string::npos);
	EXPECT_NE(BackgroundFieldPos, std::string::npos);
	EXPECT_NE(BackgroundApplyPos, std::string::npos);
	EXPECT_NE(BackgroundBlurPos, std::string::npos);
	EXPECT_LT(BackgroundInputPos, BackgroundWasActivePos);
	EXPECT_LT(BackgroundWasActivePos, BackgroundCtxPos);
	EXPECT_LT(BackgroundCtxPos, BackgroundUiPos);
	EXPECT_LT(BackgroundUiPos, BackgroundScopePos);
	EXPECT_LT(BackgroundScopePos, BackgroundFieldPos);
	EXPECT_LT(BackgroundFieldPos, BackgroundApplyPos);
	EXPECT_LT(BackgroundApplyPos, BackgroundBlurPos);
	EXPECT_NE(RunOnJoinInputPos, std::string::npos);
	EXPECT_NE(RunOnJoinEmptyTextPos, std::string::npos);
	EXPECT_NE(RunOnJoinCtxPos, std::string::npos);
	EXPECT_NE(RunOnJoinUiPos, std::string::npos);
	EXPECT_NE(RunOnJoinScopePos, std::string::npos);
	EXPECT_NE(RunOnJoinFieldPos, std::string::npos);
	EXPECT_LT(RunOnJoinInputPos, RunOnJoinEmptyTextPos);
	EXPECT_LT(RunOnJoinEmptyTextPos, RunOnJoinCtxPos);
	EXPECT_LT(RunOnJoinCtxPos, RunOnJoinUiPos);
	EXPECT_LT(RunOnJoinUiPos, RunOnJoinScopePos);
	EXPECT_LT(RunOnJoinScopePos, RunOnJoinFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_BackgroundEntitiesInput, &EditBox, 14.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_RunOnJoinInput, &Button, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, TouchControlsLayoutInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "bool CMenusIngameTouchControls::RenderLayoutSettingBlock(CUIRect Block)");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TouchControlsLayoutTextInputCtx;");
	const size_t UiPos = Body.find("TouchControlsLayoutTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t ScopePos = Body.find("TouchControlsLayoutTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"touch_controls_layout_text_inputs\");", UiPos);
	const size_t XFieldPos = Body.find("ui_widget::InputField(TouchControlsLayoutTextInputCtx, &m_InputX, EditBox, LayoutInputOptions).m_Changed", ScopePos);
	const size_t XUpdatePos = Body.find("InputPosFunction(&m_InputX);", XFieldPos);
	const size_t YFieldPos = Body.find("ui_widget::InputField(TouchControlsLayoutTextInputCtx, &m_InputY, EditBox, LayoutInputOptions).m_Changed", XUpdatePos);
	const size_t YUpdatePos = Body.find("InputPosFunction(&m_InputY);", YFieldPos);
	const size_t WFieldPos = Body.find("ui_widget::InputField(TouchControlsLayoutTextInputCtx, &m_InputW, EditBox, LayoutInputOptions).m_Changed", YUpdatePos);
	const size_t WUpdatePos = Body.find("InputPosFunction(&m_InputW);", WFieldPos);
	const size_t HFieldPos = Body.find("ui_widget::InputField(TouchControlsLayoutTextInputCtx, &m_InputH, EditBox, LayoutInputOptions).m_Changed", WUpdatePos);
	const size_t HUpdatePos = Body.find("InputPosFunction(&m_InputH);", HFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(Body.find("LayoutInputOptions.m_Clearable = true;", ScopePos), std::string::npos);
	EXPECT_NE(Body.find("LayoutInputOptions.m_FontSize = FONTSIZE;", ScopePos), std::string::npos);
	EXPECT_NE(XFieldPos, std::string::npos);
	EXPECT_NE(XUpdatePos, std::string::npos);
	EXPECT_NE(YFieldPos, std::string::npos);
	EXPECT_NE(YUpdatePos, std::string::npos);
	EXPECT_NE(WFieldPos, std::string::npos);
	EXPECT_NE(WUpdatePos, std::string::npos);
	EXPECT_NE(HFieldPos, std::string::npos);
	EXPECT_NE(HUpdatePos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, ScopePos);
	EXPECT_LT(ScopePos, XFieldPos);
	EXPECT_LT(XFieldPos, XUpdatePos);
	EXPECT_LT(XUpdatePos, YFieldPos);
	EXPECT_LT(YFieldPos, YUpdatePos);
	EXPECT_LT(YUpdatePos, WFieldPos);
	EXPECT_LT(WFieldPos, WUpdatePos);
	EXPECT_LT(WUpdatePos, HFieldPos);
	EXPECT_LT(HFieldPos, HUpdatePos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_InputX, &EditBox, FONTSIZE"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_InputY, &EditBox, FONTSIZE"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_InputW, &EditBox, FONTSIZE"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_InputH, &EditBox, FONTSIZE"), std::string::npos);
}

TEST(QmMonitoringHelpers, TouchControlsBehaviorInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "bool CMenusIngameTouchControls::RenderBehaviorSettingBlock(CUIRect Block)");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TouchControlsBehaviorTextInputCtx;");
	const size_t UiPos = Body.find("TouchControlsBehaviorTextInputCtx.m_pUi = Ui();", CtxPos);
	const size_t ScopePos = Body.find("TouchControlsBehaviorTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"touch_controls_behavior_text_inputs\");", UiPos);
	const size_t BindCommandFieldPos = Body.find("ui_widget::InputField(TouchControlsBehaviorTextInputCtx, &m_vBehaviorElements[0]->m_InputCommand, MiddleButton, BehaviorInputOptions).m_Changed", ScopePos);
	const size_t BindCommandUpdatePos = Body.find("m_vBehaviorElements[0]->UpdateCommand();", BindCommandFieldPos);
	const size_t BindLabelFieldPos = Body.find("ui_widget::InputField(TouchControlsBehaviorTextInputCtx, &m_vBehaviorElements[0]->m_InputLabel, MiddleButton, BehaviorInputOptions).m_Changed", BindCommandUpdatePos);
	const size_t BindLabelUpdatePos = Body.find("m_vBehaviorElements[0]->UpdateLabel();", BindLabelFieldPos);
	const size_t ToggleCommandFieldPos = Body.find("ui_widget::InputField(TouchControlsBehaviorTextInputCtx, &m_vBehaviorElements[CommandIndex]->m_InputCommand, MiddleButton, BehaviorInputOptions).m_Changed", BindLabelUpdatePos);
	const size_t ToggleCommandUpdatePos = Body.find("m_vBehaviorElements[CommandIndex]->UpdateCommand();", ToggleCommandFieldPos);
	const size_t ToggleLabelFieldPos = Body.find("ui_widget::InputField(TouchControlsBehaviorTextInputCtx, &m_vBehaviorElements[CommandIndex]->m_InputLabel, MiddleButton, BehaviorInputOptions).m_Changed", ToggleCommandUpdatePos);
	const size_t ToggleLabelUpdatePos = Body.find("m_vBehaviorElements[CommandIndex]->UpdateLabel();", ToggleLabelFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(Body.find("BehaviorInputOptions.m_Clearable = true;", ScopePos), std::string::npos);
	EXPECT_NE(Body.find("BehaviorInputOptions.m_FontSize = 10.0f;", ScopePos), std::string::npos);
	EXPECT_NE(BindCommandFieldPos, std::string::npos);
	EXPECT_NE(BindCommandUpdatePos, std::string::npos);
	EXPECT_NE(BindLabelFieldPos, std::string::npos);
	EXPECT_NE(BindLabelUpdatePos, std::string::npos);
	EXPECT_NE(ToggleCommandFieldPos, std::string::npos);
	EXPECT_NE(ToggleCommandUpdatePos, std::string::npos);
	EXPECT_NE(ToggleLabelFieldPos, std::string::npos);
	EXPECT_NE(ToggleLabelUpdatePos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, ScopePos);
	EXPECT_LT(ScopePos, BindCommandFieldPos);
	EXPECT_LT(BindCommandFieldPos, BindCommandUpdatePos);
	EXPECT_LT(BindCommandUpdatePos, BindLabelFieldPos);
	EXPECT_LT(BindLabelFieldPos, BindLabelUpdatePos);
	EXPECT_LT(BindLabelUpdatePos, ToggleCommandFieldPos);
	EXPECT_LT(ToggleCommandFieldPos, ToggleCommandUpdatePos);
	EXPECT_LT(ToggleCommandUpdatePos, ToggleLabelFieldPos);
	EXPECT_LT(ToggleLabelFieldPos, ToggleLabelUpdatePos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_vBehaviorElements[0]->m_InputCommand, &MiddleButton, 10.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_vBehaviorElements[0]->m_InputLabel, &MiddleButton, 10.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_vBehaviorElements[CommandIndex]->m_InputCommand, &MiddleButton, 10.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_vBehaviorElements[CommandIndex]->m_InputLabel, &MiddleButton, 10.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, TouchControlsButtonBrowserSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenusIngameTouchControls::RenderTouchButtonBrowser(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t CtxPos = Body.find("IUiContext TouchControlsBrowserSearchCtx;");
	const size_t UiPos = Body.find("TouchControlsBrowserSearchCtx.m_pUi = Ui();", CtxPos);
	const size_t ScopePos = Body.find("TouchControlsBrowserSearchCtx.m_ScopeHash = MakeUiScopeHash(\"touch_controls_button_browser_search\");", UiPos);
	const size_t SearchFieldPos = Body.find("ui_widget::InputField(TouchControlsBrowserSearchCtx, &m_FilterInput, EditBox, BrowserSearchOptions).m_Changed", ScopePos);
	const size_t NeedFilterPos = Body.find("m_NeedFilter = true;", SearchFieldPos);
	const size_t SearchLabelPos = Body.find("str_format(aBufSearch, sizeof(aBufSearch), \"%s:\", Localize(\"Search\"));");
	const size_t ManualSearchIconPos = Body.find("FontIcons::FONT_ICON_MAGNIFYING_GLASS");
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CtxPos, std::string::npos);
	EXPECT_NE(UiPos, std::string::npos);
	EXPECT_NE(ScopePos, std::string::npos);
	EXPECT_NE(Body.find("BrowserSearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;", ScopePos), std::string::npos);
	EXPECT_NE(Body.find("BrowserSearchOptions.m_Clearable = true;", ScopePos), std::string::npos);
	EXPECT_NE(Body.find("BrowserSearchOptions.m_SearchHotkeyEnabled = !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive();", ScopePos), std::string::npos);
	EXPECT_NE(Body.find("BrowserSearchOptions.m_FontSize = FONTSIZE;", ScopePos), std::string::npos);
	EXPECT_NE(SearchFieldPos, std::string::npos);
	EXPECT_NE(NeedFilterPos, std::string::npos);
	EXPECT_NE(SearchLabelPos, std::string::npos);
	EXPECT_EQ(ManualSearchIconPos, std::string::npos);
	EXPECT_LT(CtxPos, UiPos);
	EXPECT_LT(UiPos, ScopePos);
	EXPECT_LT(ScopePos, SearchFieldPos);
	EXPECT_LT(SearchFieldPos, NeedFilterPos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_FilterInput, &EditBox, FONTSIZE"), std::string::npos);
}

TEST(QmMonitoringHelpers, Tee7SkinSearchUsesSharedQmInputField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings7.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTee7Content(CUIRect MainView, const SSettingsContentMetrics &Metrics)");
	ASSERT_FALSE(Body.empty());

	const size_t SearchPos = Body.find("ui_widget::InputField(Tee7SkinSearchCtx, &s_SkinFilterInput, QuickSearch, SkinSearchOptions).m_Changed");
	const size_t RefreshPos = Body.find("m_SkinList7LastRefreshTime = std::nullopt;", SearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext Tee7SkinSearchCtx = SettingsUiContext(\"settings_tee7_skin_search\");"), std::string::npos);
	EXPECT_NE(SearchPos, std::string::npos);
	EXPECT_NE(RefreshPos, std::string::npos);
	EXPECT_LT(SearchPos, RefreshPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&s_SkinFilterInput, &QuickSearch, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, PlayerFlagSearchUsesSharedQmInputField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext PlayerFlagSearchCtx = SettingsUiContext(\"settings_player_flag_search\", UiScale);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(PlayerFlagSearchCtx, &s_FlagFilterInput, QuickSearch, FlagSearchOptions);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&s_FlagFilterInput, &QuickSearch, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, PlayerIdentityTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string IdentityBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTeeIdentity(CUIRect MainView, CUIRect *pFlagButton, float BodySize)");
	const std::string PlayerBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	ASSERT_FALSE(IdentityBody.empty());
	ASSERT_FALSE(PlayerBody.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(IdentityBody.find("IUiContext TeeIdentityTextInputCtx;"), std::string::npos);
	EXPECT_NE(IdentityBody.find("TeeIdentityTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_tee_identity_text_inputs\");"), std::string::npos);
	EXPECT_NE(IdentityBody.find("ui_widget::InputField(TeeIdentityTextInputCtx, &s_NameInput, NameInputRect, Client()->PlayerName(), BodySize)"), std::string::npos);
	EXPECT_NE(IdentityBody.find("ui_widget::InputField(TeeIdentityTextInputCtx, &s_ClanInput, ClanInput, \"\", BodySize)"), std::string::npos);
	EXPECT_EQ(IdentityBody.find("Ui()->DoEditBox(&s_NameInput, &NameInputRect, 14.0f"), std::string::npos);
	EXPECT_EQ(IdentityBody.find("Ui()->DoEditBox(&s_ClanInput, &ClanInput, 14.0f"), std::string::npos);

	EXPECT_NE(PlayerBody.find("const IUiContext PlayerIdentityTextInputCtx = SettingsUiContext(\"settings_player_identity_text_inputs\", UiScale);"), std::string::npos);
	EXPECT_NE(PlayerBody.find("ui_widget::InputField(PlayerIdentityTextInputCtx, &s_NameInput, Row, Client()->PlayerName(), BodySize)"), std::string::npos);
	EXPECT_NE(PlayerBody.find("ui_widget::InputField(PlayerIdentityTextInputCtx, &s_ClanInput, Row, \"\", BodySize)"), std::string::npos);
	EXPECT_EQ(PlayerBody.find("Ui()->DoEditBox(&s_NameInput, &Row, 14.0f"), std::string::npos);
	EXPECT_EQ(PlayerBody.find("Ui()->DoEditBox(&s_ClanInput, &Row, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameCallvoteSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerControl(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext CallvoteSearchCtx;"), std::string::npos);
	EXPECT_NE(Body.find("CallvoteSearchCtx.m_ScopeHash = MakeUiScopeHash(\"ingame_callvote_search\");"), std::string::npos);
	EXPECT_NE(Body.find("bool Searching = ui_widget::InputField(CallvoteSearchCtx, &m_FilterInput, QuickSearch, CallvoteSearchOptions).m_Changed;"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&m_FilterInput, &QuickSearch, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameCallvoteTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerControl(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext CallvoteTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("CallvoteTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"ingame_callvote_text_inputs\");"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(CallvoteTextInputCtx, &m_CallvoteReasonInput, Reason, ReasonInputOptions);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(CallvoteTextInputCtx, &s_VoteDescriptionInput, Button, VoteDescriptionInputOptions);"), std::string::npos);
	EXPECT_NE(Body.find("ui_widget::InputField(CallvoteTextInputCtx, &s_VoteCommandInput, Button, VoteCommandInputOptions);"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&m_CallvoteReasonInput, &Reason, 14.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_VoteDescriptionInput, &Button, 14.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_VoteCommandInput, &Button, 14.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameUnfinishedMapsPlayerNameUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderUnfinishedMaps(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t TextFieldPos = Body.find("if(ui_widget::InputField(UnfinishedMapsTextInputCtx, &s_PlayerNameInput, Row, PlayerNameInputOptions).m_Changed)");
	const size_t DirtyPos = Body.find("s_NameDirty = true;", TextFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext UnfinishedMapsTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("UnfinishedMapsTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"ingame_unfinished_maps_text_inputs\");"), std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_NE(DirtyPos, std::string::npos);
	EXPECT_LT(TextFieldPos, DirtyPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_PlayerNameInput, &Row, 12.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameMapNoteUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfo(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t TextFieldPos = Body.find("if(ui_widget::InputField(ServerInfoTextInputCtx, &s_MapNoteInput, NoteInput, MapNoteInputOptions).m_Changed)");
	const size_t SaveNotePos = Body.find("GameClient()->m_TClient.SetMapNote(CurrentServerInfo.m_aMap, s_MapNoteInput.GetString());", TextFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext ServerInfoTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("ServerInfoTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"ingame_server_info_text_inputs\");"), std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_NE(SaveNotePos, std::string::npos);
	EXPECT_LT(TextFieldPos, SaveNotePos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_MapNoteInput, &NoteInput, FontSizeBody * 0.85f"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoBrowserSearchUsesSharedQmInputField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderDemoBrowserButtons(CUIRect ButtonsView, bool WasListboxItemActivated)");
	ASSERT_FALSE(Body.empty());

	const size_t NewUiSearchPos = Body.find("ui_widget::InputField(DemoBrowserSearchCtx, &m_DemoSearchInput, DemoSearch, NewUiSearchOptions).m_Changed");
	const size_t NewUiRefreshPos = Body.find("RefreshFilteredDemos();", NewUiSearchPos);
	const size_t LegacySearchPos = Body.find("ui_widget::InputField(DemoBrowserSearchCtx, &m_DemoSearchInput, DemoSearch, LegacySearchOptions).m_Changed");
	const size_t LegacyRefreshPos = Body.find("RefreshFilteredDemos();", LegacySearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext DemoBrowserSearchCtx = SettingsUiContext(\"demo_browser_search\");"), std::string::npos);
	EXPECT_NE(NewUiSearchPos, std::string::npos);
	EXPECT_NE(NewUiRefreshPos, std::string::npos);
	EXPECT_LT(NewUiSearchPos, NewUiRefreshPos);
	EXPECT_NE(LegacySearchPos, std::string::npos);
	EXPECT_NE(LegacyRefreshPos, std::string::npos);
	EXPECT_LT(LegacySearchPos, LegacyRefreshPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&m_DemoSearchInput, &DemoSearch"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerBrowserStatusInputsUseSharedQmFields)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)");
	ASSERT_FALSE(Body.empty());

	const size_t SearchHotkeyPos = Body.find("Input()->KeyPress(KEY_F) && Input()->ModifierIsPressed()");
	const size_t SearchPopupGuardPos = Body.find("!Ui()->IsPopupOpen()", SearchHotkeyPos > 40 ? SearchHotkeyPos - 40 : 0);
	const size_t SearchSetActivePos = Body.find("Ui()->SetActiveItem(&s_FilterInput)", SearchHotkeyPos);
	const size_t SearchSelectAllPos = Body.find("s_FilterInput.SelectAll();", SearchHotkeyPos);
	const size_t SearchFieldPos = Body.find("ui_widget::InputField(ServerBrowserSearchCtx, &s_FilterInput, QuickSearch, SearchOptions).m_Changed");
	const size_t SearchRefreshPos = Body.find("Client()->ServerBrowserUpdate();", SearchFieldPos);
	const size_t ExcludeHotkeyPos = Body.find("Input()->KeyPress(KEY_X) && Input()->ShiftIsPressed() && Input()->ModifierIsPressed()");
	const size_t ExcludePopupGuardPos = Body.find("!Ui()->IsPopupOpen()", ExcludeHotkeyPos > 40 ? ExcludeHotkeyPos - 40 : 0);
	const size_t ExcludeSetActivePos = Body.find("Ui()->SetActiveItem(&s_ExcludeInput)", ExcludeHotkeyPos);
	const size_t ExcludeSelectAllPos = Body.find("s_ExcludeInput.SelectAll();", ExcludeHotkeyPos);
	const size_t ExcludeFieldPos = Body.find("ui_widget::InputField(ServerBrowserExcludeCtx, &s_ExcludeInput, QuickExclude, SearchOptions).m_Changed");
	const size_t ExcludeRefreshPos = Body.find("Client()->ServerBrowserUpdate();", ExcludeFieldPos);
	const size_t AddressFieldPos = Body.find("ui_widget::InputField(ServerBrowserAddressCtx, &s_ServerAddressInput, ServerAddrEditBox, AddressOptions).m_Changed");
	const size_t RevealPos = Body.find("m_ServerBrowserShouldRevealSelection = true;", AddressFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext ServerBrowserSearchCtx = SettingsUiContext(\"server_browser_search\");"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext ServerBrowserExcludeCtx = SettingsUiContext(\"server_browser_exclude\");"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext ServerBrowserAddressCtx = SettingsUiContext(\"server_browser_address\");"), std::string::npos);
	EXPECT_NE(SearchHotkeyPos, std::string::npos);
	EXPECT_NE(SearchPopupGuardPos, std::string::npos);
	EXPECT_NE(SearchSetActivePos, std::string::npos);
	EXPECT_NE(SearchSelectAllPos, std::string::npos);
	EXPECT_NE(SearchFieldPos, std::string::npos);
	EXPECT_NE(SearchRefreshPos, std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoLabel(&QuickSearch, FONT_ICON_MAGNIFYING_GLASS"), std::string::npos);
	EXPECT_LT(SearchPopupGuardPos, SearchHotkeyPos);
	EXPECT_LT(SearchHotkeyPos, SearchFieldPos);
	EXPECT_LT(SearchHotkeyPos, SearchSetActivePos);
	EXPECT_LT(SearchSetActivePos, SearchSelectAllPos);
	EXPECT_LT(SearchSelectAllPos, SearchFieldPos);
	EXPECT_LT(SearchFieldPos, SearchRefreshPos);
	EXPECT_EQ(Body.find(";\n", SearchFieldPos), SearchRefreshPos + str_length("Client()->ServerBrowserUpdate()"));
	EXPECT_NE(ExcludeHotkeyPos, std::string::npos);
	EXPECT_NE(ExcludePopupGuardPos, std::string::npos);
	EXPECT_NE(ExcludeSetActivePos, std::string::npos);
	EXPECT_NE(ExcludeSelectAllPos, std::string::npos);
	EXPECT_NE(ExcludeFieldPos, std::string::npos);
	EXPECT_NE(ExcludeRefreshPos, std::string::npos);
	EXPECT_LT(ExcludePopupGuardPos, ExcludeHotkeyPos);
	EXPECT_LT(ExcludeHotkeyPos, ExcludeFieldPos);
	EXPECT_LT(ExcludeHotkeyPos, ExcludeSetActivePos);
	EXPECT_LT(ExcludeSetActivePos, ExcludeSelectAllPos);
	EXPECT_LT(ExcludeSelectAllPos, ExcludeFieldPos);
	EXPECT_LT(ExcludeFieldPos, ExcludeRefreshPos);
	EXPECT_EQ(Body.find(";\n", ExcludeFieldPos), ExcludeRefreshPos + str_length("Client()->ServerBrowserUpdate()"));
	EXPECT_NE(AddressFieldPos, std::string::npos);
	EXPECT_NE(RevealPos, std::string::npos);
	EXPECT_LT(AddressFieldPos, RevealPos);
	EXPECT_EQ(Body.find(";\n", AddressFieldPos), RevealPos + str_length("m_ServerBrowserShouldRevealSelection = true"));
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_FilterInput, &QuickSearch"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_ExcludeInput, &QuickExclude"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_ServerAddressInput, &ServerAddrEditBox"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerBrowserStatusInputsShareHorizontalBounds)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const float SearchExcludeAddrInputOffset = SearchExcludeAddrStrMax + 5.0f + ExcludeIconWidth + 5.0f;"), std::string::npos);
	EXPECT_NE(Body.find("QuickSearch.VSplitLeft(SearchExcludeAddrStrMax, &SearchLabel, nullptr);"), std::string::npos);
	EXPECT_NE(Body.find("QuickSearch.VSplitLeft(SearchExcludeAddrInputOffset, nullptr, &QuickSearch);"), std::string::npos);
	EXPECT_NE(Body.find("QuickExclude.VSplitLeft(ExcludeIconWidth + 5.0f, nullptr, &ExcludeLabel);"), std::string::npos);
	EXPECT_NE(Body.find("QuickExclude.VSplitLeft(SearchExcludeAddrInputOffset, nullptr, &QuickExclude);"), std::string::npos);
	EXPECT_NE(Body.find("ServerAddr.VSplitLeft(SearchExcludeAddrInputOffset, &ServerAddrLabel, &ServerAddrEditBox);"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerBrowserFiltersTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserFilters(CUIRect View)");
	ASSERT_FALSE(Body.empty());

	const size_t GameTypeInputPos = Body.find("static CLineInput s_GametypeInput(g_Config.m_BrFilterGametype, sizeof(g_Config.m_BrFilterGametype));");
	const size_t GameTypeCtxPos = Body.find("const IUiContext ServerBrowserGameTypeCtx = SettingsUiContext(\"server_browser_filter_game_type\");", GameTypeInputPos);
	const size_t GameTypeFieldPos = Body.find("ui_widget::InputField(ServerBrowserGameTypeCtx, &s_GametypeInput, Button, GameTypeOptions).m_Changed", GameTypeCtxPos);
	const size_t GameTypeRefreshPos = Body.find("Client()->ServerBrowserUpdate();", GameTypeFieldPos);
	const size_t AddressInputPos = Body.find("static CLineInput s_FilterServerAddressInput(g_Config.m_BrFilterServerAddress, sizeof(g_Config.m_BrFilterServerAddress));");
	const size_t AddressCtxPos = Body.find("const IUiContext ServerBrowserFilterAddressCtx = SettingsUiContext(\"server_browser_filter_address\");", AddressInputPos);
	const size_t AddressFieldPos = Body.find("ui_widget::InputField(ServerBrowserFilterAddressCtx, &s_FilterServerAddressInput, Button, FilterAddressOptions).m_Changed", AddressCtxPos);
	const size_t AddressRefreshPos = Body.find("Client()->ServerBrowserUpdate();", AddressFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(GameTypeInputPos, std::string::npos);
	EXPECT_NE(GameTypeCtxPos, std::string::npos);
	EXPECT_NE(GameTypeFieldPos, std::string::npos);
	EXPECT_NE(GameTypeRefreshPos, std::string::npos);
	EXPECT_LT(GameTypeInputPos, GameTypeCtxPos);
	EXPECT_LT(GameTypeCtxPos, GameTypeFieldPos);
	EXPECT_LT(GameTypeFieldPos, GameTypeRefreshPos);
	EXPECT_EQ(Body.find(";\n", GameTypeFieldPos), GameTypeRefreshPos + str_length("Client()->ServerBrowserUpdate()"));
	EXPECT_NE(AddressInputPos, std::string::npos);
	EXPECT_NE(AddressCtxPos, std::string::npos);
	EXPECT_NE(AddressFieldPos, std::string::npos);
	EXPECT_NE(AddressRefreshPos, std::string::npos);
	EXPECT_LT(AddressInputPos, AddressCtxPos);
	EXPECT_LT(AddressCtxPos, AddressFieldPos);
	EXPECT_LT(AddressFieldPos, AddressRefreshPos);
	EXPECT_EQ(Body.find(";\n", AddressFieldPos), AddressRefreshPos + str_length("Client()->ServerBrowserUpdate()"));
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_GametypeInput, &Button, FontSize"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_FilterServerAddressInput, &Button, FontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerBrowserAddFriendInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	ASSERT_FALSE(Body.empty());

	const size_t NameInputPos = Body.find("static CLineInputBuffered<MAX_NAME_LENGTH> s_NameInput;");
	const size_t TextInputCtxPos = Body.find("const IUiContext ServerBrowserAddFriendTextInputCtx = SettingsUiContext(\"server_browser_add_friend_text_inputs\");", NameInputPos);
	const size_t NameFieldPos = Body.find("ui_widget::InputField(ServerBrowserAddFriendTextInputCtx, &s_NameInput, Button, AddFriendInputOptions);", TextInputCtxPos);
	const size_t ClanInputPos = Body.find("static CLineInputBuffered<MAX_CLAN_LENGTH> s_ClanInput;", NameFieldPos);
	const size_t ClanFieldPos = Body.find("ui_widget::InputField(ServerBrowserAddFriendTextInputCtx, &s_ClanInput, Button, AddFriendInputOptions);", ClanInputPos);
	const size_t AddFriendPos = Body.find("GameClient()->Friends()->AddFriend(s_NameInput.GetString(), s_ClanInput.GetString(), pCategory);", ClanFieldPos);
	const size_t ClearNamePos = Body.find("s_NameInput.Clear();", AddFriendPos);
	const size_t ClearClanPos = Body.find("s_ClanInput.Clear();", ClearNamePos);
	const size_t FriendlistUpdatePos = Body.find("FriendlistOnUpdate();", ClearClanPos);
	const size_t ServerBrowserUpdatePos = Body.find("Client()->ServerBrowserUpdate();", FriendlistUpdatePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(NameInputPos, std::string::npos);
	EXPECT_NE(TextInputCtxPos, std::string::npos);
	EXPECT_NE(NameFieldPos, std::string::npos);
	EXPECT_NE(ClanInputPos, std::string::npos);
	EXPECT_NE(ClanFieldPos, std::string::npos);
	EXPECT_NE(AddFriendPos, std::string::npos);
	EXPECT_NE(ClearNamePos, std::string::npos);
	EXPECT_NE(ClearClanPos, std::string::npos);
	EXPECT_NE(FriendlistUpdatePos, std::string::npos);
	EXPECT_NE(ServerBrowserUpdatePos, std::string::npos);
	EXPECT_LT(NameInputPos, TextInputCtxPos);
	EXPECT_LT(TextInputCtxPos, NameFieldPos);
	EXPECT_LT(NameFieldPos, ClanInputPos);
	EXPECT_LT(ClanInputPos, ClanFieldPos);
	EXPECT_LT(ClanFieldPos, AddFriendPos);
	EXPECT_LT(AddFriendPos, ClearNamePos);
	EXPECT_LT(ClearNamePos, ClearClanPos);
	EXPECT_LT(ClearClanPos, FriendlistUpdatePos);
	EXPECT_LT(FriendlistUpdatePos, ServerBrowserUpdatePos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_NameInput, &Button, FontSize + 2.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_ClanInput, &Button, FontSize + 2.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerBrowserScrollRegionsUseSharedQmScrollPresets)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string FilterBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserDDNetFilter(CUIRect View,\n\tIFilterList &Filter,\n\tfloat ItemHeight, int MaxItems, int ItemsPerRow,\n\tCScrollRegion &ScrollRegion, std::vector<unsigned char> &vItemIds,\n\tbool UpdateCommunityCacheOnChange,\n\tconst std::function<const char *(int ItemIndex)> &GetItemName,\n\tconst std::function<void(int ItemIndex, CUIRect Item, const void *pItemId, bool Active)> &RenderItem)");
	const std::string FriendsBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserFriends(CUIRect View)");
	const std::string InfoScoreboardBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserInfoScoreboard(CUIRect View, const CServerInfo *pSelectedServer)");
	const std::string QmBody = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerbrowserQm(CUIRect View)");
	ASSERT_FALSE(FilterBody.empty());
	ASSERT_FALSE(FriendsBody.empty());
	ASSERT_FALSE(InfoScoreboardBody.empty());
	ASSERT_FALSE(QmBody.empty());

	EXPECT_NE(Source.find("#include <game/client/ui_scrollregion.h>"), std::string::npos);
	EXPECT_NE(FilterBody.find("ScrollRequest.m_Profile = EQmScrollProfile::FILTER_GRID;"), std::string::npos);
	EXPECT_NE(FilterBody.find("ScrollRequest.m_RowExtent = ItemHeight;"), std::string::npos);
	EXPECT_NE(FilterBody.find("ScrollRequest.m_RowsPerStep = 2;"), std::string::npos);
	EXPECT_NE(FilterBody.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);"), std::string::npos);
	EXPECT_NE(FilterBody.find("CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);"), std::string::npos);
	EXPECT_EQ(FilterBody.find("m_ScrollUnit = 2.0f * ItemHeight"), std::string::npos);
	EXPECT_EQ(FilterBody.find("ScrollParams.m_ScrollbarThickness = 10.0f;"), std::string::npos);
	EXPECT_EQ(FilterBody.find("ScrollParams.m_ScrollbarMargin = 3.0f;"), std::string::npos);

	EXPECT_NE(FriendsBody.find("ScrollRequest.m_Profile = EQmScrollProfile::MENU_LIST;"), std::string::npos);
	EXPECT_NE(FriendsBody.find("ScrollRequest.m_RowExtent = 24.0f;"), std::string::npos);
	EXPECT_NE(FriendsBody.find("ScrollRequest.m_RowsPerStep = 3;"), std::string::npos);
	EXPECT_NE(FriendsBody.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);"), std::string::npos);
	EXPECT_NE(FriendsBody.find("CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);"), std::string::npos);
	EXPECT_EQ(FriendsBody.find("m_ForceShowScrollbar"), std::string::npos);
	EXPECT_EQ(FriendsBody.find("ScrollParams.m_ScrollbarThickness = 16.0f;"), std::string::npos);
	EXPECT_EQ(FriendsBody.find("ScrollParams.m_ScrollbarMargin = 5.0f;"), std::string::npos);

	EXPECT_EQ(InfoScoreboardBody.find("QmScrollRegionParamsForSize"), std::string::npos);
	EXPECT_EQ(InfoScoreboardBody.find("SetScrollbarWidth"), std::string::npos);
	EXPECT_EQ(InfoScoreboardBody.find("SetScrollbarMargin"), std::string::npos);
	EXPECT_EQ(InfoScoreboardBody.find("s_ListBox.SetScrollbarWidth(16.0f);"), std::string::npos);
	EXPECT_EQ(InfoScoreboardBody.find("s_ListBox.SetScrollbarMargin(5.0f);"), std::string::npos);

	EXPECT_EQ(QmBody.find("QmScrollRegionParamsForSize"), std::string::npos);
	EXPECT_EQ(QmBody.find("SetScrollbarWidth"), std::string::npos);
	EXPECT_EQ(QmBody.find("SetScrollbarMargin"), std::string::npos);
	EXPECT_EQ(QmBody.find("s_QmServerListBox.SetScrollbarWidth(16.0f);"), std::string::npos);
	EXPECT_EQ(QmBody.find("s_QmServerListBox.SetScrollbarMargin(5.0f);"), std::string::npos);
}

TEST(QmMonitoringHelpers, CountryFilterUsesHiddenRailScrollPolicy)
{
	const std::string Browser = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string CountryFilterBody = ExtractSourceFunctionBody(Browser, "void CMenus::RenderServerbrowserCountriesFilter(CUIRect View)");
	ASSERT_FALSE(CountryFilterBody.empty());
	EXPECT_NE(CountryFilterBody.find("false, GetItemName, RenderItem"), std::string::npos);
	const std::string FilterBody = ExtractSourceFunctionBody(Browser, "void CMenus::RenderServerbrowserDDNetFilter(CUIRect View,");
	ASSERT_FALSE(FilterBody.empty());
	EXPECT_NE(FilterBody.find("ScrollRequest.m_RowsPerStep = 2;"), std::string::npos);
	EXPECT_EQ(FilterBody.find("m_HideScrollbar ="), std::string::npos);
}

TEST(QmMonitoringHelpers, IngameMotdScrollRegionUsesSharedQmScrollPreset)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_ingame.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderServerInfoMotd(CUIRect Motd)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Source.find("#include <game/client/ui_scrollregion.h>"), std::string::npos);
	EXPECT_NE(Body.find("CScrollRegionParams ScrollParams = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM);"), std::string::npos);
	EXPECT_NE(Body.find("ScrollParams.m_ScrollUnit = 5 * MotdFontSize;"), std::string::npos);
	EXPECT_EQ(Body.find("CScrollRegionParams ScrollParams;\n\tScrollParams.m_ScrollUnit"), std::string::npos);
}

TEST(QmMonitoringHelpers, ServerBrowserFriendPopupsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string CategoryBody = ExtractSourceFunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory(void *pContext, CUIRect View, bool Active)");
	const std::string NoteBody = ExtractSourceFunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupFriendNote(void *pContext, CUIRect View, bool Active)");
	ASSERT_FALSE(CategoryBody.empty());
	ASSERT_FALSE(NoteBody.empty());

	const size_t CategoryLabelPos = CategoryBody.find("pMenus->Ui()->DoLabel(&Label, pPopupContext->m_Mode == CFriendsCategoryPopupContext::MODE_ADD ? Localize(\"Category name\") : Localize(\"New category name\"), FontSize, TEXTALIGN_ML);");
	const size_t CategoryCtxPos = CategoryBody.find("const IUiContext FriendsCategoryTextInputCtx = pMenus->SettingsUiContext(\"server_browser_friends_category_text_input\");", CategoryLabelPos);
	const size_t CategoryFieldPos = CategoryBody.find("ui_widget::InputField(FriendsCategoryTextInputCtx, &pPopupContext->m_NameInput, Input, CategoryInputOptions);", CategoryCtxPos);
	const size_t CategoryCancelPos = CategoryBody.find("const bool CancelPressed", CategoryFieldPos);
	const size_t CategoryEscapePos = CategoryBody.find("pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)", CategoryCancelPos);
	const size_t CategoryConfirmPos = CategoryBody.find("const bool ConfirmPressed", CategoryFieldPos);
	const size_t CategoryEnterPos = CategoryBody.find("pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)", CategoryConfirmPos);
	const size_t CategoryTrimPos = CategoryBody.find("str_copy(aCategory, str_utf8_skip_whitespaces(pPopupContext->m_NameInput.GetString()), sizeof(aCategory));", CategoryConfirmPos);
	const size_t CategoryUpdatePos = CategoryBody.find("pMenus->FriendlistOnUpdate();", CategoryTrimPos);
	const size_t NoteLabelPos = NoteBody.find("pMenus->Ui()->DoLabel(&Label, Localize(\"Friend note\"), FontSize, TEXTALIGN_ML);");
	const size_t NoteCtxPos = NoteBody.find("const IUiContext FriendNoteTextInputCtx = pMenus->SettingsUiContext(\"server_browser_friend_note_text_input\");", NoteLabelPos);
	const size_t NoteFieldPos = NoteBody.find("ui_widget::InputField(FriendNoteTextInputCtx, &pPopupContext->m_NoteInput, Input, NoteInputOptions);", NoteCtxPos);
	const size_t NoteCancelPos = NoteBody.find("const bool CancelPressed", NoteFieldPos);
	const size_t NoteEscapePos = NoteBody.find("pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)", NoteCancelPos);
	const size_t NoteConfirmPos = NoteBody.find("const bool ConfirmPressed", NoteFieldPos);
	const size_t NoteEnterPos = NoteBody.find("pMenus->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)", NoteConfirmPos);
	const size_t NoteCopyPos = NoteBody.find("str_copy(aNote, pPopupContext->m_NoteInput.GetString(), sizeof(aNote));", NoteConfirmPos);
	const size_t NoteSavePos = NoteBody.find("pMenus->GameClient()->Friends()->SetFriendNote(pPopupContext->m_aName, pPopupContext->m_aClan, aNote);", NoteCopyPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(CategoryLabelPos, std::string::npos);
	EXPECT_NE(CategoryCtxPos, std::string::npos);
	EXPECT_NE(CategoryFieldPos, std::string::npos);
	EXPECT_NE(CategoryCancelPos, std::string::npos);
	EXPECT_NE(CategoryEscapePos, std::string::npos);
	EXPECT_NE(CategoryConfirmPos, std::string::npos);
	EXPECT_NE(CategoryEnterPos, std::string::npos);
	EXPECT_NE(CategoryTrimPos, std::string::npos);
	EXPECT_NE(CategoryUpdatePos, std::string::npos);
	EXPECT_LT(CategoryLabelPos, CategoryCtxPos);
	EXPECT_LT(CategoryCtxPos, CategoryFieldPos);
	EXPECT_LT(CategoryFieldPos, CategoryCancelPos);
	EXPECT_LT(CategoryCancelPos, CategoryEscapePos);
	EXPECT_LT(CategoryEscapePos, CategoryConfirmPos);
	EXPECT_LT(CategoryFieldPos, CategoryConfirmPos);
	EXPECT_LT(CategoryConfirmPos, CategoryEnterPos);
	EXPECT_LT(CategoryEnterPos, CategoryTrimPos);
	EXPECT_LT(CategoryConfirmPos, CategoryTrimPos);
	EXPECT_LT(CategoryTrimPos, CategoryUpdatePos);
	EXPECT_NE(NoteLabelPos, std::string::npos);
	EXPECT_NE(NoteCtxPos, std::string::npos);
	EXPECT_NE(NoteFieldPos, std::string::npos);
	EXPECT_NE(NoteCancelPos, std::string::npos);
	EXPECT_NE(NoteEscapePos, std::string::npos);
	EXPECT_NE(NoteConfirmPos, std::string::npos);
	EXPECT_NE(NoteEnterPos, std::string::npos);
	EXPECT_NE(NoteCopyPos, std::string::npos);
	EXPECT_NE(NoteSavePos, std::string::npos);
	EXPECT_LT(NoteLabelPos, NoteCtxPos);
	EXPECT_LT(NoteCtxPos, NoteFieldPos);
	EXPECT_LT(NoteFieldPos, NoteCancelPos);
	EXPECT_LT(NoteCancelPos, NoteEscapePos);
	EXPECT_LT(NoteEscapePos, NoteConfirmPos);
	EXPECT_LT(NoteFieldPos, NoteConfirmPos);
	EXPECT_LT(NoteConfirmPos, NoteEnterPos);
	EXPECT_LT(NoteEnterPos, NoteCopyPos);
	EXPECT_LT(NoteConfirmPos, NoteCopyPos);
	EXPECT_LT(NoteCopyPos, NoteSavePos);
	EXPECT_EQ(CategoryBody.find("pMenus->Ui()->DoEditBox(&pPopupContext->m_NameInput, &Input, FontSize + 1.0f"), std::string::npos);
	EXPECT_EQ(NoteBody.find("pMenus->Ui()->DoEditBox(&pPopupContext->m_NoteInput, &Input, FontSize + 1.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, NonCardServerAndFriendsUseSharedRuntime)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::array<const char *, 5> apSignatures = {
		"void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)",
		"void CMenus::RenderServerbrowserFilters(CUIRect View)",
		"void CMenus::RenderServerbrowserDDNetFilter(CUIRect View,",
		"void CMenus::RenderServerbrowserFriends(CUIRect View)",
		"CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory(void *pContext, CUIRect View, bool Active)",
	};
	const std::array<const char *, 12> apForbidden = {
		"ui_widget::TextField(",
		"ui_widget::SearchField(",
		"ui_widget::ClearableTextField(",
		"ui_widget::IconTextField(",
		"DoEditBox(",
		"QmScrollRegionParamsForSize(",
		"m_ScrollUnit =",
		"ForceShowScrollbar",
		"KEY_MOUSE_WHEEL_UP",
		"KEY_MOUSE_WHEEL_DOWN",
		"SettingsCard(",
		"RegisterSettingsCardDeckItem(",
	};

	for(const char *pSignature : apSignatures)
	{
		const std::string Body = ExtractSourceFunctionBody(Source, pSignature);
		ASSERT_FALSE(Body.empty()) << pSignature;
		for(const char *pForbidden : apForbidden)
			EXPECT_EQ(Body.find(pForbidden), std::string::npos) << pSignature << ": " << pForbidden;
	}

	const std::string StatusBody = ExtractSourceFunctionBody(Source, apSignatures[0]);
	const std::string FilterBody = ExtractSourceFunctionBody(Source, apSignatures[2]);
	const std::string FriendsBody = ExtractSourceFunctionBody(Source, apSignatures[3]);
	EXPECT_NE(StatusBody.find("SettingsUiContext("), std::string::npos);
	EXPECT_NE(StatusBody.find("ui_widget::InputField("), std::string::npos);
	EXPECT_NE(FilterBody.find("QmResolveScrollPolicy("), std::string::npos);
	EXPECT_NE(FriendsBody.find("SettingsUiContext("), std::string::npos);
	EXPECT_NE(FriendsBody.find("ui_widget::InputField("), std::string::npos);
	EXPECT_NE(FriendsBody.find("QmResolveScrollPolicy("), std::string::npos);
}

TEST(QmMonitoringHelpers, NonCardMenuLegacyUiPathsAreGone)
{
	struct SFunctionScope
	{
		const char *m_pPath;
		const char *m_pSignature;
	};
	const std::array<SFunctionScope, 7> aScopes = {{
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFilters(CUIRect View)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserDDNetFilter(CUIRect View,"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFriends(CUIRect View)"},
		{"src/game/client/components/menus_demo.cpp", "void CMenus::RenderDemoBrowser(CUIRect MainView)"},
		{"src/game/client/components/menus_demo.cpp", "void CMenus::RenderDemoBrowserList(CUIRect ListView, bool &WasListboxItemActivated)"},
		{"src/game/client/components/menus_settings_assets.cpp", "void CMenus::RenderSettingsCustom(CUIRect MainView)"},
	}};
	const std::array<const char *, 11> apForbidden = {
		"ui_widget::TextField(",
		"ui_widget::SearchField(",
		"ui_widget::ClearableTextField(",
		"ui_widget::IconTextField(",
		"KEY_MOUSE_WHEEL_UP",
		"KEY_MOUSE_WHEEL_DOWN",
		"ForceShowScrollbar",
		"m_ScrollUnit =",
		"SetScrollbarWidth(",
		"SettingsCard(",
		"RegisterSettingsCardDeckItem(",
	};

	for(const SFunctionScope &Scope : aScopes)
	{
		const std::string Source = ReadRepoFile(Scope.m_pPath);
		const std::string Body = ExtractSourceFunctionBody(Source, Scope.m_pSignature);
		ASSERT_FALSE(Body.empty()) << Scope.m_pPath << ": " << Scope.m_pSignature;
		for(const char *pForbidden : apForbidden)
			EXPECT_EQ(Body.find(pForbidden), std::string::npos) << Scope.m_pPath << ": " << pForbidden;
	}
}

TEST(QmMonitoringHelpers, InputFieldForwardingAliasesAreDeletedAfterP7)
{
	const std::array<const char *, 9> apCallPatterns = {
		"ui_widget::TextField(",
		"ui_widget::TextFieldEx(",
		"ui_widget::LegacyTextFieldEx(",
		"ui_widget::SearchField(",
		"ui_widget::SearchFieldEx(",
		"ui_widget::ClearableTextField(",
		"ui_widget::ClearableTextFieldEx(",
		"ui_widget::IconTextField(",
		"ui_widget::IconTextFieldEx(",
	};
	const std::filesystem::path ComponentsPath = TestSourcePath("src/game/client/components");
	for(const std::filesystem::directory_entry &Entry : std::filesystem::recursive_directory_iterator(ComponentsPath))
	{
		if(!Entry.is_regular_file() || (Entry.path().extension() != ".cpp" && Entry.path().extension() != ".h"))
			continue;
		std::ifstream File(Entry.path());
		ASSERT_TRUE(File.good()) << Entry.path().string();
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		for(const char *pPattern : apCallPatterns)
			EXPECT_EQ(Source.find(pPattern), std::string::npos) << Entry.path().string() << ": " << pPattern;
	}

	const std::array<const char *, 3> apQmUiPaths = {
		"src/game/client/QmUi/UiForms.h",
		"src/game/client/QmUi/UiForms.cpp",
		"src/game/client/QmUi/UiDogfood.cpp",
	};
	const std::array<const char *, 9> apAliasNames = {
		"TextFieldEx(const IUiContext",
		"LegacyTextFieldEx(const IUiContext",
		"bool TextField(const IUiContext",
		"SearchFieldEx(const IUiContext",
		"bool SearchField(const IUiContext",
		"ClearableTextFieldEx(const IUiContext",
		"bool ClearableTextField(const IUiContext",
		"IconTextFieldEx(const IUiContext",
		"bool IconTextField(const IUiContext",
	};
	for(const char *pPath : apQmUiPaths)
	{
		const std::string Source = ReadRepoFile(pPath);
		for(const char *pAlias : apAliasNames)
			EXPECT_EQ(Source.find(pAlias), std::string::npos) << pPath << ": " << pAlias;
		for(const char *pPattern : apCallPatterns)
			EXPECT_EQ(Source.find(pPattern), std::string::npos) << pPath << ": " << pPattern;
	}
}

TEST(QmMonitoringHelpers, DemoSliceNameInputUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderDemoPlayerSliceSavePopup(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t TextFieldPos = Body.find("ui_widget::InputField(DemoSliceTextInputCtx, &m_DemoSliceInput, NameBox, SliceNameInputOptions);");
	const size_t OkButtonPos = Body.find("if(DoButton_Menu(&s_ButtonOk", TextFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext DemoSliceTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("DemoSliceTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"demo_slice_text_input\");"), std::string::npos);
	EXPECT_NE(Body.find("SliceNameInputOptions.m_pPlaceholder = Localize(\"New name\");"), std::string::npos);
	EXPECT_NE(Body.find("SliceNameInputOptions.m_FontSize = 12.0f;"), std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_NE(OkButtonPos, std::string::npos);
	EXPECT_LT(TextFieldPos, OkButtonPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&m_DemoSliceInput, &NameBox, 12.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoRenamePopupUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderPopupFullscreen(CUIRect Screen)");
	ASSERT_FALSE(Body.empty());

	const size_t RenamePopupPos = Body.find("else if(m_Popup == POPUP_RENAME_DEMO)");
	const size_t OkButtonPos = Body.find("if(DoButton_Menu(&s_ButtonOk", RenamePopupPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(DemoRenameTextInputCtx, &m_DemoRenameInput, TextBox, RenameInputOptions);", OkButtonPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext DemoRenameTextInputCtx;", RenamePopupPos), std::string::npos);
	EXPECT_NE(Body.find("DemoRenameTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"demo_rename_text_input\");", RenamePopupPos), std::string::npos);
	EXPECT_NE(RenamePopupPos, std::string::npos);
	EXPECT_NE(OkButtonPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_LT(OkButtonPos, TextFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&m_DemoRenameInput, &TextBox, 12.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, DemoRenderPopupUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderPopupFullscreen(CUIRect Screen)");
	ASSERT_FALSE(Body.empty());

	const size_t RenderPopupPos = Body.find("else if(m_Popup == POPUP_RENDER_DEMO)");
	const size_t VideoNameLabelPos = Body.find("Ui()->DoLabel(&Label, Localize(\"Video name:\"), 12.8f, TEXTALIGN_ML);", RenderPopupPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(DemoRenderTextInputCtx, &m_DemoRenderInput, TextBox, RenderInputOptions);", VideoNameLabelPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext DemoRenderTextInputCtx;", RenderPopupPos), std::string::npos);
	EXPECT_NE(Body.find("DemoRenderTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"demo_render_text_input\");", RenderPopupPos), std::string::npos);
	EXPECT_NE(RenderPopupPos, std::string::npos);
	EXPECT_NE(VideoNameLabelPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_LT(VideoNameLabelPos, TextFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&m_DemoRenderInput, &TextBox, 12.8f"), std::string::npos);
}

TEST(QmMonitoringHelpers, PopupClearableInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderPopupFullscreen(CUIRect Screen)");
	ASSERT_FALSE(Body.empty());

	const size_t PasswordBufferPos = Source.find("m_PasswordInput.SetBuffer(g_Config.m_Password, sizeof(g_Config.m_Password));");
	const size_t PasswordHiddenPos = Source.find("m_PasswordInput.SetHidden(true);", PasswordBufferPos);
	const size_t PasswordPopupPos = Body.find("else if(m_Popup == POPUP_PASSWORD)");
	const size_t ConnectPos = Body.find("Client()->Connect(aAddr, g_Config.m_Password);", PasswordPopupPos);
	const size_t PasswordLabelPos = Body.find("Ui()->DoLabel(&Label, Localize(\"Password\"), 18.0f, TEXTALIGN_ML);", PasswordPopupPos);
	const size_t PasswordCtxPos = Body.find("IUiContext PasswordPopupTextInputCtx;", PasswordLabelPos);
	const size_t PasswordUiPos = Body.find("PasswordPopupTextInputCtx.m_pUi = Ui();", PasswordCtxPos);
	const size_t PasswordScopePos = Body.find("PasswordPopupTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"password_popup_text_input\");", PasswordUiPos);
	const size_t PasswordFieldPos = Body.find("ui_widget::InputField(PasswordPopupTextInputCtx, &m_PasswordInput, TextBox, PasswordInputOptions);", PasswordScopePos);
	const size_t AddressLabelPos = Body.find("Ui()->DoLabel(&Label, Localize(\"Address\"), 18.0f, TEXTALIGN_ML);", PasswordFieldPos);
	const size_t SaveSkinPopupPos = Body.find("else if(m_Popup == POPUP_SAVE_SKIN)");
	const size_t YesButtonPos = Body.find("if(DoButton_Menu(&s_ButtonYes", SaveSkinPopupPos);
	const size_t ValidFilenamePos = Body.find("str_valid_filename(m_SkinNameInput.GetString())", YesButtonPos);
	const size_t SpecialSkinPos = Body.find("CSkins7::IsSpecialSkin(m_SkinNameInput.GetString())", ValidFilenamePos);
	const size_t SaveSkinFilePos = Body.find("GameClient()->m_Skins7.SaveSkinfile(m_SkinNameInput.GetString(), m_Dummy)", SpecialSkinPos);
	const size_t SaveSkinLabelPos = Body.find("Ui()->DoLabel(&Label, Localize(\"Name\"), 18.0f, TEXTALIGN_ML);", YesButtonPos);
	const size_t SaveSkinCtxPos = Body.find("IUiContext SaveSkinTextInputCtx;", SaveSkinLabelPos);
	const size_t SaveSkinUiPos = Body.find("SaveSkinTextInputCtx.m_pUi = Ui();", SaveSkinCtxPos);
	const size_t SaveSkinScopePos = Body.find("SaveSkinTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"save_skin_text_input\");", SaveSkinUiPos);
	const size_t SaveSkinFieldPos = Body.find("ui_widget::InputField(SaveSkinTextInputCtx, &m_SkinNameInput, TextBox, SaveSkinInputOptions);", SaveSkinScopePos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(PasswordBufferPos, std::string::npos);
	EXPECT_NE(PasswordHiddenPos, std::string::npos);
	EXPECT_LT(PasswordBufferPos, PasswordHiddenPos);
	EXPECT_NE(PasswordPopupPos, std::string::npos);
	EXPECT_NE(ConnectPos, std::string::npos);
	EXPECT_NE(PasswordLabelPos, std::string::npos);
	EXPECT_NE(PasswordCtxPos, std::string::npos);
	EXPECT_NE(PasswordUiPos, std::string::npos);
	EXPECT_NE(PasswordScopePos, std::string::npos);
	EXPECT_NE(Body.find("PasswordInputOptions.m_Clearable = true;", PasswordScopePos), std::string::npos);
	EXPECT_NE(Body.find("PasswordInputOptions.m_FontSize = 12.0f;", PasswordScopePos), std::string::npos);
	EXPECT_NE(PasswordFieldPos, std::string::npos);
	EXPECT_NE(AddressLabelPos, std::string::npos);
	EXPECT_LT(PasswordPopupPos, ConnectPos);
	EXPECT_LT(ConnectPos, PasswordLabelPos);
	EXPECT_LT(PasswordLabelPos, PasswordCtxPos);
	EXPECT_LT(PasswordCtxPos, PasswordUiPos);
	EXPECT_LT(PasswordUiPos, PasswordScopePos);
	EXPECT_LT(PasswordScopePos, PasswordFieldPos);
	EXPECT_LT(PasswordFieldPos, AddressLabelPos);
	EXPECT_NE(SaveSkinPopupPos, std::string::npos);
	EXPECT_NE(YesButtonPos, std::string::npos);
	EXPECT_NE(ValidFilenamePos, std::string::npos);
	EXPECT_NE(SpecialSkinPos, std::string::npos);
	EXPECT_NE(SaveSkinFilePos, std::string::npos);
	EXPECT_NE(SaveSkinLabelPos, std::string::npos);
	EXPECT_NE(SaveSkinCtxPos, std::string::npos);
	EXPECT_NE(SaveSkinUiPos, std::string::npos);
	EXPECT_NE(SaveSkinScopePos, std::string::npos);
	EXPECT_NE(Body.find("SaveSkinInputOptions.m_Clearable = true;", SaveSkinScopePos), std::string::npos);
	EXPECT_NE(Body.find("SaveSkinInputOptions.m_FontSize = 12.0f;", SaveSkinScopePos), std::string::npos);
	EXPECT_NE(SaveSkinFieldPos, std::string::npos);
	EXPECT_LT(YesButtonPos, ValidFilenamePos);
	EXPECT_LT(ValidFilenamePos, SpecialSkinPos);
	EXPECT_LT(SpecialSkinPos, SaveSkinFilePos);
	EXPECT_LT(SaveSkinFilePos, SaveSkinLabelPos);
	EXPECT_LT(YesButtonPos, SaveSkinLabelPos);
	EXPECT_LT(SaveSkinLabelPos, SaveSkinCtxPos);
	EXPECT_LT(SaveSkinCtxPos, SaveSkinUiPos);
	EXPECT_LT(SaveSkinUiPos, SaveSkinScopePos);
	EXPECT_LT(SaveSkinScopePos, SaveSkinFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_PasswordInput, &TextBox, 12.0f"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&m_SkinNameInput, &TextBox, 12.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, FirstLaunchPlayerNameUsesSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderPopupFullscreen(CUIRect Screen)");
	ASSERT_FALSE(Body.empty());

	const size_t FirstLaunchPopupPos = Body.find("else if(m_Popup == POPUP_FIRST_LAUNCH)");
	const size_t EmptyTextPos = Body.find("s_PlayerNameInput.SetEmptyText(Client()->PlayerName());", FirstLaunchPopupPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(FirstLaunchTextInputCtx, &s_PlayerNameInput, TextBox, PlayerNameInputOptions);", EmptyTextPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext FirstLaunchTextInputCtx;", FirstLaunchPopupPos), std::string::npos);
	EXPECT_NE(Body.find("FirstLaunchTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"first_launch_text_input\");", FirstLaunchPopupPos), std::string::npos);
	EXPECT_NE(FirstLaunchPopupPos, std::string::npos);
	EXPECT_NE(EmptyTextPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_LT(EmptyTextPos, TextFieldPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_PlayerNameInput, &TextBox, 12.0f"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsSearchUsesSharedQmInputField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t SearchPos = Body.find("ui_widget::InputField(AssetsSearchCtx, &s_aFilterInputs[s_CurCustomTab], QuickSearch, AssetsSearchOptions).m_Changed");
	const size_t RefreshPos = Body.find("gs_aInitCustomList[s_CurCustomTab] = true;", SearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext AssetsSearchCtx = SettingsUiContext(\"settings_assets_search\", ContentMetrics.m_UiScale);"), std::string::npos);
	EXPECT_NE(SearchPos, std::string::npos);
	EXPECT_NE(RefreshPos, std::string::npos);
	EXPECT_LT(SearchPos, RefreshPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch"), std::string::npos);
}

TEST(QmMonitoringHelpers, AudioPackEditorSearchUsesSharedQmSearchField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderAudioPackEditorScreen(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t SlotSearchPos = Body.find("ui_widget::InputField(AudioPackSlotSearchCtx, &m_AudioPackEditorState.m_FilterInput, SlotSearchInput, EditorFontSize");
	const size_t SlotFilterPos = Body.find("const char *pSlotFilter = m_AudioPackEditorState.m_FilterInput.GetString();", SlotSearchPos);
	const size_t CandidateSearchPos = Body.find("ui_widget::InputField(AudioPackCandidateSearchCtx, &m_AudioPackEditorState.m_CandidateFilterInput, CandidateSearchInput, EditorFontSize");
	const size_t CandidateFilterPos = Body.find("const char *pCandidateFilter = m_AudioPackEditorState.m_CandidateFilterInput.GetString();", CandidateSearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext AudioPackSlotSearchCtx;"), std::string::npos);
	EXPECT_NE(Body.find("AudioPackSlotSearchCtx.m_ScopeHash = MakeUiScopeHash(\"settings_audio_pack_slot_search\");"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext AudioPackCandidateSearchCtx;"), std::string::npos);
	EXPECT_NE(Body.find("AudioPackCandidateSearchCtx.m_ScopeHash = MakeUiScopeHash(\"settings_audio_pack_candidate_search\");"), std::string::npos);
	EXPECT_NE(SlotSearchPos, std::string::npos);
	EXPECT_NE(SlotFilterPos, std::string::npos);
	EXPECT_LT(SlotSearchPos, SlotFilterPos);
	EXPECT_NE(CandidateSearchPos, std::string::npos);
	EXPECT_NE(CandidateFilterPos, std::string::npos);
	EXPECT_LT(CandidateSearchPos, CandidateFilterPos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&m_AudioPackEditorState.m_FilterInput, &SlotSearchInput"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox_Search(&m_AudioPackEditorState.m_CandidateFilterInput, &CandidateSearchInput"), std::string::npos);
}

TEST(QmMonitoringHelpers, AudioPackEditorUsesStableSlotItemIds)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderAudioPackEditorScreen(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("static std::vector<int> s_vAudioPackEditorSlotItemIds;"), std::string::npos);
	EXPECT_NE(Body.find("s_vAudioPackEditorSlotItemIds.resize(vAllSlots.size());"), std::string::npos);
	EXPECT_NE(Body.find("s_AudioPackEditorSlotListBox.DoNextItem(&s_vAudioPackEditorSlotItemIds[SlotIndex], SelectedVisibleSlot == VisibleIndex)"), std::string::npos);
	EXPECT_EQ(Body.find("s_AudioPackEditorSlotListBox.DoNextItem(&Slot, SelectedVisibleSlot == VisibleIndex)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AudioPackDirectoryOpensWritableSaveFolder)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsSound(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("Storage()->GetCompletePath(IStorage::TYPE_SAVE, \"audio\", aBuf, sizeof(aBuf));"), std::string::npos);
	EXPECT_EQ(Body.find("Storage()->GetCompletePath(IStorage::TYPE_ALL, \"audio\""), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsPageSwitchesSkipWholePageOffsetAnimation)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettings(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("SettingsPageSwitchNode"), std::string::npos);
	EXPECT_EQ(Body.find("s_SettingsTransitionDirection"), std::string::npos);
	EXPECT_EQ(Body.find("ApplyUiSwitchOffset(ContentView"), std::string::npos);
	EXPECT_NE(Body.find("m_SettingsPageSwitchActive = false;"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEditorSearchUsesSharedQmInputField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_assets_editor.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderAssetsEditorScreen(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t DonorSearchPos = Body.find("ui_widget::InputField(AssetsEditorDonorSearchCtx, &s_aDonorSearchInputs[m_AssetsEditorState.m_Type], DonorSearchBox, DonorSearchOptions);");
	const size_t DonorFilterPos = Body.find("for(size_t Index = 0; Index < vFilteredDonorAssetIndices.size(); ++Index)", DonorSearchPos);
	const size_t MainSearchPos = Body.find("ui_widget::InputField(AssetsEditorMainSearchCtx, &s_aMainSearchInputs[m_AssetsEditorState.m_Type], MainSearchBox, MainSearchOptions);");
	const size_t MainFilterPos = Body.find("for(size_t Index = 0; Index < vFilteredMainAssetIndices.size(); ++Index)", MainSearchPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext AssetsEditorDonorSearchCtx = SettingsUiContext(\"assets_editor_donor_search\");"), std::string::npos);
	EXPECT_NE(Body.find("const IUiContext AssetsEditorMainSearchCtx = SettingsUiContext(\"assets_editor_main_search\");"), std::string::npos);
	EXPECT_NE(DonorSearchPos, std::string::npos);
	EXPECT_NE(DonorFilterPos, std::string::npos);
	EXPECT_LT(DonorSearchPos, DonorFilterPos);
	EXPECT_NE(MainSearchPos, std::string::npos);
	EXPECT_NE(MainFilterPos, std::string::npos);
	EXPECT_LT(MainSearchPos, MainFilterPos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_aDonorSearchInputs[m_AssetsEditorState.m_Type], &DonorSearchBox"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoClearableEditBox(&s_aMainSearchInputs[m_AssetsEditorState.m_Type], &MainSearchBox"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsEditorExportNameUsesSharedQmInputField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_assets_editor.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderAssetsEditorScreen(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t SetPlaceholderPos = Body.find("s_ExportNameInput.SetEmptyText(aExportPlaceholder);");
	const size_t TextInputCtxPos = Body.find("const IUiContext AssetsEditorExportNameCtx = SettingsUiContext(\"assets_editor_export_name\");", SetPlaceholderPos);
	const size_t TextFieldPos = Body.find("ui_widget::InputField(AssetsEditorExportNameCtx, &s_ExportNameInput, ExportRow, ExportNameOptions).m_Changed", TextInputCtxPos);
	const size_t CommitPos = Body.find("AssetsEditorCommitExportNameForType();", TextFieldPos);
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(SetPlaceholderPos, std::string::npos);
	EXPECT_NE(TextInputCtxPos, std::string::npos);
	EXPECT_NE(TextFieldPos, std::string::npos);
	EXPECT_NE(CommitPos, std::string::npos);
	EXPECT_LT(SetPlaceholderPos, TextInputCtxPos);
	EXPECT_LT(TextInputCtxPos, TextFieldPos);
	EXPECT_LT(TextFieldPos, CommitPos);
	EXPECT_EQ(Body.find(";\n", TextFieldPos), CommitPos + str_length("AssetsEditorCommitExportNameForType()"));
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&s_ExportNameInput, &ExportRow, EditBoxFontSize"), std::string::npos);
}

TEST(QmMonitoringHelpers, AudioPackEditorTextInputsUseSharedQmTextField)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderAudioPackEditorScreen(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t PackNamePos = Body.find("ui_widget::InputField(AudioPackEditorTextInputCtx, &m_AudioPackEditorState.m_PackNameInput, PackInput, Localize(\"Pack name\"), EditorFontSize)");
	const size_t RefreshPos = Body.find("AudioPackEditorRefreshCandidates();", PackNamePos);
	const size_t ManualPathPos = Body.find("ui_widget::InputField(AudioPackEditorTextInputCtx, &m_AudioPackEditorState.m_SourcePathInput, ManualInput, Localize(\"Manual source file\"), EditorFontSize);");
	EXPECT_NE(Source.find("#include <game/client/QmUi/UiForms.h>"), std::string::npos);
	EXPECT_NE(Body.find("IUiContext AudioPackEditorTextInputCtx;"), std::string::npos);
	EXPECT_NE(Body.find("AudioPackEditorTextInputCtx.m_ScopeHash = MakeUiScopeHash(\"settings_audio_pack_text_inputs\");"), std::string::npos);
	EXPECT_NE(PackNamePos, std::string::npos);
	EXPECT_NE(RefreshPos, std::string::npos);
	EXPECT_LT(PackNamePos, RefreshPos);
	EXPECT_NE(ManualPathPos, std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&m_AudioPackEditorState.m_PackNameInput, &PackInput"), std::string::npos);
	EXPECT_EQ(Body.find("Ui()->DoEditBox(&m_AudioPackEditorState.m_SourcePathInput, &ManualInput"), std::string::npos);
}

TEST(QmMonitoringHelpers, DropdownPopupUsesComputedGeometrySize)
{
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadRepoFile("src/game/client/ui.h");
	const std::string DropdownHeader = ReadRepoFile("src/game/client/QmUi/QmDropdown.h");
	const std::string DropdownSource = ReadRepoFile("src/game/client/QmUi/QmDropdown.cpp");
	const std::string PopupBody = ExtractSourceFunctionBody(Ui, "void CUi::DoPopupMenu(const SPopupMenuId *pId, float X, float Y, float Width, float Height, void *pContext, FPopupMenuFunction pfnFunc, const SPopupMenuProperties &Props)");
	const std::string SelectionResetBody = ExtractSourceFunctionBody(Ui, "void CUi::SSelectionPopupContext::Reset()");
	const std::string Body = ExtractSourceFunctionBody(Ui, "void CUi::ShowPopupSelection(float X, float Y, SSelectionPopupContext *pContext)");
	const std::string PopupSelectionBody = ExtractSourceFunctionBody(Ui, "CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)");
	const std::string RenderPopupsBody = ExtractSourceFunctionBody(Ui, "void CUi::RenderPopupMenus()");
	ASSERT_FALSE(PopupBody.empty());
	ASSERT_FALSE(SelectionResetBody.empty());
	ASSERT_FALSE(Body.empty());
	ASSERT_FALSE(PopupSelectionBody.empty());
	ASSERT_FALSE(RenderPopupsBody.empty());

	EXPECT_NE(UiHeader.find("bool m_AutoReposition = true;"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool m_AnchorVisible = true;"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool m_PopupVisible = true;"), std::string::npos);
	EXPECT_NE(SelectionResetBody.find("m_AnchorVisible = true;"), std::string::npos);
	EXPECT_NE(SelectionResetBody.find("m_PopupVisible = true;"), std::string::npos);
	EXPECT_NE(PopupBody.find("if(Props.m_AutoReposition)"), std::string::npos);
	EXPECT_NE(DropdownHeader.find("bool m_PopupVisible = false;"), std::string::npos);
	EXPECT_NE(DropdownSource.find("Result.m_PopupVisible = Result.m_Rect.w > 0.0f && Result.m_Rect.h > 0.0f && RectsOverlap(Result.m_Rect, ViewportRect);"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool m_ClipToViewport = false;"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool m_BlockUnderlyingScroll = false;"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool m_BlockUnderlyingPointerInput = false;"), std::string::npos);
	EXPECT_NE(UiHeader.find("CUIRect m_Viewport{};"), std::string::npos);
	EXPECT_NE(UiHeader.find("SQmDropdownPopupPolicy m_PopupPolicy;"), std::string::npos);
	EXPECT_NE(SelectionResetBody.find("m_BlockUnderlyingScroll = false;"), std::string::npos);
	EXPECT_NE(SelectionResetBody.find("m_Viewport = {};"), std::string::npos);
	EXPECT_NE(Body.find("QmResolveDropdownPopupPolicy("), std::string::npos);
	EXPECT_NE(Body.find("const CUIRect &Viewport = pContext->m_Viewport;"), std::string::npos);
	EXPECT_NE(Body.find("QmComputeDropdownPopupGeometry(AnchorRect, Viewport, GeometryConfig);"), std::string::npos);
	EXPECT_NE(Body.find("const bool Scrollable = pContext->m_PopupVisible && QmDropdownPopupScrollable("), std::string::npos);
	EXPECT_NE(Body.find("const bool BlockUnderlying = QmDropdownPopupBlocksUnderlying(pContext->m_PopupVisible);"), std::string::npos);
	EXPECT_NE(Body.find("RegisterWheelOwner(pContext, EUiWheelOwnerPriority::POPUP, PopupRect, BlockUnderlying);"), std::string::npos);
	EXPECT_NE(Body.find("pContext->m_BlockUnderlyingScroll = BlockUnderlying;"), std::string::npos);
	EXPECT_NE(PopupSelectionBody.find("QmResolveScrollPolicy(ScrollRequest)"), std::string::npos);
	EXPECT_NE(PopupSelectionBody.find("ScrollParams.m_HideScrollbar = !pSelectionPopup->m_Scrollable;"), std::string::npos);
	EXPECT_NE(PopupSelectionBody.find("pScrollRegion->AddRect(Slot, QmDropdownActiveItemShouldScrollIntoView"), std::string::npos);
	EXPECT_EQ(PopupSelectionBody.find("m_ScrollbarThickness = 10.0f"), std::string::npos);
	EXPECT_EQ(PopupSelectionBody.find("m_ScrollUnit = 3 *"), std::string::npos);
	EXPECT_NE(RenderPopupsBody.find("PopupMenu.m_Props.m_BlockUnderlyingScroll"), std::string::npos);
	EXPECT_NE(RenderPopupsBody.find("PopupMenu.m_Props.m_BlockUnderlyingPointerInput"), std::string::npos);
	EXPECT_NE(RenderPopupsBody.find("PopupMenu.m_Props.m_ClipToViewport"), std::string::npos);
	EXPECT_NE(Body.find("pContext->m_AnchorVisible = Geometry.m_AnchorVisible;"), std::string::npos);
	EXPECT_NE(Body.find("pContext->m_PopupVisible = Geometry.m_PopupVisible;"), std::string::npos);
	EXPECT_NE(Body.find("if(!pContext->m_AnchorVisible || !pContext->m_PopupVisible)"), std::string::npos);
	EXPECT_NE(Body.find("ClosePopupMenu(pContext);"), std::string::npos);
	EXPECT_NE(Body.find("float PopupWidth = pContext->m_Width;"), std::string::npos);
	EXPECT_NE(Body.find("float PopupHeightResolved = PopupHeight;"), std::string::npos);
	EXPECT_NE(Body.find("pContext->m_Props.m_AutoReposition = false;"), std::string::npos);
	EXPECT_NE(Body.find("PopupWidth = Geometry.m_Rect.w;"), std::string::npos);
	EXPECT_NE(Body.find("PopupHeightResolved = Geometry.m_Rect.h;"), std::string::npos);
	EXPECT_NE(Body.find("DoPopupMenu(pContext, X, Y, PopupWidth, PopupHeightResolved, pContext, PopupSelection, pContext->m_Props);"), std::string::npos);
	EXPECT_EQ(Body.find("DoPopupMenu(pContext, X, Y, pContext->m_Width, PopupHeight, pContext, PopupSelection, pContext->m_Props);"), std::string::npos);
}

TEST(QmMonitoringHelpers, ColorPickerUsesModalPointerInputAndFullGradientHitAreas)
{
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp");
	const std::string UiHeader = ReadRepoFile("src/game/client/ui.h");
	const std::string ColorPickerBody = ExtractSourceFunctionBody(Ui, "CUi::EPopupMenuFunctionResult CUi::PopupColorPicker(void *pContext, CUIRect View, bool Active)");
	const std::string ShowColorPickerBody = ExtractSourceFunctionBody(Ui, "void CUi::ShowPopupColorPicker(float X, float Y, SColorPickerPopupContext *pContext)");
	const std::string MouseHoveredBody = ExtractSourceFunctionBody(Ui, "bool CUi::MouseHovered(const CUIRect *pRect) const");
	ASSERT_FALSE(ColorPickerBody.empty());
	ASSERT_FALSE(ShowColorPickerBody.empty());
	ASSERT_FALSE(MouseHoveredBody.empty());

	EXPECT_NE(UiHeader.find("bool UnderlyingPointerInputBlocked() const;"), std::string::npos);
	EXPECT_NE(UiHeader.find("int m_PopupInputDepth = 0;"), std::string::npos);
	EXPECT_NE(UiHeader.find("if(UnderlyingPointerInputBlocked())"), std::string::npos);
	EXPECT_NE(MouseHoveredBody.find("!UnderlyingPointerInputBlocked()"), std::string::npos);
	EXPECT_NE(ShowColorPickerBody.find("PopupProps.m_BlockUnderlyingPointerInput = true;"), std::string::npos);
	EXPECT_NE(ShowColorPickerBody.find("PopupProps.m_BlockUnderlyingScroll = true;"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("const CUIRect ColorsHitArea = ColorsArea;"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("const CUIRect HueHitArea = HueArea;"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("DoPickerLogic(&pColorPicker->m_ColorPickerId, &ColorsHitArea"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("const float ColorX = std::clamp(PickerX - (ColorsArea.x - ColorsHitArea.x), 0.0f, ColorsArea.w);"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("const float ColorY = std::clamp(PickerY - (ColorsArea.y - ColorsHitArea.y), 0.0f, ColorsArea.h);"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("PickerColorHSV.y = ColorX / ColorsArea.w;"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("PickerColorHSV.z = 1.0f - ColorY / ColorsArea.h;"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("DoPickerLogic(&pColorPicker->m_HuePickerId, &HueHitArea"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("const float HueY = std::clamp(PickerY - (HueArea.y - HueHitArea.y), 0.0f, HueArea.h);"), std::string::npos);
	EXPECT_NE(ColorPickerBody.find("PickerColorHSV.x = 1.0f - HueY / HueArea.h;"), std::string::npos);
}

TEST(QmMonitoringHelpers, ListBoxResolvesMenuScrollPolicyFromRowSemantics)
{
	const std::string Header = ReadRepoFile("src/game/client/ui_listbox.h");
	const std::string Source = ReadRepoFile("src/game/client/ui_listbox.cpp");
	const std::string DoStartBody = ExtractSourceFunctionBody(Source, "void CListBox::DoStart(float RowHeight, int NumItems, int ItemsPerRow, int RowsPerScroll, int SelectedIndex, const CUIRect *pRect, bool Background, int BackgroundCorners)");
	ASSERT_FALSE(DoStartBody.empty());

	EXPECT_NE(Header.find("EQmScrollProfile m_ScrollProfile = EQmScrollProfile::MENU_LIST;"), std::string::npos);
	EXPECT_NE(Header.find("void SetScrollProfile(EQmScrollProfile Profile)"), std::string::npos);
	EXPECT_NE(DoStartBody.find("ScrollRequest.m_Profile = m_ScrollProfile;"), std::string::npos);
	EXPECT_NE(DoStartBody.find("ScrollRequest.m_RowExtent = m_ListBoxRowHeight + m_AutoSpacing;"), std::string::npos);
	EXPECT_NE(DoStartBody.find("ScrollRequest.m_RowsPerStep = RowsPerScroll;"), std::string::npos);
	EXPECT_NE(DoStartBody.find("const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);"), std::string::npos);
	EXPECT_NE(DoStartBody.find("CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);"), std::string::npos);
	EXPECT_NE(DoStartBody.find("QmListBoxScrollbarMetric(ScrollParams.m_ScrollbarThickness, m_ScrollbarWidth, m_ScrollbarWidthOverridden)"), std::string::npos);
	EXPECT_NE(DoStartBody.find("ScrollParams.m_ScrollbarThickness = m_ScrollbarWidth;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_InitialScrollPending;"), std::string::npos);
	EXPECT_NE(Header.find("QmListBoxShouldScrollToInitialSelection"), std::string::npos);
	EXPECT_NE(Header.find("QmListBoxInitialScrollRemainsPending"), std::string::npos);
	EXPECT_NE(DoStartBody.find("if(QmListBoxShouldScrollToInitialSelection"), std::string::npos);
	EXPECT_NE(DoStartBody.find("m_InitialScrollPending = QmListBoxInitialScrollRemainsPending"), std::string::npos);
	EXPECT_EQ(DoStartBody.find("m_ScrollbarThickness = ScrollbarWidthMax()"), std::string::npos);
	EXPECT_EQ(DoStartBody.find("m_ScrollbarMargin = ScrollbarMargin()"), std::string::npos);
	EXPECT_EQ(DoStartBody.find("m_ForceShowScrollbar = ForceShowScrollbar"), std::string::npos);
}

TEST(QmMonitoringHelpers, ScrollRegionsOnlyReserveRailsForRealOverflow)
{
	const std::string Header = ReadRepoFile("src/game/client/ui_scrollregion.h");
	const std::string Source = ReadRepoFile("src/game/client/ui_scrollregion.cpp");
	const std::string EndBody = ExtractSourceFunctionBody(Source, "void CScrollRegion::End()");
	const std::string ScrollbarShownBody = ExtractSourceFunctionBody(Source, "bool CScrollRegion::ScrollbarShown() const");
	const std::string UpdateHotScrollRegionBody = ExtractSourceFunctionBody(Source, "void CScrollRegion::UpdateHotScrollRegion()");
	ASSERT_FALSE(EndBody.empty());
	ASSERT_FALSE(ScrollbarShownBody.empty());
	ASSERT_FALSE(UpdateHotScrollRegionBody.empty());

	EXPECT_EQ(Header.find("m_ForceShowScrollbar"), std::string::npos);
	EXPECT_NE(EndBody.find("if(!ContentOverflows())"), std::string::npos);
	EXPECT_EQ(EndBody.find("m_ForceShowScrollbar"), std::string::npos);
	EXPECT_NE(ScrollbarShownBody.find("return !m_Params.m_HideScrollbar && ContentOverflows();"), std::string::npos);
	EXPECT_NE(UpdateHotScrollRegionBody.find("const CUIRect RegionRect = WheelHotRect();"), std::string::npos);
	EXPECT_NE(Source.find("if(ScrollbarShown())\n\t{"), std::string::npos);
}

TEST(QmMonitoringHelpers, ScrollRegionDelegatesMutableScrollStateToQmScrollState)
{
	const std::string Header = ReadRepoFile("src/game/client/ui_scrollregion.h");
	const std::string Source = ReadRepoFile("src/game/client/ui_scrollregion.cpp");
	const std::string AnimationBody = ExtractSourceFunctionBody(Source, "void CScrollRegion::AdvanceAnimation()");
	const std::string SliderBody = ExtractSourceFunctionBody(Source, "void CScrollRegion::DoSlider()");
	ASSERT_FALSE(AnimationBody.empty());
	ASSERT_FALSE(SliderBody.empty());

	EXPECT_NE(Header.find("CQmScrollState m_ScrollState;"), std::string::npos);
	EXPECT_EQ(Header.find("float m_ScrollPos;"), std::string::npos);
	EXPECT_EQ(Header.find("float m_AnimTimeMax;"), std::string::npos);
	EXPECT_EQ(Header.find("float m_AnimTime;"), std::string::npos);
	EXPECT_EQ(Header.find("float m_AnimInitScrollPos;"), std::string::npos);
	EXPECT_EQ(Header.find("float m_AnimTargetScrollPos;"), std::string::npos);
	EXPECT_EQ(Header.find("float m_SliderGrabPos;"), std::string::npos);
	EXPECT_NE(AnimationBody.find("m_ScrollState.Advance("), std::string::npos);
	EXPECT_NE(SliderBody.find("m_ScrollState.SetOffset("), std::string::npos);
}

TEST(QmMonitoringHelpers, WheelOwnershipFrameBeginsOnlyFromUiUpdate)
{
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp");
	const std::string Update = ExtractSourceFunctionBody(Ui, "void CUi::Update()");
	const std::string Begin = ExtractSourceFunctionBody(Ui, "void CUi::BeginWheelOwnershipFrame()");
	ASSERT_FALSE(Update.empty());
	ASSERT_FALSE(Begin.empty());
	EXPECT_EQ(CountSubstring(Ui, "BeginWheelOwnershipFrame();"), 1u);
	EXPECT_LT(Update.find("BeginWheelOwnershipFrame();"), Update.find("const vec2 WindowSize"));
	EXPECT_NE(Begin.find("const uint64_t FrameId = Client()->PerfFrame();"), std::string::npos);
	EXPECT_NE(Begin.find("m_WheelOwnership.FrameStarted(FrameId)"), std::string::npos);
	EXPECT_NE(Begin.find("m_WheelOwnership.BeginFrame(FrameId, RawDelta"), std::string::npos);
}
TEST(QmMonitoringHelpers, DropdownRegistersWheelOwnerBeforeParentCanConsume)
{
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp");
	const std::string ShowPopup = ExtractSourceFunctionBody(Ui, "void CUi::ShowPopupSelection(float X, float Y, SSelectionPopupContext *pContext)");
	const std::string Register = ExtractSourceFunctionBody(Ui, "void CUi::RegisterWheelOwner(const void *pOwnerId, EUiWheelOwnerPriority Priority, const CUIRect &HotRect, bool Eligible)");
	const std::string Consume = ExtractSourceFunctionBody(Ui, "bool CUi::TryConsumeWheel(const void *pOwnerId, float *pDelta)");
	const std::string Popup = ExtractSourceFunctionBody(Ui, "CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)");
	const std::string Region = ReadRepoFile("src/game/client/ui_scrollregion.cpp");
	const std::string Input = ExtractSourceFunctionBody(Region, "void CScrollRegion::DoScrollInput()");
	ASSERT_FALSE(ShowPopup.empty());
	ASSERT_FALSE(Register.empty());
	ASSERT_FALSE(Consume.empty());
	ASSERT_FALSE(Popup.empty());
	ASSERT_FALSE(Input.empty());
	EXPECT_NE(ShowPopup.find("RegisterWheelOwner(pContext"), std::string::npos);
	EXPECT_LT(ShowPopup.find("RegisterWheelOwner(pContext"), ShowPopup.find("DoPopupMenu("));
	EXPECT_NE(Register.find("QmRegisterWheelOwnerCandidate(m_WheelOwnership"), std::string::npos);
	EXPECT_NE(Consume.find("QmTryConsumeWheel(m_WheelOwnership, pOwnerId, pDelta)"), std::string::npos);
	EXPECT_NE(Popup.find("ScrollParams.m_pWheelOwnerId = pSelectionPopup"), std::string::npos);
	EXPECT_NE(Popup.find("ScrollParams.m_WheelOwnerPreRegistered = true"), std::string::npos);
	EXPECT_NE(Input.find("TryConsumeWheel(pWheelOwnerId"), std::string::npos);
	EXPECT_EQ(Input.find("ConsumeHotkey(CUi::HOTKEY_SCROLL_"), std::string::npos);
}
TEST(QmMonitoringHelpers, QmUiCardPresetCarriesQmClientSettingsStyle)
{
	const std::string Containers = ReadRepoFile("src/game/client/QmUi/UiContainers.h");
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string StyleBody = ExtractSourceFunctionBody(Menus, "CMenus::SQmSettingsCardStyle CMenus::QmSettingsCardStyle(float UiScale) const");
	ASSERT_FALSE(StyleBody.empty());

	EXPECT_NE(Containers.find("SCardProps QmClientCardProps(float UiScale = 1.0f, const SUiTheme *pTheme = nullptr)"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_Padding = 14.0f * UiScale;"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_Radius = 10.0f * UiScale;"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_DrawBorder = true;"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_FillColor = ColorRGBA(0.17f, 0.18f, 0.22f, 0.72f);"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_HighlightColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f);"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_BorderColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f);"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_FillColor = pTheme->m_Surface;"), std::string::npos);
	EXPECT_NE(Containers.find("Props.m_BorderColor = pTheme->m_Border;"), std::string::npos);
	EXPECT_NE(Containers.find("DrawRoundedSurface(Ctx, Rect, Props.m_FillColor"), std::string::npos);
	EXPECT_NE(Containers.find("Highlight.Draw(Props.m_HighlightColor"), std::string::npos);
	EXPECT_NE(Containers.find("BorderBg.Margin(-1.0f, &BorderBg);"), std::string::npos);
	EXPECT_NE(Containers.find("DrawRoundedSurface(Ctx, BorderBg, Props.m_BorderColor"), std::string::npos);
	EXPECT_EQ(Containers.find("Border.Margin(0.5f, &Border);"), std::string::npos);
	EXPECT_NE(Menus.find("#include <game/client/QmUi/UiContainers.h>"), std::string::npos);
	EXPECT_NE(StyleBody.find("const SUiTheme Theme = ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiFocusColor), ColorHSLA(g_Config.m_QmUiAccentColor), ColorHSLA(g_Config.m_QmUiSelectedColor));"), std::string::npos);
	EXPECT_NE(StyleBody.find("const ui_widget::SCardProps CardProps = ui_widget::QmClientCardProps(UiScale, &Theme);"), std::string::npos);
	EXPECT_NE(StyleBody.find("Style.m_Padding = CardProps.m_Padding;"), std::string::npos);
	EXPECT_NE(StyleBody.find("Style.m_CornerRadius = CardProps.m_Radius;"), std::string::npos);
	EXPECT_NE(StyleBody.find("Style.m_GlassColor = CardProps.m_FillColor;"), std::string::npos);
	EXPECT_NE(StyleBody.find("Style.m_HighlightColor = CardProps.m_HighlightColor;"), std::string::npos);
	EXPECT_NE(StyleBody.find("Style.m_HairlineColor = CardProps.m_BorderColor;"), std::string::npos);
	EXPECT_NE(MenusHeader.find("SQmSettingsCardStyle QmSettingsCardStyle(float UiScale) const;"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmUiPresenceBacksFavoriteCommunityTabs)
{
	const std::string Menus = ReadRepoFile("src/game/client/components/menus.cpp");
	const auto CountOccurrences = [](const std::string &Haystack, const char *pNeedle) {
		const std::string Needle = pNeedle;
		size_t Count = 0;
		size_t Pos = 0;
		while((Pos = Haystack.find(Needle, Pos)) != std::string::npos)
		{
			++Count;
			Pos += Needle.size();
		}
		return Count;
	};
	const auto ExtractFavoriteCommunityBlock = [](const std::string &Source, size_t Occurrence) {
		const std::string Anchor = "static CButtonContainer s_aFavoriteCommunityButtons[5];";
		size_t Pos = 0;
		for(size_t Index = 0; Index <= Occurrence; ++Index)
		{
			Pos = Source.find(Anchor, Pos);
			if(Pos == std::string::npos)
				return std::string();
			if(Index < Occurrence)
				Pos += Anchor.size();
		}
		const size_t End = Source.find("TextRender()->SetRenderFlags(0);", Pos);
		if(End == std::string::npos)
			return std::string();
		return Source.substr(Pos, End - Pos);
	};
	const std::string NewMenubarCommunityBlock = ExtractFavoriteCommunityBlock(Menus, 0);
	const std::string LegacyMenubarCommunityBlock = ExtractFavoriteCommunityBlock(Menus, 1);
	ASSERT_FALSE(NewMenubarCommunityBlock.empty());
	ASSERT_FALSE(LegacyMenubarCommunityBlock.empty());

	EXPECT_NE(Menus.find("#include <game/client/QmUi/QmTree.h>"), std::string::npos);
	EXPECT_NE(NewMenubarCommunityBlock.find("CUiV2Tree &Tree = GameClient()->UiRuntimeV2()->Tree();"), std::string::npos);
	EXPECT_NE(LegacyMenubarCommunityBlock.find("CUiV2Tree &Tree = GameClient()->UiRuntimeV2()->Tree();"), std::string::npos);
	EXPECT_EQ(CountOccurrences(Menus, "const SUiPresenceResult Presence = Tree.ResolvePresence(AnimRuntime, NodeKey, true, AppearTransition);"), 2u);
	EXPECT_EQ(CountOccurrences(Menus, "const float AppearStrength = std::clamp(Presence.m_Alpha, 0.0f, 1.0f);"), 2u);
	EXPECT_EQ(CountOccurrences(Menus, "const float RevealWidth = maximum(2.0f, Button.w * AppearStrength);"), 2u);
	EXPECT_EQ(CountOccurrences(Menus, "InactiveColor.a *= AppearStrength;"), 2u);
	EXPECT_EQ(CountOccurrences(Menus, "ActiveColor.a *= AppearStrength;"), 2u);
	EXPECT_EQ(CountOccurrences(Menus, "HoverColor.a *= AppearStrength;"), 2u);
	EXPECT_EQ(NewMenubarCommunityBlock.find("ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_EQ(LegacyMenubarCommunityBlock.find("ResolveUiAnimValue(AnimRuntime, NodeKey, EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_EQ(NewMenubarCommunityBlock.find("WasVisibleLastFrame"), std::string::npos);
	EXPECT_EQ(LegacyMenubarCommunityBlock.find("WasVisibleLastFrame"), std::string::npos);
	EXPECT_EQ(Menus.find("s_aPrevFavoriteCommunityAnimNodes"), std::string::npos);
	EXPECT_EQ(Menus.find("s_PrevFavoriteCommunityAnimNodeCount"), std::string::npos);
	EXPECT_EQ(Menus.find("WasVisibleLastFrame"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmUiPresenceBacksImeCandidatePopup)
{
	const std::string Source = ReadRepoFile("src/game/client/qm_ime_candidate_popup.cpp");
	const std::string Header = ReadRepoFile("src/game/client/qm_ime_candidate_popup.h");
	const std::string RenderBody = ExtractSourceFunctionBody(Source, "void CQmImeCandidatePopup::Render(CGameClient *pGameClient, const SQmImePopupState &State)");
	const std::string ResetBody = ExtractSourceFunctionBody(Source, "void CQmImeCandidatePopup::Reset()");
	ASSERT_FALSE(RenderBody.empty());
	ASSERT_FALSE(ResetBody.empty());

	EXPECT_NE(Source.find("#include \"QmUi/QmTree.h\""), std::string::npos);
	EXPECT_NE(Header.find("uint64_t m_PresenceGeneration = 1;"), std::string::npos);
	EXPECT_NE(ResetBody.find("++m_PresenceGeneration;"), std::string::npos);
	EXPECT_NE(ResetBody.find("m_Presentation = {};"), std::string::npos);
	EXPECT_NE(ResetBody.find("m_CandidateStart = 0;"), std::string::npos);
	EXPECT_NE(ResetBody.find("if(m_PresenceGeneration == 0)"), std::string::npos);
	EXPECT_NE(RenderBody.find("CUiV2Tree &Tree = pGameClient->UiRuntimeV2()->Tree();"), std::string::npos);
	EXPECT_NE(RenderBody.find("const uint64_t PopupKey = BuildUiAnimNodeKey(str_quickhash(\"qm_ime_popup\"), m_PresenceGeneration);"), std::string::npos);
	EXPECT_NE(RenderBody.find("const SUiPresenceResult Presence = Tree.ResolvePresence(AnimRuntime, PopupKey, TargetVisible, PresenceTransition);"), std::string::npos);
	EXPECT_NE(RenderBody.find("if(!Presence.m_Render)"), std::string::npos);
	EXPECT_NE(RenderBody.find("const float Alpha = minimum(Presence.m_Alpha, PresentationAlpha);"), std::string::npos);
	EXPECT_EQ(RenderBody.find("AnimRuntime.SetValue(PopupKey, EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_EQ(RenderBody.find("ResolveMotionValue(AnimRuntime, PopupKey, EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_NE(RenderBody.find("CUIRect Panel = Presentation.m_Rect;"), std::string::npos);
	EXPECT_NE(RenderBody.find("ResolveUiPresentationStateValue(AnimRuntime, CapsuleNode, EUiAnimProperty::POS_X"), std::string::npos);
	EXPECT_NE(RenderBody.find("const float CandidateDrawAlpha = Alpha * CandidateAlpha;"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmUiPresenceBacksFavoriteButtonVisibility)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "int CMenus::DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("CUiV2Tree &Tree = GameClient()->UiRuntimeV2()->Tree();"), std::string::npos);
	EXPECT_NE(Body.find("VisibilityTransition.m_DurationSec = 0.12f;"), std::string::npos);
	EXPECT_NE(Body.find("const SUiPresenceResult Visibility = Tree.ResolvePresence(AnimRuntime, VisibilityNodeKey, ShouldShow, VisibilityTransition);"), std::string::npos);
	EXPECT_NE(Body.find("const float ShowAlpha = std::clamp(Visibility.m_Alpha, 0.0f, 1.0f);"), std::string::npos);
	const size_t RenderBranchPos = Body.find("if(Visibility.m_Render && ShowAlpha > MENU_TAB_ANIM_EPSILON)");
	ASSERT_NE(RenderBranchPos, std::string::npos);
	const size_t ButtonLogicPos = Body.find("return Ui()->DoButtonLogic(pButtonId, 0, pRect, BUTTONFLAG_LEFT);");
	ASSERT_NE(ButtonLogicPos, std::string::npos);
	EXPECT_LT(RenderBranchPos, ButtonLogicPos);
	EXPECT_EQ(Body.find("ResolveUiAnimValue(AnimRuntime, VisibilityNodeKey, EUiAnimProperty::ALPHA"), std::string::npos);
	EXPECT_NE(Body.find("ResolveUiAnimValue(AnimRuntime, HoverNodeKey, EUiAnimProperty::SCALE"), std::string::npos);
}

TEST(QmMonitoringHelpers, QmUiScrollContainerContentDragIsOptIn)
{
	const std::string Containers = ReadRepoFile("src/game/client/QmUi/UiContainers.h");
	const std::string Dogfood = ReadRepoFile("src/game/client/QmUi/UiDogfood.cpp");

	EXPECT_NE(Containers.find("bool m_ContentDragAllowed = false;"), std::string::npos);
	EXPECT_NE(Containers.find("Input.m_ContentDragAllowed = Props.m_ContentDragAllowed;"), std::string::npos);
	EXPECT_NE(Containers.find("Input.m_ContentDragBlocked = Ctx.m_pUi->ActiveItem() != nullptr;"), std::string::npos);
	EXPECT_EQ(Dogfood.find("ScrollProps.m_ContentDragAllowed = true;"), std::string::npos);
}

TEST(QmMonitoringHelpers, SettingsPagesExposeSectionLevelPerfStages)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Controls = ReadRepoFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string TClient = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_NE(Settings.find("ddnet_tab_shell"), std::string::npos);
	EXPECT_NE(Settings.find("ddnet_demo_section"), std::string::npos);
	EXPECT_NE(Settings.find("ddnet_gameplay_section"), std::string::npos);
	EXPECT_NE(Settings.find("ddnet_controls_section"), std::string::npos);
	EXPECT_NE(Controls.find("controls_tab_shell"), std::string::npos);
	EXPECT_NE(Controls.find("controls_bind_list"), std::string::npos);
	EXPECT_NE(Controls.find("controls_interactive_layer"), std::string::npos);
	EXPECT_NE(Controls.find("controls_text_cache"), std::string::npos);
	EXPECT_NE(TClient.find("tclient_tab_3_shell"), std::string::npos);
	EXPECT_NE(TClient.find("tclient_tab_4_shell"), std::string::npos);
	EXPECT_NE(TClient.find("tclient_tab_5_shell"), std::string::npos);
}

TEST(QmMonitoringHelpers, HudAppearanceTabExposesSectionLevelPerfStages)
{
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Settings, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("appearance_hud_tab_shell"), std::string::npos);
	EXPECT_NE(Body.find("appearance_hud_core_section"), std::string::npos);
	EXPECT_NE(Body.find("appearance_hud_ddrace_section"), std::string::npos);
	EXPECT_NE(Body.find("appearance_hud_freeze_bars_section"), std::string::npos);
	EXPECT_NE(Body.find("appearance_hud_text_cache"), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientSettingsDoesNotWriteScrollMetadataBeforeFinish)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsTClientSettings(CUIRect MainView, bool PrewarmOnly)");
	ASSERT_FALSE(Body.empty());

	const size_t DeckRenderPos = Body.find("m_SettingsCardDeck.RenderCached(");
	ASSERT_NE(DeckRenderPos, std::string::npos);
	EXPECT_EQ(Body.find("BeginSettingsScrollRegion("), std::string::npos);
	EXPECT_EQ(Body.find("FinishSettingsScrollRegion("), std::string::npos);
	const std::string BeforeDeckRender = Body.substr(0, DeckRenderPos);
	EXPECT_EQ(BeforeDeckRender.find("m_SettingsRuntimeMetadata.m_LastScrollY ="), std::string::npos);
	EXPECT_EQ(BeforeDeckRender.find("m_SettingsRuntimeMetadata.m_LastScrollPage ="), std::string::npos);
}

TEST(QmMonitoringHelpers, TClientTabNamesInitializeBeforeLanguageChange)
{
	const std::string Source = ReadRepoFile("src/game/client/components/tclient/menus_tclient.cpp");
	const size_t TabNamesPos = Source.find("static const char *s_apTClientTabNames[NUMBER_OF_TCLIENT_TABS] = {};");
	ASSERT_NE(TabNamesPos, std::string::npos);
	const size_t FirstTabDrawPos = Source.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apTClientTabNames[Tab]", TabNamesPos);
	ASSERT_NE(FirstTabDrawPos, std::string::npos);
	const std::string TabNamesBody = Source.substr(TabNamesPos, FirstTabDrawPos - TabNamesPos);

	EXPECT_NE(TabNamesBody.find("s_TClientTabNamesInitialized"), std::string::npos);
	EXPECT_NE(TabNamesBody.find("!s_TClientTabNamesInitialized || str_comp"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuTextStyleKeyRejectsNonFiniteMaxWidth)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "CMenus::SMenuTextStyleKey CMenus::BuildMenuTextStyleKey");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("std::isfinite(MaxWidth)"), std::string::npos);
	EXPECT_NE(Body.find("MaxWidth >= 0.0f && std::isfinite(MaxWidth)"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuTextStyleKeyRejectsNonFiniteHiDpiScale)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "CMenus::SMenuTextStyleKey CMenus::BuildMenuTextStyleKey");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("const float HiDpiScale ="), std::string::npos);
	EXPECT_NE(Body.find("std::isfinite(HiDpiScale)"), std::string::npos);
	EXPECT_NE(Body.find("HiDpiScale >= 0.0f && std::isfinite(HiDpiScale)"), std::string::npos);
}

TEST(QmMonitoringHelpers, AppearanceTabNamesInitializeBeforeLanguageChange)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsAppearance(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t TabNamesPos = Body.find("static const char *s_apAppearanceTabNames[NUMBER_OF_APPEARANCE_TABS] = {};");
	ASSERT_NE(TabNamesPos, std::string::npos);
	const size_t FirstTabDrawPos = Body.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAppearanceTabNames[Tab]", TabNamesPos);
	ASSERT_NE(FirstTabDrawPos, std::string::npos);
	const std::string TabNamesBody = Body.substr(TabNamesPos, FirstTabDrawPos - TabNamesPos);

	EXPECT_NE(TabNamesBody.find("s_AppearanceTabNamesInitialized"), std::string::npos);
	EXPECT_NE(TabNamesBody.find("!s_AppearanceTabNamesInitialized || str_comp"), std::string::npos);
}

TEST(QmMonitoringHelpers, AssetsTabNamesInitializeBeforeLanguageChange)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Body = ExtractSourceFunctionBody(Source, "void CMenus::RenderSettingsCustom(CUIRect MainView)");
	ASSERT_FALSE(Body.empty());

	const size_t TabNamesPos = Body.find("static const char *s_apAssetsTabNames[NUMBER_OF_ASSETS_TABS] = {};");
	ASSERT_NE(TabNamesPos, std::string::npos);
	const size_t FirstTabDrawPos = Body.find("DoButton_MenuTab(&s_aPageTabs[Tab], s_apAssetsTabNames[Tab]", TabNamesPos);
	ASSERT_NE(FirstTabDrawPos, std::string::npos);
	const std::string TabNamesBody = Body.substr(TabNamesPos, FirstTabDrawPos - TabNamesPos);

	EXPECT_NE(TabNamesBody.find("s_AssetsTabNamesInitialized"), std::string::npos);
	EXPECT_NE(TabNamesBody.find("!s_AssetsTabNamesInitialized || str_comp"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuPerfEventsExposePageAttributionFields)
{
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("event=page_switch from=%s to=%s dur_ms=%.3f"), std::string::npos);
		EXPECT_NE(Source.find("event=section page=%s section=%s dur_ms=%.3f visible=%d dirty=%s text_new=%d text_reused=%d"), std::string::npos);
		EXPECT_NE(Source.find("LogSettingsSectionPerf(IClient *pClient, int Page, int Tab, const char *pSectionId, double DurationMs, const char *pDirtyReason, int TextNew, int TextReused)"), std::string::npos);
		EXPECT_NE(Source.find("pDirtyReason != nullptr ? pDirtyReason : \"unknown\""), std::string::npos);
		EXPECT_NE(Source.find("TextNew, TextReused"), std::string::npos);
		EXPECT_NE(Source.find("CScopedSettingsTextPerfStats TextStats(this);"), std::string::npos);
		EXPECT_NE(Source.find("TextStats.Stats().m_New"), std::string::npos);
		EXPECT_NE(Source.find("TextStats.Stats().m_Reused"), std::string::npos);
		EXPECT_EQ(Source.find("SectionCacheHit ? \"clean\" : \"cache_miss\", 0, 0"), std::string::npos);
		EXPECT_EQ(Source.find("\"cache_miss\""), std::string::npos);
		EXPECT_NE(Source.find("page=%s transition=0 sections=%d sections_visible=%d tab=%s"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus.h"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("class CScopedSettingsTextPerfStats"), std::string::npos);
		EXPECT_NE(Source.find("m_pActiveSettingsTextPerfStats = &m_Stats;"), std::string::npos);
		EXPECT_NE(Source.find("m_pActiveSettingsTextPerfStats = m_pPrevious;"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("event=page_switch from=%s to=%s dur_ms=%.3f source=menu_page_switch"), std::string::npos);
		EXPECT_NE(Source.find("event=page_switch from=%s to=%s dur_ms=%.3f source=game_page_switch"), std::string::npos);
		EXPECT_NE(Source.find("DoSettingsLabelStreamed"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/ui.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("pTextContainerRecreated"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/section_loader.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("void CSectionLoader::InvalidateCache(ESettingsCacheDirtyReason Reason)"), std::string::npos);
		EXPECT_EQ(Source.find("(void)Reason;"), std::string::npos);
		EXPECT_NE(Source.find("m_LastDirtyReason = Reason;"), std::string::npos);
		EXPECT_NE(Source.find("Section.m_Dirty = true;"), std::string::npos);
		EXPECT_NE(Source.find("event=section_loader sections_total=%d sections_visible=%d sections_skipped=%d layout_dirty=%d dirty_reason=%s"), std::string::npos);
		EXPECT_NE(Source.find("SettingsCacheDirtyReasonName(m_LastFrameStats.m_DirtyReason)"), std::string::npos);
		EXPECT_EQ(Source.find("*pDirtyReason = Section.m_DirtyReason"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus_demo.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("event=list_frame page=demo_browser items_total=%d rows_visible=%d rows_processed=%d rows_skipped=%d dur_ms=%.3f"), std::string::npos);
		EXPECT_NE(Source.find("const auto ListFrameStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();"), std::string::npos);
		EXPECT_NE(Source.find("const double ListFrameDurationMs = MenuUiPerfEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - ListFrameStartTime).count() : -1.0;"), std::string::npos);
		EXPECT_NE(Source.find("ListFrameDurationMs >= QmPerfThresholdMs()"), std::string::npos);
		EXPECT_NE(Source.find("ListFrameDurationMs);"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus_browser.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("event=list_frame page=server_browser items_total=%d rows_visible=%d rows_rendered=%d rows_iterated=%d rows_skipped=%d dur_ms=%.3f source=server_browser"), std::string::npos);
		EXPECT_NE(Source.find("QmPerfShouldLogDuration(ListFrameDurationMs, false)"), std::string::npos);
		EXPECT_NE(Source.find("const bool PerfListFrameEnabled = QmPerfEnabled();"), std::string::npos);
		EXPECT_NE(Source.find("RowsIterated += PerfListFrameEnabled ? 1 : 0;"), std::string::npos);
	}
	{
		std::ifstream File(TestSourcePath("src/game/client/components/menus_settings.cpp"));
		ASSERT_TRUE(File.good());
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();

		EXPECT_NE(Source.find("event=work_drain page=settings:tee kind=merge count=%llu bytes=%d dur_ms=%.3f stop=%s source=list_drain_summary scope=session"), std::string::npos);
	}
}

TEST(QmMonitoringHelpers, VulkanStandardLinePipelineCreatesTexturedVariant)
{
	std::ifstream File(TestSourcePath("src/engine/client/backend/vulkan/backend_vulkan.cpp"));
	ASSERT_TRUE(File.good());
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Source = Buffer.str();

	EXPECT_NE(Source.find("SPipelineContainer m_StandardLinePipeline;"), std::string::npos);
	EXPECT_NE(Source.find("GetStandardPipe(IsLineGeometry, IsTextured, BlendModeIndex, DynamicIndex)"), std::string::npos);
	EXPECT_NE(Source.find("GetStandardPipeLayout(IsLineGeometry, IsTextured, BlendModeIndex, DynamicIndex)"), std::string::npos);
	EXPECT_NE(Source.find("if(!CreateStandardGraphicsPipeline(\"shader/vulkan/prim.vert.spv\", \"shader/vulkan/prim.frag.spv\", false, true))"), std::string::npos);
	EXPECT_NE(Source.find("if(!CreateStandardGraphicsPipeline(\"shader/vulkan/prim_textured.vert.spv\", \"shader/vulkan/prim_textured.frag.spv\", true, true))"), std::string::npos);
}

TEST(QmMonitoringHelpers, VulkanDescriptorPoolFreeAvoidsUserVisibleAccountingAssert)
{
	const std::string Source = ReadRepoFile("src/engine/client/backend/vulkan/backend_vulkan.cpp");
	const std::string FreeBody = ExtractSourceFunctionBody(Source, "void FreeDescriptorSetFromPool(SDeviceDescriptorSet &DescrSet)");
	const std::string DestroyBody = ExtractSourceFunctionBody(Source, "void DestroyDescriptorPools()");
	const std::string CreateImageBody = ExtractSourceFunctionBody(Source, "[[nodiscard]] bool CreateImage(uint32_t Width, uint32_t Height, uint32_t Depth, size_t MipMapLevelCount, VkFormat Format, VkImageTiling Tiling, VkImage &Image, SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> &ImageMemory, VkImageUsageFlags ImageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM)");
	const std::string CreateRenderTargetDescriptorSetBody = ExtractSourceFunctionBody(Source, "[[nodiscard]] bool CreateRenderTargetDescriptorSet(SRenderTarget &Target, size_t DescrIndex)");
	const std::string CreateRenderTargetBody = ExtractSourceFunctionBody(Source, "[[nodiscard]] bool Cmd_RenderTarget_Create(const CCommandBuffer::SCommand_RenderTarget_Create *pCommand)");
	ASSERT_FALSE(FreeBody.empty());
	ASSERT_FALSE(DestroyBody.empty());
	ASSERT_FALSE(CreateImageBody.empty());
	ASSERT_FALSE(CreateRenderTargetDescriptorSetBody.empty());
	ASSERT_FALSE(CreateRenderTargetBody.empty());

	EXPECT_NE(DestroyBody.find("DestroyDescriptorPoolList"), std::string::npos);
	EXPECT_NE(DestroyBody.find("DescriptorPools.m_vPools.clear();"), std::string::npos);
	EXPECT_NE(FreeBody.find("DescrSet.m_PoolIndex >= DescrSet.m_pPools->m_vPools.size()"), std::string::npos);
	EXPECT_NE(FreeBody.find("descriptor set references stale pool index"), std::string::npos);
	EXPECT_NE(FreeBody.find("Pool.m_CurSize == 0"), std::string::npos);
	EXPECT_NE(FreeBody.find("descriptor pool accounting underflow prevented"), std::string::npos);
	EXPECT_EQ(FreeBody.find("dbg_assert(Pool.m_CurSize > 0"), std::string::npos);
	EXPECT_NE(CreateImageBody.find("Image = VK_NULL_HANDLE;"), std::string::npos);
	EXPECT_NE(CreateImageBody.find("return false;"), std::string::npos);
	EXPECT_LT(CreateImageBody.find("return false;"), CreateImageBody.find("vkGetImageMemoryRequirements("));
	EXPECT_NE(CreateImageBody.find("if(!GetImageMemory("), std::string::npos);
	EXPECT_NE(CreateImageBody.find("vkDestroyImage(m_VKDevice, Image, nullptr);"), std::string::npos);
	EXPECT_NE(CreateImageBody.find("if(vkBindImageMemory("), std::string::npos);
	EXPECT_NE(CreateImageBody.find("FreeImageMemBlock(ImageMemory);"), std::string::npos);
	EXPECT_NE(CreateImageBody.find("ImageMemory = {};"), std::string::npos);
	EXPECT_NE(CreateRenderTargetDescriptorSetBody.find("FreeDescriptorSetFromPool(DescrSet);"), std::string::npos);
	EXPECT_NE(CreateRenderTargetDescriptorSetBody.find("m_FrameProfileStats.m_DescriptorAllocations++;"), std::string::npos);
	EXPECT_NE(CreateRenderTargetBody.find("DestroyRenderTarget(Target);\n\t\t\treturn false;"), std::string::npos);
}

TEST(QmMonitoringHelpers, WindowsReleaseBuildProducesPdbSymbols)
{
	const std::string Source = ReadRepoFile("CMakeLists.txt");

	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:ProgramDatabase>"), std::string::npos);
	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:/DEBUG>"), std::string::npos);
	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF>"), std::string::npos);
	EXPECT_NE(Source.find("$<$<CONFIG:Release,RelWithDebInfo>:/OPT:ICF>"), std::string::npos);
}

// Phase A 阶段 1: IFrameScheduler service 抽出。独立 service 持有 per-consumer state，
// 让 scheduler 从 menus-private 升级为全局 service。本测试锁定 service 接口契约。
TEST(QmMonitoringHelpers, FrameSchedulerServiceExposesConsumerScopedInterface)
{
	const std::string Header = ReadRepoFile("src/game/client/frame_scheduler.h");
	const std::string Source = ReadRepoFile("src/game/client/frame_scheduler.cpp");
	const std::string Client = ReadRepoFile("src/engine/client/client.cpp");
	const std::string Cmake = ReadRepoFile("CMakeLists.txt");

	ASSERT_FALSE(Header.empty());
	ASSERT_FALSE(Source.empty());

	EXPECT_NE(Header.find("class IFrameScheduler : public IInterface"), std::string::npos);
	EXPECT_NE(Header.find("MACRO_INTERFACE(\"frame_scheduler\")"), std::string::npos);
	EXPECT_NE(Header.find("enum class EFrameSchedulerConsumer"), std::string::npos);
	EXPECT_NE(Header.find("SettingsText"), std::string::npos);
	EXPECT_NE(Header.find("IngameText"), std::string::npos);
	EXPECT_NE(Header.find("Assets"), std::string::npos);
	EXPECT_NE(Header.find("DemoBrowser"), std::string::npos);
	EXPECT_NE(Header.find("IngameServerInfo"), std::string::npos);
	EXPECT_NE(Header.find("Count"), std::string::npos);
	EXPECT_NE(Header.find("ComputeBudget"), std::string::npos);
	EXPECT_NE(Header.find("Reset()"), std::string::npos);
	EXPECT_NE(Header.find("BeginFrame"), std::string::npos);
	EXPECT_NE(Header.find("EndFrame"), std::string::npos);
	EXPECT_NE(Header.find("CreateFrameScheduler"), std::string::npos);

	EXPECT_NE(Source.find("SettingsAdaptiveBudgetStep(Input, m_aState"), std::string::npos);
	EXPECT_NE(Source.find("IFrameScheduler *CreateFrameScheduler()"), std::string::npos);

	EXPECT_NE(Client.find("#include <game/client/frame_scheduler.h>"), std::string::npos);
	EXPECT_NE(Client.find("IFrameScheduler *pFrameScheduler = CreateFrameScheduler();"), std::string::npos);
	EXPECT_NE(Client.find("pKernel->RegisterInterface(pFrameScheduler)"), std::string::npos);

	// CMakeLists.txt 必须显式列出 frame_scheduler.cpp 才会被纳入 GAME_CLIENT 目标；
	// set_src(GAME_CLIENT GLOB_RECURSE ...) 仅用 GLOB 校验与磁盘文件对齐，
	// 真正参与编译的是 ${ARGN} 显式列表（见 CMakeLists.txt set_glob 函数）。
	EXPECT_NE(Cmake.find("frame_scheduler.cpp"), std::string::npos);

	EXPECT_NE(Header.find("#include <game/client/components/settings_resource_jobs.h>"), std::string::npos);
}

// Phase A 阶段 2: CGameClient::OnRender 在帧入口/出口调用 IFrameScheduler 的 BeginFrame/EndFrame。
// 这是阶段 3 同步渲染路径消费 token 的前置：所有同步 UI 都在 frame scope 内执行。
TEST(QmMonitoringHelpers, OnRenderHooksFrameSchedulerBeginAndEndFrame)
{
	const std::string GameClientHeader = ReadRepoFile("src/game/client/gameclient.h");
	const std::string GameClientSource = ReadRepoFile("src/game/client/gameclient.cpp");

	ASSERT_FALSE(GameClientHeader.empty());
	ASSERT_FALSE(GameClientSource.empty());

	EXPECT_NE(GameClientHeader.find("class IFrameScheduler *m_pFrameScheduler = nullptr;"), std::string::npos);

	EXPECT_NE(GameClientSource.find("#include <game/client/frame_scheduler.h>"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pFrameScheduler = Kernel()->RequestInterface<IFrameScheduler>();"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pFrameScheduler->BeginFrame(Client()->PerfFrame());"), std::string::npos);
	EXPECT_NE(GameClientSource.find("m_pFrameScheduler->EndFrame();"), std::string::npos);
}

// Phase A 阶段 3 前置：把文档测试改成运行时行为测试。
// service 真正被调用并产出非平凡 token；per-consumer state 互相独立；
// LastOutput 反映最近一次 ComputeBudget 的结果。
TEST(QmMonitoringHelpers, FrameSchedulerServiceProducesRealTokensAndIsolatesConsumers)
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

TEST(QmMonitoringHelpers, FrameSchedulerResetClearsConsumerStateAndFrameScope)
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

TEST(QmMonitoringHelpers, MenuUiPerfFacadeKeepsOneStableSchemaAndDisabledFastPath)
{
	const std::string Header = ReadRepoFile("src/game/client/QmUi/QmUiPerf.h");
	const std::string Source = ReadRepoFile("src/game/client/QmUi/QmUiPerf.cpp");
	const std::string Cmake = ReadRepoFile("CMakeLists.txt");

	ASSERT_FALSE(Header.empty());
	ASSERT_FALSE(Source.empty());
	EXPECT_NE(Header.find("struct SQmMenuUiFramePerf"), std::string::npos);
	EXPECT_NE(Header.find("void QmLogMenuUiFramePerf(const SQmMenuUiFramePerf &Frame, const IClient *pClient);"), std::string::npos);
	EXPECT_LT(Source.find("if(!QmPerfEnabled())"), Source.find("str_format("));
	EXPECT_NE(Source.find("QmPerfLogPayload(\"perf/menu-ui\", pPayload, pClient);"), std::string::npos);

	const std::array<const char *, 16> apFields = {
		"event=menu_ui_frame", "page=%s", "operation=%s", "frame=%", "items_total=%d", "items_visible=%d",
		"items_processed=%d", "items_skipped=%d", "ui_ms=%.3f", "layout_ms=%.3f", "text_ms=%.3f", "heap_allocs=%d",
		"cache_hits=%d", "cache_misses=%d", "cache_evictions=%d", "source=qm_ui_perf"};
	for(const char *pField : apFields)
		EXPECT_NE(Source.find(pField), std::string::npos) << pField;

	EXPECT_NE(Cmake.find("QmUi/QmUiPerf.cpp"), std::string::npos);
	EXPECT_NE(Cmake.find("QmUi/QmUiPerf.h"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuUiPerfTreatsImmediateWheelConsumptionAsActiveScroll)
{
	EXPECT_TRUE(QmMenuUiScrollPerfActive(true, false, false));
	EXPECT_TRUE(QmMenuUiScrollPerfActive(false, true, false));
	EXPECT_TRUE(QmMenuUiScrollPerfActive(false, false, true));
	EXPECT_FALSE(QmMenuUiScrollPerfActive(false, false, false));
}

TEST(QmMonitoringHelpers, MenuUiCacheBoundaryConstantsAreExplicitAndBounded)
{
	const std::string PerfHeader = ReadRepoFile("src/game/client/QmUi/QmUiPerf.h");
	const std::string AnimHeader = ReadRepoFile("src/game/client/QmUi/QmAnim.h");
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string SettingsSource = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string AssetsSource = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_EQ(PerfHeader.find("QM_MENU_PAGE_LAYOUT_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(PerfHeader.find("QM_MENU_TEXT_CACHE_CAPACITY = 4096"), std::string::npos);
	EXPECT_NE(PerfHeader.find("QM_MENU_TEXT_CACHE_MAX_AGE_FRAMES = 600"), std::string::npos);
	EXPECT_EQ(PerfHeader.find("QM_MENU_FILTER_GENERATIONS"), std::string::npos);
	EXPECT_NE(PerfHeader.find("QM_ASSET_METADATA_CACHE_CAPACITY = 512"), std::string::npos);
	EXPECT_NE(PerfHeader.find("QM_TEE_PREVIEW_CACHE_CAPACITY = 192"), std::string::npos);
	EXPECT_NE(PerfHeader.find("QM_LANGUAGE_ROW_CACHE_CAPACITY = 128"), std::string::npos);
	EXPECT_NE(AnimHeader.find("MAX_LAST_TARGETS_SOFT = 4096"), std::string::npos);
	EXPECT_NE(AnimHeader.find("MAX_LAST_TARGETS_HARD = 8192"), std::string::npos);
	EXPECT_NE(MenusHeader.find("#include <game/client/QmUi/QmUiPerf.h>"), std::string::npos);
	EXPECT_NE(ReadRepoFile("src/game/client/components/menus.cpp").find("QM_MENU_TEXT_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(SettingsSource.find("QM_TEE_PREVIEW_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(SettingsSource.find("QM_LANGUAGE_ROW_CACHE_CAPACITY"), std::string::npos);
	EXPECT_NE(AssetsSource.find("QM_ASSET_METADATA_CACHE_CAPACITY"), std::string::npos);
}

TEST(QmMonitoringHelpers, MenuTextCacheEvictionCancelsPendingBuildRequestsBeforeErase)
{
	const std::string Source = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string TrimBody = ExtractSourceFunctionBody(Source, "int CMenus::TrimMenuTextPoolForInsert(uint64_t CurrentFrame)");
	const std::string InvalidateBody = ExtractSourceFunctionBody(Source, "void CMenus::InvalidateMenuTextPool(const char *pReason)");

	ASSERT_FALSE(TrimBody.empty());
	ASSERT_FALSE(InvalidateBody.empty());
	EXPECT_LT(TrimBody.find("RemoveMenuTextContainerBuildRequest(It->second.m_Element);"), TrimBody.find("m_MenuTextPool.erase(It)"));
	EXPECT_LT(TrimBody.find("RemoveMenuTextContainerBuildRequest(Oldest->second.m_Element);"), TrimBody.find("m_MenuTextPool.erase(Oldest)"));
	EXPECT_LT(InvalidateBody.find("m_vMenuTextContainerBuildRequests.clear();"), InvalidateBody.find("m_MenuTextPool.clear();"));
}

TEST(QmMonitoringHelpers, MenuUiPerfOperationsAreEmittedFromRealListOwners)
{
	const auto Count = [](const std::string &Haystack, const char *pNeedle) {
		size_t Result = 0;
		for(size_t Pos = 0; (Pos = Haystack.find(pNeedle, Pos)) != std::string::npos; Pos += str_length(pNeedle))
			++Result;
		return Result;
	};
	const std::string Browser = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string Demo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string Ui = ReadRepoFile("src/game/client/ui.cpp");
	const std::string Scroll = ReadRepoFile("src/game/client/ui_scrollregion.cpp");

	for(const char *pOperation : {"server_browser_scroll", "friends_scroll"})
		EXPECT_NE(Browser.find(pOperation), std::string::npos) << pOperation;
	EXPECT_NE(Demo.find("demo_browser_scroll"), std::string::npos);
	for(const char *pOperation : {"skins_grid_scroll", "flags_grid_scroll", "language_list_scroll"})
		EXPECT_NE(Settings.find(pOperation), std::string::npos) << pOperation;
	EXPECT_NE(Assets.find("assets_grid_scroll"), std::string::npos);
	EXPECT_NE(Ui.find("dropdown_first_wheel"), std::string::npos);
	EXPECT_NE(Ui.find("WheelConsumedThisFrame()"), std::string::npos);
	EXPECT_NE(Scroll.find("m_WheelConsumedThisFrame = true;"), std::string::npos);

	EXPECT_EQ(Count(Browser, "QmLogMenuUiFramePerf("), 2u);
	EXPECT_EQ(Count(Demo, "QmLogMenuUiFramePerf("), 1u);
	EXPECT_GE(Count(Settings, "QmLogMenuUiFramePerf("), 3u);
	EXPECT_GE(Count(Assets, "QmLogMenuUiFramePerf("), 2u);
}

TEST(QmMonitoringHelpers, SettingsUiMigrationFinalStructureContract)
{
	const std::array<const char *, 11> apRetiredSymbols = {
		"RenderQmSettingsGlassCard",
		"BeginSettingsCardDeck",
		"RegisterSettingsCardDeckItemFromFrame",
		"LegacyTextFieldEx",
		"DoSettingsSliderInputField",
		"m_TClientSettingsCardDragState",
		"m_SettingsCardDeckOrders",
		"ForceShowScrollbar",
		"CachedHeightForStableCardId",
		"DrawTClientCacheSectionBox",
		"InsetTClientCacheSectionContent",
	};
	const std::filesystem::path ClientPath = TestSourcePath("src/game/client");
	for(const std::filesystem::directory_entry &Entry : std::filesystem::recursive_directory_iterator(ClientPath))
	{
		if(!Entry.is_regular_file() || (Entry.path().extension() != ".cpp" && Entry.path().extension() != ".h"))
			continue;
		std::ifstream File(Entry.path());
		ASSERT_TRUE(File.good()) << Entry.path().string();
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const std::string Source = Buffer.str();
		for(const char *pSymbol : apRetiredSymbols)
			EXPECT_EQ(Source.find(pSymbol), std::string::npos) << Entry.path().string() << ": " << pSymbol;
	}

	const std::string ScrollHeader = ReadRepoFile("src/game/client/ui_scrollregion.h");
	EXPECT_EQ(CountSubstring(ScrollHeader, "CQmScrollState m_ScrollState;"), 1u);
	EXPECT_EQ(ScrollHeader.find("float m_ScrollPos;"), std::string::npos);
	EXPECT_EQ(ScrollHeader.find("float m_AnimTargetScrollPos;"), std::string::npos);

	const std::array<std::pair<const char *, const char *>, 4> aNonCardScopes = {{
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFilters(CUIRect View)"},
		{"src/game/client/components/menus_browser.cpp", "void CMenus::RenderServerbrowserFriends(CUIRect View)"},
		{"src/game/client/components/menus_settings.cpp", "bool CMenus::RenderLanguageSelection(CUIRect MainView, const SSettingsContentMetrics *pMetrics)"},
	}};
	for(const auto &[pPath, pSignature] : aNonCardScopes)
	{
		const std::string Body = ExtractSourceFunctionBody(ReadRepoFile(pPath), pSignature);
		ASSERT_FALSE(Body.empty()) << pSignature;
		EXPECT_EQ(Body.find("SettingsCard("), std::string::npos) << pSignature;
		EXPECT_EQ(Body.find("m_SettingsCardDeck.Render("), std::string::npos) << pSignature;
		EXPECT_EQ(Body.find("m_SettingsCardDeck.RenderCached("), std::string::npos) << pSignature;
	}
}

TEST(QmMonitoringHelpers, MenuUiPerfScrollOwnersGateSamplesAndReuseOneFpsTracker)
{
	const std::string MenusHeader = ReadRepoFile("src/game/client/components/menus.h");
	const std::string MenusSource = ReadRepoFile("src/game/client/components/menus.cpp");
	const std::string Browser = ReadRepoFile("src/game/client/components/menus_browser.cpp");
	const std::string Demo = ReadRepoFile("src/game/client/components/menus_demo.cpp");
	const std::string Settings = ReadRepoFile("src/game/client/components/menus_settings.cpp");
	const std::string Assets = ReadRepoFile("src/game/client/components/menus_settings_assets.cpp");
	const std::string ListBoxHeader = ReadRepoFile("src/game/client/ui_listbox.h");
	const std::string UiHeader = ReadRepoFile("src/game/client/ui.h");
	const std::string UiSource = ReadRepoFile("src/game/client/ui.cpp");

	EXPECT_NE(MenusHeader.find("StartSettingsPerfScrollWindow(const char *pOperation"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_SettingsPerfWindowTracker.EnsureScrollWindow("), std::string::npos);
	EXPECT_NE(MenusSource.find("Ui()->ConsumeMenuUiFirstWheelPerf()"), std::string::npos);
	EXPECT_NE(UiHeader.find("bool ConsumeMenuUiFirstWheelPerf()"), std::string::npos);
	EXPECT_NE(UiSource.find("pUI->m_MenuUiFirstWheelPerf = MenuUiPerfEnabled;"), std::string::npos);
	EXPECT_NE(UiSource.find("m_MenuUiFirstWheelPerf = false;"), std::string::npos);
	EXPECT_NE(ListBoxHeader.find("bool WheelConsumedThisFrame() const { return m_ScrollRegion.WheelConsumedThisFrame(); }"), std::string::npos);

	for(const auto &[Source, Guard] : std::array<std::pair<const std::string *, const char *>, 7>{
		    std::pair{&Browser, "if(ListScrollActive)"},
		    std::pair{&Browser, "if(FriendsScrollActive)"},
		    std::pair{&Demo, "if(ListScrollActive)"},
		    std::pair{&Settings, "if(FlagListScrollActive)"},
		    std::pair{&Settings, "if(SkinListScrollActive)"},
		    std::pair{&Settings, "if(LanguageScrollActive)"},
		    std::pair{&Assets, "if(ListScrollActive)"}})
		EXPECT_NE(Source->find(Guard), std::string::npos) << Guard;
	for(const std::string *pSource : {&Browser, &Demo, &Settings, &Assets})
		EXPECT_NE(pSource->find("WheelConsumedThisFrame()"), std::string::npos);
}

TEST(QmMonitoringHelpers, AndroidBundledCryptoUsesBoringSslAndSystemCertificates)
{
	const std::string FindCrypto = ReadRepoFile("cmake/FindCrypto.cmake");
	const std::string Http = ReadRepoFile("src/engine/shared/http.cpp");

	EXPECT_NE(FindCrypto.find("set_extra_dirs_lib(CRYPTO boringssl)"), std::string::npos);
	EXPECT_EQ(FindCrypto.find("set_extra_dirs_lib(CRYPTO openssl)"), std::string::npos);
	EXPECT_NE(Http.find("curl_easy_setopt(pH, CURLOPT_CAPATH, \"/system/etc/security/cacerts\");"), std::string::npos);
	EXPECT_EQ(Http.find("curl_easy_setopt(pH, CURLOPT_CAINFO, \"data/cacert.pem\");"), std::string::npos);
}

TEST(QmMonitoringHelpers, EntitiesBackgroundExplicitlyDrawsConfiguredColor)
{
	const std::string Header = ReadRepoFile("src/game/client/components/background.h");
	const std::string Source = ReadRepoFile("src/game/client/components/background.cpp");
	const std::string Render = ExtractSourceFunctionBody(Source, "void CBackground::OnRender()");
	const std::string RenderCustom = ExtractSourceFunctionBody(Source, "bool CBackground::RenderCustom(const vec2 &Center, float Zoom)");
	const std::string RenderColor = ExtractSourceFunctionBody(Source, "void CBackground::RenderBackgroundColor()");
	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(RenderCustom.empty());
	ASSERT_FALSE(RenderColor.empty());

	EXPECT_NE(Header.find("void RenderBackgroundColor();"), std::string::npos);
	EXPECT_NE(RenderColor.find("g_Config.m_ClBackgroundEntitiesColor"), std::string::npos);
	EXPECT_NE(RenderColor.find("Graphics()->DrawRect("), std::string::npos);
	EXPECT_LT(Render.find("RenderBackgroundColor();"), Render.find("if(!m_Loaded)"));
	EXPECT_LT(RenderCustom.find("RenderBackgroundColor();"), RenderCustom.find("if(!m_Loaded)"));
}
