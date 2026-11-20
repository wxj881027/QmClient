// 客户端侧权威世界（CLocalWorld）每 tick 热路径基准。
//
// 运行：配置加 -DDOWNLOAD_BENCHMARK=ON，构建 qm-benchmarks 目标，然后
//   qm-benchmarks --benchmark_filter=LocalWorld
//
// 覆盖：帧序列化、双轨采样、事件保序插入、开关状态编码、长轨迹分段重组、
//       世界音效门控。全部是练习/回放共用路径，回归时对比 ns/op 即可发现
//       「每加一个 actor 就线性变慢」这类退化。
#include <game/client/components/qmclient/local_world.h>
#include <game/client/components/qmclient/world_event_gate.h>

#include <benchmark/benchmark.h>

#include <vector>

static SLocalActorFrame MakeFrame(int Tick)
{
	SLocalActorFrame Frame;
	Frame.m_Tick = Tick;
	Frame.m_X = 3200 + Tick;
	Frame.m_Y = 1600;
	Frame.m_VelX = 512;
	Frame.m_VelY = -256;
	Frame.m_Angle = 42;
	Frame.m_Direction = -1;
	Frame.m_Weapon = 1;
	Frame.m_AttackTick = Tick - 10;
	return Frame;
}

static void BM_LocalWorldFrameToNetObj(benchmark::State &State)
{
	const SLocalActorFrame Frame = MakeFrame(1234);
	for(auto _ : State)
	{
		const CNetObj_Character Obj = QmLocalWorldFrameToNetObj(Frame);
		benchmark::DoNotOptimize(Obj);
	}
}
BENCHMARK(BM_LocalWorldFrameToNetObj);

static void BM_LocalWorldAddTwoTrackFrames(benchmark::State &State)
{
	CLocalWorld Track;
	Track.BeginTrack(0);
	int Tick = 0;
	for(auto _ : State)
	{
		if(Tick % 4096 == 0)
		{
			State.PauseTiming();
			Track.BeginTrack(Tick); // 防止轨迹无界增长污染测量
			State.ResumeTiming();
		}
		const SLocalActorFrame Frame = MakeFrame(Tick++);
		Track.AddTrackFrame(0, Frame);
		Track.AddTrackFrame(1, Frame);
		benchmark::DoNotOptimize(Track.CurrentFrameCount());
	}
	State.SetItemsProcessed(State.iterations() * 2);
}
BENCHMARK(BM_LocalWorldAddTwoTrackFrames);

static void BM_LocalWorldAddTrackEvent(benchmark::State &State)
{
	CLocalWorld Track;
	Track.BeginTrack(0);
	int Tick = 0;
	for(auto _ : State)
	{
		if(Tick % 4096 == 0)
		{
			State.PauseTiming();
			Track.BeginTrack(Tick);
			State.ResumeTiming();
		}
		SLocalActorEvent Event;
		Event.m_Tick = Tick++;
		Event.m_Type = 1;
		Event.m_aData[2] = 7;
		Track.AddTrackEvent(Event);
		benchmark::DoNotOptimize(Track.CurrentEventCount());
	}
}
BENCHMARK(BM_LocalWorldAddTrackEvent);

static void BM_LocalWorldEncodeSwitchState(benchmark::State &State)
{
	std::vector<SSwitchers> vSwitchers(State.range(0));
	for(size_t i = 0; i < vSwitchers.size(); i++)
	{
		vSwitchers[i] = {};
		vSwitchers[i].m_aStatus[3] = (i % 5) == 0;
	}
	for(auto _ : State)
	{
		const SLocalSwitchState Encoded = QmEncodeLocalSwitchState(1000, vSwitchers, 3);
		benchmark::DoNotOptimize(Encoded);
	}
	State.SetItemsProcessed(State.iterations() * vSwitchers.size());
}
BENCHMARK(BM_LocalWorldEncodeSwitchState)->Arg(16)->Arg(256);

static void BM_LocalWorldBestSegment(benchmark::State &State)
{
	const int Frames = State.range(0);
	CLocalWorld Track;
	Track.BeginTrack(0);
	for(int i = 0; i < Frames; i++)
		Track.AddTrackFrame(0, MakeFrame(i));
	for(int i = 0; i < Frames; i += 50)
	{
		SLocalActorEvent Event;
		Event.m_Tick = i;
		Event.m_Type = 1;
		Track.AddTrackEvent(Event);
	}
	for(auto _ : State)
	{
		SLocalSegment Segment;
		Track.BestSegment(&Segment);
		benchmark::DoNotOptimize(Segment);
	}
	State.SetItemsProcessed(State.iterations() * Frames);
}
BENCHMARK(BM_LocalWorldBestSegment)->Arg(1000)->Arg(10000);

static void BM_LocalWorldSoundGate(benchmark::State &State)
{
	for(auto _ : State)
	{
		const bool Allowed = QmShouldPlayWorldSound(SOUND_GUN_FIRE, true, true, true, true, true, true);
		benchmark::DoNotOptimize(Allowed);
	}
}
BENCHMARK(BM_LocalWorldSoundGate);
