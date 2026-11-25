#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CROSS_SERVER_FOLLOW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CROSS_SERVER_FOLLOW_H

#include "cross_server_follow_logic.h"

#include <engine/console.h>

#include <game/client/component.h>

class CServerInfo;

class CCrossServerFollow final : public CComponent
{
	CCrossServerFollowEngine m_Engine;
	std::string m_AutomaticConnectAddress;
	double m_NextScan = 0.0;
	double m_NextRefresh = 0.0;
	bool m_Initialized = false;
	bool m_AwaitingConnectionStart = false;

	static void ConFollow(IConsole::IResult *pResult, void *pUserData);
	static void ConStopFollow(IConsole::IResult *pResult, void *pUserData);

	bool CanFollowClientId(int ClientId) const;
	bool IsLocalIdentity(const SFollowTarget &Target) const;
	void Begin(const char *pName, const char *pClan, const NETADDR *pTogetherAddress, const CServerInfo *pImmediateServerHint);
	void UpdateFollow(double Now, const CServerInfo *pImmediateServerHint = nullptr);
	void ApplyDecision(const SFollowDecision &Decision, double Now);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;

	bool Start(const char *pName, const char *pClan = "", const CServerInfo *pImmediateServerHint = nullptr);
	bool StartByClientId(int ClientId);
	void Stop(bool Notify = true);
	void Toggle(const char *pName, const char *pClan = "", const CServerInfo *pImmediateServerHint = nullptr);
	void ToggleByClientId(int ClientId);
	bool IsActive() const { return m_Engine.Target().m_Active; }
	bool IsFollowing(const char *pName, const char *pClan = "") const;
	const char *TargetName() const { return m_Engine.Target().m_Name.c_str(); }
	const char *TargetClan() const { return m_Engine.Target().m_Clan.c_str(); }
	void OnManualConnect(const char *pAddress);
	void NotifyManualDisconnect();
};

#endif
