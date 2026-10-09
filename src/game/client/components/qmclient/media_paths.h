#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_PATHS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MEDIA_PATHS_H

#include <base/log.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <algorithm>
#include <string>

namespace qmclient::media_paths
{

enum class EKind
{
	DEMOS,
	VIDEOS,
	SCREENSHOTS,
};

inline int StorageType(const char *pPath)
{
	return fs_is_relative_path(pPath) ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE;
}

inline const char *DirectorySetting(const CConfig &Config, EKind Kind)
{
	switch(Kind)
	{
	case EKind::DEMOS: return Config.m_QmDemoDirectory;
	case EKind::VIDEOS: return Config.m_QmVideoDirectory;
	case EKind::SCREENSHOTS: return Config.m_QmScreenshotDirectory;
	}
	dbg_assert_failed("Invalid media kind");
}

inline const char *DefaultDirectory(EKind Kind)
{
	switch(Kind)
	{
	case EKind::DEMOS: return "demos";
	case EKind::VIDEOS: return "videos";
	case EKind::SCREENSHOTS: return "screenshots";
	}
	dbg_assert_failed("Invalid media kind");
}

// 拼接失败时返回空串，不能用截断的路径写入另一个文件。
inline std::string Join(const std::string &Directory, const std::string &Name)
{
	if(Directory.empty())
		return {};
	std::string Path = Directory;
	if(!Name.empty())
	{
		if(Path.back() != '/')
			Path += '/';
		Path += Name;
	}
	return Path.size() < IO_MAX_PATH_LENGTH ? Path : std::string();
}

inline bool HasParentSegment(const std::string &Path)
{
	return Path == ".." || str_startswith(Path.c_str(), "../") ||
		str_find(Path.c_str(), "/../") != nullptr || str_endswith(Path.c_str(), "/..") != nullptr;
}

inline std::string CompletePath(IStorage *pStorage, std::string Path)
{
	if(Path.empty() || Path.size() >= IO_MAX_PATH_LENGTH)
		return {};
	if(fs_is_relative_path(Path.c_str()))
	{
		char aSaveRoot[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aSaveRoot, sizeof(aSaveRoot));
		if(aSaveRoot[0] != '\0')
			Path = Join(aSaveRoot, Path);
	}
	if(Path.empty())
		return {};
	if(fs_is_relative_path(Path.c_str()))
	{
		char aWorkingDirectory[IO_MAX_PATH_LENGTH];
		if(!fs_getcwd(aWorkingDirectory, sizeof(aWorkingDirectory)))
			return {};
		Path = Join(aWorkingDirectory, Path);
	}
	std::replace(Path.begin(), Path.end(), '\\', '/');
	return Path;
}

inline std::string Directory(IStorage *pStorage, const CConfig &Config, EKind Kind)
{
	const char *pSetting = DirectorySetting(Config, Kind);
	if(pSetting[0] == '\0')
		return DefaultDirectory(Kind);
	std::string Path = pSetting;
	std::replace(Path.begin(), Path.end(), '\\', '/');
	while(Path.size() > 1 && Path.back() == '/' && !(Path.size() == 3 && Path[1] == ':'))
		Path.pop_back();
	if(Path.size() >= 2 && Path[1] == ':' && (Path.size() < 3 || Path[2] != '/'))
		return {};
	if(fs_is_relative_path(Path.c_str()))
	{
		// 相对目录锚定配置目录，禁止越过该目录；不依赖启动工作目录。
		if(HasParentSegment(Path) || Path == "." || (Path.size() >= 2 && Path[1] == ':'))
			return {};
	}
	return CompletePath(pStorage, Path);
}

// 只映射这三类文件的逻辑根目录，保留其下全部子目录。
inline std::string Resolve(IStorage *pStorage, const CConfig &Config, const char *pLogicalPath)
{
	std::string Path = pLogicalPath;
	std::replace(Path.begin(), Path.end(), '\\', '/');
	for(const EKind Kind : {EKind::DEMOS, EKind::VIDEOS, EKind::SCREENSHOTS})
	{
		const std::string Root = DefaultDirectory(Kind);
		if(Path == Root || str_startswith(Path.c_str(), (Root + '/').c_str()))
		{
			const std::string Suffix = Path.size() == Root.size() ? "" : Path.substr(Root.size() + 1);
			if(HasParentSegment(Suffix) || (!Suffix.empty() && Suffix.front() == '/'))
				return {};
			return Join(Directory(pStorage, Config, Kind), Suffix);
		}
	}
	return Path.size() < IO_MAX_PATH_LENGTH ? Path : std::string();
}

inline bool PrepareWrite(IStorage *pStorage, const std::string &Path)
{
	if(!Path.empty())
	{
		const std::string WholePath = CompletePath(pStorage, Path);
		if(!WholePath.empty() && fs_makedir_rec_for(WholePath.c_str()) == 0)
			return true;
	}
	log_error("media_paths", "Could not create the output directory for '%s'", Path.c_str());
	return false;
}

} // namespace qmclient::media_paths

#endif
