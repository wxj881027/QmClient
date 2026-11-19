#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN_SOURCE_H

#include <base/str.h>

#include <cstdint>

// 皮肤列表与下载来源。LOCAL 表示用户目录或随包文件，其余值记录远端来源。
enum class ESkinSource : uint8_t
{
	UNKNOWN,
	LOCAL,
	OFFICIAL,
	COMMUNITY,
	DDSTATS,
	CUSTOM_OFFICIAL,
	CUSTOM_COMMUNITY,
};

inline const char *SkinSourceName(ESkinSource Source)
{
	switch(Source)
	{
	case ESkinSource::OFFICIAL: return "official";
	case ESkinSource::COMMUNITY: return "community";
	case ESkinSource::DDSTATS: return "ddstats";
	case ESkinSource::CUSTOM_OFFICIAL: return "custom_official";
	case ESkinSource::CUSTOM_COMMUNITY: return "custom_community";
	case ESkinSource::LOCAL: return "local";
	case ESkinSource::UNKNOWN: break;
	}
	return "unknown";
}

inline ESkinSource SkinSourceFromName(const char *pName)
{
	if(pName == nullptr)
		return ESkinSource::UNKNOWN;
	if(str_comp(pName, "official") == 0)
		return ESkinSource::OFFICIAL;
	if(str_comp(pName, "community") == 0)
		return ESkinSource::COMMUNITY;
	if(str_comp(pName, "ddstats") == 0)
		return ESkinSource::DDSTATS;
	if(str_comp(pName, "custom_official") == 0)
		return ESkinSource::CUSTOM_OFFICIAL;
	if(str_comp(pName, "custom_community") == 0)
		return ESkinSource::CUSTOM_COMMUNITY;
	if(str_comp(pName, "local") == 0)
		return ESkinSource::LOCAL;
	return ESkinSource::UNKNOWN;
}

inline bool SkinSourceVisible(ESkinSource Source, bool Official, bool Community, bool DdStats)
{
	switch(Source)
	{
	case ESkinSource::OFFICIAL:
	case ESkinSource::CUSTOM_OFFICIAL:
		return Official;
	case ESkinSource::COMMUNITY:
	case ESkinSource::CUSTOM_COMMUNITY:
		return Community;
	case ESkinSource::DDSTATS:
		return DdStats;
	case ESkinSource::LOCAL:
	case ESkinSource::UNKNOWN:
		return true;
	}
	return true;
}

#endif
