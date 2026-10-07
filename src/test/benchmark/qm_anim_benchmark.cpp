// qm_anim_benchmark.cpp：UI 动画系统每帧成本基准（Google Benchmark）
// 覆盖 CUiV2AnimationRuntime 的五条真实每帧路径：
//  1. 稳态帧：Advance + 全节点布局解析（无活跃动画，测查表/空轨道遍历成本）
//  2. 活跃帧：全节点弹簧 REPLACE 重请求 + Advance + 解析（持续动画 UI 上界）
//  3. 出现帧：全节点 ResolvePresence（稳态 PRESENT）+ EndFrame + Advance
//  4. 切换帧：全节点每帧翻转可见性（进入/退出瞬态 + 打断 churn 上界）
//  5. 缓存：固定键命中与交替键集插入/剪枝分别测量
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，构建 run_cxx_benchmarks 目标
// 注意：带参数化 fixture 必须用 BENCHMARK_DEFINE_F + BENCHMARK_REGISTER_F(...)->Arg(n)；
// BENCHMARK_F 会自动注册一个无参裸实例，State.range(0) 读空参数向量直接 0xC0000005。
#include <engine/shared/config.h>

#include <game/client/QmUi/QmAnim.h>
#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmScroll.h>
#include <game/client/QmUi/QmTree.h>
#include <game/client/QmUi/UiButtonStyle.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/QmUi/cards/QmCardMeasureRevision.h>
#include <game/client/components/scoreboard.h>
#include <game/client/qm_icon.h>
#include <game/client/qm_icon_label.h>
#include <game/client/qm_icon_label_runs.h>
#include <game/client/ui.h>

#include <benchmark/benchmark.h>

#include <cmath>
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

// 各用例隔离全局运动配置，交错重复时不污染其他 fixture。
class UiMotionBenchmark : public benchmark::Fixture
{
	int m_SavedMotionLevel = 0;

public:
	void SetUp(const benchmark::State &) override
	{
		m_SavedMotionLevel = g_Config.m_QmUiMotionLevel;
		g_Config.m_QmUiMotionLevel = 2;
	}
	void TearDown(const benchmark::State &) override { g_Config.m_QmUiMotionLevel = m_SavedMotionLevel; }
};

// 夹具：按参数规模建立 N 节点 × 4 属性的动画运行时。
class AnimBenchmark : public UiMotionBenchmark
{
public:
	void SetUp(const benchmark::State &State) override
	{
		UiMotionBenchmark::SetUp(State);
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
			m_pRuntime->SetValue(NodeKey, EUiAnimProperty::WIDTH, m_Target.w);
			m_pRuntime->SetValue(NodeKey, EUiAnimProperty::HEIGHT, m_Target.h);
			(void)m_pTree->SyncLayoutTransition(*m_pRuntime, NodeKey, m_Target);
		}

