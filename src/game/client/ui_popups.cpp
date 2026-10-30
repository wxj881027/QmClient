/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmUi/ColorPickerSlider.h"
#include "QmUi/QmAnimResolve.h"
#include "QmUi/QmDropdown.h"
#include "QmUi/QmPopupPointer.h"
#include "QmUi/QmPopupSource.h"
#include "QmUi/QmUiPerf.h"
#include "QmUi/SecondaryPanel.h"
#include "QmUi/UiSurface.h"
#include "QmUi/UiSurfaceText.h"
#include "QmUi/UiTheme.h"
#include "QmUi/UiTokens.h"
#include "components/qmclient/perf_logging.h"
#include "ui.h"
#include "ui_scrollregion.h"

#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/qm_icon.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
	// 弹层转场引擎轨道作用域：单一 ALPHA 值驱动开/合全程（开场目标 1、
	// 出场目标 0），打断由 MERGE_TARGET 速度继承续接，不再保存时间戳。
	constexpr uint64_t POPUP_ANIM_SCOPE = 0x9D1A7E5Bull;
	uint64_t PopupAnimKey(const SPopupMenuId *pId)
	{
		return BuildUiAnimNodeKey(POPUP_ANIM_SCOPE, reinterpret_cast<uint64_t>(pId));
	}
}

SPopupMenuProperties CUi::SecondaryPanelProperties()
{
	return ui_widget::SecondaryPanelProperties();
}

const CUIRect *CUi::GetPopupMenuRect(const SPopupMenuId *pId) const
{
	const auto PopupMenuIt = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return !PopupMenu.m_Closing && PopupMenu.m_pId == pId; });
	return PopupMenuIt == m_vPopupMenus.end() ? nullptr : &PopupMenuIt->m_Rect;
}

void CUi::DoPopupMenu(const SPopupMenuId *pId, float X, float Y, float Width, float Height, void *pContext, FPopupMenuFunction pfnFunc, const SPopupMenuProperties &Props)
{
	if(RenderOnly())
		return;
	const auto CaptureFocus = [this]() {
		SPopupFocus Focus = m_KeyboardFocus;
		if(CLineInput *pInput = CLineInput::GetActiveInput())
		{
			Focus.m_pItem = pInput;
			Focus.m_pTextInput = pInput;
			Focus.m_pPopupId = m_pTrackedTextPopupId;
			Focus.m_Source = m_TextInputSource;
		}
		else if(Focus.m_pItem == nullptr)
		{
			Focus.m_pItem = m_pLastActiveItem;
			Focus.m_pPopupId = m_pRenderingPopupId;
			Focus.m_Source.Assign(m_pInteractionContext, m_pInteractionCardId, PopupSourceFrame());
		}
		return Focus;
	};
	const auto SuspendFocus = [this]() {
		if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
			pActiveInput->Deactivate();
		ReleaseTrackedTextInput();
		m_KeyboardFocus = {};
		m_PendingPopupFocus = {};
		m_pLastActiveItem = nullptr;
		SetActiveItem(nullptr);
		m_ActiveButtonLogicButton = -1;
	};
	SPopupMenuProperties ResolvedProps = Props;
	// 三组件规约之「popup（动作展开菜单）」：调用方未显式配色（仍是旧默认
	// 黑底灰边）时，解析为统一下拉列表主题色——编辑器与客户端的裸
	// DoPopupMenu 调用点一次性接入设计语言；显式配色（二级面板、好友主题、
	// 下拉框等）不受影响。
	if(ResolvedProps.m_BackgroundColor == SPopupMenuProperties{}.m_BackgroundColor && ResolvedProps.m_BorderColor == SPopupMenuProperties{}.m_BorderColor)
	{
		const SUiTheme Theme = ResolveConfiguredDropdownListTheme();
		ResolvedProps.m_BackgroundColor = Theme.m_Surface;
		ResolvedProps.m_BorderColor = Theme.m_Border;
	}
	// 转场连续性默认启用，但受两重资格门控：
	// 1. 动效等级 qm_ui_motion_level == 0 时全局硬切（分级动效规约，
	//    各封装弹窗的局部门控同义，统一收口到此裁决）；
	// 2. 无动画运行时的环境（部分 UI 单测）没有轨道时钟，静默退化为硬切
	//    （转场已由引擎 ALPHA 轨道驱动，依赖运行时而非 IClient 时钟）。
	if(g_Config.m_QmUiMotionLevel <= 0 || QmAnimationRuntime() == nullptr)
		ResolvedProps.m_Animate = false;
	if(ResolvedProps.m_CenterInViewport)
	{
		// 居中模态弹层阻断底层滚动，动画开关保留调用方配置。
		ResolvedProps.m_BlockUnderlyingScroll = true;
	}
	if(ResolvedProps.m_CenterInViewport)
	{
		// 二级界面弹窗：在当前视口内居中，忽略调用点传入的锚定坐标；
		// 之后的 AutoReposition 仍兜底弹窗超出视口的情形。
		X = Screen()->x + (Screen()->w - Width) / 2.0f;
		Y = Screen()->y + (Screen()->h - Height) / 2.0f;
	}
	if(ResolvedProps.m_AutoReposition)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		if(X + Width > Screen()->w - Margin)
			X = maximum<float>(X - Width, Margin);
		if(Y + Height > Screen()->h - Margin)
			Y = maximum<float>(Y - Height, Margin);
	}

	auto ExistingPopupMenu = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(ExistingPopupMenu != m_vPopupMenus.end())
	{
		ExistingPopupMenu->m_Props = ResolvedProps;
		ExistingPopupMenu->m_Source.Assign(m_pInteractionContext, m_pInteractionCardId, PopupSourceFrame());
		ExistingPopupMenu->m_Rect.x = X;
		ExistingPopupMenu->m_Rect.y = Y;
		ExistingPopupMenu->m_Rect.w = Width;
		ExistingPopupMenu->m_Rect.h = Height;
		ExistingPopupMenu->m_pContext = pContext;
		ExistingPopupMenu->m_pfnFunc = pfnFunc;
		// 出场动画期间被重新打开：取消关闭状态即可——引擎 ALPHA 轨道按
		// MERGE_TARGET 从当前值带速度继续回到展开态，天然无跳变
		//（旧「等效进度反推」数学已随引擎迁移删除）。
		if(ExistingPopupMenu->m_Closing)
		{
			SPopupFocus Focus = CaptureFocus();
			// 来源尚未重绘时立即重开，沿用这一面板原来的返回目标；新点击焦点优先。
			if(m_KeyboardFocus.m_pItem == nullptr && CLineInput::GetActiveInput() == nullptr)
				Focus = ExistingPopupMenu->m_ReturnFocus;
			ExistingPopupMenu->m_ReturnFocus = ResolvedProps.m_ReturnFocus ? Focus : SPopupFocus{};
			ExistingPopupMenu->m_OpenedFrame = PopupSourceFrame();
			ExistingPopupMenu->m_Closing = false;
			if(ResolvedProps.m_BlockUnderlyingPointerInput)
				SuspendFocus();
		}
		return;
	}

	m_vPopupMenus.emplace_back();
	SPopupMenu *pNewMenu = &m_vPopupMenus.back();
	pNewMenu->m_pId = pId;
	pNewMenu->m_Props = ResolvedProps;
	pNewMenu->m_Source.Assign(m_pInteractionContext, m_pInteractionCardId, PopupSourceFrame());
	pNewMenu->m_ReturnFocus = ResolvedProps.m_ReturnFocus ? CaptureFocus() : SPopupFocus{};
	pNewMenu->m_OpenedFrame = PopupSourceFrame();
	pNewMenu->m_Rect.x = X;
	pNewMenu->m_Rect.y = Y;
	pNewMenu->m_Rect.w = Width;
	pNewMenu->m_Rect.h = Height;
	pNewMenu->m_pContext = pContext;
	pNewMenu->m_pfnFunc = pfnFunc;
	// 引擎轨道预置 0：首帧渲染从全透明展开，且地址复用不会继承旧轨道值。
	if(ResolvedProps.m_Animate && QmAnimationRuntime() != nullptr)
		QmAnimationRuntime()->SetValue(PopupAnimKey(pId), EUiAnimProperty::ALPHA, 0.0f);
	if(ResolvedProps.m_BlockUnderlyingPointerInput)
	{
		SuspendFocus();
		SetHotItem(pId);
	}
}

