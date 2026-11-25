#include "QmColorGradientEditor.h"

#include <engine/shared/config.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/chat_gradient.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
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

	CSettingsContentLayoutFlow Flow(*pView);
	CUIRect TopRow;
	// 行距由本行自己声明（声明阶段记到上一行末尾）：行后不再补 Space，
	// 下面的调色板行同样自带行距，相邻两块之间不会再叠成双倍间距。
	const bool HasTopRow = Flow.Control(pLabelTextId, ResolvedButtonHeight, TopRow, ui_token::settings::RowGap(LineSpacing));
	bool Changed = false;
	if(HasTopRow)
	{
		const SSettingsColorRowLayout TopLayout = ResolveSettingsColorRowLayout(TopRow, Metrics, CheckBoxSpacing && pCheckBoxValue == nullptr);
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

		GameClient()->m_Tooltips.DoToolTip(pResetButton, &TopLayout.m_ResetButtonRect, Localize("Reset"));
		if(Ui()->DoButton_QmIcon(pResetButton, EQmIcon::ARROW_ROTATE_RIGHT, FontIcons::FONT_ICON_ARROW_ROTATE_RIGHT, 0, &TopLayout.m_ResetButtonRect, BUTTONFLAG_LEFT) && !Ui()->RenderOnly())
		{
			*pBaseColor = color_cast<ColorHSLA>(DefaultColor).Pack(false);
			CMessageGradient::Reset(pGradient, GradientSize);
			QmChatGradientBinding(g_Config, Role).Reset();
			Changed = true;
		}
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
	CSettingsContentLayoutFlow Flow(Content);
	CUIRect Row, Label, Control;
	// 每个可见行都在自己这一处声明行距，行后不再补 Space：
	// 上一段的调色板行和下面的渐变几何行都是同一套规则，块与块之间只有一档行距。
	if(Flow.Control("appearance-chat-gradient-title", Height, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing)))
		Ui()->DoLabel(&Row, Localize("Gradient settings"), Metrics.m_BodySize, TEXTALIGN_ML);
	const bool HasRole = Flow.Control("appearance-chat-gradient-role", Height, Row, ui_token::settings::RowGap(Metrics.m_LineSpacing));
	float LabelWidth = ResolveSettingsCardLabelWidth(Content.w, Metrics);
	if(HasRole)
	{
		SplitSettingsControlRow(Row, Metrics, &Label, &Control, ESettingsControlWidth::LONG_CHOICE);
		LabelWidth = Control.x - Row.x;
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, Localize("Edit color"), Metrics.m_BodySize, TEXTALIGN_ML, Props);
	}
	static int s_Role = 5;
	static CUi::SDropDownState s_RoleDropdown;
	static CScrollRegion s_RoleScroll;
	static std::array<SQmGradientGeometryState, 6> s_aStates;
	s_RoleDropdown.m_SelectionPopupContext.m_pScrollRegion = &s_RoleScroll;
	if(HasRole)
	{
		const char *apRoles[] = {Localize("System message"), Localize("Client message"), Localize("Highlighted message"),
			Localize("Team message"), Localize("Friend message"), Localize("Normal message")};
		const int Selected = DoSettingsDropDown(&Control, s_Role, apRoles, std::size(apRoles), s_RoleDropdown);
		if(Flow.Input() && !Ui()->RenderOnly())
			s_Role = std::clamp(Selected, 0, 5);
	}
	const auto Binding = QmChatGradientBinding(g_Config, static_cast<EQmChatGradientRole>(s_Role));
	if(DoColorGradientGeometry(Content, Binding, s_aStates[s_Role], Metrics, LabelWidth))
	{
		Chat.RebuildChat();
		ConfigManager()->Save();
	}
}
