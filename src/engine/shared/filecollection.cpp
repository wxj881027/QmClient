/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "filecollection.h"

#include <base/math.h>

#include <engine/storage.h>

#include <algorithm>
#include <cstring>

void CFileCollection::Init(IStorage *pStorage, const char *pPath, const char *pFileDesc, const char *pFileExt, int MaxEntries) noexcept
try
{
	m_vFileEntries.clear();
	if(MaxEntries < 0 || std::strlen(pPath) >= sizeof(m_aPath) ||
		std::strlen(pFileDesc) >= sizeof(m_aFileDesc) || std::strlen(pFileExt) >= sizeof(m_aFileExt))
		return;
	str_copy(m_aFileDesc, pFileDesc);
	m_FileDescLength = str_length(m_aFileDesc);
	str_copy(m_aFileExt, pFileExt);
	m_FileExtLength = str_length(m_aFileExt);
	str_copy(m_aPath, pPath);
	m_pStorage = pStorage;

	const int StorageType = fs_is_relative_path(m_aPath) ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE;
	if(m_pStorage->ListDirectoryChecked(StorageType, m_aPath, FilelistCallback, this) != EFsListDirectoryResult::SUCCESS)
	{
		m_vFileEntries.clear();
		return;
	}
	std::sort(m_vFileEntries.begin(), m_vFileEntries.end(), [](const CFileEntry &Lhs, const CFileEntry &Rhs) { return Lhs.m_Timestamp < Rhs.m_Timestamp; });

	char aCompletePath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(StorageType, m_aPath, aCompletePath, sizeof(aCompletePath));
	size_t Remaining = m_vFileEntries.size();
	for(const auto &FileEntry : m_vFileEntries)
	{
		if(Remaining <= static_cast<size_t>(MaxEntries))
			break;

		char aBuf[IO_MAX_PATH_LENGTH];
		const size_t FilenameLength = std::strlen(FileEntry.m_aFilename);
		if(std::strlen(m_aPath) + 1 + FilenameLength >= sizeof(aBuf) ||
			std::strlen(aCompletePath) + 1 + FilenameLength >= sizeof(aBuf))
			continue;
		// 只删除实际枚举的文件，不从可能被日期解析归一化的时间戳重建名称。
		str_format(aBuf, sizeof(aBuf), "%s/%s", m_aPath, FileEntry.m_aFilename);

		if(m_pStorage->RemoveFile(aBuf, StorageType))
			--Remaining;
	}
}
catch(...)
{
	// 枚举/准备失败不执行清理；删除阶段异常时保留已完成的结果，停止本轮。
	m_vFileEntries.clear();
}

bool CFileCollection::ExtractTimestamp(const char *pTimestring, time_t *pTimestamp)
{
	// Discard anything after timestamp length from pTimestring (most likely the extension)
	char aStrippedTimestring[TIMESTAMP_LENGTH];
	str_copy(aStrippedTimestring, pTimestring);
	return timestamp_from_str(aStrippedTimestring, FORMAT_NOSPACE, pTimestamp);
}

bool CFileCollection::ParseFilename(const char *pFilename, time_t *pTimestamp)
{
	// Check if filename is valid
	if(!str_endswith(pFilename, m_aFileExt))
		return false;

	const char *pTimestring = pFilename;

	if(m_aFileDesc[0] == '\0')
	{
		int FilenameLength = str_length(pFilename);
		if(m_FileExtLength + TIMESTAMP_LENGTH > FilenameLength)
		{
			return false;
		}

		pTimestring += FilenameLength - m_FileExtLength - TIMESTAMP_LENGTH + 1;
	}
	else
	{
		if(str_length(pFilename) != m_FileDescLength + TIMESTAMP_LENGTH + m_FileExtLength ||
			!str_startswith(pFilename, m_aFileDesc) || pFilename[m_FileDescLength] != '_')
			return false;

		pTimestring += m_FileDescLength + 1;
	}

	// Extract timestamp
	if(!ExtractTimestamp(pTimestring, pTimestamp))
		return false;

	return true;
}

int CFileCollection::FilelistCallback(const char *pFilename, int IsDir, int StorageType, void *pUser)
{
	CFileCollection *pThis = static_cast<CFileCollection *>(pUser);

	// Try to parse filename and extract timestamp
	time_t Timestamp;
	if(IsDir || std::strlen(pFilename) >= sizeof(CFileEntry::m_aFilename) ||
		!pThis->ParseFilename(pFilename, &Timestamp))
		return 0;

	// Add the entry
	pThis->m_vFileEntries.emplace_back(Timestamp, pFilename);

	return 0;
}
