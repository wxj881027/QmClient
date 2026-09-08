/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_ui_model.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace
{
bool IsValidColumn(const ECardColumn Column)
{
	return Column == ECardColumn::FULL || Column == ECardColumn::LEFT || Column == ECardColumn::RIGHT;
}
}

std::string CCardUiModel::PlacementKey(const std::string &PageId, const std::string &CardId)
{
	return PageId + '\x1f' + CardId;
}

CCardUiModel::CCardUiModel(const CCardRegistry &Registry) :
	m_Registry(Registry), m_OrderModel(Registry.BuildDefaultOrderModel()) {}

SCardUiPreferences CCardUiModel::Preferences(const std::string &PageId, const std::string &CardId) const
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	if(!pCard)
		return {};
	const auto It = m_Preferences.find(PlacementKey(PageId, CardId));
	if(It != m_Preferences.end())
		return It->second;
	return {pCard->m_DefaultVisible, pCard->m_DefaultCollapsed};
}

bool CCardUiModel::SetPreferences(const std::string &PageId, const std::string &CardId, const SCardUiPreferences Value)
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	const SCardOrderEntry *pPlacement = m_OrderModel.Find(PageId, CardId);
	if(!pCard || !pPlacement || !pPlacement->m_Present)
		return false;
	const auto It = m_Preferences.find(PlacementKey(PageId, CardId));
	const bool Unchanged = It != m_Preferences.end() ? It->second.m_Visible == Value.m_Visible && It->second.m_Collapsed == Value.m_Collapsed : Value.m_Visible == pCard->m_DefaultVisible && Value.m_Collapsed == pCard->m_DefaultCollapsed;
	if(Unchanged)
		return true;
	if(Value.m_Visible == pCard->m_DefaultVisible && Value.m_Collapsed == pCard->m_DefaultCollapsed)
		m_Preferences.erase(PlacementKey(PageId, CardId));
	else
		m_Preferences[PlacementKey(PageId, CardId)] = Value;
	m_Dirty = true;
	++m_Revision;
	return true;
}

SCardModelSnapshot CCardUiModel::Snapshot(const std::string &PageId, const std::string &CardId) const
{
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	if(!pCard)
		return {};
	const SFeatureModel *pFeature = pCard->m_FeatureId.empty() ? nullptr : m_Registry.FindFeature(pCard->m_FeatureId);
	return {pCard, Preferences(PageId, CardId), !pFeature || pFeature->m_Enabled, !pFeature || pFeature->m_Available};
}

bool CCardUiModel::SetViewPreferences(const SCardViewPreferences Value)
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

bool CCardUiModel::MoveCardWithinPage(const std::string &PageId, const std::string &CardId, const ECardColumn Column, const int Order)
{
	if(!m_OrderModel.Move(PageId, CardId, Column, Order))
		return false;
	++m_Revision;
	return true;
}

bool CCardUiModel::MoveCard(const std::string &CardId, const std::string &FromPageId, const std::string &ToPageId, const ECardColumn Column, const int Order)
{
	if(!m_Registry.FindPage(ToPageId) || !m_OrderModel.MoveToPage(CardId, FromPageId, ToPageId, Column, Order))
		return false;
	++m_Revision;
	return true;
}

bool CCardUiModel::MoveCardRelative(const std::string &PageId, const std::string &CardId, const std::string &TargetCardId, const bool After)
{
	const auto *pSource = m_OrderModel.Find(PageId, CardId);
	const auto *pTarget = m_OrderModel.Find(PageId, TargetCardId);
	if(!pSource || !pTarget || pSource == pTarget || !pSource->m_Present || !pTarget->m_Present)
		return false;
	int Order = pTarget->m_Order + (After ? 1 : 0);
	if(pSource->m_Column == pTarget->m_Column && pSource->m_Order < pTarget->m_Order)
		--Order;
	return MoveCardWithinPage(PageId, CardId, pTarget->m_Column, Order);
}

SCardUiState CCardUiModel::ExportState() const
{
	// 只导出覆盖：与默认声明不同、用户显式新增或移出的放置，以及非默认偏好。
	const CCardOrderModel Defaults = m_Registry.BuildDefaultOrderModel();
	SCardUiState State;
	State.m_View = m_ViewPreferences;
	for(const SCardOrderEntry &Entry : m_OrderModel.Entries())
	{
		const SCardOrderEntry *pDefault = Defaults.Find(Entry.m_PageId, Entry.m_Id);
		const bool PlacementDiffers = !pDefault || !Entry.m_Present || pDefault->m_Column != Entry.m_Column || pDefault->m_Order != Entry.m_Order;
		const auto It = m_Preferences.find(PlacementKey(Entry.m_PageId, Entry.m_Id));
		const bool PreferenceDiffers = It != m_Preferences.end();
		if(!PlacementDiffers && !PreferenceDiffers)
			continue;
		SCardUiPreferences Prefs{It != m_Preferences.end() ? It->second.m_Visible : Preferences(Entry.m_PageId, Entry.m_Id).m_Visible,
			It != m_Preferences.end() ? It->second.m_Collapsed : Preferences(Entry.m_PageId, Entry.m_Id).m_Collapsed};
		State.m_vPlacements.push_back({Entry.m_Id, Entry.m_PageId, Entry.m_Column, Entry.m_Order, Entry.m_Present, Prefs.m_Visible, Prefs.m_Collapsed});
	}
	std::sort(State.m_vPlacements.begin(), State.m_vPlacements.end(), [](const SCardPlacementState &Left, const SCardPlacementState &Right) {
		if(Left.m_PageId != Right.m_PageId)
			return Left.m_PageId < Right.m_PageId;
		return Left.m_CardId < Right.m_CardId;
	});
	return State;
}

