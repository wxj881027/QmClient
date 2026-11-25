#ifndef GAME_CLIENT_QMUI_QMUIPERF_H
#define GAME_CLIENT_QMUI_QMUIPERF_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

class IClient;

// 全量计划约 600–1000 条，且键会随窗口宽度/缩放产生旁支；8192 只用于兜底容量淘汰，
// 主要命中问题仍是计划与可见页的布局键漂移，不能只靠扩容掩盖。
inline constexpr size_t QM_MENU_TEXT_CACHE_CAPACITY = 8192;
// 容量已经覆盖全量计划，墙钟寿命只负责清理跨天不用的动态文本；不能设成几分钟，
// 否则长时间游戏后第一次打开设置仍会整页冷重建。
// 不能用 600 帧这类 UI 帧数：700 fps 下不足 1 秒，会把刚预建完的计划又清空。
inline constexpr int64_t QM_MENU_TEXT_CACHE_MAX_AGE_NS = 24LL * 60 * 60 * 1000 * 1000 * 1000;
inline constexpr int64_t QM_MENU_TEXT_CACHE_TRIM_INTERVAL_NS = 1000 * 1000 * 1000LL;
inline constexpr size_t QM_ASSET_METADATA_CACHE_CAPACITY = 512;
inline constexpr size_t QM_LANGUAGE_ROW_CACHE_CAPACITY = 128;
// 计划收集单元是不可切分的整页测量；运行时/加载阶段单步最多 4 个，
// 给字形预热、进度条和输入留出余量。启动前阻塞阶段仍由墙钟截止保护。
inline constexpr int QM_MENU_TEXT_PLAN_UNITS_PER_STEP = 4;
// 静态字形需求表完成后，最近 miss 热力图每个后台步骤最多补 8 个字形。
inline constexpr int QM_MENU_TEXT_DYNAMIC_GLYPH_PREWARM_PER_STEP = 8;
inline constexpr size_t QM_SETTINGS_GLYPH_DEMAND_BATCH_GLYPHS = 64;

constexpr int QmMenuTextPlanCollectionUnitsForStep(int RequestedBudget)
{
	return std::clamp(RequestedBudget, 0, QM_MENU_TEXT_PLAN_UNITS_PER_STEP);
}

// 菜单文本池按单调时钟过期，不能用 UI 帧或主循环 PerfFrame：帧率越高缓存寿命越短，
// 异步渲染下循环帧又会在两次可见帧之间跳号。
constexpr bool QmMenuTextCacheEntryExpired(int64_t Now, int64_t LastUsed)
{
	return LastUsed > 0 && Now >= LastUsed && Now - LastUsed > QM_MENU_TEXT_CACHE_MAX_AGE_NS;
}

// 过期扫描与插入解耦：每帧插入都遍历全池在 700 fps 下本身就是一条隐藏热路径。
constexpr bool QmMenuTextCacheTrimDue(int64_t Now, int64_t LastTrim)
{
	return LastTrim <= 0 || (Now >= LastTrim && Now - LastTrim >= QM_MENU_TEXT_CACHE_TRIM_INTERVAL_NS);
}

// 后台预热只吃主线程相对显示器刷新周期的剩余 CPU 时间；没有余量就完全不跑。
// FrameMsAverage/P95 传主循环 CPU 忙时，不能传墙钟帧间隔：vsync 休眠不是已占用预算。
constexpr float QmMenuTextBackgroundBudgetMs(float TargetFrameMs, float FrameMsAverage, float FrameMsP95)
{
	if(!(TargetFrameMs > 0.0f))
		return 0.0f;
	const float AverageMs = FrameMsAverage > 0.0f ? FrameMsAverage : TargetFrameMs * 0.5f;
	const float P95Ms = FrameMsP95 > 0.0f ? std::max(FrameMsP95, AverageMs) : AverageMs;
	// 预留 15%（最多 0.5 ms）吸收监控窗口滞后与不可切分的单次容器构建。
	const float ReserveMs = std::min(0.5f, TargetFrameMs * 0.15f);
	const float AverageHeadroomMs = TargetFrameMs - AverageMs - ReserveMs;
	if(AverageHeadroomMs < 0.05f)
		return 0.0f;
	const float P95HeadroomMs = TargetFrameMs - P95Ms - ReserveMs;
	if(P95HeadroomMs < 0.05f)
	{
		// p95 恰好贴住刷新周期时不能把后台预热永久归零；均值仍有空间时
		// 只放一个极小步，既不让单次抖动导致整段会话不预热，也不挤占可见帧。
		return std::min(0.1f, AverageHeadroomMs);
	}
	return std::min(1.0f, std::min(AverageHeadroomMs, P95HeadroomMs));
}

// UI 动画的呈现时钟：长帧（加载、系统弹窗、一次性资源任务）不能把弹簧和
// 过渡一次性推进到终点。逻辑状态仍按真实 tick 推进，只有纯呈现插值截断。
inline constexpr float QM_UI_RENDER_DELTA_MAX_SEC = 1.0f / 20.0f;
constexpr float QmUiRenderDeltaSeconds(float Dt)
{
	return std::clamp(Dt, 0.0f, QM_UI_RENDER_DELTA_MAX_SEC);
}

