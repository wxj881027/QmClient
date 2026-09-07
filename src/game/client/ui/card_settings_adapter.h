/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_SETTINGS_ADAPTER_H
#define GAME_CLIENT_UI_CARD_SETTINGS_ADAPTER_H

#include "card_registry.h"

#include <optional>

struct SCardSettingSnapshot
{
	int m_Value = 0;
	int m_DefaultValue = 0;
	int m_Min = 0;
	int m_Max = 1;
	bool m_RestartRequired = false;
};

enum class ECardSettingResult
{
	APPLIED,
	UNSUPPORTED,
	INVALID_VALUE,
};

// presentation 只读快照并提交显式动作；配置与 feature 状态仍由原 owner 管理。
class ICardSettingsAdapter
{
public:
	virtual ~ICardSettingsAdapter() = default;
	virtual std::optional<SCardSettingSnapshot> Read(const SCardDescriptor &Card) const = 0;
	virtual ECardSettingResult Apply(const SCardDescriptor &Card, int Value) = 0;
};

#endif
