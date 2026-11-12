#include "friend_last_seen.h"

#include "axiom_scores_data.h"
#include "bounded_file_reader.h"

#include <base/system.h>

#include <engine/engine.h>
#include <engine/friends.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <utility>

namespace
{
	constexpr const char *CACHE_FILE = "qm_friend_last_seen.json";
	constexpr const char *CACHE_TEMP_FILE = "qm_friend_last_seen.json.tmp";
}

// 任务独立持有 HTTP 响应，不借用组件；关闭功能后可以直接丢弃其结果。
class CQmFriendLastSeenParseJob : public IJob
{
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::string m_Name;
	std::optional<std::string> m_Date;

	void Run() override
	{
		unsigned char *pData = nullptr;
		size_t Size = 0;
		m_pRequest->Result(&pData, &Size);
		if(pData)
			m_Date = QmParseDdStatsLastSeen(std::string_view(reinterpret_cast<const char *>(pData), Size), m_Name.c_str());
	}

public:
	CQmFriendLastSeenParseJob(std::shared_ptr<IHttpRequest> pRequest, std::string Name) :
		m_pRequest(std::move(pRequest)), m_Name(std::move(Name)) {}
	std::optional<std::string> TakeDate() { return std::move(m_Date); }
};

CQmFriendLastSeen::~CQmFriendLastSeen()
{
	CancelRequests();
}

void CQmFriendLastSeen::OnInit()
{
	std::string Body;
	if(QmReadFileBounded(Storage()->OpenFile(CACHE_FILE, IOFLAG_READ, IStorage::TYPE_SAVE), 1024 * 1024, Body))
		m_Cache.Load(Body, time_timestamp());
}

void CQmFriendLastSeen::CancelRequests()
{
	if(m_pRequest)
		m_pRequest->Abort();
	m_pRequest.reset();
	m_pParseJob.reset();
	m_Cache.CancelRequests();
}

const SQmFriendLastSeenEntry *CQmFriendLastSeen::Get(const char *pName)
{
	return g_Config.m_QmFriendLastSeen ? m_Cache.Get(pName, time_timestamp()) : nullptr;
}

void CQmFriendLastSeen::OnUpdate()
{
	const uint64_t Revision = GameClient()->Friends()->Revision();
	if(Revision != m_FriendsRevision)
	{
		m_FriendsRevision = Revision;
		std::unordered_set<std::string> Names;
		for(int i = 0; i < GameClient()->Friends()->NumFriends(); ++i)
		{
			const char *pName = GameClient()->Friends()->GetFriend(i)->m_aName;
			if(pName[0] != '\0')
				Names.emplace(pName);
		}
		m_Cache.RetainNames(Names);
	}
	if(!g_Config.m_QmFriendLastSeen)
	{
		CancelRequests();
		return;
	}
	if((m_pRequest || m_pParseJob) && !m_Cache.IsCurrent(m_Request.m_Id))
		CancelRequests();
	const int64_t Now = time_timestamp();
	if(m_pParseJob)
	{
		if(!m_pParseJob->Done())
			return;
		m_Cache.Complete(m_Request.m_Id, m_pParseJob->State() == IJob::STATE_DONE ? m_pParseJob->TakeDate() : std::nullopt, Now);
		m_pParseJob.reset();
	}
	if(m_pRequest)
	{
		if(!m_pRequest->Done())
			return;
		const int Status = m_pRequest->CompletedStatusCode();
		if(m_pRequest->State() == EHttpState::DONE && Status == 200)
		{
			m_pParseJob = std::make_shared<CQmFriendLastSeenParseJob>(std::move(m_pRequest), m_Request.m_Name);
			Engine()->AddJob(m_pParseJob);
			return;
		}
		if(Status == 404)
			m_Cache.Complete(m_Request.m_Id, std::string{}, Now);
		else
			m_Cache.Complete(m_Request.m_Id, std::nullopt, Now, m_pRequest->ResultRetryAfterSeconds().value_or(0));
		m_pRequest.reset();
	}
	const auto Request = m_Cache.NextRequest(Now);
	if(!Request)
		return;
	m_Request = *Request;
	const std::string Url = QmBuildDdStatsPlayerUrl(m_Request.m_Name.c_str());
	m_pRequest = HttpGet(Url.c_str());
	if(!m_pRequest)
	{
		m_Cache.Complete(m_Request.m_Id, std::nullopt, Now);
		return;
	}
	m_pRequest->HeaderString("Accept", "application/json");
	m_pRequest->Timeout(CTimeout{5000, 30000, 100, 10});
	m_pRequest->MaxResponseSize(8 * 1024 * 1024);
	m_pRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pRequest);
}

void CQmFriendLastSeen::OnShutdown()
{
	CancelRequests();
	if(!m_Cache.Dirty())
		return;
	const std::string Body = m_Cache.Serialize();
	IOHANDLE File = Storage()->OpenFile(CACHE_TEMP_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	const bool Written = io_write(File, Body.data(), Body.size()) == Body.size();
	const bool Closed = io_close(File) == 0;
	if(Written && Closed)
		Storage()->RenameFile(CACHE_TEMP_FILE, CACHE_FILE, IStorage::TYPE_SAVE);
}
