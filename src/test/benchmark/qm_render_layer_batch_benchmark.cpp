// qm_render_layer_batch_benchmark.cpp：tile 层绘制批次折算（Google Benchmark）
// 覆盖 QRenderLayer 的两条真实路径：
//  1. 整层左右边界都在可见区——所有可见行合并成一次提交
//  2. 只有中间一段可见——逐行合并相邻行
// 被测入口是生产实现 QmBuildTileLayerDrawRanges，基准只提供合成 tile 数据，
// 不复制合并算法。这里测的是 CPU 侧批次折算，不含 GPU 绘制调用成本。
#include <game/map/render_layer.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

namespace
{
	constexpr int LAYER_WIDTH = 2000;
	constexpr int LAYER_HEIGHT = 2000;
	constexpr int VISIBLE_ROWS = 40;
	constexpr uintptr_t INDICES_PER_TILE = 6;
	constexpr uintptr_t BYTES_PER_INDEX = sizeof(uint32_t);

	struct STileLayerData
	{
		std::vector<offset_ptr> m_vOffsets;
		std::vector<uint8_t> m_vDrawn;
	};

	// 平铺层：每 1/DRAWN_EVERY 个 tile 未绘制，其余绘制，偏移按绘制顺序连续增长。
	STileLayerData BuildTileLayerData(int DrawnEvery)
	{
		STileLayerData Data;
		Data.m_vOffsets.resize((size_t)LAYER_WIDTH * LAYER_HEIGHT);
		Data.m_vDrawn.resize((size_t)LAYER_WIDTH * LAYER_HEIGHT);
		offset_ptr Offset = 0;
		for(size_t i = 0; i < Data.m_vOffsets.size(); ++i)
		{
			const bool Drawn = DrawnEvery <= 1 || (i % (size_t)DrawnEvery) != 0;
			Data.m_vOffsets[i] = Offset;
			Data.m_vDrawn[i] = Drawn ? 1 : 0;
			if(Drawn)
				Offset += INDICES_PER_TILE * BYTES_PER_INDEX;
		}
		return Data;
	}

	const STileLayerData &DenseLayer()
	{
		static const STileLayerData s_Layer = BuildTileLayerData(1);
		return s_Layer;
	}

	const STileLayerData &SparseLayer()
	{
		static const STileLayerData s_Layer = BuildTileLayerData(3);
		return s_Layer;
	}

	void RunCase(benchmark::State &State, const STileLayerData &Layer, int X0, int X1)
	{
		std::vector<STileLayerDrawRange> vRanges;
		vRanges.reserve((size_t)VISIBLE_ROWS + 1);
		auto OffsetAt = [&Layer](size_t Index) { return Layer.m_vOffsets[Index]; };
		auto DrawAt = [&Layer](size_t Index) { return Layer.m_vDrawn[Index] != 0; };

		for(auto _ : State)
		{
			QmBuildTileLayerDrawRanges(LAYER_WIDTH, X0, X1, 0, VISIBLE_ROWS, OffsetAt, DrawAt, vRanges);
			benchmark::DoNotOptimize(vRanges.data());
			benchmark::ClobberMemory();
		}
		State.SetItemsProcessed(State.iterations() * (int64_t)VISIBLE_ROWS);
		State.counters["rows"] = VISIBLE_ROWS;
		State.counters["ranges"] = (double)vRanges.size();
	}
} // namespace

static void BM_TileLayerBatch_FullWidthDense(benchmark::State &State)
{
	RunCase(State, DenseLayer(), 0, LAYER_WIDTH);
}
BENCHMARK(BM_TileLayerBatch_FullWidthDense)->Unit(benchmark::kNanosecond);

static void BM_TileLayerBatch_FullWidthSparse(benchmark::State &State)
{
	RunCase(State, SparseLayer(), 0, LAYER_WIDTH);
}
BENCHMARK(BM_TileLayerBatch_FullWidthSparse)->Unit(benchmark::kNanosecond);

static void BM_TileLayerBatch_PartialWidthDense(benchmark::State &State)
{
	RunCase(State, DenseLayer(), 100, LAYER_WIDTH - 100);
}
BENCHMARK(BM_TileLayerBatch_PartialWidthDense)->Unit(benchmark::kNanosecond);

static void BM_TileLayerBatch_PartialWidthSparse(benchmark::State &State)
{
	RunCase(State, SparseLayer(), 100, LAYER_WIDTH - 100);
}
BENCHMARK(BM_TileLayerBatch_PartialWidthSparse)->Unit(benchmark::kNanosecond);
