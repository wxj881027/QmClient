// 固定字体列表，分别计量输入变化时过滤和查询未变时的缓存路径；不读系统目录。
#include <engine/client/qm_font_scan_job.h>

#include <game/client/QmUi/QmDropdown.h>

#include <benchmark/benchmark.h>

static void FontSearchFilter(benchmark::State &State)
{
	std::vector<std::string> vNames;
	for(int Index = 0; Index < State.range(0); ++Index)
		vNames.push_back("Font " + std::to_string(Index));
	CQmDropdownSearch Search;
	if(!Search.Update(vNames, "Font") || Search.Indices().size() != vNames.size())
	{
		State.SkipWithError("Font filter precheck failed");
		return;
	}
	bool Toggle = false;
	for(auto _ : State)
	{
		Toggle = !Toggle;
		benchmark::DoNotOptimize(Search.Update(vNames, Toggle ? "Font 1" : "Font 2"));
		benchmark::DoNotOptimize(Search.Indices().size());
	}
	State.SetItemsProcessed(State.iterations() * vNames.size());
}

static void FontSearchCached(benchmark::State &State)
{
	std::vector<std::string> vNames;
	for(int Index = 0; Index < State.range(0); ++Index)
		vNames.push_back("Font " + std::to_string(Index));
	CQmDropdownSearch Search;
	Search.Update(vNames, "Font 1");
	if(Search.Update(vNames, "Font 1"))
	{
		State.SkipWithError("Font search cache precheck failed");
		return;
	}
	for(auto _ : State)
		benchmark::DoNotOptimize(Search.Update(vNames, "Font 1"));
	State.SetItemsProcessed(State.iterations());
}

BENCHMARK(FontSearchFilter)->Arg(32)->Arg(512)->Arg(2048);
BENCHMARK(FontSearchCached)->Arg(32)->Arg(512)->Arg(2048);

// 在途扫描的逐帧查询成本；任务尚未运行，查询不得执行枚举或读取。
static void FontScanPendingPoll(benchmark::State &State)
{
	int Reads = 0;
	CQmFontScanJob Job([](const auto &) { return std::vector<std::string>{"font.ttf"}; }, [&](const char *, void **, unsigned *) {
		++Reads;
		return false; });
	CQmFontScanJob::SFile File;
	if(Job.TakeNext(File) || Job.Exhausted() || Reads != 0)
	{
		State.SkipWithError("Pending scan published or read a file");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Job.TakeNext(File));
		benchmark::DoNotOptimize(Job.Exhausted());
	}
	State.SetItemsProcessed(State.iterations());
}

BENCHMARK(FontScanPendingPoll);
