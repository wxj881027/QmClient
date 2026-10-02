#ifndef GAME_CLIENT_QMUI_SETTINGSCARDDECKLOGIC_H
#define GAME_CLIENT_QMUI_SETTINGSCARDDECKLOGIC_H

#include <game/client/QmUi/QmAnimationBackend.h>
#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SCardMotionSpec;

struct SSettingsCardDeckItemGeometry
{
	int m_StateIndex = -1;
	int m_Column = 0;
	CUIRect m_Rect;
};

struct SSettingsCardDeckDiagnosticGeometry
{
	const char *m_pStableId = nullptr;
	int m_Column = 0;
	CUIRect m_Rect;
	float m_TargetContentHeight = 0.0f;
	float m_AnimatedContentHeight = 0.0f;
	bool m_FirstLayout = false;
	bool m_HeightAnimationActive = false;
};

struct SSettingsCardDeckFrameDiagnostics
{
	static constexpr size_t MAX_GEOMETRY = 64;

	uint32_t m_DefinitionsPrepareCount = 0;
	uint32_t m_MeasureCount = 0;
	uint32_t m_EntryAnimationResolveCount = 0;
	uint32_t m_HeightAnimationResolveCount = 0;
	uint32_t m_ReflowAnimationResolveCount = 0;
	uint32_t m_PreLayoutInputCount = 0;
	uint32_t m_RenderedCardCount = 0;
	uint32_t m_ChromePlanCount = 0;
	uint32_t m_TotalGeometryCount = 0;
	size_t m_GeometryCount = 0;
	bool m_FirstLayout = false;
	std::array<SSettingsCardDeckDiagnosticGeometry, MAX_GEOMETRY> m_aGeometry;
};

// Deck 与无图形测试共同使用的本帧状态。诊断默认关闭，稳定帧不清零或复制诊断几何。
class CSettingsCardDeckFrameRuntime
{
public:
	bool BeginDisplayCycle(uint64_t DisplayCycle, bool AnimateEntry);
	void OnTabChanged(bool StartEntryCycle = false);
	bool EntryCyclePending() const { return m_EntryDisplayCycle != m_DisplayCycle; }
	bool ConsumeEntryCycle();
	float ResolveContinuousEntryOffset(CUiV2AnimationRuntime &AnimRuntime, uint64_t NodeKey, const SCardMotionSpec &Motion);
	bool AnimateEntry() const { return m_AnimateEntry; }
	bool EntryWasActive() const { return m_EntryWasActive; }
	void SetEntryActive(bool Active) { m_EntryWasActive = Active; }

	void BeginFrame(SSettingsCardDeckFrameDiagnostics *pDiagnostics);
	void CountDefinitionsPrepare();
	void CountMeasure();
	void CountEntryAnimationResolve();
	void CountHeightAnimationResolve();
	void CountReflowAnimationResolve();
	void CountPreLayoutInput();
	void CountRenderedCard(bool ChromePlanned);
	void MarkFirstLayout();
	void RecordGeometry(const SSettingsCardDeckDiagnosticGeometry &Geometry);

private:
	uint64_t m_DisplayCycle = 0;
	uint64_t m_EntryDisplayCycle = UINT64_MAX;
	bool m_AnimateEntry = false;
	bool m_EntryWasActive = false;
	SSettingsCardDeckFrameDiagnostics *m_pDiagnostics = nullptr;
};

inline bool SettingsCardDeckHasActiveItemContinuation(const bool HasPointerInput, const bool HasActiveItem)
{
	// 持续处理只服务于按住/释放的鼠标序列。空闲的文本编辑或数值编辑
	// 不应驱动整个 Deck（包括被裁剪卡片）的预布局回调。
	return HasPointerInput && HasActiveItem;
}

inline bool SettingsCardDeckShouldRunPreLayoutInput(const bool HasPointerInput, const bool HasPendingInput, const bool HasActiveItemContinuation, const bool ControllerVisible, const bool Collapsed, const float VisibleContentHeight)
{
	return (((HasPointerInput || HasPendingInput) && ControllerVisible) || HasActiveItemContinuation) && !Collapsed && VisibleContentHeight > 0.0f;
}

inline bool SettingsCardDeckUsesDefaultCollapseControl(const bool HasCustomCollapsedState, const bool HasCustomHeaderInput)
{
	return !HasCustomCollapsedState && !HasCustomHeaderInput;
}

inline bool SettingsCardDeckResolveCollapsed(const bool HasCustomCollapsedState, const bool CustomCollapsed, const bool DefaultCollapsed)
{
	return HasCustomCollapsedState ? CustomCollapsed : DefaultCollapsed;
}