void CUi::RenderPopupMenus()
{
	PruneInteractionSources(false);
	// 公共入口自行接入模糊链，聊天和编辑器无需逐个补接线。
	CUiScopedGaussianBlur PopupBlurScope(this);
	// 禁用弹层模糊时，背景和分隔线的半透明绘制也不能触发自动背板模糊。
	CUiScopedGaussianBlurSuppression PopupBlurSuppression(this, g_Config.m_QmGaussianBlur == 0);
	// 页面文字绘制晚于菜单背板捕获；弹层栈需要完整页面的新快照，栈内继续复用。
	if(!m_vPopupMenus.empty() && g_Config.m_QmGaussianBlur != 0 && GaussianBlurScopeActive())
		m_GaussianBlurPrepared = false;
	m_RenderingPopupMenus = true;
	for(size_t i = 0; i < m_vPopupMenus.size(); ++i)
	{
		const SPopupMenu &PopupMenu = m_vPopupMenus[i];
		const SPopupMenuId *pId = PopupMenu.m_pId;

		// 出场动画中：逻辑已关闭，不参与任何输入判定与回调，只画收缩渐隐，
		// 动画结束后由本循环移除（不触发关闭回调，逻辑关闭时已触发过）。
		if(PopupMenu.m_Closing)
		{
			const float SpatialScale = g_Config.m_QmUiMotionLevel == 1 ? 0.35f : 1.0f;
			// 出场唯一事实源：引擎 ALPHA 轨道（MERGE_TARGET 打断继承速度）。
			// 壳几何与内容淡出全部由同一值派生（Carry 一致性）；动效等级的
			// 时长缩放由引擎内部应用。运行时缺失（异常环境）直接按已收完处理。
			float Alpha = 0.0f;
			if(CUiV2AnimationRuntime *pAnim = QmAnimationRuntime())
				Alpha = ResolveUiAnimValue(*pAnim, PopupAnimKey(pId), EUiAnimProperty::ALPHA, 0.0f, ui_token::motion::POPUP_CLOSE_SEC, EEasing::EASE_OUT);
			const float Remain = 1.0f - Alpha;
			CUIRect ShrinkRect = PopupMenu.m_Rect;
			const bool HasAlignedAnchorClosing = PopupMenu.m_Props.m_HasAnchorSurface &&
							     PopupMenu.m_Props.m_AnchorSurface.w > 0.0f && PopupMenu.m_Props.m_AnchorSurface.h > 0.0f &&
							     std::fabs(PopupMenu.m_Props.m_AnchorSurface.x - ShrinkRect.x) < 1.5f &&
							     std::fabs(PopupMenu.m_Props.m_AnchorSurface.w - ShrinkRect.w) < 1.5f;
			if(HasAlignedAnchorClosing)
			{
				const float ScaleY = 1.0f - 0.18f * SpatialScale * Remain;
				const bool Below = ShrinkRect.y >= PopupMenu.m_Props.m_AnchorSurface.y;
				if(!Below)
					ShrinkRect.y += ShrinkRect.h * (1.0f - ScaleY);
				ShrinkRect.h *= ScaleY;
			}
			else if(PopupMenu.m_Props.m_CenterInViewport)
			{
				// 居中模态面板与入场（上浮 8px）保持对称轨迹：退场向下微沉，不改變宽高。
				ShrinkRect.y += 8.0f * SpatialScale * Remain;
			}
			else
			{
				// 与入场（顶部锚定向下展开）互逆：顶部固定向上收起，不做中心缩放。
				const float ScaleY = 1.0f - 0.10f * SpatialScale * Remain;
				ShrinkRect.h *= ScaleY;
			}
			if(PopupMenu.m_Props.m_CenterInViewport && PopupMenu.m_Props.m_BlockUnderlyingPointerInput)
			{
				// 遮罩只叠色，避免模糊重画页面之前缓存的菜单背景。
				CUiScopedGaussianBlurSuppression OverlayBlurSuppression(this);
				CUIRect ScreenRect = *Screen();
				ScreenRect.Draw(ui_token::color::SURFACE_OVERLAY.WithAlpha(ui_token::color::SURFACE_OVERLAY.a * Alpha), IGraphics::CORNER_NONE, 0.0f);
			}
			const float CornerRadius = PopupMenu.m_Props.m_CenterInViewport ? ui_token::radius::CARD : ui_token::radius::BASE;
			ColorRGBA BgColor = PopupMenu.m_Props.m_BackgroundColor;
			ColorRGBA BorderColor = PopupMenu.m_Props.m_BorderColor;
			if(PopupMenu.m_Props.m_CenterInViewport)
			{
				const SUiTheme Theme = ResolveConfiguredSecondaryPanelTheme();
				BgColor = Theme.m_Surface;
				BorderColor = Theme.m_Border;
			}
			if(HasAlignedAnchorClosing)
			{
				const CUIRect &Anchor = PopupMenu.m_Props.m_AnchorSurface;
				const bool Below = ShrinkRect.y > Anchor.y;
				CUIRect UnionRect;
				if(Below)
					UnionRect = CUIRect{Anchor.x, Anchor.y, Anchor.w, ShrinkRect.y + ShrinkRect.h - Anchor.y};
				else
					UnionRect = CUIRect{Anchor.x, ShrinkRect.y, Anchor.w, Anchor.y + Anchor.h - ShrinkRect.y};
				DrawRoundedSurface(this, UnionRect, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), BorderColor.WithAlpha(BorderColor.a * Alpha), ui_token::radius::BASE, SPopupMenu::POPUP_BORDER, IGraphics::CORNER_ALL);
				CUIRect FillRect = ShrinkRect;
				FillRect.x += SPopupMenu::POPUP_BORDER;
				FillRect.w -= SPopupMenu::POPUP_BORDER * 2.0f;
				if(Below)
					FillRect.h -= SPopupMenu::POPUP_BORDER;
				else
				{
					FillRect.y += SPopupMenu::POPUP_BORDER;
					FillRect.h -= SPopupMenu::POPUP_BORDER;
				}
				if(g_Config.m_QmGaussianBlur != 0 && GaussianBlurScopeActive())
				{
					RenderGaussianBlur(FillRect, Alpha, Below ? IGraphics::CORNER_B : IGraphics::CORNER_T, ui_token::radius::BASE);
				}
				DrawRoundedSurface(this, FillRect, BgColor.WithAlpha(BgColor.a * Alpha), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ui_token::radius::BASE, 0.0f, Below ? IGraphics::CORNER_B : IGraphics::CORNER_T);
			}
			else
			{
				if(g_Config.m_QmGaussianBlur != 0 && GaussianBlurScopeActive())
				{
					RenderGaussianBlur(ShrinkRect, Alpha, PopupMenu.m_Props.m_Corners, CornerRadius);
				}
				DrawRoundedSurface(this, ShrinkRect,
					BgColor.WithAlpha(BgColor.a * Alpha),
					BorderColor.WithAlpha(BorderColor.a * Alpha),
					CornerRadius, SPopupMenu::POPUP_BORDER, PopupMenu.m_Props.m_Corners);
			}
			// 逻辑关闭后调用方可以立即重置或释放上下文；退场只绘制弹层外壳，不能再次调用内容回调。
			if(Alpha <= 0.001f)
			{
				m_vPopupMenus.erase(m_vPopupMenus.begin() + i);
				--i;
			}
			continue;
		}

		// 来源弹窗通常在同一渲染循环中刷新下拉层，但嵌套弹窗的父层回调
		// 可能在下一次 UI 更新才运行。允许一个 UI 帧差，避免下拉刚打开就
		// 被来源新鲜度检查收掉；CUi::Update 的兜底清扫仍会关闭真正失联的弹窗。
		if(PopupMenu.m_Props.m_RequireSourceRefresh && !QmDropdownSourceAlive(PopupSourceFrame(), PopupMenu.m_Props.m_SourceFrame, true))
		{
			ClosePopupMenu(pId, false, false);
			--i;
			continue;
		}
		const bool Inside = MouseInside(&PopupMenu.m_Rect) && (!PopupMenu.m_Props.m_ClipToViewport || MouseInside(&PopupMenu.m_Props.m_Viewport));
		// 活跃弹窗 = 栈顶未处于出场动画的弹窗（closing 弹窗不接管输入，
		// 下层的活跃弹窗/页面交互在短动画期间照常）。
		bool Active = true;
		for(size_t j = i + 1; j < m_vPopupMenus.size(); ++j)
		{
			if(!m_vPopupMenus[j].m_Closing)
			{
				Active = false;
				break;
			}
		}
		const bool ClipToViewport = PopupMenu.m_Props.m_ClipToViewport;
		// 任意活动弹层都能读取自己的鼠标；嵌套非模态子层不能被父层屏蔽。
		const bool AllowPopupPointerInput = Active;
		CQmPopupInputScope PopupInputScope(m_PopupInputDepth, AllowPopupPointerInput);

		// 非阻断弹窗沿用上游机制：栈顶弹窗每帧接管热项，既阻止弹窗打开期间
		// 底层 UI 被悬停/激活，也保证「弹窗外按下 → 成为活动项 → 弹窗外
		// 松开即关闭」的链路可用（编辑器菜单栏等非阻断弹窗依赖它）。
		if(Active && !PopupMenu.m_Props.m_BlockUnderlyingPointerInput)
			SetHotItem(pId);

		// 先保存关闭意图，绘制完成后释放输入深度，再运行关闭回调。
		// 作用域兜底保证提前退出也不会泄漏深度，防止底层页面抢占弹窗拖拽。
		const bool InsideGroup = std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [this](const SPopupMenu &Menu) {
			return !Menu.m_Closing && MouseInside(&Menu.m_Rect) && (!Menu.m_Props.m_ClipToViewport || MouseInside(&Menu.m_Props.m_Viewport));
		});
		const bool OpeningFrame = PopupMenu.m_Props.m_IgnoreOpeningClick && PopupMenu.m_OpenedFrame == PopupSourceFrame();
		const SQmPopupPointerInput PointerInput{Active, PopupMenu.m_Props.m_BlockUnderlyingPointerInput, Inside, !OpeningFrame && MouseButtonClicked(0) != 0, !OpeningFrame && MouseButton(0) != 0, CheckActiveItem(pId), HotItem() == pId, InsideGroup};
		const EQmPopupPointerAction PointerAction = QmResolvePopupPointerAction(PointerInput);
		const bool CloseBeforeRender = PointerAction == EQmPopupPointerAction::CLOSE || PointerAction == EQmPopupPointerAction::CLOSE_GROUP;
		if(PointerAction == EQmPopupPointerAction::CAPTURE)
			SetActiveItem(pId);
		else if(PointerAction == EQmPopupPointerAction::RELEASE)
			SetActiveItem(nullptr);

		EPopupMenuFunctionResult Result = POPUP_KEEP_OPEN;
		if(!CloseBeforeRender)
		{
			if(Inside && PopupMenu.m_Props.m_BlockUnderlyingScroll)
			{
				// Prevent scroll regions directly behind popup menus from using the mouse scroll events.
				SetHotScrollRegion(nullptr);
			}
			if(ClipToViewport)
				ClipEnable(&PopupMenu.m_Props.m_Viewport);

			CUIRect PopupRect = PopupMenu.m_Rect;
			// 满幅矩形：抽屉展开期内容按它布局（见下方 RevealDrawer）。
			const CUIRect FullPopupRect = PopupRect;
			const SPopupMenuProperties &PopupProps = PopupMenu.m_Props;
			// 对齐判定留浮点余量：几何端 AlignToAnchor 已保证严格相等，
			// 余量只吸收布局趟/渲染趟的舍入误差。
			const bool HasAlignedAnchor = PopupProps.m_HasAnchorSurface &&
						      PopupProps.m_AnchorSurface.w > 0.0f && PopupProps.m_AnchorSurface.h > 0.0f &&
						      std::fabs(PopupProps.m_AnchorSurface.x - PopupRect.x) < 1.5f &&
						      std::fabs(PopupProps.m_AnchorSurface.w - PopupRect.w) < 1.5f;

			// 入场转场：引擎 ALPHA 轨道为唯一事实源（首帧已预置 0），壳几何
			// 与内容透明度由同一值派生；与关闭共用一条轨道，中途反转由
			// MERGE_TARGET 带速度续接。非动画弹窗乘子恒 1，路径零变化。
			float AnimAlphaMul = 1.0f;
			if(PopupProps.m_Animate)
			{
				const float SpatialScale = g_Config.m_QmUiMotionLevel == 1 ? 0.35f : 1.0f;
				float OpenAlpha = 1.0f;
				if(CUiV2AnimationRuntime *pAnim = QmAnimationRuntime())
					OpenAlpha = ResolveUiAnimValue(*pAnim, PopupAnimKey(pId), EUiAnimProperty::ALPHA, 1.0f, ui_token::motion::POPUP_OPEN_SEC, EEasing::EASE_OUT);
				const float Remain = 1.0f - OpenAlpha;
				if(HasAlignedAnchor)
				{
					const float ScaleY = 1.0f - 0.12f * SpatialScale * Remain;
					const bool Below = PopupRect.y >= PopupProps.m_AnchorSurface.y;
					if(!Below)
					{
						const float OrigH = PopupRect.h;
						PopupRect.h *= ScaleY;
						PopupRect.y += (OrigH - PopupRect.h);
					}
					else
					{
						PopupRect.h *= ScaleY;
					}
				}
				else if(PopupProps.m_CenterInViewport)
				{
					// 模态面板保持内容尺寸，避免动画重排触发临时滚动条与命中漂移。
					PopupRect.y += 8.0f * SpatialScale * Remain;
				}
				else
				{
					// 无锚点弹层（下拉列表/上下文菜单等）：统一为顶部锚定的
					// 向下展开，与对齐锚点弹层同一抽屉语言；不再中心缩放——
					// 内容随缩放重排读作「弹一下」（onetake：大 ground 稳定）。
					const float ScaleY = 1.0f - 0.10f * SpatialScale * Remain;
					PopupRect.h *= ScaleY;
				}
				AnimAlphaMul = OpenAlpha;
			}
			if(PopupProps.m_CenterInViewport && PopupProps.m_BlockUnderlyingPointerInput)
			{
				// 遮罩只叠色，避免模糊重画页面之前缓存的菜单背景。
				CUiScopedGaussianBlurSuppression OverlayBlurSuppression(this);
				CUIRect ScreenRect = *Screen();
				ScreenRect.Draw(ui_token::color::SURFACE_OVERLAY.WithAlpha(ui_token::color::SURFACE_OVERLAY.a * AnimAlphaMul), IGraphics::CORNER_NONE, 0.0f);
			}
			ColorRGBA ForegroundSurface = PopupProps.m_BackgroundColor;
			if(HasAlignedAnchor)
			{
				// 边框把触发按钮与列表包成一个整体：外框只描边，填充由按钮
				//（页面阶段已绘制）与列表各自负责，接缝一侧用直角贴合。
				const CUIRect &Anchor = PopupProps.m_AnchorSurface;
				const bool Below = PopupRect.y > Anchor.y;
				CUIRect UnionRect;
				if(Below)
					UnionRect = CUIRect{Anchor.x, Anchor.y, Anchor.w, PopupRect.y + PopupRect.h - Anchor.y};
				else
					UnionRect = CUIRect{Anchor.x, PopupRect.y, Anchor.w, Anchor.y + Anchor.h - PopupRect.y};
				ColorRGBA BorderColor = PopupProps.m_BorderColor;
				ColorRGBA BgColor = PopupProps.m_BackgroundColor;
				if(BorderColor.a <= 0.0f)
					BorderColor = ui_token::color::BORDER_SUBTLE;
				ForegroundSurface = BgColor;
				DrawRoundedSurface(this, UnionRect, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), BorderColor.WithAlpha(BorderColor.a * AnimAlphaMul), ui_token::radius::BASE, SPopupMenu::POPUP_BORDER, IGraphics::CORNER_ALL);
				CUIRect FillRect = PopupRect;
				FillRect.x += SPopupMenu::POPUP_BORDER;
				FillRect.w -= SPopupMenu::POPUP_BORDER * 2.0f;
				if(Below)
					FillRect.h -= SPopupMenu::POPUP_BORDER;
				else
				{
					FillRect.y += SPopupMenu::POPUP_BORDER;
					FillRect.h -= SPopupMenu::POPUP_BORDER;
				}
				if(g_Config.m_QmGaussianBlur != 0 && GaussianBlurScopeActive())
				{
					RenderGaussianBlur(FillRect, AnimAlphaMul, Below ? IGraphics::CORNER_B : IGraphics::CORNER_T, ui_token::radius::BASE);
				}
				DrawRoundedSurface(this, FillRect, BgColor.WithAlpha(BgColor.a * AnimAlphaMul), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ui_token::radius::BASE, 0.0f, Below ? IGraphics::CORNER_B : IGraphics::CORNER_T);
			}
			else
			{
				const float CornerRadius = PopupProps.m_CenterInViewport ? ui_token::radius::CARD : ui_token::radius::BASE;
				ColorRGBA EffectiveBg = PopupProps.m_BackgroundColor;
				ColorRGBA EffectiveBorder = PopupProps.m_BorderColor;
				if(PopupProps.m_CenterInViewport)
				{
					const SUiTheme Theme = ResolveConfiguredSecondaryPanelTheme();
					EffectiveBg = Theme.m_Surface;
					EffectiveBorder = Theme.m_Border;
				}
				if(g_Config.m_QmGaussianBlur != 0 && GaussianBlurScopeActive())
				{
					RenderGaussianBlur(PopupRect, AnimAlphaMul, PopupProps.m_Corners, CornerRadius);
				}
				ForegroundSurface = EffectiveBg;
				DrawRoundedSurface(this, PopupRect,
					EffectiveBg.WithAlpha(EffectiveBg.a * AnimAlphaMul),
					EffectiveBorder.WithAlpha(EffectiveBorder.a * AnimAlphaMul),
					CornerRadius, SPopupMenu::POPUP_BORDER, PopupProps.m_Corners);
			}
			const float ContentInset = PopupProps.m_ContentInset >= 0.0f ? PopupProps.m_ContentInset : SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
			PopupRect.Margin(ContentInset, &PopupRect);

			// 抽屉展开期（尺寸在变的分支）内容按满幅矩形布局、用动画矩形做揭示
			// 裁剪：若让内容跟随临时高度布局，滚动区会把选中项滚进临时视口、
			// 抽屉长满后又移回（列表内容整体平移，读作弹跳——选中项在列表末尾
			// 的下拉必现，如模糊模式默认选中最后一项；选中首项的下拉从不触发，
			// 这就是「有的弹有的不弹」的根源）。满幅布局下滚动偏移从首帧就是
			// 终值，抽屉只做逐行揭示，展开方向始终从按钮边缘向外。居中模态
			// 只位移不改尺寸，无需揭示。
			const bool RevealDrawer = PopupProps.m_Animate && AnimAlphaMul < 0.999f &&
						  (HasAlignedAnchor || !PopupProps.m_CenterInViewport);
			CUIRect ContentRect = PopupRect;
			if(RevealDrawer)
			{
				ContentRect = FullPopupRect;
				ContentRect.Margin(ContentInset, &ContentRect);
				ClipEnable(&PopupRect);
			}

			// The popup render function can open/close popups, which may resize the vector and thus
			// invalidate the variable PopupMenu. We therefore store pId in a separate variable.
			CUiScopedSurfaceText SurfaceText(TextRender(), ForegroundSurface);
			// 公共控件跟随真实弹层入场透明度，不借用页面背景透明度。
			CUiScopedControlAlphaScale ContentAlpha(this, AnimAlphaMul);
			CUiScopedInteractionOwner InteractionOwner(this, PopupMenu.m_Source.Context(), PopupMenu.m_Source.CardId());
			m_pRenderingPopupId = pId;
			Result = PopupMenu.m_pfnFunc(PopupMenu.m_pContext, ContentRect, Active);
			m_pRenderingPopupId = nullptr;
			if(RevealDrawer)
				ClipDisable();
			if(ClipToViewport)
				ClipDisable();
		}
		PopupInputScope.Release();
		if(CloseBeforeRender)
		{
			// 点击父层只收起子层；点击整组外部则一起关闭，避免留下遮挡页面的父层。
			if(PointerAction == EQmPopupPointerAction::CLOSE_GROUP)
			{
				ClosePopupMenus();
				break;
			}
			ClosePopupMenu(pId);
			--i;
		}
		else if(Result != POPUP_KEEP_OPEN || (Active && ConsumeHotkey(HOTKEY_ESCAPE)))
		{
			ClosePopupMenu(pId, Result == POPUP_CLOSE_CURRENT_AND_DESCENDANTS);
		}
	}
	m_RenderingPopupMenus = false;
}

void CUi::ClosePopupMenu(const SPopupMenuId *pId, bool IncludeDescendants, bool RestoreFocus)
{
	auto PopupMenuToClose = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(PopupMenuToClose != m_vPopupMenus.end())
	{
		// 出场动画已在播放：等待渲染循环自然移除，避免重置动画起点或重复回调。
		if(!IncludeDescendants && PopupMenuToClose->m_Closing)
			return;
		const SPopupFocus ReturnFocus = PopupMenuToClose->m_Closing ? SPopupFocus{} : PopupMenuToClose->m_ReturnFocus;
		std::vector<std::function<void()>> vClosedCallbacks;
		for(auto It = PopupMenuToClose; It != m_vPopupMenus.end(); ++It)
		{
			if(!It->m_Closing && It->m_Props.m_OnClosed)
				vClosedCallbacks.push_back(It->m_Props.m_OnClosed);
			if(m_KeyboardFocus.m_pPopupId == It->m_pId)
				m_KeyboardFocus = {};
		}
		if(m_pTrackedTextPopupId != nullptr && std::any_of(PopupMenuToClose, m_vPopupMenus.end(), [this](const SPopupMenu &Popup) {
			   return Popup.m_pId == m_pTrackedTextPopupId;
		   }))
			ReleaseTrackedTextInput();
		// 带出场动画的弹窗（仅二级界面大弹窗启用）：后代立即移除，自身标记
		// closing 播放收缩渐隐后由渲染循环移除；逻辑关闭（输入解除、关闭回调）
		// 在标记瞬间完成，与立即关闭语义一致。
		if(!IncludeDescendants && PopupMenuToClose->m_Props.m_Animate)
		{
			m_vPopupMenus.erase(PopupMenuToClose + 1, m_vPopupMenus.end());
			// 出场由引擎 ALPHA 轨道接管：目标翻转为 0，MERGE_TARGET 从当前值
			// 带速度继续（中途关闭不再需要等效进度换算，旧数学已删）。
			PopupMenuToClose->m_Closing = true;
			SetActiveItem(nullptr);
			if(RestoreFocus)
				QueuePopupFocusRestore(ReturnFocus);
			for(const auto &Callback : vClosedCallbacks)
				Callback();
			if(m_pfnPopupMenuClosedCallback)
				m_pfnPopupMenuClosedCallback();
			return;
		}
		if(IncludeDescendants)
			m_vPopupMenus.erase(PopupMenuToClose, m_vPopupMenus.end());
		else
		{
			// 关闭弹窗时，栈存其上方的子弹窗必须一并结束：子弹窗的来源渲染
			// 多半就是被关弹窗，遗留成孤儿阻断弹窗会永久锁死底层指针输入。
			// 带出场动画的子弹窗转为收缩渐隐，由渲染循环自然移除。
			for(auto It = PopupMenuToClose + 1; It != m_vPopupMenus.end(); ++It)
			{
				if(It->m_Closing || !It->m_Props.m_Animate)
					continue;
				It->m_Closing = true;
			}
			m_vPopupMenus.erase(
				std::remove_if(PopupMenuToClose, m_vPopupMenus.end(), [](const SPopupMenu &PopupMenu) { return !PopupMenu.m_Closing; }),
				m_vPopupMenus.end());
		}
		SetActiveItem(nullptr);
		if(RestoreFocus)
			QueuePopupFocusRestore(ReturnFocus);
		for(const auto &Callback : vClosedCallbacks)
			Callback();
		if(m_pfnPopupMenuClosedCallback)
			m_pfnPopupMenuClosedCallback();
	}
}

void CUi::RefreshInteractionSource(const void *pContext, const char *pCardId)
{
	if(RenderOnly())
		return;
	const uint64_t Frame = PopupSourceFrame();
	for(SPopupMenu &Popup : m_vPopupMenus)
		Popup.m_Source.Refresh(pContext, pCardId, Frame);
	m_TextInputSource.Refresh(pContext, pCardId, Frame);
	for(const auto &Pending : m_vPendingSelections)
		if(const auto pCommit = Pending.lock())
			pCommit->m_Source.Refresh(pContext, pCardId, Frame);
}

void CUi::RegisterSelectionCommit(std::shared_ptr<SQmUiSelectionCommit> &pCommit)
{
	if(m_pInteractionContext == nullptr || m_pInteractionCardId == nullptr || pCommit)
		return;
	pCommit = std::make_shared<SQmUiSelectionCommit>();
	pCommit->m_Source.Assign(m_pInteractionContext, m_pInteractionCardId, PopupSourceFrame());
	m_vPendingSelections.emplace_back(pCommit);
}

bool CUi::HasPendingInteractionSelection() const
{
	return std::any_of(m_vPendingSelections.begin(), m_vPendingSelections.end(), [this](const auto &Pending) {
		const auto pCommit = Pending.lock();
		return pCommit && !pCommit->m_Cancelled && pCommit->m_Source.Matches(m_pInteractionContext, m_pInteractionCardId);
	});
}

void CUi::CloseInteractionSource(const void *pContext, const char *pCardId)
{
	if(RenderOnly())
		return;
	for(const auto &Pending : m_vPendingSelections)
		if(const auto pCommit = Pending.lock(); pCommit && pCommit->m_Source.Matches(pContext, pCardId))
			pCommit->m_Cancelled = true;
	for(size_t Index = 0; Index < m_vPopupMenus.size();)
	{
		if(m_vPopupMenus[Index].m_Source.Matches(pContext, pCardId))
			ClosePopupMenu(m_vPopupMenus[Index].m_pId, true, false);
		else
			++Index;
	}
	if(m_TextInputSource.Matches(pContext, pCardId))
		ReleaseTrackedTextInput();
	if(m_KeyboardFocus.m_Source.Matches(pContext, pCardId))
		m_KeyboardFocus = {};
	if(m_PendingPopupFocus.m_Source.Matches(pContext, pCardId))
		m_PendingPopupFocus = {};
}

