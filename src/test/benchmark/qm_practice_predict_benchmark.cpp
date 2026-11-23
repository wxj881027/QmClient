// 快速练习预测循环与回放会话查询的每帧热路径基准。
//
// 运行：配置加 -DDOWNLOAD_BENCHMARK=ON，构建 qm-benchmarks 目标，然后
//   qm-benchmarks --benchmark_filter=PracticePredict
//
// 覆盖：
//   * 录制事件过滤：预测循环每 tick 对每条世界事件判定一次；
//   * 预测发布节拍：两条循环每 tick 各解析一次；
//   * 回放会话解析：HUD、相机与菜单每帧查询。
// 输入是固定组合，稳态计时；不含文件、网络与配置读取。
#include <game/client/components/qmclient/practice_branch_state.h>
#include <game/client/components/qmclient/practice_light_phase.h>
#include <game/client/components/qmclient/practice_predict_plan.h>
#include <game/client/components/qmclient/replay_session.h>

#include <benchmark/benchmark.h>

#include <array>
#include <iterator>
#include <vector>

namespace
{
	struct SRecordEvent
	{
		int m_EventId;
		int m_ExtraInfo;
	};

	// 一帧里典型的事件组合：交互事件、可播放/不可播放的世界音效与被过滤的类型。
	// 放进 vector 让每次判定读真实队列内存，避免常量数组被整段折叠。
	const SRecordEvent gs_aRecordEvents[] = {
		{NETEVENTTYPE_HAMMERHIT, 0},
		{NETEVENTTYPE_EXPLOSION, 0},
		{NETEVENTTYPE_DAMAGEIND, 0},
		{NETEVENTTYPE_SOUNDWORLD, SOUND_PLAYER_JUMP},
		{NETEVENTTYPE_SOUNDWORLD, SOUND_HOOK_ATTACH_GROUND},
		{NETEVENTTYPE_SOUNDWORLD, SOUND_GRENADE_EXPLODE},
		{NETEVENTTYPE_MAPSOUNDWORLD, 0},
		{NETEVENTTYPE_DEATH, 0},
	};
}

static void BM_PracticeRecordFilter(benchmark::State &State)
{
	const bool SndGame = State.range(0) != 0;
	std::vector<SRecordEvent> vEvents(std::begin(gs_aRecordEvents), std::end(gs_aRecordEvents));
	for(auto _ : State)
	{
		int Recorded = 0;
		for(const SRecordEvent &Event : vEvents)
		{
			if(QmShouldRecordPracticeEvent(Event.m_EventId, Event.m_ExtraInfo, SndGame,
				   true, true, true, true, true))
				Recorded++;
		}
		benchmark::DoNotOptimize(Recorded);
	}
	State.SetItemsProcessed(State.iterations() * (int)vEvents.size());
}
// 0：关闭游戏音效（全部走早退）；1：全部开关打开（完整判定）。
BENCHMARK(BM_PracticeRecordFilter)->Arg(0)->Arg(1);

static void BM_PracticePredictPublishActions(benchmark::State &State)
{
	const int FastInputTicks = (int)State.range(0);
	const bool PredictOthers = State.range(1) != 0;
	// 视觉外推计划：真实末帧 104，外推 1/6/20 帧，他人发布开关两种状态。
	const SPracticePredictTickPlan Plan = QmBuildPracticePredictTickPlan(104, 104, FastInputTicks, PredictOthers);
	for(auto _ : State)
	{
		int Actions = 0;
		for(int Tick = Plan.m_FirstTick; Tick <= Plan.m_FinalTickSelf; Tick++)
		{
			const SPracticePublishActions Resolved = QmResolvePracticePublishActions(Plan, Tick);
			Actions += Resolved.m_CopyPrevWorld + Resolved.m_PublishPrevRole + Resolved.m_PublishPrevOthers +
				   Resolved.m_PublishPredRole + Resolved.m_PublishPredOthers;
		}
		benchmark::DoNotOptimize(Actions);
	}
	State.SetItemsProcessed(State.iterations() * Plan.StepCount());
}
BENCHMARK(BM_PracticePredictPublishActions)->ArgsProduct({{1, 6, 20}, {0, 1}});

static void BM_PracticeEyeEmoteResolve(benchmark::State &State)
{
	const bool ServerAfk = State.range(0) != 0;
	SQmPracticeEyeEmoteState Override;
	if(State.range(1) != 0)
	{
		Override.m_Emote = EMOTE_ANGRY;
		Override.m_EndTick = 120;
	}
	// 每帧上限一队满员：服务器表情随参与者变化，避免整段循环被折叠成常量。
	std::array<int, MAX_CLIENTS> aServerEmotes;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		aServerEmotes[ClientId] = (ClientId & 1) ? EMOTE_BLINK : EMOTE_PAIN;
	for(auto _ : State)
	{
		int EmoteSum = 0;
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			EmoteSum += QmResolvePracticeEyeEmote(aServerEmotes[ClientId], !ServerAfk, ServerAfk,
				EMOTE_HAPPY, Override, 100);
		benchmark::DoNotOptimize(EmoteSum);
	}
	State.SetItemsProcessed(State.iterations() * MAX_CLIENTS);
}
// 0/1：服务器是否 AFK；0/1：手动表情覆盖是否在有效期内。
BENCHMARK(BM_PracticeEyeEmoteResolve)->ArgsProduct({{0, 1}, {0, 1}});

static void BM_ReplaySessionStatusResolve(benchmark::State &State)
{
	qmclient::replay::SReplaySessionInputs Inputs;
	switch(State.range(0))
	{
	case 0:
		// 查看模式：底层手动回放同时打开，判断优先级。
		Inputs.m_ViewModeActive = true;
		Inputs.m_ManualActive = true;
		Inputs.m_ManualPlaying = true;
		Inputs.m_ManualTick = 900;
		Inputs.m_ManualEndTick = 3600;
		Inputs.m_ManualSpeed = 1.0f;
		break;
	case 1:
		// 普通手动回放。
		Inputs.m_ManualActive = true;
		Inputs.m_ManualPlaying = false;
		Inputs.m_ManualTick = 120;
		Inputs.m_ManualEndTick = 480;
		Inputs.m_ManualSpeed = 0.5f;
		break;
	default:
		// 没有回放：最常见的空查询。
		break;
	}

	for(auto _ : State)
	{
		// 输入通过内存重新读取，避免把整次查询提升到计时循环外。
		benchmark::DoNotOptimize(Inputs);
		const qmclient::replay::SReplaySessionStatus Status = qmclient::replay::QmResolveReplaySessionStatus(Inputs);
		benchmark::DoNotOptimize(Status.m_Tick);
		benchmark::DoNotOptimize(Status.m_Kind);
	}
}
// 0：查看模式；1：手动回放；2：无回放。
BENCHMARK(BM_ReplaySessionStatusResolve)->Arg(0)->Arg(1)->Arg(2);

static void BM_PracticeLightPhase(benchmark::State &State)
{
	const int Ticks = (int)State.range(0);
	for(auto _ : State)
	{
		const qm_practice_light::SPhase Phase = qm_practice_light::After(0.25f, 40, 3, 256, pi / 180, 7, Ticks);
		benchmark::DoNotOptimize(Phase);
	}
}
BENCHMARK(BM_PracticeLightPhase)->Arg(120)->Arg(1200)->Arg(12000);
