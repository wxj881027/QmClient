/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_DECK_PROJECTION_H
#define GAME_CLIENT_UI_CARD_DECK_PROJECTION_H

#include "card_order_model.h"
#include "card_registry.h"
#include "card_ui_model.h"

#include <array>
#include <string>
#include <vector>

struct SCardDeckProjection
{
	std::array<std::vector<const SCardDescriptor *>, 3> m_aColumns;
	unsigned m_LayoutRevision = 0;
	bool m_TwoColumns = false;
};

// 把全局卡片按当前页面、用户 order、可见性和列布局投影出来。
// 同一卡片在多个页面的投影共享业务状态，只是放置记录不同。
SCardDeckProjection BuildCardDeckProjection(const CCardRegistry &Registry, const CCardOrderModel &OrderModel, const CCardUiModel &UiModel, const std::string &PageId);

#endif
