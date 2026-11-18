#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_NAME_INDEX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_NAME_INDEX_H

#include <base/str.h>

#include <cstddef>
#include <string>
#include <unordered_map>

inline bool SkinNameHasUpperCase(const char *pName)
{
	if(pName == nullptr)
		return false;
	for(const char *pCursor = pName; *pCursor != '\0'; ++pCursor)
		if(*pCursor >= 'A' && *pCursor <= 'Z')
			return true;
	return false;
}

/**
 * 皮肤名身份索引。
 *
 * 皮肤名在协议和磁盘文件名中保留原始大小写；查找和下载使用不区分大小写的身份键。
 * 索引保存身份键对应的 canonical 名称，下载 URL、缓存路径和显示名优先使用该名称。
 */
class CSkinNameIndex
{
public:
	static std::string Key(const char *pName)
	{
		if(pName == nullptr)
			return {};
		char aLower[256];
		str_utf8_tolower(pName, aLower, sizeof(aLower));
		return aLower;
	}

	void Clear() { m_Entries.clear(); }

	bool Add(const char *pName, int Priority = 0)
	{
		if(pName == nullptr || pName[0] == '\0')
			return false;
		const std::string NameKey = Key(pName);
		const auto Existing = m_Entries.find(NameKey);
		if(Existing != m_Entries.end())
		{
			if(Existing->second.m_Priority > Priority)
				return false;
			if(Existing->second.m_Priority == Priority &&
				(SkinNameHasUpperCase(Existing->second.m_Name.c_str()) || !SkinNameHasUpperCase(pName)))
				return false;
		}
		m_Entries[NameKey] = SEntry{pName, Priority};
		return true;
	}

	const char *Find(const char *pName) const
	{
		if(pName == nullptr)
			return nullptr;
		const auto Entry = m_Entries.find(Key(pName));
		return Entry == m_Entries.end() ? nullptr : Entry->second.m_Name.c_str();
	}

private:
	struct SEntry
	{
		std::string m_Name;
		int m_Priority = 0;
	};

	std::unordered_map<std::string, SEntry> m_Entries;
};

struct SSkinNameHash
{
	std::size_t operator()(const std::string &Name) const
	{
		return std::hash<std::string>{}(CSkinNameIndex::Key(Name.c_str()));
	}
};

struct SSkinNameEqual
{
	bool operator()(const std::string &Left, const std::string &Right) const
	{
		return str_utf8_comp_nocase(Left.c_str(), Right.c_str()) == 0;
	}
};

#endif
