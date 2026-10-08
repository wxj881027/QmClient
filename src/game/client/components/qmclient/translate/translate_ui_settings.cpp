// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "translate_ui_settings.h"

#include <base/color.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

void NTranslateUiSettings::RenderTranslateUiModule(CMenus *pMenus, CUIRect &CardContent, float LineHeight, float BodySize, float LineSpacing)
{
	static CButtonContainer s_TranslateBtnDisabledId, s_TranslateBtnEnabledId;
	static CButtonContainer s_TranslateMenuBgId, s_TranslateMenuSelectedId, s_TranslateMenuNormalId;

	CUIRect Row;
	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateBtnDisabledId, LineHeight, BodySize, 0, &Row,
		Localize("Button - Disabled"), &g_Config.m_QmTranslateBtnColorDisabled,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateBtnColorDisabled, true)), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateBtnEnabledId, LineHeight, BodySize, 0, &Row,
		Localize("Button - Enabled"), &g_Config.m_QmTranslateBtnColorEnabled,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateBtnColorEnabled, true)), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateMenuBgId, LineHeight, BodySize, 0, &Row,
		Localize("Menu background"), &g_Config.m_QmTranslateMenuBgColor,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateMenuBgColor, true)), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateMenuSelectedId, LineHeight, BodySize, 0, &Row,
		Localize("Menu option - selected"), &g_Config.m_QmTranslateMenuOptionSelected,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateMenuOptionSelected, true)), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateMenuNormalId, LineHeight, BodySize, 0, &Row,
		Localize("Menu option - normal"), &g_Config.m_QmTranslateMenuOptionNormal,
		color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::QmTranslateMenuOptionNormal, true)), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);
	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	static CButtonContainer s_ResetAppearance;
	if(pMenus->DoButton_Menu(&s_ResetAppearance, Localize("Reset translation colors"), 0, &Row) && !pMenus->Ui()->RenderOnly())
	{
		g_Config.m_QmTranslateBtnColorDisabled = DefaultConfig::QmTranslateBtnColorDisabled;
		g_Config.m_QmTranslateBtnColorEnabled = DefaultConfig::QmTranslateBtnColorEnabled;
		g_Config.m_QmTranslateMenuBgColor = DefaultConfig::QmTranslateMenuBgColor;
		g_Config.m_QmTranslateMenuOptionSelected = DefaultConfig::QmTranslateMenuOptionSelected;
		g_Config.m_QmTranslateMenuOptionNormal = DefaultConfig::QmTranslateMenuOptionNormal;
	}
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);
}
