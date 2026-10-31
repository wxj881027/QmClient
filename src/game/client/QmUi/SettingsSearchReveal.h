#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H

#include <base/str.h>

#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>

#include <string>
#include <vector>

class CScrollRegion;

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
		int m_FramesLeft = 0;
	};

	inline SState &State()
	{
		static SState s_State;
		return s_State;
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
		S.m_FramesLeft = 0;
	}

	inline void Request(const char *pCardStableId, const std::vector<std::string> &vKeys)
	{
		if(pCardStableId == nullptr || pCardStableId[0] == '\0' || vKeys.empty())
			return;
		SState &S = State();
		S.m_TargetCard = pCardStableId;
		S.m_vKeys = vKeys;
		S.m_Scrolled = false;
		S.m_FramesLeft = 300;
	}

	inline bool Pending()
	{
		return !State().m_TargetCard.empty();
	}

	inline void BeginCard(const char *pCardStableId, CScrollRegion *pScrollRegion, bool Interactive)
	{
		SState &S = State();
		if(S.m_TargetCard.empty() || pCardStableId == nullptr || S.m_TargetCard != pCardStableId)
			return;
		S.m_ActiveCard = pCardStableId;
		S.m_pScrollRegion = pScrollRegion;
		S.m_Interactive = Interactive;
	}

	inline void EndCard()
	{
		SState &S = State();
		const bool WasTarget = !S.m_ActiveCard.empty() && S.m_ActiveCard == S.m_TargetCard;
		S.m_ActiveCard.clear();
		S.m_pScrollRegion = nullptr;
		S.m_Interactive = false;
		// 目标卡片每绘制一帧消耗一次寿命；命中卡被折叠或尚未构建时也不会永远挂着。
		if(WasTarget && S.m_FramesLeft > 0 && --S.m_FramesLeft == 0)
			Clear();
	}

	// 命中目标卡片里的某一行时请求滚动，并返回是否命中。
	inline bool OnRow(const char *pTextId, const char *pText, const CUIRect &Rect)
	{
		SState &S = State();
		if(!S.m_Interactive || S.m_TargetCard.empty() || S.m_ActiveCard != S.m_TargetCard)
			return false;
		if(!KeyMatches(S.m_vKeys, pTextId, pText))
			return false;
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
