#ifndef TEST_SUPPORT_FRIEND_LAST_SEEN_TEST_SUPPORT_H
#define TEST_SUPPORT_FRIEND_LAST_SEEN_TEST_SUPPORT_H

#include <base/system.h>

#include <engine/engine.h>
#include <engine/friends.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>

// 仅替换好友接口输入，不实现待测缓存、修订或请求调度。
class CFriendLastSeenTestFriends : public IFriends
{
	CFriendInfo m_aFriends[8]{};
	int m_Count = 0;
	uint64_t m_Revision = 0;

public:
	void Set(std::initializer_list<const char *> Names)
	{
		m_Count = 0;
		for(const char *pName : Names)
			if(m_Count < 8)
				str_copy(m_aFriends[m_Count++].m_aName, pName);
		++m_Revision;
	}
	void Init(bool) override {}
	bool ReplaceAll(const CFriendList &) override { return false; }
	int NumFriends() const override { return m_Count; }
	uint64_t Revision() const override { return m_Revision; }
	const CFriendInfo *GetFriend(int Index) const override { return &m_aFriends[Index]; }
	int GetFriendState(const char *, const char *) const override { return FRIEND_NO; }
	bool IsFriend(const char *, const char *, bool) const override { return false; }
	bool IsFriendPinned(const char *, const char *) const override { return false; }
	bool SetFriendPinned(const char *, const char *, bool) override { return false; }
	const char *GetFriendCategory(const char *, const char *) const override { return DEFAULT_CATEGORY; }
	const char *GetFriendNote(const char *, const char *) const override { return ""; }
	bool SetFriendNote(const char *, const char *, const char *) override { return false; }
	bool ClearFriendNote(const char *, const char *) override { return false; }
	const char *DefaultCategory() const override { return DEFAULT_CATEGORY; }
	int NumCategories() const override { return 0; }
	const char *GetCategory(int) const override { return ""; }
	int FindCategory(const char *) const override { return -1; }
	bool AddCategory(const char *) override { return false; }
	bool MoveCategory(int, int) override { return false; }
	bool RenameCategory(const char *, const char *) override { return false; }
	bool RemoveCategory(const char *) override { return false; }
	bool SetFriendCategory(const char *, const char *, const char *) override { return false; }
	void AddFriend(const char *, const char *, const char *) override {}
	void RemoveFriend(const char *, const char *) override {}
};

class CFriendLastSeenTestRequest : public IHttpRequest
{
public:
	char m_aHeader[64]{};
	int m_AbortCalls = 0;
	bool m_ThrowAbort = false;
	explicit CFriendLastSeenTestRequest(const char *pUrl) :
		IHttpRequest(pUrl) {}
	void Header(const char *pHeader) override { str_copy(m_aHeader, pHeader); }
	void Abort() override
	{
		++m_AbortCalls;
		if(m_ThrowAbort)
			throw std::runtime_error("abort boundary failure");
		IHttpRequest::Abort();
	}
	void SetState(EHttpState State) { m_State = State; }
	const CTimeout &TimeoutSettings() const { return m_Timeout; }
	int64_t ResponseLimit() const { return m_MaxResponseSize; }
	bool Finish(std::string_view Body, int Status = 200, EHttpState State = EHttpState::DONE, int64_t RetryAfter = 0)
	{
		auto *pData = static_cast<unsigned char *>(malloc(Body.size() + 1));
		if(!pData)
			return false;
		mem_copy(pData, Body.data(), Body.size());
		pData[Body.size()] = '\0';
		free(m_pBuffer);
		m_pBuffer = pData;
		m_BufferSize = Body.size() + 1;
		m_ResponseLength = Body.size();
		m_StatusCode = Status;
		m_ResultRetryAfterSeconds = RetryAfter;
		m_State = State;
		return true;
	}
};

inline std::unique_ptr<IHttpRequest> CreateFriendLastSeenTestRequest(const char *pUrl)
{
	return std::make_unique<CFriendLastSeenTestRequest>(pUrl);
}

class CFriendLastSeenTestHttp : public IHttp
{
public:
	std::shared_ptr<CFriendLastSeenTestRequest> m_pLast;
	int m_Submissions = 0;
	bool m_FailAdmission = false;
	void Run(std::shared_ptr<IHttpRequest> pRequest) override
	{
		++m_Submissions;
		m_pLast = std::static_pointer_cast<CFriendLastSeenTestRequest>(std::move(pRequest));
		m_pLast->SetState(m_FailAdmission ? EHttpState::ERROR : EHttpState::RUNNING);
	}
	bool HasIpresolveBug() const override { return false; }
};

class CFriendLastSeenTestEngine : public IEngine
{
	CJobPool m_Pool;

public:
	std::weak_ptr<IJob> m_pLast;
	int m_Offers = 0;
	int m_Accepted = 0;
	bool m_RejectNext = false;
	bool Start() { return m_Pool.TryInit(1); }
	void Init() override {}
	void AddJob(std::shared_ptr<IJob> pJob) override { (void)TryAddJob(std::move(pJob)); }
	bool TryAddJob(std::shared_ptr<IJob> pJob) override
	{
		++m_Offers;
		if(m_RejectNext)
		{
			m_RejectNext = false;
			return false;
		}
		if(!m_Pool.TryAdd(pJob))
			return false;
		++m_Accepted;
		m_pLast = pJob;
		return true;
	}
	void ShutdownJobs() override { m_Pool.Shutdown(); }
	void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
};

