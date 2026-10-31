#include "updater.h"

#include "updater_file.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/version.h>

#include <cstdlib>
#include <unordered_set>
#include <utility>

#if !defined(CONF_FAMILY_WINDOWS)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// addition of '/' to keep paths intact, because EscapeUrl() (using curl_easy_escape) doesn't do this
static inline bool IsUnreserved(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '_' ||
	       c == '.' || c == '~' || c == '/';
}

static bool IsAllowedUpdaterPath(const char *pPath)
{
	if(!fs_is_relative_path(pPath) || str_find(pPath, "..") != nullptr)
		return false;

	char aSegment[IO_MAX_PATH_LENGTH];
	size_t SegmentLength = 0;
	for(const char *pIter = pPath;; ++pIter)
	{
		if(*pIter == '/' || *pIter == '\0')
		{
			if(SegmentLength == 0 || SegmentLength >= sizeof(aSegment))
				return false;
			aSegment[SegmentLength] = '\0';
			if(str_comp(aSegment, ".") == 0 || !str_valid_filename(aSegment))
				return false;
			SegmentLength = 0;
			if(*pIter == '\0')
				return true;
			continue;
		}
		if(SegmentLength + 1 >= sizeof(aSegment))
			return false;
		aSegment[SegmentLength++] = *pIter;
	}
}

static void UrlEncodePath(const char *pIn, char *pOut, size_t OutSize)
{
	if(!pIn || !pOut || OutSize == 0)
		return;
	static const char HEX[] = "0123456789ABCDEF";
	size_t WriteIndex = 0;
	for(size_t i = 0; pIn[i] != '\0'; ++i)
	{
		unsigned char c = static_cast<unsigned char>(pIn[i]);
		if(IsUnreserved(c))
		{
			if(OutSize - WriteIndex < 2) // require 1 byte + NUL
				break;
			pOut[WriteIndex++] = static_cast<char>(c);
		}
		else
		{
			if(OutSize - WriteIndex < 4) // require 3 bytes + NUL
				break;
			pOut[WriteIndex++] = '%';
			pOut[WriteIndex++] = HEX[c >> 4]; // upper 4 bits of c
			pOut[WriteIndex++] = HEX[c & 0x0F]; // lower 4 bits of c
		}
	}
	pOut[WriteIndex] = '\0';
}

static constexpr const char *UPDATE_CLIENT_FILE = "DDNet.exe";
static constexpr const char *UPDATE_CLIENT_URL = "https://github.com/wxj881027/Q1menG_Client/releases/latest/download/DDNet.exe";
static constexpr const char *UPDATE_FALLBACK_BASE_URL = "https://raw.githubusercontent.com/wxj881027/Q1menG_Client/master/";

static const char *GetUpdaterUrl(char *pBuf, int BufSize, const char *pFile)
{
	if(str_comp(pFile, UPDATE_CLIENT_FILE) == 0)
	{
		str_copy(pBuf, UPDATE_CLIENT_URL, BufSize);
		return pBuf;
	}

	char aBuf[1024];
	UrlEncodePath(pFile, aBuf, sizeof(aBuf));
	str_format(pBuf, BufSize, "%s%s", UPDATE_FALLBACK_BASE_URL, aBuf);
	return pBuf;
}

static void FormatUpdaterDestPath(char *pBuf, int BufSize, const char *pFile, const char *pDestPath)
{
	if(!pDestPath)
	{
		pDestPath = pFile;
	}
	str_format(pBuf, BufSize, "update/%s", pDestPath);
}

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
	const CLockScope LockScope(m_Lock);
	char aDestination[IO_MAX_PATH_LENGTH];
	char aUrl[256];
	FormatUpdaterDestPath(aDestination, sizeof(aDestination), pFile, pDestPath);
	auto pRequest = std::shared_ptr<IHttpRequest>(CreateHttpRequest(GetUpdaterUrl(aUrl, sizeof(aUrl), pFile)));
	pRequest->WriteToFile(m_pStorage, aDestination, -2);
	pRequest->Timeout(CTimeout{10000, 10 * 60 * 1000, 8192, 20});
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
	auto Resolve = [&](const char *pRelative, char *pOut, size_t Size) {
		if(static_cast<size_t>(str_length(aRoot)) + str_length(pRelative) >= Size)
			return false;
		str_copy(pOut, aRoot, Size);
		str_append(pOut, pRelative, Size);
		return true;
	};
	if(!Resolve(pCandidate, aCandidate, sizeof(aCandidate)) || !Resolve(pDestination, aDestination, sizeof(aDestination)) || !Resolve(pBackup, aBackup, sizeof(aBackup)))
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

