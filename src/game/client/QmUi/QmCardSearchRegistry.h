#ifndef GAME_CLIENT_QMUI_QMCARDSEARCHREGISTRY_H
#define GAME_CLIENT_QMUI_QMCARDSEARCHREGISTRY_H

#include <base/str.h>

#include <engine/shared/localization.h>

#include <game/client/QmUi/QmCardRegistry.h>

#include <cstddef>
#include <iterator>
#include <vector>

// 设置搜索索引的声明式注册：卡片旁的 QmCardSearch/*.inc 声明检索项并用
// QM_CARD_SEARCH_REGISTER 自动登记；搜索只按 stableId 查询，不再维护集中映射表。
namespace qm_card_search
{
	struct SRegistration
	{
		const char *m_pStableId;
		const qm_card_registry::SCardSettingSearch *m_pEntries;
		size_t m_Count;
	};

	std::vector<SRegistration> &Registrations();
	void Register(const char *pStableId, const qm_card_registry::SCardSettingSearch *pEntries, size_t Count);
	const SRegistration *Find(const char *pStableId);

	class CRegistrar
	{
	public:
		CRegistrar(const char *pStableId, const qm_card_registry::SCardSettingSearch *pEntries, size_t Count)
		{
			Register(pStableId, pEntries, Count);
		}
	};
}

#define QM_CARD_SEARCH_REGISTER(stable_id, symbol) \
	const qm_card_search::CRegistrar s_QmSearchRegistrar_##symbol(stable_id, s_aQmSearch_##symbol, std::size(s_aQmSearch_##symbol))

#endif // GAME_CLIENT_QMUI_QMCARDSEARCHREGISTRY_H
