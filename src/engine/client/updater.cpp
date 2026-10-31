#include "updater.h"

#include "updater_data.h"
#include "updater_file.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/http.h>
#include <engine/storage.h>

#include <game/version.h>

#include <utility>

#if !defined(CONF_FAMILY_WINDOWS)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static constexpr const char *UPDATE_CLIENT_FILE = "DDNet.exe";

#if !defined(CONF_FAMILY_WINDOWS)
static bool SetExecutableBit(const char *pPath)
{
	const int FileDescriptor = open(pPath, O_RDWR);
	struct SCloseFile
	{
		int m_File;
		~SCloseFile()
		{
			if(m_File >= 0)
				close(m_File);
		}
	} CloseFile{FileDescriptor};
	if(FileDescriptor < 0)
	{
		log_error("updater", "Failed to open file descriptor to set executable bit of '%s'", pPath);
		return false;
	}
	bool Success = true;
	struct stat FileStats;
	if(fstat(FileDescriptor, &FileStats) != 0)
	{
		log_error("updater", "Failed to determine file stats to set executable bit of '%s'", pPath);
		Success = false;
	}
	else if(fchmod(FileDescriptor, FileStats.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0)
	{
		log_error("updater", "Failed to set executable bit of '%s'", pPath);
		Success = false;
	}
	if(close(std::exchange(CloseFile.m_File, -1)) != 0)
	{
		log_error("updater", "Failed to close file descriptor after setting executable bit of '%s'", pPath);
		Success = false;
	}
	return Success;
}
#endif

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pHttp = nullptr;
	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_CurrentJob = m_FileJobs.end();
	m_pCurrentTask = nullptr;

	m_ClientUpdate = m_ServerUpdate = m_ClientFetched = m_ServerFetched = false;

	IStorage::FormatTmpPath(m_aClientExecTmp, sizeof(m_aClientExecTmp), CLIENT_EXEC);
	IStorage::FormatTmpPath(m_aServerExecTmp, sizeof(m_aServerExecTmp), SERVER_EXEC);
}

void CUpdater::Init(IHttp *pHttp)
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = pHttp;
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_pCurrentTask ? m_pCurrentTask->Progress() : 0;
}

bool CUpdater::FetchFile(const char *pFile, const char *pDestPath) noexcept
try
{
	char aRoot[IO_MAX_PATH_LENGTH];
	char aSuffix[32];
	m_pStorage->GetBinaryPath("", aRoot, sizeof(aRoot));
	IStorage::FormatTmpPath(aSuffix, sizeof(aSuffix), "");
	QmLegacyUpdater::SRequestPaths Paths;
	if(!QmLegacyUpdater::PrepareRequestPaths(pFile, pDestPath ? pDestPath : pFile, aRoot, str_length(aSuffix), Paths))
	{
		SetCurrentState(IUpdater::FAIL);
		return false;
	}
	auto pRequest = std::shared_ptr<IHttpRequest>(CreateHttpRequest(Paths.m_aUrl));
	pRequest->WriteToFile(m_pStorage, Paths.m_aDestination, -2);
	if(str_comp(Paths.m_aDestination, "update/update.json") == 0)
		pRequest->MaxResponseSize(QmLegacyUpdater::MAX_MANIFEST_BYTES);
	pRequest->Timeout(CTimeout{10000, 10 * 60 * 1000, 8192, 20});
	const CLockScope LockScope(m_Lock);
	m_pCurrentTask = std::move(pRequest);
	str_copy(m_aStatus, m_pCurrentTask->Dest());
	m_pHttp->Run(m_pCurrentTask);
	return true;
}
catch(...)
{
	SetCurrentState(IUpdater::FAIL);
	return false;
}

