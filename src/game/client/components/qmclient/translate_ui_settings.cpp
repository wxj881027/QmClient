#include <base/color.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include "translate_ui_settings.h"

void NTranslateUiSettings::RenderTranslateUiModule(CMenus *pMenus, CUIRect &CardContent, float LineHeight, float BodySize, float LineSpacing)
{
	static CButtonContainer s_TranslateBtnDisabledId, s_TranslateBtnEnabledId;
	static CButtonContainer s_TranslateMenuBgId, s_TranslateMenuSelectedId, s_TranslateMenuNormalId;

	CUIRect Row;
	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateBtnDisabledId, LineHeight, BodySize, 0, &Row,
		Localize("Button - Disabled"), &g_Config.m_QmTranslateBtnColorDisabled,
		ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateBtnEnabledId, LineHeight, BodySize, 0, &Row,
		Localize("Button - Enabled"), &g_Config.m_QmTranslateBtnColorEnabled,
		ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateMenuBgId, LineHeight, BodySize, 0, &Row,
		Localize("Menu Background"), &g_Config.m_QmTranslateMenuBgColor,
		ColorRGBA(0.12f, 0.12f, 0.12f, 0.95f), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateMenuSelectedId, LineHeight, BodySize, 0, &Row,
		Localize("Menu Option - Selected"), &g_Config.m_QmTranslateMenuOptionSelected,
		ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);

	CardContent.HSplitTop(LineHeight, &Row, &CardContent);
	pMenus->DoLine_ColorPicker(&s_TranslateMenuNormalId, LineHeight, BodySize, 0, &Row,
		Localize("Menu Option - Normal"), &g_Config.m_QmTranslateMenuOptionNormal,
		ColorRGBA(0.20f, 0.20f, 0.20f, 0.90f), true, nullptr, true);
	CardContent.HSplitTop(LineSpacing, nullptr, &CardContent);
}
