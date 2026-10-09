#include "QmColorGradientEditor.h"

#include <engine/shared/config.h>

#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <array>

bool CMenus::DoMessageGradientLine(CChat &Chat, CUIRect *pView, int Tab, const char *pLabelTextId, const char *pLabel, unsigned *pBaseColor, char *pGradient, int GradientSize, ColorRGBA DefaultColor, CButtonContainer *pResetButton, CButtonContainer *pAddButton, CButtonContainer *pRemoveButton, unsigned *pColorValues, EQmChatGradientRole Role, bool CheckBoxSpacing, int *pCheckBoxValue, float LineHeight, float LineSpacing, float BodySize, float ButtonHeight)
{
	const float ResolvedButtonHeight = ButtonHeight > 0.0f ? ButtonHeight : LineHeight;
	SSettingsContentMetrics Metrics;
	Metrics.m_UiScale = std::clamp(LineHeight / ui_token::settings::ROW_HEIGHT, 0.5f, 1.5f);
	Metrics.m_LineHeight = LineHeight;
	Metrics.m_ButtonHeight = ResolvedButtonHeight;
	Metrics.m_BodySize = BodySize;
	Metrics.m_LineSpacing = LineSpacing;

	bool Changed = false;
	const SSettingsColorRowLayout TopLayout = ResolveSettingsColorRowLayout(*pView, Metrics, CheckBoxSpacing && pCheckBoxValue == nullptr);
	pView->y += TopLayout.m_ConsumedHeight;
	pView->h = std::max(0.0f, pView->h - TopLayout.m_ConsumedHeight);
	CUIRect Label = TopLayout.m_LabelRect;
	Label.w = TopLayout.m_ColorButtonRect.x + TopLayout.m_ColorButtonRect.w - Label.x;

	if(pCheckBoxValue != nullptr)
	{
		SLabelProperties LabelProps;
		if(DoSettingsButton_CheckBox(SETTINGS_APPEARANCE, Tab, Tab, pCheckBoxValue, pLabelTextId, pLabel, *pCheckBoxValue, &Label, LabelProps, true, BodySize) && !Ui()->RenderOnly())
		{
			*pCheckBoxValue ^= 1;
			Changed = true;
		}
	}
	if(pCheckBoxValue == nullptr)
		DoSettingsMenuLabel(SETTINGS_APPEARANCE, Tab, Tab, pLabelTextId, &Label, pLabel, BodySize, TEXTALIGN_ML);

	if(DoSettingsButton_Menu(SETTINGS_APPEARANCE, Tab, Tab, pResetButton, "appearance-chat-gradient-reset", Localize("Reset"), 0, &TopLayout.m_ResetButtonRect, Metrics, BUTTONFLAG_LEFT, IGraphics::CORNER_ALL, ui_token::radius::BASE, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 0.1f) && !Ui()->RenderOnly())
	{
		*pBaseColor = color_cast<ColorHSLA>(DefaultColor).Pack(false);
		CMessageGradient::Reset(pGradient, GradientSize);
		QmChatGradientBinding(g_Config, Role).Reset();
		Changed = true;
	}

	Changed |= DoColorGradientPalette(pView, pBaseColor, pGradient, GradientSize, pAddButton, pRemoveButton, pColorValues, Metrics, CheckBoxSpacing);
	if(Changed)
	{
		Chat.RebuildChat();
		ConfigManager()->Save();
	}
	return Changed;
}

void CMenus::RenderQmChatGradientSettings(CUIRect &Content, CChat &Chat, const SSettingsContentMetrics &Metrics)
{
	const float Height = std::max(Metrics.m_LineHeight, Metrics.m_ButtonHeight);
	CUIRect Row, Label, Control;
	Content.HSplitTop(Height, &Row, &Content);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	Ui()->DoLabel(&Row, Localize("Gradient settings"), Metrics.m_BodySize, TEXTALIGN_ML);
	Content.HSplitTop(Height, &Row, &Content);
	Content.HSplitTop(Metrics.m_LineSpacing, nullptr, &Content);
	const float LabelWidth = Row.w * 0.4f;
	Row.VSplitLeft(LabelWidth, &Label, &Control);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Label, Localize("Edit color"), Metrics.m_BodySize, TEXTALIGN_ML, Props);
	const char *apRoles[] = {Localize("System message"), Localize("Client message"), Localize("Highlighted message"),
		Localize("Team message"), Localize("Friend message"), Localize("Normal message")};
	static int s_Role = 5;
	static CUi::SDropDownState s_RoleDropdown;
	static CScrollRegion s_RoleScroll;
	static std::array<SQmGradientGeometryState, 6> s_aStates;
	s_RoleDropdown.m_SelectionPopupContext.m_pScrollRegion = &s_RoleScroll;
	const int Selected = DoSettingsDropDown(&Control, s_Role, apRoles, std::size(apRoles), s_RoleDropdown);
	if(!Ui()->RenderOnly())
		s_Role = std::clamp(Selected, 0, 5);
	const auto Binding = QmChatGradientBinding(g_Config, static_cast<EQmChatGradientRole>(s_Role));
	if(DoColorGradientGeometry(Content, Binding, s_aStates[s_Role], Metrics, LabelWidth))
	{
		Chat.RebuildChat();
		ConfigManager()->Save();
	}
}
