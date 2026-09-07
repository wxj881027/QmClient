/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_ui_model.h"

#include <algorithm>
#include <unordered_set>

bool CCardUiModel::SetPreferences(const std::string &CardId, SCardUiPreferences Value)
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	const SCardOrderEntry *pPlacement = m_OrderModel.Find(CardId);
	if(!pCard || !pPlacement || Value.m_Order < 0)
		return false;
	const SCardUiPreferences Previous = Preferences(CardId);
	const bool Moved = m_OrderModel.Move(CardId, pPlacement->m_Column, Value.m_Order);
	if(Previous.m_Visible == Value.m_Visible && Previous.m_Collapsed == Value.m_Collapsed && !Moved)
		return true;
	if(Value.m_Visible == pCard->m_DefaultVisible && Value.m_Collapsed == pCard->m_DefaultCollapsed)
		m_Preferences.erase(CardId);
	else
		m_Preferences[CardId] = {Value.m_Visible, Value.m_Collapsed};
	m_Dirty = true;
	++m_Revision;
	return true;
}

SCardUiPreferences CCardUiModel::Preferences(const std::string &CardId) const
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	const SCardOrderEntry *pPlacement = m_OrderModel.Find(CardId);
	if(!pCard || !pPlacement)
		return {};
	const auto It = m_Preferences.find(CardId);
	if(It != m_Preferences.end())
		return {It->second.m_Visible, It->second.m_Collapsed, pPlacement->m_Order};
	return {pCard->m_DefaultVisible, pCard->m_DefaultCollapsed, pPlacement->m_Order};
}

SCardModelSnapshot CCardUiModel::Snapshot(const std::string &CardId) const
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	if(!pCard)
		return {};
	const SFeatureModel *pFeature = pCard->m_FeatureId.empty() ? nullptr : m_Registry.FindFeature(pCard->m_FeatureId);
	return {pCard, Preferences(CardId), !pFeature || pFeature->m_Enabled, !pFeature || pFeature->m_Available};
}

std::vector<std::pair<std::string, SCardUiPreferences>> CCardUiModel::ExportPreferences() const
{
	const CCardOrderModel Defaults = m_Registry.BuildDefaultOrderModel();
	std::vector<std::pair<std::string, SCardUiPreferences>> vResult;
	for(const SCardOrderEntry &Entry : m_OrderModel.Entries())
	{
		const SCardOrderEntry *pDefault = Defaults.Find(Entry.m_Id);
		if(m_Preferences.find(Entry.m_Id) != m_Preferences.end() || !pDefault ||
			Entry.m_PageId != pDefault->m_PageId || Entry.m_Column != pDefault->m_Column || Entry.m_Order != pDefault->m_Order)
			vResult.emplace_back(Entry.m_Id, Preferences(Entry.m_Id));
	}
	std::sort(vResult.begin(), vResult.end(), [](const auto &Left, const auto &Right) { return Left.first < Right.first; });
	return vResult;
}

bool CCardUiModel::ImportPreferences(const std::vector<std::pair<std::string, SCardUiPreferences>> &vPreferences)
{
	std::unordered_set<std::string> Seen;
	auto Updated = m_Preferences;
	std::vector<SCardOrderEntry> vPlacements;
	for(const auto &Entry : vPreferences)
	{
		const SCardDescriptor *pCard = m_Registry.FindCard(Entry.first);
		const SCardOrderEntry *pPlacement = m_OrderModel.Find(Entry.first);
		if(!pCard || !pPlacement || Entry.second.m_Order < 0 || !Seen.insert(Entry.first).second)
			return false;
		if(Entry.second.m_Visible == pCard->m_DefaultVisible && Entry.second.m_Collapsed == pCard->m_DefaultCollapsed)
			Updated.erase(Entry.first);
		else
			Updated[Entry.first] = {Entry.second.m_Visible, Entry.second.m_Collapsed};
		vPlacements.push_back({Entry.first, pPlacement->m_PageId, pPlacement->m_Column, Entry.second.m_Order});
	}
	CCardOrderModel Order = m_OrderModel;
	Order.ApplyOverrides(vPlacements);
	m_OrderModel = std::move(Order);
	m_Preferences.swap(Updated);
	if(!vPreferences.empty())
	{
		m_Dirty = true;
		++m_Revision;
	}
	return true;
}

bool CCardUiModel::ReplacePreferences(const std::vector<std::pair<std::string, SCardUiPreferences>> &vPreferences)
{
	return ReplaceState(vPreferences, {});
}

bool CCardUiModel::ReplaceState(const std::vector<std::pair<std::string, SCardUiPreferences>> &vPreferences, const std::vector<SCardOrderEntry> &vPlacements)
{
	CCardUiModel Loaded(m_Registry);
	if(!Loaded.ImportPreferences(vPreferences))
		return false;
	std::unordered_set<std::string> Seen;
	for(const SCardOrderEntry &Entry : vPlacements)
	{
		if(!m_Registry.FindCard(Entry.m_Id) || !m_Registry.FindPage(Entry.m_PageId) || Entry.m_Order < 0 ||
			(Entry.m_Column != ECardColumn::FULL && Entry.m_Column != ECardColumn::LEFT && Entry.m_Column != ECardColumn::RIGHT) ||
			!Seen.insert(Entry.m_Id).second)
			return false;
	}
	Loaded.m_OrderModel.ApplyOverrides(vPlacements);
	m_Preferences.swap(Loaded.m_Preferences);
	m_OrderModel = std::move(Loaded.m_OrderModel);
	m_Dirty = true;
	++m_Revision;
	return true;
}

