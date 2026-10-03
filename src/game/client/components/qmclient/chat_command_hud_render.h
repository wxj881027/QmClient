#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_RENDER_H

#include "chat_command_hud.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/localization.h>

inline void QmRenderChatCommandHud(const CQmChatCommandHud &Hud, CUi *pUi, ITextRender *pTextRender, vec2 MousePos)
{
	const auto &Layout = Hud.Layout();
	if(Layout.m_VisibleRows == 0)
		return;

	const float Scale = CQmChatCommandHud::UI_SCALE;
	const float FontSize = Layout.m_FontSize;
	const float Rounding = 3.0f * Scale;
	const CUIRect Panel{Layout.m_X, Layout.m_Y, Layout.m_W, Layout.m_H};
	const float UserOpacity = std::clamp(g_Config.m_QmImeOpacity, 0, 100) / 100.0f;
	ColorRGBA PanelBackground = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmImeBgColor));
	PanelBackground.a *= UserOpacity;
	if(UserOpacity < 0.999f && g_Config.m_QmGaussianBlur != 0)
		pUi->RenderGaussianBlur(Panel, UserOpacity, IGraphics::CORNER_ALL, Rounding);
	CUiScopedGaussianBlurSuppression BlurSuppression(pUi);
	Panel.Draw(PanelBackground, IGraphics::CORNER_ALL, Rounding);
	const ColorRGBA TextColor = pTextRender->GetTextColor();
	CUIRect Header{Panel.x + 5.0f * Scale, Panel.y, Panel.w - 10.0f * Scale, Layout.m_HeaderHeight};
	CUIRect CountRect;
	Header.VSplitRight(44.0f * Scale, &Header, &CountRect);
	SLabelProperties LabelProps;
	LabelProps.m_MaxWidth = Header.w;
	LabelProps.m_EllipsisAtEnd = true;
	LabelProps.m_StopAtEnd = true;
	LabelProps.m_DisallowNewline = true;
	LabelProps.m_EnableWidthCheck = false;
	// HUD 已按自身坐标缩放字号，不能再被菜单标签默认的最小字号放大。
	LabelProps.m_MinimumFontSize = 0.0f;
	pTextRender->TextColor(0.65f, 0.82f, 0.90f, 0.90f);
	pUi->DoLabel(&Header, Localize("Command completion"), FontSize * 0.85f, TEXTALIGN_ML, LabelProps);
	char aCount[48];
	str_format(aCount, sizeof(aCount), "%d-%d / %d", Hud.Offset() + 1, Hud.Offset() + Layout.m_VisibleRows, Hud.Count());
	LabelProps.m_MaxWidth = CountRect.w;
	pUi->DoLabel(&CountRect, aCount, FontSize * 0.7f, TEXTALIGN_MR, LabelProps);

	const int HoveredRow = Hud.HoveredRow(MousePos.x, MousePos.y);
	for(int Row = 0; Row < Layout.m_VisibleRows; ++Row)
	{
		const int Index = Hud.Offset() + Row;
		const auto &Candidate = Hud.Candidate(Index);
		const CUIRect Rect{Panel.x, Panel.y + Layout.m_HeaderHeight + Row * Layout.m_RowHeight, Panel.w, Layout.m_RowHeight};
		if(HoveredRow == Index)
			Rect.Draw(ColorRGBA(0.24f, 0.43f, 0.48f, 0.55f), IGraphics::CORNER_NONE, 0.0f);
		const CUIRect Name{Rect.x + 5.0f * Scale, Rect.y + CQmChatCommandHud::ROW_PADDING_Y, Rect.w - 10.0f * Scale, FontSize + 2.0f * Scale};
		const CUIRect Detail{Name.x, Name.y + Name.h + CQmChatCommandHud::ROW_LINE_GAP, Name.w, Layout.m_DetailFontSize + 2.0f * Scale};
		LabelProps.m_MaxWidth = Name.w;
		pTextRender->TextColor(0.95f, 0.97f, 0.98f, 1.0f);
		pUi->DoLabel(&Name, Candidate.m_Name.c_str(), FontSize, TEXTALIGN_ML, LabelProps);
		pTextRender->TextColor(0.70f, 0.75f, 0.77f, 0.90f);
		pUi->DoLabel(&Detail, Candidate.m_Detail.c_str(), Layout.m_DetailFontSize, TEXTALIGN_ML, LabelProps);
	}
	pTextRender->TextColor(TextColor);
}

#endif
