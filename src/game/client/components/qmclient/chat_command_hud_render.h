#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_COMMAND_HUD_RENDER_H

#include "chat_command_hud.h"
#include "chat_emoji.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmChatCompletionAppearance.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTextRenderState.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <new>

inline void QmRenderChatCommandHud(const CQmChatCommandHud &Hud, CUi *pUi, ITextRender *pTextRender, vec2 MousePos, CQmChatEmoji *pEmoji = nullptr)
{
	const auto &Layout = Hud.Layout();
	if(Layout.m_VisibleRows == 0)
		return;

	const float Scale = CQmChatCommandHud::UI_SCALE;
	const float FontSize = Layout.m_FontSize;
	const SQmChatCompletionAppearance Appearance = QmChatCompletionAppearance(g_Config);
	const auto &Ime = Appearance.m_Theme;
	const float Rounding = Ime.m_Radius * Scale;
	const bool IsEmoji = Hud.Source() == CQmChatCommandHud::ESource::EMOJI;
	const CUIRect Panel{Layout.m_X, Layout.m_Y, Layout.m_W, Layout.m_H};
	SRoundedSurfaceParams SurfaceParams;
	SurfaceParams.m_Radius = Rounding;
	float ScreenLeft, ScreenTop, ScreenRight, ScreenBottom;
	pUi->Graphics()->GetScreen(&ScreenLeft, &ScreenTop, &ScreenRight, &ScreenBottom);
	SurfaceParams.m_PixelSize = (ScreenBottom - ScreenTop) / std::max(1, pUi->Graphics()->ScreenHeight());
	CUIRect Shadow = Panel;
	Shadow.x += Ime.m_ShadowX * Scale;
	Shadow.y += Ime.m_ShadowY * 0.65f * Scale;
	DrawRoundedSurface(pUi->Graphics(), Shadow, Ime.m_PanelShadow.WithMultipliedAlpha(0.46f), ColorRGBA(), SurfaceParams);
	Shadow = Panel;
	Shadow.y += Ime.m_ShadowY * 1.7f * Scale;
	DrawRoundedSurface(pUi->Graphics(), Shadow, Ime.m_PanelShadow.WithMultipliedAlpha(0.28f), ColorRGBA(), SurfaceParams);
	if(Appearance.m_BackgroundOpacity < 0.999f && g_Config.m_QmGaussianBlur != 0)
		pUi->RenderGaussianBlur(Panel, Appearance.m_BackgroundOpacity, IGraphics::CORNER_ALL, Rounding);
	CUiScopedGaussianBlurSuppression BlurSuppression(pUi);
	SurfaceParams.m_BorderWidth = Ime.m_BorderInset * Scale;
	DrawRoundedSurface(pUi->Graphics(), Panel, Appearance.m_Background,
		Ime.m_PanelBorder.WithMultipliedAlpha(Appearance.m_BackgroundOpacity), SurfaceParams);
	const CUiScopedTextRenderState TextState(*pTextRender);
	CUIRect Header{Panel.x + 5.0f * Scale, Panel.y, Panel.w - 10.0f * Scale, Layout.m_HeaderHeight};
	CUIRect CountRect;
	Header.VSplitRight(44.0f * Scale, &Header, &CountRect);
	SLabelProperties LabelProps;
	LabelProps.m_EllipsisAtEnd = true;
	LabelProps.m_StopAtEnd = true;
	LabelProps.m_DisallowNewline = true;
	LabelProps.m_EnableWidthCheck = false;
	// HUD 已按自身坐标缩放字号，不能再被菜单标签默认的最小字号放大。
	LabelProps.m_MinimumFontSize = 0.0f;
	const auto DrawLabel = [&](const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color) {
		LabelProps.m_MaxWidth = Rect.w;
		pTextRender->TextColor(Color);
		try
		{
			pUi->DoLabel(&Rect, pText, Size, Align, LabelProps);
		}
		catch(const std::bad_alloc &)
		{
			// 文字引擎接管未释放的容器。本帧略过此标签，下帧照常重试。
		}
	};
	DrawLabel(Header, IsEmoji ? Localize("Emoji completion") : Localize("Command completion"), Layout.m_HeaderFontSize, TEXTALIGN_ML, Ime.m_Text);
	char aCount[48];
	str_format(aCount, sizeof(aCount), "%d-%d / %d", Hud.Offset() + 1, Hud.Offset() + Layout.m_VisibleRows, Hud.Count());
	DrawLabel(CountRect, aCount, FontSize * 0.7f, TEXTALIGN_MR, Ime.m_TextMuted);
	const CUIRect Divider{Panel.x + 5.0f * Scale, Panel.y + Layout.m_HeaderHeight - Scale, Panel.w - 10.0f * Scale, 0.5f * Scale};
	Divider.Draw(Ime.m_PanelBorder, IGraphics::CORNER_NONE, 0.0f);

	const int HoveredRow = Hud.HoveredRow(MousePos.x, MousePos.y);
	for(int Row = 0; Row < Layout.m_VisibleRows; ++Row)
	{
		const int Index = Hud.Offset() + Row;
		const auto &Candidate = Hud.Candidate(Index);
		const CUIRect Rect{Panel.x, Panel.y + Layout.m_HeaderHeight + Row * Layout.m_RowHeight, Panel.w, Layout.m_RowHeight};
		const bool Selected = Hud.Selected() == Index;
		const bool Highlighted = Selected || HoveredRow == Index;
		if(Highlighted)
		{
			// 内缩并保留圆角，末行选中时不会覆盖面板底部的圆角边界。
			const float Inset = Scale;
			CUIRect Selection;
			Rect.Margin(Inset, &Selection);
			SurfaceParams.m_BorderWidth = 0.0f;
			SurfaceParams.m_Radius = std::min(Rounding - Inset, Selection.h * 0.5f);
			DrawRoundedSurface(pUi->Graphics(), Selection, Ime.m_SelectedBg.WithMultipliedAlpha(Selected ? 1.0f : 0.65f), ColorRGBA(), SurfaceParams);
		}
		CUIRect Name{Rect.x + 5.0f * Scale, Rect.y + CQmChatCommandHud::ROW_PADDING_Y, Rect.w - 10.0f * Scale, FontSize + (IsEmoji ? 2.0f : 1.0f) * Scale};
		if(IsEmoji)
		{
			const float EmojiSize = Name.h;
			if(pEmoji != nullptr)
				pEmoji->Render(QmChatEmojiFromText(Candidate.m_Name.c_str()), Name.x, Name.y, EmojiSize, EmojiSize, 1.0f);
			Name.x += EmojiSize + 4.0f * Scale;
			Name.w -= EmojiSize + 4.0f * Scale;
		}
		DrawLabel(Name, Candidate.m_Label.c_str(), FontSize, TEXTALIGN_ML, Highlighted ? Ime.m_TextSelected : Ime.m_Text);
		if(Layout.m_DetailFontSize > 0.0f)
		{
			const CUIRect Detail{Name.x, Name.y + Name.h + CQmChatCommandHud::ROW_LINE_GAP, Name.w, Layout.m_DetailFontSize + Scale};
			DrawLabel(Detail, Candidate.m_Detail.c_str(), Layout.m_DetailFontSize, TEXTALIGN_ML, Ime.m_TextMuted);
		}
	}
}

#endif
