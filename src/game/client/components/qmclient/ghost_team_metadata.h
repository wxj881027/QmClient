#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TEAM_METADATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_GHOST_TEAM_METADATA_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <string>
#include <vector>

// 影子队伍 sidecar：.gho 的二进制格式保持不变，队伍信息只作为同目录/同前缀的
// 可丢失元数据读取。旧缓存没有这份文件时，旁观面板按“未知队伍”处理。
struct SGhostTeamEntry
{
	std::string m_Filename;
	int m_Team = -1;
};

inline const char *QmGhostTeamMetadataName()
{
	return "teams.qmtm";
}

inline std::string QmGhostBasename(const char *pPath)
{
	if(pPath == nullptr)
		return {};
	const char *pSlash = str_rchr(pPath, '/');
	const char *pBackslash = str_rchr(pPath, '\\');
	const char *pName = pPath;
	if(pSlash != nullptr)
		pName = pSlash + 1;
	if(pBackslash != nullptr && pBackslash + 1 > pName)
		pName = pBackslash + 1;
	return pName;
}

inline std::string QmGhostTeamMetadataForSingleFile(const char *pGhostPath)
{
	std::string Path = pGhostPath != nullptr ? pGhostPath : "";
	if(Path.size() > 4 && str_endswith_nocase(Path.c_str(), ".gho"))
		Path.resize(Path.size() - 4);
	Path += ".qmtm";
	return Path;
}

inline int QmGhostTeamForFile(const std::vector<SGhostTeamEntry> &vEntries, const std::string &Filename)
{
	for(const SGhostTeamEntry &Entry : vEntries)
	{
		if(str_comp_nocase(Entry.m_Filename.c_str(), Filename.c_str()) == 0)
			return Entry.m_Team;
	}
	return -1;
}

inline bool QmWriteGhostTeamMetadata(IStorage *pStorage, const char *pPath, const std::vector<SGhostTeamEntry> &vEntries)
{
	if(pStorage == nullptr || pPath == nullptr || pPath[0] == '\0')
		return false;

	char aTmpPath[IO_MAX_PATH_LENGTH];
	str_format(aTmpPath, sizeof(aTmpPath), "%s.tmp_%d", pPath, pid());
	pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
	IOHANDLE File = pStorage->OpenFile(aTmpPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;

	const char aHeader[] = "QMGTEAM 1\n";
	bool Ok = io_write(File, aHeader, sizeof(aHeader) - 1) == sizeof(aHeader) - 1;
	for(const SGhostTeamEntry &Entry : vEntries)
	{
		if(!Ok || Entry.m_Filename.empty() || Entry.m_Team < 0)
			continue;
		char aLine[IO_MAX_PATH_LENGTH + 32];
		str_format(aLine, sizeof(aLine), "%s\t%d\n", Entry.m_Filename.c_str(), Entry.m_Team);
		Ok = io_write(File, aLine, str_length(aLine)) == (unsigned)str_length(aLine);
	}
	Ok = io_close(File) == 0 && Ok;
	if(!Ok || !pStorage->RenameFile(aTmpPath, pPath, IStorage::TYPE_SAVE))
	{
		pStorage->RemoveFile(aTmpPath, IStorage::TYPE_SAVE);
		return false;
	}
	return true;
}

inline bool QmReadGhostTeamMetadata(IStorage *pStorage, const char *pPath, std::vector<SGhostTeamEntry> &vEntries)
{
	vEntries.clear();
	if(pStorage == nullptr || pPath == nullptr || !pStorage->FileExists(pPath, IStorage::TYPE_SAVE))
		return false;
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;
	const int64_t Length = io_length(File);
	if(Length <= 0 || Length > 1024 * 1024)
	{
		io_close(File);
		return false;
	}

	std::string Content((size_t)Length, '\0');
	const bool ReadOk = io_read(File, Content.data(), (unsigned)Content.size()) == (unsigned)Content.size();
	io_close(File);
	if(!ReadOk)
		return false;

	const char aPrefix[] = "QMGTEAM 1";
	bool HeaderSeen = false;
	size_t Offset = 0;
	while(Offset < Content.size())
	{
		const size_t End = Content.find('\n', Offset);
		std::string Line = Content.substr(Offset, End == std::string::npos ? std::string::npos : End - Offset);
		Offset = End == std::string::npos ? Content.size() : End + 1;
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		if(!HeaderSeen)
		{
			HeaderSeen = str_comp(Line.c_str(), aPrefix) == 0;
			if(HeaderSeen)
				continue;
			return false;
		}
		if(Line.empty())
			continue;
		const size_t Tab = Line.find('\t');
		if(Tab == std::string::npos || Tab == 0)
			continue;
		const std::string Filename = Line.substr(0, Tab);
		const int Team = str_toint(Line.c_str() + Tab + 1);
		if(Team < 0)
			continue;
		vEntries.push_back({Filename, Team});
	}
	return HeaderSeen;
}

#endif