bool CCardUiModel::MoveCard(const std::string &CardId, const std::string &PageId, ECardColumn Column, int Order)
{
	if(!m_Registry.FindPage(PageId) || !m_OrderModel.MoveToPage(CardId, PageId, Column, Order))
		return false;
	++m_Revision;
	return true;
}

bool CCardUiModel::MoveCardRelative(const std::string &CardId, const std::string &TargetId, bool After)
{
	const auto *pSource = m_OrderModel.Find(CardId);
	const auto *pTarget = m_OrderModel.Find(TargetId);
	if(!pSource || !pTarget || pSource == pTarget)
		return false;
	int Order = pTarget->m_Order + (After ? 1 : 0);
	if(pSource->m_PageId == pTarget->m_PageId && pSource->m_Column == pTarget->m_Column &&
		pSource->m_Order < pTarget->m_Order)
		--Order;
	return MoveCard(CardId, pTarget->m_PageId, pTarget->m_Column, Order);
}

bool CCardUiModel::SetViewPreferences(SCardViewPreferences Value)
{
	if(Value.m_Mode < 0 || Value.m_Mode > 2)
		return false;
	if(Value.m_Mode != m_ViewPreferences.m_Mode || Value.m_LightTheme != m_ViewPreferences.m_LightTheme || Value.m_Animations != m_ViewPreferences.m_Animations)
	{
		m_ViewPreferences = Value;
		m_Dirty = true;
		++m_Revision;
	}
	return true;
}

bool CCardUiModel::ResetPreferences(const std::string &CardId)
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	if(!pCard)
		return false;
	const CCardOrderModel Defaults = m_Registry.BuildDefaultOrderModel();
	const SCardOrderEntry *pDefault = Defaults.Find(CardId);
	if(!pDefault)
		return false;
	const bool Moved = MoveCard(CardId, pDefault->m_PageId, pDefault->m_Column, pDefault->m_Order);
	if(m_Preferences.erase(CardId) != 0 || Moved)
	{
		m_Dirty = true;
		++m_Revision;
	}
	return true;
}

void CCardUiModel::ResetAllPreferences()
{
	if(ExportPreferences().empty())
		return;
	m_Preferences.clear();
	m_OrderModel = m_Registry.BuildDefaultOrderModel();
	m_Dirty = true;
	++m_Revision;
}

std::vector<const SCardDescriptor *> CCardUiModel::CardsForPage(const std::string &PageId) const
{
	std::vector<const SCardDescriptor *> vpCards;
	for(const ECardColumn Column : {ECardColumn::FULL, ECardColumn::LEFT, ECardColumn::RIGHT})
		for(const SCardOrderEntry *pEntry : m_OrderModel.EntriesForPage(PageId, Column))
		{
			const auto Card = Snapshot(pEntry->m_Id);
			if(Card.m_pDescriptor && Card.m_Preferences.m_Visible && Card.m_Available)
				vpCards.push_back(Card.m_pDescriptor);
		}
	return vpCards;
}

std::vector<const SCardDescriptor *> CCardUiModel::Search(const std::string &Query) const
{
	std::vector<const SCardDescriptor *> vpResults;
	for(const SCardDescriptor *pCard : m_Registry.Search(Query))
	{
		const auto Card = Snapshot(pCard->m_Id);
		if(Card.m_Preferences.m_Visible && Card.m_Available)
			vpResults.push_back(pCard);
	}
	std::sort(vpResults.begin(), vpResults.end(), [this](const SCardDescriptor *pLeft, const SCardDescriptor *pRight) {
		const auto *pLeftEntry = m_OrderModel.Find(pLeft->m_Id);
		const auto *pRightEntry = m_OrderModel.Find(pRight->m_Id);
		const auto *pLeftPage = m_Registry.FindPage(pLeftEntry->m_PageId);
		const auto *pRightPage = m_Registry.FindPage(pRightEntry->m_PageId);
		if(pLeftPage->m_Order != pRightPage->m_Order)
			return pLeftPage->m_Order < pRightPage->m_Order;
		if(pLeftEntry->m_PageId != pRightEntry->m_PageId)
			return pLeftEntry->m_PageId < pRightEntry->m_PageId;
		if(pLeftEntry->m_Column != pRightEntry->m_Column)
			return pLeftEntry->m_Column < pRightEntry->m_Column;
		if(pLeftEntry->m_Order != pRightEntry->m_Order)
			return pLeftEntry->m_Order < pRightEntry->m_Order;
		return pLeft->m_Id < pRight->m_Id;
	});
	return vpResults;
}
