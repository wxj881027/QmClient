/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_UI_LISTBOX_H
#define GAME_CLIENT_UI_LISTBOX_H

#include "ui_scrollregion.h"

#include <game/client/QmUi/QmAnimResolve.h>
#include <game/client/QmUi/QmListEntryAnimation.h>
#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <cstdint>
#include <optional>

constexpr float QmListBoxScrollbarMetric(float PolicyValue, float CurrentValue, bool Overridden)
{
	return Overridden ? CurrentValue : PolicyValue;
}

constexpr bool QmListBoxShouldScrollToInitialSelection(bool InitialScrollPending, int SelectedIndex)
{
	return InitialScrollPending && SelectedIndex >= 0;
}

constexpr bool QmListBoxInitialScrollRemainsPending(bool InitialScrollPending, int SelectedIndex)
{
	return InitialScrollPending && SelectedIndex < 0;
}

inline CUIRect QmListBoxEntryAnimatedRect(const CUIRect &Rect, float OffsetY)
{
	CUIRect AnimatedRect = Rect;
	AnimatedRect.y += OffsetY;
	return AnimatedRect;
}

// 列表行的选中/悬停反馈复用一条轨道：旧行淡出、新行淡入，不逐行建节点。
// 滚动期间直接 snap，避免滚动中的行索引与上一帧反馈错位。
class CQmListRowFeedback
{
public:
	void Reset()
	{
		m_pAnim = nullptr;
		m_NodeKey = 0;
		m_Snap = true;
		m_Update = false;
		m_Initialized = false;
		m_Transitioning = false;
		m_Previous = -1;
		m_Current = -1;
		m_Candidate = -1;
		m_Progress = 1.0f;
	}

	void BeginFrame(CUiV2AnimationRuntime *pAnim, uint64_t NodeKey, bool Snap, bool Update)
	{
		m_pAnim = pAnim;
		m_NodeKey = NodeKey;
		m_Snap = Snap;
		m_Update = Update;
		m_Candidate = -1;
		if(!m_Update)
			return;
		if(m_Initialized && m_Transitioning && m_pAnim != nullptr)
			m_Progress = std::clamp(m_pAnim->GetValue(m_NodeKey, EUiAnimProperty::ALPHA, m_Progress), 0.0f, 1.0f);
	}

	void Observe(int Index)
	{
		if(Index >= 0)
			m_Candidate = Index;
	}

	float Alpha(int Index) const
	{
		// 滚动/筛选换页时为 snap 帧：只认本帧观察到的行，不复用上一帧的交叉淡变。
		if(m_Snap)
			return Index >= 0 && Index == m_Candidate ? 1.0f : 0.0f;
		if(!m_Initialized)
			return Index >= 0 && Index == m_Candidate ? 1.0f : 0.0f;
		if(Index < 0 || m_Current < 0)
			return 0.0f;
		if(m_Previous == m_Current)
			return Index == m_Current ? 1.0f : 0.0f;
		if(Index == m_Previous)
			return 1.0f - m_Progress;
		if(Index == m_Current)
			return m_Progress;
		return 0.0f;
	}

