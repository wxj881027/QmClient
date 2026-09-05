/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_UI_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_UI_MODEL_H

#include <array>
#include <deque>
#include <string>
#include <utility>
#include <vector>

enum class EQmUiPage
{
	HOME,
	SEARCH,
	COUNT,
};

struct SQmFeatureModel
{
	std::string m_Id;
	std::string m_TitleKey;
	bool m_Enabled = false;
	bool m_Available = true;

	SQmFeatureModel() = default;
	SQmFeatureModel(const char *pId, const char *pTitleKey, bool Enabled, bool Available) :
		m_Id(pId ? pId : ""),
		m_TitleKey(pTitleKey ? pTitleKey : ""),
		m_Enabled(Enabled),
		m_Available(Available)
	{
	}
	SQmFeatureModel(std::string Id, std::string TitleKey, bool Enabled, bool Available) :
		m_Id(std::move(Id)),
		m_TitleKey(std::move(TitleKey)),
		m_Enabled(Enabled),
		m_Available(Available)
	{
	}
};

struct SQmUiCard
{
	EQmUiPage m_Page = EQmUiPage::HOME;
	std::string m_Id;
	std::string m_IconId;
	std::string m_FeatureId;

	SQmUiCard() = default;
	SQmUiCard(EQmUiPage Page, const char *pId, const char *pIconId, const SQmFeatureModel &Feature) :
		m_Page(Page),
		m_Id(pId ? pId : ""),
		m_IconId(pIconId ? pIconId : ""),
		m_FeatureId(Feature.m_Id)
	{
	}
	SQmUiCard(EQmUiPage Page, std::string Id, std::string IconId, const SQmFeatureModel &Feature) :
		m_Page(Page),
		m_Id(std::move(Id)),
		m_IconId(std::move(IconId)),
		m_FeatureId(Feature.m_Id)
	{
	}
};

class CQmUiModel final
{
	std::deque<SQmUiCard> m_vCards;
	std::vector<const SQmFeatureModel *> m_vFeatures;
	std::array<std::vector<const SQmUiCard *>, static_cast<size_t>(EQmUiPage::COUNT)> m_aaCardsByPage;
	bool m_Frozen = false;

public:
	// Feature 必须由调用方持有，并且生命周期长于本 registry。
	bool RegisterFeature(SQmFeatureModel &Feature);
	bool RegisterCard(SQmUiCard Card);
	void Freeze() { m_Frozen = true; }
	const SQmFeatureModel *FindFeature(const std::string &Id) const;
	const SQmUiCard *FindCard(const std::string &Id) const;
	const std::vector<const SQmUiCard *> &CardsForPage(EQmUiPage Page) const;

private:
	static bool IsStableId(const std::string &Id);
};

#endif
