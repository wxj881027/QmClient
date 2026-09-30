// qm_benchmark_main.cpp：qm-benchmarks 可执行的公共入口与链接桩
// 所有 *_benchmark.cpp 共享这一个 main / IsInterrupted，避免重复定义
#include <benchmark/benchmark.h>

// 链接桩：game-server-without-main 不含 main.cpp，server.cpp 引用的
// IsInterrupted 需在此提供（与 gameworld_test.cpp 的 testrunner 桩同款）
bool IsInterrupted()
{
	return false;
}

BENCHMARK_MAIN();