bool CCardUiModel::ImportState(const SCardUiState &State, std::string &Error)
{
	Error.clear();
	// 先整体验证再一次性交换：坏文件不能覆盖现有偏好。
	std::unordered_set<std::string> Seen;
	for(const SCardPlacementState &Placement : State.m_vPlacements)
	{
		if(!m_Registry.FindCard(Placement.m_CardId) || !m_Registry.FindPage(Placement.m_PageId) ||
			!IsValidColumn(Placement.m_Column) || Placement.m_Order < 0 ||
			!Seen.insert(PlacementKey(Placement.m_PageId, Placement.m_CardId)).second)
		{
			Error = "invalid card placement";
			return false;
		}
	}

	CCardUiModel Loaded(m_Registry);
	Loaded.m_ViewPreferences = State.m_View;
	std::vector<SCardOrderEntry> vOverrides;
	for(const SCardPlacementState &Placement : State.m_vPlacements)
	{
		vOverrides.push_back({Placement.m_CardId, Placement.m_PageId, Placement.m_Column, Placement.m_Order, Placement.m_Present});
		const SCardDescriptor *pCard = m_Registry.FindCard(Placement.m_CardId);
		if(pCard && (Placement.m_Visible != pCard->m_DefaultVisible || Placement.m_Collapsed != pCard->m_DefaultCollapsed))
			Loaded.m_Preferences[PlacementKey(Placement.m_PageId, Placement.m_CardId)] = {Placement.m_Visible, Placement.m_Collapsed};
	}
	Loaded.m_OrderModel.ApplyOverrides(vOverrides);
	Loaded.ClearDirty();

	m_OrderModel = std::move(Loaded.m_OrderModel);
	m_Preferences = std::move(Loaded.m_Preferences);
	m_ViewPreferences = Loaded.m_ViewPreferences;
	m_Dirty = true;
	++m_Revision;
	return true;
}

bool CCardUiModel::ResetPreferences(const std::string &PageId, const std::string &CardId)
{
	const SCardOrderEntry *pEntry = m_OrderModel.Find(PageId, CardId);
	const SCardDescriptor *pCard = m_Registry.FindCard(CardId);
	if(!pEntry || !pCard)
		return false;
	bool Changed = false;
	const CCardOrderModel Defaults = m_Registry.BuildDefaultOrderModel();
	const SCardOrderEntry *pDefault = Defaults.Find(PageId, CardId);
	if(pDefault)
	{
		// 页面默认声明存在：恢复默认列序并重新挂载（曾被移出的卡片回到本页）。
		std::vector<SCardOrderEntry> vRestore;
		if(!pEntry->m_Present || pEntry->m_Column != pDefault->m_Column || pEntry->m_Order != pDefault->m_Order)
			vRestore.push_back({CardId, PageId, pDefault->m_Column, pDefault->m_Order, true});
		if(!vRestore.empty())
			Changed |= m_OrderModel.ApplyOverrides(vRestore);
	}
	else if(pEntry->m_Present)
		Changed |= m_OrderModel.RemovePlacement(PageId, CardId);
	Changed |= m_Preferences.erase(PlacementKey(PageId, CardId)) != 0;
	if(Changed)
	{
		m_Dirty = true;
		++m_Revision;
	}
	return true;
}

bool CCardUiModel::ResetPagePreferences(const std::string &PageId)
{
	if(!m_Registry.FindPage(PageId))
		return false;
	const CCardOrderModel Defaults = m_Registry.BuildDefaultOrderModel();
	bool Changed = false;
	std::vector<SCardOrderEntry> vRestores;
	std::vector<std::string> vRemoveIds;
	for(const SCardOrderEntry &Entry : m_OrderModel.Entries())
	{
		if(Entry.m_PageId != PageId)
			continue;
		const SCardOrderEntry *pDefault = Defaults.Find(PageId, Entry.m_Id);
		if(pDefault)
		{
			if(!Entry.m_Present || Entry.m_Column != pDefault->m_Column || Entry.m_Order != pDefault->m_Order)
				vRestores.push_back({Entry.m_Id, PageId, pDefault->m_Column, pDefault->m_Order, true});
		}
		else if(Entry.m_Present)
			vRemoveIds.push_back(Entry.m_Id);
	}
	if(!vRestores.empty())
		Changed |= m_OrderModel.ApplyOverrides(vRestores);
	for(const std::string &Id : vRemoveIds)
		Changed |= m_OrderModel.RemovePlacement(PageId, Id);
	for(auto It = m_Preferences.begin(); It != m_Preferences.end();)
	{
		if(It->first.substr(0, It->first.find('\x1f')) == PageId)
		{
			It = m_Preferences.erase(It);
			Changed = true;
		}
		else
			++It;
	}
	if(Changed)
	{
		m_Dirty = true;
		++m_Revision;
	}
	return true;
}

void CCardUiModel::ResetAllPreferences()
{
	if(ExportState().m_vPlacements.empty() && !m_Dirty)
	{
		if(!m_OrderModel.IsDirty())
			return;
	}
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
			const auto Card = Snapshot(PageId, pEntry->m_Id);
			if(Card.m_pDescriptor && Card.m_Preferences.m_Visible && Card.m_Available)
				vpCards.push_back(Card.m_pDescriptor);
		}
	return vpCards;
}
