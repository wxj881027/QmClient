#ifndef GAME_CLIENT_QMUI_CARDS_QMSEARCHANSWERCARD_H
#define GAME_CLIENT_QMUI_CARDS_QMSEARCHANSWERCARD_H

#include <functional>
#include <memory>

class CUi;
class ITextRender;
class IInput;
class IStorage;
class IClient;
struct SSettingsCardDefinition;
struct SSettingsContentMetrics;
namespace qm_search_answers
{
	struct SResult;
}

namespace qm_card_catalog
{
	struct SSearchAnswerCardState;
	std::shared_ptr<SSearchAnswerCardState> CreateSearchAnswerCardState();

	struct SSearchAnswerContext
	{
		CUi *m_pUi;
		ITextRender *m_pTextRender;
		IInput *m_pInput;
		IStorage *m_pStorage;
		IClient *m_pClient;
	};

	inline constexpr const char *SEARCH_ANSWER_CARD_ID = "deck:global-search-answers";
	void BuildSearchAnswerCard(const SSearchAnswerContext &Ctx, const qm_search_answers::SResult &Result, bool ReadOnly, const SSettingsContentMetrics &Metrics, const std::function<void(const char *)> &SelectQuestion, SSettingsCardDefinition &Out, const std::shared_ptr<SSearchAnswerCardState> &State = {});
}

#endif