bool CUpdater::ReplaceBinaryFile(const char *pCandidate, const char *pDestination, const char *pBackup, bool Executable)
{
	char aCandidate[IO_MAX_PATH_LENGTH];
	char aDestination[IO_MAX_PATH_LENGTH];
	char aBackup[IO_MAX_PATH_LENGTH];
	char aRoot[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath("", aRoot, sizeof(aRoot));
	if(!QmLegacyUpdater::ResolveBinaryPath(aRoot, pCandidate, aCandidate, sizeof(aCandidate)) ||
		!QmLegacyUpdater::ResolveBinaryPath(aRoot, pDestination, aDestination, sizeof(aDestination)) ||
		!QmLegacyUpdater::ResolveBinaryPath(aRoot, pBackup, aBackup, sizeof(aBackup)))
		return false;
#if !defined(CONF_FAMILY_WINDOWS)
	if(Executable && !SetExecutableBit(aCandidate))
		return false;
#else
	(void)Executable;
#endif
	QmLegacyUpdater::CStorageFiles Files(*m_pStorage);
	return QmLegacyUpdater::ReplaceFile(Files, aCandidate, aDestination, aBackup);
}

bool CUpdater::MoveFile(const char *pFile)
{
#if !defined(CONF_FAMILY_WINDOWS)
	if(str_endswith_nocase(pFile, ".dll"))
		return true;
#endif
#if !defined(CONF_PLATFORM_LINUX)
	if(str_endswith_nocase(pFile, ".so"))
		return true;
#endif
	char aCandidate[IO_MAX_PATH_LENGTH];
	char aBackup[IO_MAX_PATH_LENGTH];
	if(static_cast<size_t>(str_length(pFile)) + sizeof("update/") > sizeof(aCandidate) ||
		static_cast<size_t>(str_length(pFile)) + sizeof(".old") > sizeof(aBackup))
		return false;
	str_format(aCandidate, sizeof(aCandidate), "update/%s", pFile);
	str_format(aBackup, sizeof(aBackup), "%s.old", pFile);
	return ReplaceBinaryFile(aCandidate, pFile, aBackup, false);
}

void CUpdater::Update()
try
{
	switch(GetCurrentState())
	{
	case IUpdater::GETTING_MANIFEST:
		if(m_pCurrentTask && m_pCurrentTask->Done())
			SetCurrentState(m_pCurrentTask->State() == EHttpState::DONE ? IUpdater::GOT_MANIFEST : IUpdater::FAIL);
		break;
	case IUpdater::GOT_MANIFEST:
		PerformUpdate();
		break;
	case IUpdater::DOWNLOADING:
		RunningUpdate();
		break;
	case IUpdater::MOVE_FILES:
		CommitUpdate();
		break;
	default:
		return;
	}
}
catch(...)
{
	SetCurrentState(IUpdater::FAIL);
}

bool CUpdater::ReplaceClient()
{
	log_debug("updater", "Replacing " PLAT_CLIENT_EXEC);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "update/%s", m_aClientExecTmp);
	return ReplaceBinaryFile(aPath, PLAT_CLIENT_EXEC, CLIENT_EXEC ".old", true);
}

bool CUpdater::ReplaceServer()
{
	log_debug("updater", "Replacing " PLAT_SERVER_EXEC);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "update/%s", m_aServerExecTmp);
	return ReplaceBinaryFile(aPath, PLAT_SERVER_EXEC, SERVER_EXEC ".old", true);
}

bool CUpdater::ParseUpdate()
{
	char aRoot[IO_MAX_PATH_LENGTH];
	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath("", aRoot, sizeof(aRoot));
	QmLegacyUpdater::SUpdatePlan Plan;
	if(!QmLegacyUpdater::ResolveBinaryPath(aRoot, "update/update.json", aPath, sizeof(aPath)) ||
		!QmLegacyUpdater::LoadManifest(m_pStorage, aPath, QMCLIENT_VERSION, Plan))
		return false;
	m_FileJobs.swap(Plan.m_FileJobs);
	m_ClientUpdate = Plan.m_ClientUpdate;
	m_ServerUpdate = Plan.m_ServerUpdate;
	m_ClientFetched = m_ServerFetched = false;
	return true;
}

void CUpdater::InitiateUpdate()
{
	if(!m_pStorage || !m_pHttp || !m_pClient)
	{
		SetCurrentState(IUpdater::FAIL);
		return;
	}
	if(!QmLegacyUpdater::CanInitiate(GetCurrentState(), m_pCurrentTask && !m_pCurrentTask->Done()))
		return;
	m_FileJobs.clear();
	m_CurrentJob = m_FileJobs.end();
	m_ClientUpdate = true;
	m_ServerUpdate = false;
	m_ClientFetched = true;
	m_ServerFetched = false;
	SetCurrentState(IUpdater::DOWNLOADING);
	FetchFile(UPDATE_CLIENT_FILE, m_aClientExecTmp);
}

