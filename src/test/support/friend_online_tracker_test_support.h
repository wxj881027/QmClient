#ifndef TEST_SUPPORT_FRIEND_ONLINE_TRACKER_TEST_SUPPORT_H
#define TEST_SUPPORT_FRIEND_ONLINE_TRACKER_TEST_SUPPORT_H

#include <game/client/components/qmclient/friend_online_tracker.h>

#include <gtest/gtest.h>

#include <new>
#include <utility>

namespace QmFriendTests
{
	struct CFailingFriendCopy
	{
		int m_FailAt;
		mutable int m_Calls = 0;
		qm_friend_notify::CFriend operator()(const qm_friend_notify::CFriend &Friend) const
		{
			if(++m_Calls == m_FailAt)
				throw std::bad_alloc();
			return Friend;
		}
	};

	inline std::vector<qm_friend_notify::CFriend> UpdateOnline(qm_friend_notify::COnlineTracker &Tracker,
		const std::vector<qm_friend_notify::CFriend> &vFriends, const std::unordered_set<std::string> &AvailableServers)
	{
		auto Notifications = Tracker.Update(vFriends, AvailableServers);
		EXPECT_TRUE(Notifications.has_value());
		return Notifications ? std::move(*Notifications) : std::vector<qm_friend_notify::CFriend>{};
	}
}

#endif
