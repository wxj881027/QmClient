#include <base/logger.h>
#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/qmclient/stutter_capture.h>

#include <gtest/gtest.h>
#include <test/support/perf_config_manager.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{
	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

	class CStutterLog : public ILogger
	{
	public:
		std::vector<std::string> m_vMessages;
		void Log(const CLogMessage *pMessage) override
		{
			if(str_comp(pMessage->m_aSystem, "perf/stutter") == 0)
				m_vMessages.emplace_back(pMessage->Message());
		}
	};

	class CObservedOperations : public CQmStutterCapture::COperations
	{
	public:
		bool m_FailCapture = false;
		int m_FailOutputAt = -1;
		int m_OutputCount = 0;
		CQmStutterCapture::SFeatures CaptureFeatures(const CQmStutterCapture::SFrame &Frame) override
		{
			auto Features = COperations::CaptureFeatures(Frame);
			if(m_FailCapture)
				throw std::bad_alloc();
			return Features;
		}
		void Emit(const CQmStutterCapture::SFrame &Frame, bool Force, const char *pPayload) override
		{
			COperations::Emit(Frame, Force, pPayload);
			if(++m_OutputCount == m_FailOutputAt)
				throw std::bad_alloc();
		}
	};

	class QmStutterCaptureLogging : public ::testing::Test
	{
		CConfig m_SavedConfig = g_Config;

	protected:
		CStutterLog m_Log;
		CLogScope m_Scope{&m_Log};
		int m_Tc = 0, m_Mode = 0, m_Cl = 0, m_Disabled = 0, m_Signed = 0, m_Gfx = 0;
		char m_aSecret[128] = "";
		char m_aOldSecret[128] = "";
		SIntConfigVariable m_TcVar{nullptr, "tc_flag", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Tc, 1, 0, 1};
		SIntConfigVariable m_ModeVar{nullptr, "qm_mode", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Mode, 3, 0, 10};
		SIntConfigVariable m_ClVar{nullptr, "cl_flag", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Cl, 1, 0, 1};
		SIntConfigVariable m_DisabledVar{nullptr, "cl_disabled", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Disabled, 0, 0, 1};
		SIntConfigVariable m_SignedVar{nullptr, "qm_signed", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Signed, 1, -1, 10};
		SIntConfigVariable m_GfxVar{nullptr, "gfx_flag", SConfigVariable::VAR_INT, CFGFLAG_CLIENT, "", "", &m_Gfx, 1, 0, 1};
		SStringConfigVariable m_SecretVar{nullptr, "qm_token", SConfigVariable::VAR_STRING, CFGFLAG_CLIENT, "", "", m_aSecret, "private-content", sizeof(m_aSecret), m_aOldSecret};
		CPerfConfigManager m_Manager;
		CObservedOperations m_Operations;
		CQmStutterCapture m_Capture{&m_Operations};
		const char *m_apNames[2] = {"menus", "hud"};
		std::string m_Page = "settings";
		std::string m_Operation = "scroll";
		EQmStutterLimitCause m_Cause = EQmStutterLimitCause::NONE;

		void SetUp() override
		{
			g_Config.m_QmPerfDebug = 0;
			g_Config.m_QmPerfLogfile = 0;
			g_Config.m_QmPerfStutterDiagnostics = 1;
			m_Manager.m_vVariables = {&m_TcVar, &m_ModeVar, &m_ClVar, &m_DisabledVar, &m_SignedVar, &m_GfxVar, &m_SecretVar};
			ASSERT_TRUE(m_Capture.PrepareComponents(2));
		}
		void TearDown() override { g_Config = m_SavedConfig; }

		CQmStutterCapture::SFrame Frame(uint64_t Id, double Duration, bool Enabled = true)
		{
			CQmStutterCapture::SFrame Result;
			Result.m_Frame = Id;
			Result.m_FrameMs = Duration;
			Result.m_Enabled = Enabled;
			Result.m_Online = true;
			Result.m_ppComponentNames = m_apNames;
			Result.m_ComponentCount = 2;
			Result.m_pConfig = &m_Manager;
			Result.m_pUser = this;
			Result.m_pfnUiContext = [](void *pUser) {
				auto &Test = *static_cast<QmStutterCaptureLogging *>(pUser);
				return CQmStutterCapture::SUiContext{Test.m_Page.c_str(), Test.m_Operation.c_str(), Test.m_Cause};
			};
			return Result;
		}
		TJson Parse(size_t Index)
		{
			const auto &Message = m_Log.m_vMessages.at(Index);
			return TJson(JsonParse(Message.c_str(), Message.size()), json_value_free);
		}
		std::vector<size_t> Events(const char *pName)
		{
			std::vector<size_t> vIndices;
			for(size_t Index = 0; Index < m_Log.m_vMessages.size(); ++Index)
			{
				const auto Root = Parse(Index);
				EXPECT_NE(Root, nullptr);
				if(!Root)
					continue;
				const auto *pEvent = json_object_get(Root.get(), "event");
				if(pEvent->type == json_string && str_comp(pEvent->u.string.ptr, pName) == 0)
					vIndices.push_back(Index);
			}
			return vIndices;
		}
	};

	void ExpectString(const json_value *pRoot, const char *pKey, const char *pExpected)
	{
		const auto *pValue = json_object_get(pRoot, pKey);
		ASSERT_EQ(pValue->type, json_string) << pKey;
		EXPECT_STREQ(pValue->u.string.ptr, pExpected) << pKey;
	}
	void ExpectInteger(const json_value *pRoot, const char *pKey, int64_t Expected)
	{
		const auto *pValue = json_object_get(pRoot, pKey);
		ASSERT_EQ(pValue->type, json_integer) << pKey;
		EXPECT_EQ(pValue->u.integer, Expected) << pKey;
	}
	void ExpectDouble(const json_value *pRoot, const char *pKey, double Expected)
	{
		const auto *pValue = json_object_get(pRoot, pKey);
		ASSERT_EQ(pValue->type, json_double) << pKey;
		EXPECT_NEAR(pValue->u.dbl, Expected, 0.001) << pKey;
	}
}

