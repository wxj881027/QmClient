#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_SOURCES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_IME_QM_IME_SOURCES_H

#include <cstdint>
#include <string>

class CGameClient;
class IImeDomainLexicon;

// 只借用客户端与词库，在主线程轮询。词条去重和容量归词库管理。
class CQmImeSources
{
public:
	CQmImeSources(CGameClient *pGameClient, IImeDomainLexicon *pLexicon);
	void OnFrame();
	void Reset();

private:
	CGameClient *m_pGameClient;
	IImeDomainLexicon *m_pLexicon;
	int64_t m_NextPoll = 0;
	int64_t m_SessionStart = 0;
	bool m_HasSession = false;
	std::string m_ServerAddress;
	std::string m_Map;
};

#endif
