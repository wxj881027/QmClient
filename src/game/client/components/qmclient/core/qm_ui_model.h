/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_UI_MODEL_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_UI_MODEL_H

#include <array>
#include <deque>
#include <string>
#include <vector>

enum class EQmUiPage
{
	HOME,
	SEARCH,
	COUNT,
};

struct SQmFeatureModel
{
	const char *m_pId = nullptr;
	const char *m_pTitleKey = nullptr;
	bool m_Enabled = false;
	bool m_Available = true;
};

struct SQmUiCard
{
	EQmUiPage m_Page;
	const char *m_pId = nullptr;
	const char *m_pIconId = nullptr;
	const SQmFeatureModel *m_pFeature = nullptr;
};

class CQmUiModel final
{
	std::deque<SQmUiCard> m_vCards;
	std::vector<const SQmFeatureModel *> m_vFeatures;
	std::array<std::vector<const SQmUiCard *>, static_cast<size_t>(EQmUiPage::COUNT)> m_aaCardsByPage;
	bool m_Frozen = false;

public:
	bool RegisterFeature(const SQmFeatureModel &Feature);
	bool RegisterCard(SQmUiCard Card);
	void Freeze() { m_Frozen = true; }
	const SQmFeatureModel *FindFeature(const std::string &Id) const;
	const SQmUiCard *FindCard(const std::string &Id) const;
	const std::vector<const SQmUiCard *> &CardsForPage(EQmUiPage Page) const;

private:
	static bool IsStableId(const char *pId);
};

#endif