TEST_F(QmStutterCaptureLogging, ShutdownPreservesSummaryComponentsAndFilteredFeatureSchema)
{
	m_Cause = EQmStutterLimitCause::CONFIGURED_LIMIT;
	m_Capture.RecordUpdate(0, 2.125);
	m_Capture.RecordRender(0, 3.5);
	m_Capture.RecordUpdate(1, 4.25);
	m_Capture.RecordRender(1, 5.75);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(10, 20.0)));
	ASSERT_TRUE(m_Capture.Finish(Frame(11, 0.0)));
	ASSERT_EQ(m_Log.m_vMessages.size(), 8u);
	const auto Summary = Parse(0);
	ASSERT_NE(Summary, nullptr);
	ExpectString(Summary.get(), "system", "perf/stutter");
	ExpectString(Summary.get(), "event", "stutter_event");
	ExpectString(Summary.get(), "end_reason", "shutdown");
	ExpectString(Summary.get(), "classification", "frame_drop");
	ExpectString(Summary.get(), "page", "settings");
	ExpectString(Summary.get(), "context", "online");
	ExpectString(Summary.get(), "cap_reason", "configured_limit");
	ExpectInteger(Summary.get(), "schema", 1);
	ExpectInteger(Summary.get(), "stutter_id", 1);
	ExpectInteger(Summary.get(), "segment", 0);
	ExpectInteger(Summary.get(), "sample_frames", 1);
	ExpectInteger(Summary.get(), "worst_frame", 10);
	ExpectInteger(Summary.get(), "window_start_frame", 10);
	ExpectInteger(Summary.get(), "window_end_frame", 10);
	ExpectInteger(Summary.get(), "target_fps", 300);
	ExpectInteger(Summary.get(), "cap_limited", 1);
	ExpectDouble(Summary.get(), "frame_ms_avg", 20.0);
	ExpectDouble(Summary.get(), "frame_ms_p95", 20.0);
	ExpectDouble(Summary.get(), "frame_ms_p99", 20.0);
	const auto Component = Parse(1);
	ASSERT_NE(Component, nullptr);
	ExpectString(Component.get(), "module", "menus");
	ExpectString(Component.get(), "callback", "on_update");
	ExpectInteger(Component.get(), "sample_count", 1);
	ExpectInteger(Component.get(), "max_frame", 10);
	ExpectDouble(Component.get(), "total_ms", 2.125);
	const char *apFeatures[] = {"cl_flag", "qm_mode", "tc_flag"};
	const char *apOwners[] = {"ddnet", "qmclient", "tclient"};
	for(size_t Index = 0; Index < 3; ++Index)
	{
		const auto Feature = Parse(5 + Index);
		ASSERT_NE(Feature, nullptr);
		ExpectString(Feature.get(), "feature", apFeatures[Index]);
		ExpectString(Feature.get(), "owner", apOwners[Index]);
		ExpectString(Feature.get(), "current_page", "settings");
		ExpectInteger(Feature.get(), "config_value", Index == 1 ? 3 : 1);
	}
	for(const auto &Message : m_Log.m_vMessages)
		EXPECT_EQ(Message.find("private-content"), std::string::npos);
}

