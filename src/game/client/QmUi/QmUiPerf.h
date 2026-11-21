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

// 后台预热只吃本帧相对显示器刷新周期的剩余时间；没有余量就完全不跑。
constexpr float QmMenuTextBackgroundBudgetMs(float TargetFrameMs, float FrameMsAverage, float FrameMsP95)
{
	if(!(TargetFrameMs > 0.0f))
		return 0.0f;
	float ObservedFrameMs = std::max(FrameMsAverage, FrameMsP95);
	if(!(ObservedFrameMs > 0.0f))
		ObservedFrameMs = TargetFrameMs * 0.5f;
	// 预留 15%（最多 0.5 ms）吸收监控窗口滞后与不可切分的单次容器构建。
	const float ReserveMs = std::min(0.5f, TargetFrameMs * 0.15f);
	const float HeadroomMs = TargetFrameMs - ObservedFrameMs - ReserveMs;
	if(HeadroomMs < 0.05f)
		return 0.0f;
	return std::min(1.0f, HeadroomMs);
}

// 加载画面是可阻塞阶段，不受显示器帧率余量限制；仍按步限时，避免加载界面自身失去响应。
inline constexpr float QM_MENU_TEXT_LOADING_PREWARM_STEP_MS = 8.0f;

constexpr float QmMenuTextWarmupBudgetMs(bool BlockingLoading, float TargetFrameMs, float FrameMsAverage, float FrameMsP95)
{
	return BlockingLoading ? QM_MENU_TEXT_LOADING_PREWARM_STEP_MS : QmMenuTextBackgroundBudgetMs(TargetFrameMs, FrameMsAverage, FrameMsP95);
}

constexpr float QmMenuTargetFrameMs(int RefreshRateHz)
{
	const int RefreshRate = std::clamp(RefreshRateHz > 0 ? RefreshRateHz : 60, 30, 1000);
	return 1000.0f / (float)RefreshRate;
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
