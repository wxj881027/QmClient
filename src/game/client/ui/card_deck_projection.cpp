/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_deck_projection.h"

#include <algorithm>

namespace
{
int ColumnIndex(const ECardColumn Column)
{
	return static_cast<int>(Column);
}

bool ContainsCard(const std::array<std::vector<const SCardDescriptor *>, 3> &Columns, const std::string &Id)
{
	for(const auto &Column : Columns)
		for(const SCardDescriptor *pCard : Column)
			if(pCard && pCard->m_Id == Id)
				return true;
	return false;
}
}

SCardDeckProjection BuildCardDeckProjection(const CCardRegistry &Registry, const CCardOrderModel &OrderModel, const CCardUiModel &UiModel, const std::string &PageId)
{
	SCardDeckProjection Projection;
	Projection.m_LayoutRevision = OrderModel.LayoutRevision();
	if(!Registry.FindPage(PageId))
		return Projection;
	for(const SCardOrderEntry &Entry : OrderModel.Entries())
	{
		if(Entry.m_PageId != PageId)
			continue;
		const SCardDescriptor *pCard = Registry.FindCard(Entry.m_Id);
		if(!pCard || !UiModel.Preferences(pCard->m_Id).m_Visible)
			continue;
		if(!UiModel.Snapshot(pCard->m_Id).m_Available)
			continue;
		Projection.m_aColumns[ColumnIndex(Entry.m_Column)].push_back(pCard);
	}

	// 新卡在持久化布局中不存在时，使用 descriptor 默认值补位，不让旧配置隐藏新功能。
	for(const SCardDescriptor *pCard : Registry.CardsForPage(PageId))
	{
		if(!pCard || OrderModel.Find(pCard->m_Id) || ContainsCard(Projection.m_aColumns, pCard->m_Id) || !UiModel.Preferences(pCard->m_Id).m_Visible)
			continue;
		if(!UiModel.Snapshot(pCard->m_Id).m_Available)
			continue;
		Projection.m_aColumns[ColumnIndex(pCard->m_DefaultColumn)].push_back(pCard);
	}

	for(auto &Column : Projection.m_aColumns)
		std::stable_sort(Column.begin(), Column.end(), [&OrderModel](const SCardDescriptor *pLeft, const SCardDescriptor *pRight) {
			const SCardOrderEntry *pLeftEntry = nullptr;
			const SCardOrderEntry *pRightEntry = nullptr;
			for(const SCardOrderEntry &Entry : OrderModel.Entries())
			{
				if(Entry.m_Id == pLeft->m_Id)
					pLeftEntry = &Entry;
				if(Entry.m_Id == pRight->m_Id)
					pRightEntry = &Entry;
			}
			const int LeftOrder = pLeftEntry ? pLeftEntry->m_Order : pLeft->m_Order;
			const int RightOrder = pRightEntry ? pRightEntry->m_Order : pRight->m_Order;
			return LeftOrder < RightOrder || (LeftOrder == RightOrder && pLeft->m_Id < pRight->m_Id);
		});
	Projection.m_TwoColumns = !Projection.m_aColumns[1].empty() || !Projection.m_aColumns[2].empty();
	return Projection;
}
