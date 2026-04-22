#ifndef GAME_CLIENT_COMPONENTS_THEME_SCAN_H
#define GAME_CLIENT_COMPONENTS_THEME_SCAN_H

#include <base/system.h>

#include <string>

inline bool IsThemeFileCandidate(const char *pName)
{
	return str_endswith_nocase(pName, ".map") || str_endswith_nocase(pName, ".png");
}

inline std::string ThemeIconPathFromName(const char *pName)
{
	const char *pThemeName = pName != nullptr && pName[0] != '\0' ? pName : "none";
	if(str_endswith_nocase(pThemeName, ".png"))
		return std::string("themes/") + pThemeName;

	char aBaseName[IO_MAX_PATH_LENGTH];
	str_copy(aBaseName, pThemeName, sizeof(aBaseName));
	if(const char *pExt = str_endswith_nocase(aBaseName, ".map"))
	{
		char aTmp[IO_MAX_PATH_LENGTH];
		str_truncate(aTmp, sizeof(aTmp), aBaseName, pExt - aBaseName);
		str_copy(aBaseName, aTmp, sizeof(aBaseName));
	}

	if(const char *pDaySuffix = str_endswith(aBaseName, "_day"))
	{
		char aTmp[IO_MAX_PATH_LENGTH];
		str_truncate(aTmp, sizeof(aTmp), aBaseName, pDaySuffix - aBaseName);
		str_copy(aBaseName, aTmp, sizeof(aBaseName));
	}
	else if(const char *pNightSuffix = str_endswith(aBaseName, "_night"))
	{
		char aTmp[IO_MAX_PATH_LENGTH];
		str_truncate(aTmp, sizeof(aTmp), aBaseName, pNightSuffix - aBaseName);
		str_copy(aBaseName, aTmp, sizeof(aBaseName));
	}

	return std::string("themes/") + aBaseName + ".png";
}

#endif
