#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_NOTIFY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_NOTIFY_H

#include "friend_enter_tracker.h"

#include <string>

namespace qm_friend_notify
{
	class CEnterNotifier
	{
	public:
		struct CSettings
		{
			bool m_GreetEnabled = false;
			bool m_GreetTextConfigured = false;
			bool m_BroadcastEnabled = false;
		};

	private:
		CEnterTracker m_Tracker;
		CGreetQueue m_Pending;

	public:
		void Reset() noexcept
		{
			m_Tracker.Reset();
			m_Pending.Reset();
		}
		void CancelPending() noexcept { m_Pending.Reset(); }

		// 只由当前服务端提示推进状态；准备失败保留旧状态，不重新引入名单轮询。
		// Parse 只操作局部跟踪器，完整准备通知与问候后才发布；返回值表示接纳了一次入服。
		template<typename TParse, typename TPrepareBroadcast, typename TShowBroadcast>
		bool OnMessage(const CSettings &Settings, const TParse &Parse, const TPrepareBroadcast &PrepareBroadcast, const TShowBroadcast &ShowBroadcast) noexcept
		{
			if(!Settings.m_GreetEnabled && !Settings.m_BroadcastEnabled)
			{
				Reset();
				return false;
			}
			if(!Settings.m_GreetEnabled || !Settings.m_GreetTextConfigured)
				CancelPending();
			try
			{
				CEnterNotifier Candidate = *this;
				const auto Message = Parse(Candidate.m_Tracker);
				if(Message.m_Event == EPresenceEvent::LEAVE)
				{
					Candidate.m_Pending.Remove(Message.m_Name);
					m_Tracker.Swap(Candidate.m_Tracker);
					m_Pending.Swap(Candidate.m_Pending);
					return false;
				}
				if(Message.m_Event != EPresenceEvent::ENTER)
					return false;
				std::string BroadcastText;
				if(Settings.m_BroadcastEnabled)
					BroadcastText = PrepareBroadcast(Message.m_Name);
				if(Settings.m_GreetEnabled && Settings.m_GreetTextConfigured)
					Candidate.m_Pending.Add(Message.m_Name);
				m_Tracker.Swap(Candidate.m_Tracker);
				m_Pending.Swap(Candidate.m_Pending);
				if(!BroadcastText.empty())
				{
					try
					{
						ShowBroadcast(BroadcastText);
					}
					catch(...)
					{
						// 显示失败不回退已接纳的消息，也不阻断已经准备好的待发问候。
					}
				}
				return true;
			}
			catch(...)
			{
				return false;
			}
		}

		// 沿原有聊天额度入口重试；发送前准备或回调失败不清空尚未确认的名字。
		template<typename TSend>
		bool TrySend(const CSettings &Settings, const TSend &Send) noexcept
		{
			if(!Settings.m_GreetEnabled || !Settings.m_GreetTextConfigured)
			{
				CancelPending();
				return false;
			}
			try
			{
				return m_Pending.TrySend(Send);
			}
			catch(...)
			{
				return false;
			}
		}
	};
}

#endif