		// presence 预热：把 BM_AnimPresenceFrame 的状态推进到稳态 PRESENT
		//（alpha 收敛到 1），使测量只含稳态帧成本
		const SUiAnimTransition DefaultTransition;
		for(int Frame = 0; Frame < 90; Frame++)
		{
			m_pTree->BeginFrame();
			for(uint64_t NodeKey : m_vNodeKeys)
				(void)m_pTree->ResolvePresence(*m_pRuntime, NodeKey, true, DefaultTransition);
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
	if(m_pRuntime->ActiveTrackCount() != 0)
	{
		State.SkipWithError("idle fixture still has active tracks");
		return;
	}
	State.SetLabel("stable-layout-v2");
	const int NumNodes = static_cast<int>(State.range(0));
	for(auto _ : State)
	{
		m_pRuntime->Advance(FRAME_DT);
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			CUIRect Resolved = m_pTree->ResolveLayoutTransition(*m_pRuntime, NodeKey, m_Target, ui_token::motion::CARD_REORDER);
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
	bool HighTarget = true;
	State.SetLabel("alternating-spring-target-v2");
	for(auto _ : State)
	{
		CUIRect Target = m_Target;
		Target.x = HighTarget ? 100.0f : 40.0f;
		HighTarget = !HighTarget;
		for(uint64_t NodeKey : m_vNodeKeys)
			m_pRuntime->RequestAnimation(MakeSpringRequest(NodeKey, Target.x, static_cast<uint32_t>(NodeKey)));
		m_pRuntime->Advance(FRAME_DT);
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			const CUIRect Resolved = m_pTree->ResolveLayoutTransition(*m_pRuntime, NodeKey, Target, ui_token::motion::CARD_REORDER);
			benchmark::DoNotOptimize(Resolved.x);
		}
	}
	State.SetItemsProcessed(State.iterations() * NumNodes);
}
BENCHMARK_REGISTER_F(AnimBenchmark, BM_AnimActiveSpringFrame)->Arg(16)->Arg(64)->Arg(256);

// 出现帧：每帧全节点 ResolvePresence（SetUp 已预热到稳态 PRESENT）+ EndFrame + Advance
BENCHMARK_DEFINE_F(AnimBenchmark, BM_AnimPresenceFrame)(benchmark::State &State)
{
	if(m_pRuntime->ActiveTrackCount() != 0)
	{
		State.SkipWithError("presence fixture did not settle");
		return;
	}
	State.SetLabel("stable-presence-v2");
	const int NumNodes = static_cast<int>(State.range(0));
	const SUiAnimTransition DefaultTransition;
	for(auto _ : State)
	{
		m_pTree->BeginFrame();
		for(uint64_t NodeKey : m_vNodeKeys)
		{
			SUiPresenceResult Presence = m_pTree->ResolvePresence(*m_pRuntime, NodeKey, true, DefaultTransition);
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
		m_pTree->BeginFrame();
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

// 缓存测量不初始化无关 presence/弹簧轨道，避免大规模夹具预热污染。
class LayoutCacheBenchmark : public UiMotionBenchmark
{
public:
	void SetUp(const benchmark::State &State) override
	{
		UiMotionBenchmark::SetUp(State);
		m_Runtime.Reset();
		m_Tree.Reset();
		m_Target = {40.0f, 80.0f, 120.0f, 240.0f};
	}
	CUiV2AnimationRuntime m_Runtime;
	CUiV2Tree m_Tree;
	CUIRect m_Target;
};

BENCHMARK_DEFINE_F(LayoutCacheBenchmark, BM_AnimLayoutCacheHits)(benchmark::State &State)
{
	const int NumKeys = static_cast<int>(State.range(0));
	for(int i = 0; i < NumKeys; i++)
		(void)m_Tree.SyncLayoutTransition(m_Runtime, 10000 + i, m_Target);
	State.SetLabel("fixed-key-cache-hit-v2");
	for(auto _ : State)
	{
		for(int i = 0; i < NumKeys; i++)
		{
			CUIRect Resolved = m_Tree.ResolveLayoutTransition(m_Runtime, 10000 + i, m_Target, ui_token::motion::CARD_REORDER);
			benchmark::DoNotOptimize(Resolved);
		}
	}
	State.SetItemsProcessed(State.iterations() * NumKeys);
}
BENCHMARK_REGISTER_F(LayoutCacheBenchmark, BM_AnimLayoutCacheHits)->Arg(256)->Arg(4096);

// 两组不相交键集交替访问，持续触发插入/剪枝；运行时值表最多保留 2N 个键。
BENCHMARK_DEFINE_F(LayoutCacheBenchmark, BM_AnimLayoutCacheChurn)(benchmark::State &State)
{
	const int NumKeys = static_cast<int>(State.range(0));
	bool SecondBank = false;
	State.SetLabel("alternating-key-cache-churn-v2");
	for(auto _ : State)
	{
		const uint64_t BaseKey = 10000 + (SecondBank ? NumKeys : 0);
		SecondBank = !SecondBank;
		for(int i = 0; i < NumKeys; i++)
		{
			CUIRect Resolved = m_Tree.ResolveLayoutTransition(m_Runtime, BaseKey + i, m_Target, ui_token::motion::CARD_REORDER);
			benchmark::DoNotOptimize(Resolved);
		}
	}
	State.SetItemsProcessed(State.iterations() * NumKeys);
}
BENCHMARK_REGISTER_F(LayoutCacheBenchmark, BM_AnimLayoutCacheChurn)->Arg(4096)->Arg(8192);

// 测量依赖在页面空闲与内容切换时都经过生产接口，覆盖搜索每帧聚合成本。
class CardMeasureRevisionBenchmark : public benchmark::Fixture
{
	CConfig m_SavedConfig;

public:
	void SetUp(const benchmark::State &) override
	{
		m_SavedConfig = g_Config;
		str_copy(g_Config.m_QmTranslateBackend, "llm");
		g_Config.m_QmTranslateShowAdvanced = 0;
	}
	void TearDown(const benchmark::State &) override { g_Config = m_SavedConfig; }
};

BENCHMARK_DEFINE_F(CardMeasureRevisionBenchmark, SearchRevision)(benchmark::State &State)
{
	qm_card_catalog::SQmFunctionCardLayoutState Layout;
	for(auto _ : State)
	{
		if(State.range(0) != 0)
			g_Config.m_QmTranslateShowAdvanced = !g_Config.m_QmTranslateShowAdvanced;
		benchmark::ClobberMemory();
		auto Revision = qm_card_catalog::MeasureModuleCardsRevision(Layout);
		benchmark::DoNotOptimize(Revision);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(CardMeasureRevisionBenchmark, SearchRevision)->Arg(0)->Arg(1);

BENCHMARK_DEFINE_F(CardMeasureRevisionBenchmark, TranslationRevision)(benchmark::State &State)
{
	for(auto _ : State)
	{
		if(State.range(0) != 0)
			g_Config.m_QmTranslateShowAdvanced = !g_Config.m_QmTranslateShowAdvanced;
		benchmark::ClobberMemory();
		auto Revision = qm_card_catalog::MeasureModuleCardRevision(qm_module::EQmModuleId::Translate);
		benchmark::DoNotOptimize(Revision);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK_REGISTER_F(CardMeasureRevisionBenchmark, TranslationRevision)->Arg(0)->Arg(1);

// 测量计分板真实内容尺寸与共享滚动状态更新，不包含图形设备或玩家绘制。
static void BM_ScoreboardScrollFrame(benchmark::State &State)
{
	const int NumPlayers = static_cast<int>(State.range(0));
	const SQmScrollConfig Config = QmNativeWheelScrollConfig(1.0f, 0.25f);
	CQmScrollState Scroll;
	int Frame = 0;
	for(auto _ : State)
	{
		const SQmScrollMetrics Metrics{333.0f, ScoreboardRowsHeight(NumPlayers, NumPlayers / 4, NumPlayers / 8, 20.0f, 0.0f, 8.0f, 12.0f)};
		if(Frame++ % 10 == 0)
			Scroll.AddWheelImpulse(Scroll.Offset() > Metrics.MaxOffset() / 2.0f ? 120.0f : -120.0f, Metrics, Config);
		Scroll.Advance(1.0f / 60.0f, Metrics, Config);
		benchmark::DoNotOptimize(Scroll.Offset());
	}
}
BENCHMARK(BM_ScoreboardScrollFrame)->Arg(17)->Arg(64)->Arg(128);

// 每轮解析一项按钮几何与一个弹层主题；输入由外部基准屏障防止常量折叠。
static void BM_SecondaryPanelPresentation(benchmark::State &State)
{
	CUIRect Slot{10.0f, 20.0f, 26.0f, 20.0f};
	unsigned Background = 0x97FFA6;
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Slot);
		benchmark::DoNotOptimize(Background);
		const CUIRect Button = QmUiSquareIconButtonRect(Slot);
		const SUiTheme Theme = ResolveSecondaryPanelTheme(Background, 75, 0xFFFFFF);
		benchmark::DoNotOptimize(Button);
		benchmark::DoNotOptimize(Theme);
	}
}
BENCHMARK(BM_SecondaryPanelPresentation);

// 覆盖深色、浅色及透明表面的生产前景解析，不包含绘制或设备工作。
static void BM_SurfaceForeground(benchmark::State &State)
{
	ColorRGBA Surface = State.range(0) == 0 ? ColorRGBA(0.1f, 0.1f, 0.1f, 1) : ColorRGBA(1, 1, 1, State.range(0) == 1 ? 1.0f : 0.2f);
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Surface);
		const ColorRGBA Foreground = ResolveUiSurfaceForeground(Surface);
		benchmark::DoNotOptimize(Foreground);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_SurfaceForeground)->Arg(0)->Arg(1)->Arg(2);

// 测量自动与手动文本策略及共享图标反馈，输入经过屏障避免折叠。
static void BM_TextColorPolicy(benchmark::State &State)
{
	ColorRGBA Surface(0.2f, 0.4f, 0.6f, 0.75f);
	unsigned Custom = 0x8FDDAD;
	int Mode = static_cast<int>(State.range(0));
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Surface);
		benchmark::DoNotOptimize(Custom);
		benchmark::DoNotOptimize(Mode);
		const ColorRGBA Color = ResolveUiTextColor(Surface, Mode, Custom);
		benchmark::DoNotOptimize(Color);
	}
}
BENCHMARK(BM_TextColorPolicy)->Arg(0)->Arg(1)->Arg(2)->Arg(3);

static void BM_IconButtonFeedback(benchmark::State &State)
{
	ColorRGBA Surface(0.2f, 0.4f, 0.6f, 1);
	bool Hovered = State.range(0) != 0;
	bool Pressed = State.range(0) == 2;
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Surface);
		benchmark::DoNotOptimize(Hovered);
		benchmark::DoNotOptimize(Pressed);
		const ColorRGBA Feedback = ResolveUiIconButtonFeedback(Surface, true, Hovered, Pressed);
		benchmark::DoNotOptimize(Feedback);
	}
}
BENCHMARK(BM_IconButtonFeedback)->Arg(0)->Arg(1)->Arg(2);

// 每轮解析一个次级按钮表面；分别测空闲、悬浮、按下和禁用，不包含 GPU 绘制。
static void BM_SecondaryButtonStyle(benchmark::State &State)
{
	ColorRGBA Surface(0.2f, 0.4f, 0.6f, 0.25f), Backdrop(0.1f, 0.1f, 0.1f, 1);
	bool Enabled = State.range(0) != 3;
	bool Hovered = State.range(0) != 0;
	bool Pressed = State.range(0) == 2;
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Surface);
		benchmark::DoNotOptimize(Backdrop);
		benchmark::DoNotOptimize(Enabled);
		benchmark::DoNotOptimize(Hovered);
		benchmark::DoNotOptimize(Pressed);
		const auto Style = ResolveUiSecondaryButtonStyle(Surface, Backdrop, Enabled, Hovered, Pressed);
		benchmark::DoNotOptimize(Style);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_SecondaryButtonStyle)->Arg(0)->Arg(1)->Arg(2)->Arg(3);

