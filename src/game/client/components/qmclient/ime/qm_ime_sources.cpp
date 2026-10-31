#include "qm_ime_sources.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/qm_ime_provider.h>

#include <game/client/gameclient.h>

#include <string>
#include <vector>

CQmImeSources::CQmImeSources(CGameClient *pGameClient, IImeDomainLexicon *pLexicon) :
	m_pGameClient(pGameClient), m_pLexicon(pLexicon)
{
}

void CQmImeSources::Reset()
{
	if(m_pLexicon != nullptr)
		m_pLexicon->BeginSession();
	m_NextPoll = 0;
	m_SessionStart = 0;
	m_HasSession = false;
	m_ServerAddress.clear();
	m_Map.clear();
}

void CQmImeSources::OnFrame()
{
	if(m_pGameClient == nullptr || m_pLexicon == nullptr || m_pGameClient->Client() == nullptr)
		return;
	const int64_t Now = time_get();
	if(Now < m_NextPoll)
		return;
	m_NextPoll = Now + 2 * time_freq();
	IClient *pClient = m_pGameClient->Client();
	if(pClient->State() != IClient::STATE_ONLINE)
	{
		// 加载和重连期间保留上次快照；真正断开时清掉会话词。
		if(pClient->State() == IClient::STATE_OFFLINE && m_HasSession)
			Reset();
		return;
	}
	const char *pMap = pClient->GetCurrentMap();
	const NETADDR *pAddress = pClient->ServerAddress();
	if(pMap == nullptr || pMap[0] == '\0' || pAddress == nullptr)
		return;
	char aAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(pAddress, aAddress, sizeof(aAddress), true);
	const bool NewSession = !m_HasSession || m_ServerAddress != aAddress || m_Map != pMap;
	if(NewSession)
	{
		m_pLexicon->BeginSession();
		m_ServerAddress = aAddress;
		m_Map = pMap;
		m_HasSession = true;
		m_SessionStart = Now;
		m_pLexicon->FeedCommands({"/w", "/team", "/me", "/pause", "/spec", "/r", "/rescue", "/rank", "/top5", "/info", "/help"});
	}
	m_pLexicon->FeedMapName(pMap);
	const auto &Server = pClient->ServerInfo();
	if(Server.m_aName[0] != '\0')
		m_pLexicon->FeedServerName(Server.m_aName);
	std::vector<std::string> vNames;
	vNames.reserve(MAX_CLIENTS);
	for(const auto &Client : m_pGameClient->m_aClients)
		if(Client.m_Active && Client.m_aName[0] != '\0')
			vNames.emplace_back(Client.m_aName);
	// 聊天提供有界索引接口，无需向 gameclient/chat 增加通知或开放私有容器。
	for(int Index = 0; ; ++Index)
	{
		const auto *pLine = m_pGameClient->m_Chat.GetLineByIndex(Index);
		if(pLine == nullptr)
			break;
		if(!pLine->m_Initialized || pLine->m_ClientId < 0 || pLine->m_Time < m_SessionStart)
			continue;
		if(pLine->m_vMergedAuthors.empty() && pLine->m_aName[0] != '\0')
			vNames.emplace_back(pLine->m_aName);
		for(const auto &Author : pLine->m_vMergedAuthors)
			if(Author.m_aPlayerName[0] != '\0')
				vNames.emplace_back(Author.m_aPlayerName);
	}
	m_pLexicon->FeedPlayerNames(vNames);
}
