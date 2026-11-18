// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "UiButtons.h"

#include "QmAnimResolve.h"
#include "UiSurface.h"
#include "UiSurfaceText.h"

#include <game/client/qm_icon.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

namespace ui_widget
{
	namespace
	{
		// 禁用/警示斜线的双色：暗遮罩衬底 + 纯警示红。语义是「不可用」标记，
		// 与通知语义的 ui_token::color::DANGER（柔和珊瑚红）刻意区分。
		constexpr ColorRGBA SlashMaskColor{0.0f, 0.0f, 0.0f, 0.65f};
		constexpr ColorRGBA SlashWarnColor{1.0f, 0.0f, 0.0f, 1.0f};

		// 悬停、按下、选中三态共用这一条白色描边：状态差异只由填充深浅（0.16 → 0.30
		// 的反馈遮罩）和按下 0.97 缩放表达，描边不再随状态换色，也不另提一档 alpha。
		// 旧实现把表面自动前景色当描边色，默认选中色 #8FDDAD 提到 0.88 alpha 之后
		// 亮度超过阈值被判成 NEUTRAL_BLACK，于是 Shift 这类选中键按下时描出黑边，
		// 和悬停白边割裂。
		constexpr ColorRGBA ActiveBorderColor = ui_token::feedback::ACTIVE_BORDER;

		// 按钮描边的唯一出口：可用按钮进入悬停/按下/选中一律白色描边；禁用、透明
		// 未激活等状态仍由主题解析结果和调用处的覆盖决定。
		// 弹层条目（LIST_ENTRY）例外：它和列表胶囊行是同一个视觉件，选中/悬停只由填充
		// 表达、任何状态都不描边。ResolveUiButtonStyle 里已清过一次，这里必须同样早退，
		// 否则会把那次清空覆盖回白色描边——用户要的「选中 = 纯填充」就又会带上白边。
		ColorRGBA ResolveActiveButtonBorder(EUiButtonRole Role, const SUiSecondaryButtonStyle &Style, const SUiButtonState &State, bool ListEntryBorder = false)
		{
			if(Role == EUiButtonRole::LIST_ENTRY && !ListEntryBorder)
				return ColorRGBA();
			return State.m_Enabled && (State.m_Hovered || State.m_Selected) ? ActiveBorderColor : Style.m_Border;
		}

		// 按下缩放是按钮表面的唯一几何动画入口。分段按钮需要让填充、共用外框与
		// 中缝共用这一份矩形，否则只有被按住的半边缩进、外框仍停在原始尺寸。
		CUIRect ResolveButtonDrawRect(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, bool Enabled, bool Pressed, bool Suppress)
		{
			CUIRect DrawRect = Rect;
			if(Suppress || Ctx.m_pUi == nullptr || pId == nullptr || !Enabled)
				return DrawRect;
			CUiV2AnimationRuntime *pAnim = Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : Ctx.m_pUi->QmAnimationRuntime();
			if(pAnim == nullptr || Ctx.m_pUi->AnimationsBlocked())
				return DrawRect;
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
			// 悬浮反馈形态由 qm_ui_hover_style 决定：0 = 缩放，1 = 只有边框高亮（不缩）。
			const bool BorderHoverOnly = g_Config.m_QmUiHoverStyle != 0;
			const float TargetScale = BorderHoverOnly ? 1.0f : (Pressed ? 0.97f : 1.0f);
			const float Scale = std::clamp(ResolveUiAnimSpringValue(*pAnim, NodeKey, EUiAnimProperty::SCALE, TargetScale, ui_token::motion::TWITCH_SPRING, 2), 0.90f, 1.05f);
			if(std::abs(Scale - 1.0f) > 0.001f)
			{
				DrawRect.x += DrawRect.w * (1.0f - Scale) * 0.5f;
				DrawRect.y += DrawRect.h * (1.0f - Scale) * 0.5f;
				DrawRect.w *= Scale;
				DrawRect.h *= Scale;
			}
			return DrawRect;
		}
	}

