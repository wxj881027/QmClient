// 字体商店只写 TYPE_SAVE 下的下载目录，旧目录仅用于兼容已安装判断。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FONT_DOWNLOAD_STORAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FONT_DOWNLOAD_STORAGE_H

#include <base/str.h>

#include <engine/storage.h>

#include <string>

namespace qm_font_download
{
	inline constexpr const char *DIRECTORY = "fonts/downloaded_fonts";

	inline bool ValidFilename(const std::string &File)
	{
		return !File.empty() && File.size() < IO_MAX_PATH_LENGTH - 64 &&
		       File.find_first_of("/\\:") == std::string::npos && File.find("..") == std::string::npos &&
		       (str_endswith_nocase(File.c_str(), ".ttf") != nullptr || str_endswith_nocase(File.c_str(), ".otf") != nullptr);
	}

	inline bool EnsureDirectory(IStorage &Storage)
	{
		// CreateFolder 不会递归建立父目录，因此按顺序建立两级目录。
		return Storage.CreateFolder("fonts", IStorage::TYPE_SAVE) && Storage.CreateFolder(DIRECTORY, IStorage::TYPE_SAVE);
	}

	inline bool TargetPath(const std::string &File, char *pPath, unsigned PathSize)
	{
		if(PathSize == 0)
			return false;
		pPath[0] = '\0';
		if(!ValidFilename(File) || File.size() + str_length(DIRECTORY) + 2 > PathSize)
			return false;
		str_format(pPath, PathSize, "%s/%s", DIRECTORY, File.c_str());
		return true;
	}

	inline bool Installed(IStorage &Storage, const std::string &File)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		if(!TargetPath(File, aPath, sizeof(aPath)))
			return false;
		if(Storage.FileExists(aPath, IStorage::TYPE_SAVE))
			return true;
		// 保留旧已下载字体和用户自己放入的字体；不搬动未知文件，不覆盖新目录文件。
		str_format(aPath, sizeof(aPath), "qmclient/fonts/%s", File.c_str());
		return Storage.FileExists(aPath, IStorage::TYPE_SAVE);
	}

	inline bool CompleteDirectoryPath(IStorage &Storage, char *pPath, unsigned PathSize)
	{
		if(PathSize == 0)
			return false;
		pPath[0] = '\0';
		if(!EnsureDirectory(Storage))
			return false;
		Storage.GetCompletePath(IStorage::TYPE_SAVE, DIRECTORY, pPath, PathSize);
		return pPath[0] != '\0';
	}
} // namespace qm_font_download

#endif