// 默认折叠按钮的唯一状态转移；自定义卡片和 RenderOnly 不能被公共按钮改写。
inline bool SettingsCardDeckApplyDefaultCollapseToggle(const bool HasCustomCollapsedState, const bool Collapsed, const bool TogglePressed, const bool RenderOnly)
{
	return !HasCustomCollapsedState && !RenderOnly && TogglePressed ? !Collapsed : Collapsed;
}

inline bool SettingsCardDeckDefinitionsRevisionChanged(const bool Initialized, const uint64_t CurrentRevision, const uint64_t NextRevision)
{
	return !Initialized || CurrentRevision != NextRevision;
}

inline bool SettingsCardDeckDefinitionsCacheKeyChanged(const bool Initialized, const uint64_t CurrentRevision, const uint64_t NextRevision, const char *pCurrentTab, const char *pNextTab)
{
	return SettingsCardDeckDefinitionsRevisionChanged(Initialized, CurrentRevision, NextRevision) ||
	       std::string_view(pCurrentTab != nullptr ? pCurrentTab : "") != std::string_view(pNextTab != nullptr ? pNextTab : "");
}

inline bool SettingsCardDeckLoadCollapsed(const std::unordered_map<std::string, bool> &States, const char *pStableId, const bool DefaultValue)
{
	const auto It = States.find(pStableId != nullptr ? pStableId : "");
	return It != States.end() ? It->second : DefaultValue;
}

inline void SettingsCardDeckStoreCollapsed(std::unordered_map<std::string, bool> &States, const char *pStableId, const bool Collapsed)
{
	if(pStableId != nullptr && pStableId[0] != '\0')
		States[pStableId] = Collapsed;
}

struct SSettingsCardAnimationWork
{
	bool m_ResolveEntry = false;
	bool m_ResetEntry = false;
	bool m_ResolveReflow = false;
	bool m_SetReflowTarget = false;
};

struct SSettingsCardHeightAnimationWork
{
	bool m_ResolveHeight = false;
	bool m_SetHeightTarget = false;
};

struct SSettingsCardColumnFrame
{
	float m_Y = 0.0f;
	float m_Height = 0.0f;
	float m_NextY = 0.0f;
};

class CSettingsCardColumnFramePlan
{
public:
	CSettingsCardColumnFramePlan(float CursorY, float CardGap) :
		m_CursorY(CursorY),
		m_CardGap(std::max(0.0f, CardGap))
	{
	}

	SSettingsCardColumnFrame Append(float CardHeight)
	{
		const float ResolvedHeight = std::max(0.0f, CardHeight);
		const SSettingsCardColumnFrame Frame{m_CursorY, ResolvedHeight, m_CursorY + ResolvedHeight + m_CardGap};
		m_CursorY = Frame.m_NextY;
		return Frame;
	}

	float CursorY() const { return m_CursorY; }
	void SetCursorY(float CursorY) { m_CursorY = CursorY; }

private:
	float m_CursorY;
	float m_CardGap;
};

// 以下函数是公共 Deck 的无渲染决策层；仅处理 model/order/geometry，不能依赖 UI renderer。
// 活动 state 集合由当前页面 definitions 决定，未注册的条件卡不得占用任何 layout slot。
std::array<std::vector<int>, 3> BuildSettingsCardDeckColumnOrder(const qm_card_order::CModel &Model, const char *pTab, const std::vector<int> &vActiveStateIndices);

// 单列沿用宽屏的阅读层级：每层先左、再右、最后全宽分隔卡。
// 回调签名为 void(int StateIndex, int CanonicalColumn)，遍历过程不分配内存。
template<typename F>
void ForEachSettingsCardDeckVisualOrder(const std::array<std::vector<int>, 3> &aColumns, F &&Callback, int LeadingFullWidthCards = 0)
{
	const size_t Leading = std::min(aColumns[0].size(), static_cast<size_t>(std::max(0, LeadingFullWidthCards)));
	for(size_t Index = 0; Index < Leading; ++Index)
		Callback(aColumns[0][Index], 0);
	const size_t NumLayers = std::max({aColumns[0].size() - Leading, aColumns[1].size(), aColumns[2].size()});
	for(size_t Layer = 0; Layer < NumLayers; ++Layer)
	{
		if(Layer < aColumns[1].size())
			Callback(aColumns[1][Layer], 1);
		if(Layer < aColumns[2].size())
			Callback(aColumns[2][Layer], 2);
		if(Layer + Leading < aColumns[0].size())
			Callback(aColumns[0][Layer + Leading], 0);
	}
}

