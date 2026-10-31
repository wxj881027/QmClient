#ifndef GAME_CLIENT_COMPONENTS_SECTION_LOADER_H
#define GAME_CLIENT_COMPONENTS_SECTION_LOADER_H

#include <base/math.h>
#include <base/system.h>

#include <game/client/components/settings_runtime_cache.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class IStorage;

enum class ESettingsSectionState : uint8_t
{
	UNINITIALIZED,
	MEASURING,
	COMPACT,
	FULL,
};

enum class ESettingsCardDeckColumn : uint8_t
{
	LEFT,
	RIGHT,
};

enum class ESettingsCardDragHitRegion : uint8_t
{
	NONE,
	CHROME,
	CONTENT,
};

struct SSettingsCardDeckItem
{
	const char *m_pStableId = nullptr;
	const char *m_pSectionName = nullptr;
	ESettingsCardDeckColumn m_Column = ESettingsCardDeckColumn::LEFT;
	int m_Order = -1;
	float m_CachedHeight = 0.0f;
	CUIRect m_Rect;
	CUIRect m_HeaderRect;
};

struct SSettingsCardDeckDragState
{
	bool m_Active = false;
	bool m_PressPending = false;
	int64_t m_PressStartTime = 0; // 长按触发：按下时 time_get() 时间戳
	SSettingsCardDeckItem m_Item;
	SSettingsCardDeckItem m_PressedItem;
	float m_PlaceholderHeight = 0.0f;
	ESettingsCardDeckColumn m_DropColumn = ESettingsCardDeckColumn::LEFT;
	int m_DropIndex = -1;
};

struct SSettingsCardDeckDragStartInput
{
	const SSettingsCardDeckItem *m_pItem = nullptr;
	bool m_CtrlPressed = false;
	ESettingsCardDragHitRegion m_HitRegion = ESettingsCardDragHitRegion::NONE;
};

inline bool SettingsCardDeckCanStartDrag(const SSettingsCardDeckDragStartInput &Input)
{
	// 长按触发（不依赖 Ctrl，统一栖梦范式）
	return Input.m_HitRegion == ESettingsCardDragHitRegion::CHROME &&
	       Input.m_pItem != nullptr &&
	       Input.m_pItem->m_pStableId != nullptr &&
	       Input.m_pItem->m_pStableId[0] != '\0';
}

inline bool SettingsCardDeckSameStableId(const SSettingsCardDeckItem &A, const SSettingsCardDeckItem &B)
{
	return A.m_pStableId != nullptr &&
	       B.m_pStableId != nullptr &&
	       A.m_pStableId == std::string(B.m_pStableId);
}

inline void SettingsCardDeckBeginPress(SSettingsCardDeckDragState &DragState, const SSettingsCardDeckItem &Item)
{
	DragState.m_PressPending = true;
	DragState.m_PressedItem = Item;
}

inline bool SettingsCardDeckTryPromotePress(SSettingsCardDeckDragState &DragState)
{
	if(!DragState.m_PressPending || DragState.m_Active)
		return false;
	DragState.m_Active = true;
	DragState.m_Item = DragState.m_PressedItem;
	DragState.m_PlaceholderHeight = DragState.m_PressedItem.m_CachedHeight;
	DragState.m_DropColumn = DragState.m_PressedItem.m_Column;
	DragState.m_DropIndex = DragState.m_PressedItem.m_Order;
	DragState.m_PressPending = false;
	DragState.m_PressedItem = {};
	return true;
}

inline void SettingsCardDeckClearPress(SSettingsCardDeckDragState &DragState)
{
	DragState.m_PressPending = false;
	DragState.m_PressedItem = {};
}

inline bool SettingsCardDeckMoveWithinColumn(std::vector<std::string> &vOrder, const char *pStableId, int DropIndex)
{
	if(pStableId == nullptr || pStableId[0] == '\0')
		return false;

	auto It = std::find(vOrder.begin(), vOrder.end(), pStableId);
	if(It == vOrder.end())
		return false;

	std::string StableId = std::move(*It);
	const int OldIndex = (int)std::distance(vOrder.begin(), It);
	vOrder.erase(It);
	if(DropIndex > OldIndex)
		--DropIndex;
	if(DropIndex < 0)
		DropIndex = 0;
	if(DropIndex > (int)vOrder.size())
		DropIndex = (int)vOrder.size();
	vOrder.insert(vOrder.begin() + DropIndex, std::move(StableId));
	return true;
}

