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

// 皮肤列表的展示筛选与下载来源开关独立：下载开关决定是否发请求，本枚举只决定列表显示什么。
enum class ESkinListFilter : int
{
	ALL = 0,
	OFFICIAL,
	COMMUNITY,
	DDSTATS,
	CUSTOM,
	VANILLA,
	COUNT,
};

// 同一皮肤可以同时属于多个来源目录。筛选展示的是目录并集，不做互斥归类。
enum class ESkinSourceMask : uint8_t
{
	NONE = 0,
	OFFICIAL = 1 << 0,
	COMMUNITY = 1 << 1,
	DDSTATS = 1 << 2,
	CUSTOM = 1 << 3,
	VANILLA = 1 << 4,
};

// 皮肤列表计划的初始输入分成四类：目录已扫完、目录结果已合并、目录快照已合并、
// 无图标目录缓存的首个网络请求仍在等待。四类全部结算前不启动计划，避免目录每批
// 合并都让后台计划取到不同快照，出现连续重建。
struct SSkinListSourceSettleState
{
	bool m_HasInitialDirectoryScan = false;
	bool m_DirectoryScanPending = false;
	bool m_CatalogMergePending = false;
	bool m_CatalogRequestPending = false;
};

constexpr bool SkinListSourcesSettled(const SSkinListSourceSettleState &State)
{
	return State.m_HasInitialDirectoryScan &&
	       !State.m_DirectoryScanPending &&
	       !State.m_CatalogMergePending &&
	       !State.m_CatalogRequestPending;
}

constexpr ESkinSourceMask operator|(ESkinSourceMask Left, ESkinSourceMask Right)
{
	return static_cast<ESkinSourceMask>(static_cast<uint8_t>(Left) | static_cast<uint8_t>(Right));
}

constexpr ESkinSourceMask &operator|=(ESkinSourceMask &Left, ESkinSourceMask Right)
{
	Left = Left | Right;
	return Left;
}

constexpr bool SkinSourceMaskHas(ESkinSourceMask Mask, ESkinSourceMask Value)
{
	return (static_cast<uint8_t>(Mask) & static_cast<uint8_t>(Value)) != 0;
}

inline ESkinSourceMask SkinSourceMaskFromSource(ESkinSource Source)
{
	switch(Source)
	{
	case ESkinSource::OFFICIAL:
		return ESkinSourceMask::OFFICIAL;
	case ESkinSource::COMMUNITY:
		return ESkinSourceMask::COMMUNITY;
	case ESkinSource::DDSTATS:
		return ESkinSourceMask::DDSTATS;
	case ESkinSource::CUSTOM_OFFICIAL:
		return ESkinSourceMask::OFFICIAL | ESkinSourceMask::CUSTOM;
	case ESkinSource::CUSTOM_COMMUNITY:
		return ESkinSourceMask::COMMUNITY | ESkinSourceMask::CUSTOM;
	case ESkinSource::LOCAL:
	case ESkinSource::UNKNOWN:
		break;
	}
	return ESkinSourceMask::NONE;
}

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

inline ESkinListFilter SkinListFilterFromInt(int Value)
{
	if(Value < 0 || Value >= static_cast<int>(ESkinListFilter::COUNT))
		return ESkinListFilter::ALL;
	return static_cast<ESkinListFilter>(Value);
}

// 旧版只有 cl_vanilla_skins_only；未设置新筛选时继续沿用原生皮肤筛选，
// 但界面和列表都读取同一个有效值，避免显示“全部”却仍按原生皮肤加载。
inline ESkinListFilter ResolveSkinListFilter(int ConfiguredFilter, bool LegacyVanillaOnly)
{
	const ESkinListFilter Filter = SkinListFilterFromInt(ConfiguredFilter);
	if(Filter == ESkinListFilter::ALL && LegacyVanillaOnly)
		return ESkinListFilter::VANILLA;
	return Filter;
}

inline bool VanillaSkinsOnlyActive(int ConfiguredFilter, bool LegacyVanillaOnly)
{
	return ResolveSkinListFilter(ConfiguredFilter, LegacyVanillaOnly) == ESkinListFilter::VANILLA;
}

inline bool SkinListFilterMatches(ESkinSourceMask Sources, ESkinListFilter Filter)
{
	switch(Filter)
	{
	case ESkinListFilter::ALL:
		return true;
	case ESkinListFilter::OFFICIAL:
		return SkinSourceMaskHas(Sources, ESkinSourceMask::OFFICIAL);
	case ESkinListFilter::COMMUNITY:
		return SkinSourceMaskHas(Sources, ESkinSourceMask::COMMUNITY);
	case ESkinListFilter::DDSTATS:
		return SkinSourceMaskHas(Sources, ESkinSourceMask::DDSTATS);
	case ESkinListFilter::CUSTOM:
		return SkinSourceMaskHas(Sources, ESkinSourceMask::CUSTOM);
	case ESkinListFilter::VANILLA:
		return SkinSourceMaskHas(Sources, ESkinSourceMask::VANILLA);
	case ESkinListFilter::COUNT:
		break;
	}
	return true;
}

#endif
