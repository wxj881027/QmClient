#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_detect.h>
#include <game/client/components/qmclient/translate/translate_jobs.h>
#include <game/client/components/qmclient/translate/translate_parse.h>

#include <benchmark/benchmark.h>

#include <array>
#include <string>

static void BM_TranslateDetectShortChinese(benchmark::State &State)
{
	for(auto _ : State)
		benchmark::DoNotOptimize(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage("泥蛙！！！"), "zh", 3, 50, false));
}
BENCHMARK(BM_TranslateDetectShortChinese);

static void BM_TranslateDetectRoster(benchmark::State &State)
{
	std::array<std::string, 128> aNames;
	std::array<qm_translate::SPlayerReference, 128> aPlayers;
	const int Count = static_cast<int>(State.range(0));
	for(int i = 0; i < Count; ++i)
	{
		aNames[i] = "Player" + std::to_string(i);
		aPlayers[i] = {aNames[i].c_str(), i};
	}
	const std::string Text = "@" + aNames[Count - 1] + " 泥蛙！！！";
	for(auto _ : State)
		benchmark::DoNotOptimize(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage(Text.c_str(), aPlayers.data(), Count), "zh", 3, 50, false));
}
BENCHMARK(BM_TranslateDetectRoster)->Arg(1)->Arg(64)->Arg(128);

static void BM_TranslateDetectUnknownAddress(benchmark::State &State)
{
	const qm_translate::SPlayerReference aPlayers[] = {{"DYL", 7}};
	const std::string Text(static_cast<size_t>(State.range(0)), 'a');
	const std::string Message = Text + ": 好";
	for(auto _ : State)
		benchmark::DoNotOptimize(qm_translate::AnalyzeLanguage(Message.c_str(), aPlayers, 1));
}
BENCHMARK(BM_TranslateDetectUnknownAddress)->Arg(16)->Arg(256);

static void BM_TranslateDetectEmpty(benchmark::State &State)
{
	for(auto _ : State)
		benchmark::DoNotOptimize(qm_translate::ShouldTranslateIncoming(qm_translate::AnalyzeLanguage(""), "zh", 3, 50, false));
}
BENCHMARK(BM_TranslateDetectEmpty);

#include <engine/shared/json.h>

#include <game/client/components/qmclient/translate/translate_jobs.h>
#include <game/client/components/qmclient/translate/translate_parse.h>

namespace
{
	class CTranslatePendingBenchmark : public ITranslateBackend
	{
	public:
		const char *Name() const override { return "benchmark"; }
		std::optional<bool> Update(CTranslateResponse &) override { return std::nullopt; }
	};
}

static void BM_TranslateQueuePending(benchmark::State &State)
{
	CTranslateJobQueue Queue;
	const int Count = static_cast<int>(State.range(0));
	for(int i = 0; i < Count; ++i)
	{
		STranslateJob Job;
		Job.m_Outgoing = true;
		Job.m_pBackend = std::make_unique<CTranslatePendingBenchmark>();
		Queue.Submit(std::move(Job), Count);
	}
	const std::function<bool(const STranslateJob &)> Valid = [](const auto &) { return true; };
	for(auto _ : State)
		benchmark::DoNotOptimize(Queue.Update(Valid));
}
BENCHMARK(BM_TranslateQueuePending)->Arg(0)->Arg(1)->Arg(20);

static void BM_TranslateResponsesParse(benchmark::State &State)
{
	const char *pJson = R"({"output":[{"type":"message","content":[{"type":"output_text","text":"你好"},{"type":"output_text","text":"世界"}]}]})";
	json_value *pValue = json_parse(pJson, str_length(pJson));
	SLlmParseResult Result;
	for(auto _ : State)
		benchmark::DoNotOptimize(ParseLlmResponsesJson(pValue, Result));
	json_value_free(pValue);
}
BENCHMARK(BM_TranslateResponsesParse);