inline bool SettingsCardDeckMoveBetweenColumns(std::vector<std::string> &vFromOrder, std::vector<std::string> &vToOrder, const char *pStableId, int DropIndex)
{
	if(pStableId == nullptr || pStableId[0] == '\0')
		return false;

	auto It = std::find(vFromOrder.begin(), vFromOrder.end(), pStableId);
	if(It == vFromOrder.end())
		return false;

	std::string StableId = std::move(*It);
	vFromOrder.erase(It);
	if(DropIndex < 0)
		DropIndex = 0;
	if(DropIndex > (int)vToOrder.size())
		DropIndex = (int)vToOrder.size();
	vToOrder.insert(vToOrder.begin() + DropIndex, std::move(StableId));
	return true;
}

inline int SettingsCardDeckDropIndexForHoveredItem(const SSettingsCardDeckItem &Item, float MouseY)
{
	const float MidY = Item.m_Rect.y + Item.m_Rect.h * 0.5f;
	return MouseY > MidY ? Item.m_Order + 1 : Item.m_Order;
}

inline int SettingsCardDeckDropIndexForColumnItems(const std::vector<SSettingsCardDeckItem> &vItems, ESettingsCardDeckColumn Column, float MouseX, float MouseY, int FallbackDropIndex)
{
	std::vector<SSettingsCardDeckItem> vColumnItems;
	for(const SSettingsCardDeckItem &Item : vItems)
	{
		if(Item.m_Column == Column)
			vColumnItems.push_back(Item);
	}
	if(vColumnItems.empty())
		return FallbackDropIndex;

	float ColumnLeft = vColumnItems.front().m_Rect.x;
	float ColumnRight = vColumnItems.front().m_Rect.x + vColumnItems.front().m_Rect.w;
	for(const SSettingsCardDeckItem &Item : vColumnItems)
	{
		ColumnLeft = minimum(ColumnLeft, Item.m_Rect.x);
		ColumnRight = maximum(ColumnRight, Item.m_Rect.x + Item.m_Rect.w);
	}
	if(MouseX < ColumnLeft || MouseX > ColumnRight)
		return FallbackDropIndex;

	std::sort(vColumnItems.begin(), vColumnItems.end(), [](const SSettingsCardDeckItem &A, const SSettingsCardDeckItem &B) {
		return A.m_Rect.y < B.m_Rect.y;
	});
	for(const SSettingsCardDeckItem &Item : vColumnItems)
	{
		const float MidY = Item.m_Rect.y + Item.m_Rect.h * 0.5f;
		if(MouseY <= MidY)
			return Item.m_Order;
		if(MouseY <= Item.m_Rect.y + Item.m_Rect.h)
			return Item.m_Order + 1;
	}
	return vColumnItems.back().m_Order + 1;
}

inline ESettingsCardDeckColumn SettingsCardDeckDropColumnForMouseX(const CUIRect &LeftColumn, const CUIRect &RightColumn, float MouseX, ESettingsCardDeckColumn FallbackColumn)
{
	if(MouseX >= LeftColumn.x && MouseX <= LeftColumn.x + LeftColumn.w)
		return ESettingsCardDeckColumn::LEFT;
	if(MouseX >= RightColumn.x && MouseX <= RightColumn.x + RightColumn.w)
		return ESettingsCardDeckColumn::RIGHT;
	return FallbackColumn;
}

inline bool SettingsCardDeckIsDraggingItem(const SSettingsCardDeckDragState &DragState, const SSettingsCardDeckItem &Item)
{
	return DragState.m_Active && SettingsCardDeckSameStableId(DragState.m_Item, Item);
}