void CUpdater::AddFileJob(const char *pFile, bool Job)
{
	m_FileJobs.emplace_front(pFile, Job);
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
	char aPath[IO_MAX_PATH_LENGTH];
	void *pBuf;
	unsigned Length;
	if(!m_pStorage->ReadFile(m_pStorage->GetBinaryPath("update/update.json", aPath, sizeof(aPath)), IStorage::TYPE_ABSOLUTE, &pBuf, &Length))
		return false;

	std::unique_ptr<void, decltype(&free)> pContent(pBuf, free);
	std::unique_ptr<json_value, decltype(&json_value_free)> pParsed(JsonParse((json_char *)pBuf, Length), json_value_free);
	json_value *pVersions = pParsed.get();

	if(!pVersions || pVersions->type != json_array)
	{
		return false;
	}

	// if we're already downloading a file, or it's been deleted in the latest version, we skip it if it comes up again
	std::unordered_set<std::string> SkipSet;

	for(int i = 0; i < json_array_length(pVersions); i++)
	{
		const json_value *pCurrent = json_array_get(pVersions, i);
		if(!pCurrent || pCurrent->type != json_object)
			continue;

		const char *pVersion = json_string_get(json_object_get(pCurrent, "version"));
		if(!pVersion)
			continue;

		if(str_comp(pVersion, QMCLIENT_VERSION) == 0)
			break;

		if(json_boolean_get(json_object_get(pCurrent, "client")))
			m_ClientUpdate = true;
		if(json_boolean_get(json_object_get(pCurrent, "server")))
			m_ServerUpdate = true;

		const json_value *pDownload = json_object_get(pCurrent, "download");
		if(pDownload && pDownload->type == json_array)
		{
			for(int j = 0; j < json_array_length(pDownload); j++)
			{
				const char *pName = json_string_get(json_array_get(pDownload, j));
				if(!pName || !IsAllowedUpdaterPath(pName))
				{
					log_error("updater", "Update manifest contains invalid path to download: '%s'", pName == nullptr ? "(not a string)" : pName);
					continue;
				}

				if(SkipSet.insert(pName).second)
				{
					AddFileJob(pName, true);
				}
			}
		}

		const json_value *pRemove = json_object_get(pCurrent, "remove");
		if(pRemove && pRemove->type == json_array)
		{
			for(int j = 0; j < json_array_length(pRemove); j++)
			{
				const char *pName = json_string_get(json_array_get(pRemove, j));
				if(!pName || !IsAllowedUpdaterPath(pName))
				{
					log_error("updater", "Update manifest contains invalid path to remove: '%s'", pName == nullptr ? "(not a string)" : pName);
					continue;
				}

				if(SkipSet.insert(pName).second)
				{
					AddFileJob(pName, false);
				}
			}
		}
	}
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
	bool Success = true;

	for(auto &FileJob : m_FileJobs)
	{
		if(FileJob.second)
		{
			Success &= MoveFile(FileJob.first.c_str());
		}
	}

	if(m_ClientUpdate)
	{
		Success &= ReplaceClient();
	}
	if(m_ServerUpdate)
	{
		Success &= ReplaceServer();
	}

	if(Success)
	{
		for(const auto &[Filename, JobSuccess] : m_FileJobs)
		{
			if(!JobSuccess)
			{
				m_pStorage->RemoveBinaryFile(Filename.c_str());
			}
		}
	}

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
