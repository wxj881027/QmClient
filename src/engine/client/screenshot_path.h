#ifndef ENGINE_CLIENT_SCREENSHOT_PATH_H
#define ENGINE_CLIENT_SCREENSHOT_PATH_H

#include <game/client/components/qmclient/media_paths.h>

#include <new>
#include <stdexcept>
#include <string>

namespace screenshot_path
{
	// 目录准备可能分配或失败，完成前不改仍在等待的截图路径。
	inline bool Prepare(IStorage *pStorage, const CConfig &Config, const char *pFilename, const char *pTimestamp, char (&aPath)[IO_MAX_PATH_LENGTH])
	{
		try
		{
			const char *pExtension = ".png";
			char aBaseName[IO_MAX_PATH_LENGTH];
			str_copy(aBaseName, pFilename != nullptr && pFilename[0] != '\0' ? pFilename : "screenshot");
			if(str_endswith_nocase(aBaseName, ".webp") != nullptr)
			{
				aBaseName[str_length(aBaseName) - str_length(".webp")] = '\0';
				pExtension = ".webp";
			}
			else if(str_endswith_nocase(aBaseName, ".png") != nullptr)
			{
				aBaseName[str_length(aBaseName) - str_length(".png")] = '\0';
			}
			char aCandidate[IO_MAX_PATH_LENGTH];
			str_format(aCandidate, sizeof(aCandidate), "screenshots/%s_%s%s", aBaseName, pTimestamp, pExtension);
			const std::string Path = qmclient::media_paths::Resolve(pStorage, Config, aCandidate);
			if(!qmclient::media_paths::PrepareWrite(pStorage, Path))
				return false;
			str_copy(aPath, Path.c_str());
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}
}

#endif