// 每轮解析一个真实标签：正文快速拒绝、单图标与组合图标，计时包含识别与布局。
static void BM_IconLabelResolve(benchmark::State &State)
{
	const std::string Loading = std::string(FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT) + FontIcons::FONT_ICON_ELLIPSIS;
	const char *pText = State.range(0) == 0 ? "Settings" : (State.range(0) == 1 ? FontIcons::FONT_ICON_STAR : Loading.c_str());
	const EFontPreset Preset = State.range(0) == 0 ? EFontPreset::DEFAULT_FONT : EFontPreset::ICON_FONT;
	const CUIRect Rect{0, 0, 40, 20};
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(pText);
		const auto Icons = QmIconLabelGlyphs(Preset, pText);
		benchmark::DoNotOptimize(Icons);
		if(Icons.m_Count > 0)
			benchmark::DoNotOptimize(QmIconLabelRect(Rect, 16, TEXTALIGN_MC));
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_IconLabelResolve)->Arg(0)->Arg(1)->Arg(2);

// 测量一条混合按钮标签的两遍分段，覆盖生产测量与绘制的无分配解析成本。
static void BM_IconLabelRuns(benchmark::State &State)
{
	const std::string Labels[] = {
		std::string(FontIcons::FONT_ICON_LIST_UL) + "12",
		std::string("菜单12") + FontIcons::FONT_ICON_GEAR,
		std::string("\xEF\x83\x89") + "3" + FontIcons::FONT_ICON_STAR};
	const char *pText = Labels[State.range(0)].c_str();
	int ExpectedRuns = State.range(0) == 2 ? 3 : 2;
	int PreflightRuns = 0;
	const bool Valid = QmVisitIconLabelRuns(pText, [&](const auto &) { ++PreflightRuns; });
	if(!Valid || PreflightRuns != ExpectedRuns)
	{
		State.SkipWithError("icon label run preflight failed");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(pText);
		for(int Pass = 0; Pass < 2; ++Pass)
		{
			int Bytes = 0;
			QmVisitIconLabelRuns(pText, [&](const SQmIconLabelRun &Run) {
				Bytes += Run.m_Length;
				benchmark::DoNotOptimize(Run.m_Icon);
				benchmark::DoNotOptimize(Run.m_pFallback);
			});
			benchmark::DoNotOptimize(Bytes);
		}
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_IconLabelRuns)->Arg(0)->Arg(1)->Arg(2);

// 单次解析本体与保护色，分别测量已知 UI 背景和未知地图背景；准备与恢复不计时。
static void BM_IconColorResolve(benchmark::State &State)
{
	const double OriginalTime = CQmIconFrameColorClock::Time();
	CQmIconFrameColorClock::BeginFrame(1.25);
	const int OriginalPreset = g_Config.m_QmUiIconColor;
	const int OriginalEnabled = g_Config.m_QmUiIconCustomColorEnabled;
	const unsigned OriginalCustom = g_Config.m_QmUiIconCustomColor;
	g_Config.m_QmUiIconColor = static_cast<int>(State.range(0));
	g_Config.m_QmUiIconCustomColorEnabled = static_cast<int>(State.range(1));
	g_Config.m_QmUiIconCustomColor = ColorHSLA(0.37f, 0.8f, 0.45f).Pack(false);
	CUiScopedSurfaceText Surface(nullptr, ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f), State.range(2) != 0);
	ColorRGBA Input(1.0f, 0.85f, 0.3f, 0.65f);
	const ColorRGBA Preflight = ConfiguredQmUiIconColor(Input);
	const ColorRGBA OutlinePreflight = ConfiguredQmUiIconContrastColor(Preflight);
	if(!std::isfinite(Preflight.r) || !std::isfinite(Preflight.g) || !std::isfinite(Preflight.b) || Preflight.a != Input.a || !std::isfinite(OutlinePreflight.r) || !std::isfinite(OutlinePreflight.g) || !std::isfinite(OutlinePreflight.b) || !std::isfinite(OutlinePreflight.a) || OutlinePreflight.a < 0.0f || OutlinePreflight.a > Input.a * 0.35f)
		State.SkipWithError("icon color must preserve state alpha and bounded finite surface protection");
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Input);
		const ColorRGBA Output = ConfiguredQmUiIconColor(Input);
		const ColorRGBA Outline = ConfiguredQmUiIconContrastColor(Output);
		benchmark::DoNotOptimize(Output);
		benchmark::DoNotOptimize(Outline);
	}
	CQmIconFrameColorClock::BeginFrame(OriginalTime);
	g_Config.m_QmUiIconColor = OriginalPreset;
	g_Config.m_QmUiIconCustomColorEnabled = OriginalEnabled;
	g_Config.m_QmUiIconCustomColor = OriginalCustom;
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_IconColorResolve)->Args({1, 0, 0})->Args({2, 0, 0})->Args({4, 0, 0})->Args({1, 1, 0})->Args({4, 1, 0})->Args({1, 0, 1})->Args({2, 0, 1})->Args({4, 0, 1})->Args({1, 1, 1})->Args({4, 1, 1});
