#ifndef GAME_CLIENT_QMUI_QMUIPERF_H
#define GAME_CLIENT_QMUI_QMUIPERF_H

#include <cstddef>
#include <cstdint>

class IClient;

inline constexpr size_t QM_MENU_TEXT_CACHE_CAPACITY = 4096;
inline constexpr uint64_t QM_MENU_TEXT_CACHE_MAX_AGE_FRAMES = 600;
inline constexpr size_t QM_ASSET_METADATA_CACHE_CAPACITY = 512;
inline constexpr size_t QM_LANGUAGE_ROW_CACHE_CAPACITY = 128;

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
