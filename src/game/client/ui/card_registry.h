/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_REGISTRY_H
#define GAME_CLIENT_UI_CARD_REGISTRY_H

#include "card_order_model.h"

#include <deque>
#include <string>
#include <utility>
#include <vector>

enum class ECardOwner
{
	UPSTREAM,
	QM,
	TC_REBUILT,
	BC_REBUILT,
};

struct SFeatureModel
{
	std::string m_Id;
	std::string m_TitleKey;
	bool m_Enabled = false;
	bool m_Available = true;

	SFeatureModel() = default;
	SFeatureModel(const char *pId, const char *pTitleKey, bool Enabled, bool Available) :
		m_Id(pId ? pId : ""),
		m_TitleKey(pTitleKey ? pTitleKey : ""),
		m_Enabled(Enabled),
		m_Available(Available)
	{
	}
	SFeatureModel(std::string Id, std::string TitleKey, bool Enabled, bool Available) :
		m_Id(std::move(Id)),
		m_TitleKey(std::move(TitleKey)),
		m_Enabled(Enabled),
		m_Available(Available)
	{
	}
};

using SQmFeatureModel = SFeatureModel;

struct SCardPage
{
	std::string m_Id;
	std::string m_TitleKey;
	int m_Order = 0;
};

struct SCardDescriptor
{
	std::string m_Id;
	std::string m_PageId;
	std::string m_TitleKey;
	std::string m_DescriptionKey;
	std::string m_IconId;
	std::string m_FeatureId;
	std::vector<std::string> m_SearchKeywords;
	ECardOwner m_Owner = ECardOwner::UPSTREAM;
	int m_Order = 0;
	bool m_DefaultVisible = true;
	std::string m_PresentationId = "default";
	int m_InputPriority = 0;
	bool m_DefaultCollapsed = false;
	ECardColumn m_DefaultColumn = ECardColumn::FULL;
};

class CCardRegistry final
{
	std::deque<SCardPage> m_vPages;
	std::vector<SFeatureModel *> m_vFeatures;
	std::deque<SCardDescriptor> m_vCards;
	bool m_Frozen = false;

public:
	bool RegisterPage(SCardPage Page);
	bool RegisterFeature(SFeatureModel &Feature);
	bool RegisterCard(SCardDescriptor Card);
	void Freeze() { m_Frozen = true; }
	bool IsFrozen() const { return m_Frozen; }

	const SCardPage *FindPage(const std::string &Id) const;
	const SFeatureModel *FindFeature(const std::string &Id) const;
	const SCardDescriptor *FindCard(const std::string &Id) const;
	std::vector<const SCardPage *> Pages() const;
	std::vector<const SCardDescriptor *> CardsForPage(const std::string &PageId) const;
	std::vector<const SCardDescriptor *> CardsByInputPriority() const;
	std::vector<const SCardDescriptor *> Search(const std::string &Query) const;
	CCardOrderModel BuildDefaultOrderModel() const;

private:
	static bool IsStableId(const std::string &Id);
};

#endif
