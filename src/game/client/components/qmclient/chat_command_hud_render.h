#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_RENDER_H

#include "chat_command_hud.h"

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/localization.h>

inline void QmRenderChatCommandHud(const CQmChatCommandHud &Hud, CUi *pUi, ITextRender *pTextRender, vec2 MousePos, float FontSize)
{
	const auto &Layout = Hud.Layout();
	if(Layout.m_VisibleRows == 0)
		return;

	CUiScopedGaussianBlurSuppression BlurSuppression(pUi);
	const CUIRect Panel{Layout.m_X, Layout.m_Y, Layout.m_W, Layout.m_H};
	Panel.Draw(ColorRGBA(0.05f, 0.07f, 0.08f, 0.88f), IGraphics::CORNER_ALL, 3.0f);
	const ColorRGBA TextColor = pTextRender->GetTextColor();
	CUIRect Header{Panel.x + 5.0f, Panel.y, Panel.w - 10.0f, Layout.m_HeaderHeight};
	CUIRect CountRect;
	Header.VSplitRight(44.0f, &Header, &CountRect);
	SLabelProperties LabelProps;
	LabelProps.m_MaxWidth = Header.w;
	LabelProps.m_EllipsisAtEnd = true;
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
		CUIRect Name{Rect.x + 5.0f, Rect.y + 1.0f, Rect.w - 10.0f, FontSize + 1.0f};
		CUIRect Detail{Name.x, Name.y + Name.h, Name.w, std::max(1.0f, Rect.h - Name.h - 2.0f)};
		LabelProps.m_MaxWidth = Name.w;
		pTextRender->TextColor(0.95f, 0.97f, 0.98f, 1.0f);
		pUi->DoLabel(&Name, Candidate.m_Name.c_str(), FontSize, TEXTALIGN_ML, LabelProps);
		pTextRender->TextColor(0.70f, 0.75f, 0.77f, 0.90f);
		pUi->DoLabel(&Detail, Candidate.m_Detail.c_str(), FontSize * 0.67f, TEXTALIGN_ML, LabelProps);
	}
	pTextRender->TextColor(TextColor);
}

#endif
