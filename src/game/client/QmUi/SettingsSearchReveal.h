#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H

#include <base/str.h>

#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>

#include <chrono>
#include <string>
#include <vector>

// 搜索结果的逐行定位：搜索页发起请求，卡片绘制时只让目标卡片内的命中行
// 滚动到可视区并加底色。状态全部留在客户端 UI 层，不触碰配置或页面状态。
namespace qm_settings_reveal
{
	struct SState
	{
		std::string m_TargetCard;
		std::vector<std::string> m_vKeys;
		std::string m_ActiveCard;
		CScrollRegion *m_pScrollRegion = nullptr;
		bool m_Interactive = false;
		bool m_Scrolled = false;
		double m_ExpireSeconds = 0.0;
	};

	inline SState &State()
	{
		static SState s_State;
		return s_State;
	}

	inline double NowSeconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	inline bool KeyMatches(const std::vector<std::string> &vKeys, const char *pTextId, const char *pText)
	{
		for(const std::string &Key : vKeys)
		{
			if(Key.empty())
				continue;
			if(pTextId != nullptr && str_comp_nocase(Key.c_str(), pTextId) == 0)
				return true;
			if(pText != nullptr && str_comp_nocase(Key.c_str(), pText) == 0)
				return true;
		}
		return false;
	}

	inline void Clear()
	{
		SState &S = State();
		S.m_TargetCard.clear();
		S.m_vKeys.clear();
		S.m_ActiveCard.clear();
		S.m_pScrollRegion = nullptr;
		S.m_Interactive = false;
		S.m_Scrolled = false;
		S.m_ExpireSeconds = 0.0;
	}

	inline void Request(const char *pCardStableId, const std::vector<std::string> &vKeys)
	{
		if(pCardStableId == nullptr || pCardStableId[0] == '\0' || vKeys.empty())
			return;
		SState &S = State();
		S.m_TargetCard = pCardStableId;
		S.m_vKeys = vKeys;
		S.m_Scrolled = false;
		// 墙钟到期：折叠卡/不在当前 tab 时不会永远挂着，也不随刷新率缩短。
		S.m_ExpireSeconds = NowSeconds() + 5.0;
	}

	inline bool Pending()
	{
		SState &S = State();
		if(!S.m_TargetCard.empty() && NowSeconds() >= S.m_ExpireSeconds)
			Clear();
		return !S.m_TargetCard.empty();
	}

	inline void BeginCard(const char *pCardStableId, CScrollRegion *pScrollRegion, bool Interactive)
	{
		SState &S = State();
		if(!Pending() || pCardStableId == nullptr || S.m_TargetCard != pCardStableId)
			return;
		S.m_ActiveCard = pCardStableId;
		S.m_pScrollRegion = pScrollRegion;
		S.m_Interactive = Interactive;
	}

	inline void EndCard()
	{
		SState &S = State();
		S.m_ActiveCard.clear();
		S.m_pScrollRegion = nullptr;
		S.m_Interactive = false;
	}

	// 纯匹配规则：是否命中目标卡片里的这一行（不触发滚动，便于测试）。
	inline bool ShouldReveal(const char *pTextId, const char *pText)
	{
		SState &S = State();
		if(!Pending() || !S.m_Interactive || S.m_ActiveCard != S.m_TargetCard)
			return false;
		return KeyMatches(S.m_vKeys, pTextId, pText);
	}

	// 命中目标卡片里的某一行时请求滚动，并返回是否命中。
	inline bool OnRow(const char *pTextId, const char *pText, const CUIRect &Rect)
	{
		if(!ShouldReveal(pTextId, pText))
			return false;
		SState &S = State();
		if(!S.m_Scrolled && S.m_pScrollRegion != nullptr)
		{
			S.m_Scrolled = true;
			if(Rect.w > 0.0f && Rect.h > 0.0f)
			{
				S.m_pScrollRegion->AddRect(Rect, true);
			}
		}
		return true;
	}
}

#endif // GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H
