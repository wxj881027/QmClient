#ifndef GAME_CLIENT_QMUI_CARDS_QMAPPEARANCEPRESET_H
#define GAME_CLIENT_QMUI_CARDS_QMAPPEARANCEPRESET_H

#include <engine/shared/config.h>
#include <engine/shared/localization.h>

#include <array>

namespace QmAppearancePreset
{
	struct SIntSetting
	{
		const char *m_pLabel;
		const char *m_pValueText;
		int CConfig::*m_pMember;
		int m_Value;
		bool m_RequiresFont = false;
	};

	inline const std::array<SIntSetting, 9> &IntSettings()
	{
		static const std::array<SIntSetting, 9> s_aSettings = {{
			{Localizable("Latin font weight"), "400", &CConfig::m_QmCustomFontWeight, 400, true},
			{Localizable("CJK font weight"), "400", &CConfig::m_QmCustomFontWeightCjk, 400, true},
			{Localizable("Global UI size"), "100%", &CConfig::m_QmUiScale, 100},
			{Localizable("UI animations"), Localizable("Full"), &CConfig::m_QmUiMotionLevel, 2},
			{Localizable("Menu panel opacity"), "30%", &CConfig::m_ClMenuPanelOpacity, 30},
			{Localizable("Menu accent panel opacity"), "30%", &CConfig::m_ClMenuPanelElevatedOpacity, 30},
			{Localizable("Settings page opacity"), "30%", &CConfig::m_ClSettingsTabbarOpacity, 30},
			{Localizable("Backdrop blur"), Localizable("On"), &CConfig::m_QmGaussianBlur, 1},
			{Localizable("Popup blur"), Localizable("On"), &CConfig::m_QmUiPopupBlur, 1},
		}};
		return s_aSettings;
	}

	struct SColorSetting
	{
		const char *m_pLabel;
		const char *m_pValueText;
		unsigned CConfig::*m_pMember;
		unsigned m_Value;
	};

	inline const std::array<SColorSetting, 2> &ColorSettings()
	{
		static const std::array<SColorSetting, 2> s_aSettings = {{
			{Localizable("UI color"), Localizable("Dark translucent"), &CConfig::m_UiColor, 0x4D000000},
			{Localizable("Menu panel color"), Localizable("Black"), &CConfig::m_ClMenuPanelColor, 0x000000},
		}};
		return s_aSettings;
	}

	inline bool Apply(CConfig &Config, const char *pResolvedFont)
	{
		const bool FontAvailable = pResolvedFont != nullptr && pResolvedFont[0] != '\0' &&
			str_length(pResolvedFont) < static_cast<int>(sizeof(Config.m_QmCustomFont)) &&
			str_length(pResolvedFont) < static_cast<int>(sizeof(Config.m_QmCustomFontCjk));
		for(const auto &Setting : IntSettings())
			if(FontAvailable || !Setting.m_RequiresFont)
				Config.*(Setting.m_pMember) = Setting.m_Value;
		for(const auto &Setting : ColorSettings())
			Config.*(Setting.m_pMember) = Setting.m_Value;
		if(FontAvailable)
		{
			str_copy(Config.m_QmCustomFont, pResolvedFont);
			str_copy(Config.m_QmCustomFontCjk, pResolvedFont);
		}
		return FontAvailable;
	}
}

#endif
