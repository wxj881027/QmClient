#include "axiom_scores_data.h"
#include "bounded_file_reader.h"
#include "friend_last_seen.h"

#include <base/system.h>

#include <engine/engine.h>
#include <engine/friends.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <atomic>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
	constexpr const char *CACHE_FILE = "qm_friend_last_seen.json";

	struct SCandidateCleanup
	{
		IStorage &m_Storage;
		const char *m_pName;
		bool m_Active = true;
		~SCandidateCleanup()
		{
			if(m_Active)
			{
				try
				{
					(void)m_Storage.RemoveFile(m_pName, IStorage::TYPE_SAVE);
				}
				catch(...)
				{
					// 只回收本次独占创建的候选，清理异常不能阻断退出。
				}
			}
		}
	};
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

CQmFriendLastSeenService::~CQmFriendLastSeenService()
{
	CancelRequests();
}

bool CQmFriendLastSeenService::Load(IStorage &Storage, int64_t Now)
{
	bool Loaded = false;
	try
	{
		CQmCheckedFileData Data;
		if(!Data.Read(Storage, CACHE_FILE, IStorage::TYPE_SAVE, 1024 * 1024))
			return false;
		Loaded = m_Cache.Load(std::string_view(static_cast<const char *>(Data.Data()), Data.Size()), Now);
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	if(Loaded)
		CancelRequests();
	return Loaded;
}

void CQmFriendLastSeenService::CancelRequests() noexcept
{
	if(m_pRequest)
	{
		try
		{
			m_pRequest->Abort();
		}
		catch(...)
		{
			// Abort 是外部边界；即使失败也要撤回本地发布和 active 身份。
		}
	}
	m_pRequest.reset();
	m_pParseJob.reset();
	m_TransportRequestId = 0;
	m_Cache.CancelRequests();
}

void CQmFriendLastSeenService::Update(const IFriends &Friends, IEngine &Engine, IHttp &Http, bool Enabled, int64_t Now)
{
	// 关闭优先撤回在途输入，名单候选分配失败也不能留下旧网络请求。
	if(!Enabled)
		CancelRequests();
	const uint64_t Revision = Friends.Revision();
	if(Revision != m_FriendsRevision)
	{
		std::unordered_set<std::string> Names;
		try
		{
			Names.reserve(Friends.NumFriends());
			for(int i = 0; i < Friends.NumFriends(); ++i)
			{
				const char *pName = Friends.GetFriend(i)->m_aName;
				if(pName[0] != '\0')
					Names.emplace(pName);
			}
		}
		catch(const std::bad_alloc &)
		{
			return;
		}
		catch(const std::length_error &)
		{
			return;
		}
		m_Cache.RetainNames(Names);
		m_FriendsRevision = Revision;
	}
	if(!Enabled)
		return;
	if((m_pRequest || m_pParseJob) && !m_Cache.IsCurrent(m_TransportRequestId))
		CancelRequests();
	if(m_pParseJob)
	{
		if(!m_pParseJob->Done())
			return;
		m_Cache.Complete(m_TransportRequestId, m_pParseJob->State() == IJob::STATE_DONE ? m_pParseJob->TakeDate() : std::nullopt, Now);
		m_pParseJob.reset();
		m_pRequest.reset();
		m_TransportRequestId = 0;
	}
	if(m_pRequest)
	{
		if(!m_pRequest->Done())
			return;
		const int Status = m_pRequest->CompletedStatusCode();
		if(m_pRequest->State() == EHttpState::DONE && Status == 200)
		{
			const auto *pCurrent = m_Cache.CurrentRequest();
			std::shared_ptr<CQmFriendLastSeenParseJob> Candidate;
			try
			{
				Candidate = std::make_shared<CQmFriendLastSeenParseJob>(m_pRequest, pCurrent->m_Name);
			}
			catch(const std::bad_alloc &)
			{
				return;
			}
			catch(const std::length_error &)
			{
				return;
			}
			m_pParseJob = Candidate;
			// 接收边界在准备 catch 外；未知提交异常不能伪装成未接收。
			if(!Engine.TryAddJob(Candidate))
			{
				m_pParseJob.reset();
				return;
			}
			m_pRequest.reset();
			return;
		}
		if(Status == 404)
			m_Cache.Complete(m_TransportRequestId, std::string{}, Now);
		else
			m_Cache.Complete(m_TransportRequestId, std::nullopt, Now, m_pRequest->ResultRetryAfterSeconds().value_or(0));
		m_pRequest.reset();
		m_TransportRequestId = 0;
	}
	if(!m_Cache.CurrentRequest())
		(void)m_Cache.NextRequest(Now);
	const auto *pCurrent = m_Cache.CurrentRequest();
	if(!pCurrent)
		return;
	std::shared_ptr<IHttpRequest> Candidate;
	try
	{
		const std::string Url = QmBuildDdStatsPlayerUrl(pCurrent->m_Name.c_str());
		Candidate = m_pCreateRequest ? m_pCreateRequest(Url.c_str()) : nullptr;
		if(!Candidate)
			return;
		Candidate->HeaderString("Accept", "application/json");
		Candidate->Timeout(CTimeout{5000, 30000, 100, 10});
		Candidate->MaxResponseSize(8 * 1024 * 1024);
		Candidate->LogProgress(HTTPLOG::NONE);
	}
	catch(const std::bad_alloc &)
	{
		return;
	}
	catch(const std::length_error &)
	{
		return;
	}
	// 原生接收失败会发布 ERROR 终态，下一帧按既有站点失败策略消费。
	m_pRequest = std::move(Candidate);
	m_TransportRequestId = pCurrent->m_Id;
	Http.Run(m_pRequest);
}

bool CQmFriendLastSeenService::Save(IStorage &Storage) const
{
	if(!m_Cache.Dirty())
		return true;
	std::string Body;
	if(!m_Cache.TrySerialize(Body))
		return false;
	char aTargetPath[IO_MAX_PATH_LENGTH * 2] = {};
	try
	{
		Storage.GetCompletePath(IStorage::TYPE_SAVE, CACHE_FILE, aTargetPath, sizeof(aTargetPath));
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
	static std::atomic<uint64_t> s_NextCandidate{0};
	for(int Attempt = 0; Attempt < 32; ++Attempt)
	{
		char aSuffix[64];
		str_format(aSuffix, sizeof(aSuffix), ".%d.%llu.tmp", pid(), static_cast<unsigned long long>(s_NextCandidate.fetch_add(1, std::memory_order_relaxed)));
		if(str_length(aTargetPath) + str_length(aSuffix) >= IO_MAX_PATH_LENGTH)
			return false;
		char aCandidate[128];
		str_format(aCandidate, sizeof(aCandidate), "%s%s", CACHE_FILE, aSuffix);
		IOHANDLE File = nullptr;
		bool Occupied = false;
		try
		{
			File = Storage.OpenFile(aCandidate, IOFLAG_WRITE | IOFLAG_EXCLUSIVE, IStorage::TYPE_SAVE);
			if(!File)
				Occupied = Storage.FileExists(aCandidate, IStorage::TYPE_SAVE) || Storage.FolderExists(aCandidate, IStorage::TYPE_SAVE);
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		if(!File)
		{
			// 未取得独占句柄的路径不归本次保存所有，不能截断或清理。
			if(Occupied)
				continue;
			return false;
		}
		SCandidateCleanup Cleanup{Storage, aCandidate};
		const auto Close = [](void *pFile) noexcept { (void)io_close(pFile); };
		std::unique_ptr<void, decltype(Close)> OwnedFile(File, Close);
		const bool Written = io_write(File, Body.data(), static_cast<unsigned>(Body.size())) == Body.size() && io_error(File) == 0;
		const bool Closed = io_close(OwnedFile.release()) == 0;
		if(!Written || !Closed || !Storage.RenameFile(aCandidate, CACHE_FILE, IStorage::TYPE_SAVE, false))
			return false;
		Cleanup.m_Active = false;
		return true;
	}
	return false;
}
