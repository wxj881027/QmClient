#include "cross_server_follow.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

namespace
{
	double FollowTime()
	{
		return static_cast<double>(time_get()) / time_freq();
	}

	std::span<const NETADDR> ServerAddresses(const CServerInfo &Server)
	{
		return std::span(Server.m_aAddresses).first(std::clamp(Server.m_NumAddresses, 0, static_cast<int>(MAX_SERVER_ADDRESSES)));
	}

	SFollowServerCandidate MakeCandidate(const CServerInfo &Server, std::span<const NETADDR> Addresses, SFollowPlayerMatches Players)
	{
		return {Server.m_aAddress, Server.m_aName, Server.m_aMap, {Addresses.begin(), Addresses.end()},
			Server.m_NumPlayers >= Server.m_MaxPlayers || Server.m_NumClients >= Server.m_MaxClients,
			(Server.m_Flags & SERVER_FLAG_PASSWORD) != 0, Players};
	}

	SFollowPlayerMatches ServerPlayerMatches(const CServerInfo &Server, const SFollowTarget &Target)
	{
		SFollowPlayerMatches Matches;
		for(const auto &Player : Server.m_vClients)
			Matches.Add(Target, Player.m_aName, Player.m_aClan);
		return Matches;
	}
}

void CCrossServerFollow::OnConsoleInit()
{
	Console()->Register("qm_follow", "s[name] ?s[clan]", CFGFLAG_CLIENT, ConFollow, this, "Follow a player across servers for this session");
	Console()->Register("qm_stop_follow", "", CFGFLAG_CLIENT, ConStopFollow, this, "Stop following a player across servers");
	Console()->Register("qm_pie_menu_stop_follow", "", CFGFLAG_CLIENT, ConStopFollow, this, "Stop following a player across servers");
}

void CCrossServerFollow::OnInit()
{
	m_Initialized = true;
}

void CCrossServerFollow::ConFollow(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CCrossServerFollow *>(pUserData)->Start(pResult->GetString(0), pResult->NumArguments() > 1 ? pResult->GetString(1) : "");
}

void CCrossServerFollow::ConStopFollow(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CCrossServerFollow *>(pUserData)->Stop();
}

bool CCrossServerFollow::IsFollowing(const char *pName, const char *pClan) const
{
	return pName && m_Engine.Target().Matches(pName, pClan ? pClan : "", g_Config.m_ClFriendsIgnoreClan != 0);
}

bool CCrossServerFollow::IsLocalIdentity(const SFollowTarget &Target) const
{
	const bool IgnoreClan = g_Config.m_ClFriendsIgnoreClan != 0;
	for(int Conn = 0; Conn < NUM_DUMMIES; ++Conn)
	{
		const int Id = Client()->State() == IClient::STATE_ONLINE ? GameClient()->m_aLocalIds[Conn] : -1;
		if(Id >= 0 && Id < MAX_CLIENTS && GameClient()->m_aClients[Id].m_Active)
		{
			const auto &Player = GameClient()->m_aClients[Id];
			if(Target.Matches(Player.m_aName, Player.m_aClan, IgnoreClan))
				return true;
		}
		else if(Target.Matches(Conn == IClient::CONN_MAIN ? Client()->PlayerName() : Client()->DummyName(),
				Conn == IClient::CONN_MAIN ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan, IgnoreClan))
			return true;
	}
	return false;
}

bool CCrossServerFollow::CanFollowClientId(int ClientId) const
{
	return Client()->State() == IClient::STATE_ONLINE && ClientId >= 0 && ClientId < MAX_CLIENTS &&
	       !GameClient()->IsLocalClientId(ClientId) && GameClient()->m_aClients[ClientId].m_Active &&
	       GameClient()->m_aClients[ClientId].m_aName[0] != '\0';
}

bool CCrossServerFollow::Start(const char *pName, const char *pClan, const CServerInfo *pImmediateServerHint)
{
	if(!pName || !pName[0] || Client()->State() > IClient::STATE_DEMOPLAYBACK ||
		IsLocalIdentity({true, pName, pClan ? pClan : ""}))
		return false;

	Begin(pName, pClan ? pClan : "", nullptr, pImmediateServerHint);
	return true;
}

bool CCrossServerFollow::StartByClientId(int ClientId)
{
	if(!CanFollowClientId(ClientId))
		return false;

	const auto &Player = GameClient()->m_aClients[ClientId];
	Begin(Player.m_aName, Player.m_aClan, Client()->ServerAddress(), nullptr);
	return true;
}

