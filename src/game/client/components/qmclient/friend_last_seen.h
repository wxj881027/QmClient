#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_LAST_SEEN_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FRIEND_LAST_SEEN_H

#include "friend_last_seen_data.h"

#include <game/client/component.h>

#include <memory>

class IHttpRequest;
class IHttp;
class IEngine;
class IFriends;
class IStorage;
class CQmFriendLastSeenParseJob;

// 缓存和在途输入独立于界面；组件只提供当前接口、开关与时钟。
class CQmFriendLastSeenService
{
	using FCreateRequest = std::unique_ptr<IHttpRequest> (*)(const char *);
	CQmFriendLastSeenCache m_Cache;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::shared_ptr<CQmFriendLastSeenParseJob> m_pParseJob;
	uint64_t m_TransportRequestId = 0;
	uint64_t m_FriendsRevision = static_cast<uint64_t>(-1);
	FCreateRequest m_pCreateRequest;

public:
	explicit CQmFriendLastSeenService(FCreateRequest pCreateRequest) :
		m_pCreateRequest(pCreateRequest) {}
	CQmFriendLastSeenService(const CQmFriendLastSeenService &) = delete;
	CQmFriendLastSeenService &operator=(const CQmFriendLastSeenService &) = delete;
	~CQmFriendLastSeenService();
	const SQmFriendLastSeenEntry *Get(const char *pName, int64_t Now) { return m_Cache.Get(pName, Now); }
	const SQmFriendLastSeenEntry *Find(const char *pName) const { return m_Cache.Find(pName); }
	bool Load(IStorage &Storage, int64_t Now);
	bool Save(IStorage &Storage) const;
	void Update(const IFriends &Friends, IEngine &Engine, IHttp &Http, bool Enabled, int64_t Now);
	void CancelRequests() noexcept;
};

class CQmFriendLastSeen : public CComponent
{
	CQmFriendLastSeenService m_Service;

public:
	CQmFriendLastSeen();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;
	void OnShutdown() override;
	const SQmFriendLastSeenEntry *Get(const char *pName);
};

#endif
