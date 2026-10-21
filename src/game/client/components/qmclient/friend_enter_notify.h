#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_NOTIFY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_ENTER_NOTIFY_H

#include "friend_enter_tracker.h"

#include <cstddef>
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
			bool m_IgnoreClan = false;
		};

	private:
		CEnterTracker m_Tracker;
		float m_NextCheck = 0.0f;
		std::string m_PendingNames;
		float m_PendingSendAt = 0.0f;

		template<typename TSendGreet>
		void ProcessPending(float Now, const CSettings &Settings, const TSendGreet &SendGreet) noexcept
		{
			if(!Settings.m_GreetEnabled)
			{
				m_PendingNames.clear();
				m_PendingSendAt = 0.0f;
				return;
			}
			if(m_PendingNames.empty() || Now < m_PendingSendAt)
				return;
			std::string Names;
			Names.swap(m_PendingNames);
			m_PendingSendAt = 0.0f;
			if(Settings.m_GreetTextConfigured)
			{
				// 发送入口可能已有副作用；先领取本批，失败也不在下一帧重复发送。
				try
				{
					SendGreet(Names);
				}
				catch(...)
				{
				}
			}
		}

	public:
		void Reset() noexcept
		{
			m_Tracker.Reset();
			m_NextCheck = 0.0f;
			m_PendingNames.clear();
			m_PendingSendAt = 0.0f;
		}

		// 连接、好友身份或开关/分身变化时由调用方先 Reset；文本设置在消费时读取。
		// 回调同步采集自有名单、准备文本或消费输出；返回值只表示新名单是否提交。
		template<typename TCollect, typename TPrepareBroadcast, typename TShowBroadcast, typename TSendGreet>
		bool Update(float Now, const CSettings &Settings, const TCollect &Collect, const TPrepareBroadcast &PrepareBroadcast, const TShowBroadcast &ShowBroadcast, const TSendGreet &SendGreet) noexcept
		{
			if(!Settings.m_GreetEnabled && !Settings.m_BroadcastEnabled)
			{
				Reset();
				return false;
			}
			// 到期问候不依赖下一次名单采集，保持原先先发送、后采集的顺序。
			ProcessPending(Now, Settings, SendGreet);
			if(Now < m_NextCheck)
				return false;
			m_NextCheck = Now + 0.2f;
			try
			{
				const auto vClients = Collect();
				CEnterNotifier Candidate = *this;
				const auto vNewFriends = Candidate.m_Tracker.Update(vClients, Now, Settings.m_IgnoreClan);
				std::string NewNames;
				if(!vNewFriends.empty())
				{
					NewNames.reserve(64);
					for(std::size_t Index = 0; Index < vNewFriends.size(); ++Index)
					{
						if(Index > 0)
							NewNames.push_back(' ');
						NewNames.append(vNewFriends[Index]);
					}
				}
				std::string BroadcastText;
				if(!NewNames.empty())
				{
					if(Settings.m_GreetEnabled && Settings.m_GreetTextConfigured)
					{
						if(!Candidate.m_PendingNames.empty())
							Candidate.m_PendingNames.push_back(' ');
						Candidate.m_PendingNames.append(NewNames);
						if(Candidate.m_PendingSendAt <= 0.0f)
							Candidate.m_PendingSendAt = Now + 3.0f;
					}
					if(Settings.m_BroadcastEnabled)
						BroadcastText = PrepareBroadcast(NewNames);
				}

				m_Tracker.Swap(Candidate.m_Tracker);
				m_PendingNames.swap(Candidate.m_PendingNames);
				m_PendingSendAt = Candidate.m_PendingSendAt;
				if(!BroadcastText.empty())
				{
					try
					{
						ShowBroadcast(BroadcastText);
					}
					catch(...)
					{
					}
				}
				return true;
			}
			catch(...)
			{
				return false;
			}
		}
	};
}

#endif
