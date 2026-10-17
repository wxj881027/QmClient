// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_THEME_SCAN_H
#define GAME_CLIENT_COMPONENTS_THEME_SCAN_H

#include <base/system.h>

#include <game/client/components/background.h>

#include <string>
#include <vector>

inline bool IsThemeFileCandidate(const char *pName)
{
	const char *pExtension = FindBackgroundFileExtension(pName);
	return pExtension != nullptr && str_length(pName) > str_length(pExtension);
}

struct SThemeFileEntry
{
	std::string m_Name;
	bool m_HasDay = true;
	bool m_HasNight = true;
	std::string m_IconPath;
};

enum class EThemeSelectionMatch
{
	NONE,
	OTHER_TIME,
	ANY_TIME,
	PREFERRED_TIME,
	EXACT,
};

// 完整文件名优先；旧配置的不带后缀名称仍按当前时段、通用地图、另一时段匹配。
inline EThemeSelectionMatch ResolveThemeSelectionMatch(const char *pName, const char *pSelectedName, bool IsDaytime)
{
	if(str_comp(pName, pSelectedName) == 0)
		return EThemeSelectionMatch::EXACT;
	if(pSelectedName[0] == '\0' || IsThemeFileCandidate(pSelectedName))
		return EThemeSelectionMatch::NONE;
	const char *pSuffix = str_startswith(pName, pSelectedName);
	if(pSuffix == nullptr)
		return EThemeSelectionMatch::NONE;
	if(str_comp_nocase(pSuffix, IsDaytime ? "_day.map" : "_night.map") == 0)
		return EThemeSelectionMatch::PREFERRED_TIME;
	if(str_comp_nocase(pSuffix, ".map") == 0)
		return EThemeSelectionMatch::ANY_TIME;
	if(str_comp_nocase(pSuffix, IsDaytime ? "_night.map" : "_day.map") == 0)
		return EThemeSelectionMatch::OTHER_TIME;
	return EThemeSelectionMatch::NONE;
}

inline std::string ThemeDisplayName(const char *pName)
{
	if(pName == nullptr || pName[0] == '\0')
		return "(none)";
	if(str_comp(pName, "auto") == 0)
		return "(seasons)";
	if(str_comp(pName, "rand") == 0)
		return "(random)";
	return pName;
}

inline std::string BuildThemeCandidatePath(const char *pBasePath, const char *pExtension)
{
	return std::string(pBasePath) + pExtension;
}

inline std::string ThemeIconPathFromName(const char *pName)
{
	const char *pThemeName = pName != nullptr && pName[0] != '\0' ? pName : "none";
	if(IsBackgroundImageExtension(pThemeName))
		return std::string("themes/") + pThemeName;

	char aBaseName[IO_MAX_PATH_LENGTH];
	str_copy(aBaseName, pThemeName, sizeof(aBaseName));
	if(const char *pExt = str_endswith_nocase(aBaseName, ".map"))
	{
		char aTmp[IO_MAX_PATH_LENGTH];
		str_truncate(aTmp, sizeof(aTmp), aBaseName, pExt - aBaseName);
		str_copy(aBaseName, aTmp, sizeof(aBaseName));
	}
	else if(const char *pBackgroundExt = FindBackgroundFileExtension(aBaseName))
	{
		char aTmp[IO_MAX_PATH_LENGTH];
		str_truncate(aTmp, sizeof(aTmp), aBaseName, str_length(aBaseName) - str_length(pBackgroundExt));
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

inline void AppendThemeFileEntry(std::vector<SThemeFileEntry> &vEntries, const char *pName, bool IsDirectory)
{
	if(IsDirectory || !IsThemeFileCandidate(pName))
		return;
	for(const auto &Entry : vEntries)
	{
		if(Entry.m_Name == pName)
			return;
	}
	// 预览图也可以单独作为背景，由玩家选择；仅去掉不同存储路径中的同名文件。
	vEntries.push_back({pName, true, true, ThemeIconPathFromName(pName)});
}

#endif
