#include "warlist_model.h"

#include <engine/shared/config.h>

#include <algorithm>

CQmWarListModel::~CQmWarListModel()
{
	for(CWarType *pType : m_WarTypes)
		delete pType;
}

void CQmWarListModel::UpdateWarEntry(int Index, const char *pName, const char *pClan, const char *pReason, CWarType *pType)
{
	if(Index >= 0 && Index < static_cast<int>(m_vWarEntries.size()))
	{
		str_copy(m_vWarEntries[Index].m_aName, pName);
		str_copy(m_vWarEntries[Index].m_aClan, pClan);
		str_copy(m_vWarEntries[Index].m_aReason, pReason);
		m_vWarEntries[Index].m_pWarType = pType;
		MarkWarEntriesDirty();
	}
}

void CQmWarListModel::UpsertWarType(int Index, const char *pType, ColorRGBA Color)
{
	if(str_comp(pType, "none") == 0)
		return;

	if(Index >= 0 && Index < static_cast<int>(m_WarTypes.size()))
	{
		CWarType *pExisting = FindWarType(pType);
		if(pExisting != m_pWarTypeNone && pExisting != m_WarTypes[Index])
			return;
		str_copy(m_WarTypes[Index]->m_aWarName, pType);
		m_WarTypes[Index]->m_Color = Color;
		MarkWarEntriesDirty();
	}
	else
	{
		AddWarType(pType, Color);
	}
}