void CUi::PruneInteractionSources(bool AllowPreviousFrame)
{
	if(RenderOnly())
		return;
	const uint64_t Frame = PopupSourceFrame();
	m_vPendingSelections.erase(std::remove_if(m_vPendingSelections.begin(), m_vPendingSelections.end(), [Frame, AllowPreviousFrame](const auto &Pending) {
		const auto pCommit = Pending.lock();
		if(!pCommit)
			return true;
		if(pCommit->m_Source.Expired(Frame, AllowPreviousFrame))
			pCommit->m_Cancelled = true;
		return pCommit->m_Cancelled;
	}),
		m_vPendingSelections.end());
	for(size_t Index = 0; Index < m_vPopupMenus.size();)
	{
		if(m_vPopupMenus[Index].m_Source.Expired(Frame, AllowPreviousFrame))
			ClosePopupMenu(m_vPopupMenus[Index].m_pId, true, false);
		else
			++Index;
	}
	if(m_TextInputSource.Expired(Frame, AllowPreviousFrame))
		ReleaseTrackedTextInput();
}

void CUi::RefreshPopupMenuSource(const SPopupMenuId *pId, bool RequireRefresh, uint64_t Frame)
{
	if(!RenderOnly())
		QmRefreshPopupSource(m_vPopupMenus, pId, RequireRefresh, Frame);
}

bool CUi::CloseTopPopupMenu()
{
	if(RenderOnly())
		return false;
	const auto Top = std::find_if(m_vPopupMenus.rbegin(), m_vPopupMenus.rend(), [](const SPopupMenu &Popup) { return !Popup.m_Closing; });
	if(Top == m_vPopupMenus.rend())
		return false;
	const SPopupMenuId *pId = Top->m_pId;
	ClosePopupMenu(pId);
	return true;
}

void CUi::ClosePopupMenus(bool RestoreFocus)
{
	if(m_vPopupMenus.empty())
		return;

	if(m_pTrackedTextPopupId != nullptr)
		ReleaseTrackedTextInput();
	const SPopupFocus ReturnFocus = m_vPopupMenus.front().m_Closing ? SPopupFocus{} : m_vPopupMenus.front().m_ReturnFocus;
	std::vector<std::function<void()>> vClosedCallbacks;
	for(const auto &Popup : m_vPopupMenus)
	{
		if(!Popup.m_Closing && Popup.m_Props.m_OnClosed)
			vClosedCallbacks.push_back(Popup.m_Props.m_OnClosed);
	}
	m_vPopupMenus.clear();
	m_KeyboardFocus = {};
	SetActiveItem(nullptr);
	if(RestoreFocus)
		QueuePopupFocusRestore(ReturnFocus);
	else
		m_PendingPopupFocus = {};
	for(const auto &Callback : vClosedCallbacks)
		Callback();
	if(m_pfnPopupMenuClosedCallback)
		m_pfnPopupMenuClosedCallback();
}

bool CUi::IsPopupOpen() const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [](const SPopupMenu &PopupMenu) { return !PopupMenu.m_Closing; });
}

bool CUi::IsPopupOpen(const SPopupMenuId *pId) const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return !PopupMenu.m_Closing && PopupMenu.m_pId == pId; });
}

bool CUi::IsPopupVisible(const SPopupMenuId *pId) const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
}

bool CUi::IsPopupHovered() const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [this](const SPopupMenu PopupMenu) { return !PopupMenu.m_Closing && MouseHovered(&PopupMenu.m_Rect); });
}

void CUi::SetPopupMenuClosedCallback(FPopupMenuClosedCallback pfnCallback)
{
	m_pfnPopupMenuClosedCallback = std::move(pfnCallback);
}

void CUi::SMessagePopupContext::DefaultColor(ITextRender *pTextRender)
{
	m_TextColor = pTextRender->DefaultTextColor();
}

void CUi::SMessagePopupContext::ErrorColor()
{
	m_TextColor = ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f);
}

CUi::EPopupMenuFunctionResult CUi::PopupMessage(void *pContext, CUIRect View, bool Active)
{
	SMessagePopupContext *pMessagePopup = static_cast<SMessagePopupContext *>(pContext);
	CUi *pUI = pMessagePopup->m_pUI;

	pUI->TextRender()->TextColor(pMessagePopup->m_TextColor);
	pUI->TextRender()->Text(View.x, View.y, SMessagePopupContext::POPUP_FONT_SIZE, pMessagePopup->m_aMessage, View.w);
	pUI->TextRender()->TextColor(pUI->TextRender()->DefaultTextColor());

	return (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)) ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupMessage(float X, float Y, SMessagePopupContext *pContext)
{
	const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SMessagePopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	pContext->m_pUI = this;
	SPopupMenuProperties Props;
	Props.m_CenterInViewport = true;
	Props.m_BlockUnderlyingPointerInput = true;
	Props.m_BlockUnderlyingScroll = true;
	Props.m_Animate = true;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, TextHeight + 10.0f, pContext, PopupMessage, Props);
}

CUi::SConfirmPopupContext::SConfirmPopupContext()
{
	Reset();
}

void CUi::SConfirmPopupContext::Reset()
{
	m_Result = SConfirmPopupContext::UNSET;
}

void CUi::SConfirmPopupContext::YesNoButtons()
{
	str_copy(m_aPositiveButtonLabel, Localize("Yes"));
	str_copy(m_aNegativeButtonLabel, Localize("No"));
}

void CUi::ShowPopupConfirm(float X, float Y, SConfirmPopupContext *pContext, bool CenterInViewport)
{
	const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SConfirmPopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	const float PopupHeight = TextHeight + SConfirmPopupContext::POPUP_BUTTON_HEIGHT + SConfirmPopupContext::POPUP_BUTTON_SPACING + 10.0f;
	pContext->m_pUI = this;
	pContext->m_Result = SConfirmPopupContext::UNSET;
	SPopupMenuProperties Props;
	// 资源下载确认可锚定点击位置，居中确认仍沿用原有遮罩。
	Props.m_CenterInViewport = CenterInViewport;
	Props.m_BlockUnderlyingPointerInput = true;
	Props.m_BlockUnderlyingScroll = true;
	Props.m_Animate = true;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, PopupHeight, pContext, PopupConfirm, Props);
}

