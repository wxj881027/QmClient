#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSEARCH_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSEARCH_H

#include <game/client/QmUi/QmCardRegistry.h>

namespace qm_search_answers
{
	struct SResult;
}

namespace qm_card_catalog
{
	// 配置名、控件标签及检索别名由卡片模块提供；搜索时按 stableId 解析，
	// 避免静态初始化顺序影响注册结果。
	bool FindSearchMetadata(const char *pStableId, const qm_card_registry::SCardSettingSearch **ppSettings, size_t *pCount);
	// 明确的问答命中补入真实设置卡片，沿用用户保存的位置并按稳定 ID 去重。
	void AddAnswerCards(const qm_search_answers::SResult &Answers, const qm_card_order::CModel &Model, std::vector<qm_card_registry::SCardSearchResult> &vCards);
}

#endif
