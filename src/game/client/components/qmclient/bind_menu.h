#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MENU_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MENU_H

#include <base/str.h>

#include <cstddef>
#include <optional>
#include <string_view>

namespace qm_bind_menu
{
	enum class EMenu
	{
		IDENTITY,
		CHAT,
		ASSETS,
		CUSTOM,
		NUM_MENUS,
	};

	enum class ELayout
	{
		WHEEL,
		PANEL,
	};

	inline constexpr int NUM_MENUS = static_cast<int>(EMenu::NUM_MENUS);

	inline const char *Name(EMenu Menu)
	{
		switch(Menu)
		{
		case EMenu::IDENTITY: return "identity";
		case EMenu::CHAT: return "chat";
		case EMenu::ASSETS: return "assets";
		case EMenu::CUSTOM: return "custom";
		default: return "";
		}
	}

	inline const char *LocalizedName(EMenu Menu)
	{
		switch(Menu)
		{
		case EMenu::IDENTITY: return "Identity";
		case EMenu::CHAT: return "Chat";
		case EMenu::ASSETS: return "Assets";
		case EMenu::CUSTOM: return "Custom";
		default: return "";
		}
	}

	inline bool HasHoverDetails(EMenu Menu)
	{
		// 身份行的名字、战队和本体/分身两列已经完整展示，不再叠一层悬浮详情。
		return Menu == EMenu::CHAT || Menu == EMenu::ASSETS || Menu == EMenu::CUSTOM;
	}

	inline std::optional<EMenu> Parse(std::string_view Text)
	{
		for(int Index = 0; Index < NUM_MENUS; ++Index)
		{
			const EMenu Menu = static_cast<EMenu>(Index);
			const size_t NameLength = str_length(Name(Menu));
			if(Text.size() == NameLength && str_comp_nocase_num(Text.data(), Name(Menu), Text.size()) == 0)
				return Menu;
		}
		return std::nullopt;
	}

	inline const char *TriggerCommand(EMenu Menu)
	{
		switch(Menu)
		{
		case EMenu::IDENTITY: return "+bindmenu identity";
		case EMenu::CHAT: return "+bindmenu chat";
		case EMenu::ASSETS: return "+bindmenu assets";
		case EMenu::CUSTOM: return "+bindmenu custom";
		default: return "";
		}
	}
} // namespace qm_bind_menu

#endif
