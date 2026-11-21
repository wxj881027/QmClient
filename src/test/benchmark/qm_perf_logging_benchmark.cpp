// qm_perf_logging_benchmark.cpp：性能日志 JSON 字段扫描与序列化微基准（Google Benchmark）
// 覆盖 QmPerfPayloadHasField / QmPerfLogPayloadUnchecked 的生产实现：每迭代处理一条完整
// payload，包含保留字段探测、JSON 拼接与 dbg_msg 分发。计时使用无操作 logger 排除后端
// 输出 I/O，但包含时间戳格式化与日志分发，不代表磁盘/控制台写入成本。
#include <base/logger.h>

#include <game/client/components/qmclient/perf_logging.h>

#include <benchmark/benchmark.h>

#include <memory>
#include <string>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

namespace
{
	// 常见交互日志：不含 frame/session/page/tab，四次保留字段探测都需要扫完整个 payload。
	constexpr const char *COMMON_PAYLOAD = "event=interaction action=click duration_ms=2.5 widget=settings_card visible=1 hovered=0";
	// 保留字段放在 payload 末尾：每个探测都要扫描到最后一个字段才命中。
	constexpr const char *RESERVED_PAYLOAD = "event=stutter_event duration_ms=12.5 widget=settings_card worst_frame=10 frame=10 session=42 page=detail tab=tee";
	// 带空格与反斜杠的引号值：覆盖 payload 解析和 JSON 转义路径。
	constexpr const char *QUOTED_PAYLOAD = "event=stutter_event classification=\"frame drop\" page=\"detail page\" path=\"C:\\\\maps\\\\a b.map\" text=\"alpha beta gamma delta\" duration_ms=12.5 worst_frame=10 schema=1";

	size_t CountOccurrences(const std::string &Text, const char *pNeedle)
	{
		size_t Count = 0;
		for(size_t Pos = Text.find(pNeedle); Pos != std::string::npos; Pos = Text.find(pNeedle, Pos + 1))
			++Count;
		return Count;
	}

	// 计时前的正确性预检：保留字段必须只出现一次，且 payload 自带值优先于全局值。
	bool VerifyLog(const char *pPayload, const char *pPage, const char *pTab, const char *pMustContain, const char *pMustNotContain)
	{
		CMemoryLogger Logger;
		{
			CLogScope Scope(&Logger);
			QmPerfLogPayloadUnchecked("perf/bench", pPayload, nullptr, pPage, pTab);
		}
		const auto vLines = Logger.Lines();
		if(vLines.size() != 1)
			return false;
		const std::string Message = vLines.front().Message();
		return Message.find(pMustContain) != std::string::npos &&
		       Message.find(pMustNotContain) == std::string::npos &&
		       CountOccurrences(Message, "\"frame\":") == 1 &&
		       CountOccurrences(Message, "\"session\":") == 1 &&
		       CountOccurrences(Message, "\"page\":") == 1 &&
		       CountOccurrences(Message, "\"tab\":") == 1;
	}

	class PerfLoggingBenchmark : public benchmark::Fixture
	{
	public:
		void SetUp(const benchmark::State &) override
		{
			m_pLogger = log_logger_noop();
			m_pScope = std::make_unique<CLogScope>(m_pLogger.get());
		}
		void TearDown(const benchmark::State &) override
		{
			m_pScope.reset();
			m_pLogger.reset();
		}

	protected:
		std::unique_ptr<ILogger> m_pLogger;
		std::unique_ptr<CLogScope> m_pScope;
	};
} // namespace

