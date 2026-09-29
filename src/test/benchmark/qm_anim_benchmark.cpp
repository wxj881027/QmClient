// qm_anim_benchmark.cpp：UI 动画系统每帧成本基准（Google Benchmark）
// 覆盖 CUiV2AnimationRuntime 的五条真实每帧路径：
//  1. 稳态帧：Advance + 全节点布局解析（无活跃动画，测查表/空轨道遍历成本）
//  2. 活跃帧：全节点弹簧 REPLACE 重请求 + Advance + 解析（持续动画 UI 上界）
//  3. 出现帧：全节点 ResolvePresence（稳态 PRESENT）+ EndFrame + Advance
//  4. 切换帧：全节点每帧翻转可见性（进入/退出瞬态 + 打断 churn 上界）
//  5. 剪枝帧：大量一次性布局键的插入+剪枝 churn（缓存超限行为）
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，构建 run_cxx_benchmarks 目标
// 注意：带参数化 fixture 必须用 BENCHMARK_DEFINE_F + BENCHMARK_REGISTER_F(...)->Arg(n)；
// BENCHMARK_F 会自动注册一个无参裸实例，State.range(0) 读空参数向量直接 0xC0000005。
#include <benchmark/benchmark.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/UiTokens.h>

#include <memory>
#include <vector>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

namespace
{
	constexpr float FRAME_DT = 1.0f / 60.0f;

	SUiAnimRequest MakeSpringRequest(uint64_t NodeKey, float Target, uint32_t TrackId)
	{
		SUiAnimRequest Request;
		Request.m_NodeKey = NodeKey;
		Request.m_Property = EUiAnimProperty::POS_X;
		Request.m_Target = Target;
		Request.m_Transition.m_Driver = EUiAnimDriver::SPRING;
		Request.m_Transition.m_Interrupt = EUiAnimInterruptPolicy::REPLACE;
		Request.m_TrackId = TrackId;
		return Request;
	}
} // namespace

// 固件：按参数规模建立 N 节点 × 4 属性的动画运行时
class AnimBenchmark : public benchmark::Fixture
{
public:
	void SetUp(const benchmark::State &State) override
	{
		g_Config.m_QmUiMotionLevel = 2;
		const int NumNodes = static_cast<int>(State.range(0));
		m_pRuntime = std::make_unique<CUiV2AnimationRuntime>();
		m_pTree = std::make_unique<CUiV2Tree>();
		m_vNodeKeys.clear();
		m_Target.x = 40.0f;
		m_Target.y = 80.0f;
		m_Target.w = 120.0f;
		m_Target.h = 240.0f;
		for(int i = 0; i < NumNodes; i++)
		{
			const uint64_t NodeKey = 1000 + i;
			m_vNodeKeys.push_back(NodeKey);
			// POS 直接置为目标值：稳态帧基准（BM_AnimIdleFrame）从第一帧起
			// 就是纯查表路径，避免首帧 PositionNeedsSync 起弹簧造成瞬态污染
			m_pRuntime->SetValue(NodeKey, EUiAnimProperty::POS_X, m_Target.x);
			m_pRuntime->SetValue(NodeKey, EUiAnimProperty::POS_Y, m_Target.y);
			m_pRuntime->SetValue(NodeKey, EUiAnimProperty::WIDTH, 120.0f);
			m_pRuntime->SetValue(NodeKey, EUiAnimProperty::HEIGHT, 40.0f);
		}

		// presence 预热：把 BM_AnimPresenceFrame 的状态推进到稳态 PRESENT
		//（alpha 收敛到 1），使测量只含稳态帧成本
		const SUiAnimTransition DefaultTransition;
		for(uint64_t NodeKey : m_vNodeKeys)
			(void)m_pTree->ResolvePresence(*m_pRuntime, NodeKey, true, DefaultTransition);
		for(int Frame = 0; Frame < 90; Frame++)
		{
			m_pTree->EndFrame(*m_pRuntime);
			m_pRuntime->Advance(FRAME_DT);
		}
	}

	std::unique_ptr<CUiV2AnimationRuntime> m_pRuntime;
	std::unique_ptr<CUiV2Tree> m_pTree;
	std::vector<uint64_t> m_vNodeKeys;
	CUIRect m_Target;
};

// 稳态帧：无活跃动画，每帧 Advance + 全节点解析（纯查表路径）
BENCHMARK_DEFINE_F(AnimBenchmark, BM_AnimIdleFrame)(benchmark::State &State)
{
	const int NumNodes = static_cast<int>(State.range(0));
	for(auto _ : State)
	{
		m_pRuntime->Advance(FRAME_DT);
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			const CUIRect Resolved = m_pTree->ResolveLayoutTransition(*m_pRuntime, NodeKey, m_Target, ui_token::motion::CARD_REORDER);
			benchmark::DoNotOptimize(Resolved.x);
		}
	}
	State.SetItemsProcessed(State.iterations() * NumNodes);
}
BENCHMARK_REGISTER_F(AnimBenchmark, BM_AnimIdleFrame)->Arg(16)->Arg(64)->Arg(256);

