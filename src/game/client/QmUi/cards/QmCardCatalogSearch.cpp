#include "QmCardCatalogSearch.h"

#include <base/detect.h>

#include <game/client/QmUi/QmCardSearchRegistry.h>
#include <game/client/components/qmclient/search_answers.h>
#include <game/localization.h>

// 各卡片源文件旁的声明式搜索表，由本聚合器统一编译与注册。
#include "QmCardSearch/QmCardCatalogAppearance.inc"
#include "QmCardSearch/QmCardCatalogAppearancePreset.inc"
#include "QmCardSearch/QmCardCatalogChatCompletion.inc"
#include "QmCardSearch/QmCardCatalogControls.inc"
#include "QmCardSearch/QmCardCatalogDDNet.inc"
#include "QmCardSearch/QmCardCatalogFallback.inc"
#include "QmCardSearch/QmCardCatalogFunction.inc"
#include "QmCardSearch/QmCardCatalogGeneral.inc"
#include "QmCardSearch/QmCardCatalogGraphics.inc"
#include "QmCardSearch/QmCardCatalogHud.inc"
#include "QmCardSearch/QmCardCatalogNameplate.inc"
#include "QmCardSearch/QmCardCatalogPlayer.inc"
#include "QmCardSearch/QmCardCatalogSkin.inc"
#include "QmCardSearch/QmCardCatalogSound.inc"
#include "QmCardSearch/QmCardCatalogSteam.inc"
#include "QmCardSearch/QmCardCatalogTClient.inc"
#include "QmCardSearch/QmCardCatalogTClientBindWheel.inc"
#include "QmCardSearch/QmCardCatalogTClientConfigs.inc"
#include "QmCardSearch/QmCardCatalogTClientMain.inc"
#include "QmCardSearch/QmCardCatalogTClientProfiles.inc"
#include "QmCardSearch/QmCardCatalogTClientStatusBar.inc"
#include "QmCardSearch/QmCardCatalogTClientWarList.inc"
#include "QmCardSearch/QmCardCatalogTee.inc"
#include "QmCardSearch/QmCardCatalogTooltip.inc"
#include "QmCardSearch/QmCardCatalogVisual.inc"
#include "QmCardSearch/QmCardCatalogWaterHammer.inc"

#include <base/str.h>

#include <engine/shared/localization.h>

#include <algorithm>
#include <iterator>

namespace qm_card_search
{
	std::vector<SRegistration> &Registrations()
	{
		// 函数内静态：卡片文件的静态注册器可能先于本 TU 的全局对象运行。
		static std::vector<SRegistration> s_vRegistrations;
		return s_vRegistrations;
	}

	void Register(const char *pStableId, const qm_card_registry::SCardSettingSearch *pEntries, size_t Count)
	{
		if(pStableId == nullptr || pStableId[0] == '\0' || pEntries == nullptr || Count == 0)
			return;
		Registrations().push_back({pStableId, pEntries, Count});
	}

	const SRegistration *Find(const char *pStableId)
	{
		if(pStableId == nullptr)
			return nullptr;
		for(const SRegistration &Registration : Registrations())
		{
			if(str_comp(Registration.m_pStableId, pStableId) == 0)
				return &Registration;
		}
		return nullptr;
	}
}

namespace qm_card_catalog
{
	void AddAnswerCards(const qm_search_answers::SResult &Answers, const qm_card_order::CModel &Model, std::vector<qm_card_registry::SCardSearchResult> &vCards)
	{
		if(Answers.m_pAnswer == nullptr)
			return;
		std::vector<qm_card_registry::SCardSearchResult> vRelated;
		for(const char *pId : Answers.m_pAnswer->m_vRelatedCards)
		{
			const auto *pDefault = qm_card_registry::FindByStableId(pId);
			if(pDefault == nullptr)
				continue;
			const auto Target = qm_card_registry::ResolveCardNavigationTarget(*pDefault, Model);
			const auto Existing = std::find_if(vCards.begin(), vCards.end(), [&](const auto &Card) { return str_comp(Card.m_pStableId, Target.m_pStableId) == 0; });
			if(Existing != vCards.end())
			{
				vRelated.push_back(std::move(*Existing));
				vCards.erase(Existing);
			}
			else
			{
				qm_card_registry::SCardSearchResult Card;
				Card.m_pStableId = Target.m_pStableId;
				Card.m_Target = Target;
				Card.m_Title = Localize(pDefault->m_pTitle);
				Card.m_Description = qm_card_registry::ResolveLocalizedDescription(pId);
				vRelated.push_back(std::move(Card));
			}
		}
		vCards.insert(vCards.begin(), std::make_move_iterator(vRelated.begin()), std::make_move_iterator(vRelated.end()));
	}

	bool FindSearchMetadata(const char *pStableId, const qm_card_registry::SCardSettingSearch **ppSettings, size_t *pCount)
	{
		const qm_card_search::SRegistration *pRegistration = qm_card_search::Find(pStableId);
		if(pRegistration == nullptr)
			return false;
		*ppSettings = pRegistration->m_pEntries;
		*pCount = pRegistration->m_Count;
		return true;
	}
} // namespace qm_card_catalog
