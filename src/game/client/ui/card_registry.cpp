/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
bool IsAsciiLowerOrDigit(char Character)
{
	return (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9');
}

std::string LowerAscii(const std::string &Value)
{
	std::string Lower = Value;
	for(char &Character : Lower)
		Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
	return Lower;
}

bool Contains(const std::string &Haystack, const std::string &Needle)
{
	return !Needle.empty() && LowerAscii(Haystack).find(LowerAscii(Needle)) != std::string::npos;
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
	if(m_Frozen || !IsStableId(Card.m_Id) || Card.m_PageId.empty() || Card.m_TitleKey.empty() || Card.m_IconId.empty() || FindCard(Card.m_Id) || !FindPage(Card.m_PageId))
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
	std::vector<const SCardDescriptor *> Cards;
	for(const SCardDescriptor &Card : m_vCards)
		if(Card.m_PageId == PageId)
			Cards.push_back(&Card);
	std::sort(Cards.begin(), Cards.end(), CardOrderLess);
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

std::vector<const SCardDescriptor *> CCardRegistry::Search(const std::string &Query) const
{
	std::vector<const SCardDescriptor *> Results;
	const std::string LowerQuery = LowerAscii(Query);
	if(LowerQuery.empty())
		return Results;

	for(const SCardDescriptor &Card : m_vCards)
	{
		bool Matches = Contains(Card.m_Id, LowerQuery) || Contains(Card.m_TitleKey, LowerQuery) || Contains(Card.m_DescriptionKey, LowerQuery);
		for(const std::string &Keyword : Card.m_SearchKeywords)
			Matches = Matches || Contains(Keyword, LowerQuery);
		if(Matches)
			Results.push_back(&Card);
	}
	std::sort(Results.begin(), Results.end(), CardOrderLess);
	return Results;
}

CCardOrderModel CCardRegistry::BuildDefaultOrderModel() const
{
	CCardOrderModel Model;
	std::vector<SCardOrderEntry> Entries;
	Entries.reserve(m_vCards.size());
	for(const SCardDescriptor &Card : m_vCards)
		Entries.push_back({Card.m_Id, Card.m_PageId, Card.m_DefaultColumn, Card.m_Order});
	Model.SetDefaults(std::move(Entries));
	return Model;
}
