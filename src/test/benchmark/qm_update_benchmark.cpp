// 更新源规划在冷路径执行；计时包含候选构造、排序和分组去重，不包含网络。
#include <game/client/components/qmclient/update_manifest.h>
#include <game/client/components/qmclient/update_sources.h>

#include <benchmark/benchmark.h>

static void BM_QmUpdateSourcePlanning(benchmark::State &State)
{
	qm_update::CSourceRegistry Sources;
	Sources.SetRecent("https://gh-proxy.org/");
	const std::string Url = "https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.7z";
	if(Sources.Candidates(Url, qm_update::RELEASE, 0).size() != 3)
	{
		State.SkipWithError("approved release candidates are incomplete");
		return;
	}
	for(auto _ : State)
	{
		auto Candidates = Sources.Candidates(Url, qm_update::RELEASE, 0);
		benchmark::DoNotOptimize(Candidates.data());
		benchmark::DoNotOptimize(Candidates.size());
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_QmUpdateSourcePlanning);

// 每帧下载进度监控：每轮更新时间与字节数，避免只测固定的过期结果。
static void BM_QmUpdateProgressDeadline(benchmark::State &State)
{
	qm_update::CProgressDeadline Deadline;
	Deadline.Begin(0);
	double Now = 0;
	for(auto _ : State)
	{
		Now += 0.01;
		const bool Expired = Deadline.Expired(Now, Now * 1024);
		benchmark::DoNotOptimize(Expired);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_QmUpdateProgressDeadline);

// 元数据只有一组支持 API；失败降级后仍保留官方，测量退化路径规划成本。
static void BM_QmUpdateDegradedMetadataPlanning(benchmark::State &State)
{
	qm_update::CSourceRegistry Sources;
	Sources.Failed(qm_update::BuiltinSources().front(), 0);
	const std::string Url = "https://api.github.com/repos/wxj881027/QmClient/releases/latest";
	if(Sources.Candidates(Url, qm_update::API, 1).size() != 1)
	{
		State.SkipWithError("official fallback is missing");
		return;
	}
	for(auto _ : State)
	{
		auto Candidates = Sources.Candidates(Url, qm_update::API, 1);
		benchmark::DoNotOptimize(Candidates.data());
		benchmark::DoNotOptimize(Candidates.size());
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_QmUpdateDegradedMetadataPlanning);

// 无下载任务时的截止时间检查，不分配内存；生产每帧检查路径。
static void BM_QmUpdateFirstByteDeadline(benchmark::State &State)
{
	qm_update::CProgressDeadline Deadline;
	Deadline.Begin(0);
	double Now = 0;
	for(auto _ : State)
	{
		Now += 0.01;
		const bool Expired = Deadline.Expired(Now, 0);
		benchmark::DoNotOptimize(Expired);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_QmUpdateFirstByteDeadline);

// 每轮解析一份有效发布信息，计时包含 JSON、版本/附件检查和说明存储，不包含网络。
static void BM_QmReleaseInfo(benchmark::State &State)
{
	const std::string Json = R"({"tag_name":"v3.4","draft":false,"prerelease":false,"body":"## Changes\nReliable update downloads","assets":[{"name":"QmClient-windows.zip","browser_download_url":"https://github.com/wxj881027/QmClient/releases/download/v3.4/QmClient-windows.zip"}]})";
	const char *pCurrent = State.range(0) == 0 ? "3.4" : "3.3";
	SQmClientUpdateRelease Release;
	char aError[256];
	if(!ParseQmClientReleaseInfo(Json.c_str(), Json.size(), pCurrent, Release, aError, sizeof(aError), false, true) || Release.m_PackageAvailable || Release.m_Notes.empty())
	{
		State.SkipWithError("release information precheck failed");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(pCurrent);
		const bool Valid = ParseQmClientReleaseInfo(Json.c_str(), Json.size(), pCurrent, Release, aError, sizeof(aError), false, true);
		benchmark::DoNotOptimize(Valid);
		benchmark::DoNotOptimize(Release);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_QmReleaseInfo)->Arg(0)->Arg(1);