void CUpdater::PerformUpdate()
{
	SetCurrentState(IUpdater::PARSING_UPDATE);
	log_debug("updater", "Parsing update.json");
	if(!ParseUpdate())
	{
		SetCurrentState(IUpdater::FAIL);
		return;
	}
	m_CurrentJob = m_FileJobs.begin();
	SetCurrentState(IUpdater::DOWNLOADING);
}

void CUpdater::RunningUpdate()
{
	if(m_pCurrentTask)
	{
		const auto State = QmLegacyUpdater::DownloadState(m_pCurrentTask->State());
		if(State != IUpdater::MOVE_FILES)
		{
			if(State == IUpdater::FAIL)
				SetCurrentState(IUpdater::FAIL);
			return;
		}
	}

	if(m_CurrentJob != m_FileJobs.end())
	{
		auto &Job = *m_CurrentJob;
		if(Job.second)
		{
			const char *pFile = Job.first.c_str();
			if(str_endswith_nocase(pFile, ".dll"))
			{
#if defined(CONF_FAMILY_WINDOWS)
				char aBuf[IO_MAX_PATH_LENGTH];
				if(!QmLegacyUpdater::FormatPlatformLibraryName(pFile, 4, PLAT_NAME, aBuf, sizeof(aBuf)))
				{
					SetCurrentState(IUpdater::FAIL);
					return;
				}
				FetchFile(aBuf, pFile);
#endif
				// Ignore DLL downloads on other platforms
			}
			else if(str_endswith_nocase(pFile, ".so"))
			{
#if defined(CONF_PLATFORM_LINUX)
				char aBuf[IO_MAX_PATH_LENGTH];
				if(!QmLegacyUpdater::FormatPlatformLibraryName(pFile, 3, PLAT_NAME, aBuf, sizeof(aBuf)))
				{
					SetCurrentState(IUpdater::FAIL);
					return;
				}
				FetchFile(aBuf, pFile);
#endif
				// Ignore DLL downloads on other platforms, on Linux we statically link anyway
			}
			else
			{
				FetchFile(pFile);
			}
		}
		m_CurrentJob++;
	}
	else
	{
		if(m_ServerUpdate && !m_ServerFetched)
		{
			FetchFile(PLAT_SERVER_DOWN, m_aServerExecTmp);
			m_ServerFetched = true;
			return;
		}

		if(m_ClientUpdate && !m_ClientFetched)
		{
			FetchFile(PLAT_CLIENT_DOWN, m_aClientExecTmp);
			m_ClientFetched = true;
			return;
		}

		SetCurrentState(IUpdater::MOVE_FILES);
	}
}

void CUpdater::CommitUpdate()
{
	char aRoot[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath("", aRoot, sizeof(aRoot));
	const bool Success = QmLegacyUpdater::CommitFiles(m_FileJobs, m_ClientUpdate, m_ServerUpdate, [this](const char *pFile) { return MoveFile(pFile); }, [this]() { return ReplaceClient(); }, [this]() { return ReplaceServer(); }, [this, &aRoot](const char *pFile) {
			char aPath[IO_MAX_PATH_LENGTH];
			if(!QmLegacyUpdater::ResolveBinaryPath(aRoot, pFile, aPath, sizeof(aPath)))
				return false;
			if(!m_pStorage->FileExists(aPath, IStorage::TYPE_ABSOLUTE))
				return !m_pStorage->FolderExists(aPath, IStorage::TYPE_ABSOLUTE);
			return m_pStorage->RemoveFile(aPath, IStorage::TYPE_ABSOLUTE); });

	if(!Success)
	{
		SetCurrentState(IUpdater::FAIL);
	}
	else if(m_pClient->State() == IClient::STATE_ONLINE || m_pClient->EditorHasUnsavedData())
	{
		SetCurrentState(IUpdater::NEED_RESTART);
	}
	else
	{
		m_pClient->Restart();
	}
}