inline CUIRect SettingsCardDeckDropIndicatorRect(const SSettingsCardDeckItem &Item, int DropIndex, float Thickness)
{
	const float IndicatorY = DropIndex <= Item.m_Order ? Item.m_Rect.y : Item.m_Rect.y + Item.m_Rect.h;
	return {Item.m_Rect.x, IndicatorY - Thickness * 0.5f, Item.m_Rect.w, Thickness};
}

inline CUIRect SettingsCardDeckProxyRect(const SSettingsCardDeckItem &Item, float MouseX, float MouseY)
{
	return {MouseX - Item.m_Rect.w * 0.5f, MouseY - Item.m_Rect.h * 0.5f, Item.m_Rect.w, Item.m_Rect.h};
}

inline float SettingsCardDeckAutoScrollDelta(float MouseY, float ViewTop, float ViewBottom, float EdgeBand, float MaxDelta)
{
	if(EdgeBand <= 0.0f || MaxDelta <= 0.0f || ViewBottom <= ViewTop)
		return 0.0f;
	if(MouseY < ViewTop + EdgeBand)
		return -MaxDelta * ((ViewTop + EdgeBand - MouseY) / EdgeBand);
	if(MouseY > ViewBottom - EdgeBand)
		return MaxDelta * ((MouseY - (ViewBottom - EdgeBand)) / EdgeBand);
	return 0.0f;
}

struct SSecondaryPanelSpec
{
	float m_AnchorX = 0.0f;
	float m_AnchorY = 0.0f;
	float m_PreferredWidth = 0.0f;
	float m_PreferredHeight = 0.0f;
	float m_MinWidth = 0.0f;
	float m_MinHeight = 0.0f;
	float m_MaxWidth = 0.0f;
	float m_MaxHeight = 0.0f;
	float m_ScreenWidth = 0.0f;
	float m_ScreenHeight = 0.0f;
	float m_Margin = 0.0f;
};

inline CUIRect SettingsSecondaryPanelRect(const SSecondaryPanelSpec &Spec)
{
	const float AvailableWidth = maximum(0.0f, Spec.m_ScreenWidth - Spec.m_Margin * 2.0f);
	const float AvailableHeight = maximum(0.0f, Spec.m_ScreenHeight - Spec.m_Margin * 2.0f);
	const float MaxWidth = Spec.m_MaxWidth > 0.0f ? minimum(Spec.m_MaxWidth, AvailableWidth) : AvailableWidth;
	const float MaxHeight = Spec.m_MaxHeight > 0.0f ? minimum(Spec.m_MaxHeight, AvailableHeight) : AvailableHeight;
	const float Width = std::clamp(Spec.m_PreferredWidth, minimum(Spec.m_MinWidth, MaxWidth), MaxWidth);
	const float Height = std::clamp(Spec.m_PreferredHeight, minimum(Spec.m_MinHeight, MaxHeight), MaxHeight);
	const float X = std::clamp(Spec.m_AnchorX, Spec.m_Margin, maximum(Spec.m_Margin, Spec.m_ScreenWidth - Spec.m_Margin - Width));
	const float Y = std::clamp(Spec.m_AnchorY, Spec.m_Margin, maximum(Spec.m_Margin, Spec.m_ScreenHeight - Spec.m_Margin - Height));
	return {X, Y, Width, Height};
}

/**
 * A single section of a settings page.
 *
 * Each section provides three render callbacks:
 *   - MeasureFn:  calculate height without any rendering
 *   - RenderCompactFn: optional warmup/full-equivalent fallback; never render visible summary text
 *   - RenderFullFn:    render the full interactive section
 *
 * All callbacks receive a CUIRect for the available column space and return
 * the consumed height. The caller is responsible for advancing the column rect.
 *
 * Dependencies track which g_Config values affect this section's output.
 * Dirty sections refresh their config hash, but FULL sections still render
 * every frame because DDNet menus are immediate-mode UI.
 */
struct SSettingsSection
{
	const char *m_pName;
	const char *m_pStableCardId = nullptr;
	ESettingsSectionState m_State = ESettingsSectionState::UNINITIALIZED;
	float m_CachedHeight = 0.0f;
	bool m_HasCachedHeight = false;

