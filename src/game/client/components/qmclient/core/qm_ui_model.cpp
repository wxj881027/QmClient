#include "qm_ui_model.h"

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

bool CQmUiModel::IsStableId(const std::string &Id)
{
	if(Id.size() < 4 || Id[0] != 'q' || Id[1] != 'm' || Id[2] != '.')
		return false;

	bool PreviousWasSeparator = true;
	for(size_t i = 3; i < Id.size(); ++i)
	{
		const char Character = Id[i];
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

bool CQmUiModel::RegisterFeature(SQmFeatureModel &Feature)
{
	if(m_Frozen || !IsStableId(Feature.m_Id) || Feature.m_TitleKey.empty() || FindFeature(Feature.m_Id))
		return false;
	m_vFeatures.push_back(&Feature);
	return true;
}

bool CQmUiModel::RegisterCard(SQmUiCard Card)
{
	const size_t Page = PageIndex(Card.m_Page);
	if(m_Frozen || Page >= static_cast<size_t>(EQmUiPage::COUNT) || !IsStableId(Card.m_Id) || Card.m_IconId.empty() || Card.m_FeatureId.empty() || !FindFeature(Card.m_FeatureId) || FindCard(Card.m_Id))
		return false;
	m_vCards.push_back(Card);
	m_aaCardsByPage[Page].push_back(&m_vCards.back());
	return true;
}

const SQmFeatureModel *CQmUiModel::FindFeature(const std::string &Id) const
{
	for(const SQmFeatureModel *pFeature : m_vFeatures)
	{
		if(pFeature && Id == pFeature->m_Id)
			return pFeature;
	}
	return nullptr;
}

const SQmUiCard *CQmUiModel::FindCard(const std::string &Id) const
{
	for(const SQmUiCard &Card : m_vCards)
	{
		if(Id == Card.m_Id)
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