CUi::EPopupMenuFunctionResult CUi::PopupConfirm(void *pContext, CUIRect View, bool Active)
{
	SConfirmPopupContext *pConfirmPopup = static_cast<SConfirmPopupContext *>(pContext);
	CUi *pUI = pConfirmPopup->m_pUI;

	CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
	View.HSplitBottom(SConfirmPopupContext::POPUP_BUTTON_HEIGHT, &Label, &ButtonBar);
	ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, SConfirmPopupContext::POPUP_BUTTON_SPACING);

	pUI->TextRender()->Text(Label.x, Label.y, SConfirmPopupContext::POPUP_FONT_SIZE, pConfirmPopup->m_aMessage, Label.w);

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_CancelButton, pConfirmPopup->m_aNegativeButtonLabel, &CancelButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CANCELED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_ConfirmButton, pConfirmPopup->m_aPositiveButtonLabel, &ConfirmButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC) || (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CONFIRMED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::SSelectionPopupContext::SSelectionPopupContext() :
	SSelectionPopupContext(nullptr)
{
}

CUi::SSelectionPopupContext::SSelectionPopupContext(IAllocationResource *pSourceMemory) :
	m_Search(pSourceMemory)
{
	Reset();
}

void CUi::SSelectionPopupContext::Reset()
{
	m_pCommit.reset();
	m_pUI = nullptr;
	m_pScrollRegion = nullptr;
	m_Props = SPopupMenuProperties();
	m_aMessage[0] = '\0';
	m_pSelection = nullptr;
	m_SelectionIndex = -1;
	m_ActiveIndex = -1;
	m_vEntries.clear();
	m_vButtonContainers.clear();
	m_EntryHeight = 12.0f;
	m_EntryPadding = 0.0f;
	m_EntrySpacing = 5.0f;
	m_FontSize = 10.0f;
	m_MinimumFontSize = -1.0f;
	m_Width = 300.0f + (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	m_AlignmentHeight = -1.0f;
	m_ActiveEntryColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f);
	m_HoverEntryColor = std::nullopt;
	m_TransparentButtons = false;
	m_AnchorVisible = true;
	m_PopupVisible = true;
	m_BlockUnderlyingScroll = false;
	m_Scrollable = false;
	m_ScrollToActiveItem = false;
	m_MenuUiFirstWheelLogged = false;
	m_Viewport = {};
	m_PopupPolicy = {};
	m_SpecialFontRenderMode = false;
	m_FontFaceAvailabilityCheck = false;
	m_FontFamilySelection = false;
	m_Searchable = false;
	m_FocusSearch = false;
	m_SearchInput.Clear();
	m_Search.Reset();
}

CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)
{
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	SSelectionPopupContext *pSelectionPopup = static_cast<SSelectionPopupContext *>(pContext);
	CUi *pUI = pSelectionPopup->m_pUI;
	CScrollRegion *pScrollRegion = pSelectionPopup->m_pScrollRegion;
	if(pScrollRegion == nullptr)
	{
		log_error("ui", "Selection popup opened without a scroll region");
		return CUi::POPUP_CLOSE_CURRENT;
	}
	if(pSelectionPopup->m_Searchable)
	{
		CUIRect SearchRect;
		View.HSplitTop(pSelectionPopup->m_EntryHeight, &SearchRect, &View);
		View.HSplitTop(pSelectionPopup->m_EntrySpacing, nullptr, &View);
		pSelectionPopup->m_SearchInput.SetEmptyText(Localize("Search"));
		SEditBoxRenderOptions SearchOptions;
		SearchOptions.m_ReleaseFocusOnEnter = false;
		pUI->DoClearableEditBox(&pSelectionPopup->m_SearchInput, &SearchRect, pSelectionPopup->m_FontSize, IGraphics::CORNER_ALL, {}, SearchOptions);
		if(Active && pSelectionPopup->m_FocusSearch)
		{
			pUI->SetActiveItem(&pSelectionPopup->m_SearchInput);
			pSelectionPopup->m_SearchInput.Activate(EInputPriority::UI);
			pUI->TrackActiveTextInput(&pSelectionPopup->m_SearchInput);
			pSelectionPopup->m_FocusSearch = false;
		}
	}
	const auto SearchResult = pSelectionPopup->m_Search.TryUpdate(pSelectionPopup->m_vEntries, pSelectionPopup->m_Searchable ? pSelectionPopup->m_SearchInput.GetString() : "");
	if(SearchResult == CQmDropdownSearch::EUpdateResult::FAILED)
		return CUi::POPUP_CLOSE_CURRENT;
	if(SearchResult == CQmDropdownSearch::EUpdateResult::UPDATED && pSelectionPopup->m_Searchable)
	{
		pSelectionPopup->m_ActiveIndex = pSelectionPopup->m_Search.Indices().empty() ? -1 : 0;
		pSelectionPopup->m_ScrollToActiveItem = true;
	}
	const auto &vVisible = pSelectionPopup->m_Search.Indices();

	// 键盘输入属于栈顶子层，父菜单失活时不能停止导航或让父层抢走按键。
	SQmDropdownInput KeyboardInput;
	KeyboardInput.m_KeepOpenWhenEmpty = pSelectionPopup->m_Searchable;
	if(Active)
	{
		// IME 组合期间，方向键和回车属于候选词窗口。
		if(pUI->Input() == nullptr || !pUI->Input()->HasComposition())
		{
			KeyboardInput.m_KeyUp = pUI->ConsumeHotkey(HOTKEY_UP);
			KeyboardInput.m_KeyDown = pUI->ConsumeHotkey(HOTKEY_DOWN);
			KeyboardInput.m_KeyEnter = pUI->ConsumeHotkey(HOTKEY_ENTER);
		}
		KeyboardInput.m_KeyEscape = pUI->ConsumeHotkey(HOTKEY_ESCAPE);
	}
	const int PreviousActiveIndex = pSelectionPopup->m_ActiveIndex;
	const SQmDropdownUpdateResult KeyboardResult = QmUpdateDropdownPopupSelection(KeyboardInput, vVisible.size(), Active, pSelectionPopup->m_ActiveIndex);
	if(KeyboardResult.m_Selected)
	{
		pSelectionPopup->m_SelectionIndex = pSelectionPopup->m_Search.SourceIndex(KeyboardResult.m_SelectedIndex);
		pSelectionPopup->m_pSelection = &pSelectionPopup->m_vEntries[pSelectionPopup->m_SelectionIndex];
		pUI->RegisterSelectionCommit(pSelectionPopup->m_pCommit);
	}
	if(KeyboardResult.m_Closed)
		return CUi::POPUP_CLOSE_CURRENT;
	if(QmDropdownShouldRequestActiveScroll(true, PreviousActiveIndex, pSelectionPopup->m_ActiveIndex))
		pSelectionPopup->m_ScrollToActiveItem = true;

	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::POPUP_LIST;
	ScrollRequest.m_RowExtent = pSelectionPopup->m_EntryHeight + pSelectionPopup->m_EntrySpacing;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollParams.m_HideScrollbar = !pSelectionPopup->m_Scrollable;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_pWheelOwnerId = pSelectionPopup;
	ScrollParams.m_WheelOwnerPreRegistered = true;
	const float PopupOuterHeight = (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2.0f;
	const float MessageHeight = pUI->TextRender()->TextBoundingBox(pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, -1, pSelectionPopup->m_Width).m_H;
	const auto ListPolicy = QmResolveDropdownPopupPolicy(vVisible.size(), pSelectionPopup->m_EntryHeight, pSelectionPopup->m_EntrySpacing, pSelectionPopup->m_aMessage[0] != '\0', MessageHeight, PopupOuterHeight);
	pScrollRegion->SetContentHeightForNextFrame(std::max(0.0f, ListPolicy.m_ContentHeight - PopupOuterHeight));
	pScrollRegion->Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	CUIRect Slot;
	if(pSelectionPopup->m_aMessage[0] != '\0')
	{
		const STextBoundingBox TextBoundingBox = pUI->TextRender()->TextBoundingBox(pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, -1, pSelectionPopup->m_Width);
		View.HSplitTop(TextBoundingBox.m_H, &Slot, &View);
		if(pScrollRegion->AddRect(Slot))
		{
			pUI->TextRender()->Text(Slot.x, Slot.y, pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, Slot.w);
		}
	}

	pSelectionPopup->m_vButtonContainers.resize(pSelectionPopup->m_vEntries.size());

	size_t Index = 0;
	int VisibleEntries = 0;
	for(int SourceIndex : vVisible)
	{
		const auto &Entry = pSelectionPopup->m_vEntries[SourceIndex];
		if(pSelectionPopup->m_aMessage[0] != '\0' || Index != 0)
			View.HSplitTop(pSelectionPopup->m_EntrySpacing, nullptr, &View);
		View.HSplitTop(pSelectionPopup->m_EntryHeight, &Slot, &View);
		const bool ActiveEntry = pSelectionPopup->m_ActiveIndex == static_cast<int>(Index);
		if(pScrollRegion->AddRect(Slot, QmDropdownActiveItemShouldScrollIntoView(pSelectionPopup->m_ScrollToActiveItem, ActiveEntry)))
		{
			++VisibleEntries;
			// 字体预览只选择可见条目的临时面，不修改默认字体角色或清空共享图集。
			// QmClient: m_FontFaceAvailabilityCheck 供商店搜索弹层使用——条目是
			// 未安装的在线字体，只有预览面已加载的条目才切换，避免逐帧失败日志。
			if(pSelectionPopup->m_SpecialFontRenderMode)
			{
				std::string PreviewName = Entry;
				const bool FaceAvailable = pSelectionPopup->m_FontFamilySelection ? pUI->TextRender()->QmFontFamilyDefaultConfig(Entry.c_str(), PreviewName) : !pSelectionPopup->m_FontFaceAvailabilityCheck || pUI->TextRender()->QmHasCustomFace(Entry.c_str());
				pUI->TextRender()->SetFontPreviewFace(FaceAvailable ? PreviewName.c_str() : nullptr);
			}
			// 活动项与悬浮项使用同一种整行背景，避免左侧竖条与条目背景重叠。
			CButtonContainer *pButton = &pSelectionPopup->m_vButtonContainers[SourceIndex];
			const bool Hovered = pUI->HotItem() == pButton || pUI->CheckActiveItem(pButton);
			const std::optional<ColorRGBA> EntryColor = ActiveEntry ? std::optional<ColorRGBA>(pSelectionPopup->m_ActiveEntryColor) : Hovered ? pSelectionPopup->m_HoverEntryColor :
																			    std::nullopt;
			if(pUI->DoButton_PopupMenu(pButton, Entry.c_str(), &Slot, pSelectionPopup->m_FontSize, TEXTALIGN_ML, pSelectionPopup->m_EntryPadding, pSelectionPopup->m_TransparentButtons, true, EntryColor, pSelectionPopup->m_MinimumFontSize))
			{
				pSelectionPopup->m_pSelection = &Entry;
				pSelectionPopup->m_SelectionIndex = SourceIndex;
				pUI->RegisterSelectionCommit(pSelectionPopup->m_pCommit);
			}
		}
		++Index;
	}
	if(vVisible.empty())
		pUI->DoLabel(&View, Localize("No results"), pSelectionPopup->m_FontSize, TEXTALIGN_ML);
	// TClient
	if(pSelectionPopup->m_SpecialFontRenderMode)
		pUI->TextRender()->SetFontPreviewFace(nullptr);

	pScrollRegion->End();
	pSelectionPopup->m_ScrollToActiveItem = false;
	if(!pSelectionPopup->m_MenuUiFirstWheelLogged && pScrollRegion->WheelConsumedThisFrame())
	{
		pUI->m_MenuUiFirstWheelPerf = MenuUiPerfEnabled;
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "dropdown";
		MenuUiPerf.m_pOperation = "dropdown_first_wheel";
		MenuUiPerf.m_ItemsTotal = (int)pSelectionPopup->m_vEntries.size();
		MenuUiPerf.m_ItemsVisible = VisibleEntries;
		MenuUiPerf.m_ItemsProcessed = VisibleEntries;
		MenuUiPerf.m_ItemsSkipped = maximum(0, MenuUiPerf.m_ItemsTotal - VisibleEntries);
		MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0;
		QmLogMenuUiFramePerf(MenuUiPerf, pUI->Client());
		pSelectionPopup->m_MenuUiFirstWheelLogged = true;
	}

	return pSelectionPopup->m_pSelection == nullptr ? CUi::POPUP_KEEP_OPEN : CUi::POPUP_CLOSE_CURRENT;
}

void CUi::ShowPopupSelection(float X, float Y, SSelectionPopupContext *pContext)
{
	const bool HasMessage = pContext->m_aMessage[0] != '\0';
	const STextBoundingBox TextBoundingBox = TextRender()->TextBoundingBox(pContext->m_FontSize, pContext->m_aMessage, -1, pContext->m_Width);
	const float OuterHeight = (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	const float SearchHeight = pContext->m_Searchable ? pContext->m_EntryHeight + pContext->m_EntrySpacing : 0.0f;
	const size_t ItemCount = pContext->m_Searchable ? std::max<size_t>(1, pContext->m_Search.Indices().size()) : pContext->m_vEntries.size();
	pContext->m_PopupPolicy = QmResolveDropdownPopupPolicy(ItemCount, pContext->m_EntryHeight, pContext->m_EntrySpacing, HasMessage, TextBoundingBox.m_H, OuterHeight + SearchHeight);
	const float PopupHeight = pContext->m_PopupPolicy.m_PreferredHeight;
	if(pContext->m_Viewport.w <= 0.0f || pContext->m_Viewport.h <= 0.0f)
		pContext->m_Viewport = *Screen();
	const CUIRect &Viewport = pContext->m_Viewport;
	pContext->m_pUI = this;
	pContext->m_pSelection = nullptr;
	pContext->m_SelectionIndex = -1;
	pContext->m_Props.m_Corners = IGraphics::CORNER_ALL;
	// 对齐模式下记录锚点（触发按钮），让弹窗外框把按钮与列表包成一个整体。
	pContext->m_Props.m_HasAnchorSurface = pContext->m_AlignmentHeight >= 0.0f;
	pContext->m_Props.m_AnchorSurface = CUIRect{X, Y, pContext->m_Width, pContext->m_Props.m_HasAnchorSurface ? pContext->m_AlignmentHeight : 0.0f};
	// 阻断底层指针输入：点击弹窗（含触发按钮）以外区域时立即关闭弹窗。
	// 几何判定不可见的路径会在下方直接 ClosePopupMenu，阻断标志无副作用。
	pContext->m_Props.m_BlockUnderlyingPointerInput = true;
	// 动画资格（动效等级 / IClient 可用性）由 DoPopupMenu 入口统一裁决。
	pContext->m_Props.m_Animate = true;
	float PopupWidth = pContext->m_Width;
	float PopupHeightResolved = PopupHeight;
	if(pContext->m_AlignmentHeight >= 0.0f)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		CUIRect AnchorRect;
		AnchorRect.x = X;
		AnchorRect.y = Y;
		AnchorRect.w = pContext->m_Width;
		AnchorRect.h = pContext->m_AlignmentHeight;
		SQmDropdownGeometryConfig GeometryConfig;
		GeometryConfig.m_Width = pContext->m_Width;
		GeometryConfig.m_Height = PopupHeight;
		GeometryConfig.m_Margin = Margin;
		GeometryConfig.m_RowHeight = pContext->m_EntryHeight;
		GeometryConfig.m_RowSpacing = pContext->m_EntrySpacing;
		GeometryConfig.m_FixedHeight = QmDropdownFixedHeight(HasMessage, TextBoundingBox.m_H, OuterHeight + SearchHeight);
		GeometryConfig.m_LeadingRowSpacing = HasMessage ? pContext->m_EntrySpacing : 0.0f;
		// 外框包裹模式：弹层与锚点按钮严格对齐（宽度/左缘），渲染端的
		// HasAlignedAnchor 判定才能稳定成立，边框连成一个整体。
		GeometryConfig.m_AlignToAnchor = true;
		const SQmDropdownGeometryResult Geometry = QmComputeDropdownPopupGeometry(AnchorRect, Viewport, GeometryConfig);
		pContext->m_AnchorVisible = Geometry.m_AnchorVisible;
		pContext->m_PopupVisible = Geometry.m_PopupVisible;
		if(!pContext->m_AnchorVisible || !pContext->m_PopupVisible)
		{
			ClosePopupMenu(pContext);
			return;
		}
		X = Geometry.m_Rect.x;
		Y = Geometry.m_Rect.y;
		PopupWidth = Geometry.m_Rect.w;
		PopupHeightResolved = Geometry.m_Rect.h;
		pContext->m_Props.m_AutoReposition = false;
		pContext->m_Props.m_Corners = Geometry.m_PlacedBelow ? IGraphics::CORNER_B : IGraphics::CORNER_T;
	}
	const CUIRect PopupRect{X, Y, PopupWidth, PopupHeightResolved};
	const bool Scrollable = pContext->m_PopupVisible && QmDropdownPopupScrollable(pContext->m_PopupPolicy, PopupHeightResolved);
	const bool BlockUnderlying = QmDropdownPopupBlocksUnderlying(pContext->m_PopupVisible);
	RegisterWheelOwner(pContext, EUiWheelOwnerPriority::POPUP, PopupRect, BlockUnderlying);
	pContext->m_Scrollable = Scrollable;
	pContext->m_BlockUnderlyingScroll = BlockUnderlying;
	pContext->m_Props.m_ClipToViewport = true;
	pContext->m_Props.m_BlockUnderlyingScroll = BlockUnderlying;
	pContext->m_Props.m_Viewport = Viewport;
	DoPopupMenu(pContext, X, Y, PopupWidth, PopupHeightResolved, pContext, PopupSelection, pContext->m_Props);
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)
{
	DoConfigTooltip(&State.m_ButtonContainer, pRect, DropDownProps.m_pConfigValue, DropDownProps.m_pSecondConfigValue);
	// 防御：选中索引必须落在 [-1, Num) 才能安全用作 pStrs 下标。索引来源包括
	// g_Config 与跨帧 UI 状态，一旦被越界写破坏（2026-09-30 崩溃报告：按钮文字
	// lambda 捕获块在两次调用之间被栈上野写覆盖成 0x2D6D6C67），直接下标访问
	// 会野读崩溃。这里收敛到合法区间：越界正值按最后一项处理，非法负值按未选中处理。
	if(Num <= 0)
	{
		CurSelection = -1;
	}
	else if(CurSelection >= Num)
	{
		CurSelection = Num - 1;
	}
	else if(CurSelection < -1)
	{
		CurSelection = -1;
	}
	const float ResolvedFontSize = DropDownProps.m_FontSize > 0.0f ? DropDownProps.m_FontSize : m_DropDownFontSize > 0.0f ? m_DropDownFontSize :
																pRect->h * ms_FontmodHeight * 0.8f;
	if(RenderOnly())
	{
		if(BackgroundRender() && !DrawingSuppressed() && pRect != nullptr && pStrs != nullptr)
		{
			if(!State.m_Init)
			{
				State.m_UiElement.Init(this, -1);
				State.m_Init = true;
			}
			SMenuButtonProperties Props;
			Props.m_Enabled = DropDownProps.m_Enabled;
			Props.m_Checked = IsPopupOpen(&State.m_SelectionPopupContext) ? 1 : 0;
			Props.m_ShowDropDownIcon = true;
			Props.m_HintRequiresStringCheck = true;
			Props.m_HintCanChangePositionOrSize = true;
			Props.m_FontSize = ResolvedFontSize;
			Props.m_Color = DropDownProps.m_VisualStyle.m_TriggerColor;
			DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, [CurSelection, pStrs, Num] { return CurSelection >= 0 && CurSelection < Num ? pStrs[CurSelection] : ""; }, pRect, Props);
		}
		return CurSelection;
	}
	if(State.m_SelectionPopupContext.m_pCommit && State.m_SelectionPopupContext.m_pCommit->m_Cancelled)
	{
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
	}

	if(!State.m_Init || !State.m_pOwnedScrollRegion)
	{
		if(!State.m_Init)
			State.m_UiElement.Init(this, -1);
		State.m_pOwnedScrollRegion = std::make_shared<CScrollRegion>();
		State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion != nullptr ? State.m_SelectionPopupContext.m_pScrollRegion : State.m_pOwnedScrollRegion.get();
		State.m_SelectionPopupContext.m_pScrollRegion = State.m_pScrollRegion;
		State.m_Init = true;
	}
	else if(State.m_SelectionPopupContext.m_pScrollRegion != nullptr && State.m_SelectionPopupContext.m_pScrollRegion != State.m_pScrollRegion)
		State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion;

	bool PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);
	if((PopupOpen || State.m_SelectionPopupContext.m_SelectionIndex >= 0) &&
		!QmDropdownEntriesMatch(State.m_SelectionPopupContext.m_vEntries, pStrs, Num))
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.m_pSelection = nullptr;
		State.m_SelectionPopupContext.m_SelectionIndex = -1;
		State.m_SelectionPopupContext.m_ActiveIndex = -1;
		PopupOpen = false;
	}
	// 弹窗使用设置页最外层裁剪区，不能越过 Tab 或页面容器；卡片内容裁剪区
	// 只判断锚点是否仍完整可见，锚点滚出卡片后应关闭弹窗。
	const CUIRect Viewport = DropDownProps.m_pPopupViewport != nullptr ? *DropDownProps.m_pPopupViewport : IsClipped() ? *OutermostClipArea() :
															     *Screen();
	const CUIRect AnchorViewport = DropDownProps.m_pAnchorViewport != nullptr ? *DropDownProps.m_pAnchorViewport : IsClipped() ? *ClipArea() :
																     Viewport;
	const uint64_t SourceFrame = PopupSourceFrame();
	if(PopupOpen && !QmDropdownAnchorFullyVisible(*pRect, AnchorViewport))
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
		PopupOpen = false;
	}
	if(State.m_DropDownState.IsOpen() && !PopupOpen)
		State.m_DropDownState.Reset();

	const SQmDropdownVisualStyle &VisualStyle = DropDownProps.m_VisualStyle;
	const ColorRGBA TriggerColor = VisualStyle.m_TriggerColor;
	const auto LabelFunc = [CurSelection, pStrs]() {
		return CurSelection > -1 ? pStrs[CurSelection] : "";
	};
	if(!DropDownProps.m_Enabled)
	{
		if(DropDownProps.m_ClosePopupWhenDisabled)
		{
			if(State.m_DropDownState.Disable(PopupOpen))
				ClosePopupMenu(&State.m_SelectionPopupContext);
			State.m_SelectionPopupContext.m_SelectionIndex = -1;
			State.m_SelectionPopupContext.m_ActiveIndex = -1;
		}
		else if(QmDropdownShouldKeepPopupAliveWhenDisabled(PopupOpen, false))
		{
			State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = DropDownProps.m_RequireSourceRefresh;
			State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
			RefreshPopupMenuSource(&State.m_SelectionPopupContext, DropDownProps.m_RequireSourceRefresh, SourceFrame);
			// 父触发器失活后不再经过 ShowPopupSelection，但活动子列表仍需逐帧注册滚轮归属。
			// 使用栈内实际矩形，不重开弹层或重置选择、滚动与动画状态。
			if(const CUIRect *pPopupRect = GetPopupMenuRect(&State.m_SelectionPopupContext))
				RegisterWheelOwner(&State.m_SelectionPopupContext, EUiWheelOwnerPriority::POPUP, *pPopupRect, State.m_SelectionPopupContext.m_BlockUnderlyingScroll);
		}
		SMenuButtonProperties ButtonProps;
		ButtonProps.m_Enabled = false;
		ButtonProps.m_HintRequiresStringCheck = true;
		ButtonProps.m_HintCanChangePositionOrSize = true;
		ButtonProps.m_ShowDropDownIcon = true;
		ButtonProps.m_FontSize = ResolvedFontSize;
		ButtonProps.m_Color = TriggerColor;
		DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, ButtonProps);
		return CurSelection;
	}

	SMenuButtonProperties Props;
	Props.m_HintRequiresStringCheck = true;
	Props.m_HintCanChangePositionOrSize = true;
	Props.m_ShowDropDownIcon = true;
	Props.m_Checked = PopupOpen ? 1 : 0;
	Props.m_Flags |= BUTTONFLAG_CURRENT_HIT;
	Props.m_FontSize = ResolvedFontSize;
	Props.m_Color = TriggerColor;
	if(PopupOpen)
	{
		State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = DropDownProps.m_RequireSourceRefresh;
		State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		Props.m_Corners = IGraphics::CORNER_ALL & (~State.m_SelectionPopupContext.m_Props.m_Corners);
	}
	const bool TogglePressed = DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, Props);

	SQmDropdownInput DropDownInput;
	DropDownInput.m_TogglePressed = TogglePressed;
	DropDownInput.m_InitialIndex = CurSelection;
	// 子弹层在自身回调中更新选择，父层只同步活动条目并处理触发按钮。
	if(PopupOpen)
		DropDownInput.m_HoveredIndex = State.m_SelectionPopupContext.m_ActiveIndex;
	const int PreviousActiveIndex = State.m_DropDownState.ActiveIndex();
	const SQmDropdownUpdateResult DropDownResult = State.m_DropDownState.Update(DropDownInput, Num);
	State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();
	if(QmDropdownShouldRequestActiveScroll(PopupOpen, PreviousActiveIndex, State.m_DropDownState.ActiveIndex()))
		State.m_SelectionPopupContext.m_ScrollToActiveItem = true;
	if(PopupOpen)
	{
		State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_Viewport = Viewport;
		State.m_SelectionPopupContext.m_Props.m_BorderColor = VisualStyle.m_PopupBorderColor;
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = VisualStyle.m_PopupBackgroundColor;
		State.m_SelectionPopupContext.m_ActiveEntryColor = DropDownProps.m_VisualStyle.m_ActiveEntryColor;
		State.m_SelectionPopupContext.m_TransparentButtons = DropDownProps.m_VisualStyle.m_TransparentEntries;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
		PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);
		if(State.m_DropDownState.IsOpen() && !PopupOpen)
			State.m_DropDownState.Reset();
	}
	if(DropDownResult.m_Opened)
	{
		CScrollRegion *pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion;
		const bool SpecialFontRenderMode = State.m_SelectionPopupContext.m_SpecialFontRenderMode;
		const bool FontFaceAvailabilityCheck = State.m_SelectionPopupContext.m_FontFaceAvailabilityCheck;
		const bool FontFamilySelection = State.m_SelectionPopupContext.m_FontFamilySelection;
		State.m_SelectionPopupContext.Reset();
		State.m_SelectionPopupContext.m_Props.m_ReturnFocus = true;
		State.m_SelectionPopupContext.m_pScrollRegion = pScrollRegion != nullptr ? pScrollRegion : State.m_pScrollRegion;
		State.m_SelectionPopupContext.m_SpecialFontRenderMode = SpecialFontRenderMode;
		State.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = FontFaceAvailabilityCheck;
		State.m_SelectionPopupContext.m_FontFamilySelection = FontFamilySelection;
		State.m_SelectionPopupContext.m_Searchable = DropDownProps.m_Searchable;
		State.m_SelectionPopupContext.m_FocusSearch = DropDownProps.m_Searchable;
		State.m_SelectionPopupContext.m_Props.m_BorderColor = VisualStyle.m_PopupBorderColor;
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = VisualStyle.m_PopupBackgroundColor;
		State.m_SelectionPopupContext.m_ActiveEntryColor = DropDownProps.m_VisualStyle.m_ActiveEntryColor;
		for(int i = 0; i < Num; ++i)
			State.m_SelectionPopupContext.m_vEntries.emplace_back(pStrs[i]);
		State.m_SelectionPopupContext.m_Search.Update(State.m_SelectionPopupContext.m_vEntries, State.m_SelectionPopupContext.m_SearchInput.GetString());
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_TransparentButtons = DropDownProps.m_VisualStyle.m_TransparentEntries;
		State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();
		State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = DropDownProps.m_RequireSourceRefresh;
		State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		State.m_SelectionPopupContext.m_ScrollToActiveItem = true;
		State.m_SelectionPopupContext.m_Viewport = Viewport;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
	}
	if(DropDownResult.m_Selected)
	{
		const int NewSelection = QmResolveDropdownSelection(CurSelection, DropDownResult.m_SelectedIndex, State.m_SelectionPopupContext.m_vEntries, pStrs, Num);
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_SelectionPopupContext.Reset();
		return NewSelection;
	}
	else if(DropDownResult.m_Closed)
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_SelectionPopupContext.Reset();
	}
	else if(State.m_SelectionPopupContext.m_SelectionIndex >= 0)
	{
		const int NewSelection = QmResolveDropdownSelection(CurSelection, State.m_SelectionPopupContext.m_SelectionIndex, State.m_SelectionPopupContext.m_vEntries, pStrs, Num);
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
		return NewSelection;
	}

	return CurSelection;
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, bool Enabled)
{
	SDropDownProperties DropDownProps;
	DropDownProps.m_Enabled = Enabled;
	DropDownProps.m_ClosePopupWhenDisabled = false;
	return DoDropDown(pRect, CurSelection, pStrs, Num, State, DropDownProps);
}