	void EndFrame()
	{
		if(!m_Update)
			return;
		if(!m_Initialized)
		{
			if(m_Candidate < 0)
				return;
			m_Previous = m_Current = m_Candidate;
			m_Progress = 1.0f;
			m_Initialized = true;
			if(m_pAnim != nullptr)
				m_pAnim->SetValue(m_NodeKey, EUiAnimProperty::ALPHA, 1.0f);
			return;
		}

		if(m_Candidate != m_Current)
		{
			if(m_Snap || m_pAnim == nullptr)
			{
				m_Previous = m_Current = m_Candidate;
				m_Progress = 1.0f;
				m_Transitioning = false;
				if(m_pAnim != nullptr)
					m_pAnim->SetValue(m_NodeKey, EUiAnimProperty::ALPHA, 1.0f);
				return;
			}

			if(m_Transitioning && m_Candidate == m_Previous)
			{
				std::swap(m_Previous, m_Current);
				m_Progress = 1.0f - m_Progress;
			}
			else
			{
				m_Previous = m_Current;
				m_Current = m_Candidate;
				m_Progress = 0.0f;
			}
			m_Transitioning = true;
			m_pAnim->SetValue(m_NodeKey, EUiAnimProperty::ALPHA, m_Progress);
			SUiAnimTransition Transition = ui_token::motion::HOVER_FADE;
			Transition.m_Priority = 1;
			Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
			m_pAnim->ResolveTargetValue(m_NodeKey, EUiAnimProperty::ALPHA, 1.0f, Transition);
			return;
		}

		if(!m_Transitioning || m_pAnim == nullptr)
			return;

		SUiAnimTransition Transition = ui_token::motion::HOVER_FADE;
		Transition.m_Priority = 1;
		Transition.m_Interrupt = EUiAnimInterruptPolicy::MERGE_TARGET;
		m_Progress = std::clamp(m_pAnim->ResolveTargetValue(m_NodeKey, EUiAnimProperty::ALPHA, 1.0f, Transition), 0.0f, 1.0f);
		if(!m_pAnim->HasActiveAnimation(m_NodeKey, EUiAnimProperty::ALPHA) && m_Progress > 0.999f)
		{
			m_Previous = m_Current;
			m_Progress = 1.0f;
			m_Transitioning = false;
			m_pAnim->SetValue(m_NodeKey, EUiAnimProperty::ALPHA, 1.0f);
		}
	}

private:
	CUiV2AnimationRuntime *m_pAnim = nullptr;
	uint64_t m_NodeKey = 0;
	bool m_Snap = true;
	bool m_Update = false;
	bool m_Initialized = false;
	bool m_Transitioning = false;
	int m_Previous = -1;
	int m_Current = -1;
	int m_Candidate = -1;
	float m_Progress = 1.0f;
};

struct CListboxItem
{
	bool m_Visible;
	bool m_Selected;
	// 整行矩形：行背景、命中区域和调用方自己的按钮都以它为准。
	CUIRect m_Rect;
	// 行内容区：胶囊行已经把 token 派生的左右内边距让出来，文字/图标排在这里；
	// 未启用胶囊行时与 m_Rect 相同，旧调用点用 m_Rect 排版不受影响。
	CUIRect m_ContentRect;
	CUi *m_pUi = nullptr;
	bool m_GaussianBlurSuppressed = false;

	CUiScopedGaussianBlurSuppression SuppressGaussianBlur() const
	{
		return CUiScopedGaussianBlurSuppression(m_pUi, m_GaussianBlurSuppressed);
	}
};

