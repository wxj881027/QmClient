// qm_str_benchmark.cpp：项目字符串格式化热路径微基准（Google Benchmark）
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，然后构建 run_cxx_benchmarks 目标
// 注意：本文件不进 testrunner（不是单元测试），由 CMakeLists 的 qm-benchmarks 目标单独编译
#include <benchmark/benchmark.h>

#include <base/system.h>

#include <cstdio>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

// 项目封装的 str_format（日志/控制台输出热路径）
static void BM_StrFormat(benchmark::State &State)
{
	char aBuf[256];
	for(auto _ : State)
	{
		str_format(aBuf, sizeof(aBuf), "player %s killed %s with weapon %d at (%d, %d)",
			"chen", "death", 42, -1024, 512);
	}
	benchmark::DoNotOptimize(aBuf[0]);
}
BENCHMARK(BM_StrFormat);

// 标准 snprintf 对照组，量化 str_format 封装自身的开销
static void BM_Snprintf(benchmark::State &State)
{
	char aBuf[256];
	for(auto _ : State)
	{
		snprintf(aBuf, sizeof(aBuf), "player %s killed %s with weapon %d at (%d, %d)",
			"chen", "death", 42, -1024, 512);
	}
	benchmark::DoNotOptimize(aBuf[0]);
}
BENCHMARK(BM_Snprintf);
