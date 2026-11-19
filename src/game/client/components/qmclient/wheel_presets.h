#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WHEEL_PRESETS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WHEEL_PRESETS_H

#include "bind_menu.h"

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
	const char *AssetCategoryName(int Index);
	qm_bind_menu::EMenu MenuForType(EType Type);
	std::optional<qm_bind_menu::EMenu> MenuForCommand(std::string_view Command);
	bool BuildCommand(const SPreset &Preset, std::string &Command);
	std::optional<SPreset> ParseCommand(std::string_view Command);
	// 旧版轮盘条目常把身份、皮肤和资源写进独立 cfg，再由 exec 调用。
	// 只读取可识别的配置行用于预览，不改变原 exec 命令的执行方式。
	std::optional<SPreset> ParseLegacyConfig(std::string_view Text);
	std::optional<std::string> ExecPath(std::string_view Command);

} // namespace qm_wheel_presets

#endif
