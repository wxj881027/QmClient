// 皮肤列表排序策略基准：直接调用生产策略 SkinListValueOrder，不复制比较逻辑。
// 样本固定随机种子，混合收藏、发布日期、修改时间与本地/目录来源。
#include <game/client/components/qmclient/skin_sort_policy.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <random>
#include <vector>

namespace
{
	struct SSkinSortSample
	{
		bool m_Favorite;
		int m_ReleaseDate;
		std::time_t m_Modified;
		bool m_Local;
		uint32_t m_Index;
	};

	std::vector<SSkinSortSample> MakeSamples(int Count)
	{
		std::mt19937 Engine(20261120u);
		std::uniform_int_distribution<int> FavoriteDist(0, 9);
		std::uniform_int_distribution<int> DateDist(0, 1);
		std::uniform_int_distribution<int> ModifiedDist(0, 1);
		std::uniform_int_distribution<int> LocalDist(0, 9);
		std::vector<SSkinSortSample> vSamples;
		vSamples.reserve(Count);
		for(int Index = 0; Index < Count; ++Index)
		{
			vSamples.push_back({
				FavoriteDist(Engine) == 0,
				DateDist(Engine) != 0 ? 20150101 + Index * 3 : 0,
				ModifiedDist(Engine) != 0 ? (std::time_t)(1600000000 + Index) : 0,
				LocalDist(Engine) == 0,
				(uint32_t)Index,
			});
		}
		return vSamples;
	}

	bool SampleLess(const SSkinSortSample &Left, const SSkinSortSample &Right)
	{
		const ESkinSortOrder Order = SkinListValueOrder(Left.m_Favorite, Right.m_Favorite,
			Left.m_ReleaseDate, Right.m_ReleaseDate,
			Left.m_Modified, Right.m_Modified,
			Left.m_Local, Right.m_Local, true);
		if(Order != ESkinSortOrder::EQUAL_KEYS)
			return Order == ESkinSortOrder::LESS;
		// 真实实现还会比较名称与颜色键；基准只覆盖本策略负责的取值段。
		return Left.m_Index < Right.m_Index;
	}
}

// 单次比较策略成本：每次迭代比较一对样本，计时包含策略调用与分支。
static void SkinListValueOrderPair(benchmark::State &State)
{
	const std::vector<SSkinSortSample> vSamples = MakeSamples(4096);
	if(!SampleLess(vSamples[0], vSamples[1]) && !SampleLess(vSamples[1], vSamples[0]))
	{
		State.SkipWithError("skin sort policy precheck failed");
		return;
	}
	size_t Index = 0;
	for(auto _ : State)
	{
		const SSkinSortSample &Left = vSamples[Index % vSamples.size()];
		const SSkinSortSample &Right = vSamples[(Index + 1) % vSamples.size()];
		benchmark::DoNotOptimize(SkinListValueOrder(Left.m_Favorite, Right.m_Favorite,
			Left.m_ReleaseDate, Right.m_ReleaseDate,
			Left.m_Modified, Right.m_Modified,
			Left.m_Local, Right.m_Local, true));
		++Index;
	}
	State.SetItemsProcessed(State.iterations());
}

// 目录规模下的整表排序：每轮复制样本后排序，复制成本计入重置，注释说明。
static void SkinListSortCatalog(benchmark::State &State)
{
	const int Count = (int)State.range(0);
	const std::vector<SSkinSortSample> vSource = MakeSamples(Count);
	std::vector<SSkinSortSample> vWork = vSource;
	for(auto _ : State)
	{
		vWork = vSource;
		std::sort(vWork.begin(), vWork.end(), SampleLess);
		benchmark::DoNotOptimize(vWork.data());
	}
	State.SetItemsProcessed(State.iterations() * Count);
}

BENCHMARK(SkinListValueOrderPair);
BENCHMARK(SkinListSortCatalog)->Arg(7046);
