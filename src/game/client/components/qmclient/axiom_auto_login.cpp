// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "axiom_auto_login.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

const char *CQmAxiomAutoLogin::CurrentCommunityId() const
{
	// 直连服务器可能不在服务器列表中；在线后的 ServerInfo 是当前连接的权威来源。
	if(Client()->State() == IClient::STATE_ONLINE)
	{
		const char *pCurrentCommunityId = Client()->ServerInfo().m_aCommunityId;
		if(pCurrentCommunityId[0] != '\0')
			return pCurrentCommunityId;
	}

	IServerBrowser *pServerBrowser = ServerBrowser();
	if(!pServerBrowser)
		return nullptr;

	const char *pCommunityId = nullptr;
	const NETADDR *pServerAddr = Client()->ServerAddress();
	const IServerBrowser::CServerEntry *pEntry = pServerAddr ? pServerBrowser->Find(*pServerAddr) : nullptr;
	if(pEntry)
		pCommunityId = pEntry->m_Info.m_aCommunityId;
	else if(GameClient()->m_ConnectServerInfo)
		pCommunityId = GameClient()->m_ConnectServerInfo->m_aCommunityId;

	if(!pCommunityId || pCommunityId[0] == '\0')
		return nullptr;
	return pCommunityId;
}

bool CQmAxiomAutoLogin::IsAxiomCommunity() const
{
	const char *pCommunityId = CurrentCommunityId();
	if(!pCommunityId)
		return false;

	if(str_find_nocase(pCommunityId, "axiom"))
		return true;

	IServerBrowser *pServerBrowser = ServerBrowser();
	if(!pServerBrowser)
		return false;

	const CCommunity *pCommunity = pServerBrowser->Community(pCommunityId);
	return pCommunity && pCommunity->Name() && str_find_nocase(pCommunity->Name(), "axiom");
}

void CQmAxiomAutoLogin::EnableDummyReconnectForServer()
{
	m_DummyLoginAllowedThisServer = true;
}

void CQmAxiomAutoLogin::DisableDummyReconnectForServer()
{
	m_DummyLoginAllowedThisServer = false;
	m_DummyAutoLoginSent = false;
	m_aDummyAutoLoginServer[0] = '\0';
}

void CQmAxiomAutoLogin::ResetState()
{
	m_AutoLoginState = {};
	m_aAutoLoginServer[0] = '\0';
	m_DummyAutoLoginSent = false;
	m_DummyWasConnected = false;
	m_DummyLoginAllowedThisServer = false;
	m_aDummyAutoLoginServer[0] = '\0';
}

void CQmAxiomAutoLogin::TrySendLogin()
{
	if(g_Config.m_QmAxiomAutoLogin == 0 || g_Config.m_QmAxiomLoginPassword[0] == '\0')
		return;
	if(Client()->State() != IClient::STATE_ONLINE || !IsAxiomCommunity())
		return;
	if(m_AutoLoginState.m_Succeeded || m_AutoLoginState.m_HardFailed)
		return;

	char aServerAddress[NETADDR_MAXSTRSIZE] = "";
	const NETADDR *pServerAddr = Client()->ServerAddress();
	if(pServerAddr)
		net_addr_str(pServerAddr, aServerAddress, sizeof(aServerAddress), true);
	if(aServerAddress[0] != '\0')
		str_copy(m_aAutoLoginServer, aServerAddress, sizeof(m_aAutoLoginServer));

	char aLoginCommand[192];
	str_format(aLoginCommand, sizeof(aLoginCommand), "/login %s", g_Config.m_QmAxiomLoginPassword);
	GameClient()->m_Chat.SendChatOnConn(IClient::CONN_MAIN, 0, aLoginCommand);

	QmMarkAxiomAutoLoginAttempt(m_AutoLoginState, time_get(), time_freq());

	if(!m_AutoLoginState.m_Announced)
	{
		GameClient()->Echo(Localize("Trying Axiom auto login"));
		m_AutoLoginState.m_Announced = true;
	}
}

void CQmAxiomAutoLogin::TrySendDummyLogin()
{
	if(g_Config.m_QmAxiomAutoLogin == 0 || g_Config.m_QmAxiomDummyLoginPassword[0] == '\0')
		return;
	if(Client()->State() != IClient::STATE_ONLINE || !Client()->DummyConnected() || !IsAxiomCommunity())
		return;
	if(!m_DummyLoginAllowedThisServer)
		return;
	if(m_DummyAutoLoginSent)
		return;

	char aServerAddress[NETADDR_MAXSTRSIZE] = "";
	const NETADDR *pServerAddr = Client()->ServerAddress();
	if(pServerAddr)
		net_addr_str(pServerAddr, aServerAddress, sizeof(aServerAddress), true);
	if(aServerAddress[0] != '\0')
		str_copy(m_aDummyAutoLoginServer, aServerAddress, sizeof(m_aDummyAutoLoginServer));

	char aLoginCommand[192];
	str_format(aLoginCommand, sizeof(aLoginCommand), "/login %s", g_Config.m_QmAxiomDummyLoginPassword);
	GameClient()->m_Chat.SendChatOnConn(IClient::CONN_DUMMY, 0, aLoginCommand);
	m_DummyAutoLoginSent = true;
	GameClient()->Echo(Localize("Trying Axiom dummy auto login"));
}

