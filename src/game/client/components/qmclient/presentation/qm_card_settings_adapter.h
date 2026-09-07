/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PRESENTATION_QM_CARD_SETTINGS_ADAPTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PRESENTATION_QM_CARD_SETTINGS_ADAPTER_H

#include <game/client/ui/card_settings_adapter.h>

class CConfig;

class CQmCardSettingsAdapter final : public ICardSettingsAdapter
{
	CConfig &m_Config;

public:
	explicit CQmCardSettingsAdapter(CConfig &Config) : m_Config(Config) {}
	std::optional<SCardSettingSnapshot> Read(const SCardDescriptor &Card) const override;
	ECardSettingResult Apply(const SCardDescriptor &Card, int Value) override;
};

bool RegisterQmSettingsAdapterCards(CCardRegistry &Registry);

#endif