	std::function<float(CUIRect &)> m_MeasureFn;
	std::function<float(CUIRect &)> m_RenderCompactFn;
	std::function<float(CUIRect &)> m_RenderFullFn;

	std::vector<const int *> m_DependencyConfigInts;
	std::vector<const unsigned *> m_DependencyConfigCols;
	uint64_t m_LastConfigHash = 0;
	bool m_Dirty = true; // force render on first frame
};

inline void SettingsCardDeckApplyOrder(std::vector<SSettingsSection> &vSections, const std::vector<std::string> &vOrder)
{
	std::vector<SSettingsSection> vCards;
	vCards.reserve(vSections.size());

	for(const std::string &StableId : vOrder)
	{
		for(const SSettingsSection &Section : vSections)
		{
			if(Section.m_pStableCardId == nullptr || StableId != Section.m_pStableCardId)
				continue;
			vCards.push_back(Section);
			break;
		}
	}

	for(const SSettingsSection &Section : vSections)
	{
		if(Section.m_pStableCardId == nullptr)
			continue;
		const auto It = std::find_if(vCards.begin(), vCards.end(), [pStableId = Section.m_pStableCardId](const SSettingsSection &Card) {
			return Card.m_pStableCardId != nullptr && Card.m_pStableCardId == std::string(pStableId);
		});
		if(It == vCards.end())
			vCards.push_back(Section);
	}

	size_t CardIndex = 0;
	for(SSettingsSection &Section : vSections)
	{
		if(Section.m_pStableCardId == nullptr)
			continue;
		if(CardIndex < vCards.size())
			Section = std::move(vCards[CardIndex++]);
	}
}

/**
 * Session UI cache saved to disk across sessions.
 *
 * Stores the last active tab and scroll position so the next launch can
 * pre-warm the relevant sections during the loading screen.
 */
struct SSessionUiCache
{
	int m_LastSettingsPage = -1;
	int m_LastTClientTab = -1;
	int m_LastQmTab = -1;
	float m_LastScrollY = 0.0f;
	SSettingsSectionCacheRuntimeKey m_RuntimeKey;
	bool m_Valid = false;
};

struct SSectionLoaderFrameStats
{
	int m_SectionsTotal = 0;
	int m_SectionsVisible = 0;
	int m_SectionsSkipped = 0;
	int m_LayoutDirtySections = 0;
	ESettingsCacheDirtyReason m_DirtyReason = ESettingsCacheDirtyReason::NONE;
};

/**
 * Drives progressive rendering of settings-page sections.
 *
 * Usage:
 *   1. Register() all sections before Begin(); frame-local callbacks must be
 *      registered each frame with the same section names to preserve state.
 *   2. Call Begin() for the current frame.
 *   3. Call Process() every frame; returns true while there is still work.
 *   4. Call Reset() on tab switch.
 *   5. Optionally call Warmup() during the loading screen.
 *
 * The loader advances sections through a four-state machine with a
 * per-frame time budget:
 *
 *   UNINITIALIZED  →  measure height (negligible cost)
 *   MEASURING      →  optional progressive warmup path when explicitly enabled
 *   COMPACT        →  render full interactive section (1–2 per frame max)
 *   FULL           →  render full UI every frame; dirty refreshes config hash
 *
 * Viewport priority ensures that sections near the current scroll position
 * are promoted before off-screen sections.
 *
 * Process() clears callbacks before returning, so persistent loaders do not
 * retain references to frame-local UI layout objects.
 */
class CSectionLoader
{
public:
	CSectionLoader();
	~CSectionLoader();

	/** Register the full set of sections, preserving state for matching names. */
	void Register(std::vector<SSettingsSection> vSections);

	/**
	 * Start progressive rendering for the given viewport rect.
	 * @param MainView     Full available area for the sections.
	 * @param TimeBudgetMs Per-frame CPU budget in milliseconds (default 5.0).
	 */
	void Begin(CUIRect MainView, float TimeBudgetMs = 5.0f);
	void Begin(CUIRect MainView, CUIRect Viewport, float TimeBudgetMs);