void CQmWarListModel::AddWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType)
{
	if(str_comp(pName, "") == 0 && str_comp(pClan, "") == 0)
		return;

	CWarType *WarType = FindWarType(pType);
	if(WarType == m_pWarTypeNone)
	{
		AddWarType(pType, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
		WarType = FindWarType(pType);
	}

	CWarEntry Entry(WarType);
	str_copy(Entry.m_aReason, pReason);

	if(str_comp(pClan, "") != 0)
		str_copy(Entry.m_aClan, pClan);
	else if(str_comp(pName, "") != 0)
		str_copy(Entry.m_aName, pName);

	if(!g_Config.m_QmWarListAllowDuplicates)
		RemoveWarEntryDuplicates(pName, pClan);
	m_vWarEntries.push_back(Entry);
	MarkWarEntriesDirty();
}

void CQmWarListModel::RemoveWarEntryDuplicates(const char *pName, const char *pClan)
{
	if(str_comp(pName, "") == 0 && str_comp(pClan, "") == 0)
		return;

	bool RemovedAny = false;
	for(auto Iter = m_vWarEntries.begin(); Iter != m_vWarEntries.end();)
	{
		bool IsDuplicate =
			(str_comp(Iter->m_aName, pName) == 0) &&
			(str_comp(Iter->m_aClan, pClan) == 0);

		if(IsDuplicate)
		{
			Iter = m_vWarEntries.erase(Iter);
			RemovedAny = true;
		}
		else
		{
			++Iter;
		}
	}

	if(RemovedAny)
		MarkWarEntriesDirty();
}

void CQmWarListModel::AddWarType(const char *pType, ColorRGBA Color)
{
	if(str_comp(pType, "none") == 0)
		return;

	CWarType *Type = FindWarType(pType);
	if(Type == m_pWarTypeNone)
	{
		CWarType *NewType = new CWarType(pType, Color);
		m_WarTypes.push_back(NewType);
		MarkWarTypesDirty();
	}
	else
	{
		Type->m_Color = Color;
		MarkWarEntriesDirty();
	}
}

void CQmWarListModel::RemoveWarEntry(const char *pName, const char *pClan, const char *pType)
{
	CWarType *pWarType = FindWarType(pType);
	CWarEntry Entry(pWarType, pName, pClan, "");
	auto Iter = std::find(m_vWarEntries.begin(), m_vWarEntries.end(), Entry);
	if(Iter != m_vWarEntries.end())
	{
		m_vWarEntries.erase(Iter);
		MarkWarEntriesDirty();
	}
}

void CQmWarListModel::RemoveWarEntry(CWarEntry *Entry)
{
	auto Iter = std::find_if(m_vWarEntries.begin(), m_vWarEntries.end(),
		[Entry](const CWarEntry &WarEntry) { return &WarEntry == Entry; });
	if(Iter != m_vWarEntries.end())
	{
		m_vWarEntries.erase(Iter);
		MarkWarEntriesDirty();
	}
}

void CQmWarListModel::RemoveWarType(const char *pType)
{
	CWarType Type(pType);

	auto Iter = std::find_if(m_WarTypes.begin(), m_WarTypes.end(),
		[&Type](CWarType *pWarTypePtr) { return *pWarTypePtr == Type; });
	if(Iter != m_WarTypes.end())
	{
		// Don't remove default war types
		if(!(*Iter)->m_Removable)
			return;

		// Find all war entries and set them to None if they are using this type
		for(CWarEntry &Entry : m_vWarEntries)
		{
			if(*Entry.m_pWarType == **Iter)
			{
				Entry.m_pWarType = m_pWarTypeNone;
			}
		}
		delete *Iter;
		m_WarTypes.erase(Iter);
		MarkWarTypesDirty();
	}
}

CWarType *CQmWarListModel::FindWarType(const char *pType)
{
	CWarType Type(pType);
	auto Iter = std::find_if(m_WarTypes.begin(), m_WarTypes.end(),
		[&Type](CWarType *pWarTypePtr) { return *pWarTypePtr == Type; });
	if(Iter != m_WarTypes.end())
		return *Iter;
	else
		return m_pWarTypeNone;
}

CWarEntry *CQmWarListModel::FindWarEntry(const char *pName, const char *pClan, const char *pType)
{
	CWarType *pWarType = FindWarType(pType);
	CWarEntry Entry(pWarType, pName, pClan, "");
	auto Iter = std::find(m_vWarEntries.begin(), m_vWarEntries.end(), Entry);

	if(Iter != m_vWarEntries.end())
		return &(*Iter);
	else
		return nullptr;
}

void CQmWarListModel::MarkWarEntriesDirty()
{
	m_WarEntryLookupDirty = true;
	++m_Revision;
}

void CQmWarListModel::MarkWarTypesDirty()
{
	m_WarTypeIndicesDirty = true;
	MarkWarEntriesDirty();
}

void CQmWarListModel::RefreshWarTypeIndices()
{
	if(!m_WarTypeIndicesDirty)
		return;

	for(int i = 0; i < (int)m_WarTypes.size(); ++i)
		m_WarTypes[i]->m_Index = i;

	m_WarTypeIndicesDirty = false;
}

void CQmWarListModel::RebuildWarEntryLookup() const
{
	if(!m_WarEntryLookupDirty)
		return;

	m_WarEntriesByName.clear();
	m_WarEntriesByClan.clear();
	m_WarEntriesByName.reserve(m_vWarEntries.size());
	m_WarEntriesByClan.reserve(m_vWarEntries.size());

	for(const CWarEntry &Entry : m_vWarEntries)
	{
		if(Entry.m_aName[0] != '\0')
			m_WarEntriesByName[std::string_view(Entry.m_aName)].push_back(&Entry);
		else if(Entry.m_aClan[0] != '\0')
			m_WarEntriesByClan[std::string_view(Entry.m_aClan)].push_back(&Entry);
	}

	m_WarEntryLookupDirty = false;
}

bool CQmWarListModel::MatchesEnemy(const char *pName, const char *pClan) const
{
	// 按当前身份查询，名单编辑和玩家改名不必等下一次快照才能生效。
	RebuildWarEntryLookup();
	const CWarType *pEnemyType = m_WarTypes[WARLIST_BUILTIN_GROUP_ENEMY];
	const auto Matches = [pEnemyType](const TWarEntryLookup &Lookup, const char *pValue) {
		if(pValue[0] == '\0')
			return false;
		const auto It = Lookup.find(std::string_view(pValue));
		return It != Lookup.end() && std::any_of(It->second.begin(), It->second.end(), [pEnemyType](const CWarEntry *pEntry) {
			return pEntry->m_pWarType == pEnemyType;
		});
	};
	return Matches(m_WarEntriesByName, pName) || Matches(m_WarEntriesByClan, pClan);
}
