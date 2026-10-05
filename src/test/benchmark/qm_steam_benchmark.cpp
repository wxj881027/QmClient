// qm_steam_benchmark.cpp：Steam 客户端探测链微基准（Google Benchmark）
// 被测职责：qm_steam_auto_launch 启动路径每进程一次的 SteamFindClient 生产扫描，
// 以及注册表全部未命中时冷路径的主导项（运行中进程快照层）。
// 结果取决于本机是否安装 Steam 及其安装形态：装有 Steam 的机器上热路径命中
// 注册表用户级记录；未安装的机器上同一 case 即完整冷路径。数值均为系统调用
// 边界成本，不涉及任何窗口或进程启动副作用。
// 注意：本文件不进 testrunner（不是单元测试），由 CMakeLists 的 qm-benchmarks 目标单独编译
#include <base/fs.h>
#include <base/system.h>

#include <engine/client/steam_probe.h>
#include <engine/steam.h>

#include <benchmark/benchmark.h>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

// 计时外预检：探测成功时写出的路径必须真实存在（保证循环里测的是生产语义
// 而非空转）；本机未装 Steam 属合法被测状态，不视为错误。
static void PrecheckSteamFindClient(benchmark::State &State)
{
	char aPath[1024];
	if(SteamFindClient(aPath, (int)sizeof(aPath)) && (aPath[0] == '\0' || !fs_is_file(aPath)))
		State.SkipWithError("SteamFindClient succeeded but returned a non-existent path");
}

// 完整生产扫描：装有 Steam 的机器 = 热路径（注册表用户级记录命中）；
// 未安装的机器 = 完整冷路径（注册表未命中 → 进程快照 → 常见目录 → PATH）。
// 每次迭代 = 与客户端启动时完全一致的一次 SteamFindClient 调用。
static void BM_SteamFindClient_Scan(benchmark::State &State)
{
	PrecheckSteamFindClient(State);
	if(State.error_occurred())
		return;
	char aPath[1024];
	for(auto _ : State)
	{
		const bool Found = SteamFindClient(aPath, (int)sizeof(aPath));
		benchmark::DoNotOptimize(Found);
		benchmark::DoNotOptimize(aPath);
		benchmark::ClobberMemory();
	}
	State.SetItemsProcessed(State.iterations()); // 1 = 一次完整扫描
}
BENCHMARK(BM_SteamFindClient_Scan);

// 冷路径主导项：全系统进程快照层（仅注册表全部未命中后才会执行）。
// 每次迭代 = 一次 Toolhelp32 进程快照枚举，命中 steam.exe 时另含映像路径解析
// 与 fs_is_file 验证。Windows 专有；其他平台报告跳过。
static void BM_SteamProbe_PathFromRunningProcess(benchmark::State &State)
{
#if defined(CONF_FAMILY_WINDOWS)
	char aPath[1024];
	for(auto _ : State)
	{
		const bool Found = SteamProbePathFromRunningProcess(aPath, (int)sizeof(aPath));
		if(Found && (aPath[0] == '\0' || !fs_is_file(aPath)))
		{
			State.SkipWithError("probe succeeded but returned a non-existent path");
			return;
		}
		benchmark::DoNotOptimize(Found);
		benchmark::DoNotOptimize(aPath);
		benchmark::ClobberMemory();
	}
	State.SetItemsProcessed(State.iterations()); // 1 = 一次完整进程快照枚举
#else
	State.SkipWithError("Windows-only probe layer");
#endif
}
BENCHMARK(BM_SteamProbe_PathFromRunningProcess);
