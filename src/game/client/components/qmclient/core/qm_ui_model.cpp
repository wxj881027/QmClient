#include "qm_ui_model.h"

#include <base/str.h>

namespace
{
size_t PageIndex(EQmUiPage Page)
{
	return static_cast<size_t>(Page);
}

bool IsAsciiLowerOrDigit(char Character)
{
	return (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9');
}
}

bool CQmUiModel::IsStableId(const char *pId)
{
	if(!pId || str_length(pId) < 4 || pId[0] != 'q' || pId[1] != 'm' || pId[2] != '.')
		return false;

	bool PreviousWasSeparator = true;
	for(const char *p = pId + 3; *p; ++p)
	{
		if(*p == '.' || *p == '-' || *p == '_')
		{
			if(PreviousWasSeparator)
				return false;
			PreviousWasSeparator = true;
			continue;
		}
		if(!IsAsciiLowerOrDigit(*p))
			return false;
		PreviousWasSeparator = false;
	}
	return !PreviousWasSeparator;
}

bool CQmUiModel::RegisterFeature(const SQmFeatureModel &Feature)
{
	if(m_Frozen || !IsStableId(Feature.m_pId) || !Feature.m_pTitleKey || Feature.m_pTitleKey[0] == '\0' || FindFeature(Feature.m_pId))
		return false;
	m_vFeatures.push_back(&Feature);
	return true;
}

bool CQmUiModel::RegisterCard(SQmUiCard Card)
{
	const size_t Page = PageIndex(Card.m_Page);
	if(m_Frozen || Page >= static_cast<size_t>(EQmUiPage::COUNT) || !IsStableId(Card.m_pId) || !Card.m_pIconId || Card.m_pIconId[0] == '\0' || !Card.m_pFeature || !Card.m_pFeature->m_pId || FindFeature(Card.m_pFeature->m_pId) != Card.m_pFeature || FindCard(Card.m_pId))
		return false;
	m_vCards.push_back(Card);
	m_aaCardsByPage[Page].push_back(&m_vCards.back());
	return true;
}

const SQmFeatureModel *CQmUiModel::FindFeature(const std::string &Id) const
{
	for(const SQmFeatureModel *pFeature : m_vFeatures)
	{
		if(pFeature && pFeature->m_pId && Id == pFeature->m_pId)
			return pFeature;
	}
	return nullptr;
}

const SQmUiCard *CQmUiModel::FindCard(const std::string &Id) const
{
	for(const SQmUiCard &Card : m_vCards)
	{
		if(Card.m_pId && Id == Card.m_pId)
			return &Card;
	}
	return nullptr;
}

const std::vector<const SQmUiCard *> &CQmUiModel::CardsForPage(EQmUiPage Page) const
{
	static const std::vector<const SQmUiCard *> Empty;
	const size_t Index = PageIndex(Page);
	return Index < static_cast<size_t>(EQmUiPage::COUNT) ? m_aaCardsByPage[Index] : Empty;
}
