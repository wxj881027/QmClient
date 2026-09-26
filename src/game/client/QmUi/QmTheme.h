// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMTHEME_H
#define GAME_CLIENT_QMUI_QMTHEME_H

#include "UiTokens.h"

namespace qm_theme
{

	struct SIconTheme
	{
		ColorRGBA m_Normal;
		ColorRGBA m_Hover;
		ColorRGBA m_Active;
		ColorRGBA m_Disabled;
	};

	struct SImeTheme
	{
		ColorRGBA m_PanelBg;
		ColorRGBA m_PanelBorder;
		ColorRGBA m_PanelShadow;
		ColorRGBA m_CompositionBg;
		ColorRGBA m_CompositionSelection;
		ColorRGBA m_CompositionUnderline;
		ColorRGBA m_SelectedBg;
		ColorRGBA m_Text;
		ColorRGBA m_TextMuted;
		ColorRGBA m_TextSelected;
		float m_FontComposition;
		float m_FontCandidate;
		float m_PaddingX;
		float m_PaddingY;
		float m_RowGap;
		float m_RowHeight;
		float m_CompositionRowHeight;
		float m_NumWidth;
		float m_MinWidth;
		float m_MaxWidth;
		float m_Radius;
		float m_ScreenHeight;
		float m_ScreenMargin;
		float m_ShadowX;
		float m_ShadowY;
		float m_BorderInset;
		float m_CompositionTextPaddingX;
		float m_CandidateNumPaddingX;
		float m_CandidateGap;
		float m_CandidatePaddingX;
		float m_SelectedPaddingX;
		float m_TextSafePaddingX;
		float m_TextSafePaddingY;
		float m_TrailingWidth;
		float m_MaxCandidateTextWidth;
	};

	inline constexpr SIconTheme ICON{
		ColorRGBA{1.0f, 1.0f, 1.0f, 0.92f},
		ColorRGBA{1.0f, 1.0f, 1.0f, 1.0f},
		ui_token::color::ACCENT_PRIMARY,
		ColorRGBA{1.0f, 1.0f, 1.0f, 0.36f},
	};

	inline constexpr SImeTheme IME_LIGHT{
		ui_token::ime::PANEL_BG_LIGHT,
		ui_token::ime::PANEL_BORDER_LIGHT,
		ui_token::ime::PANEL_SHADOW_LIGHT,
		ui_token::ime::COMPOSITION_BG_LIGHT,
		ui_token::ime::COMPOSITION_SELECTION,
		ui_token::ime::COMPOSITION_UNDERLINE_LIGHT,
		ui_token::ime::SELECTED_BG_LIGHT,
		ui_token::ime::TEXT_LIGHT,
		ui_token::ime::TEXT_MUTED_LIGHT,
		ui_token::ime::TEXT_SELECTED_LIGHT,
		ui_token::ime::FONT_COMPOSITION,
		ui_token::ime::FONT_CANDIDATE,
		ui_token::ime::PADDING_X,
		ui_token::ime::PADDING_Y,
		ui_token::ime::ROW_GAP,
		ui_token::ime::ROW_HEIGHT,
		ui_token::ime::COMPOSITION_ROW_HEIGHT,
		ui_token::ime::NUM_WIDTH,
		ui_token::ime::MIN_WIDTH,
		ui_token::ime::MAX_WIDTH,
		ui_token::ime::RADIUS,
		ui_token::ime::SCREEN_HEIGHT,
		ui_token::ime::SCREEN_MARGIN,
		ui_token::ime::SHADOW_X,
		ui_token::ime::SHADOW_Y,
		ui_token::ime::BORDER_INSET,
		ui_token::ime::COMPOSITION_TEXT_PADDING_X,
		ui_token::ime::CANDIDATE_NUM_PADDING_X,
		ui_token::ime::CANDIDATE_GAP,
		ui_token::ime::CANDIDATE_PADDING_X,
		ui_token::ime::SELECTED_PADDING_X,
		ui_token::ime::TEXT_SAFE_PADDING_X,
		ui_token::ime::TEXT_SAFE_PADDING_Y,
		ui_token::ime::TRAILING_WIDTH,
		ui_token::ime::MAX_CANDIDATE_TEXT_WIDTH,
	};

	inline constexpr SImeTheme IME_DARK{
		ui_token::ime::PANEL_BG_DARK,
		ui_token::ime::PANEL_BORDER_DARK,
		ui_token::ime::PANEL_SHADOW_DARK,
		ui_token::ime::COMPOSITION_BG_DARK,
		ui_token::ime::COMPOSITION_SELECTION,
		ui_token::ime::COMPOSITION_UNDERLINE_DARK,
		ui_token::ime::SELECTED_BG_DARK,
		ui_token::ime::TEXT_DARK,
		ui_token::ime::TEXT_MUTED_DARK,
		ui_token::ime::TEXT_SELECTED_DARK,
		ui_token::ime::FONT_COMPOSITION,
		ui_token::ime::FONT_CANDIDATE,
		ui_token::ime::PADDING_X,
		ui_token::ime::PADDING_Y,
		ui_token::ime::ROW_GAP,
		ui_token::ime::ROW_HEIGHT,
		ui_token::ime::COMPOSITION_ROW_HEIGHT,
		ui_token::ime::NUM_WIDTH,
		ui_token::ime::MIN_WIDTH,
		ui_token::ime::MAX_WIDTH,
		ui_token::ime::RADIUS,
		ui_token::ime::SCREEN_HEIGHT,
		ui_token::ime::SCREEN_MARGIN,
		ui_token::ime::SHADOW_X,
		ui_token::ime::SHADOW_Y,
		ui_token::ime::BORDER_INSET,
		ui_token::ime::COMPOSITION_TEXT_PADDING_X,
		ui_token::ime::CANDIDATE_NUM_PADDING_X,
		ui_token::ime::CANDIDATE_GAP,
		ui_token::ime::CANDIDATE_PADDING_X,
		ui_token::ime::SELECTED_PADDING_X,
		ui_token::ime::TEXT_SAFE_PADDING_X,
		ui_token::ime::TEXT_SAFE_PADDING_Y,
		ui_token::ime::TRAILING_WIDTH,
		ui_token::ime::MAX_CANDIDATE_TEXT_WIDTH,
	};

	inline constexpr SImeTheme IME = IME_LIGHT;

	constexpr const SImeTheme &ImeTheme(bool Dark)
	{
		return Dark ? IME_DARK : IME_LIGHT;
	}

} // namespace qm_theme

#endif
