/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_PRESENTATION_H
#define GAME_CLIENT_UI_CARD_PRESENTATION_H

#include "card_registry.h"

#include <string>

enum class ECardVisualState
{
	NORMAL,
	HOVERED,
	FOCUSED,
	PRESSED,
	DISABLED,
	LOADING,
	ERROR,
	COLLAPSED,
};

enum class ECardActionType
{
	NONE,
	TOGGLE,
	ACTIVATE,
	RESET,
	NAVIGATE,
};

struct SCardRect
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_Width = 0.0f;
	float m_Height = 0.0f;
};

struct SCardPresentationSpec
{
	std::string m_Id;
	float m_MinWidth = 220.0f;
	float m_MinHeight = 72.0f;
	bool m_SupportsCollapse = true;
	bool m_SupportsDrag = true;
};

struct SCardPresentationInput
{
	SCardPresentationSpec m_Spec;
	SCardRect m_Bounds;
	float m_UiScale = 1.0f;
	bool m_Visible = true;
	bool m_Available = true;
	bool m_Collapsed = false;
	bool m_Hovered = false;
	bool m_Focused = false;
	bool m_Pressed = false;
	bool m_Loading = false;
	bool m_Error = false;
};

struct SCardPresentationLayout
{
	SCardRect m_Header;
	SCardRect m_Content;
	ECardVisualState m_State = ECardVisualState::NORMAL;
	bool m_DrawContent = false;
	bool m_DrawChrome = false;
	bool m_Valid = false;
};

struct SCardPresentationAction
{
	ECardActionType m_Type = ECardActionType::NONE;
	std::string m_CardId;
};

SCardPresentationLayout ResolveCardPresentationLayout(const SCardPresentationInput &Input);
SCardPresentationAction ResolveCardPresentationAction(const std::string &CardId, bool TogglePressed, bool ActivatePressed, bool ResetPressed);

#endif