namespace
{
	// 取色器二级弹窗页面布局常量（逻辑像素，外层套 ui 缩放）。
	constexpr float PICKER_POPUP_WIDTH = 280.0f;
	constexpr float PICKER_POPUP_HEIGHT = 256.0f;
	constexpr float PICKER_TITLE_HEIGHT = 15.0f;
	constexpr float PICKER_TAB_HEIGHT = 15.0f;
	constexpr float PICKER_TAB_WIDTH = 36.0f;
	constexpr float PICKER_TAB_GAP = 2.0f;
	constexpr float PICKER_SHAPE_TOGGLE_WIDTH = 46.0f;
	constexpr float PICKER_SV_SQUARE_HEIGHT = 96.0f;
	constexpr float PICKER_WHEEL_SIZE = 112.0f;
	constexpr float PICKER_WHEEL_RING_WIDTH = 10.0f;
	constexpr float PICKER_HUE_BAR_WIDTH = 10.0f;
	constexpr float PICKER_ROW_HEIGHT = 13.0f;
	constexpr float PICKER_ROW_GAP = 2.5f;
	constexpr float PICKER_SLIDER_LABEL_WIDTH = 9.0f;
	constexpr float PICKER_SLIDER_TRACK_HEIGHT = 7.0f;
	constexpr float PICKER_VALUE_BOX_WIDTH = 30.0f;
	// Hex 行标签用“颜色代码”文案，需要容纳 4 个全角字（字号 7 时约 28u）。
	constexpr float PICKER_HEX_LABEL_WIDTH = 34.0f;
	constexpr float PICKER_PALETTE_COLGAP = 2.0f;
	constexpr float PICKER_PALETTE_ROWGAP = 2.0f;
	// 底部调色板行：紧凑单行（标签 + 12 格色块 + ＋），避免独立大卡片的空旷感。
	constexpr float PICKER_PALETTE_ROW_HEIGHT = 16.0f;
	constexpr float PICKER_PALETTE_SWATCH_SIZE = 14.0f;
	constexpr float PICKER_PALETTE_LABEL_WIDTH = 30.0f;
	constexpr int PICKER_PALETTE_COLS = 12;
	constexpr float PICKER_PREVIEW_BAR_HEIGHT = 12.0f;
	constexpr int PICKER_PRESET_COLS = 8;
	constexpr int PICKER_PRESET_ROWS = 3;
	constexpr float PICKER_DOUBLE_CLICK_INTERVAL = 0.4f;

	// 勾选图标的字体回退字形（U+E182，与 qm_icon_registry.inc 的 CHECK 一致）。
	constexpr const char *PICKER_ICON_CHECK = "\xEE\x86\x82";

	// SV 平面渐变：底层白→纯色相，上层透明→黑。方图与色环共用。
	void PickerDrawSvGradient(CUi *pUI, const CUIRect &Area, float Hue)
	{
		ColorRGBA TL, TR, BL, BR;
		TL = BL = color_cast<ColorRGBA>(ResolveUiColorPickerSelection(Hue, 0.0f, 0.0f, 1.0f));
		TR = BR = color_cast<ColorRGBA>(ResolveUiColorPickerSelection(Hue, 1.0f, 0.0f, 1.0f));
		Area.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

		TL = TR = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
		BL = BR = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
		Area.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);
	}

	// 水平色相谱条（红→黄→绿→青→蓝→品红→红），用于色相滑条轨道。
	void PickerDrawHueStrip(const CUIRect &Area)
	{
		static constexpr float s_aaHueStops[7][3] = {
			{1.0f, 0.0f, 0.0f}, // red
			{1.0f, 0.0f, 1.0f}, // magenta
			{0.0f, 0.0f, 1.0f}, // blue
			{0.0f, 1.0f, 1.0f}, // cyan
			{0.0f, 1.0f, 0.0f}, // green
			{1.0f, 1.0f, 0.0f}, // yellow
			{1.0f, 0.0f, 0.0f}, // red
		};
		const float StopWidth = Area.w / (std::size(s_aaHueStops) - 1);
		CUIRect StopArea;
		for(size_t j = 0; j < std::size(s_aaHueStops) - 1; j++)
		{
			StopArea.x = Area.x + StopWidth * j;
			StopArea.y = Area.y;
			StopArea.w = StopWidth + 0.5f; // 微重叠消除接缝
			StopArea.h = Area.h;
			const ColorRGBA Left(s_aaHueStops[j][0], s_aaHueStops[j][1], s_aaHueStops[j][2], 1.0f);
			const ColorRGBA Right(s_aaHueStops[j + 1][0], s_aaHueStops[j + 1][1], s_aaHueStops[j + 1][2], 1.0f);
			int Corners = IGraphics::CORNER_NONE;
			if(j == 0)
				Corners = IGraphics::CORNER_L;
			else if(j == std::size(s_aaHueStops) - 2)
				Corners = IGraphics::CORNER_R;
			StopArea.Draw4(Left, Right, Left, Right, Corners, 3.5f);
		}
	}

	// 通道滑条轨道渐变：返回 false 表示该通道用色相谱条（H 行）特殊绘制。
	// Mode/Channel 组合语义：HSVA 页 H 色相谱、S 灰→当前纯色、V 黑→当前纯色；
	// RGBA 页 R/G/B 黑→纯通道色；HSLA 页 H 色相谱、S 灰→纯色、L 黑→白；A 透明→当前色。
	bool PickerChannelTrackColors(int Mode, int Channel, const ColorHSVA &Hsv, const ColorRGBA &Rgb, ColorRGBA &Left, ColorRGBA &Right)
	{
		const ColorRGBA PureHue = color_cast<ColorRGBA>(ColorHSVA(Hsv.h, 1.0f, 1.0f, 1.0f));
		if(Mode == CUi::SColorPickerPopupContext::MODE_HSVA)
		{
			if(Channel == 0)
				return false; // H：色相谱条
			if(Channel == 1)
			{
				Left = ColorRGBA(0.5f, 0.5f, 0.5f, 1.0f); // S：灰 → 当前纯色
				Right = PureHue;
				return true;
			}
			if(Channel == 2)
			{
				Left = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f); // V：黑 → 当前纯色
				Right = PureHue;
				return true;
			}
		}
		else if(Mode == CUi::SColorPickerPopupContext::MODE_RGBA)
		{
			static constexpr float s_aaChannelHues[3][3] = {
				{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
			if(Channel < 3)
			{
				Left = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f); // R/G/B：黑 → 纯通道色
				Right = ColorRGBA(s_aaChannelHues[Channel][0], s_aaChannelHues[Channel][1], s_aaChannelHues[Channel][2], 1.0f);
				return true;
			}
		}
		else if(Mode == CUi::SColorPickerPopupContext::MODE_HSLA)
		{
			if(Channel == 0)
				return false; // H：色相谱条
			if(Channel == 1)
			{
				Left = ColorRGBA(0.5f, 0.5f, 0.5f, 1.0f); // S：灰 → 当前纯色（同亮度）
				Right = PureHue;
				return true;
			}
			if(Channel == 2)
			{
				// L：黑 → 当前纯色 → 白（三段，退化为左黑右白 + 中点由 Draw4 顶点近似）。
				Left = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
				Right = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
				return true;
			}
		}
		// A 行（各页共用）与兜底。
		Left = ColorRGBA(Rgb.r, Rgb.g, Rgb.b, 0.0f);
		Right = ColorRGBA(Rgb.r, Rgb.g, Rgb.b, 1.0f);
		return true;
	}

	// 色环：180 段纯色微段消除弦线锯齿与马赫带，红在正上方，顺时针色相递增；
	// 内外缘各一圈深色细描边收敛边缘。
	// 色环纹理：CPU 逐像素生成一次并缓存。freeform 分段扇形依赖渲染后端对
	// 四边形的三角化/剔除路径，在 Vulkan 后端下会产生鳞片状伪影且弦段渐变
	// 失真；逐像素纹理在任意后端下都平滑，边缘抗锯齿与描边直接烘焙。
	// 红在正上方，顺时针随色相递增（与 DoPickerLogic 命中映射一致）。
	IGraphics::CTextureHandle PickerWheelTexture(CUi *pUI)
	{
		static IGraphics::CTextureHandle s_WheelTexture;
		if(s_WheelTexture.IsValid())
			return s_WheelTexture;

		constexpr int Size = 256;
		CImageInfo Info;
		Info.m_Width = Size;
		Info.m_Height = Size;
		Info.m_Format = CImageInfo::FORMAT_RGBA;
		Info.m_pData = static_cast<uint8_t *>(malloc((size_t)Size * Size * 4));
		if(Info.m_pData == nullptr)
			return s_WheelTexture;

		const float Center = (Size - 1) * 0.5f;
		const float Outer = Size * 0.5f - 1.0f;
		// 环宽比例按半径归一，与命中检测的 WheelInner（PICKER_WHEEL_SIZE/2 - RING_WIDTH）一致。
		const float Inner = Outer * (1.0f - PICKER_WHEEL_RING_WIDTH / (PICKER_WHEEL_SIZE * 0.5f));
		constexpr float AaWidth = 1.5f;
		constexpr float OutlineDarkness = 0.55f;
		for(int y = 0; y < Size; y++)
		{
			for(int x = 0; x < Size; x++)
			{
				const float Dx = x - Center;
				const float Dy = y - Center;
				const float Dist = std::sqrt(Dx * Dx + Dy * Dy);
				// 屏幕坐标 y 向下：色相 0 在正上方、顺时针递增。
				float Angle = std::atan2(Dy, Dx) + (float)pi * 0.5f;
				if(Angle < 0.0f)
					Angle += 2.0f * (float)pi;
				const float Hue = Angle / (2.0f * (float)pi);
				ColorRGBA Color = color_cast<ColorRGBA>(ColorHSVA(Hue, 1.0f, 1.0f, 1.0f));
				// 内外缘抗锯齿。
				const float AlphaOuter = std::clamp((Outer - Dist) / AaWidth + 0.5f, 0.0f, 1.0f);
				const float AlphaInner = std::clamp((Dist - Inner) / AaWidth + 0.5f, 0.0f, 1.0f);
				const float Alpha = minimum(AlphaOuter, AlphaInner);
				// 内外缘 1px 深色描边烘焙。
				const float EdgeDist = minimum(Outer - Dist, Dist - Inner);
				const float Outline = 1.0f - std::clamp(1.0f - EdgeDist, 0.0f, 1.0f) * OutlineDarkness;
				Color = ColorRGBA(Color.r * Outline, Color.g * Outline, Color.b * Outline, Color.a * Alpha);
				uint8_t *pPixel = &Info.m_pData[(y * Size + x) * 4];
				pPixel[0] = (uint8_t)round_truncate(Color.r * 255.0f);
				pPixel[1] = (uint8_t)round_truncate(Color.g * 255.0f);
				pPixel[2] = (uint8_t)round_truncate(Color.b * 255.0f);
				pPixel[3] = (uint8_t)round_truncate(Color.a * 255.0f);
			}
		}

		s_WheelTexture = pUI->Graphics()->LoadTextureRawMove(Info, 0, "qm-picker-wheel");
		return s_WheelTexture;
	}

	// 色环：单张 CPU 纹理一次 quad 绘制，后端无关。
	void PickerDrawColorWheel(CUi *pUI, const CUIRect &Area)
	{
		IGraphics *pGraphics = pUI->Graphics();
		const IGraphics::CTextureHandle Texture = PickerWheelTexture(pUI);
		if(!Texture.IsValid())
			return;
		pGraphics->TextureSet(Texture);
		pGraphics->WrapClamp();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		const IGraphics::CQuadItem Item(Area.x, Area.y, Area.w, Area.h);
		pGraphics->QuadsDrawTL(&Item, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
	}

	// 推荐页预设色：8 色相 × 两档深浅 + 灰阶行 + 两个强调色。
	ColorHSVA PickerPresetColorHsv(int Index)
	{
		const int Row = Index / PICKER_PRESET_COLS;
		const int Col = Index % PICKER_PRESET_COLS;
		if(Row == 0)
			return ColorHSVA(Col / 8.0f, 1.0f, 1.0f, 1.0f);
		if(Row == 1)
			return ColorHSVA(Col / 8.0f, 0.65f, 0.90f, 1.0f);
		static constexpr ColorHSVA ROW3[8] = {
			ColorHSVA(0.0f, 0.0f, 0.00f, 1.0f),
			ColorHSVA(0.0f, 0.0f, 0.22f, 1.0f),
			ColorHSVA(0.0f, 0.0f, 0.45f, 1.0f),
			ColorHSVA(0.0f, 0.0f, 0.68f, 1.0f),
			ColorHSVA(0.0f, 0.0f, 0.86f, 1.0f),
			ColorHSVA(0.0f, 0.0f, 1.00f, 1.0f),
			ColorHSVA(0.55f, 0.75f, 0.96f, 1.0f),
			ColorHSVA(0.99f, 0.72f, 0.96f, 1.0f),
		};
		return ROW3[Col];
	}

	// 调色板工作副本 → 配置串（增删改名即刻回写，随 CFGFLAG_SAVE 持久化）。
	void PickerSavePalette(CUi::SColorPickerPopupContext *pPicker)
	{
		qm_picker_palette::Serialize(g_Config.m_QmColorPickerPalette, (int)sizeof(g_Config.m_QmColorPickerPalette), pPicker->m_vPalette);
	}

	// 绘制单个色块（圆角外框、透明棋盘格衬底、选中状态指示与高亮悬浮微浮起）。
	void PickerDrawSwatch(CUi *pUI, const CUIRect &Cell, const ColorRGBA &Color, bool CurrentMatch, bool Hovered, bool Pressed, float Radius = 2.5f)
	{
		CUIRect DrawCell = Cell;
		if(Hovered && !Pressed)
		{
			DrawCell.Margin(-1.0f, &DrawCell);
			const CUIRect Shadow{DrawCell.x, DrawCell.y + 0.8f, DrawCell.w, DrawCell.h};
			DrawRoundedSurface(pUI, Shadow, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), ColorRGBA(), Radius, 0.0f);
		}
		else if(Pressed)
		{
			DrawCell.Margin(0.5f, &DrawCell);
		}

		// 半透明颜色时绘制深色衬底
		if(Color.a < 0.99f)
			DrawRoundedSurface(pUI, DrawCell, ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f), ColorRGBA(), Radius);

		const float Lum = 0.299f * Color.r + 0.587f * Color.g + 0.114f * Color.b;
		ColorRGBA Outline;
		float OutlineWidth = 1.0f;
		if(CurrentMatch)
		{
			Outline = ui_token::color::ACCENT_PRIMARY;
			OutlineWidth = 2.0f;
		}
		else if(Hovered)
		{
			Outline = ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f);
			OutlineWidth = 1.5f;
		}
		else
		{
			Outline = Lum < 0.2f ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f);
			OutlineWidth = 1.0f;
		}

		DrawRoundedSurface(pUI, DrawCell, Color, Outline, Radius, OutlineWidth);

		// 当前选中颜色：中心显示高对比指示圆点，即便所选颜色与主题强调色接近也能清晰识别
		if(CurrentMatch)
		{
			const ColorRGBA PipColor = Lum > 0.5f ? ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f);
			const vec2 Center = DrawCell.Center();
			const float PipRadius = minimum(Radius, 2.0f);
			const CUIRect CenterPip{Center.x - PipRadius, Center.y - PipRadius, PipRadius * 2.0f, PipRadius * 2.0f};
			DrawRoundedSurface(pUI, CenterPip, PipColor, ColorRGBA(), PipRadius);
		}
	}
} // namespace