TEST_F(QmStutterCaptureLogging, RecoveryDropsFastPendingSamplesAndNextEpisodeUsesFreshSnapshot)
{
	m_Capture.RecordUpdate(0, 0.5);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(1, 6.0)));
	for(uint64_t Id = 2; Id <= 335; ++Id)
	{
		m_Capture.RecordUpdate(0, 100.0);
		ASSERT_TRUE(m_Capture.ProcessFrame(Frame(Id, 3.0)));
	}
	ASSERT_EQ(m_Log.m_vMessages.size(), 8u);
	const auto Recovered = Parse(0);
	ASSERT_NE(Recovered, nullptr);
	ExpectString(Recovered.get(), "end_reason", "recovered");
	const auto Component = Parse(1);
	ASSERT_NE(Component, nullptr);
	ExpectDouble(Component.get(), "total_ms", 0.5);
	m_Page = "game";
	m_Tc = 0;
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(336, 7.0)));
	ASSERT_TRUE(m_Capture.Finish(Frame(337, 0.0)));
	const auto Summaries = Events("stutter_event");
	ASSERT_EQ(Summaries.size(), 2u);
	const auto Next = Parse(Summaries[1]);
	ASSERT_NE(Next, nullptr);
	ExpectInteger(Next.get(), "stutter_id", 2);
	ExpectInteger(Next.get(), "sample_frames", 1);
	ExpectString(Next.get(), "page", "game");
	EXPECT_EQ(Events("feature_snapshot").size(), 5u);
}

TEST_F(QmStutterCaptureLogging, PartialConfigEnumerationDoesNotReplaceEarlierWorstSnapshot)
{
	m_Capture.RecordUpdate(0, 2.0);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(1, 10.0)));
	m_Page = "other";
	m_Mode = 4;
	m_Manager.m_FailAfter = 1;
	m_Capture.RecordUpdate(0, 100.0);
	EXPECT_FALSE(m_Capture.ProcessFrame(Frame(2, 20.0)));
	m_Manager.m_FailAfter = -1;
	m_Capture.RecordUpdate(0, 3.0);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(3, 8.0)));
	ASSERT_TRUE(m_Capture.Finish(Frame(4, 0.0)));
	ASSERT_EQ(m_Log.m_vMessages.size(), 8u);
	const auto Summary = Parse(0);
	ASSERT_NE(Summary, nullptr);
	ExpectInteger(Summary.get(), "sample_frames", 2);
	ExpectInteger(Summary.get(), "worst_frame", 1);
	ExpectString(Summary.get(), "page", "settings");
	const auto Component = Parse(1);
	ASSERT_NE(Component, nullptr);
	ExpectDouble(Component.get(), "total_ms", 5.0);
	const auto Mode = Parse(6);
	ASSERT_NE(Mode, nullptr);
	ExpectString(Mode.get(), "feature", "qm_mode");
	ExpectInteger(Mode.get(), "config_value", 3);
}