void CCrossServerFollow::Begin(const char *pName, const char *pClan, const NETADDR *pTogetherAddress, const CServerInfo *pImmediateServerHint)
{
	m_Engine.Start(pName, pClan, pTogetherAddress);
	m_AwaitingConnectionStart = false;
	m_NextScan = 0.0;
	m_NextRefresh = 0.0;
	Client()->SetReconnectTime(0);
	if(m_Initialized)
	{
		char aMessage[192];
		str_format(aMessage, sizeof(aMessage), Localize("Following %s across servers"), TargetName());
		GameClient()->Echo(aMessage, true);
		if(pImmediateServerHint)
			UpdateFollow(FollowTime(), pImmediateServerHint);
	}
}

void CCrossServerFollow::Stop(bool Notify)
{
	if(!IsActive())
		return;
	m_Engine.OnManualConnectOrDisconnect();
	m_AutomaticConnectAddress.clear();
	m_AwaitingConnectionStart = false;
	if(Notify && m_Initialized)
		GameClient()->Echo(Localize("Cross-server following stopped"), true);
}

void CCrossServerFollow::Toggle(const char *pName, const char *pClan, const CServerInfo *pImmediateServerHint)
{
	if(IsFollowing(pName, pClan))
		Stop();
	else
		Start(pName, pClan, pImmediateServerHint);
}

void CCrossServerFollow::ToggleByClientId(int ClientId)
{
	if(!CanFollowClientId(ClientId))
		return;
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(IsFollowing(Player.m_aName, Player.m_aClan))
		Stop();
	else
		StartByClientId(ClientId);
}

void CCrossServerFollow::OnManualConnect(const char *pAddress)
{
	if(!IsActive())
		return;

	// 护栏由 Connect 入口消费一次，连接脚本再次发起的连接仍然属于手动请求。
	if(!m_AutomaticConnectAddress.empty() && m_AutomaticConnectAddress == pAddress)
	{
		m_AutomaticConnectAddress.clear();
		return;
	}

	char aToken[128];
	std::vector<NETADDR> vAddresses;
	while((pAddress = str_next_token(pAddress, ",", aToken, sizeof(aToken))))
	{
		NETADDR Address;
		if(net_addr_from_url(&Address, aToken, nullptr, 0) && net_addr_from_str(&Address, aToken))
		{
			vAddresses.clear();
			break;
		}
		if(Address.port == 0)
			Address.port = 8303;
		vAddresses.push_back(Address);
	}
	if(m_Engine.TryManualConnectToTarget(vAddresses, FollowTime()))
		m_AwaitingConnectionStart = true;
	else
		NotifyManualDisconnect();
}

void CCrossServerFollow::NotifyManualDisconnect()
{
	Stop();
}

void CCrossServerFollow::OnStateChange(int NewState, int OldState)
{
	if(!IsActive())
		return;
	if(NewState == IClient::STATE_CONNECTING)
		m_AwaitingConnectionStart = false;
	else if(NewState == IClient::STATE_ONLINE)
		m_Engine.OnConnectionEstablished(Client()->ServerAddress());
	else if(!m_AwaitingConnectionStart && NewState == IClient::STATE_OFFLINE && (OldState == IClient::STATE_CONNECTING || OldState == IClient::STATE_LOADING))
		m_Engine.OnConnectionFailed(FollowTime(), str_find_nocase(Client()->ErrorString(), "password") != nullptr);
}

void CCrossServerFollow::OnUpdate()
{
	if(IsActive())
		UpdateFollow(FollowTime());
}

