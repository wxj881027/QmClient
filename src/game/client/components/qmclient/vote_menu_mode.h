#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_MENU_MODE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_MENU_MODE_H

#include <engine/serverbrowser.h>

#include <string_view>

class CQmVoteMenuMode
{
	bool m_OfficialDdnet = false;
	bool m_Axiom = false;
	bool m_ShowServerOptions = false;

public:
	bool UpdateCommunity(std::string_view CommunityId, bool AxiomCommunity = false)
	{
		// 社区 ID 由服务器浏览器按地址匹配，游戏类型不能证明官方身份。
		const bool OfficialDdnet = CommunityId == IServerBrowser::COMMUNITY_DDNET;
		const bool Axiom = !OfficialDdnet && (AxiomCommunity || CommunityId == "axiom");
		if(m_OfficialDdnet == OfficialDdnet && m_Axiom == Axiom)
			return false;
		m_OfficialDdnet = OfficialDdnet;
		m_Axiom = Axiom;
		m_ShowServerOptions = false;
		return true;
	}

	bool ToggleServerOptions()
	{
		if(!m_OfficialDdnet && !m_Axiom)
			return false;
		m_ShowServerOptions = !m_ShowServerOptions;
		return true;
	}

	void Reset()
	{
		m_OfficialDdnet = false;
		m_Axiom = false;
		m_ShowServerOptions = false;
	}

	bool IsOfficialDdnet() const { return m_OfficialDdnet; }
	bool IsAxiom() const { return m_Axiom; }
	bool UseMapLibrary() const { return (m_OfficialDdnet || m_Axiom) && !m_ShowServerOptions; }
};

#endif
