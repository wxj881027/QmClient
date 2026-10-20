#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WHEEL_PRESETS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WHEEL_PRESETS_H

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace qm_wheel_presets
{

	enum class EType
	{
		NAME,
		IDENTITY,
		CHAT,
		ASSETS,
	};

	enum EIdentityField
	{
		NAME,
		CLAN,
		SKIN,
		CUSTOM_COLORS,
		BODY_COLOR,
		FEET_COLOR,
		COUNTRY,
		EYES,
		NUM_IDENTITY_FIELDS,
	};

	inline constexpr int NUM_ASSETS = 9;
	inline constexpr size_t COMMAND_CAPACITY = 1024;

	struct SIdentity
	{
		// 空 optional 表示保持原值；空字符串仍可用于清空战队。
		std::array<std::optional<std::string>, NUM_IDENTITY_FIELDS> m_aValues;
	};

	struct SPreset
	{
		EType m_Type = EType::NAME;
		std::array<SIdentity, 2> m_aTees;
		std::string m_Chat;
		bool m_TeamChat = false;
		std::array<std::string, NUM_ASSETS> m_aAssets;
	};

	const char *IdentityCommand(int Tee, int Field);
	const char *AssetCommand(int Index);
	bool BuildCommand(const SPreset &Preset, std::string &Command);
	std::optional<SPreset> ParseCommand(std::string_view Command);

} // namespace qm_wheel_presets

#endif
