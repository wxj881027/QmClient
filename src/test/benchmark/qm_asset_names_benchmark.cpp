#include <game/client/components/assets_resource_registry.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
	std::vector<std::string> OccupiedNames(const std::string &Base, int64_t Count)
	{
		std::vector<std::string> Names;
		Names.reserve(static_cast<std::size_t>(Count));
		if(Count > 0)
			Names.push_back(Base);
		for(int64_t Suffix = 2; Suffix <= Count; ++Suffix)
			Names.push_back(Base + "_" + std::to_string(Suffix));
		return Names;
	}

	// 输入在计时外构造；每轮选择一个名称，计入索引与候选的创建/销毁。
	void BM_QmAssetLegacyOccupiedNames(benchmark::State &State)
	{
		const auto Names = OccupiedNames("legacy", State.range(0));
		if(NextLegacyAssetName(Names) != "legacy_" + std::to_string(State.range(0) + 1))
		{
			State.SkipWithError("unexpected legacy candidate");
			return;
		}
		for(auto _ : State)
			benchmark::DoNotOptimize(NextLegacyAssetName(Names));
		State.SetItemsProcessed(State.iterations());
		State.counters["existing_names"] = static_cast<double>(Names.size());
	}

	// 同一固定占用集重复选择，不写磁盘；每轮一个迁移名称，预检在计时外。
	void BM_QmAssetMigrationOccupiedNames(benchmark::State &State)
	{
		const auto *pCategory = FindAssetResourceCategory("arrow");
		const auto Names = OccupiedNames("arrow_local", State.range(0));
		if(!pCategory || NextReservedNamedSingleFileMigrationName(*pCategory, "arrow", Names) != "arrow_local_" + std::to_string(State.range(0) + 1))
		{
			State.SkipWithError("unexpected migration candidate");
			return;
		}
		for(auto _ : State)
			benchmark::DoNotOptimize(NextReservedNamedSingleFileMigrationName(*pCategory, "arrow", Names));
		State.SetItemsProcessed(State.iterations());
		State.counters["existing_names"] = static_cast<double>(Names.size());
	}

	// 基础名称可用的常见路径；只读遍历之外不应构造占用索引。
	void BM_QmAssetLegacyAvailableName(benchmark::State &State)
	{
		const auto Names = OccupiedNames("unrelated", State.range(0));
		if(NextLegacyAssetName(Names) != "legacy")
		{
			State.SkipWithError("unexpected available legacy name");
			return;
		}
		for(auto _ : State)
			benchmark::DoNotOptimize(NextLegacyAssetName(Names));
		State.SetItemsProcessed(State.iterations());
		State.counters["existing_names"] = static_cast<double>(Names.size());
	}

	// 只有基础名冲突、首个数字名可用；多数名称与候选前缀无关。
	void BM_QmAssetLegacyFirstNumberAvailable(benchmark::State &State)
	{
		auto Names = OccupiedNames("unrelated", State.range(0));
		Names[0] = "legacy";
		if(NextLegacyAssetName(Names) != "legacy_2")
		{
			State.SkipWithError("unexpected first numbered candidate");
			return;
		}
		for(auto _ : State)
			benchmark::DoNotOptimize(NextLegacyAssetName(Names));
		State.SetItemsProcessed(State.iterations());
		State.counters["existing_names"] = static_cast<double>(Names.size());
	}
}

BENCHMARK(BM_QmAssetLegacyOccupiedNames)->Arg(16)->Arg(256)->Arg(4096);
BENCHMARK(BM_QmAssetMigrationOccupiedNames)->Arg(16)->Arg(256)->Arg(4096);
BENCHMARK(BM_QmAssetLegacyAvailableName)->Arg(16)->Arg(256)->Arg(4096);
BENCHMARK(BM_QmAssetLegacyFirstNumberAvailable)->Arg(16)->Arg(256)->Arg(4096);
