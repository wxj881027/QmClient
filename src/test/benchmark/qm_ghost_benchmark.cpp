// 幽灵轨迹分块容器基准：.gho 加载/录制逐帧追加的生产实现。
#include <game/client/components/qmclient/ghost_path.h>

#include <benchmark/benchmark.h>

#include <cstdint>

namespace
{
	struct SGhostSample
	{
		int m_X;
		int m_Y;
		int m_VelX;
		int m_VelY;
		int m_Angle;
		int m_Tick;
	};
} // namespace

// 每轮重建容器，计时包含真实的分块扩容与逐项追加，不包含文件 I/O。
static void BM_GhostPathAppend(benchmark::State &State)
{
	const int Samples = State.range(0);
	for(auto _ : State)
	{
		qm_ghost_path::CChunkedPath<SGhostSample> Path;
		for(int i = 0; i < Samples; ++i)
		{
			if(!Path.Add({i, i * 2, 128, -256, 42, i}))
			{
				State.SkipWithError("ghost path allocation failed");
				break;
			}
		}
		benchmark::DoNotOptimize(Path.Size());
		benchmark::DoNotOptimize(Path.Get(Samples - 1));
	}
	State.SetItemsProcessed(State.iterations() * Samples);
}
BENCHMARK(BM_GhostPathAppend)->Arg(1000)->Arg(100000);
