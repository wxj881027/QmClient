// qm_net_benchmark.cpp：网络层热路径微基准（Google Benchmark）
// 覆盖 CNetBase 的 Huffman 压缩/解压——demo 录制/回放逐 chunk（demo.cpp:307/741）、
// ghost 回放逐帧（ghost.cpp:152/414）与数据包的真实路径。
// 静态树权重偏置 0 字节，与 delta 快照数据分布吻合（engine/docs/snapshots.txt:47），
// 故输入数据构造为「大部分 0 + 周期性结构化载荷」。
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，构建 run_cxx_benchmarks 目标
#include <engine/shared/network.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

namespace
{
	// 典型 delta 快照规模；每 64 字节嵌入 8 字节伪随机载荷（LCG 确定性生成）
	constexpr int DATA_SIZE = 16 * 1024;

	void FillSnapshotLikeData(unsigned char *pData, int Size)
	{
		uint32_t Seed = 0x1234567u;
		for(int i = 0; i < Size; i++)
		{
			if((i % 64) >= 8)
			{
				pData[i] = 0;
				continue;
			}
			Seed = Seed * 1664525u + 1013904223u;
			pData[i] = static_cast<unsigned char>(Seed >> 24);
		}
	}
} // namespace

// 固件：构造快照分布输入并预压缩一份供解压基准使用
class NetHuffmanBenchmark : public benchmark::Fixture
{
public:
	void SetUp(const benchmark::State &State) override
	{
		CNetBase::Init();
		m_aInput.resize(DATA_SIZE);
		FillSnapshotLikeData(m_aInput.data(), DATA_SIZE);
		m_aCompressed.resize(DATA_SIZE + 64);
		m_CompressedSize = CNetBase::Compress(m_aInput.data(), DATA_SIZE, m_aCompressed.data(), static_cast<int>(m_aCompressed.size()));
	}

	std::vector<unsigned char> m_aInput;
	std::vector<unsigned char> m_aCompressed;
	int m_CompressedSize = 0;
};

// 压缩：每迭代压缩一整份 16 KiB 快照分布数据
BENCHMARK_DEFINE_F(NetHuffmanBenchmark, BM_NetHuffmanCompress)(benchmark::State &State)
{
	unsigned char aOutput[DATA_SIZE + 64];
	for(auto _ : State)
	{
		const int Size = CNetBase::Compress(m_aInput.data(), DATA_SIZE, aOutput, static_cast<int>(sizeof(aOutput)));
		benchmark::DoNotOptimize(Size);
	}
	State.SetItemsProcessed(State.iterations() * DATA_SIZE);
	State.SetBytesProcessed(State.iterations() * DATA_SIZE);
}
BENCHMARK_REGISTER_F(NetHuffmanBenchmark, BM_NetHuffmanCompress);

// 解压：解压 SetUp 预压缩的真实 Huffman 流
BENCHMARK_DEFINE_F(NetHuffmanBenchmark, BM_NetHuffmanDecompress)(benchmark::State &State)
{
	if(m_CompressedSize <= 0)
	{
		State.SkipWithError("huffman compress failed during SetUp");
		return;
	}
	unsigned char aOutput[DATA_SIZE + 64];
	for(auto _ : State)
	{
		const int Size = CNetBase::Decompress(m_aCompressed.data(), m_CompressedSize, aOutput, static_cast<int>(sizeof(aOutput)));
		benchmark::DoNotOptimize(Size);
	}
	State.SetItemsProcessed(State.iterations() * DATA_SIZE);
	State.SetBytesProcessed(State.iterations() * DATA_SIZE);
}
BENCHMARK_REGISTER_F(NetHuffmanBenchmark, BM_NetHuffmanDecompress);