// Instances of CListBox must be static, as member addresses are used as UI item IDs
class CListBox : private CUIElementBase
{
private:
	CUIRect m_ListBoxView;
	CUIRect m_RowView;
	float m_ListBoxRowHeight;
	int m_ListBoxItemIndex;
	int m_ListBoxSelectedIndex;
	int m_ListBoxNewSelected;
	int m_ListBoxNewSelOffset;
	bool m_ListBoxUpdateScroll;
	int m_ListBoxNumItems;
	int m_ListBoxItemsPerRow;
	EUiWheelOwnerPriority m_WheelOwnerPriority;
	bool m_ListBoxItemSelected;
	bool m_ListBoxItemActivated;
	bool m_ScrollbarShown;
	float m_AutoSpacing;
	CScrollRegion m_ScrollRegion;
	EQmScrollProfile m_ScrollProfile = EQmScrollProfile::MENU_LIST;
	int m_BackgroundCorners;
	float m_ScrollbarWidth;
	float m_ScrollbarMargin;
	bool m_ScrollbarWidthOverridden;
	bool m_ScrollbarMarginOverridden;
	bool m_HasHeader;
	bool m_Active;
	bool m_HideScrollbar;
	bool m_ScrollbarAlwaysReserved;
	// 滚动条出现/消失时占位宽度平滑过渡：卡片宽度连续变化，内容不再瞬移。
	// 默认开启，只有明确需要「跳动式」布局的列表才关掉。
	bool m_AnimateScrollbarReserve;
	bool m_InitialScrollPending;
	// 上一帧绘制时间：列表被隐藏后再次出现时重新把选中项滚进视野。
	int64_t m_LastDrawTime = 0;
	float m_EntryAnimationElapsedSec;
	CQmListEntryAnimation m_EntryAnimation;
	// 依次淡入 + 上浮入场：当前行的父层显隐系数已推入，待下一行发射或
	// DoEnd 时恢复。
	std::optional<CUiScopedControlAlphaScale> m_EntryAlphaScope;
	// 行延迟基准：本帧首个发射行的绝对索引（SkipItems 后），滚动到中段
	// 重放时错开也从头算起。
	int m_EntryBaseItemIndex = 0;
	bool m_EntryBaseIndexCaptured = false;
	bool m_EntryAnimationFadeOnly = false;
	float m_ItemSelectionInset = 0.0f;
	// 统一行外观（胶囊行）：行距、行内左右内边距和覆盖式滚动条都由它开启。
	bool m_CapsuleRows = false;
	float m_RowInsetX = 0.0f;
	// 胶囊行：滚动条压在内容上时，行右侧要给滚动条让出的缝（避免胶囊被压住一角）。
	float m_RowReserveRight = 0.0f;
	bool m_AutoSpacingOverridden = false;
	// 自适应高度：可见行数夹在 [MinRows, MaxRows]；MaxRows <= 0 表示不接管高度。
	int m_MinVisibleRows = 0;
	int m_MaxVisibleRows = 0;
	// 覆盖式滚动条：不预留内容宽度，出现/消失带淡入滑出。
	bool m_ScrollbarOverlay = false;
	// 调用点是否显式给过选中/悬停颜色；没给时胶囊行用列表 token 的统一配色。
	bool m_ItemColorsOverridden = false;
	// 容器圆角由容器长宽推导；带标题的列表沿用 DoHeader 用整容器算出的那一档。
	float m_ContainerRadius = ui_token::radius::SMALL_MENU;
	bool m_HeaderThisFrame = false;
	// 本帧是否画了容器背板：只有背板才有圆角，胶囊行也才需要为它内退。
	bool m_BackgroundDrawn = false;
	CQmListRowFeedback m_SelectedFeedback;
	CQmListRowFeedback m_HoverFeedback;
	bool m_ScrollFeedbackSnap = false;
	int m_FeedbackNumItems = -1;
	void QmRestoreEntryScope();
	// 行反馈圆角：调用点没显式给半径时，单列整行是胶囊，多列网格沿用旧的小圆角。
	float ResolveRowCornerRadius() const;
	ColorRGBA m_SelectedItemActiveColor;
	ColorRGBA m_SelectedItemInactiveColor;
	ColorRGBA m_HoveredItemColor;

protected:
	CListboxItem DoNextRow();

public:
	CListBox();
	void Reset();
	// 显式重放入场动画：页面切换回列表所在页时由调用方触发，
	// 不依赖渲染间隙启发（列表被背景趟持续渲染时间隙永远不会出现）。
	void RestartEntryAnimation() { m_EntryAnimation.Reset(); }
	// 入场只淡入、不位移（默认关闭）。大面积卡片网格用：整卡位移读作"晃"
	// （onetake：运动留给小元素，大 ground 保持稳定；opacity 领先位移），
	// 行级小列表仍保留淡入+上浮的 wordRise。
	void SetEntryAnimationFadeOnly(bool FadeOnly) { m_EntryAnimationFadeOnly = FadeOnly; }
	// 选中/悬浮反馈内缩：网格卡片在格子内有半格间隙，反馈矩形须贴合卡片
	// 而不是铺满格子；同时选中态附带按钮同款常驻描边。行列表保持 0。
	void SetItemSelectionInset(float Inset) { m_ItemSelectionInset = Inset; }

