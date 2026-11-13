#ifndef GAME_CLIENT_QMUI_QMPIEMENUSETTINGSCONTROLS_H
#define GAME_CLIENT_QMUI_QMPIEMENUSETTINGSCONTROLS_H

#include "UiButtonStyle.h"
#include "UiSurface.h"
#include "UiTextRenderState.h"
#include "UiTokens.h"

#include <game/client/ui.h>

#include <new>
#include <stdexcept>

inline CUIRect QmPieOptionSwatchRect(const CUIRect &Cell, float LineHeight)
{
	const float Size = std::max(0.0f, std::min({LineHeight, Cell.w, Cell.h}));
	return {Cell.x + Cell.w - Size, Cell.y + (Cell.h - Size) * 0.5f, Size, Size};
}

inline CUIRect QmPieOptionButtonRect(CUIRect Cell, float LineHeight, float Spacing)
{
	Cell.w = std::max(0.0f, Cell.w - QmPieOptionSwatchRect(Cell, LineHeight).w - std::max(0.0f, Spacing));
	return Cell;
}

// 配色与文字按钮各自处理命中；后台保留色块绘制，预布局只处理输入。
// 色块外观和右侧选项按钮对齐：统一白描边把色块从按钮底上分出来，
// 悬浮时回到原色并提亮描边，不悬浮时整体压暗，指针停在哪一格一眼能看出。
inline bool QmDrawPieOptionColorControl(const IUiContext &Ctx, const void *pColorId, ColorRGBA Color, const CUIRect &Swatch, float CornerRadius, bool ProcessInput)
{
	if(Ctx.m_pUi == nullptr || (Ctx.m_pUi->DrawingSuppressed() && !Ctx.m_pUi->PreLayoutInput()))
		return false;
	const float MouseX = Ctx.m_pUi->MouseX();
	const float MouseY = Ctx.m_pUi->MouseY();
	const bool Hovered = MouseX >= Swatch.x && MouseY >= Swatch.y && MouseX < Swatch.x + Swatch.w && MouseY < Swatch.y + Swatch.h;
	// 只改明暗不改色相：色块仍然代表玩家挑的那个颜色。
	// 非悬浮用遮罩 token 压暗，悬浮提亮复用公共按钮反馈，不在这里写亮度倍数。
	const ColorRGBA Base = Hovered ? Color : BlendUiButtonSurface(Color, ui_token::color::PREVIEW_BACKDROP);
	const ColorRGBA Fill = Hovered ? BlendUiButtonSurface(Base, ResolveUiIconButtonFeedback(Base, true, true, false)) : Base;
	// 白描边始终存在，悬浮高亮体现在底色上，边框不跟着明暗跳。
	const ColorRGBA Border = ui_token::color::TEXT_PRIMARY.WithAlpha(ui_token::feedback::ICON_BORDER_ALPHA);
	DrawRoundedSurface(Ctx, Swatch, Fill, Border, CornerRadius, ui_token::feedback::ICON_BORDER_WIDTH);
	return ProcessInput && !Ctx.m_pUi->RenderOnly() && Ctx.m_pUi->DoButtonLogic(pColorId, 0, &Swatch, BUTTONFLAG_LEFT) != 0;
}

inline bool QmDrawPieSettingsPreviewIcon(CUi &Ui, const vec2 &Position, float IconSize, const char *pIcon, float Alpha)
{
	auto &TextRender = *Ui.TextRender();
	const CUiScopedTextRenderState TextState(TextRender);
	try
	{
		TextRender.TextColor(1.0f, 1.0f, 1.0f, Alpha);
		TextRender.SetFontPreset(EFontPreset::ICON_FONT);
		const float Width = TextRender.TextWidth(IconSize, pIcon);
		const CUIRect Rect{Position.x - Width * 0.5f, Position.y - IconSize * 0.5f, Width, IconSize};
		Ui.DoLabel(&Rect, pIcon, IconSize, TEXTALIGN_MC);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

inline bool QmDrawPieSettingsPreviewTitle(ITextRender &TextRender, const vec2 &Center, float FontSize, float CenterRadius, const char *pTitle)
{
	CUiScopedTextRenderState TextState(TextRender);
	try
	{
		TextRender.TextColor(1.0f, 1.0f, 1.0f, 0.98f);
		float Width = TextRender.TextWidth(FontSize, pTitle);
		if(Width > CenterRadius * 1.65f)
		{
			FontSize *= CenterRadius * 1.65f / Width;
			Width = TextRender.TextWidth(FontSize, pTitle);
		}
		TextRender.Text(Center.x - Width * 0.5f, Center.y - FontSize * 0.5f, FontSize, pTitle);
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	TextState.Restore();
	// 正常绘制继续沿用预览末尾恢复默认文字色的行为。
	TextRender.TextColor(TextRender.DefaultTextColor());
	return true;
}

#endif
