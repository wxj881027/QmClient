#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WARLIST_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WARLIST_MODEL_H

#include <base/color.h>
#include <base/str.h>

#include <engine/shared/protocol.h>

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <game/client/QmUi/UiTokens.h>

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

class CQmWarListModel
{
	uint64_t m_Revision = 0;

public:
	CQmWarListModel() = default;
	~CQmWarListModel();
	CQmWarListModel(const CQmWarListModel &) = delete;
	CQmWarListModel &operator=(const CQmWarListModel &) = delete;

	// duplicate war types are NOT allowed
	std::vector<CWarType *> m_WarTypes = {
		new CWarType("none", ui_token::color::NEUTRAL_WHITE, false),
		new CWarType("enemy", ColorRGBA(1.0f, 0.2f, 0.2f, 1.0f), false),
		new CWarType("team", ColorRGBA(0.0f, 0.9f, 0.2f, 1.0f), false),
	};

	// None type war entries will float to the top of the list, so they can be assigned a type
	CWarType *m_pWarTypeNone = m_WarTypes[0];

	// Duplicate war entries ARE allowed
	std::vector<CWarEntry> m_vWarEntries;
	using TWarEntryLookup = std::unordered_map<std::string_view, std::vector<const CWarEntry *>>;
	TWarEntryLookup m_WarEntriesByName;
	TWarEntryLookup m_WarEntriesByClan;
	bool m_WarEntryLookupDirty = true;
	bool m_WarTypeIndicesDirty = true;

	uint64_t Revision() const { return m_Revision; }
	void UpdateWarEntry(int Index, const char *pName, const char *pClan, const char *pReason, CWarType *pType);
	void UpsertWarType(int Index, const char *pType, ColorRGBA Color);
	void AddWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType);
	void AddWarType(const char *pType, ColorRGBA Color);
	void RemoveWarEntry(const char *pName, const char *pClan, const char *pType);
	void RemoveWarEntry(CWarEntry *pEntry);
	void RemoveWarType(const char *pType);
	void RemoveWarEntryDuplicates(const char *pName, const char *pClan);
	CWarType *FindWarType(const char *pType);
	CWarEntry *FindWarEntry(const char *pName, const char *pClan, const char *pType);
	void MarkWarEntriesDirty();
	void MarkWarTypesDirty();
	void RefreshWarTypeIndices();
	void RebuildWarEntryLookup();
};

#endif
