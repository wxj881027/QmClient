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

constexpr bool QmListBoxShouldScrollToSelectionAfterRenderGap(bool AutoScrollOnReentry, bool WasDrawnBefore, int64_t RenderGap, int64_t Frequency, int SelectedIndex)
{
	return AutoScrollOnReentry && WasDrawnBefore && SelectedIndex >= 0 &&
	       RenderGap > maximum<int64_t>(1, Frequency) / 4;
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
	// 重入列表时是否自动把选中项滚回视野。皮肤网格会关掉：加载卡顿时
	// 帧间隔不能当作用户重新打开列表，否则会把正在浏览的位置强行拉走。
	bool m_AutoScrollSelectionOnReentry = true;
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
	// 滚轮步长倍率：只缩放这一个列表的滚轮位移，不动键盘 PageUp/Down 的翻页行数，
	// 也不影响别的列表。行高特别大的网格（皮肤列表）用它把「每格两行」降下来。
	float m_WheelStepScale = 1.0f;
	// 悬浮反馈描边：卡片形态列表打开，行/胶囊列表保持关闭。
	bool m_RowFeedbackBorder = false;
	// 选中/悬浮填充延后由调用方按卡片矩形绘制（网格画廊用）。
	bool m_DeferredItemFeedback = false;
	// 条目不足时容器仍铺满调用方给的高度（网格画廊用）。
	bool m_FillViewportHeight = false;
	// 统一行外观（胶囊行）：行距、行内左右内边距和覆盖式滚动条都由它开启。
	bool m_CapsuleRows = false;
	float m_RowInsetX = 0.0f;
	// 胶囊行：滚动条压在内容上时，行右侧要给滚动条让出的缝（避免胶囊被压住一角）。
	float m_RowReserveRight = 0.0f;
	// 单列胶囊行可选：只在内容真的溢出、滚动条要出现时才让出那一列；
	// 让位量按时间推进，滚动条出现/消失时行胶囊横向滑动而不是瞬跳。
	bool m_ReserveWhenOverflowing = false;
	float m_ReserveRightCurrent = 0.0f;
	int64_t m_ReserveRightStepTime = 0;
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
	// 容器内边距：胶囊列表默认按行高分档，非胶囊列表为 0（旧页面观感不变）。
	// m_ContainerPadding 是档位，X/Y 是本帧按「行数不被挤掉」与列数收敛后的生效值。
	float m_ContainerPadding = 0.0f;
	float m_ContainerPaddingX = 0.0f;
	float m_ContainerPaddingY = 0.0f;
	bool m_ContainerPaddingOverridden = false;
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
	// 行首统一步进：本条目标记为「开启新的一行」时给一次行距并切出该行矩形，
	// 返回是否新行。DoNextItem / SkipItems / DoSubheader 共用，避免三处各推一次。
	bool BeginRowIfNeeded();

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
	// 而不是铺满格子；行列表保持 0。
	void SetItemSelectionInset(float Inset) { m_ItemSelectionInset = Inset; }
	// 悬浮反馈描边（**显式能力**，默认关闭）：关闭时悬浮只换填充，与弹层条目一致；
	// 卡片形态列表（资源贴图、皮肤、指令工坊）打开后，鼠标悬停的卡片勾一圈
	// ui_token::feedback::HOVER_BORDER 白边。选中态不受影响，仍只有填充。
	void SetRowFeedbackBorder(bool Border) { m_RowFeedbackBorder = Border; }
	// 反馈矩形延后到调用方算完卡片矩形之后再画（**显式能力**，默认关闭）。
	// 多列网格里格子矩形含行距与行内退，按格子画会与卡片错开一圈；调用方在
	// DoNextItem 之后调 DrawItemFeedback(卡片矩形) 才能逐像素贴合。开启后
	// DoNextItem 不再画任何填充，动画 alpha 仍由组件持有。
	void SetDeferredItemFeedback(bool Deferred) { m_DeferredItemFeedback = Deferred; }
	// 画当前条目的选中/悬浮反馈（仅在 SetDeferredItemFeedback(true) 时使用）。
	// Index 传循环里的条目序号（DoNextItem 之后组件游标已指向下一格）。
	void DrawItemFeedback(int Index, const CUIRect &Rect, float CornerRadius);
	// 高度填满调用方给的矩形（**显式能力**，默认关闭）：条目不足时组件默认把
	// 容器收成内容高度，面板下方留出一块「没画到」的空档。网格画廊要的是
	// 容器始终铺满面板，内容短就只在上方排版、下方留白由容器背景承担。
	void SetFillViewportHeight(bool Fill) { m_FillViewportHeight = Fill; }

	void DoHeader(const CUIRect *pRect, const char *pTitle, float HeaderHeight = 20.0f, float Spacing = 2.0f, int BackgroundCorners = IGraphics::CORNER_ALL);
	// 行距：在**行首**推进一次（多列网格每行只算一次，第一行不算），DoNextItem 与
	// SkipItems 走同一条步进。传入的行高就是单行内容高度，不要再把行距折进去。
	void DoAutoSpacing(float Spacing = 20.0f)
	{
		m_AutoSpacing = Spacing;
		m_AutoSpacingOverridden = true;
	}
	// 统一行外观：整行胶囊 + token 行距 + 按字号派生的左右内边距，并默认启用
	// 覆盖式滚动条（不占行宽）。FontSize 是行内文字字号，行内边距按
	// ui_token::list::RowInsetX 从它派生，与弹层条目同一条公式。
	void SetCapsuleRows(float FontSize);
	// 容器内边距（**显式能力**，默认 0）：行区在容器四边内缩，选中/悬浮胶囊不再压在
	// 容器圆角上。调用点声明档位时**必须同时**把 2×padding 算进列表高度
	// （ui_token::list::PaddedRowsHeight），否则组件会为保行数把留白兜底缩回 0。
	// 纵向留白给所有列表；横向留白只给多列网格（单列行的左右本就有行内内退与滚动条让位）。
	void SetContainerPadding(float Padding)
	{
		m_ContainerPadding = std::max(0.0f, Padding);
		m_ContainerPaddingOverridden = true;
	}
	// 本帧实际生效的纵向留白（未声明或高度不足时为 0）。
	float ContainerPadding() const { return m_ContainerPaddingY; }
	// 自适应高度：可见行数 = 条目行数夹在 [MinRows, MaxRows]，组件自己按行高和
	// 行距算内容高度，调用方只给可用上限，不用再自己 std::min；超过上限就出滚动条。
	// MaxRows <= 0 关闭接管（默认），高度完全由调用方矩形决定。
	void SetVisibleRowRange(int MinRows, int MaxRows);
	// 覆盖式滚动条开关：开启后行宽与有没有滚动条无关（默认随 SetCapsuleRows 打开）。
	void SetScrollbarOverlay(bool Overlay) { m_ScrollbarOverlay = Overlay; }
	// 单列胶囊行的让位策略：默认恒定为滚动条留出一列（拿不到真实内容高度的列表用）。
	// 开启后改为「滚动条真出现才让位」，并把这个宽度做成过渡动画。
	// 只对能提供准确条目数的列表有意义（服务器信息里的玩家行）。
	void SetReserveRightWhenOverflowing(bool Enabled) { m_ReserveWhenOverflowing = Enabled; }
	// 胶囊行/胶囊格右侧为滚动条让出的宽度（厚度 + 外边距 + 行缝）。调用点（表头列宽、
	// 外框内容宽度）在 DoStart 前就要这份值，所以由 token（或显式覆盖值）直接算，
	// 不依赖上一帧的滚动条状态；未开胶囊行或非覆盖式轨道时为 0。
	float RowReserveWidth() const;
	// 让位量的逐帧推进：目标由「内容是否溢出」决定，步长按真实时间折算，
	// 因此窗口高度、条目数变化时都是同一条速度，不会随帧率变快变慢。
	float StepRowReserveRight(float Target);
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
	void SetAutoScrollSelectionOnReentry(bool Enabled) { m_AutoScrollSelectionOnReentry = Enabled; }
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
	void SetWheelStepScale(float Scale) { m_WheelStepScale = std::clamp(Scale, 0.1f, 4.0f); }
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
