// 通过生产日志入口、线程局部接收器与 JSON 解析验证最终记录。
#include <base/logger.h>

#include <engine/shared/json.h>

#include <game/client/QmUi/QmUiPerf.h>
#include <game/client/components/qmclient/perf_logging.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
	class CMenuPerfCapture : public ILogger
	{
	public:
		std::vector<std::string> m_vMessages;
		void Log(const CLogMessage *pMessage) override
		{
			if(str_comp(pMessage->m_aSystem, "perf/menu-ui") == 0)
				m_vMessages.emplace_back(pMessage->Message());
		}
	};

	class QmMenuUiPerfLogging : public ::testing::Test
	{
		CConfig m_SavedConfig = g_Config;
		CQmPerfDetailBudget m_SavedBudget;
		uint64_t m_SavedSession = 0;
		uint64_t m_SavedReportSecond = 0;

	protected:
		CMenuPerfCapture m_Capture;
		CLogScope m_LogScope{&m_Capture};

		void SetUp() override
		{
			// 限流状态与配置一并隔离，测试不依赖其他 suite 的日志数量。
			auto &Budget = QmPerfLogBudget();
			const std::lock_guard<std::mutex> Lock(Budget.m_Mutex);
			m_SavedBudget = Budget.m_Budget;
			m_SavedSession = Budget.m_Session;
			m_SavedReportSecond = Budget.m_LastReportSecond;
			Budget.m_Budget = CQmPerfDetailBudget();
			Budget.m_Session = QmPerfSessionId();
			g_Config.m_QmPerfDebug = 1;
			g_Config.m_QmPerfLogfile = 0;
			g_Config.m_QmPerfStutterDiagnostics = 0;
		}

		void TearDown() override
		{
			g_Config = m_SavedConfig;
			auto &Budget = QmPerfLogBudget();
			const std::lock_guard<std::mutex> Lock(Budget.m_Mutex);
			Budget.m_Budget = m_SavedBudget;
			Budget.m_Session = m_SavedSession;
			Budget.m_LastReportSecond = m_SavedReportSecond;
		}
	};

	using TJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

	void ExpectInteger(const json_value *pRoot, const char *pKey, int Expected)
	{
		const json_value *pValue = json_object_get(pRoot, pKey);
		ASSERT_EQ(pValue->type, json_integer) << pKey;
		EXPECT_EQ(pValue->u.integer, Expected) << pKey;
	}

	void ExpectString(const json_value *pRoot, const char *pKey, const char *pExpected)
	{
		const json_value *pValue = json_object_get(pRoot, pKey);
		ASSERT_EQ(pValue->type, json_string) << pKey;
		EXPECT_STREQ(pValue->u.string.ptr, pExpected) << pKey;
	}

	void ExpectDouble(const json_value *pRoot, const char *pKey, double Expected)
	{
		const json_value *pValue = json_object_get(pRoot, pKey);
		ASSERT_EQ(pValue->type, json_double) << pKey;
		EXPECT_NEAR(pValue->u.dbl, Expected, 0.001) << pKey;
	}
}

TEST_F(QmMenuUiPerfLogging, FrameCountersAndDurationsReachStructuredLog)
{
	SQmMenuUiFramePerf Frame;
	Frame.m_pPage = "settings";
	Frame.m_pOperation = "scroll";
	Frame.m_ItemsTotal = 100;
	Frame.m_ItemsVisible = 12;
	Frame.m_ItemsProcessed = 8;
	Frame.m_ItemsSkipped = 4;
	Frame.m_UiMs = 2.125f;
	Frame.m_LayoutMs = 0.375f;
	Frame.m_TextMs = 1.250f;
	Frame.m_HeapAllocs = 3;
	Frame.m_CacheHits = 17;
	Frame.m_CacheMisses = 5;
	Frame.m_CacheEvictions = 2;
	QmLogMenuUiFramePerf(Frame, nullptr);

	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	const auto &Message = m_Capture.m_vMessages.front();
	TJson Root(JsonParse(Message.c_str(), Message.size()), json_value_free);
	ASSERT_NE(Root, nullptr);
	ASSERT_EQ(Root->type, json_object);
	ExpectString(Root.get(), "system", "perf/menu-ui");
	ExpectString(Root.get(), "event", "menu_ui_frame");
	ExpectString(Root.get(), "page", "settings");
	ExpectString(Root.get(), "operation", "scroll");
	ExpectString(Root.get(), "source", "qm_ui_perf");
	ExpectInteger(Root.get(), "frame", 0);
	ExpectInteger(Root.get(), "items_total", 100);
	ExpectInteger(Root.get(), "items_visible", 12);
	ExpectInteger(Root.get(), "items_processed", 8);
	ExpectInteger(Root.get(), "items_skipped", 4);
	ExpectInteger(Root.get(), "heap_allocs", 3);
	ExpectInteger(Root.get(), "cache_hits", 17);
	ExpectInteger(Root.get(), "cache_misses", 5);
	ExpectInteger(Root.get(), "cache_evictions", 2);
	ExpectDouble(Root.get(), "ui_ms", 2.125);
	ExpectDouble(Root.get(), "layout_ms", 0.375);
	ExpectDouble(Root.get(), "text_ms", 1.250);
	const auto *pSession = json_object_get(Root.get(), "session");
	ASSERT_EQ(pSession->type, json_integer);
	EXPECT_GT(pSession->u.integer, 0);
}

TEST_F(QmMenuUiPerfLogging, DisabledLoggingCanResumeWithoutLosingNextFrame)
{
	SQmMenuUiFramePerf Frame;
	g_Config.m_QmPerfDebug = 0;
	QmLogMenuUiFramePerf(Frame, nullptr);
	EXPECT_TRUE(m_Capture.m_vMessages.empty());

	g_Config.m_QmPerfLogfile = 1;
	QmLogMenuUiFramePerf(Frame, nullptr);
	ASSERT_EQ(m_Capture.m_vMessages.size(), 1u);
	const auto &Message = m_Capture.m_vMessages.front();
	TJson Root(JsonParse(Message.c_str(), Message.size()), json_value_free);
	ASSERT_NE(Root, nullptr);
	ExpectString(Root.get(), "page", "unknown");
	ExpectString(Root.get(), "operation", "unknown");
	ExpectInteger(Root.get(), "heap_allocs", -1);
	ExpectDouble(Root.get(), "ui_ms", -1.0);

	g_Config.m_QmPerfLogfile = 0;
	QmLogMenuUiFramePerf(Frame, nullptr);
	EXPECT_EQ(m_Capture.m_vMessages.size(), 1u);
	g_Config.m_QmPerfStutterDiagnostics = 1;
	QmLogMenuUiFramePerf(Frame, nullptr);
	EXPECT_EQ(m_Capture.m_vMessages.size(), 2u);
}
