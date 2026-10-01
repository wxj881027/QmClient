// qm_snapshot_benchmark.cpp：网络快照热路径微基准（Google Benchmark）
// 覆盖每个游戏 tick 的两条真实快照路径：
//  1. 服务端发送：CSnapshotDelta::CreateDelta(prev, current)——逐项对比生成增量
//  2. 客户端应用：CSnapshotDelta::UnpackDelta(prev, delta)——增量还原完整快照
// 场景：N 个同型实体（10 int/个，类型 1 注册静态大小），当前帧所有实体
// 位置 +1（模拟一 tick 移动，全量更新是位置数据每 tick 的典型形态）。
// 夹具模式参照 src/test/snapshot_test.cpp（Rust 后端 API）。
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，构建 run_cxx_benchmarks 目标
#include <engine/shared/snapshot.h>

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <cstring>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

namespace
{
	constexpr int ITEM_TYPE = 1;
	constexpr int ITEM_INTS = 10; // 典型实体对象（如 CNetObj_Character）的整型数量
} // namespace

class SnapshotDeltaBenchmark : public benchmark::Fixture
{
public:
	// rust::Box 无默认构造，成员直接以工厂产物初始化（每次 fixture 实例化各建一份）
	rust::Box<CSnapshotDelta> m_pDelta = CSnapshotDelta::New();

	void SetUp(const benchmark::State &State) override
	{
		const int NumItems = static_cast<int>(State.range(0));
		m_Ready = false;
		if(NumItems < 0 || NumItems > CSnapshot::MAX_ITEMS)
			return;

		m_pDelta->SetStaticsize(0, 0);
		m_pDelta->SetStaticsize(ITEM_TYPE, ITEM_INTS * static_cast<int>(sizeof(int32_t)));

		const int PreviousSize = BuildSnapshot(m_Prev, NumItems, 0);
		m_CurrentSize = BuildSnapshot(m_Current, NumItems, 1); // 全部字段 +1 的固定输入
		if(PreviousSize <= 0 || m_CurrentSize <= 0)
			return;

		// 预生成一份真实 delta 供解压基准使用
		m_DeltaSize = m_pDelta->CreateDelta(*m_Prev.AsSnapshot(), *m_Current.AsSnapshot(), m_DeltaBuffer.AsMutSlice());
		if(m_DeltaSize <= 0 || m_DeltaSize % sizeof(int32_t) != 0)
			return;
		const rust::Slice<const int32_t> Delta(reinterpret_cast<const int32_t *>(m_DeltaBuffer.m_aData), m_DeltaSize / sizeof(int32_t));
		m_Ready = m_pDelta->UnpackDelta(*m_Prev.AsSnapshot(), m_Output, Delta) == m_CurrentSize &&
			  std::memcmp(m_Output.m_aData, m_Current.m_aData, m_CurrentSize) == 0;
	}

	int BuildSnapshot(CSnapshotBuffer &Buffer, int NumItems, int Offset)
	{
		rust::Box<CSnapshotBuilder> pBuilder = CSnapshotBuilder::New();
		pBuilder->Init(false);
		for(int i = 0; i < NumItems; i++)
		{
			std::array<int32_t, ITEM_INTS> aData;
			for(int j = 0; j < ITEM_INTS; j++)
				aData[j] = i * 100 + j + Offset;
			if(!pBuilder->NewItem(ITEM_TYPE, i, rust::Slice<const int32_t>(aData.data(), aData.size())))
				return -1;
		}
		return pBuilder->Finish(Buffer);
	}

	CSnapshotBuffer m_Prev;
	CSnapshotBuffer m_Current;
	CSnapshotBuffer m_Output;
	CSnapshotDeltaBuffer m_DeltaBuffer;
	CSnapshotDeltaBuffer m_DeltaOutput;
	int m_DeltaSize = 0;
	int m_CurrentSize = 0;
	bool m_Ready = false;
};

// 服务端每 tick：对比上一帧与当前帧生成 delta（netserver 发送路径）
BENCHMARK_DEFINE_F(SnapshotDeltaBenchmark, BM_SnapshotDeltaDiff)(benchmark::State &State)
{
	if(!m_Ready)
	{
		State.SkipWithError("snapshot fixture round-trip failed");
		return;
	}
	const int NumItems = static_cast<int>(State.range(0));
	for(auto _ : State)
	{
		int DeltaSize = m_pDelta->CreateDelta(*m_Prev.AsSnapshot(), *m_Current.AsSnapshot(), m_DeltaOutput.AsMutSlice());
		benchmark::DoNotOptimize(DeltaSize);
		benchmark::ClobberMemory();
		if(DeltaSize != m_DeltaSize)
		{
			State.SkipWithError("snapshot delta size differs");
			break;
		}
	}
	State.SetItemsProcessed(State.iterations() * NumItems);
}
BENCHMARK_REGISTER_F(SnapshotDeltaBenchmark, BM_SnapshotDeltaDiff)->Arg(128)->Arg(512)->Arg(1024);

// 客户端每 tick：从 delta 还原完整快照（netclient 应用路径）
BENCHMARK_DEFINE_F(SnapshotDeltaBenchmark, BM_SnapshotDeltaUnpack)(benchmark::State &State)
{
	const int NumItems = static_cast<int>(State.range(0));
	if(!m_Ready)
	{
		State.SkipWithError("snapshot fixture round-trip failed");
		return;
	}
	rust::Slice<const int32_t> Delta(
		reinterpret_cast<const int32_t *>(m_DeltaBuffer.m_aData),
		m_DeltaSize / static_cast<int>(sizeof(int32_t)));
	for(auto _ : State)
	{
		int Result = m_pDelta->UnpackDelta(*m_Prev.AsSnapshot(), m_Output, Delta);
		benchmark::DoNotOptimize(Result);
		benchmark::ClobberMemory();
		if(Result != m_CurrentSize)
		{
			State.SkipWithError("snapshot unpack did not restore the full snapshot");
			break;
		}
	}
	if(!State.skipped() && std::memcmp(m_Output.m_aData, m_Current.m_aData, m_CurrentSize) != 0)
		State.SkipWithError("snapshot restored content differs");
	State.SetItemsProcessed(State.iterations() * NumItems);
}
BENCHMARK_REGISTER_F(SnapshotDeltaBenchmark, BM_SnapshotDeltaUnpack)->Arg(128)->Arg(512)->Arg(1024);
