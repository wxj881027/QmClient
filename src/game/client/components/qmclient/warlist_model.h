#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WARLIST_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WARLIST_MODEL_H

#include <base/color.h>
#include <base/str.h>

#include <engine/shared/protocol.h>

#include <game/client/QmUi/UiTokens.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum
{
	MAX_WARLIST_TYPE_LENGTH = 16,
	MAX_WARLIST_IMPORT_ID_LENGTH = 16,
	MAX_WARLIST_REASON_LENGTH = 256,
	WARLIST_BUILTIN_GROUP_NONE = 0,
	WARLIST_BUILTIN_GROUP_ENEMY = 1,
	WARLIST_BUILTIN_GROUP_TEAM = 2
};

class CWarType
{
public:
	// Intentionally named redundantly because it was confusing otherwise
	char m_aWarName[MAX_WARLIST_TYPE_LENGTH] = "";
	ColorRGBA m_Color = ui_token::color::NEUTRAL_WHITE;
	bool m_Removable = true;
	bool m_Imported = false;
	int m_Index = 0;
	CWarType(const char *pName, ColorRGBA Color = ui_token::color::NEUTRAL_WHITE, bool Removable = true, bool IsImport = false)
	{
		str_copy(m_aWarName, pName);
		m_Color = Color;
		m_Removable = Removable;
		m_Imported = IsImport;
	}

	bool operator==(const CWarType &Other) const
	{
		return str_comp(m_aWarName, Other.m_aWarName) == 0;
	}
};

class CWarEntry
{
public:
	// a war entry can have a name, or clan, but not both
	// name matches the user with that name
	// clan matches all users with that clan
	char m_aName[MAX_NAME_LENGTH] = "";
	char m_aClan[MAX_CLAN_LENGTH] = "";
	char m_aReason[MAX_WARLIST_REASON_LENGTH] = "";

	CWarType *m_pWarType = nullptr;
	bool m_Imported = false;

	CWarEntry(CWarType *pWarType)
	{
		m_pWarType = pWarType;
	}

	CWarEntry(CWarType *pWarType, const char *pName, const char *pClan, const char *pReason, bool IsImport = false)
	{
		m_pWarType = pWarType;
		str_copy(m_aName, pName);
		str_copy(m_aClan, pClan);
		str_copy(m_aReason, pReason);
		m_Imported = IsImport;
	}

	bool operator==(const CWarEntry &Other) const
	{
		bool NameMatch = str_comp(m_aName, Other.m_aName) == 0 && str_comp(m_aName, "") != 0;
		bool ClanMatch = str_comp(m_aClan, Other.m_aClan) == 0 && str_comp(m_aClan, "") != 0;
		return (NameMatch || ClanMatch) && m_pWarType == Other.m_pWarType;
	}
};

class CWarDataCache
{
public:
	ColorRGBA m_NameColor = ColorRGBA(1, 1, 1, 1);
	ColorRGBA m_ClanColor = ColorRGBA(1, 1, 1, 1);
	bool m_WarName = false;
	bool m_WarClan = false;
	std::vector<char> m_WarGroupMatches = {false, false, false};
	char m_aReason[MAX_WARLIST_REASON_LENGTH] = "";

	bool Reset(size_t WarTypeCount) noexcept
	{
		bool Prepared = true;
		try
		{
			m_WarGroupMatches.assign(std::max(WarTypeCount, size_t{3}), false);
		}
		catch(...)
		{
			// 成功构造后始终保有三个内置槽，缩回它们不需要新分配。
			m_WarGroupMatches.resize(3);
			std::fill(m_WarGroupMatches.begin(), m_WarGroupMatches.end(), false);
			Prepared = false;
		}
		m_WarName = m_WarClan = false;
		m_aReason[0] = '\0';
		m_NameColor = m_ClanColor = ColorRGBA(1, 1, 1, 1);
		return Prepared;
	}

	void ApplyMatch(const CWarEntry &Entry, bool ByName)
	{
		if(ByName)
		{
			str_copy(m_aReason, Entry.m_aReason);
			m_WarName = true;
			m_NameColor = Entry.m_pWarType->m_Color;
		}
		else
		{
			if(!m_WarName)
				str_copy(m_aReason, Entry.m_aReason);
			m_WarClan = true;
			m_ClanColor = Entry.m_pWarType->m_Color;
		}
		m_WarGroupMatches[Entry.m_pWarType->m_Index] = true;
	}
};

