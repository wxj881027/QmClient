#include "warlist_model.h"

#include <engine/shared/config.h>

#include <algorithm>
#include <new>

namespace
{
	template<typename T>
	void ReserveAppend(T &Values)
	{
		if(Values.size() < Values.capacity())
			return;
		if(Values.size() == Values.max_size())
			throw std::bad_alloc();
		// 提前准备提交槽位，仍按几何级数扩容，避免逐项reserve造成平方复制。
		const size_t Capacity = Values.size() > Values.max_size() / 2 ? Values.max_size() : std::max(size_t{1}, Values.size() * 2);
		Values.reserve(Capacity);
	}
}

CQmWarListModel::~CQmWarListModel() = default;

std::unique_ptr<CWarType> CQmWarListModel::PrepareWarType(const char *pType, ColorRGBA Color)
{
	auto pNewType = std::make_unique<CWarType>(pType, Color);
	ReserveAppend(m_vOwnedTypes);
	ReserveAppend(m_WarTypes);
	return pNewType;
}

void CQmWarListModel::PublishWarType(std::unique_ptr<CWarType> pType) noexcept
{
	CWarType *pBorrowed = pType.get();
	m_vOwnedTypes.push_back(std::move(pType));
	m_WarTypes.push_back(pBorrowed);
	MarkWarTypesDirty();
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

bool CQmWarListModel::UpsertWarType(int Index, const char *pType, ColorRGBA Color) noexcept
{
	if(str_comp(pType, "none") == 0)
		return true;

	if(Index >= 0 && Index < static_cast<int>(m_WarTypes.size()))
	{
		CWarType *pExisting = FindWarType(pType);
		if(pExisting != m_pWarTypeNone && pExisting != m_WarTypes[Index])
			return true;
		str_copy(m_WarTypes[Index]->m_aWarName, pType);
		m_WarTypes[Index]->m_Color = Color;
		MarkWarEntriesDirty();
	}
	else
	{
		return AddWarType(pType, Color);
	}
	return true;
}

bool CQmWarListModel::AddWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType) noexcept
{
	if(pName[0] == '\0' && pClan[0] == '\0')
		return true;
	// 输入可能借用现有条目，先复制固定值，再准备任何可重分配的容器。
	CWarEntry Entry(nullptr);
	str_copy(Entry.m_aReason, pReason);
	if(pClan[0] != '\0')
		str_copy(Entry.m_aClan, pClan);
	else
		str_copy(Entry.m_aName, pName);
	CWarType TypeName(pType);
	const bool HasDuplicates = !g_Config.m_QmWarListAllowDuplicates && std::any_of(m_vWarEntries.begin(), m_vWarEntries.end(), [&](const CWarEntry &Old) {
		return str_comp(Old.m_aName, pName) == 0 && str_comp(Old.m_aClan, pClan) == 0;
	});
	char aDuplicateName[MAX_NAME_LENGTH] = "";
	char aDuplicateClan[MAX_CLAN_LENGTH] = "";
	if(HasDuplicates)
	{
		str_copy(aDuplicateName, pName);
		str_copy(aDuplicateClan, pClan);
	}
	try
	{
		CWarType *pWarType = FindWarType(TypeName.m_aWarName);
		std::unique_ptr<CWarType> pNewType;
		if(pWarType == m_pWarTypeNone && str_comp(TypeName.m_aWarName, "none") != 0)
			pNewType = PrepareWarType(TypeName.m_aWarName, ColorRGBA(0, 0, 0, 1));
		// 条目扩容是最后一个可能失败的准备，失败时旧条目地址和索引仍有效。
		if(!HasDuplicates)
			ReserveAppend(m_vWarEntries);
		if(pNewType)
		{
			PublishWarType(std::move(pNewType));
			pWarType = FindWarType(TypeName.m_aWarName);
		}
		Entry.m_pWarType = pWarType;
		if(HasDuplicates)
			RemoveWarEntryDuplicates(aDuplicateName, aDuplicateClan);
		m_vWarEntries.push_back(Entry);
		MarkWarEntriesDirty();
		return true;
	}
	catch(...)
	{
		return false;
	}
}

bool CQmWarListModel::LoadWarEntry(const char *pName, const char *pClan, const char *pReason, const char *pType) noexcept
{
	const bool Prepared = AddWarEntry(pName, pClan, pReason, pType);
	m_ConfigIncomplete |= !Prepared;
	return Prepared;
}

bool CQmWarListModel::LoadWarType(int Index, const char *pType, ColorRGBA Color) noexcept
{
	const bool Prepared = UpsertWarType(Index, pType, Color);
	m_ConfigIncomplete |= !Prepared;
	return Prepared;
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

bool CQmWarListModel::AddWarType(const char *pType, ColorRGBA Color) noexcept
{
	if(str_comp(pType, "none") == 0)
		return true;
	CWarType *pExisting = FindWarType(pType);
	if(pExisting != m_pWarTypeNone)
	{
		pExisting->m_Color = Color;
		MarkWarEntriesDirty();
		return true;
	}
	try
	{
		PublishWarType(PrepareWarType(pType, Color));
		return true;
	}
	catch(...)
	{
		return false;
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
		CWarType *pRemoved = *Iter;
		m_WarTypes.erase(Iter);
		const auto Owner = std::find_if(m_vOwnedTypes.begin(), m_vOwnedTypes.end(), [pRemoved](const auto &pOwned) { return pOwned.get() == pRemoved; });
		if(Owner != m_vOwnedTypes.end())
			m_vOwnedTypes.erase(Owner);
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

bool CQmWarListModel::RebuildWarEntryLookup() const noexcept
{
	if(!m_WarEntryLookupDirty)
		return true;

	try
	{
		m_WarEntriesByName.clear();
		m_WarEntriesByClan.clear();
		m_WarEntriesByName.reserve(m_vWarEntries.size());
		m_WarEntriesByClan.reserve(m_vWarEntries.size());

		for(const CWarEntry &Entry : m_vWarEntries)
		{
			if(Entry.m_aName[0] != '\0')
				m_WarEntriesByName[Entry.m_aName].push_back(&Entry);
			else if(Entry.m_aClan[0] != '\0')
				m_WarEntriesByClan[Entry.m_aClan].push_back(&Entry);
		}

		m_WarEntryLookupDirty = false;
		return true;
	}
	catch(...)
	{
		m_WarEntriesByName.clear();
		m_WarEntriesByClan.clear();
		return false;
	}
}

bool CQmWarListModel::MatchesEnemy(const char *pName, const char *pClan) const
{
	RebuildWarEntryLookup();
	const CWarType *pEnemyType = m_WarTypes[WARLIST_BUILTIN_GROUP_ENEMY];
	bool Matched = false;
	VisitMatches(pName, pClan, [&](const CWarEntry &Entry, bool) {
		Matched = Entry.m_pWarType == pEnemyType;
		return !Matched;
	});
	return Matched;
}
