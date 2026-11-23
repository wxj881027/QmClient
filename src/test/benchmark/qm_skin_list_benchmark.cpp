// qm_skin_list_benchmark.cpp：设置页皮肤列表计划的构建与排序 CPU 基准。
// 输入由固定公式生成，不读取用户目录、网络或真实皮肤资源。
#include <game/client/components/settings_resource_jobs.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr int SORT_BY_NAME = 0;
	constexpr int SORT_BY_DATE = 1;
	constexpr uint32_t NAME_PERMUTATION = 7919;
	constexpr int RELEASE_DATE_BASE = 20100101;
	constexpr time_t LAST_MODIFIED_BASE = 1500000000;
	constexpr time_t LAST_MODIFIED_SPAN = 100000000;

	struct SSkinListInput
	{
		std::vector<SSettingsSkinListEntry> m_vEntries;
		int m_Selected = 0;
		int m_Favorite = 0;
	};

	SSkinListInput MakeSkinListInput(int Count)
	{
		SSkinListInput Input;
		Input.m_vEntries.reserve(Count);
		for(int Index = 0; Index < Count; ++Index)
		{
			const int Order = static_cast<int>((static_cast<int64_t>(Index) * NAME_PERMUTATION + 12345) % Count);
			char aName[32];
			std::snprintf(aName, sizeof(aName), "skin-%05d-%05d", Order, Index);

			SSettingsSkinListEntry Entry;
			Entry.m_Name = aName;
			Entry.m_Selected = Order % 4093 == 0;
			Entry.m_Favorite = Order % 97 == 0;
			if(Order % 3 == 0)
				Entry.m_ColorKey = SSettingsSkinListColorKey{true, Order % 20, (Order * 7) % 20};
			Entry.m_OfficialReleaseDate = RELEASE_DATE_BASE + (Order * 37) % 5000;
			Entry.m_LastModified = LAST_MODIFIED_BASE + (static_cast<int64_t>(Order) * 104729) % LAST_MODIFIED_SPAN;

			Input.m_Selected += Entry.m_Selected ? 1 : 0;
			Input.m_Favorite += Entry.m_Favorite ? 1 : 0;
			Input.m_vEntries.push_back(std::move(Entry));
		}
		return Input;
	}

	bool ValidatePlan(const SSettingsSkinListPlan &Plan, int Count)
	{
		if(Plan.m_vNames.size() != static_cast<size_t>(Count) || Plan.m_vEntries.size() != static_cast<size_t>(Count))
			return false;
		bool SeenUnselected = false;
		for(size_t Index = 0; Index < Plan.m_vEntries.size(); ++Index)
		{
			if(Plan.m_vNames[Index] != Plan.m_vEntries[Index].m_Name)
				return false;
			if(Plan.m_vEntries[Index].m_Selected)
			{
				if(SeenUnselected)
					return false;
			}
			else
				SeenUnselected = true;
		}
		return true;
	}

	// 冷路径：每次迭代在计时外复制一份未排序输入；计时内只调用生产函数，
	// 包含 move 入参、stable_sort、名称/条目向量物化与输入排序存储释放。
	// 返回计划在计时外销毁，避免把调用方生命周期成本混入 plan 构建耗时。
	void RunSkinListPlanBenchmark(benchmark::State &State, int SortMode)
	{
		const int Count = static_cast<int>(State.range(0));
		const SSkinListInput Input = MakeSkinListInput(Count);
		{
			const SSettingsSkinListPlan Preflight = BuildSettingsSkinListPlan(Input.m_vEntries, SortMode);
			if(!ValidatePlan(Preflight, Count))
			{
				State.SkipWithError("skin list plan preflight failed");
				return;
			}
		}

		std::vector<SSettingsSkinListEntry> vWork;
		std::optional<SSettingsSkinListPlan> Plan;
		for(auto _ : State)
		{
			State.PauseTiming();
			vWork = Input.m_vEntries;
			State.ResumeTiming();

			Plan.emplace(BuildSettingsSkinListPlan(std::move(vWork), SortMode));
			benchmark::DoNotOptimize(Plan->m_vNames.data());
			benchmark::DoNotOptimize(Plan->m_vEntries.data());
			benchmark::ClobberMemory();

			State.PauseTiming();
			Plan.reset();
			State.ResumeTiming();
		}

		State.SetItemsProcessed(State.iterations() * Count);
		State.counters["input_entries"] = Count;
		State.counters["selected_entries"] = Input.m_Selected;
		State.counters["favorite_entries"] = Input.m_Favorite;
		State.counters["sort_mode"] = SortMode;
		State.SetLabel(SortMode == SORT_BY_DATE ? "cold-input;date-desc;input-refresh-excluded;plan-destruction-excluded" :
							  "cold-input;selected-favorite-name;input-refresh-excluded;plan-destruction-excluded");
	}

	void BM_SkinListPlanNameSort(benchmark::State &State)
	{
		RunSkinListPlanBenchmark(State, SORT_BY_NAME);
	}
	BENCHMARK(BM_SkinListPlanNameSort)->Arg(1000)->Arg(10000)->Arg(50000)->Unit(benchmark::kMicrosecond);

	void BM_SkinListPlanDateSort(benchmark::State &State)
	{
		RunSkinListPlanBenchmark(State, SORT_BY_DATE);
	}
	BENCHMARK(BM_SkinListPlanDateSort)->Arg(50000)->Unit(benchmark::kMicrosecond);

	// 8 行 × 6 列，并额外预热 2 行；每轮调用生产 helper，预期 (8 + 2) * 6 = 60。
	void BM_SkinListPlanWarmupEntries(benchmark::State &State)
	{
		const int Rows = static_cast<int>(State.range(0));
		const int Columns = static_cast<int>(State.range(1));
		const int ExtraRows = static_cast<int>(State.range(2));
		const int Expected = (Rows + ExtraRows) * Columns;
		int Entries = 0;
		for(auto _ : State)
		{
			Entries = SettingsSkinListFirstPageWarmupEntries(Rows * 50.0f, 50.0f, Columns, ExtraRows);
			benchmark::DoNotOptimize(Entries);
		}
		if(Entries != Expected)
		{
			State.SkipWithError("skin list first-page warmup helper mismatch");
			return;
		}
		State.SetItemsProcessed(State.iterations());
		State.counters["warmup_entries"] = Entries;
		State.SetLabel("rows=8;cols=6;extra_rows=2;production-helper");
	}
	BENCHMARK(BM_SkinListPlanWarmupEntries)->Args({8, 6, 2});

	// Tee 设置页 helper 使用实际列数（这里 6 列）和 8 行 + 1 行预取，
	// 首屏应覆盖 54 项；高度只用于标识输入规模，不再反算列数。
	void BM_SkinListPlanTeeWarmupEntries(benchmark::State &State)
	{
		const int Rows = static_cast<int>(State.range(0));
		constexpr int Columns = 6;
		const int Expected = (Rows + 1) * Columns;
		int Entries = 0;
		for(auto _ : State)
		{
			Entries = SettingsTeeSkinListFirstPageWarmupEntries(Rows * 50.0f, Columns);
			benchmark::DoNotOptimize(Entries);
		}
		if(Entries != Expected)
		{
			State.SkipWithError("tee skin list first-page warmup helper mismatch");
			return;
		}
		State.SetItemsProcessed(State.iterations());
		State.counters["warmup_entries"] = Entries;
		State.SetLabel("rows=8;cols=6;extra_rows=1;production-helper");
	}
	BENCHMARK(BM_SkinListPlanTeeWarmupEntries)->Arg(8);
} // namespace