// 运行时列投影缓存：仅在全局布局版本、tab 或活动 definition 集合变更时重建排序结果。
namespace settings_card_deck_logic
{
	class CProjectionCache
	{
	public:
		const std::array<std::vector<int>, 3> &Resolve(const qm_card_order::CModel &Model, const char *pTab, const std::vector<int> &vActiveStateIndices);
		uint64_t RebuildCount() const { return m_RebuildCount; }

	private:
		uint64_t m_LayoutRevision = UINT64_MAX;
		uint64_t m_RebuildCount = 0;
		std::string m_Tab;
		std::vector<int> m_vActiveStateIndices;
		std::array<std::vector<int>, 3> m_aColumns;
	};
} // namespace settings_card_deck_logic

void ApplySettingsCardDeckDragPlacement(std::array<std::vector<int>, 3> &aColumns, int ActiveStateIndex, int TargetColumn, int TargetOrder);
// 单列仅改变视觉顺序：full 卡保持 full 语义，普通卡按原左右可见容量稳定拆回 canonical 列。
void ApplySettingsCardDeckSingleColumnDragPlacement(std::array<std::vector<int>, 3> &aColumns, int ActiveStateIndex, int TargetOrder);
// vItems 必须由 layout 阶段按每列视觉顺序输出；该约束让拖拽热路径保持线性且零分配。
int ResolveSettingsCardDeckDropOrder(float MouseY, int TargetColumn, const std::vector<SSettingsCardDeckItemGeometry> &vItems, int IgnoredStateIndex);
float SettingsCardDeckAutoScrollDelta(float MouseY, const CUIRect &Viewport, float UiScale);
bool CommitSettingsCardDeckDrop(qm_card_order::CModel &Model, const char *pTab, const char *pStableId, int TargetColumn, int TargetOrder, const std::vector<int> *pActiveStateIndices = nullptr);
bool CommitSettingsCardDeckSingleColumnDrop(qm_card_order::CModel &Model, const char *pTab, const char *pStableId, int TargetOrder, const std::vector<int> &vActiveStateIndices);
// 折叠卡保留 header shell，但不得触发内容测量或内容绘制。
bool SettingsCardDeckNeedsContentMeasure(bool Collapsed, bool MeasureEachFrame, float CachedContentHeight);
bool SettingsCardDeckRendersContent(bool Collapsed);
// 已缓存卡片重新测量后高度发生变化时，必须像折叠/可见性变化一样直接同步几何。
bool SettingsCardDeckContentHeightChanged(float PreviousContentHeight, float ContentHeight);
// 折叠或条件可见性改变时直接同步几何；拖拽排序仍保留重排动画。
bool SettingsCardDeckShouldSnapReflow(bool GeometryStateChanged, bool DragActive);
// 滚动改变卡片在静止指针下的命中对象时，仅抑制当前滚动帧的 hover 反馈。
bool SettingsCardDeckScrollMoved(bool HasPreviousOffset, float PreviousOffsetY, float CurrentOffsetY);
// 卡片视觉位置与静态布局不一致时，禁止用静态 header 几何开始拖拽。
bool SettingsCardDeckAllowsDragStart(bool EntryPending, bool EntryPositionActive, bool ReflowTargetChanged, bool ReflowPositionActive);
// 稳定帧不得访问动画 runtime；仅目标变化、活动轨道或关闭动效后的复位需要工作。
SSettingsCardAnimationWork ResolveSettingsCardAnimationWork(float EntryDuration, bool EntryWasActive, bool ReflowInitializedThisFrame, bool SnapReflow, float ReflowDuration, bool ReflowTargetChanged, bool ReflowWasActive);
// 首帧直接采用稳定目标；之后仅在高度目标变化或轨道仍活动时访问动画 runtime。
SSettingsCardHeightAnimationWork ResolveSettingsCardHeightAnimationWork(bool InitializedThisFrame, bool TargetChanged, bool AnimationWasActive, float Duration, bool Snap);
// 只裁剪正在改变高度的卡片；同 Deck 的其他卡片不得跟随闪烁或截断 focus ring。
bool SettingsCardDeckShouldClipContent(bool HasRenderableContent, bool ContentHeightAnimationActive);
// 每一帧都从前一张卡当前动画底边继续，保证动态高度全过程同列卡片不重叠。
SSettingsCardColumnFrame ResolveSettingsCardColumnFrame(float CursorY, float CardHeight, float CardGap);
#endif // GAME_CLIENT_QMUI_SETTINGSCARDDECKLOGIC_H