// 持续动画帧：每帧全节点弹簧 REPLACE 重请求（永不收敛）+ Advance + 解析
BENCHMARK_DEFINE_F(AnimBenchmark, BM_AnimActiveSpringFrame)(benchmark::State &State)
{
	const int NumNodes = static_cast<int>(State.range(0));
	for(auto _ : State)
	{
		for(uint64_t NodeKey : m_vNodeKeys)
			m_pRuntime->RequestAnimation(MakeSpringRequest(NodeKey, 100.0f, static_cast<uint32_t>(NodeKey)));
		m_pRuntime->Advance(FRAME_DT);
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			const CUIRect Resolved = m_pTree->ResolveLayoutTransition(*m_pRuntime, NodeKey, m_Target, ui_token::motion::CARD_REORDER);
			benchmark::DoNotOptimize(Resolved.x);
		}
	}
	State.SetItemsProcessed(State.iterations() * NumNodes);
}
BENCHMARK_REGISTER_F(AnimBenchmark, BM_AnimActiveSpringFrame)->Arg(16)->Arg(64)->Arg(256);

// 出现帧：每帧全节点 ResolvePresence（SetUp 已预热到稳态 PRESENT）+ EndFrame + Advance
BENCHMARK_DEFINE_F(AnimBenchmark, BM_AnimPresenceFrame)(benchmark::State &State)
{
	const int NumNodes = static_cast<int>(State.range(0));
	const SUiAnimTransition DefaultTransition;
	for(auto _ : State)
	{
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			const SUiPresenceResult Presence = m_pTree->ResolvePresence(*m_pRuntime, NodeKey, true, DefaultTransition);
			benchmark::DoNotOptimize(Presence.m_Alpha);
		}
		m_pTree->EndFrame(*m_pRuntime);
		m_pRuntime->Advance(FRAME_DT);
	}
	State.SetItemsProcessed(State.iterations() * NumNodes);
}
BENCHMARK_REGISTER_F(AnimBenchmark, BM_AnimPresenceFrame)->Arg(16)->Arg(64)->Arg(256);

// 切换帧：每帧翻转全部节点可见性（SetUp 预热的 PRESENT 状态开始），覆盖
// ENTERING/EXITING 分支、MERGE_TARGET 优先级打断与 EndFrame 遍历——瞬态上界
BENCHMARK_DEFINE_F(AnimBenchmark, BM_AnimPresenceToggleFrame)(benchmark::State &State)
{
	const int NumNodes = static_cast<int>(State.range(0));
	const SUiAnimTransition DefaultTransition;
	bool Visible = true;
	for(auto _ : State)
	{
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			const SUiPresenceResult Presence = m_pTree->ResolvePresence(*m_pRuntime, NodeKey, Visible, DefaultTransition);
			benchmark::DoNotOptimize(Presence.m_Alpha);
		}
		m_pTree->EndFrame(*m_pRuntime);
		m_pRuntime->Advance(FRAME_DT);
		Visible = !Visible;
	}
	State.SetItemsProcessed(State.iterations() * NumNodes);
}
BENCHMARK_REGISTER_F(AnimBenchmark, BM_AnimPresenceToggleFrame)->Arg(16)->Arg(64)->Arg(256);

// 剪枝帧：每帧访问 N 个一次性布局键（10000+i，与固件节点键不相交），不调
// Advance——纯测 m_LayoutTransitions 的插入 + PruneLayoutTransitionCache 成本。
// Arg(4096)：缓存恰好饱和，测每调用全表扫描成本；Arg(8192)：超限，测扫描+逐出 churn
BENCHMARK_DEFINE_F(AnimBenchmark, BM_AnimLayoutCacheChurn)(benchmark::State &State)
{
	const int NumKeys = static_cast<int>(State.range(0));
	for(auto _ : State)
	{
		for(int i = 0; i < NumKeys; i++)
		{
			const CUIRect Resolved = m_pTree->ResolveLayoutTransition(*m_pRuntime, 10000 + i, m_Target, ui_token::motion::CARD_REORDER);
			benchmark::DoNotOptimize(Resolved.x);
		}
	}
	State.SetItemsProcessed(State.iterations() * NumKeys);
}
BENCHMARK_REGISTER_F(AnimBenchmark, BM_AnimLayoutCacheChurn)->Arg(4096)->Arg(8192);
