#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_LAST_SEEN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_LAST_SEEN_H

#include "friend_last_seen_data.h"

#include <game/client/component.h>

#include <memory>

class IHttpRequest;
class CQmFriendLastSeenParseJob;

class CQmFriendLastSeen : public CComponent
{
	CQmFriendLastSeenCache m_Cache;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::shared_ptr<CQmFriendLastSeenParseJob> m_pParseJob;
	SQmFriendLastSeenRequest m_Request{0, ""};
	uint64_t m_FriendsRevision = static_cast<uint64_t>(-1);

	void CancelRequests();

public:
	~CQmFriendLastSeen() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;
	void OnShutdown() override;
	const SQmFriendLastSeenEntry *Get(const char *pName);
};

#endif
