/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_registry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <utility>

namespace
{
bool IsAsciiLowerOrDigit(char Character)
{
	return (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9');
}

bool CardOrderLess(const SCardDescriptor *pLeft, const SCardDescriptor *pRight)
{
	if(pLeft->m_Order != pRight->m_Order)
		return pLeft->m_Order < pRight->m_Order;
	return pLeft->m_Id < pRight->m_Id;
}
}

bool CCardRegistry::IsStableId(const std::string &Id)
{
	if(Id.empty() || Id.size() > 128)
		return false;

	bool PreviousWasSeparator = true;
	for(const char Character : Id)
	{
		if(Character == '.' || Character == '-' || Character == '_')
		{
			if(PreviousWasSeparator)
				return false;
			PreviousWasSeparator = true;
			continue;
		}
		if(!IsAsciiLowerOrDigit(Character))
			return false;
		PreviousWasSeparator = false;
	}
	return !PreviousWasSeparator;
}

bool CCardRegistry::RegisterPage(SCardPage Page)
{
	if(m_Frozen || !IsStableId(Page.m_Id) || Page.m_TitleKey.empty() || FindPage(Page.m_Id))
		return false;
	m_vPages.push_back(std::move(Page));
	return true;
}

bool CCardRegistry::RegisterFeature(SFeatureModel &Feature)
{
	if(m_Frozen || !IsStableId(Feature.m_Id) || Feature.m_TitleKey.empty() || FindFeature(Feature.m_Id))
		return false;
	m_vFeatures.push_back(&Feature);
	return true;
}

bool CCardRegistry::RegisterCard(SCardDescriptor Card)
{
	if(m_Frozen || !IsStableId(Card.m_Id) || Card.m_TitleKey.empty() || Card.m_IconId.empty() || FindCard(Card.m_Id))
		return false;
	if(Card.m_PresentationId.empty())
		Card.m_PresentationId = "default";
	if(!IsStableId(Card.m_PresentationId))
		return false;
	if(Card.m_Owner != ECardOwner::UPSTREAM && Card.m_Owner != ECardOwner::QM &&
		Card.m_Owner != ECardOwner::TC_REBUILT && Card.m_Owner != ECardOwner::BC_REBUILT)
		return false;
	if(Card.m_DefaultColumn != ECardColumn::FULL && Card.m_DefaultColumn != ECardColumn::LEFT && Card.m_DefaultColumn != ECardColumn::RIGHT)
		return false;
	if(!Card.m_FeatureId.empty() && !FindFeature(Card.m_FeatureId))
		return false;
	m_vCards.push_back(std::move(Card));
	return true;
}

bool CCardRegistry::Freeze()
{
	if(m_Frozen)
		return m_Valid;
	// 页面声明的 card ID 必须已注册；未注册的声明使冻结失败，不带病上线。
	std::set<std::pair<std::string, std::string>> Declared;
	for(const SCardPage &Page : m_vPages)
	{
		for(const std::string &CardId : Page.m_vCardIds)
		{
			if(CardId.empty() || !FindCard(CardId))
			{
				m_Valid = false;
				continue;
			}
			if(!Declared.insert({Page.m_Id, CardId}).second)
				m_Valid = false;
		}
	}
	m_Frozen = true;
	return m_Valid;
}

const SCardPage *CCardRegistry::FindPage(const std::string &Id) const
{
	for(const SCardPage &Page : m_vPages)
		if(Page.m_Id == Id)
			return &Page;
	return nullptr;
}

const SFeatureModel *CCardRegistry::FindFeature(const std::string &Id) const
{
	for(const SFeatureModel *pFeature : m_vFeatures)
		if(pFeature && pFeature->m_Id == Id)
			return pFeature;
	return nullptr;
}

const SCardDescriptor *CCardRegistry::FindCard(const std::string &Id) const
{
	for(const SCardDescriptor &Card : m_vCards)
		if(Card.m_Id == Id)
			return &Card;
	return nullptr;
}

std::vector<const SCardPage *> CCardRegistry::Pages() const
{
	std::vector<const SCardPage *> vpPages;
	vpPages.reserve(m_vPages.size());
	for(const SCardPage &Page : m_vPages)
		vpPages.push_back(&Page);
	std::sort(vpPages.begin(), vpPages.end(), [](const SCardPage *pLeft, const SCardPage *pRight) {
		if(pLeft->m_Order != pRight->m_Order)
			return pLeft->m_Order < pRight->m_Order;
		return pLeft->m_Id < pRight->m_Id;
	});
	return vpPages;
}

std::vector<const SCardDescriptor *> CCardRegistry::CardsForPage(const std::string &PageId) const
{
	// 页面声明是默认位置的来源：按声明顺序返回本页卡片。
	const SCardPage *pPage = FindPage(PageId);
	if(!pPage)
		return {};
	std::vector<const SCardDescriptor *> Cards;
	for(const std::string &CardId : pPage->m_vCardIds)
		if(const SCardDescriptor *pCard = FindCard(CardId))
			Cards.push_back(pCard);
	return Cards;
}

std::vector<const SCardDescriptor *> CCardRegistry::CardsByInputPriority() const
{
	std::vector<const SCardDescriptor *> Cards;
	Cards.reserve(m_vCards.size());
	for(const SCardDescriptor &Card : m_vCards)
		Cards.push_back(&Card);
	std::sort(Cards.begin(), Cards.end(), [](const SCardDescriptor *pLeft, const SCardDescriptor *pRight) {
		if(pLeft->m_InputPriority != pRight->m_InputPriority)
			return pLeft->m_InputPriority > pRight->m_InputPriority;
		return pLeft->m_Id < pRight->m_Id;
	});
	return Cards;
}

CCardOrderModel CCardRegistry::BuildDefaultOrderModel() const
{
	// 默认布局完全由页面声明推导：同一卡片在多个声明页各有一个默认放置；
	// 列取卡片默认列，列内顺序取声明顺序。
	CCardOrderModel Model;
	std::vector<SCardOrderEntry> Entries;
	for(const SCardPage *pPage : Pages())
	{
		std::array<int, 3> aNextOrder{};
		for(const std::string &CardId : pPage->m_vCardIds)
		{
			const SCardDescriptor *pCard = FindCard(CardId);
			if(!pCard)
				continue;
			const int ColumnIndex = static_cast<int>(pCard->m_DefaultColumn);
			Entries.push_back({CardId, pPage->m_Id, pCard->m_DefaultColumn, aNextOrder[ColumnIndex]++, true});
		}
	}
	Model.SetDefaults(std::move(Entries));
	return Model;
}
