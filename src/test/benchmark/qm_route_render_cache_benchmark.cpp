#include <game/client/components/qmclient/route_render_cache.h>

#include <benchmark/benchmark.h>

namespace
{
	std::vector<vec2> MakeLongRoute(int Count)
	{
		std::vector<vec2> vPoints;
		vPoints.reserve(Count);
		for(int Index = 0; Index < Count; ++Index)
			vPoints.emplace_back(Index * 32.0f, 0.0f);
		return vPoints;
	}
}

// 输入准备不计时；重建包括路线结果复制和视野索引初始化，不包含寻路。
static void BM_QmRouteRenderCacheRebuild(benchmark::State &State)
{
	const auto vRoute = MakeLongRoute(static_cast<int>(State.range(0)));
	CQmRouteRenderCache Cache;
	CQmRouteRenderCache::SKey Key;
	for(auto _ : State)
	{
		++Key.m_Revision;
		benchmark::DoNotOptimize(Cache.Update(Key, [&](std::vector<vec2> &vPoints) {
			vPoints = vRoute;
			return true;
		}));
		benchmark::ClobberMemory();
	}
	State.SetItemsProcessed(State.iterations() * State.range(0));
}
BENCHMARK(BM_QmRouteRenderCacheRebuild)->Arg(256)->Arg(4096)->Arg(65536);

static void BM_QmRouteRenderCacheHit(benchmark::State &State)
{
	const auto vRoute = MakeLongRoute(static_cast<int>(State.range(0)));
	CQmRouteRenderCache Cache;
	const auto Build = [&](std::vector<vec2> &vPoints) {
		vPoints = vRoute;
		return true;
	};
	Cache.Update({}, Build);
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Cache);
		benchmark::DoNotOptimize(Cache.Update({}, Build));
		benchmark::ClobberMemory();
	}
}
BENCHMARK(BM_QmRouteRenderCacheHit)->Arg(256)->Arg(4096)->Arg(65536);

// 每次移动视野，避免只测矩形缓存命中；总路线长度与屏幕内点数分别观察。
static void BM_QmRouteViewportPan(benchmark::State &State)
{
	const auto vRoute = MakeLongRoute(static_cast<int>(State.range(0)));
	CQmRouteRenderCache Cache;
	Cache.Update({}, [&](std::vector<vec2> &vPoints) {
		vPoints = vRoute;
		return true;
	});
	int Origin = 0;
	size_t TotalTestedPoints = 0;
	for(auto _ : State)
	{
		Origin = (Origin + 17) % static_cast<int>(vRoute.size());
		const float Left = Origin * 32.0f;
		size_t TestedPoints = 0;
		const auto &vVisible = Cache.QueryVisible({Left, -48.0f, Left + 1024.0f, 48.0f}, &TestedPoints);
		TotalTestedPoints += TestedPoints;
		benchmark::DoNotOptimize(vVisible.data());
		benchmark::DoNotOptimize(vVisible.size());
		benchmark::ClobberMemory();
	}
	State.counters["tested_points"] = static_cast<double>(TotalTestedPoints) / State.iterations();
}
BENCHMARK(BM_QmRouteViewportPan)->Arg(256)->Arg(4096)->Arg(65536);
