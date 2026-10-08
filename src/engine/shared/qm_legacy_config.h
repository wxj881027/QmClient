#ifndef ENGINE_SHARED_QM_LEGACY_CONFIG_H
#define ENGINE_SHARED_QM_LEGACY_CONFIG_H

#include <string>
#include <string_view>
#include <unordered_set>

namespace QmLegacyConfig
{
	// 未命中映射时返回原指针；命中时返回静态存储的新名称。
	const char *CanonicalName(const char *pName);
	std::string MigrateBindCommand(std::string_view Command);

	class CWritePrecedence
	{
		std::unordered_set<std::string_view> m_CanonicalWrites;

	public:
		// 新名称一旦显式赋值，后续读取旧配置不再覆盖；交互式旧命令仍有效。
		bool ShouldExecute(const char *pName, bool HasValue, bool FromFile);
	};
}

#endif
