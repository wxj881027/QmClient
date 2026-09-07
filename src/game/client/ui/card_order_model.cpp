/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_order_model.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <utility>

namespace
{
bool EntryLess(const SCardOrderEntry *pLeft, const SCardOrderEntry *pRight)
{
	if(pLeft->m_Order != pRight->m_Order)
		return pLeft->m_Order < pRight->m_Order;
	return pLeft->m_Id < pRight->m_Id;
}

bool IsValidColumn(const ECardColumn Column)
{
	return Column == ECardColumn::FULL || Column == ECardColumn::LEFT || Column == ECardColumn::RIGHT;
}

bool IsValidEntry(const SCardOrderEntry &Entry)
{
	return !Entry.m_Id.empty() && !Entry.m_PageId.empty() && IsValidColumn(Entry.m_Column) && Entry.m_Order >= 0;
}

bool ContainsId(const std::vector<std::string> &vIds, const std::string_view Id)
{
	return std::any_of(vIds.begin(), vIds.end(), [&](const std::string &StoredId) { return StoredId == Id; });
}

bool ParseInteger(const std::string_view Value, int *pOut)
{
	if(Value.empty() || !pOut)
		return false;
	const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), *pOut);
	return Result.ec == std::errc{} && Result.ptr == Value.data() + Value.size();
}

bool ParseColumn(const std::string_view Value, ECardColumn *pOut)
{
	if(!pOut)
		return false;
	if(Value == "full" || Value == "0")
		*pOut = ECardColumn::FULL;
	else if(Value == "left" || Value == "1")
		*pOut = ECardColumn::LEFT;
	else if(Value == "right" || Value == "2")
		*pOut = ECardColumn::RIGHT;
	else
		return false;
	return true;
}

const SCardOrderEntry *FindEntry(const std::vector<SCardOrderEntry> &Entries, const std::string_view Id)
{
	for(const SCardOrderEntry &Entry : Entries)
		if(Entry.m_Id == Id)
			return &Entry;
	return nullptr;
}
}

void CCardOrderModel::SetDefaults(std::vector<SCardOrderEntry> Entries)
{
	m_vEntries.clear();
	m_vEntries.reserve(Entries.size());
	for(SCardOrderEntry &Entry : Entries)
	{
		if(IsValidEntry(Entry) && !FindEntry(m_vEntries, Entry.m_Id))
			m_vEntries.push_back(std::move(Entry));
	}
	Normalize();
	++m_StateRevision;
	m_Dirty = false;
}

bool CCardOrderModel::ApplyOverrides(const std::vector<SCardOrderEntry> &Overrides)
{
	bool Changed = false;
	std::vector<std::string> vSeenIds;
	for(const SCardOrderEntry &Override : Overrides)
	{
		if(!IsValidEntry(Override) || ContainsId(vSeenIds, Override.m_Id))
			continue;
		vSeenIds.push_back(Override.m_Id);
		SCardOrderEntry *pEntry = nullptr;
		for(SCardOrderEntry &Entry : m_vEntries)
			if(Entry.m_Id == Override.m_Id)
			{
				pEntry = &Entry;
				break;
			}
		if(!pEntry)
			continue;
		if(pEntry->m_PageId != Override.m_PageId || pEntry->m_Column != Override.m_Column || pEntry->m_Order != Override.m_Order)
		{
			*pEntry = Override;
			Changed = true;
		}
	}
	if(Changed)
	{
		Normalize();
		m_Dirty = true;
	}
	return Changed;
}

bool CCardOrderModel::LoadMerged(const std::string &Serialized, const std::vector<SCardOrderEntry> &Defaults)
{
	SetDefaults(Defaults);
	std::vector<SCardOrderEntry> Overrides;
	size_t Begin = 0;
	while(Begin < Serialized.size())
	{
		const size_t End = Serialized.find(';', Begin);
		const std::string_view Token(Serialized.data() + Begin, (End == std::string::npos ? Serialized.size() : End) - Begin);
		Begin = End == std::string::npos ? Serialized.size() : End + 1;
		if(Token.empty())
			continue;

		std::vector<std::string_view> Fields;
		size_t FieldBegin = 0;
		while(FieldBegin <= Token.size())
		{
			const size_t FieldEnd = Token.find('|', FieldBegin);
			Fields.emplace_back(Token.data() + FieldBegin, (FieldEnd == std::string::npos ? Token.size() : FieldEnd) - FieldBegin);
			if(FieldEnd == std::string::npos)
				break;
			FieldBegin = FieldEnd + 1;
		}

		SCardOrderEntry Entry;
		if(Fields.size() == 4)
		{
			Entry.m_Id = std::string(Fields[0]);
			Entry.m_PageId = std::string(Fields[1]);
			if(!ParseColumn(Fields[2], &Entry.m_Column) || !ParseInteger(Fields[3], &Entry.m_Order) || Entry.m_Order < 0)
				continue;
		}
		else
		{
			Fields.clear();
			FieldBegin = 0;
			while(FieldBegin <= Token.size())
			{
				const size_t FieldEnd = Token.find(':', FieldBegin);
				Fields.emplace_back(Token.data() + FieldBegin, (FieldEnd == std::string::npos ? Token.size() : FieldEnd) - FieldBegin);
				if(FieldEnd == std::string::npos)
					break;
				FieldBegin = FieldEnd + 1;
			}
			if(Fields.size() != 3)
				continue;
			Entry.m_Id = std::string(Fields[0]);
			if(!ParseColumn(Fields[1], &Entry.m_Column) || !ParseInteger(Fields[2], &Entry.m_Order) || Entry.m_Order < 0)
				continue;
			const SCardOrderEntry *pDefault = FindEntry(Defaults, Entry.m_Id);
			if(!pDefault)
				continue;
			Entry.m_PageId = pDefault->m_PageId;
		}

		const SCardOrderEntry *pDefault = FindEntry(Defaults, Entry.m_Id);
		if(!pDefault || !IsValidEntry(Entry) || FindEntry(Overrides, Entry.m_Id))
			continue;
		Overrides.push_back(std::move(Entry));
	}
	const bool Changed = ApplyOverrides(Overrides);
	ClearDirty();
	return Changed;
}