// 始终借用隔离的真实存储；故障只在文件系统边界注入，读写仍由真实句柄完成。
class CFriendLastSeenTestStorage : public IStorage
{
protected:
	IStorage &m_Storage;

public:
	explicit CFriendLastSeenTestStorage(IStorage &Storage) :
		m_Storage(Storage) {}
	int m_OpenCalls = 0;
	int m_WriteOpens = 0;
	int m_Removes = 0;
	int m_Renames = 0;
	bool m_RejectWrite = false;
	bool m_ThrowOpen = false;
	bool m_ReadOnlyWrite = false;
	bool m_RejectRename = false;
	bool m_ThrowAfterRename = false;
	bool m_ThrowRemove = false;
	bool m_UnsupportedRead = false;
	bool m_ThrowRead = false;
	bool m_OverlongPath = false;
	bool m_ThrowCompletePath = false;
	bool m_UsedExclusive = false;
	bool m_AllowedDestinationRemoval = false;
	char m_aLastCandidate[128]{};
	int NumPaths() const override { return m_Storage.NumPaths(); }
	bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetDataPath(pName, pBuffer, Size); }
	bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetExecutablePath(pName, pBuffer, Size); }
	void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { m_Storage.ListDirectory(Type, pPath, Callback, pUser); }
	EFsListDirectoryResult ListDirectoryChecked(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { return m_Storage.ListDirectoryChecked(Type, pPath, Callback, pUser); }
	void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_Storage.ListDirectoryInfo(Type, pPath, Callback, pUser); }
	IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer = nullptr, int Size = 0) override
	{
		++m_OpenCalls;
		if(Flags & IOFLAG_WRITE)
		{
			++m_WriteOpens;
			str_copy(m_aLastCandidate, pName);
			m_UsedExclusive = (Flags & IOFLAG_EXCLUSIVE) != 0;
			if(m_ThrowOpen)
				throw std::bad_alloc();
			if(m_RejectWrite)
				return nullptr;
			if(m_ReadOnlyWrite)
			{
				IOHANDLE Candidate = m_Storage.OpenFile(pName, Flags, Type);
				if(!Candidate || io_close(Candidate) != 0)
					return nullptr;
				return m_Storage.OpenFile(pName, IOFLAG_READ, Type, pBuffer, Size);
			}
		}
		return m_Storage.OpenFile(pName, Flags, Type, pBuffer, Size);
	}
	bool FileExists(const char *pName, int Type) override { return m_Storage.FileExists(pName, Type); }
	bool FolderExists(const char *pName, int Type) override { return m_Storage.FolderExists(pName, Type); }
	bool ReadFile(const char *pName, int Type, void **ppData, unsigned *pSize) override { return m_Storage.ReadFile(pName, Type, ppData, pSize); }
	EReadFileResult ReadFileChecked(const char *pName, int Type, void **ppData, unsigned *pSize) override { return m_Storage.ReadFileChecked(pName, Type, ppData, pSize); }
	EReadFileResult ReadFileCheckedBounded(const char *pName, int Type, void **ppData, unsigned *pSize, unsigned MaxBytes) override
	{
		if(m_ThrowRead)
			throw std::bad_alloc();
		if(m_UnsupportedRead)
		{
			*ppData = nullptr;
			*pSize = 0;
			return EReadFileResult::UNSUPPORTED;
		}
		return m_Storage.ReadFileCheckedBounded(pName, Type, ppData, pSize, MaxBytes);
	}
	char *ReadFileStr(const char *pName, int Type) override { return m_Storage.ReadFileStr(pName, Type); }
	bool RetrieveTimes(const char *pName, int Type, time_t *pCreated, time_t *pModified) override { return m_Storage.RetrieveTimes(pName, Type, pCreated, pModified); }
	bool CalculateHashes(const char *pName, int Type, SHA256_DIGEST *pSha, unsigned *pCrc) override { return m_Storage.CalculateHashes(pName, Type, pSha, pCrc); }
	bool FindFile(const char *pName, const char *pPath, int Type, char *pBuffer, int Size) override { return m_Storage.FindFile(pName, pPath, Type, pBuffer, Size); }
	size_t FindFiles(const char *pName, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_Storage.FindFiles(pName, pPath, Type, pEntries); }
	bool RemoveFile(const char *pName, int Type) override
	{
		++m_Removes;
		if(m_ThrowRemove)
			throw std::runtime_error("candidate cleanup failure");
		return m_Storage.RemoveFile(pName, Type);
	}
	bool RemoveFolder(const char *pName, int Type) override { return m_Storage.RemoveFolder(pName, Type); }
	bool RenameFile(const char *pOld, const char *pNew, int Type, bool Remove = true) override
	{
		++m_Renames;
		m_AllowedDestinationRemoval = Remove;
		const bool Renamed = !m_RejectRename && m_Storage.RenameFile(pOld, pNew, Type, Remove);
		if(Renamed && m_ThrowAfterRename)
			throw std::bad_alloc();
		return Renamed;
	}
	bool CreateFolder(const char *pName, int Type) override { return m_Storage.CreateFolder(pName, Type); }
	void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override
	{
		if(m_ThrowCompletePath)
			throw std::bad_alloc();
		if(m_OverlongPath && Size > IO_MAX_PATH_LENGTH)
		{
			mem_set(pBuffer, 'x', IO_MAX_PATH_LENGTH);
			pBuffer[IO_MAX_PATH_LENGTH] = '\0';
			return;
		}
		m_Storage.GetCompletePath(Type, pName, pBuffer, Size);
	}
	bool RemoveBinaryFile(const char *pName) override { return m_Storage.RemoveBinaryFile(pName); }
	bool RenameBinaryFile(const char *pOld, const char *pNew) override { return m_Storage.RenameBinaryFile(pOld, pNew); }
	const char *GetBinaryPath(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPath(pName, pBuffer, Size); }
	const char *GetBinaryPathAbsolute(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPathAbsolute(pName, pBuffer, Size); }
};

#endif
