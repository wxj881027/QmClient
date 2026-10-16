#include <game/client/components/qmclient/browser_map_preview.h>
#include <game/client/components/qmclient/browser_map_preview_loader.h>

#include <benchmark/benchmark.h>

// 每次从同一整图视野开始，计入相机配置、指针缩放及一次拖拽的 CPU 成本。
// 不包含 GPU 地图绘制，地图复杂度不参与本项微基准。
static void BM_BrowserMapPreviewCamera(benchmark::State &State)
{
	const vec2 World(static_cast<float>(State.range(0)), 18000.0f);
	CQmBrowserMapPreviewView Check;
	Check.Configure(World, {1500, 844});
	if(Check.ViewSize().x + 0.01f < World.x)
	{
		State.SkipWithError("Initial view does not fit the map");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(World);
		CQmBrowserMapPreviewView View;
		View.Configure(World, {1500, 844});
		View.FocusStart({160, 320});
		View.ZoomAt(8, {0.6f, 0.4f});
		View.BeginDrag({400, 200});
		View.DragTo({440, 220}, {800, 450});
		benchmark::DoNotOptimize(View.Center());
		benchmark::DoNotOptimize(View.Zoom());
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_BrowserMapPreviewCamera)->Arg(32000)->Arg(320000);

// 每轮重新读取并解析随包地图、准备图层和解码贴图；计入释放 CPU 结果的成本。
// 此职责在生产中运行于 worker；不包含 GPU 上传或菜单帧耗时。
static void BM_BrowserMapPreviewPrepare(benchmark::State &State)
{
	const char *apArgs[] = {DDNET_TEST_SOURCE_DIR "/qm-benchmarks"};
	auto pStorage = CreateTempStorage(DDNET_TEST_SOURCE_DIR "/data", 1, apArgs);
	const char *pPath = DDNET_TEST_SOURCE_DIR "/data/maps/dm1.map";
	CQmBrowserMapPreviewLoadJob Check(pStorage.get(), "dm1", pPath, false);
	Check.Run();
	if(!Check.m_Success || Check.m_pLayers->GameLayer() == nullptr || !std::any_of(Check.m_vImages.begin(), Check.m_vImages.end(), [](const CImageInfo &Image) { return Image.m_pData != nullptr; }))
	{
		State.SkipWithError("Map preparation failed");
		return;
	}
	for(auto _ : State)
	{
		CQmBrowserMapPreviewLoadJob Job(pStorage.get(), "dm1", pPath, false);
		Job.Run();
		benchmark::DoNotOptimize(Job.m_Success);
		benchmark::DoNotOptimize(Job.m_pLayers.get());
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_BrowserMapPreviewPrepare);