// 固定 120 Hz：0.22 s 的过渡有 26 个状态点，300 fps 下每 2.5 帧一个状态步；
// 插值仍按渲染帧补齐，观感不变，成本仍是每秒 120 次积分。长帧最多补 0.1 秒
// （12 步），避免加载/系统弹窗后的首个可见帧把页切换直接推到终点。
inline constexpr float QM_UI_LOGIC_STEP_SEC = 1.0f / 120.0f;
inline constexpr float QM_UI_LOGIC_MAX_CATCHUP_SEC = 0.1f;
struct SQmUiLogicStep
{
	// 本帧消费掉的完整逻辑步数对应的时间。
	float m_Seconds = 0.0f;
	// 下一逻辑步已经走过的比例 [0,1)；渲染帧用它做插值，避免高刷屏
	// 看到的动画只在 60 Hz 更新。
	float m_Fraction = 1.0f;
};
inline SQmUiLogicStep QmUiAdvanceLogicClockStep(float &AccumulatorSec, float RealDeltaSec)
{
	SQmUiLogicStep Step;
	if(!std::isfinite(RealDeltaSec) || RealDeltaSec <= 0.0f)
	{
		Step.m_Fraction = std::clamp(AccumulatorSec / QM_UI_LOGIC_STEP_SEC, 0.0f, 1.0f);
		return Step;
	}
	AccumulatorSec = std::min(AccumulatorSec + std::min(RealDeltaSec, QM_UI_LOGIC_MAX_CATCHUP_SEC * 2.0f), QM_UI_LOGIC_MAX_CATCHUP_SEC);
	const int Steps = (int)(AccumulatorSec / QM_UI_LOGIC_STEP_SEC);
	AccumulatorSec -= Steps * QM_UI_LOGIC_STEP_SEC;
	Step.m_Seconds = Steps * QM_UI_LOGIC_STEP_SEC;
	Step.m_Fraction = std::clamp(AccumulatorSec / QM_UI_LOGIC_STEP_SEC, 0.0f, 1.0f);
	return Step;
}
inline float QmUiAdvanceLogicClock(float &AccumulatorSec, float RealDeltaSec)
{
	return QmUiAdvanceLogicClockStep(AccumulatorSec, RealDeltaSec).m_Seconds;
}

// 加载画面允许比后台预热吃得更满，但仍必须给动态背景留出一个显示刷新周期。
// 8 ms 只是单步上限，160 Hz 下实际预算会收紧到约 5 ms，不能把背景拖到 120 Hz。
inline constexpr float QM_MENU_TEXT_LOADING_PREWARM_STEP_MS = 8.0f;
// 启动加载阶段按显示帧小步推进容器构建；字形仍只走 worker，主线程不等待光栅化。
inline constexpr float QM_MENU_TEXT_STARTUP_PREWARM_STEP_MS = 1.0f;

constexpr float QmMenuTextLoadingBudgetMs(float TargetFrameMs, float FrameMsAverage, float FrameMsP95)
{
	if(!(TargetFrameMs > 0.0f))
		return 0.0f;
	const float ObservedFrameMs = std::max(0.0f, std::max(FrameMsAverage, FrameMsP95));
	// 加载阶段也给渲染、输入和进度条留 20%（最多 1 ms），否则单步会把
	// 一帧直接顶到刷新周期之外。主线程忙时预算自然收缩到 0。
	const float ReserveMs = std::min(1.0f, TargetFrameMs * 0.20f);
	const float HeadroomMs = TargetFrameMs - ObservedFrameMs - ReserveMs;
	if(HeadroomMs < 0.05f)
		return 0.0f;
	return std::min(QM_MENU_TEXT_LOADING_PREWARM_STEP_MS, HeadroomMs);
}

constexpr float QmMenuTextWarmupBudgetMs(bool BlockingLoading, float TargetFrameMs, float FrameMsAverage, float FrameMsP95)
{
	return BlockingLoading ? QmMenuTextLoadingBudgetMs(TargetFrameMs, FrameMsAverage, FrameMsP95) : QmMenuTextBackgroundBudgetMs(TargetFrameMs, FrameMsAverage, FrameMsP95);
}

constexpr float QmMenuTargetFrameMs(int RefreshRateHz)
{
	const int RefreshRate = std::clamp(RefreshRateHz > 0 ? RefreshRateHz : 60, 30, 1000);
	return 1000.0f / (float)RefreshRate;
}

// 设置页文本计划允许在菜单空闲时续跑；任何输入/弹层/滚动帧都先让出。
constexpr bool QmMenuTextIdlePrewarmAllowed(bool MenuActive, bool PopupOpen, bool ActiveItem, bool TextInputActive, bool KeyBinderActive, bool UiPopupOpen, bool ScrollInputActive)
{
	return MenuActive && !PopupOpen && !ActiveItem && !TextInputActive && !KeyBinderActive && !UiPopupOpen && !ScrollInputActive;
}

constexpr bool QmMenuUiScrollPerfActive(bool WheelConsumedThisFrame, bool ScrollbarActive, bool ScrollbarAnimating)
{
	return WheelConsumedThisFrame || ScrollbarActive || ScrollbarAnimating;
}

struct SQmMenuUiFramePerf
{
	const char *m_pPage = nullptr;
	const char *m_pOperation = nullptr;
	int m_ItemsTotal = 0;
	int m_ItemsVisible = 0;
	int m_ItemsProcessed = 0;
	int m_ItemsSkipped = 0;
	float m_UiMs = -1.0f;
	float m_LayoutMs = -1.0f;
	float m_TextMs = -1.0f;
	int m_HeapAllocs = -1;
	int m_CacheHits = -1;
	int m_CacheMisses = -1;
	int m_CacheEvictions = -1;
};

void QmLogMenuUiFramePerf(const SQmMenuUiFramePerf &Frame, const IClient *pClient);

#endif
