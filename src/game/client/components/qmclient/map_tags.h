#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_TAGS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_TAGS_H

#include <memory>
#include <string>
#include <vector>

class IHttp;
class IHttpRequest;

namespace QmMapTags
{
	constexpr int MAX_TAGS = 64;
	constexpr int MAX_CHARACTERS = 10;
	struct STag
	{
		std::string m_Id;
		std::string m_Text;
		int m_Color = 0;
		int m_Count = 0;
		bool m_Voted = false;
	};
	struct SSnapshot
	{
		std::string m_Community;
		std::string m_Map;
		std::string m_Player;
		std::vector<STag> m_vTags;
		bool m_Eligible = false;
		bool m_EligibilityUnavailable = false;
	};
	bool ValidTag(const char *pText);
	bool ParseSnapshot(const char *pData, size_t Size, SSnapshot &Out);
	float ColorHue(int Color);

	class CClient
	{
		using TReadFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl);
		using TWriteFactory = std::unique_ptr<IHttpRequest> (*)(const char *pUrl, const char *pJson);
		TReadFactory m_CreateRead;
		TWriteFactory m_CreateWrite;
		std::string m_Community;
		std::string m_Map;
		std::string m_Player;
		std::shared_ptr<IHttpRequest> m_pRead;
		std::shared_ptr<IHttpRequest> m_pWrite;
		SSnapshot m_Snapshot;
		double m_NextRefresh = 0;
		const char *m_pStatus = "";
		bool m_Ready = false;
		bool m_ReadFailed = false;

	public:
		explicit CClient(TReadFactory ReadFactory = nullptr, TWriteFactory WriteFactory = nullptr);
		~CClient();
		CClient(const CClient &) = delete;
		CClient &operator=(const CClient &) = delete;
		void Clear();
		void SetContext(const char *pCommunity, const char *pMap, const char *pPlayer);
		void Poll();
		void Update(IHttp *pHttp, double Now);
		void Submit(IHttp *pHttp, const char *pTag);
		void Refresh() { m_NextRefresh = 0; }
		const SSnapshot &Snapshot() const { return m_Snapshot; }
		bool Ready() const { return m_Ready; }
		bool Busy() const { return m_pWrite != nullptr; }
		const char *Status() const { return m_pStatus; }
	};
}

#endif
