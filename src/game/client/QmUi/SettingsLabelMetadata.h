#ifndef GAME_CLIENT_QMUI_SETTINGSLABELMETADATA_H
#define GAME_CLIENT_QMUI_SETTINGSLABELMETADATA_H

#include <engine/shared/localization.h>

#include <string_view>

struct SSettingsLabelMetadata
{
	const char *m_pId;
	const char *m_pLabel;
	const char *m_pTip;
};

// 按稳定来源显式拆分解释，绝不截取译文括号；单位、名称与快捷键仍保留原标签。
inline const SSettingsLabelMetadata *SettingsLabelMetadata(const char *pId)
{
	static constexpr SSettingsLabelMetadata aMetadata[] = {
		{"appearance-show-client-ids", Localizable("Show client IDs"), Localizable("Scoreboard, chat, spectating")},
		{"Predict events (experimental)", Localizable("Predict events"), Localizable("Experimental")},
		{"AntiPing (latency compensation)", Localizable("AntiPing"), Localizable("Latency compensation")},
		{"general-lower-refresh-rate", Localizable("Save power by lowering update rate"), Localizable("Higher input latency")},
		{"tclient-mini-debug", Localizable("Show position and angle"), Localizable("Mini debug")},
		{"tclient-fast-input", Localizable("Fast input"), Localizable("Reduce visual latency")},
		{"tclient-antiping-negative-buffer", Localizable("Negative stability buffer"), Localizable("For Gores")},
		{"Tee collision (Tee to Tee)", Localizable("Tee collision"), Localizable("Tee to Tee")},
		{"Tee freeze probe (Tee to Freeze)", Localizable("Tee freeze probe"), Localizable("Tee to Freeze")},
		{"Enable FTAPI auto-translate (may overload the service)", Localizable("Enable FTAPI auto-translate"), Localizable("May overload the service")},
		{"qmclient-translate-llm-endpoint-optional", Localizable("Endpoint"), Localizable("Optional")},
		{"qmclient-translate-llm-concurrency", Localizable("Concurrency"), Localizable("0 = auto")},
		{"Enable thinking mode (slower)", Localizable("Enable thinking mode"), Localizable("Slower")},
		{"Auto gain control for mic (AGC, experimental)", Localizable("Auto gain control for mic"), Localizable("AGC, experimental")},
		{"qm-steam-client-path", Localizable("Steam client path"), Localizable("Leave empty to detect automatically")},
	};
	if(pId != nullptr)
		for(const auto &Metadata : aMetadata)
			if(std::string_view(pId) == Metadata.m_pId)
				return &Metadata;
	return nullptr;
}

struct SSettingsLabelPresentation
{
	const char *m_pText;
	const char *m_pTip = nullptr;
	const void *m_pTipId = nullptr;
};

template<typename TLocalize>
inline SSettingsLabelPresentation ResolveSettingsLabelPresentation(const char *pId, const char *pFallbackText, TLocalize &&LocalizeText)
{
	if(const auto *pMetadata = SettingsLabelMetadata(pId))
		return {LocalizeText(pMetadata->m_pLabel), LocalizeText(pMetadata->m_pTip), pMetadata};
	return {pFallbackText};
}

#endif
