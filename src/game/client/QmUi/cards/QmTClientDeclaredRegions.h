#ifndef GAME_CLIENT_QMUI_CARDS_QMTCLIENTDECLAREDREGIONS_H
#define GAME_CLIENT_QMUI_CARDS_QMTCLIENTDECLAREDREGIONS_H

#include <game/client/QmUi/SettingsContentLayout.h>

#include <map>
#include <memory>
#include <string>

class CUi;
class ITextRender;

namespace qm_tclient_cards
{
	struct SDeclaredRegion
	{
		CSettingsContentLayoutCache m_Cache;
		std::shared_ptr<const CSettingsContentLayout> m_pLayout;
	};

	struct SDeclaredRegions
	{
		std::map<std::string, SDeclaredRegion> m_Regions;
		CUi *m_pUi = nullptr;
		ITextRender *m_pText = nullptr;
		bool m_ReadOnly = false;
	};

	// 区域缓存由卡片回调持有；声明、输入或绘制异常也要撤回这次调用的借用。
	class CDeclaredRegionsScope
	{
		inline static thread_local SDeclaredRegions *s_pCurrent = nullptr;
		SDeclaredRegions *m_pPrevious;

	public:
		explicit CDeclaredRegionsScope(SDeclaredRegions &Regions) noexcept :
			m_pPrevious(s_pCurrent)
		{
			s_pCurrent = &Regions;
		}
		~CDeclaredRegionsScope() { s_pCurrent = m_pPrevious; }
		CDeclaredRegionsScope(const CDeclaredRegionsScope &) = delete;
		CDeclaredRegionsScope &operator=(const CDeclaredRegionsScope &) = delete;
		static SDeclaredRegions *Current() noexcept { return s_pCurrent; }
	};
}

#endif
