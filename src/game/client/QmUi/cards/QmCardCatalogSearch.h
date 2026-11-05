#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSEARCH_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGSEARCH_H

#include <game/client/QmUi/QmCardRegistry.h>

namespace qm_card_catalog
{
	// 配置名、控件标签及检索别名由卡片模块提供；搜索时按 stableId 解析，
	// 避免静态初始化顺序影响注册结果。
	bool FindSearchMetadata(const char *pStableId, const qm_card_registry::SCardSettingSearch **ppSettings, size_t *pCount);
}

#endif
