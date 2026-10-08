#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSEARCH_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSEARCH_H

#include <game/client/QmUi/QmCardRegistry.h>

namespace qm_card_catalog
{
	// 配置名、控件标签及检索别名由卡片模块提供；无需先渲染原页面。
	void FillCardSearchMetadata(std::vector<qm_card_registry::SCardDefault> &vCards);
}

#endif