void CCrossServerFollow::UpdateFollow(double Now, const CServerInfo *pImmediateServerHint)
{
	const int State = Client()->State();
	if(State >= IClient::STATE_DEMOPLAYBACK || Client()->PoseSessionActive() || Now < m_NextScan)
		return;
	m_NextScan = Now + 0.5;

	IServerBrowser *pBrowser = ServerBrowser();
	if(Now >= m_NextRefresh)
	{
		// HTTP 浏览器会合并正在进行的刷新，扫描始终读取当前可用的列表。
		pBrowser->RefreshHttpServerList();
		m_NextRefresh = Now + std::clamp(g_Config.m_QmFriendOnlineRefreshSeconds, 5, 10);
	}
	if(State == IClient::STATE_CONNECTING || State == IClient::STATE_LOADING)
		return;

	const NETADDR *pCurrentAddress = State == IClient::STATE_ONLINE ? Client()->ServerAddress() : nullptr;
	if(State == IClient::STATE_ONLINE && !pCurrentAddress)
		return;

	const bool HasCurrentSnapshot = pCurrentAddress && GameClient()->m_Snap.m_pLocalInfo;
	const bool IgnoreClan = g_Config.m_ClFriendsIgnoreClan != 0;
	const auto &Target = m_Engine.Target();
	std::vector<NETADDR> vCurrentAddresses;
	SFollowPlayerMatches CurrentPlayers;
	if(pCurrentAddress)
		vCurrentAddresses.push_back(*pCurrentAddress);
	if(HasCurrentSnapshot)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const auto &Player = GameClient()->m_aClients[ClientId];
			if(Player.m_Active && !GameClient()->IsLocalClientId(ClientId))
				CurrentPlayers.Add(Target, Player.m_aName, Player.m_aClan);
		}
	}

	const int Delay = g_Config.m_QmFriendAutoFollowDelay;
	if(pImmediateServerHint)
	{
		const auto Addresses = ServerAddresses(*pImmediateServerHint);
		const auto Players = ServerPlayerMatches(*pImmediateServerHint, Target);
		// 好友行提供了玩家自己选中的服务器，直接尝试这个明确的目标。
		if(!Addresses.empty() && pImmediateServerHint->m_aAddress[0] &&
			(Target.m_Clan.empty() || IgnoreClan ? Players.m_NameMatches : Players.m_ClanMatches) > 0 &&
			!QmFollowSameServer(vCurrentAddresses, Addresses))
		{
			const auto Candidate = MakeCandidate(*pImmediateServerHint, Addresses, Players);
			ApplyDecision(m_Engine.Step(Now, Delay, false, std::span(&Candidate, 1), vCurrentAddresses, {}, IgnoreClan), Now);
			return;
		}
	}
	// ONLINE 先于首个快照。可执行玩家明确选中的外服跳转，后台判断离服需要本地名单。
	if(pCurrentAddress && !HasCurrentSnapshot)
		return;

	std::vector<SFollowServerCandidate> vCandidates;
	const int HttpCount = pBrowser->NumHttpServers();
	const int Count = HttpCount > 0 ? HttpCount : pBrowser->NumServers();
	for(int Index = 0; Index < Count; ++Index)
	{
		const CServerInfo *pServer = HttpCount > 0 ? pBrowser->HttpGet(Index) : pBrowser->Get(Index);
		if(!pServer)
			continue;
		const auto Addresses = ServerAddresses(*pServer);
		const auto Matches = ServerPlayerMatches(*pServer, Target);
		if(pCurrentAddress && QmFollowSameServer(std::span(pCurrentAddress, 1), Addresses))
			vCurrentAddresses.insert(vCurrentAddresses.end(), Addresses.begin(), Addresses.end());
		if(Matches.m_NameMatches > 0)
			vCandidates.push_back(MakeCandidate(*pServer, Addresses, Matches));
	}
	ApplyDecision(m_Engine.Step(Now, Delay, false, vCandidates, vCurrentAddresses, CurrentPlayers, IgnoreClan), Now);
}

void CCrossServerFollow::ApplyDecision(const SFollowDecision &Decision, double Now)
{
	if(Decision.m_Action == EFollowAction::NONE)
		return;

	const auto &Server = *Decision.m_pServer;
	const char *pServerName = Server.m_ServerName.empty() ? Server.m_ConnectAddress.c_str() : Server.m_ServerName.c_str();
	char aDestination[256];
	str_format(aDestination, sizeof(aDestination), "%s%s%s", pServerName, Server.m_MapName.empty() ? "" : " / ", Server.m_MapName.c_str());
	char aMessage[384];
	switch(Decision.m_Action)
	{
	case EFollowAction::NOTIFY_PENDING_SWITCH:
		str_format(aMessage, sizeof(aMessage), Localize("%s moved to %s. Following in %d seconds..."), TargetName(), aDestination, Decision.m_SecondsRemaining);
		break;
	case EFollowAction::NOTIFY_SERVER_FULL:
		str_format(aMessage, sizeof(aMessage), Localize("%s is full. Waiting for a slot..."), aDestination);
		break;
	case EFollowAction::NOTIFY_PASSWORD_REQUIRED:
		str_format(aMessage, sizeof(aMessage), Localize("%s requires a password. Connect manually to enter it."), aDestination);
		break;
	case EFollowAction::CONNECT:
		str_format(aMessage, sizeof(aMessage), Localize("Following %s to %s"), TargetName(), aDestination);
		str_copy(g_Config.m_UiServerAddress, Server.m_ConnectAddress.c_str());
		m_AutomaticConnectAddress = Server.m_ConnectAddress;
		m_AwaitingConnectionStart = true;
		Client()->Connect(Server.m_ConnectAddress.c_str());
		m_AutomaticConnectAddress.clear();
		m_AwaitingConnectionStart = false;
		if(!IsActive() || !m_Engine.IsFollowingServer(Server.m_vAddresses))
			return;
		if(Client()->State() == IClient::STATE_OFFLINE)
			m_Engine.OnConnectionFailed(Now, false);
		break;
	case EFollowAction::NONE:
		return;
	}
	GameClient()->Echo(aMessage, true);
}
