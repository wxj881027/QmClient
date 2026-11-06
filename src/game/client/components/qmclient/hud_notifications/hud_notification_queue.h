#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_QUEUE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_QUEUE_H

#include <base/system.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace QmHudNotifications
{
	inline constexpr int MAX_NOTIFICATIONS = 8;

	enum class ENotificationKind
	{
		SoloEnter,
		SoloLeave,
		System,
		Echo,
	};

	struct SNotification
	{
		ENotificationKind m_Kind = ENotificationKind::Echo;
		char m_aText[256] = {};
		char m_aCollapseText[256] = {};
		char m_aRepeatText[32] = {};
		int64_t m_StartTime = 0;
		int64_t m_RepeatAnimTime = 0;
		unsigned m_EchoColor = 0;
		bool m_HasEchoColor = false;
		int m_RepeatCount = 1;
		float m_VisualY = -1.0f;
	};

	// 颜色解析、翻译和文本准备可能分配；失败由调用方继续原聊天回退。
	template<typename TPrepare>
	bool TryDelivery(TPrepare &&Prepare)
	{
		try
		{
			return Prepare();
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

	// 可见项上限固定；存储不申请内存，候选准备成功后才淘汰最旧项。
	class CNotificationQueue
	{
		static_assert(std::is_trivially_copyable_v<SNotification>);
		std::array<SNotification, MAX_NOTIFICATIONS> m_aItems{};
		size_t m_First = 0;
		size_t m_Count = 0;

	public:
		size_t size() const noexcept { return m_Count; }
		bool empty() const noexcept { return m_Count == 0; }
		SNotification &operator[](size_t Index) noexcept { return m_aItems[(m_First + Index) % MAX_NOTIFICATIONS]; }
		const SNotification &operator[](size_t Index) const noexcept { return m_aItems[(m_First + Index) % MAX_NOTIFICATIONS]; }
		SNotification &front() noexcept { return (*this)[0]; }
		const SNotification &front() const noexcept { return (*this)[0]; }
		SNotification &back() noexcept { return (*this)[m_Count - 1]; }
		const SNotification &back() const noexcept { return (*this)[m_Count - 1]; }
		void clear() noexcept
		{
			m_First = 0;
			m_Count = 0;
		}
		void pop_front() noexcept
		{
			if(empty())
				return;
			m_First = (m_First + 1) % MAX_NOTIFICATIONS;
			--m_Count;
		}

		template<typename TPrepareText>
		bool Queue(ENotificationKind Kind, const char *pText, unsigned EchoColor, bool HasEchoColor, int VisibleCount, int64_t Now, TPrepareText &&PrepareText)
		{
			if(pText == nullptr || pText[0] == '\0')
				return false;
			if(!empty())
			{
				SNotification &Last = back();
				if(Last.m_Kind == Kind && Last.m_HasEchoColor == HasEchoColor && Last.m_EchoColor == EchoColor && str_comp(Last.m_aCollapseText, pText) == 0)
				{
					if(Last.m_RepeatCount < std::numeric_limits<int>::max())
						++Last.m_RepeatCount;
					Last.m_RepeatAnimTime = Now;
					str_format(Last.m_aRepeatText, sizeof(Last.m_aRepeatText), "x%d", Last.m_RepeatCount);
					return true;
				}
			}
			return TryDelivery([&] {
				SNotification Candidate;
				Candidate.m_Kind = Kind;
				str_copy(Candidate.m_aText, pText);
				PrepareText(Candidate.m_aText);
				str_copy(Candidate.m_aCollapseText, pText);
				Candidate.m_StartTime = Now;
				Candidate.m_EchoColor = EchoColor;
				Candidate.m_HasEchoColor = HasEchoColor;
				if(m_Count == MAX_NOTIFICATIONS)
					pop_front();
				(*this)[m_Count] = Candidate;
				++m_Count;
				const size_t Limit = static_cast<size_t>(std::clamp(VisibleCount, 1, MAX_NOTIFICATIONS));
				while(m_Count > Limit)
					pop_front();
				return true;
			});
		}
	};
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_NOTIFICATIONS_HUD_NOTIFICATION_QUEUE_H