// 未命中路径：每迭代对常见 payload 执行四次字段探测，全部扫完仍返回 false。
BENCHMARK_DEFINE_F(PerfLoggingBenchmark, BM_PerfPayloadFieldLookupMiss)(benchmark::State &State)
{
	if(!VerifyLog(COMMON_PAYLOAD, "settings", "tee", "\"frame\":0,", "\"frame\":10,"))
	{
		State.SkipWithError("common payload precheck failed");
		return;
	}
	for(auto _ : State)
	{
		const bool HasFrame = QmPerfPayloadHasField(COMMON_PAYLOAD, "frame");
		const bool HasSession = QmPerfPayloadHasField(COMMON_PAYLOAD, "session");
		const bool HasPage = QmPerfPayloadHasField(COMMON_PAYLOAD, "page");
		const bool HasTab = QmPerfPayloadHasField(COMMON_PAYLOAD, "tab");
		benchmark::DoNotOptimize(HasFrame);
		benchmark::DoNotOptimize(HasSession);
		benchmark::DoNotOptimize(HasPage);
		benchmark::DoNotOptimize(HasTab);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(PerfLoggingBenchmark, BM_PerfPayloadFieldLookupMiss);

// 命中路径：保留字段位于 payload 末尾，每轮四次探测都在扫描完整 payload 后才命中。
BENCHMARK_DEFINE_F(PerfLoggingBenchmark, BM_PerfPayloadFieldLookupHit)(benchmark::State &State)
{
	if(!VerifyLog(RESERVED_PAYLOAD, "fallback", "fallback", "\"frame\":10,", "\"frame\":0,"))
	{
		State.SkipWithError("reserved payload precheck failed");
		return;
	}
	for(auto _ : State)
	{
		const bool HasFrame = QmPerfPayloadHasField(RESERVED_PAYLOAD, "frame");
		const bool HasSession = QmPerfPayloadHasField(RESERVED_PAYLOAD, "session");
		const bool HasPage = QmPerfPayloadHasField(RESERVED_PAYLOAD, "page");
		const bool HasTab = QmPerfPayloadHasField(RESERVED_PAYLOAD, "tab");
		benchmark::DoNotOptimize(HasFrame);
		benchmark::DoNotOptimize(HasSession);
		benchmark::DoNotOptimize(HasPage);
		benchmark::DoNotOptimize(HasTab);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(PerfLoggingBenchmark, BM_PerfPayloadFieldLookupHit);

// 完整日志路径（常见 payload）：字段探测未命中后补全局 frame/session/page/tab 并序列化。
BENCHMARK_DEFINE_F(PerfLoggingBenchmark, BM_PerfLogPayloadCommon)(benchmark::State &State)
{
	if(!VerifyLog(COMMON_PAYLOAD, "settings", "tee", "\"frame\":0,", "\"frame\":10,"))
	{
		State.SkipWithError("common payload precheck failed");
		return;
	}
	for(auto _ : State)
		QmPerfLogPayloadUnchecked("perf/bench", COMMON_PAYLOAD, nullptr, "settings", "tee");
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(PerfLoggingBenchmark, BM_PerfLogPayloadCommon);

// 完整日志路径（payload 自带保留字段）：全局补值必须全部让位，避免重复 JSON 键覆盖。
BENCHMARK_DEFINE_F(PerfLoggingBenchmark, BM_PerfLogPayloadReservedFields)(benchmark::State &State)
{
	if(!VerifyLog(RESERVED_PAYLOAD, "fallback", "fallback", "\"page\":\"detail\",", "\"page\":\"fallback\","))
	{
		State.SkipWithError("reserved payload precheck failed");
		return;
	}
	for(auto _ : State)
		QmPerfLogPayloadUnchecked("perf/bench", RESERVED_PAYLOAD, nullptr, "fallback", "fallback");
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(PerfLoggingBenchmark, BM_PerfLogPayloadReservedFields);

// 完整日志路径（长引号值）：覆盖带空格值、反斜杠转义与较长 payload 的序列化。
BENCHMARK_DEFINE_F(PerfLoggingBenchmark, BM_PerfLogPayloadQuotedText)(benchmark::State &State)
{
	if(!VerifyLog(QUOTED_PAYLOAD, "fallback", "fallback", "\"page\":\"detail page\",", "\"page\":\"fallback\","))
	{
		State.SkipWithError("quoted payload precheck failed");
		return;
	}
	for(auto _ : State)
		QmPerfLogPayloadUnchecked("perf/bench", QUOTED_PAYLOAD, nullptr, "fallback", "fallback");
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(PerfLoggingBenchmark, BM_PerfLogPayloadQuotedText);
