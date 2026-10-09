#ifndef GAME_CLIENT_QMUI_UICONFIGHINT_H
#define GAME_CLIENT_QMUI_UICONFIGHINT_H

#include <engine/shared/config.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

// 从配置声明生成绑定表，控件不维护另一份命令名或读取配置值。
inline const char *QmUiConfigCommand(const CConfig &Config, const void *pValue)
{
	struct SBinding
	{
		size_t m_Offset;
		const char *m_pCommand;
		int m_Flags;
	};
	static_assert(std::is_standard_layout_v<CConfig>);
	static constexpr SBinding s_aBindings[] = {
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) {offsetof(CConfig, m_##Name), #ScriptName, Flags},
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) {offsetof(CConfig, m_##Name), #ScriptName, Flags},
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) {offsetof(CConfig, m_##Name), #ScriptName, Flags},
#define SET_CONFIG_DOMAIN(Domain)
#include <engine/shared/config_includes.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
#undef SET_CONFIG_DOMAIN
	};
	const uintptr_t Base = reinterpret_cast<uintptr_t>(&Config);
	const uintptr_t Value = reinterpret_cast<uintptr_t>(pValue);
	if(pValue == nullptr || Value < Base || Value - Base >= sizeof(Config))
		return nullptr;
	const size_t Offset = Value - Base;
	const auto *pBinding = std::lower_bound(std::begin(s_aBindings), std::end(s_aBindings), Offset,
		[](const SBinding &Binding, size_t Position) { return Binding.m_Offset < Position; });
	return pBinding != std::end(s_aBindings) && pBinding->m_Offset == Offset && (pBinding->m_Flags & CFGFLAG_CLIENT) != 0 ? pBinding->m_pCommand : nullptr;
}

#endif
