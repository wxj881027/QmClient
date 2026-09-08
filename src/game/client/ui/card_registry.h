/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_REGISTRY_H
#define GAME_CLIENT_UI_CARD_REGISTRY_H

#include "card_order_model.h"
#include <game/client/components/qmclient/core/qm_feature_model.h>

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

// PageDeclaration：页面只声明本页要显示哪些 card ID、以什么默认顺序显示。
// 页面不拥有卡片业务状态；同一卡片可以被多个页面声明。
struct SCardPage
{
	std::string m_Id;
	std::string m_TitleKey;
	int m_Order = 0;
	std::vector<std::string> m_vCardIds;
};

// GlobalCardDefinition：卡片定义不绑定单一页面。默认位置由页面声明推导，
// m_DefaultColumn / m_Order 只作为声明内的默认列和搜索排序的稳定 tiebreak。
struct SCardDescriptor
{
	std::string m_Id;
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
	bool m_Valid = true;

public:
	bool RegisterPage(SCardPage Page);
	bool RegisterFeature(SFeatureModel &Feature);
	bool RegisterCard(SCardDescriptor Card);
	// 冻结时校验所有页面声明的 card ID 都已注册；未解析声明使冻结失败。
	bool Freeze();
	bool IsFrozen() const { return m_Frozen; }
	bool IsValid() const { return m_Valid; }

	const SCardPage *FindPage(const std::string &Id) const;
	const SFeatureModel *FindFeature(const std::string &Id) const;
	const SCardDescriptor *FindCard(const std::string &Id) const;
	std::vector<const SCardPage *> Pages() const;
	std::vector<const SCardDescriptor *> CardsForPage(const std::string &PageId) const;
	std::vector<const SCardDescriptor *> CardsByInputPriority() const;
	CCardOrderModel BuildDefaultOrderModel() const;

private:
	static bool IsStableId(const std::string &Id);
};

#endif