TEST_F(QmStutterCaptureLogging, ExceptionAfterCompleteSnapshotStillKeepsOldOwnedContext)
{
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(1, 6.0)));
	m_Page = "replacement-page";
	m_Mode = 4;
	m_Operations.m_FailCapture = true;
	EXPECT_FALSE(m_Capture.ProcessFrame(Frame(2, 12.0)));
	EXPECT_EQ(m_Capture.SampleCount(), 1u);
	EXPECT_EQ(m_Capture.WorstFrame(), 1u);
	ASSERT_TRUE(m_Capture.Finish(Frame(3, 0.0)));
	const auto Summary = Parse(0);
	ASSERT_NE(Summary, nullptr);
	ExpectString(Summary.get(), "page", "settings");
	const auto Mode = Parse(6);
	ASSERT_NE(Mode, nullptr);
	ExpectInteger(Mode.get(), "config_value", 3);
}

TEST_F(QmStutterCaptureLogging, DisablingUsesForcedOutputWhenAllPerformanceFlagsAreOff)
{
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(1, 6.0)));
	g_Config.m_QmPerfStutterDiagnostics = 0;
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(2, 0.0, false)));
	ASSERT_EQ(m_Log.m_vMessages.size(), 8u);
	const auto Summary = Parse(0);
	ASSERT_NE(Summary, nullptr);
	ExpectString(Summary.get(), "end_reason", "diagnostics_disabled");
	EXPECT_FALSE(m_Capture.Active());
	EXPECT_TRUE(m_Capture.ProcessFrame(Frame(3, 0.0, false)));
	EXPECT_EQ(m_Log.m_vMessages.size(), 8u);
}

TEST_F(QmStutterCaptureLogging, OutputExceptionDoesNotReplayPartiallyDeliveredWindow)
{
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(1, 6.0)));
	m_Operations.m_FailOutputAt = 2;
	EXPECT_FALSE(m_Capture.Finish(Frame(2, 0.0)));
	ASSERT_EQ(m_Log.m_vMessages.size(), 8u);
	EXPECT_EQ(m_Operations.m_OutputCount, 8);
	EXPECT_TRUE(m_Capture.Finish(Frame(3, 0.0)));
	EXPECT_EQ(m_Log.m_vMessages.size(), 8u);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(4, 7.0)));
	ASSERT_TRUE(m_Capture.Finish(Frame(5, 0.0)));
	EXPECT_EQ(m_Log.m_vMessages.size(), 16u);
	EXPECT_EQ(Events("stutter_event").size(), 2u);
}

TEST_F(QmStutterCaptureLogging, PeriodicSegmentKeepsEpisodeAndOwnsOnlyItsWindowSamples)
{
	m_Capture.RecordUpdate(0, 1.0);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(1, 9999.0)));
	m_Capture.RecordUpdate(0, 3.0);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(2, 6.0)));
	ASSERT_EQ(m_Log.m_vMessages.size(), 8u);
	const auto Periodic = Parse(0);
	ASSERT_NE(Periodic, nullptr);
	ExpectString(Periodic.get(), "end_reason", "periodic");
	ExpectInteger(Periodic.get(), "sample_frames", 2);
	m_Page = "next-window";
	m_Capture.RecordUpdate(0, 2.0);
	ASSERT_TRUE(m_Capture.ProcessFrame(Frame(3, 7.0)));
	ASSERT_TRUE(m_Capture.Finish(Frame(4, 0.0)));
	ASSERT_EQ(m_Log.m_vMessages.size(), 16u);
	const auto Next = Parse(8);
	ASSERT_NE(Next, nullptr);
	ExpectInteger(Next.get(), "stutter_id", 1);
	ExpectInteger(Next.get(), "segment", 1);
	ExpectInteger(Next.get(), "sample_frames", 1);
	ExpectString(Next.get(), "page", "next-window");
	const auto Component = Parse(9);
	ASSERT_NE(Component, nullptr);
	ExpectDouble(Component.get(), "total_ms", 2.0);
}
