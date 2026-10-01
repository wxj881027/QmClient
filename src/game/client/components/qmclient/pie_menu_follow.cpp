#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/friends.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/components/pie_menu.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

void CPieMenu::ConStopFollow(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CPieMenu *>(pUserData)->CancelFollow();
}

void CPieMenu::CancelFollow()
{
	const bool WasFollowing = m_FollowState.m_Active || g_Config.m_QmPieFollowName[0] != '\0';
	qm_pie_menu::StopFollow(m_FollowState);
	g_Config.m_QmPieFollowName[0] = '\0';
	g_Config.m_QmPieFollowClan[0] = '\0';
	m_NextFollowScan = 0.0;
	m_NextFollowRefresh = 0.0;
	if(WasFollowing)
		GameClient()->Echo(Localize("Cross-server following stopped"), true);
}

bool CPieMenu::IsFollowingTarget() const
{
	return IsFollowingPlayer(m_TargetName.c_str(), m_TargetClan.c_str());
}

bool CPieMenu::IsFollowingPlayer(const char *pName, const char *pClan) const
{
	return m_FollowState.m_Active && qm_pie_menu::MatchesPlayer(pName, pClan,
						 m_FollowState.m_Name.c_str(), m_FollowState.m_Clan.c_str(), g_Config.m_ClFriendsIgnoreClan != 0);
}

void CPieMenu::ToggleTargetFollow()
{
	if(!HasTargetPlayer())
		return;
	ToggleFollowPlayer(m_TargetClientId);
}

void CPieMenu::ToggleFollowPlayer(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE || ClientId < 0 || ClientId >= MAX_CLIENTS || GameClient()->IsLocalClientId(ClientId))
		return;
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(!Player.m_Active)
		return;
	if(IsFollowingPlayer(Player.m_aName, Player.m_aClan))
	{
		CancelFollow();
		return;
	}

	IFriends *pFriends = GameClient()->Friends();
	if(!pFriends->IsFriend(Player.m_aName, Player.m_aClan, true))
	{
		pFriends->AddFriend(Player.m_aName, Player.m_aClan, pFriends->DefaultCategory());
		if(!pFriends->IsFriend(Player.m_aName, Player.m_aClan, true))
		{
			GameClient()->Echo(Localize("Could not add the player as a friend"), true);
			return;
		}
		Client()->ServerBrowserUpdate();
	}
	str_copy(g_Config.m_QmPieFollowName, Player.m_aName);
	str_copy(g_Config.m_QmPieFollowClan, Player.m_aClan);
	qm_pie_menu::StartFollow(m_FollowState, g_Config.m_QmPieFollowName, g_Config.m_QmPieFollowClan);
	m_NextFollowScan = 0.0;
	m_NextFollowRefresh = 0.0;
	char aMessage[192];
	str_format(aMessage, sizeof(aMessage), Localize("Following %s across servers"), Player.m_aName);
	GameClient()->Echo(aMessage, true);
}

void CPieMenu::UpdateFollowState()
{
	// 控制台修改和重启恢复使用同一份目标配置；隐藏扇区、松开按键不取消跟随。
	if(m_FollowState.m_Name != g_Config.m_QmPieFollowName || m_FollowState.m_Clan != g_Config.m_QmPieFollowClan)
	{
		qm_pie_menu::StartFollow(m_FollowState, g_Config.m_QmPieFollowName, g_Config.m_QmPieFollowClan);
		m_NextFollowScan = 0.0;
		m_NextFollowRefresh = 0.0;
	}
	if(!m_FollowState.m_Active || Client()->State() >= IClient::STATE_DEMOPLAYBACK)
		return;

	const double Now = static_cast<double>(time_get()) / time_freq();
	if(Now < m_NextFollowScan)
		return;
	m_NextFollowScan = Now + 1.0;
	IServerBrowser *pBrowser = ServerBrowser();
	if(qm_pie_menu::FollowRefreshDue(pBrowser->IsGettingServerlist(), Now, g_Config.m_QmFriendOnlineRefreshSeconds, m_NextFollowRefresh))
		pBrowser->RefreshHttpServerList();

	char aCurrentAddress[NETADDR_MAXSTRSIZE] = "";
	std::string TargetAddress;
	const bool Online = Client()->State() == IClient::STATE_ONLINE;
	const bool Connecting = Client()->State() == IClient::STATE_CONNECTING || Client()->State() == IClient::STATE_LOADING;
	const bool IgnoreClan = g_Config.m_ClFriendsIgnoreClan != 0;
	if(Online)
	{
		net_addr_str(Client()->ServerAddress(), aCurrentAddress, sizeof(aCurrentAddress), true);
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			const auto &Player = GameClient()->m_aClients[ClientId];
			if(!Player.m_Active || GameClient()->IsLocalClientId(ClientId) ||
				!qm_pie_menu::MatchesPlayer(Player.m_aName, Player.m_aClan, m_FollowState.m_Name.c_str(), m_FollowState.m_Clan.c_str(), IgnoreClan))
				continue;
			TargetAddress = aCurrentAddress;
			break;
		}
	}

	if(TargetAddress.empty() && !Connecting)
	{
		// 刷新独立于好友通知和当前页面，读取完整 HTTP 列表，不受浏览器筛选影响。
		if(pBrowser->IsGettingServerlist() || pBrowser->IsServerlistError())
			return;
		for(int Index = 0; Index < pBrowser->NumHttpServers(); ++Index)
		{
			const CServerInfo *pServer = pBrowser->HttpGet(Index);
			if(pServer == nullptr)
				continue;
			bool IsCurrentServer = false;
			if(Online)
				for(int AddressIndex = 0; AddressIndex < pServer->m_NumAddresses; ++AddressIndex)
					IsCurrentServer |= net_addr_comp(Client()->ServerAddress(), &pServer->m_aAddresses[AddressIndex]) == 0;
			if(IsCurrentServer)
				continue;
			for(const CServerInfo::CClient &Player : pServer->m_vClients)
			{
				if(!qm_pie_menu::MatchesPlayer(Player.m_aName, Player.m_aClan, m_FollowState.m_Name.c_str(), m_FollowState.m_Clan.c_str(), IgnoreClan))
					continue;
				// 同名身份同时出现在多个服务器时等待下次刷新，避免误跳转。
				if(!TargetAddress.empty() && TargetAddress != pServer->m_aAddress)
				{
					m_FollowState.m_PendingAddress.clear();
					return;
				}
				TargetAddress = pServer->m_aAddress;
				break;
			}
		}
	}

	std::string ConnectAddress;
	if(qm_pie_menu::FollowStep(m_FollowState, !TargetAddress.empty(), TargetAddress.c_str(), aCurrentAddress, Connecting, Now, g_Config.m_QmFriendAutoFollowDelay, ConnectAddress))
	{
		str_copy(g_Config.m_UiServerAddress, ConnectAddress.c_str());
		Client()->Connect(ConnectAddress.c_str());
	}
}