void CQmAxiomAutoLogin::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK || MsgType != NETMSGTYPE_SV_CHAT)
		return;

	CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
	if(pMsg->m_ClientId >= 0 || !pMsg->m_pMessage)
		return;

	const char *pText = pMsg->m_pMessage;
	if(m_AutoLoginState.m_Succeeded || m_AutoLoginState.m_HardFailed || !IsAxiomCommunity() || !pText || pText[0] == '\0')
		return;

	const EQmAxiomLoginReply Reply = QmClassifyAxiomLoginReply(pText);
	const EQmAxiomLoginReply AppliedReply = QmApplyAxiomLoginReply(m_AutoLoginState, Reply, time_get(), time_freq());
	if(AppliedReply == EQmAxiomLoginReply::IGNORE || AppliedReply == EQmAxiomLoginReply::PENDING)
		return;

	if(AppliedReply == EQmAxiomLoginReply::HARD_FAILURE)
	{
		GameClient()->Echo(Localize("Axiom auto login failed"));
		return;
	}

	if(AppliedReply == EQmAxiomLoginReply::SUCCESS)
	{
		GameClient()->Echo(Localize("Axiom auto login succeeded"));
	}
	else if(AppliedReply == EQmAxiomLoginReply::RETRYABLE_FAILURE)
	{
		GameClient()->Echo(Localize("Axiom auto login failed, retrying"));
	}
}

void CQmAxiomAutoLogin::OnUpdate()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		return;
	}
	if(g_Config.m_QmAxiomAutoLogin == 0)
	{
		if(m_AutoLoginEnabledLastFrame)
			ResetState();
		m_AutoLoginEnabledLastFrame = false;
		return;
	}
	if(!m_AutoLoginEnabledLastFrame)
	{
		ResetState();
		m_AutoLoginEnabledLastFrame = true;
	}
	if(!IsAxiomCommunity())
	{
		ResetState();
		return;
	}

	char aServerAddress[NETADDR_MAXSTRSIZE] = "";
	const NETADDR *pServerAddr = Client()->ServerAddress();
	if(pServerAddr)
		net_addr_str(pServerAddr, aServerAddress, sizeof(aServerAddress), true);
	if(m_aAutoLoginServer[0] != '\0' && aServerAddress[0] != '\0' && str_comp(m_aAutoLoginServer, aServerAddress) != 0)
		ResetState();

	const bool DummyConnected = Client()->DummyConnected();
	if(!DummyConnected || g_Config.m_QmAxiomDummyLoginPassword[0] == '\0')
	{
		m_DummyAutoLoginSent = false;
		m_aDummyAutoLoginServer[0] = '\0';
		if(!DummyConnected && m_DummyLoginAllowedThisServer && g_Config.m_QmAxiomDummyLoginPassword[0] != '\0' && !Client()->DummyConnecting() && !Client()->DummyConnectingDelayed() && Client()->DummyAllowed())
			Client()->DummyConnect();
	}
	else
	{
		if(m_aDummyAutoLoginServer[0] != '\0' && aServerAddress[0] != '\0' && str_comp(m_aDummyAutoLoginServer, aServerAddress) != 0)
		{
			m_DummyAutoLoginSent = false;
			m_DummyLoginAllowedThisServer = false;
			m_aDummyAutoLoginServer[0] = '\0';
		}

		if(!m_DummyAutoLoginSent)
			TrySendDummyLogin();
	}
	m_DummyWasConnected = DummyConnected;

	if(g_Config.m_QmAxiomLoginPassword[0] == '\0')
	{
		m_AutoLoginState = {};
		m_aAutoLoginServer[0] = '\0';
	}
	else if(QmUpdateAxiomAutoLoginState(m_AutoLoginState, time_get(), time_freq()))
	{
		TrySendLogin();
	}
}

void CQmAxiomAutoLogin::OnStateChange(int NewState, int OldState)
{
	CComponent::OnStateChange(NewState, OldState);
	// 地图切换通常是 ONLINE -> LOADING -> ONLINE，同一连接的认证状态应保留。
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_DEMOPLAYBACK || OldState == IClient::STATE_DEMOPLAYBACK)
		ResetState();
}
