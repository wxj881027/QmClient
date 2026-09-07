/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_presentation.h"

#include <algorithm>

namespace
{
float Scaled(const float Value, const float Scale)
{
	return std::max(0.0f, Value) * std::clamp(Scale, 0.5f, 3.0f);
}
}

SCardPresentationLayout ResolveCardPresentationLayout(const SCardPresentationInput &Input)
{
	SCardPresentationLayout Result;
	if(!Input.m_Visible || !Input.m_Available || Input.m_Bounds.m_Width <= 0.0f || Input.m_Bounds.m_Height <= 0.0f)
		return Result;
	const float Scale = std::clamp(Input.m_UiScale, 0.5f, 3.0f);
	const float HeaderHeight = std::min(Input.m_Bounds.m_Height, std::max(24.0f * Scale, Scaled(Input.m_Spec.m_MinHeight, Scale) * 0.35f));
	Result.m_Header = {Input.m_Bounds.m_X, Input.m_Bounds.m_Y, Input.m_Bounds.m_Width, HeaderHeight};
	Result.m_Content = {Input.m_Bounds.m_X, Input.m_Bounds.m_Y + HeaderHeight, Input.m_Bounds.m_Width, std::max(0.0f, Input.m_Bounds.m_Height - HeaderHeight)};
	Result.m_DrawChrome = true;
	Result.m_DrawContent = (!Input.m_Collapsed || !Input.m_Spec.m_SupportsCollapse) && Result.m_Content.m_Height > 0.0f;
	Result.m_Valid = Input.m_Bounds.m_Width >= Scaled(Input.m_Spec.m_MinWidth, Scale) && Input.m_Bounds.m_Height >= HeaderHeight;
	if(Input.m_Error)
		Result.m_State = ECardVisualState::ERROR;
	else if(Input.m_Loading)
		Result.m_State = ECardVisualState::LOADING;
	else if(Input.m_Collapsed)
		Result.m_State = ECardVisualState::COLLAPSED;
	else if(Input.m_Pressed)
		Result.m_State = ECardVisualState::PRESSED;
	else if(Input.m_Focused)
		Result.m_State = ECardVisualState::FOCUSED;
	else if(Input.m_Hovered)
		Result.m_State = ECardVisualState::HOVERED;
	return Result;
}

SCardPresentationAction ResolveCardPresentationAction(const std::string &CardId, const bool TogglePressed, const bool ActivatePressed, const bool ResetPressed)
{
	if(CardId.empty())
		return {};
	if(ResetPressed)
		return {ECardActionType::RESET, CardId};
	if(TogglePressed)
		return {ECardActionType::TOGGLE, CardId};
	if(ActivatePressed)
		return {ECardActionType::ACTIVATE, CardId};
	return {};
}