	SSettingsActionGroupLayout PrepareSettingsActionGroup(CUi &Ui, SSettingsActionGroupState &State, const CUIRect &Row, const char *const *ppLabels, int Count, const SSettingsContentMetrics &Metrics, ESettingsActionGroupWidth WidthPolicy)
	{
		if(ppLabels == nullptr || Count <= 0 || Count > SSettingsChoiceLayout::MAX_SEGMENTS)
			return {};
		if(WidthPolicy == ESettingsActionGroupWidth::FILL)
			return ResolveSettingsActionGroupLayout(Row, Count, Metrics, 0.0f, WidthPolicy);
		const uint64_t FontRevision = Ui.TextRender()->GlyphAtlasRevision() * 31 + (uint64_t)Ui.TextRender()->GetFontPreset();
		const float Width = State.m_Widths.Resolve(ppLabels, Count, Metrics.m_BodySize, FontRevision, [&Ui](const char *pText, float Size) { return Ui.TextRender()->TextWidth(Size, pText); });
		return ResolveSettingsActionGroupLayout(Row, Count, Metrics, Width + 2.0f * ui_token::spacing::MD, WidthPolicy);
	}

	IUiContext ControlContext(CUi *pUi)
	{
		IUiContext Ctx;
		Ctx.m_pUi = pUi;
		Ctx.m_pAnim = pUi != nullptr ? pUi->QmAnimationRuntime() : nullptr;
		Ctx.m_pTheme = pUi != nullptr ? &pUi->QmControlTheme() : nullptr;
		Ctx.m_ScopeHash = MakeUiScopeHash("qm-controls");
		return Ctx;
	}