	void DoHeader(const CUIRect *pRect, const char *pTitle, float HeaderHeight = 20.0f, float Spacing = 2.0f, int BackgroundCorners = IGraphics::CORNER_ALL);
	void DoAutoSpacing(float Spacing = 20.0f)
	{
		m_AutoSpacing = Spacing;
		m_AutoSpacingOverridden = true;
	}
	// 统一行外观：整行胶囊 + token 行距 + 按字号派生的左右内边距，并默认启用
	// 覆盖式滚动条（不占行宽）。FontSize 是行内文字字号，行内边距按
	// ui_token::list::RowInsetX 从它派生，与弹层条目同一条公式。
	void SetCapsuleRows(float FontSize);
	// 自适应高度：可见行数 = 条目行数夹在 [MinRows, MaxRows]，组件自己按行高和
	// 行距算内容高度，调用方只给可用上限，不用再自己 std::min；超过上限就出滚动条。
	// MaxRows <= 0 关闭接管（默认），高度完全由调用方矩形决定。
	void SetVisibleRowRange(int MinRows, int MaxRows);
	// 覆盖式滚动条开关：开启后行宽与有没有滚动条无关（默认随 SetCapsuleRows 打开）。
	void SetScrollbarOverlay(bool Overlay) { m_ScrollbarOverlay = Overlay; }
	void DoSpacing(float Spacing = 20.0f);
	void DoStart(float RowHeight, int NumItems, int ItemsPerRow, int RowsPerScroll, int SelectedIndex, const CUIRect *pRect = nullptr, bool Background = true, int BackgroundCorners = IGraphics::CORNER_ALL);
	int ItemIndex() const { return m_ListBoxItemIndex; }
	int ItemsPerRow() const { return m_ListBoxItemsPerRow; }
	float RowHeight() const { return m_ListBoxRowHeight; }
	float ScrollOffsetY() const { return -m_ScrollRegion.ContentScrollOffsetY(); }
	float ViewHeight() const { return m_ScrollRegion.ClipRect()->h; }
	// 可见裁剪区：弹层要贴着父容器摆放时用它当边界，避免飞进导航栏。
	const CUIRect &ClipRect() const { return *m_ScrollRegion.ClipRect(); }
	void ScrollToSelected() { m_ListBoxUpdateScroll = true; }
	// 已保存的选中项不一定是本次浏览目标；调用方可取消初始自动定位，显式定位仍有效。
	void SuppressInitialSelectionScroll() { m_InitialScrollPending = false; }
	void SkipItems(int Count);
	// CornerRadius < 0 时按行类型取默认值：单列整行是胶囊，多列网格沿用旧的小圆角。
	CListboxItem DoNextItem(const void *pId, bool Selected = false, float CornerRadius = -1.0f);
	CListboxItem DoCustomRow(float Height, bool ScrollHere = false);
	void ResetScroll()
	{
		m_ScrollRegion.Reset();
		m_InitialScrollPending = true;
	}
	CListboxItem DoSubheader();
	int DoEnd();

	// Active state must be set before calling DoStart.
	bool Active() const { return m_Active; }
	void SetWheelOwnerPriority(EUiWheelOwnerPriority Priority) { m_WheelOwnerPriority = Priority; }
	void SetActive(bool Active) { m_Active = Active; }
	void SetScrollProfile(EQmScrollProfile Profile) { m_ScrollProfile = Profile; }
	void SetHideScrollbar(bool HideScrollbar) { m_HideScrollbar = HideScrollbar; }
	void SetScrollbarAlwaysReserved(bool AlwaysReserved) { m_ScrollbarAlwaysReserved = AlwaysReserved; }
	void SetAnimateScrollbarReserve(bool Animate) { m_AnimateScrollbarReserve = Animate; }
	void SetItemColors(ColorRGBA SelectedActive, ColorRGBA SelectedInactive, ColorRGBA Hovered)
	{
		m_SelectedItemActiveColor = SelectedActive;
		m_SelectedItemInactiveColor = SelectedInactive;
		m_HoveredItemColor = Hovered;
		m_ItemColorsOverridden = true;
	}

	// 弹层补画选中描边时与 CListBox 的填充共用同一条 alpha，旧行也能淡出。
	float SelectionFeedbackAlpha(int Index) const { return m_SelectedFeedback.Alpha(Index); }

	bool WasItemSelected() const { return m_ListBoxItemSelected; }
	bool WasItemActivated() const { return m_ListBoxItemActivated; }

	bool ScrollbarShown() const { return m_ScrollbarShown; }
	float ScrollbarWidth() const { return ScrollbarShown() ? ScrollbarWidthMax() : 0.0f; }
	bool ScrollbarActive() const { return m_ScrollRegion.Active(); }
	bool ScrollbarAnimating() const { return m_ScrollRegion.Animating(); }
	bool WheelConsumedThisFrame() const { return m_ScrollRegion.WheelConsumedThisFrame(); }
	float ScrollbarWidthMax() const { return m_ScrollbarWidth; }
	void SetScrollbarWidth(float Width)
	{
		m_ScrollbarWidth = Width;
		m_ScrollbarWidthOverridden = true;
	}
	float ScrollbarMargin() const { return m_ScrollbarMargin; }
	void SetScrollbarMargin(float Margin)
	{
		m_ScrollbarMargin = Margin;
		m_ScrollbarMarginOverridden = true;
	}
};

#endif