	/** Advance one frame. Returns true when there is still work left.
	 * RenderSections 为 false 时只推进测量、缓存和状态机，由外部统一卡片容器绘制内容。 */
	bool Process(bool RenderSections = true);

	/** All visible sections have reached FULL state. */
	bool IsComplete() const;

	/** Reset the state machine (e.g. when switching tabs). */
	void Reset();

	// -- Pre-warming (loading screen) --

	/**
	 * Pre-warm real section content for sections that were visible in the last
	 * session, so glyph atlases and section caches are ready before settings open.
	 * Call once per frame during the loading screen.
	 * @param pCache       Session cache from last run (null = skip).
	 * @param TimeBudgetMs Per-frame CPU budget in milliseconds (default 3.0).
	 * @returns true when warmup is finished.
	 */
	bool Warmup(const SSessionUiCache *pCache, float TimeBudgetMs = 3.0f);
	bool IsWarmupComplete() const;

	// -- Cache invalidation --

	/** Invalidate all section caches (e.g. after language change or resize). */
	void InvalidateCache(ESettingsCacheDirtyReason Reason = ESettingsCacheDirtyReason::CONFIG);

	/** Mark sections dirty that depend on the given config pointer. */
	void SetDirtyByConfig(const void *pConfigVar);

	// -- Session cache I/O --

	class CSessionCacheFileOperations
	{
	public:
		virtual ~CSessionCacheFileOperations() = default;
		virtual IOHANDLE Open(IStorage &Storage, const char *pFilename, int Flags);
		virtual unsigned Read(IOHANDLE File, void *pData, unsigned Size) { return io_read(File, pData, Size); }
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		// false或异常表示未替换，失败路径不能先删除旧目标。
		virtual bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget);
	};

	static bool LoadSessionCache(SSessionUiCache &Cache, const char *pFilename, IStorage *pStorage, CSessionCacheFileOperations *pOperations = nullptr) noexcept;
	static bool SaveSessionCache(const SSessionUiCache &Cache, const char *pFilename, IStorage *pStorage, CSessionCacheFileOperations *pOperations = nullptr) noexcept;
	static bool IsVisibleSummarySectionName(const char *pName);
	void SetRuntimeKey(const SSettingsSectionCacheRuntimeKey &RuntimeKey);
	void SetProgressiveEnabled(bool Enabled);
	void SetDeferredFarMeasurementEnabled(bool Enabled);
	void SetMaxSectionsPerFrame(int MaxSectionsPerFrame);

	// -- State exposed for the rendering loop --

	int m_ActiveTab = -1;
	CUIRect GetRunningColumn() const { return m_RunningColumn; }

	// -- Profiling --

	const char *GetPerfReport() const;
	const SSectionLoaderFrameStats &LastFrameStats() const { return m_LastFrameStats; }

private:
	std::vector<SSettingsSection> m_vSections;
	CUIRect m_MainView;
	CUIRect m_Viewport;
	CUIRect m_RunningColumn;
	double m_BudgetPerFrameMs = 5.0;

	int m_CurrentIndex = 0;
	bool m_Initialized = false;
	bool m_Complete = false;
	bool m_ProgressiveEnabled = false;
	bool m_DeferredFarMeasurementEnabled = false;
	int m_MaxSectionsPerFrame = 2;
	SSettingsSectionCacheRuntimeKey m_RuntimeKey;

	// Warmup state
	bool m_WarmupActive = false;
	int m_WarmupIndex = 0;
	float m_WarmupBudgetMs = 0.0f;
	const SSessionUiCache *m_pWarmupCache = nullptr;

	// Profiling
	double m_TotalFrameTimeMs = 0.0;
	SSectionLoaderFrameStats m_LastFrameStats;
	ESettingsCacheDirtyReason m_LastDirtyReason = ESettingsCacheDirtyReason::NONE;

	/** 0 = in viewport, 1 = near, 2 = far. */
	int ComputeViewportPriority(const CUIRect &SectionRect) const;
	static uint64_t ComputeConfigHash(const SSettingsSection &Section);
};

#endif // GAME_CLIENT_COMPONENTS_SECTION_LOADER_H
