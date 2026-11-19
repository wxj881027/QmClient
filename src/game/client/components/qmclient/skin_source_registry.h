#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_REGISTRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_REGISTRY_H

#include <game/client/components/qmclient/skin_name_index.h>
#include <game/client/components/qmclient/skin_source.h>

#include <optional>
#include <string>
#include <unordered_map>

class IStorage;

// 记录 downloadedskins 中每个皮肤最后一次成功下载的来源，供设置页按来源过滤。
class CSkinSourceRegistry
{
public:
	static constexpr const char *FILE = "downloadedskins/skin_sources.json";

	void Load(IStorage *pStorage);
	bool Save(IStorage *pStorage);
	void Clear();

	std::optional<ESkinSource> Find(const char *pName) const;
	bool Set(const char *pName, ESkinSource Source);
	bool Empty() const { return m_Sources.empty(); }

private:
	std::unordered_map<std::string, ESkinSource, SSkinNameHash, SSkinNameEqual> m_Sources;
	bool m_Dirty = false;
};

#endif
