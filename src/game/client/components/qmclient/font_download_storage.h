// 字体商店沿用 DDNet 用户目录的 fonts/fonts_store；启动迁移只合并旧目录，不改归档位置。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FONT_DOWNLOAD_STORAGE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FONT_DOWNLOAD_STORAGE_H

#include <base/str.h>

#include <engine/shared/qm_storage_layout.h>
#include <engine/storage.h>

#include <string>

namespace qm_font_download
{
	inline constexpr const char *DIRECTORY = qm_storage::FONT_STORE_DIR;

	inline bool ValidFilename(const std::string &File)
	{
		return !File.empty() && File.size() < IO_MAX_PATH_LENGTH - 64 &&
		       File.find_first_of("/\\:") == std::string::npos && File.find("..") == std::string::npos &&
		       (str_endswith_nocase(File.c_str(), ".ttf") != nullptr || str_endswith_nocase(File.c_str(), ".otf") != nullptr);
	}

	inline bool EnsureDirectory(IStorage &Storage)
	{
		return qm_storage::EnsureFolder(&Storage, qm_storage::FONT_DIR) &&
		       qm_storage::EnsureFolder(&Storage, DIRECTORY);
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
