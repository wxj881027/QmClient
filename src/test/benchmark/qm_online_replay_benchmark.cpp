#include <game/client/components/qmclient/online_replay_player.h>

#include <benchmark/benchmark.h>

// 直接测生产时钟；微基准只衡量 CPU 时间，不代替多人回放帧时间。
static void BM_OnlineReplayClock(benchmark::State &State)
{
	COnlineReplayClock Clock;
	Clock.Start(0.0, 1000000, 50);
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Clock);
		benchmark::DoNotOptimize(Clock.Tick(100.125));
		benchmark::DoNotOptimize(Clock.Intra(100.125));
	}
}
BENCHMARK(BM_OnlineReplayClock);

static void BM_OnlineReplayMemberToggle(benchmark::State &State)
{
	COnlineReplayMembers Members;
	Members.Reset(State.range(0));
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Members);
		benchmark::DoNotOptimize(Members.Toggle(0));
		benchmark::ClobberMemory();
	}
}
BENCHMARK(BM_OnlineReplayMemberToggle)->Arg(1)->Arg(16)->Arg(256);