	ColorRGBA DrawButtonSurface(const IUiContext &Ctx, const void *pId, const CUIRect &Rect, const SButtonSurfaceOptions &Options)
	{
		if(Ctx.m_pUi == nullptr)
			return ColorRGBA();
		CUi *pUi = Ctx.m_pUi;
		const SUiTheme &Theme = Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : pUi->QmControlTheme();
		const ColorRGBA Backdrop = CUiScopedSurfaceText::CurrentSurface();
		SUiButtonState State;
		State.m_Enabled = Options.m_Enabled;
		State.m_Selected = Options.m_Selected;
		// 悬浮反馈走控件外观判定：内容 DRAW 回放保留高亮，纯背景趟与测量趟不出现反馈。
		// 不把 pId 当前提——卡片出图趟里 pId 会为了跳过动画被置空。
		// 需要自算几何的调用点用 Options.m_HoveredOverride 覆盖（见 SettingsCard 的色块按钮）。
		const CUIRect &HitRect = Options.m_pHitRect != nullptr ? *Options.m_pHitRect : Rect;
		State.m_Hovered = Options.m_HoveredOverride.value_or(Options.m_Enabled && pUi->Enabled() && pUi->VisualSurfaceHovered(&HitRect));
		State.m_Pressed = State.m_Hovered && pUi->CheckActiveItem(pId) && (pUi->MouseButton(0) || pUi->MouseButton(1) || pUi->MouseButton(2));
		const ColorRGBA Base = Options.m_Color.value_or(Options.m_TransparentInactive ? ColorRGBA() : ResolveConfiguredControlSurface());
		auto Style = ResolveUiButtonStyle(Options.m_Role, Base, Backdrop, Theme, State, Options.m_ListEntryBorder);
		// 描边统一在公共表面这一层收敛，调用点（设置卡片、下拉、页签、键位格等）
		// 不再各自决定按下态描边颜色。
		Style.m_Border = ResolveActiveButtonBorder(Options.m_Role, Style, State, Options.m_ListEntryBorder);
		if(Options.m_TransparentInactive && !State.m_Hovered && !State.m_Selected)
			Style.m_Border = ColorRGBA();
		// 图标按钮的描边只表达「悬浮/选中」：静止态留白，避免每个图标都套一圈常驻边框。
		if(Options.m_Role == EUiButtonRole::ICON && !State.m_Hovered && !State.m_Selected)
			Style.m_Border = ColorRGBA();
		if(Options.m_BorderOverride.has_value())
			Style.m_Border = *Options.m_BorderOverride;
		if(pUi->DrawingSuppressed())
			return Style.m_Fill;

		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(pUi);
		CUiV2AnimationRuntime *pAnim = Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : pUi->QmAnimationRuntime();
		if(pId != nullptr && pAnim != nullptr && Options.m_Enabled && !pUi->AnimationsBlocked())
		{
			const uint64_t NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash, reinterpret_cast<uint64_t>(pId));
			const SUiAnimTransition &Transition = State.m_Pressed ? ui_token::motion::BTN_PRESS : ui_token::motion::BTN_HOVER;
			Style.m_Fill = ResolveUiAnimValueColor(*pAnim, NodeKey, Style.m_Fill, Transition.m_DurationSec, Transition.m_Easing);
		}
		const CUIRect DrawRect = ResolveButtonDrawRect(Ctx, pId, Rect, Options.m_Enabled, State.m_Pressed, Options.m_SuppressGeometryAnimation);
		const ColorRGBA Fill = pUi->ScaleControlAlpha(Style.m_Fill);
		// 图标按钮在表面这一层统一成正圆：不管调用点传的是方角、圆角还是自算半径，
		// 形状只由「短边的一半」决定，避免同一个列表里一半圆一半方。
		float ShapeRadius = Options.m_Radius;
		int ShapeCorners = Options.m_Corners;
		if(Options.m_Role == EUiButtonRole::ICON)
		{
			// 只把半径抬到半高，Corners 保持调用点值，否则引擎按方块绘制。
			ShapeRadius = std::min(DrawRect.w, DrawRect.h) * 0.5f;
		}
		DrawRoundedSurface(Ctx, DrawRect, Fill, pUi->ScaleControlAlpha(Style.m_Border), ShapeRadius, Options.m_BorderWidth, ShapeCorners, Options.m_AlignToPixel);
		return Fill;
	}

	int DoIconButton(const IUiContext &Ctx, const void *pId, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect &Rect, unsigned Flags, int Corners, bool Enabled, std::optional<ColorRGBA> Color, bool ShowSlash, bool TransparentInactive, const CUIRect *pHitRect, const SIconButtonIconDraw *pIconDraw, std::optional<bool> HoveredOverride)
	{
		if(Ctx.m_pUi == nullptr || pId == nullptr || (Ctx.m_pUi->DrawingSuppressed() && !Ctx.m_pUi->PreLayoutInput()))
			return 0;
		CUi *pUi = Ctx.m_pUi;
		const CUIRect ButtonRect = QmUiSquareIconButtonRect(Rect);
		const CUIRect &HitRect = pHitRect != nullptr ? *pHitRect : ButtonRect;
		if(pUi->PreLayoutInput())
			return Enabled && Checked >= 0 ? pUi->DoButtonLogic(pId, Checked, &HitRect, Flags) : 0;
		SButtonSurfaceOptions Options;
		Options.m_Role = EUiButtonRole::ICON;
		// 图标按钮一律正圆：方块按钮按半高取半径，和「圆形图标按钮」的既有约定一致。
		Options.m_Radius = ButtonRect.h * 0.5f;
		Options.m_Enabled = Enabled && Checked >= 0;
		Options.m_Selected = Checked > 0;
		// 图标按钮恒为正圆：半径取半高；Corners 必须保留调用点的值——
		// 引擎的四角路径里 Corners 才是形状开关，清成 NONE 会直接画成方块。
		Options.m_Corners = Corners;
		Options.m_Color = Color;
		Options.m_TransparentInactive = TransparentInactive;
		Options.m_pHitRect = &HitRect;
		Options.m_HoveredOverride = HoveredOverride;
		const ColorRGBA SemanticColor = pUi->TextRender()->GetTextColor();
		const ColorRGBA Fill = DrawButtonSurface(Ctx, pId, ButtonRect, Options);
		CUiScopedSurfaceText SurfaceText(pUi->TextRender(), Fill);
		CUIRect Label;
		ButtonRect.Margin(std::min(2.0f, ButtonRect.h * 0.1f), &Label);
		ColorRGBA Foreground = ResolveUiSurfaceForeground(SurfaceText.Surface()).WithAlpha(pUi->TextRender()->GetTextColor().a);
		if(CQmIconSemanticColorScope::Active())
			Foreground = ResolveUiSurfaceIconColor(SurfaceText.Surface(), SemanticColor);
		if(!Options.m_Enabled)
			Foreground.a *= 0.65f;
		// 自定义图标与默认字形共用同一个 Box 和前景色：换绘制方式不该改变图标在按钮里
		// 的位置、大小或颜色，否则同一个按钮在静止态和过渡态之间会跳一下。
		if(pIconDraw != nullptr && pIconDraw->m_pfnDraw != nullptr)
			pIconDraw->m_pfnDraw(Ctx, Label, Foreground, pIconDraw->m_pUser);
		else
			pUi->DrawQmIcon(Label, Icon, pFallbackIcon, Foreground);
		if((ShowSlash || !Options.m_Enabled) && Label.w > 0.0f && Label.h > 0.0f)
		{
			// 禁用标记使用左上到右下的平头斜线，不依赖字体字形的方向和端点形状。
			const float Inset = Label.w * 0.16f;
			const vec2 Start(Label.x + Inset, Label.y + Inset);
			const vec2 End(Label.x + Label.w - Inset, Label.y + Label.h - Inset);
			const vec2 Normal = normalize(vec2(-(End.y - Start.y), End.x - Start.x));
			IGraphics *pGraphics = pUi->Graphics();
			pGraphics->TextureClear();
			pGraphics->QuadsBegin();
			const auto DrawSlash = [&](float Width, ColorRGBA SlashColor) {
				const vec2 Offset = Normal * Width * 0.5f;
				const IGraphics::CFreeformItem Quad(Start + Offset, Start - Offset, End + Offset, End - Offset);
				pGraphics->SetColor(SlashColor);
				pGraphics->QuadsDrawFreeform(&Quad, 1);
			};
			DrawSlash(Label.w * 0.18f, pUi->ScaleControlAlpha(SlashMaskColor));
			DrawSlash(Label.w * 0.10f, pUi->ScaleControlAlpha(SlashWarnColor));
			pGraphics->QuadsEnd();
		}
		return Options.m_Enabled && !pUi->RenderOnly() ? pUi->DoButtonLogic(pId, Checked, &HitRect, Flags) : 0;
	}

	namespace
	{
		bool DoStyledButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonRole Role, const int Corners = IGraphics::CORNER_ALL, const bool DrawBorder = true, const EUiButtonGeometry Geometry = EUiButtonGeometry::CONTROL, const CUIRect *pSurfaceRect = nullptr, bool Selected = false, const ColorRGBA *pSurfaceColor = nullptr, bool SuppressGeometryAnimation = false, bool Interactive = true)
		{
			if(Ctx.m_pUi == nullptr || pBtn == nullptr)
				return false;
			SButtonSurfaceOptions Options;
			Options.m_Role = Role;
			// 长条按钮（宽明显大于高）给胶囊端头；接近方形的按钮仍用小圆角，
			// 否则方按钮会被压成圆形。
			const CUIRect &SurfaceRect = pSurfaceRect != nullptr ? *pSurfaceRect : Rect;
			Options.m_Radius = ResolveUiSurfaceRadius(SurfaceRect, Geometry);
			if(!DrawBorder)
				Options.m_BorderWidth = 0.0f;
			Options.m_Corners = Corners;
			Options.m_Enabled = !Disabled;
			Options.m_Selected = Selected;
			Options.m_pHitRect = &Rect;
			// 底色语义由调用点显式给出时覆盖控制表面默认色（如运行/停止两段的语义色）。
			if(pSurfaceColor != nullptr)
				Options.m_Color = *pSurfaceColor;
			Options.m_SuppressGeometryAnimation = SuppressGeometryAnimation;
			const ColorRGBA Fill = DrawButtonSurface(Ctx, pBtn, pSurfaceRect != nullptr ? *pSurfaceRect : Rect, Options);
			CUiScopedSurfaceText SurfaceText(Ctx.m_pUi->TextRender(), Fill);
			if(Disabled)
				Ctx.m_pUi->TextRender()->TextColor(Ctx.m_pUi->TextRender()->GetTextColor().WithMultipliedAlpha(0.65f));
			// 按钮字号随视口缩放：写死 12 会让窄窗口下同一排按钮出现两种大小
			// （设置卡里走 Metrics 的控件会缩到 9.6，这里不会）。
			const float ButtonBodySize = ui_token::control::BodySizeFromScale(Ctx.m_UiScale);
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, Rect.w - 2.0f * ui_token::control::PadX(ButtonBodySize));
			Props.m_MinimumFontSize = std::min(ui_token::font::SMALL, ButtonBodySize);
			Props.m_EllipsisAtEnd = true;
			if(Props.m_MaxWidth > 0.0f && Rect.h > 0.0f)
				Ctx.m_pUi->DoLabel(&Rect, pText, ButtonBodySize, TEXTALIGN_MC, Props);
			if(Disabled && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->CheckActiveItem(pBtn))
				Ctx.m_pUi->SetActiveItem(nullptr);
			const bool Clicked = Interactive && !Disabled && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->DoButtonLogic(pBtn, 0, &Rect, BUTTONFLAG_LEFT) != 0;
			if(Clicked)
				Ctx.m_pUi->SetKeyboardFocus(pBtn);
			return Clicked;
		}
	}

	bool PrimaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonGeometry Geometry)
	{
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, EUiButtonRole::PRIMARY, IGraphics::CORNER_ALL, true, Geometry);
	}

	bool SecondaryButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pText, const CUIRect &Rect, bool Disabled, EUiButtonGeometry Geometry, bool Selected)
	{
		return DoStyledButton(Ctx, pBtn, pText, Rect, Disabled, EUiButtonRole::SECONDARY, IGraphics::CORNER_ALL, true, Geometry, nullptr, Selected);
	}

	int SplitActionButton(const IUiContext &Ctx, CButtonContainer *pLeft, const char *pLeftText, CButtonContainer *pRight, const char *pRightText, const CUIRect &Rect, bool LeftDisabled, bool RightDisabled, EUiButtonGeometry Geometry, const char *pLeftTooltip, const char *pRightTooltip, const SSplitActionOptions &Options)
	{
		if(Ctx.m_pUi == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return -1;
		// 右段宽度 = 目标宽度 × 进度：Progress=0 时整条只剩左段（就是单按钮形态），
		// 弹簧过冲时略超目标宽度，左段跟着被压窄，交接线因此会「弹」一下再回稳。
		const float Progress = std::clamp(Options.m_Progress, 0.0f, 1.2f);
		const float LeftTargetW = std::clamp(Options.m_LeftFraction, 0.0f, 1.0f) * Rect.w;
		// 插画贴在整条右端，不跟着右段走：分割线扫过去时它不动，视觉锚点固定在端头。
		const bool HasImage = Options.m_ImageIdle.IsValid();
		const float ImageWidth = HasImage ? std::min(Options.m_ImageWidth > 0.0f ? Options.m_ImageWidth : Rect.h * 4.0f, Rect.w) : 0.0f;
		const float RightWidth = std::max(0.0f, (Rect.w - LeftTargetW) * Progress);
		CUIRect Left, Right;
		Rect.VSplitLeft(Rect.w - RightWidth, &Left, &Right);
		const bool HasRight = RightWidth > 0.5f;
		// 右段在生长过半前只算「视觉效果」，不能接受鼠标或键盘操作：否则键盘可以
		// 在右段还是一个小色块时触发「停止服务器」，与鼠标的进度门不一致。
		const bool RightInteractive = HasRight && Progress >= 0.5f;
		const bool LeftHovered = !LeftDisabled && Ctx.m_pUi->VisualSurfaceHovered(&Left);
		const bool RightHovered = RightInteractive && !RightDisabled && Ctx.m_pUi->VisualSurfaceHovered(&Right);
		SUiButtonState BorderState;
		BorderState.m_Enabled = !(LeftDisabled && RightDisabled);
		BorderState.m_Hovered = LeftHovered || RightHovered;
		BorderState.m_Pressed = BorderState.m_Hovered && (Ctx.m_pUi->CheckActiveItem(pLeft) || Ctx.m_pUi->CheckActiveItem(pRight)) && (Ctx.m_pUi->MouseButton(0) || Ctx.m_pUi->MouseButton(1) || Ctx.m_pUi->MouseButton(2));
		// 填充、共用外框、中缝与插画都按同一个视觉矩形绘制：按下缩进时整条一起缩，
		// 不会出现两半底色内缩、外框还停在原始尺寸的错位。
		// 测量/纯背景趟不推进动画：与普通按钮的表面路径保持同一节奏，避免一帧内
		// 布局趟和绘制趟各推一次弹簧。
		const bool DrawingSuppressed = Ctx.m_pUi->DrawingSuppressed();
		const CUIRect SurfaceRect = DrawingSuppressed ? Rect : ResolveButtonDrawRect(Ctx, RightHovered ? pRight : pLeft, Rect, BorderState.m_Enabled, BorderState.m_Pressed, false);
		const float SurfaceScale = Rect.w > 0.0f ? SurfaceRect.w / Rect.w : 1.0f;
		CUIRect SurfaceLeft, SurfaceRight;
		SurfaceRect.VSplitLeft(SurfaceRect.w - std::clamp(RightWidth * SurfaceScale, 0.0f, SurfaceRect.w), &SurfaceLeft, &SurfaceRight);
		const void *apIds[] = {pLeft, pRight};
		const bool aEnabled[] = {!LeftDisabled, !RightDisabled};
		// 右段还没长出来时不能把它算进键盘组：否则 Tab/回车会命中一个看不见的按钮。
		const int KeyboardAction = Ctx.m_pUi->DoButtonGroupKeyboard(apIds, aEnabled, RightInteractive ? 2 : 1);
		// 两半只提交状态底色；外框在最后按整条按钮的合并状态统一提交一圈，
		// 悬浮/按下时不会只有被悬停的半边发亮、中缝断开。
		Ctx.m_pUi->ClipEnable(&SurfaceLeft);
		const bool LeftClicked = DoStyledButton(Ctx, pLeft, "", Left, LeftDisabled, EUiButtonRole::SECONDARY, IGraphics::CORNER_ALL, false, Geometry, &SurfaceRect, false, Options.m_LeftSurface ? &*Options.m_LeftSurface : nullptr, true);
		Ctx.m_pUi->ClipDisable();
		bool RightClicked = false;
		if(HasRight)
		{
			Ctx.m_pUi->ClipEnable(&SurfaceRight);
			RightClicked = DoStyledButton(Ctx, pRight, "", Right, RightDisabled, EUiButtonRole::SECONDARY, IGraphics::CORNER_ALL, false, Geometry, &SurfaceRect, false, Options.m_RightSurface ? &*Options.m_RightSurface : nullptr, true, RightInteractive);
			Ctx.m_pUi->ClipDisable();
			// 生长前半段右段还没成形，点上去多半是想点左段；进度过半才放行点击，
			// 避免启动服务器的瞬间误触「停止」。
			if(!RightInteractive)
			{
				RightClicked = false;
				if(Ctx.m_pUi->CheckActiveItem(pRight))
					Ctx.m_pUi->SetActiveItem(nullptr);
				if(Ctx.m_pUi->KeyboardFocusItem() == pRight)
					Ctx.m_pUi->SetKeyboardFocus(nullptr);
			}
		}
		// 中缝不是一上来就整条竖线：随进度从中心往上下长，配合右段的弹性扫入更像「长出来」。
		const float GrowInset = (1.0f - std::clamp(Progress, 0.0f, 1.0f)) * SurfaceRect.h * 0.34f;
		const float Inset = std::min(ui_token::spacing::XS, SurfaceRect.h * 0.25f) + GrowInset;
		const CUIRect Divider{SurfaceRight.x - 0.5f, SurfaceRect.y + Inset, 1.0f, std::max(0.0f, SurfaceRect.h - 2.0f * Inset)};
		if(!Ctx.m_pUi->DrawingSuppressed())
		{
			const SUiTheme &Theme = Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : Ctx.m_pUi->QmControlTheme();
			const auto BorderStyle = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, ResolveConfiguredControlSurface(), CUiScopedSurfaceText::CurrentSurface(), Theme, BorderState);
			const float Radius = ResolveUiSurfaceRadius(SurfaceRect, Geometry);
			// 静止时保留整条共用外框：双操作按钮和旁边的普通二级按钮必须同一条轮廓，
			// 外框只在悬停时换色、不能整条消失。悬停后按半裁剪叠加激活色，外侧端头仍
			// 沿用整条按钮的胶囊曲线，中缝一侧是直边，所以只有被悬停的半圈发亮。
			SUiButtonState RestState;
			RestState.m_Enabled = BorderState.m_Enabled;
			// 样式本身已按 BorderState 解析过边框色，静止外框必须用同一套解析重新取一次，
			// 否则会把悬停色当成静止色画在整条轮廓上。
			const auto RestStyle = ResolveUiButtonStyle(EUiButtonRole::SECONDARY, ResolveConfiguredControlSurface(), CUiScopedSurfaceText::CurrentSurface(), Theme, RestState);
			const ColorRGBA RestBorder = ResolveActiveButtonBorder(EUiButtonRole::SECONDARY, RestStyle, RestState);
			DrawRoundedSurface(Ctx, SurfaceRect, ColorRGBA(), Ctx.m_pUi->ScaleControlAlpha(RestBorder), Radius, ui_token::feedback::ICON_BORDER_WIDTH);
			if(LeftHovered || RightHovered)
			{
				const CUIRect &HoverHalf = RightHovered ? SurfaceRight : SurfaceLeft;
				const ColorRGBA HoverBorder = ResolveActiveButtonBorder(EUiButtonRole::SECONDARY, BorderStyle, BorderState);
				Ctx.m_pUi->ClipEnable(&HoverHalf);
				DrawRoundedSurface(Ctx, SurfaceRect, ColorRGBA(), Ctx.m_pUi->ScaleControlAlpha(HoverBorder), Radius, ui_token::feedback::ICON_BORDER_WIDTH);
				Ctx.m_pUi->ClipDisable();
			}
			if(HasRight)
			{
				// 中缝跟着被悬停的半边走强调色：左悬停时中线是左按钮的右边界，右悬停时是右按钮的左边界。
				const ColorRGBA DividerColor = (LeftHovered || RightHovered) ? Theme.m_Accent : ui_token::color::BORDER_SUBTLE;
				Divider.Draw(Ctx.m_pUi->ScaleBackgroundAlpha(DividerColor), IGraphics::CORNER_NONE, 0.0f);
			}
			// 插画：与带图菜单按钮同一套内收，胶囊端头是圆弧，贴图太靠边会被切到。
			if(HasImage)
			{
				const IGraphics::CTextureHandle &Texture = (LeftHovered || RightHovered) && Options.m_ImageHover.IsValid() ? Options.m_ImageHover : Options.m_ImageIdle;
				if(Texture.IsValid())
				{
					CUIRect Image{SurfaceRect.x + SurfaceRect.w - ImageWidth * SurfaceScale, SurfaceRect.y, ImageWidth * SurfaceScale, SurfaceRect.h};
					const float ImageInset = Image.h * 0.12f;
					Image.x -= ImageInset * 1.5f;
					Image.y += ImageInset;
					Image.h -= ImageInset * 2.0f;
					Image.w -= ImageInset;
					IGraphics *pGraphics = Ctx.m_pUi->Graphics();
					pGraphics->TextureSet(Texture);
					pGraphics->WrapClamp();
					pGraphics->QuadsBegin();
					pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
					IGraphics::CQuadItem QuadItem(Image.x, Image.y, Image.w, Image.h);
					pGraphics->QuadsDrawTL(&QuadItem, 1);
					pGraphics->QuadsEnd();
					pGraphics->WrapNormal();
				}
			}
		}
		// 文字自己画：半宽矩形会把可用宽度压小，DoLabel 会为了塞下而自动缩字号，
		// 于是双操作按钮的字比同一排其它按钮小一号。这里固定正文档，放不下就用省略号。
		if(!Ctx.m_pUi->DrawingSuppressed())
		{
			const float LabelSize = Options.m_LabelSize > 0.0f ? Options.m_LabelSize : ui_token::control::BodySizeFromScale(Ctx.m_UiScale);
			// 左段文字区：整条右端被插画占住的那块要扣掉，并随分割线左移一起收窄。
			CUIRect LeftLabel = Left;
			if(ImageWidth > 0.0f)
				LeftLabel.w = std::max(0.0f, std::min(Left.w, Rect.w - ImageWidth));
			SLabelProperties TextProps;
			TextProps.m_MaxWidth = std::max(0.0f, LeftLabel.w - 2.0f * ui_token::control::PadX(LabelSize));
			TextProps.m_MinimumFontSize = LabelSize;
			TextProps.m_EllipsisAtEnd = true;
			if(!LeftDisabled && pLeftText != nullptr && TextProps.m_MaxWidth > 0.0f)
				Ctx.m_pUi->DoLabel(&LeftLabel, pLeftText, LabelSize, TEXTALIGN_MC, TextProps);
			// 右段文字区扣掉插画宽度；右段还没长到放得下这行字时不画，
			// 否则生长途中会先闪一截省略号再变成全称。
			CUIRect RightLabel = Right;
			if(ImageWidth > 0.0f)
				RightLabel.w = std::max(0.0f, RightLabel.w - ImageWidth);
			const float RightPad = 2.0f * ui_token::control::PadX(LabelSize);
			TextProps.m_MaxWidth = std::max(0.0f, RightLabel.w - RightPad);
			const bool RightLabelFits = HasRight && !RightDisabled && pRightText != nullptr && TextProps.m_MaxWidth > 0.0f &&
						    Ctx.m_pUi->TextRender()->TextWidth(LabelSize, pRightText) <= TextProps.m_MaxWidth;
			if(RightLabelFits)
				Ctx.m_pUi->DoLabel(&RightLabel, pRightText, LabelSize, TEXTALIGN_MC, TextProps);
		}
		Ctx.m_pUi->DoButtonTooltip(pLeft, &Left, pLeftTooltip);
		if(RightInteractive)
			Ctx.m_pUi->DoButtonTooltip(pRight, &Right, pRightTooltip);
		if(!Ctx.m_pUi->DrawingSuppressed())
		{
			const void *pFocus = Ctx.m_pUi->KeyboardFocusItem();
			// 右段收起时不画它的焦点环，左段照常。
			// 焦点环只服务键盘导航；鼠标点击或弹层关闭恢复的焦点不再留下强调色框。
			if((RightInteractive || pFocus == pLeft) && Ctx.m_pUi->KeyboardFocusFromKeyboard() && ((pFocus == pLeft && !LeftDisabled) || (pFocus == pRight && !RightDisabled)))
			{
				CUIRect FocusRect = pFocus == pLeft ? Left : Right;
				FocusRect.Margin(std::min(2.0f, std::min(FocusRect.w, FocusRect.h) * 0.25f), &FocusRect);
				const SUiTheme &Theme = Ctx.m_pTheme != nullptr ? *Ctx.m_pTheme : Ctx.m_pUi->QmControlTheme();
				// 焦点环贴的是整条按钮的外缘（不是半边的边），半径取整条的解析结果，
				// 否则半边按自己的长宽比再选一次会得出半圆，勾在分割线上。
				DrawRoundedSurface(Ctx, FocusRect, ColorRGBA(), Theme.m_FocusRing, ResolveUiSurfaceRadius(Rect, Geometry), ui_token::feedback::ICON_BORDER_WIDTH);
			}
		}
		return LeftClicked ? 0 : RightClicked ? 1 :
							KeyboardAction;
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, const char *pIcon, const CUIRect &Rect, bool Disabled)
	{
		return DoIconButton(Ctx, pBtn, EQmIcon::COUNT, pIcon, 0, Rect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Disabled) != 0;
	}

	bool IconButton(const IUiContext &Ctx, CButtonContainer *pBtn, EQmIcon Icon, const char *pFallbackIcon, const CUIRect &Rect, bool Disabled)
	{
		return DoIconButton(Ctx, pBtn, Icon, pFallbackIcon, 0, Rect, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, !Disabled) != 0;
	}
}
