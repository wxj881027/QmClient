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
		{"Predict events (experimental)", Localizable("Predict events"), Localizable("If the server allows event prediction, play predicted sounds and particles before its reply. A hammer hit can sound twice.")},
		{"AntiPing (latency compensation)", Localizable("AntiPing"), Localizable("Predict other tees and projectiles locally to show movement sooner. Their positions may shift when server updates arrive.")},
		{"graphics-lower-update-rate", Localizable("Cap update rate to save power"), Localizable("Check to cap the client update rate at 480 Hz to save power, which may increase input latency. Uncheck to restore unlimited updates (0).")},
		{"tclient-mini-debug", Localizable("Show position and angle"), Localizable("Show your coordinates and aim angle on the HUD, or those of the tee you are watching. Free view shows coordinates only.")},
		{"tclient-fast-input", Localizable("Fast input"), Localizable("Use your latest input in local prediction before the next tick so movement appears sooner.")},
		{"tclient-antiping-negative-buffer", Localizable("Negative stability buffer"), Localizable("With improved smoothing, erratic movement keeps other tees near their server positions for longer. This can help in Gores but makes network jitter more visible.")},
		{"Tee collision (Tee to Tee)", Localizable("Tee collision"), Localizable("Tee to Tee")},
		{"Tee freeze probe (Tee to Freeze)", Localizable("Tee freeze probe"), Localizable("Mark the freeze check at the center and the four death checks around each Tee")},
		{"Enable FTAPI auto-translate (may overload the service)", Localizable("Enable FTAPI auto-translate"), Localizable("Allow FTAPI to auto-translate received chat messages. Busy chats can overload the free service. Outgoing auto-translation is controlled separately.")},
		{"qmclient-translate-llm-endpoint-optional", Localizable("Endpoint"), Localizable("Optional")},
		{"qmclient-translate-llm-concurrency", Localizable("Concurrency"), Localizable("0 = auto")},
		{"Enable thinking mode (slower)", Localizable("Enable thinking mode"), Localizable("Enable LLM thinking mode (may increase response time)")},
		{"Auto gain control for mic (AGC, experimental)", Localizable("Auto gain control for mic"), Localizable("This legacy switch is saved, but microphone capture currently uses only the manual mic volume.")},
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
inline SSettingsLabelPresentation ResolveSettingsLabelPresentation(const char *pId, const char *pFallbackText, TLocalize &&LocalizeText, bool ShowMetadataTip = true)
{
	if(const auto *pMetadata = SettingsLabelMetadata(pId))
		return {LocalizeText(pMetadata->m_pLabel), ShowMetadataTip ? LocalizeText(pMetadata->m_pTip) : nullptr, ShowMetadataTip ? pMetadata : nullptr};
	return {pFallbackText};
}

#endif
