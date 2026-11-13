#ifndef GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H
#define GAME_CLIENT_QMUI_SETTINGSSEARCHREVEAL_H

#include <base/str.h>

#include <game/client/ui_rect.h>
#include <game/client/ui_scrollregion.h>

#include <chrono>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 搜索结果的逐行定位：搜索页可为多张结果卡设置预览键，卡片绘制时只让
// 当前卡片内命中的行加高亮底色；点“定位”时切换为单卡请求并滚动到首行。
// 状态全部留在客户端 UI 层，不触碰配置或页面状态。
namespace qm_settings_reveal
{
	struct SPreview
	{
		std::string m_Card;
		std::vector<std::string> m_vKeys;
	};

	struct SState
	{
		std::string m_TargetCard;
		std::vector<std::string> m_vKeys;
		std::vector<SPreview> m_vPreviews;
		std::string m_ActiveCard;
		const std::vector<std::string> *m_pActiveKeys = nullptr;
		CScrollRegion *m_pScrollRegion = nullptr;
		bool m_Interactive = false;
		bool m_Scrolled = false;
		double m_ExpireSeconds = 0.0;
		double m_PreviewExpireSeconds = 0.0;
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

	inline void EndCard()
	{
		SState &S = State();
		S.m_ActiveCard.clear();
		S.m_pActiveKeys = nullptr;
		S.m_pScrollRegion = nullptr;
		S.m_Interactive = false;
	}

	inline void Clear()
	{
		SState &S = State();
		// 先撤销当前卡片的借用，再释放对应的请求与预览存储。
		EndCard();
		S.m_TargetCard.clear();
		S.m_vKeys.clear();
		S.m_vPreviews.clear();
		S.m_Scrolled = false;
		S.m_ExpireSeconds = 0.0;
		S.m_PreviewExpireSeconds = 0.0;
	}

	// 候选由调用方完整持有，切页等外部状态提交后不再分配。
	inline bool PublishRequest(std::string TargetCard, std::vector<std::string> Keys) noexcept
	{
		if(TargetCard.empty() || Keys.empty())
			return false;
		SState &S = State();
		const double ExpireSeconds = NowSeconds() + 5.0;
		EndCard();
		S.m_TargetCard.swap(TargetCard);
		S.m_vKeys.swap(Keys);
		S.m_vPreviews.clear();
		S.m_PreviewExpireSeconds = 0.0;
		S.m_Scrolled = false;
		// 墙钟到期：折叠卡/不在当前 tab 时不会永远挂着，也不随刷新率缩短。
		S.m_ExpireSeconds = ExpireSeconds;
		return true;
	}

	inline bool Request(const char *pCardStableId, const std::vector<std::string> &vKeys)
	{
		if(pCardStableId == nullptr || pCardStableId[0] == '\0' || vKeys.empty())
			return false;
		try
		{
			std::string TargetCard(pCardStableId);
			std::vector<std::string> Keys(vKeys);
			return PublishRequest(std::move(TargetCard), std::move(Keys));
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}

	// 搜索结果页每帧刷新：每张可见结果卡各自持有命中键，多卡可同时高亮。
	// 预览不滚动、不抢“定位”请求，1 秒内没有刷新就自然失效。
	inline bool SetPreviews(const std::vector<SPreview> &vPreviews)
	{
		try
		{
			SState &S = State();
			bool Unchanged = S.m_vPreviews.size() == vPreviews.size();
			for(size_t Index = 0; Unchanged && Index < vPreviews.size(); ++Index)
				Unchanged = S.m_vPreviews[Index].m_Card == vPreviews[Index].m_Card && S.m_vPreviews[Index].m_vKeys == vPreviews[Index].m_vKeys;
			if(Unchanged)
			{
				// 每帧相同预览只续期，继续借用原存储，不重新复制命中键。
				S.m_PreviewExpireSeconds = vPreviews.empty() ? 0.0 : NowSeconds() + 1.0;
				return true;
			}
			std::vector<SPreview> Candidate(vPreviews);
			const double ExpireSeconds = vPreviews.empty() ? 0.0 : NowSeconds() + 1.0;
			EndCard();
			S.m_vPreviews.swap(Candidate);
			S.m_PreviewExpireSeconds = ExpireSeconds;
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
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
		S.m_ActiveCard.clear();
		S.m_pActiveKeys = nullptr;
		S.m_pScrollRegion = nullptr;
		S.m_Interactive = false;
		if(pCardStableId == nullptr)
			return;
		const double Now = NowSeconds();
		const bool TargetRequest = !S.m_TargetCard.empty() && S.m_TargetCard == pCardStableId && Now < S.m_ExpireSeconds;
		const std::vector<std::string> *pActiveKeys = nullptr;
		if(TargetRequest)
		{
			pActiveKeys = &S.m_vKeys;
		}
		else
		{
			if(Now >= S.m_PreviewExpireSeconds)
				return;
			for(const SPreview &Preview : S.m_vPreviews)
			{
				if(Preview.m_Card == pCardStableId && !Preview.m_vKeys.empty())
				{
					pActiveKeys = &Preview.m_vKeys;
					break;
				}
			}
		}
		if(pActiveKeys == nullptr)
			return;
		try
		{
			// 复用已分配的名称容量；首次激活失败时不发布高亮或滚动借用。
			S.m_ActiveCard = pCardStableId;
		}
		catch(const std::bad_alloc &)
		{
			return;
		}
		catch(const std::length_error &)
		{
			return;
		}
		S.m_pActiveKeys = pActiveKeys;
		if(TargetRequest)
		{
			S.m_pScrollRegion = pScrollRegion;
			S.m_Interactive = Interactive;
		}
	}

	// 绘制异常也清除当前卡片的借用指针和滚动归属。
	class CCardScope
	{
	public:
		CCardScope(const char *pCardStableId, CScrollRegion *pScrollRegion, bool Interactive)
		{
			BeginCard(pCardStableId, pScrollRegion, Interactive);
		}
		~CCardScope() { EndCard(); }
		CCardScope(const CCardScope &) = delete;
		CCardScope &operator=(const CCardScope &) = delete;
	};

	// 纯匹配规则：当前卡片是否有命中行（预览卡不滚动）。
	inline bool ShouldReveal(const char *pTextId, const char *pText)
	{
		const SState &S = State();
		if(S.m_pActiveKeys == nullptr || S.m_ActiveCard.empty())
			return false;
		return KeyMatches(*S.m_pActiveKeys, pTextId, pText);
	}

	// 命中当前卡片里的某一行时请求滚动，并返回是否命中。
	inline bool OnRow(const char *pTextId, const char *pText, const CUIRect &Rect)
	{
		if(!ShouldReveal(pTextId, pText))
			return false;
		SState &S = State();
		if(S.m_Interactive && !S.m_Scrolled && S.m_pScrollRegion != nullptr)
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