class CQmWarListModel
{
	uint64_t m_Revision = 0;
	std::array<CWarType, 3> m_aBuiltinTypes = {
		CWarType("none", ui_token::color::NEUTRAL_WHITE, false),
		CWarType("enemy", ColorRGBA(1.0f, 0.2f, 0.2f, 1.0f), false),
		CWarType("team", ColorRGBA(0.0f, 0.9f, 0.2f, 1.0f), false)};
	std::vector<std::unique_ptr<CWarType>> m_vOwnedTypes;
	bool m_ConfigIncomplete = false;
	std::unique_ptr<CWarType> PrepareWarType(const char *pType, ColorRGBA Color);
	void PublishWarType(std::unique_ptr<CWarType> pType) noexcept;

public:
	CQmWarListModel() = default;
	~CQmWarListModel();
	CQmWarListModel(const CQmWarListModel &) = delete;
	CQmWarListModel &operator=(const CQmWarListModel &) = delete;

	// 公开列表只借用稳定类型对象；内置类型按值持有，自定义类型由模型独占。
	std::vector<CWarType *> m_WarTypes = {
		&m_aBuiltinTypes[0],
		&m_aBuiltinTypes[1],
		&m_aBuiltinTypes[2],
	};

	// None type war entries will float to the top of the list, so they can be assigned a type
	CWarType *m_pWarTypeNone = m_WarTypes[0];

	// Duplicate war entries ARE allowed
	std::vector<CWarEntry> m_vWarEntries;
	struct SWarEntryKeyHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view Key) const noexcept { return std::hash<std::string_view>{}(Key); }
	};
	// 键独占文本，查询仍透明借用；只有结果指针随条目编辑失效。
	using TWarEntryLookup = std::unordered_map<std::string, std::vector<const CWarEntry *>, SWarEntryKeyHash, std::equal_to<>>;
	mutable TWarEntryLookup m_WarEntriesByName;
	mutable TWarEntryLookup m_WarEntriesByClan;
	mutable bool m_WarEntryLookupDirty = true;
	bool m_WarTypeIndicesDirty = true;

	uint64_t Revision() const { return m_Revision; }
	void UpdateWarEntry(int Index, const char *pName, const char *pClan, const char *pReason, CWarType *pType);
	// false只表示准备失败；原来的校验忽略仍视为已处理，不改变配置命令语义。
	bool UpsertWarType(int Index, const char *pType, ColorRGBA Color) noexcept;
	bool AddWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType) noexcept;
	bool AddWarType(const char *pType, ColorRGBA Color) noexcept;
	bool LoadWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType) noexcept;
	bool LoadWarType(int Index, const char *pType, ColorRGBA Color) noexcept;
	bool ConfigIncomplete() const { return m_ConfigIncomplete; }
	void RemoveWarEntry(const char *pName, const char *pClan, const char *pType);
	void RemoveWarEntry(CWarEntry *pEntry);
	void RemoveWarType(const char *pType);
	void RemoveWarEntryDuplicates(const char *pName, const char *pClan);
	CWarType *FindWarType(const char *pType);
	CWarEntry *FindWarEntry(const char *pName, const char *pClan, const char *pType);
	void MarkWarEntriesDirty();
	void MarkWarTypesDirty();
	void RefreshWarTypeIndices();
	bool RebuildWarEntryLookup() const noexcept;
	// 脏索引按原名单无分配匹配；Name先于Clan，回调返回false时停止。
	template<typename F>
	void VisitMatches(const char *pName, const char *pClan, F &&Visit) const
	{
		const auto Domain = [&](const char *pValue, bool ByName, const TWarEntryLookup &Lookup) {
			if(pValue[0] == '\0')
				return true;
			if(!m_WarEntryLookupDirty)
			{
				const auto It = Lookup.find(std::string_view(pValue));
				if(It != Lookup.end())
					for(const auto *pEntry : It->second)
						if(!Visit(*pEntry, ByName))
							return false;
			}
			else
			{
				for(const auto &Entry : m_vWarEntries)
				{
					const char *pKey = ByName ? Entry.m_aName : (Entry.m_aName[0] == '\0' ? Entry.m_aClan : "");
					if(pKey[0] != '\0' && str_comp(pKey, pValue) == 0 && !Visit(Entry, ByName))
						return false;
				}
			}
			return true;
		};
		if(Domain(pName, true, m_WarEntriesByName))
			Domain(pClan, false, m_WarEntriesByClan);
	}
	bool MatchesEnemy(const char *pName, const char *pClan) const;
};

#endif