std::string CCardOrderModel::Serialize() const
{
	std::string Result;
	for(const SCardOrderEntry &Entry : m_vEntries)
	{
		if(!IsValidEntry(Entry))
			continue;
		if(!Result.empty())
			Result.push_back(';');
		Result += Entry.m_Id;
		Result.push_back('|');
		Result += Entry.m_PageId;
		Result.push_back('|');
		Result += Entry.m_Column == ECardColumn::FULL ? "full" : Entry.m_Column == ECardColumn::LEFT ? "left" : "right";
		Result.push_back('|');
		Result += std::to_string(Entry.m_Order);
	}
	if(!Result.empty())
		Result.push_back(';');
	return Result;
}

bool CCardOrderModel::Move(const std::string &Id, const ECardColumn Column, const int Order)
{
	const SCardOrderEntry *pEntry = Find(Id);
	return pEntry && MoveToPage(Id, pEntry->m_PageId, Column, Order);
}

bool CCardOrderModel::MoveToPage(const std::string &Id, const std::string &PageId, const ECardColumn Column, const int Order)
{
	if(Id.empty() || PageId.empty() || !IsValidColumn(Column) || Order < 0)
		return false;
	for(SCardOrderEntry &Entry : m_vEntries)
	{
		if(Entry.m_Id != Id)
			continue;

		const std::string FromPageId = Entry.m_PageId;
		const ECardColumn FromColumn = Entry.m_Column;
		std::vector<SCardOrderEntry *> vTarget;
		for(SCardOrderEntry &Other : m_vEntries)
			if(&Other != &Entry && Other.m_PageId == PageId && Other.m_Column == Column)
				vTarget.push_back(&Other);
		std::sort(vTarget.begin(), vTarget.end(), EntryLess);
		const int TargetOrder = std::min(Order, static_cast<int>(vTarget.size()));

		if(FromPageId == PageId && FromColumn == Column)
		{
			std::vector<SCardOrderEntry *> vCurrent = vTarget;
			vCurrent.insert(vCurrent.begin() + TargetOrder, &Entry);
			bool SameOrder = true;
			for(size_t Index = 0; Index < vCurrent.size(); ++Index)
				SameOrder = SameOrder && vCurrent[Index]->m_Order == static_cast<int>(Index);
			if(SameOrder)
				return false;
		}

		Entry.m_PageId = PageId;
		Entry.m_Column = Column;
		vTarget.insert(vTarget.begin() + TargetOrder, &Entry);
		for(size_t Index = 0; Index < vTarget.size(); ++Index)
			vTarget[Index]->m_Order = static_cast<int>(Index);

		if(FromPageId != PageId || FromColumn != Column)
		{
			std::vector<SCardOrderEntry *> vSource;
			for(SCardOrderEntry &Other : m_vEntries)
				if(&Other != &Entry && Other.m_PageId == FromPageId && Other.m_Column == FromColumn)
					vSource.push_back(&Other);
			std::sort(vSource.begin(), vSource.end(), EntryLess);
			for(size_t Index = 0; Index < vSource.size(); ++Index)
				vSource[Index]->m_Order = static_cast<int>(Index);
		}

		m_Dirty = true;
		++m_LayoutRevision;
		return true;
	}
	return false;
}

void CCardOrderModel::Normalize()
{
	std::vector<SCardOrderEntry *> vEntries;
	vEntries.reserve(m_vEntries.size());
	for(SCardOrderEntry &Entry : m_vEntries)
		vEntries.push_back(&Entry);
	std::sort(vEntries.begin(), vEntries.end(), [](const SCardOrderEntry *pLeft, const SCardOrderEntry *pRight) {
		if(pLeft->m_PageId != pRight->m_PageId)
			return pLeft->m_PageId < pRight->m_PageId;
		if(pLeft->m_Column != pRight->m_Column)
			return static_cast<int>(pLeft->m_Column) < static_cast<int>(pRight->m_Column);
		return EntryLess(pLeft, pRight);
	});

	std::string_view CurrentPageId;
	ECardColumn CurrentColumn = ECardColumn::FULL;
	int CurrentOrder = 0;
	bool First = true;
	for(SCardOrderEntry *pEntry : vEntries)
	{
		if(First || CurrentPageId != std::string_view(pEntry->m_PageId) || CurrentColumn != pEntry->m_Column)
		{
			CurrentPageId = pEntry->m_PageId;
			CurrentColumn = pEntry->m_Column;
			CurrentOrder = 0;
			First = false;
		}
		pEntry->m_Order = CurrentOrder++;
	}
	++m_LayoutRevision;
}

const SCardOrderEntry *CCardOrderModel::Find(const std::string &Id) const
{
	for(const SCardOrderEntry &Entry : m_vEntries)
		if(Entry.m_Id == Id)
			return &Entry;
	return nullptr;
}

std::vector<const SCardOrderEntry *> CCardOrderModel::EntriesForPage(const std::string &PageId, const ECardColumn Column) const
{
	std::vector<const SCardOrderEntry *> Result;
	for(const SCardOrderEntry &Entry : m_vEntries)
		if(Entry.m_PageId == PageId && Entry.m_Column == Column)
			Result.push_back(&Entry);
	std::sort(Result.begin(), Result.end(), EntryLess);
	return Result;
}
