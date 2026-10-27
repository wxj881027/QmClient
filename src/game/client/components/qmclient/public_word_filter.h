#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PUBLIC_WORD_FILTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PUBLIC_WORD_FILTER_H

#include "word_filter_list.h"

#include <cstdint>
#include <memory>
#include <optional>

class IHttp;
class IHttpRequest;

namespace QmPublicWordFilter
{

	constexpr int64_t REFRESH_SECONDS = 30 * 60;
	constexpr int64_t RETRY_SECONDS = 60;
	constexpr std::string_view RESPONSE_HEADER = "# QmClient public word filter v1\n";

	bool IsIncomingPlayerChat(int ClientId, bool LocalClient, bool OutgoingWhisper, bool DemoPlayback);

	class CState
	{
		QmWordFilterList::CFileList m_List;
		bool m_Enabled = false;
		bool m_HasSnapshot = false;
		bool m_Refreshing = false;
		uint64_t m_Generation = 0;
		int64_t m_NextRefresh = 0;
		std::string m_Error;

	public:
		void SetEnabled(bool Enabled);
		bool Restore(std::string_view Content);
		uint64_t StartRefresh(int64_t Now);
		bool CompleteRefresh(uint64_t Generation, std::optional<std::string_view> Content, int64_t Now);
		bool Filter(std::string &Text, const char *pReplacement, bool MultiReplace, std::vector<std::string> *pMatched = nullptr) const;
		bool HasSnapshot() const { return m_HasSnapshot; }
		bool Refreshing() const { return m_Refreshing; }
		const std::vector<std::string> &Words() const { return m_List.Words(); }
		const std::string &Error() const { return m_Error; }
	};

	class CClient
	{
		CState m_State;
		std::shared_ptr<IHttpRequest> m_pRequest;
		uint64_t m_RequestGeneration = 0;

	public:
		~CClient();
		void Initialize(IStorage *pStorage, bool Enabled);
		void Update(IHttp *pHttp, IStorage *pStorage, bool Enabled, int64_t Now);
		void Shutdown();
		const CState &State() const { return m_State; }
	};

}

#endif