CUi::EPopupMenuFunctionResult CUi::PopupColorPicker(void *pContext, CUIRect View, bool Active)
{
	SColorPickerPopupContext *pColorPicker = static_cast<SColorPickerPopupContext *>(pContext);
	CUi *pUI = pColorPicker->m_pUI;
	pColorPicker->m_State = EEditState::NONE;

	// 弹窗背板（含高斯模糊）已在 RenderPopupMenus 中绘制完成，弹窗内容一律画在
	// 背板之上。若不抑制模糊，饱和度/明度渐变里透明→黑色的明度叠加层会因顶角
	// alpha < 1 触发 CUIRect::Draw4 的 DrawRectBackdrop，把已画好的色相底色覆盖
	// 成模糊背板，选择方块因此显示为灰度渐变、不随所选色相变化。
	CUiScopedGaussianBlurSuppression PickerBlurSuppression(pUI);

	// 弹窗背板沿用 RenderPopupMenus 的主题玻璃样式（与其他二级菜单一致），
	// 此处不再叠加自绘底色。

	// 输入框底板：弹窗玻璃背板与默认输入框外壳亮度接近，数值框几乎看不出边界。
	// 取色器内所有输入统一压一层深色底板 + 细描边，聚焦时切成强调色描边，
	// 与周围文字、轨道拉开层次，一眼可辨“这里能输入”。
	const auto DrawInputPlate = [&](const CUIRect &Rect, bool Focused) {
		const bool Hovered = !Focused && pUI->MouseHovered(&Rect);
		const ColorRGBA Fill = ColorRGBA(0.0f, 0.0f, 0.0f, Focused ? 0.38f : (Hovered ? 0.32f : 0.26f));
		const ColorRGBA Border = Focused ? ui_token::color::BORDER_FOCUS : ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 0.22f : 0.12f);
		DrawRoundedSurface(pUI, Rect, Fill, Border, ui_token::radius::TIGHT, Focused ? 1.4f : 1.0f);
	};
	// 文本/光标相对底板左右内缩，避免贴边；竖向仍由 DoEditBox 的 VSpacing 处理。
	const auto InputTextRect = [](const CUIRect &Plate, float Inset) {
		CUIRect Text = Plate;
		Text.x += Inset;
		Text.w -= Inset * 2.0f;
		return Text;
	};

	ColorHSVA PickerColorHSV = pColorPicker->m_HsvaColor;
	ColorRGBA PickerColorRGB = pColorPicker->m_RgbaColor;
	ColorHSLA PickerColorHSL = pColorPicker->m_HslaColor;
	const auto SyncFromHsv = [&]() {
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
	};
	const auto SyncFromRgb = [&]() {
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
		PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
	};
	const auto SyncFromHsl = [&]() {
		PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
	};

	// ===== 标题栏：颜色徽标 + 标题 + 确认/关闭 =====
	CUIRect TitleRow, Body;
	View.HSplitTop(PICKER_TITLE_HEIGHT, &TitleRow, &Body);
	CUIRect CloseButtonRect, AcceptButtonRect;
	TitleRow.VSplitRight(PICKER_TITLE_HEIGHT, &TitleRow, &CloseButtonRect);
	TitleRow.VSplitRight(PICKER_TITLE_HEIGHT + 2.0f, &TitleRow, &AcceptButtonRect);
	const bool CloseClicked = pUI->DoButton_QmIcon(&pColorPicker->m_CloseButton, EQmIcon::CLOSE, FontIcons::FONT_ICON_XMARK, 0, &CloseButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);
	const bool AcceptClicked = pUI->DoButton_QmIcon(&pColorPicker->m_AcceptButton, EQmIcon::CHECK, PICKER_ICON_CHECK, 0, &AcceptButtonRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL);

	CUIRect TitleChipRect, TitleRect;
	TitleRow.VSplitLeft(TitleRow.h, &TitleChipRect, &TitleRect);
	TitleRect.VSplitLeft(4.0f, nullptr, &TitleRect);
	// 颜色徽标左右分半：左旧色（快照）右新色（当前），改动一眼可见；点击左半直接还原初始颜色。
	CUIRect ChipOld, ChipNew;
	TitleChipRect.VSplitMid(&ChipOld, &ChipNew, 1.0f);
	if(pUI->DoButtonLogic(&pColorPicker->m_InitialColorButton, 0, &ChipOld, BUTTONFLAG_LEFT))
	{
		PickerColorHSV = pColorPicker->m_InitialHsvaColor;
		PickerColorRGB = pColorPicker->m_InitialRgbaColor;
		PickerColorHSL = pColorPicker->m_InitialHslaColor;
		pColorPicker->m_State = EEditState::ONE_GO;
	}
	DrawRoundedSurface(pUI, ChipOld, pColorPicker->m_InitialRgbaColor, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 2.0f, 1.0f, IGraphics::CORNER_L);
	DrawRoundedSurface(pUI, ChipNew, PickerColorRGB, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 2.0f, 1.0f, IGraphics::CORNER_R);
	pUI->DoSmallTooltip(&pColorPicker->m_InitialColorButton, &TitleChipRect, "左: 初始颜色（点击还原） · 右: 当前修改颜色");
	pUI->DoLabel(&TitleRect, "取色器", 8.0f, TEXTALIGN_ML);

	pUI->DoSmallTooltip(&pColorPicker->m_AcceptButton, &AcceptButtonRect, "确认并保留修改");
	pUI->DoSmallTooltip(&pColorPicker->m_CloseButton, &CloseButtonRect, "取消并还原初始颜色 (ESC)");

	if(AcceptClicked)
		return CUi::POPUP_CLOSE_CURRENT;
	if(CloseClicked || (Active && pColorPicker->m_RenamingPaletteIndex < 0 && pUI->ConsumeHotkey(HOTKEY_ESCAPE)))
	{
		// 取消：还原打开时的快照颜色后再关闭。
		pColorPicker->m_HsvaColor = pColorPicker->m_InitialHsvaColor;
		pColorPicker->m_RgbaColor = pColorPicker->m_InitialRgbaColor;
		pColorPicker->m_HslaColor = pColorPicker->m_InitialHslaColor;
		if(pColorPicker->m_pHslaColor != nullptr)
			*pColorPicker->m_pHslaColor = pColorPicker->m_HslaColor.Pack(pColorPicker->m_Alpha);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	// ===== 分割线 =====
	CUIRect Divider;
	Body.HSplitTop(2.0f, nullptr, &Body);
	Body.HSplitTop(1.0f, &Divider, &Body);
	Divider.Draw(ui_token::color::BORDER_SUBTLE, IGraphics::CORNER_NONE, 0.0f);
	Body.HSplitTop(3.0f, nullptr, &Body);

	// ===== 页签行：推荐/HSVA/RGBA/HSLA + 方图/色环切换 =====
	CUIRect TabRow, MainArea;
	Body.HSplitTop(PICKER_TAB_HEIGHT, &TabRow, &MainArea);
	MainArea.HSplitTop(3.0f, nullptr, &MainArea);

	static constexpr SColorPickerPopupContext::EColorPickerMode PICKER_MODES[] = {
		SColorPickerPopupContext::MODE_RECOMMENDED,
		SColorPickerPopupContext::MODE_HSVA,
		SColorPickerPopupContext::MODE_RGBA,
		SColorPickerPopupContext::MODE_HSLA,
	};
	static constexpr const char *PICKER_MODE_LABELS[] = {"推荐", "HSVA", "RGBA", "HSLA"};
	static constexpr const char *PICKER_MODE_TOOLTIPS[] = {
		"常用预设色板与当前颜色参数卡片",
		"色相 / 饱和度 / 明度取色模式",
		"红 / 绿 / 蓝通道取色模式",
		"色相 / 饱和度 / 亮度取色模式",
	};
	static_assert(std::size(PICKER_MODES) == std::size(PICKER_MODE_LABELS));
	static_assert(std::size(PICKER_MODES) == std::size(PICKER_MODE_TOOLTIPS));

	CUIRect ShapeToggleRect;
	TabRow.VSplitRight(PICKER_SHAPE_TOGGLE_WIDTH, &TabRow, &ShapeToggleRect);
	TabRow.VSplitRight(PICKER_TAB_GAP, &TabRow, nullptr);
	for(SColorPickerPopupContext::EColorPickerMode Mode : PICKER_MODES)
	{
		CUIRect TabButton;
		TabRow.VSplitLeft(PICKER_TAB_WIDTH, &TabButton, &TabRow);
		TabRow.VSplitLeft(PICKER_TAB_GAP, nullptr, &TabRow);
		const bool Selected = pColorPicker->m_ColorMode == Mode;
		if(pUI->DoButton_PopupMenu(&pColorPicker->m_aModeButtons[(int)Mode], PICKER_MODE_LABELS[Mode], &TabButton, 7.5f, TEXTALIGN_MC, 0.0f, true, !Selected))
		{
			pColorPicker->m_ColorMode = Mode;
		}
		pUI->DoSmallTooltip(&pColorPicker->m_aModeButtons[(int)Mode], &TabButton, PICKER_MODE_TOOLTIPS[Mode]);
		if(Selected)
		{
			// 选中页签底部强调色下划线，明确当前页。
			CUIRect TabIndicator;
			TabButton.HSplitBottom(2.0f, nullptr, &TabIndicator);
			TabIndicator.VMargin(6.0f, &TabIndicator);
			TabIndicator.Draw(ui_token::color::ACCENT_PRIMARY, IGraphics::CORNER_ALL, 1.0f);
		}
	}

	if(pColorPicker->m_ColorMode != SColorPickerPopupContext::MODE_RECOMMENDED)
	{
		if(pUI->DoButton_PopupMenu(&pColorPicker->m_ShapeButton, pColorPicker->m_Shape == SColorPickerPopupContext::SHAPE_WHEEL ? "色环" : "方图", &ShapeToggleRect, 7.5f, TEXTALIGN_MC, 0.0f, false))
		{
			pColorPicker->m_Shape = pColorPicker->m_Shape == SColorPickerPopupContext::SHAPE_WHEEL ? SColorPickerPopupContext::SHAPE_SQUARE : SColorPickerPopupContext::SHAPE_WHEEL;
			g_Config.m_QmColorPickerShape = (int)pColorPicker->m_Shape;
		}
		pUI->DoSmallTooltip(&pColorPicker->m_ShapeButton, &ShapeToggleRect, "切换取色区样式（方图 / 色环）");
	}

	// ===== 主区：取色/推荐页占满全宽，调色板收进底部行 =====
	CUIRect PaletteRow;
	MainArea.HSplitBottom(PICKER_PALETTE_ROW_HEIGHT, &MainArea, &PaletteRow);
	MainArea.HSplitBottom(4.0f, &MainArea, nullptr);
	CUIRect LeftCol = MainArea;

	// 调色板交互辅助。
	const auto StartRename = [&](int Index) {
		pColorPicker->m_RenamingPaletteIndex = Index;
		str_copy(pColorPicker->m_aRenameBuffer, pColorPicker->m_vPalette[Index].m_aName);
		pColorPicker->m_RenameInput.SetBuffer(pColorPicker->m_aRenameBuffer, sizeof(pColorPicker->m_aRenameBuffer));
		pColorPicker->m_RenameInput.Set(pColorPicker->m_aRenameBuffer);
		pColorPicker->m_RenameInput.SetCursorOffset(str_length(pColorPicker->m_aRenameBuffer));
		pUI->SetActiveItem(&pColorPicker->m_RenameInput);
	};
	const auto AddCurrentColorToPalette = [&]() {
		if((int)pColorPicker->m_vPalette.size() >= qm_picker_palette::MAX_SWATCHES)
			return;
		qm_picker_palette::SSwatch Swatch;
		str_format(Swatch.m_aName, sizeof(Swatch.m_aName), "颜色 %d", (int)pColorPicker->m_vPalette.size() + 1);
		Swatch.m_Color = PickerColorRGB;
		pColorPicker->m_vPalette.push_back(Swatch);
		PickerSavePalette(pColorPicker);
		StartRename((int)pColorPicker->m_vPalette.size() - 1);
	};

	if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_RECOMMENDED)
	{
		// 预览条：左半旧色（快照）右半新色（当前），一条横杠看差异；点击旧色恢复初始颜色。
		CUIRect PreviewRow;
		LeftCol.HSplitTop(PICKER_PREVIEW_BAR_HEIGHT, &PreviewRow, &LeftCol);
		LeftCol.HSplitTop(5.0f, nullptr, &LeftCol);
		CUIRect OldHalf, NewHalf;
		PreviewRow.VSplitMid(&OldHalf, &NewHalf);
		if(pUI->DoButtonLogic(&pColorPicker->m_InitialColorButton, 0, &OldHalf, BUTTONFLAG_LEFT))
		{
			PickerColorHSV = pColorPicker->m_InitialHsvaColor;
			PickerColorRGB = pColorPicker->m_InitialRgbaColor;
			PickerColorHSL = pColorPicker->m_InitialHslaColor;
			pColorPicker->m_State = EEditState::ONE_GO;
		}
		DrawRoundedSurface(pUI, OldHalf, pColorPicker->m_InitialRgbaColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), 3.0f, 1.0f, IGraphics::CORNER_L);
		DrawRoundedSurface(pUI, NewHalf, PickerColorRGB, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), 3.0f, 1.0f, IGraphics::CORNER_R);
		pUI->DoSmallTooltip(&pColorPicker->m_InitialColorButton, &OldHalf, "左侧: 初始颜色（点击可还原）");
		pUI->DoSmallTooltip(&pColorPicker->m_AcceptButton, &NewHalf, "右侧: 当前修改颜色");

		// 中缝指示旧/新边界。
		CUIRect Seam{PreviewRow.x + PreviewRow.w * 0.5f - 0.5f, PreviewRow.y, 1.0f, PreviewRow.h};
		Seam.Draw(ui_token::color::BORDER_SUBTLE, IGraphics::CORNER_NONE, 0.0f);

		// 预设网格 8×3。
		LeftCol.HSplitTop(2.0f, nullptr, &LeftCol);
		const float PresetSwatchSize = (LeftCol.w - (PICKER_PRESET_COLS - 1) * PICKER_PALETTE_COLGAP) / PICKER_PRESET_COLS;
		for(int Row = 0; Row < PICKER_PRESET_ROWS; Row++)
		{
			CUIRect RowRect;
			LeftCol.HSplitTop(PresetSwatchSize, &RowRect, &LeftCol);
			LeftCol.HSplitTop(PICKER_PALETTE_ROWGAP, nullptr, &LeftCol);
			CUIRect CellCursor = RowRect;
			for(int Col = 0; Col < PICKER_PRESET_COLS; Col++)
			{
				CUIRect Cell;
				CellCursor.VSplitLeft(PresetSwatchSize, &Cell, &CellCursor);
				CellCursor.VSplitLeft(PICKER_PALETTE_COLGAP, nullptr, &CellCursor);
				const int Index = Row * PICKER_PRESET_COLS + Col;
				const ColorHSVA PresetHsv = PickerPresetColorHsv(Index);
				const ColorRGBA PresetRgb = color_cast<ColorRGBA>(PresetHsv);

				const bool CurrentMatch = std::fabs(PresetRgb.r - PickerColorRGB.r) < 0.005f &&
							  std::fabs(PresetRgb.g - PickerColorRGB.g) < 0.005f &&
							  std::fabs(PresetRgb.b - PickerColorRGB.b) < 0.005f;

				const int Click = pUI->DoButtonLogic(&pColorPicker->m_aPresetButtons[Index], 0, &Cell, BUTTONFLAG_LEFT);
				const bool Hovered = (pUI->HotItem() == &pColorPicker->m_aPresetButtons[Index]) || pUI->MouseHovered(&Cell);
				const bool Pressed = pUI->CheckActiveItem(&pColorPicker->m_aPresetButtons[Index]);

				PickerDrawSwatch(pUI, Cell, PresetRgb, CurrentMatch, Hovered, Pressed, 3.0f);

				if(Hovered)
				{
					char aHexTip[32];
					str_format(aHexTip, sizeof(aHexTip), "#%06x\n点击应用颜色", PresetRgb.Pack(false));
					pUI->DoSmallTooltip(&pColorPicker->m_aPresetButtons[Index], &Cell, aHexTip);
				}

				if(Click == 1)
				{
					PickerColorHSV = PresetHsv;
					SyncFromHsv();
					pColorPicker->m_State = EEditState::ONE_GO;
				}
			}
		}

		// 当前颜色参数卡片
		LeftCol.HSplitTop(5.0f, nullptr, &LeftCol);
		if(LeftCol.h >= 24.0f)
		{
			CUIRect InfoCard;
			LeftCol.HSplitTop(minimum(LeftCol.h, 34.0f), &InfoCard, &LeftCol);
			DrawRoundedSurface(pUI, InfoCard, ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), ui_token::color::BORDER_SUBTLE, 3.0f, 1.0f);
			InfoCard.Margin(3.0f, &InfoCard);
			CUIRect HexRow, DetailRow;
			InfoCard.HSplitMid(&HexRow, &DetailRow);
			char aHexStr[32];
			str_format(aHexStr, sizeof(aHexStr), "HEX: #%06x", PickerColorRGB.Pack(false));
			pUI->DoLabel(&HexRow, aHexStr, 7.5f, TEXTALIGN_MC);
			char aDetailStr[64];
			str_format(aDetailStr, sizeof(aDetailStr), "RGB: %d, %d, %d   HSV: %d°, %d%%, %d%%",
				round_to_int(PickerColorRGB.r * 255.0f),
				round_to_int(PickerColorRGB.g * 255.0f),
				round_to_int(PickerColorRGB.b * 255.0f),
				round_to_int(PickerColorHSV.h * 360.0f),
				round_to_int(PickerColorHSV.s * 100.0f),
				round_to_int(PickerColorHSV.v * 100.0f));
			pUI->DoLabel(&DetailRow, aDetailStr, 6.0f, TEXTALIGN_MC);
		}
	}
	else
	{
		// ===== 取色页：主取色区（方图 / 色环）+ 通道数值行 + Hex =====
		CUIRect PickerZone, RowsZone;
		LeftCol.HSplitTop(pColorPicker->m_Shape == SColorPickerPopupContext::SHAPE_WHEEL ? PICKER_WHEEL_SIZE : PICKER_SV_SQUARE_HEIGHT, &PickerZone, &RowsZone);
		RowsZone.HSplitTop(PICKER_ROW_GAP, nullptr, &RowsZone);

		const ColorRGBA BorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
		CUIRect SvArea, HueArea;
		CUIRect SvHitArea, HueHitArea, WheelHitArea;
		vec2 WheelCenter(0.0f, 0.0f);
		float WheelOuter = 0.0f, WheelInner = 0.0f;
		const bool WheelShape = pColorPicker->m_Shape == SColorPickerPopupContext::SHAPE_WHEEL;

		if(WheelShape)
		{
			// 色环：水平居中；环内放 SV 方图。
			const float WheelX = PickerZone.x + (PickerZone.w - PICKER_WHEEL_SIZE) * 0.5f;
			const CUIRect WheelArea{WheelX, PickerZone.y, PICKER_WHEEL_SIZE, PICKER_WHEEL_SIZE};
			WheelHitArea = WheelArea;
			WheelCenter = WheelArea.Center();
			WheelOuter = PICKER_WHEEL_SIZE * 0.5f;
			WheelInner = WheelOuter - PICKER_WHEEL_RING_WIDTH;
			const float SvSide = WheelInner * 2.0f * 0.7071f * 0.88f;
			SvArea = CUIRect{WheelCenter.x - SvSide * 0.5f, WheelCenter.y - SvSide * 0.5f, SvSide, SvSide};
			SvHitArea = SvArea;
			PickerDrawColorWheel(pUI, WheelArea);

			// SV 方图背景卡片与微边框
			CUIRect SvBorder = SvArea;
			SvBorder.Margin(-1.0f, &SvBorder);
			DrawRoundedSurface(pUI, SvBorder, ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), 2.5f, 1.0f);
			SvArea.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_NONE, 0.0f);
			PickerDrawSvGradient(pUI, SvArea, PickerColorHSV.x);
		}
		else
		{
			// 方图：SV 平面 + 右侧竖向色相条（保留原有视觉）。
			PickerZone.VSplitRight(PICKER_HUE_BAR_WIDTH + PICKER_ROW_GAP, &SvArea, &HueHitArea);
			HueHitArea.VSplitLeft(PICKER_ROW_GAP, nullptr, &HueHitArea);
			HueArea = HueHitArea;
			SvHitArea = SvArea;
			SvArea.Draw(BorderColor, IGraphics::CORNER_NONE, 0.0f);
			SvArea.Margin(1.0f, &SvArea);
			HueArea.Draw(BorderColor, IGraphics::CORNER_NONE, 0.0f);
			HueArea.Margin(1.0f, &HueArea);
			PickerDrawSvGradient(pUI, SvArea, PickerColorHSV.x);

			static const float s_aaColorIndices[7][3] = {
				{1.0f, 0.0f, 0.0f}, // red
				{1.0f, 0.0f, 1.0f}, // magenta
				{0.0f, 0.0f, 1.0f}, // blue
				{0.0f, 1.0f, 1.0f}, // cyan
				{0.0f, 1.0f, 0.0f}, // green
				{1.0f, 1.0f, 0.0f}, // yellow
				{1.0f, 0.0f, 0.0f}, // red
			};
			const float HuePickerOffset = HueArea.h / 6.0f;
			CUIRect HuePartialArea = HueArea;
			HuePartialArea.h = HuePickerOffset;
			for(size_t j = 0; j < std::size(s_aaColorIndices) - 1; j++)
			{
				const ColorRGBA TL = ColorRGBA(s_aaColorIndices[j][0], s_aaColorIndices[j][1], s_aaColorIndices[j][2], 1.0f);
				const ColorRGBA BL = ColorRGBA(s_aaColorIndices[j + 1][0], s_aaColorIndices[j + 1][1], s_aaColorIndices[j + 1][2], 1.0f);
				HuePartialArea.y = HueArea.y + HuePickerOffset * j;
				HuePartialArea.Draw4(TL, TL, BL, BL, IGraphics::CORNER_NONE, 0.0f);
			}
		}

		// 主取色区交互。
		float PickerX, PickerY;
		if(!WheelShape)
		{
			const EEditState ColorPickerRes = pUI->DoPickerLogic(&pColorPicker->m_ColorPickerId, &SvHitArea, &PickerX, &PickerY);
			if(ColorPickerRes != EEditState::NONE)
			{
				const float ColorX = std::clamp(PickerX - (SvArea.x - SvHitArea.x), 0.0f, SvArea.w);
				const float ColorY = std::clamp(PickerY - (SvArea.y - SvHitArea.y), 0.0f, SvArea.h);
				PickerColorHSV = ResolveUiColorPickerSelection(PickerColorHSV.x, ColorX / std::max(SvArea.w, 1.0f), ColorY / std::max(SvArea.h, 1.0f), PickerColorHSV.a);
				SyncFromHsv();
				pColorPicker->m_State = ColorPickerRes;
			}

			const EEditState HuePickerRes = pUI->DoPickerLogic(&pColorPicker->m_HuePickerId, &HueHitArea, &PickerX, &PickerY);
			if(HuePickerRes != EEditState::NONE)
			{
				const float HueY = std::clamp(PickerY - (HueArea.y - HueHitArea.y), 0.0f, HueArea.h);
				PickerColorHSV.x = 1.0f - HueY / HueArea.h;
				SyncFromHsv();
				pColorPicker->m_State = HuePickerRes;
			}
		}
		else
		{
			// 1. 中间 SV 方图交互：独立使用 m_ColorPickerId 与 SvArea，支持自由拖拽并全向自动 clamp。
			const EEditState ColorPickerRes = pUI->DoPickerLogic(&pColorPicker->m_ColorPickerId, &SvArea, &PickerX, &PickerY);
			if(ColorPickerRes != EEditState::NONE)
			{
				PickerColorHSV = ResolveUiColorPickerSelection(
					PickerColorHSV.x,
					PickerX / std::max(SvArea.w, 1.0f),
					PickerY / std::max(SvArea.h, 1.0f),
					PickerColorHSV.a);
				SyncFromHsv();
				pColorPicker->m_State = ColorPickerRes;
			}

			// 2. 色环外环交互：独立使用 m_WheelPickerId，点击外环激活，拖拽时按环心极角丝滑调相。
			const void *pWheelId = &pColorPicker->m_WheelPickerId;
			const float MouseDx = pUI->MouseX() - WheelCenter.x;
			const float MouseDy = pUI->MouseY() - WheelCenter.y;
			const float MouseDist = std::sqrt(MouseDx * MouseDx + MouseDy * MouseDy);
			const bool OverRing = (MouseDist >= WheelInner - 2.0f && MouseDist <= WheelOuter + 3.0f);
			const bool InsideSv = pUI->MouseHovered(&SvArea);

			if(pUI->CheckActiveItem(pWheelId))
			{
				if(!pUI->MouseButton(0))
				{
					pUI->SetActiveItem(nullptr);
					pColorPicker->m_State = EEditState::END;
				}
				else
				{
					float Angle = std::atan2(MouseDy, MouseDx) + (float)pi * 0.5f;
					if(Angle < 0.0f)
						Angle += 2.0f * (float)pi;
					PickerColorHSV.x = std::clamp(Angle / (2.0f * (float)pi), 0.0f, 0.9999f);
					SyncFromHsv();
					pColorPicker->m_State = EEditState::EDITING;
				}
			}
			else if(OverRing && !InsideSv)
			{
				pUI->SetHotItem(pWheelId);
				if(pUI->MouseButtonClicked(0))
				{
					pUI->SetActiveItem(pWheelId);
					float Angle = std::atan2(MouseDy, MouseDx) + (float)pi * 0.5f;
					if(Angle < 0.0f)
						Angle += 2.0f * (float)pi;
					PickerColorHSV.x = std::clamp(Angle / (2.0f * (float)pi), 0.0f, 0.9999f);
					SyncFromHsv();
					pColorPicker->m_State = EEditState::START;
				}
			}
		}

		// ===== 通道滑条行：语义渐变轨道 + 圆形滑块 + 数值小输入框（拖动与输入双向）=====
		const unsigned OldA = round_to_int(PickerColorHSV.a * 255.0f);
		const auto DoChannelSliderRow = [&](int ChannelIndex, const char *pLabel, unsigned CurValue) -> SEditResult<int64_t> {
			CUIRect RowRect;
			RowsZone.HSplitTop(PICKER_ROW_HEIGHT, &RowRect, &RowsZone);
			RowsZone.HSplitTop(PICKER_ROW_GAP, nullptr, &RowsZone);

			CUIRect LabelRect, TrackRect, ValueRect;
			RowRect.VSplitLeft(PICKER_SLIDER_LABEL_WIDTH, &LabelRect, &TrackRect);
			TrackRect.VSplitRight(PICKER_VALUE_BOX_WIDTH, &TrackRect, &ValueRect);
			TrackRect.VSplitRight(4.0f, &TrackRect, nullptr);
			TrackRect.VSplitLeft(2.0f, nullptr, &TrackRect);

			pUI->DoLabel(&LabelRect, pLabel, 7.5f, TEXTALIGN_ML);

			// 语义渐变轨道（H 通道用色相谱条）；纵向居中于行高，横向铺满滑条宽度。
			// 轨道宽度即滑块圆心行程（见 qm_picker_slider），两端才能对齐。
			CUIRect TrackBar;
			TrackRect.VMargin((TrackRect.h - PICKER_SLIDER_TRACK_HEIGHT) * 0.5f, &TrackBar);
			TrackBar.h = PICKER_SLIDER_TRACK_HEIGHT;
			ColorRGBA TrackLeft, TrackRight;
			if(!PickerChannelTrackColors((int)pColorPicker->m_ColorMode, ChannelIndex, PickerColorHSV, PickerColorRGB, TrackLeft, TrackRight))
				PickerDrawHueStrip(TrackBar);
			else
				TrackBar.Draw4(TrackLeft, TrackRight, TrackLeft, TrackRight, IGraphics::CORNER_ALL, 3.5f);

			// 轨道外细描边增强质感
			DrawRoundedSurface(pUI, TrackBar, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), 3.5f, 1.0f);

			// 轨道交互：点击/拖动设值（PickerX 已经是相对 TrackRect 的局部坐标，
			// 与滑块圆心行程同源换算，滑块落点跟随鼠标）。
			float PickerX = 0.0f, PickerY = 0.0f;
			int64_t NewValue = CurValue;
			EEditState ResultState = EEditState::NONE;
			const EEditState TrackState = pUI->DoPickerLogic(&pColorPicker->m_aValueSelectorIds[ChannelIndex], &TrackRect, &PickerX, &PickerY);
			if(TrackState != EEditState::NONE)
			{
				NewValue = round_to_int(qm_picker_slider::ValueFromTrackOffset(TrackRect.w, PickerX) * 255.0f);
				ResultState = TrackState;
			}

			// 数值输入框：非编辑态回显当前值，编辑态实时提交合法数字。
			// 底板自绘（DoEditBox 关掉自带外壳），文字居中回显；
			// 聚焦时底板描边转强调色，与旁边的渐变轨道形成清晰的“拖动 / 输入”分工。
			CLineInput &Input = pColorPicker->m_aChannelInputs[ChannelIndex];
			if(!Input.IsActive())
			{
				str_format(pColorPicker->m_aacChannelBuffers[ChannelIndex], sizeof(pColorPicker->m_aacChannelBuffers[ChannelIndex]), "%u", CurValue);
				Input.Set(pColorPicker->m_aacChannelBuffers[ChannelIndex]);
			}
			const bool InputFocused = Input.IsActive();
			DrawInputPlate(ValueRect, InputFocused);
			const CUIRect ValueTextRect = InputTextRect(ValueRect, 2.5f);
			CUi::SEditBoxRenderOptions ValueRenderOptions;
			ValueRenderOptions.m_DrawBackground = false;
			// 命中区仍是整块底板：文本内缩只为排版留白，不该让边缘点击落空。
			ValueRenderOptions.m_pHitRect = &ValueRect;
			if(pUI->DoEditBox(&Input, &ValueTextRect, 7.0f, IGraphics::CORNER_ALL, {}, TEXTALIGN_MC, ValueRenderOptions))
			{
				const char *pText = pColorPicker->m_aacChannelBuffers[ChannelIndex];
				char *pEnd = nullptr;
				const long Parsed = strtol(pText, &pEnd, 10);
				if(pEnd != pText && *str_skip_whitespaces(pEnd) == '\0')
				{
					NewValue = std::clamp<long>(Parsed, 0, 255);
					ResultState = EEditState::ONE_GO;
				}
			}

			// 圆形滑块：立体阴影 + 纯白外圈 + 对应通道颜色内核；悬停或拖拽时高亮微放大。
			// 圆心行程两端各内缩基础半径，0 / 255 时滑块完整落在轨道内且贴住轨道端沿。
			const bool KnobHovered = pUI->MouseHovered(&TrackRect) || TrackState != EEditState::NONE;
			const float TrackT = std::clamp(NewValue / 255.0f, 0.0f, 1.0f);
			const float KnobX = TrackRect.x + qm_picker_slider::KnobCenterOffset(TrackRect.w, TrackT);
			const float KnobY = TrackRect.y + TrackRect.h * 0.5f;
			const float KnobR = KnobHovered ? qm_picker_slider::KNOB_HOVER_RADIUS : qm_picker_slider::KNOB_RADIUS;
			const CUIRect KnobShadow{KnobX - KnobR, KnobY - KnobR + 0.6f, KnobR * 2.0f, KnobR * 2.0f};
			DrawRoundedSurface(pUI, KnobShadow, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), ColorRGBA(), KnobR, 0.0f);
			const CUIRect Knob{KnobX - KnobR, KnobY - KnobR, KnobR * 2.0f, KnobR * 2.0f};
			DrawRoundedSurface(pUI, Knob, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), KnobR, 1.0f);
			const float InnerDotR = KnobR - 1.8f;
			const CUIRect InnerDot{KnobX - InnerDotR, KnobY - InnerDotR, InnerDotR * 2.0f, InnerDotR * 2.0f};
			InnerDot.Draw(PickerColorRGB, IGraphics::CORNER_ALL, InnerDotR);

			return SEditResult<int64_t>{ResultState, NewValue};
		};

		if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSVA)
		{
			const unsigned OldH = round_to_int(PickerColorHSV.h * 255.0f);
			const unsigned OldS = round_to_int(PickerColorHSV.s * 255.0f);
			const unsigned OldV = round_to_int(PickerColorHSV.v * 255.0f);

			const auto [StateH, H] = DoChannelSliderRow(0, "H", OldH);
			const auto [StateS, S] = DoChannelSliderRow(1, "S", OldS);
			const auto [StateV, V] = DoChannelSliderRow(2, "V", OldV);
			int64_t A = OldA;
			EEditState StateA = EEditState::NONE;
			if(pColorPicker->m_Alpha)
			{
				const auto [StateTmp, ValueTmp] = DoChannelSliderRow(3, "A", OldA);
				StateA = StateTmp;
				A = ValueTmp;
			}

			if(OldH != (unsigned)H || OldS != (unsigned)S || OldV != (unsigned)V || OldA != (unsigned)A)
			{
				PickerColorHSV = ColorHSVA(H / 255.0f, S / 255.0f, V / 255.0f, A / 255.0f);
				SyncFromHsv();
			}
			for(auto State : {StateH, StateS, StateV, StateA})
			{
				if(State != EEditState::NONE)
				{
					pColorPicker->m_State = State;
					break;
				}
			}
		}
		else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_RGBA)
		{
			const unsigned OldR = round_to_int(PickerColorRGB.r * 255.0f);
			const unsigned OldG = round_to_int(PickerColorRGB.g * 255.0f);
			const unsigned OldB = round_to_int(PickerColorRGB.b * 255.0f);

			const auto [StateR, R] = DoChannelSliderRow(0, "R", OldR);
			const auto [StateG, G] = DoChannelSliderRow(1, "G", OldG);
			const auto [StateB, B] = DoChannelSliderRow(2, "B", OldB);
			int64_t A = OldA;
			EEditState StateA = EEditState::NONE;
			if(pColorPicker->m_Alpha)
			{
				const auto [StateTmp, ValueTmp] = DoChannelSliderRow(3, "A", OldA);
				StateA = StateTmp;
				A = ValueTmp;
			}

			if(OldR != (unsigned)R || OldG != (unsigned)G || OldB != (unsigned)B || OldA != (unsigned)A)
			{
				PickerColorRGB = ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
				SyncFromRgb();
			}
			for(auto State : {StateR, StateG, StateB, StateA})
			{
				if(State != EEditState::NONE)
				{
					pColorPicker->m_State = State;
					break;
				}
			}
		}
		else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSLA)
		{
			const unsigned OldH = round_to_int(PickerColorHSL.h * 255.0f);
			const unsigned OldS = round_to_int(PickerColorHSL.s * 255.0f);
			const unsigned OldL = round_to_int(PickerColorHSL.l * 255.0f);

			const auto [StateH, H] = DoChannelSliderRow(0, "H", OldH);
			const auto [StateS, S] = DoChannelSliderRow(1, "S", OldS);
			const auto [StateL, L] = DoChannelSliderRow(2, "L", OldL);
			int64_t A = OldA;
			EEditState StateA = EEditState::NONE;
			if(pColorPicker->m_Alpha)
			{
				const auto [StateTmp, ValueTmp] = DoChannelSliderRow(3, "A", OldA);
				StateA = StateTmp;
				A = ValueTmp;
			}

			if(OldH != (unsigned)H || OldS != (unsigned)S || OldL != (unsigned)L || OldA != (unsigned)A)
			{
				PickerColorHSL = ColorHSLA(H / 255.0f, S / 255.0f, L / 255.0f, A / 255.0f);
				SyncFromHsl();
			}
			for(auto State : {StateH, StateS, StateL, StateA})
			{
				if(State != EEditState::NONE)
				{
					pColorPicker->m_State = State;
					break;
				}
			}
		}
		else
		{
			dbg_assert_failed("Color picker mode invalid: %d", (int)pColorPicker->m_ColorMode);
		}

		// Hex 行：常驻输入框，#RRGGBB[AA] 实时解析（所有取色模式共享）。
		// 标签用“颜色代码”文案（不加图标），输入框沿用同款深色底板。
		{
			CUIRect RowRect;
			RowsZone.HSplitTop(PICKER_ROW_HEIGHT, &RowRect, &RowsZone);
			RowsZone.HSplitTop(PICKER_ROW_GAP, nullptr, &RowsZone);
			CUIRect LabelRect, HexBox;
			RowRect.VSplitLeft(PICKER_HEX_LABEL_WIDTH, &LabelRect, &HexBox);
			pUI->DoLabel(&LabelRect, "颜色代码", 7.0f, TEXTALIGN_ML);

			const unsigned OldHex = PickerColorRGB.PackAlphaLast(pColorPicker->m_Alpha);
			if(!pColorPicker->m_HexInput.IsActive())
			{
				str_format(pColorPicker->m_aHexBuffer, sizeof(pColorPicker->m_aHexBuffer), pColorPicker->m_Alpha ? "#%08x" : "#%06x", OldHex);
				pColorPicker->m_HexInput.Set(pColorPicker->m_aHexBuffer);
			}
			const bool HexFocused = pColorPicker->m_HexInput.IsActive();
			DrawInputPlate(HexBox, HexFocused);
			const CUIRect HexTextRect = InputTextRect(HexBox, 3.0f);
			CUi::SEditBoxRenderOptions HexRenderOptions;
			HexRenderOptions.m_DrawBackground = false;
			HexRenderOptions.m_pHitRect = &HexBox;
			const bool HexChanged = pUI->DoEditBox(&pColorPicker->m_HexInput, &HexTextRect, 7.0f, IGraphics::CORNER_ALL, {}, TEXTALIGN_ML, HexRenderOptions);
			pUI->DoSmallTooltip(&pColorPicker->m_HexInput, &HexBox, "支持 3/4/6/8 位十六进制颜色（例如 #F00、#FF0000）");
			if(HexChanged)
			{
				ColorRGBA ParsedColor;
				if(qm_picker_palette::ParseHexColor(pColorPicker->m_aHexBuffer, ParsedColor))
				{
					const float OldAlphaFloat = PickerColorRGB.a;
					PickerColorRGB = ParsedColor;
					if(!pColorPicker->m_Alpha)
						PickerColorRGB.a = OldAlphaFloat;
					SyncFromRgb();
					pColorPicker->m_State = EEditState::ONE_GO;
				}
			}
		}

		// 标记点：SV 方图双环高对比度指针，暗底亮底均清晰可见
		const float MarkerX = SvArea.x + SvArea.w * PickerColorHSV.y;
		const float MarkerY = SvArea.y + SvArea.h * (1.0f - PickerColorHSV.z);
		const CUIRect ColorMarkerOuter{MarkerX - 4.5f, MarkerY - 4.5f, 9.0f, 9.0f};
		DrawRoundedSurface(pUI, ColorMarkerOuter, PickerColorRGB, ColorRGBA(0.0f, 0.0f, 0.0f, 0.75f), 4.5f, 1.8f);
		const CUIRect ColorMarkerInner{MarkerX - 3.5f, MarkerY - 3.5f, 7.0f, 7.0f};
		DrawRoundedSurface(pUI, ColorMarkerInner, PickerColorRGB, ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f), 3.5f, 1.0f);

		if(!WheelShape)
		{
			CUIRect HueMarker;
			HueArea.Margin(-2.5f, &HueMarker);
			HueMarker.h = 6.5f;
			HueMarker.y = (HueArea.y + HueArea.h * (1.0f - PickerColorHSV.x)) - HueMarker.h / 2.0f;

			const ColorRGBA HueMarkerColor = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f, 1.0f));
			const float HueMarkerOutlineColor = PickerColorHSV.x > 0.75f ? 1.0f : 0.0f;
			const ColorRGBA HueMarkerOutline = ColorRGBA(HueMarkerOutlineColor, HueMarkerOutlineColor, HueMarkerOutlineColor, 1.0f);
			DrawRoundedSurface(pUI, HueMarker, HueMarkerColor, HueMarkerOutline, 1.2f, 1.2f);
		}
		else
		{
			const float MarkerAngle = PickerColorHSV.x * 2.0f * (float)pi - (float)pi * 0.5f;
			const float MarkerRadius = (WheelOuter + WheelInner) * 0.5f;
			const vec2 WheelMarkerPos = WheelCenter + direction(MarkerAngle) * MarkerRadius;
			const float HandleR = (WheelOuter - WheelInner) * 0.5f;
			const CUIRect WheelMarkerShadow{WheelMarkerPos.x - HandleR, WheelMarkerPos.y - HandleR + 0.6f, HandleR * 2.0f, HandleR * 2.0f};
			DrawRoundedSurface(pUI, WheelMarkerShadow, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), ColorRGBA(), HandleR, 0.0f);
			const CUIRect WheelMarker{WheelMarkerPos.x - HandleR, WheelMarkerPos.y - HandleR, HandleR * 2.0f, HandleR * 2.0f};
			const ColorRGBA WheelMarkerColor = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f, 1.0f));
			DrawRoundedSurface(pUI, WheelMarker, WheelMarkerColor, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), HandleR, 1.8f);
		}
	}

	// ===== 底部：我的调色板行（所有页面常驻，带半透明底卡与悬停高光）=====
	{
		CUIRect PaletteCard = PaletteRow;
		PaletteCard.Margin(-2.0f, &PaletteCard);
		DrawRoundedSurface(pUI, PaletteCard, ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), 4.0f, 1.0f);

		CUIRect LabelRect, SwatchZone;
		PaletteRow.VSplitLeft(PICKER_PALETTE_LABEL_WIDTH, &LabelRect, &SwatchZone);
		SwatchZone.VSplitLeft(2.0f, nullptr, &SwatchZone);

		// 行首标签：永远固定显示“调色板”，彻底消除文本闪烁
		pUI->DoLabel(&LabelRect, "调色板", 7.0f, TEXTALIGN_ML);

		const int NumSwatches = (int)pColorPicker->m_vPalette.size();
		const bool HasAddCell = NumSwatches < qm_picker_palette::MAX_SWATCHES;

		// ＋ 固定当前颜色：紧随色块后排列，聚合度更高
		const float AddX = SwatchZone.x + NumSwatches * (PICKER_PALETTE_SWATCH_SIZE + PICKER_PALETTE_COLGAP);
		const CUIRect AddRect{AddX, SwatchZone.y, PICKER_PALETTE_SWATCH_SIZE, SwatchZone.h};
		const CUIRect SwatchArea = SwatchZone;

		if(pColorPicker->m_RenamingPaletteIndex < 0)
		{
			for(int i = 0; i < NumSwatches; i++)
			{
				const CUIRect Cell{SwatchArea.x + i * (PICKER_PALETTE_SWATCH_SIZE + PICKER_PALETTE_COLGAP), SwatchArea.y, PICKER_PALETTE_SWATCH_SIZE, SwatchArea.h};
				const qm_picker_palette::SSwatch &Entry = pColorPicker->m_vPalette[i];

				const bool CurrentMatch = std::fabs(Entry.m_Color.r - PickerColorRGB.r) < 0.002f &&
							  std::fabs(Entry.m_Color.g - PickerColorRGB.g) < 0.002f &&
							  std::fabs(Entry.m_Color.b - PickerColorRGB.b) < 0.002f &&
							  std::fabs(Entry.m_Color.a - PickerColorRGB.a) < 0.002f;

				const int Click = pUI->DoButtonLogic(&pColorPicker->m_aPaletteSwatchButtons[i], 0, &Cell, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				const bool Hovered = (pUI->HotItem() == &pColorPicker->m_aPaletteSwatchButtons[i]) || pUI->MouseHovered(&Cell);
				const bool Pressed = pUI->CheckActiveItem(&pColorPicker->m_aPaletteSwatchButtons[i]);

				PickerDrawSwatch(pUI, Cell, Entry.m_Color, CurrentMatch, Hovered, Pressed, 2.8f);

				if(Hovered)
				{
					char aTooltip[128];
					str_format(aTooltip, sizeof(aTooltip), "%s (#%06x)\n左键: 应用 · 双击: 重命名 · 右键: 删除",
						Entry.m_aName, Entry.m_Color.Pack(false));
					pUI->DoSmallTooltip(&pColorPicker->m_aPaletteSwatchButtons[i], &Cell, aTooltip);
				}

				if(Click == 1)
				{
					// 双击改名：两次快速左键点击同一色块。
					const double Now = pUI->Client()->LocalTime();
					if(pColorPicker->m_LastSwatchClickIndex == i && Now - pColorPicker->m_aSwatchClickTime[i] < PICKER_DOUBLE_CLICK_INTERVAL)
					{
						StartRename(i);
					}
					else
					{
						PickerColorRGB = Entry.m_Color;
						SyncFromRgb();
						pColorPicker->m_State = EEditState::ONE_GO;
					}
					pColorPicker->m_LastSwatchClickIndex = i;
					pColorPicker->m_aSwatchClickTime[i] = Now;
				}
				else if(Click == 2)
				{
					pColorPicker->m_vPalette.erase(pColorPicker->m_vPalette.begin() + i);
					pColorPicker->m_LastSwatchClickIndex = -1;
					PickerSavePalette(pColorPicker);
					break;
				}
			}

			// 色块与＋之后剩余区域的提示文本（常驻静态，绝不随悬停变化，杜绝闪烁）
			const float GuideX = AddX + (HasAddCell ? PICKER_PALETTE_SWATCH_SIZE + 6.0f : 2.0f);
			const float GuideWidth = (SwatchZone.x + SwatchZone.w) - GuideX;
			if(GuideWidth > 30.0f)
			{
				CUIRect GuideRect{GuideX, SwatchZone.y, GuideWidth, SwatchZone.h};
				if(NumSwatches == 0)
				{
					pUI->DoLabel(&GuideRect, "点击 ＋ 保存当前颜色", 6.5f, TEXTALIGN_ML);
				}
				else if(GuideWidth >= 70.0f)
				{
					pUI->DoLabel(&GuideRect, "双击改名 · 右键删除", 6.0f, TEXTALIGN_ML);
				}
			}
		}
		else
		{
			// 行内改名输入框：覆盖色块区整行，左侧附当前修改色块徽标
			const int Index = pColorPicker->m_RenamingPaletteIndex;
			if(Index >= 0 && Index < NumSwatches)
			{
				CUIRect ChipRect, InputRect;
				SwatchArea.VSplitLeft(PICKER_PALETTE_SWATCH_SIZE, &ChipRect, &InputRect);
				InputRect.VSplitLeft(3.0f, nullptr, &InputRect);
				DrawRoundedSurface(pUI, ChipRect, pColorPicker->m_vPalette[Index].m_Color, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), 2.5f, 1.0f);

				const bool WasActive = pColorPicker->m_RenameInput.IsActive();
				DrawInputPlate(InputRect, WasActive);
				const CUIRect RenameTextRect = InputTextRect(InputRect, 3.0f);
				CUi::SEditBoxRenderOptions RenameRenderOptions;
				RenameRenderOptions.m_DrawBackground = false;
				RenameRenderOptions.m_pHitRect = &InputRect;
				pUI->DoEditBox(&pColorPicker->m_RenameInput, &RenameTextRect, 7.5f, IGraphics::CORNER_ALL, {}, TEXTALIGN_ML, RenameRenderOptions);

				const bool SaveConfirmed = pUI->ConsumeHotkey(HOTKEY_ENTER) || (WasActive && !pColorPicker->m_RenameInput.IsActive());
				const bool Canceled = pUI->ConsumeHotkey(HOTKEY_ESCAPE);

				if(SaveConfirmed)
				{
					char aName[qm_picker_palette::MAX_NAME_BYTES];
					str_copy(aName, pColorPicker->m_aRenameBuffer, (int)sizeof(aName));
					qm_picker_palette::SanitizeName(aName, (int)sizeof(aName));
					if(aName[0] == '\0')
						str_format(aName, sizeof(aName), "颜色 %d", Index + 1);
					str_copy(pColorPicker->m_vPalette[Index].m_aName, aName);
					pColorPicker->m_RenamingPaletteIndex = -1;
					PickerSavePalette(pColorPicker);
				}
				else if(Canceled)
				{
					pColorPicker->m_RenamingPaletteIndex = -1;
				}
			}
			else
			{
				pColorPicker->m_RenamingPaletteIndex = -1;
			}
		}

		// ＋ 新增当前颜色
		if(HasAddCell && pColorPicker->m_RenamingPaletteIndex < 0)
		{
			if(pUI->DoButton_QmIcon(&pColorPicker->m_AddPaletteButton, EQmIcon::PLUS, FontIcons::FONT_ICON_PLUS, 0, &AddRect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL))
			{
				AddCurrentColorToPalette();
			}
			pUI->DoSmallTooltip(&pColorPicker->m_AddPaletteButton, &AddRect, "固定当前颜色到调色板（最多 12 项）");
		}
	}

	// ===== 回写 =====
	pColorPicker->m_HsvaColor = PickerColorHSV;
	pColorPicker->m_RgbaColor = PickerColorRGB;
	pColorPicker->m_HslaColor = PickerColorHSL;
	if(pColorPicker->m_pHslaColor != nullptr)
		*pColorPicker->m_pHslaColor = PickerColorHSL.Pack(pColorPicker->m_Alpha);

	return CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupColorPicker(float X, float Y, SColorPickerPopupContext *pContext)
{
	pContext->m_pUI = this;
	if(pContext->m_ColorMode == SColorPickerPopupContext::MODE_UNSET)
		pContext->m_ColorMode = SColorPickerPopupContext::MODE_HSVA;
	pContext->m_InitialHsvaColor = pContext->m_HsvaColor;
	pContext->m_InitialRgbaColor = pContext->m_RgbaColor;
	pContext->m_InitialHslaColor = pContext->m_HslaColor;
	pContext->m_vPalette = qm_picker_palette::Parse(g_Config.m_QmColorPickerPalette);
	pContext->m_RenamingPaletteIndex = -1;
	pContext->m_LastSwatchClickIndex = -1;
	pContext->m_Shape = (SColorPickerPopupContext::EColorPickerShape)std::clamp(g_Config.m_QmColorPickerShape, 0, 1);
	for(int i = 0; i < 4; i++)
	{
		pContext->m_aacChannelBuffers[i][0] = '\0';
		pContext->m_aChannelInputs[i].SetBuffer(pContext->m_aacChannelBuffers[i], (int)sizeof(pContext->m_aacChannelBuffers[i]));
	}
	pContext->m_aHexBuffer[0] = '\0';
	pContext->m_HexInput.SetBuffer(pContext->m_aHexBuffer, (int)sizeof(pContext->m_aHexBuffer));

	// 二级菜单弹窗页面：视口内居中，阻断下层滚动与指针交互。
	SPopupMenuProperties PopupProps;
	PopupProps.m_CenterInViewport = true;
	PopupProps.m_BlockUnderlyingPointerInput = true;
	PopupProps.m_BlockUnderlyingScroll = true;
	PopupProps.m_Animate = true;
	DoPopupMenu(pContext, X, Y, PICKER_POPUP_WIDTH, PICKER_POPUP_HEIGHT, pContext, PopupColorPicker, PopupProps);
}
