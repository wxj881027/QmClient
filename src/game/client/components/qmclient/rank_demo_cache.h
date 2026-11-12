// Rank 回放缓存目录的文件枚举，以及“demo 浏览器是否已下载同一场次”的判定。
//
// 抽成独立的头文件工具（只依赖 IStorage）是为了能用真实存储做集成测试：
// 这两个判定直接决定“要不要重新下载”，判错时玩家看到的是“点了没反应”
// 或“下到了另一场回放”，都是没有崩溃日志的静默故障。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_DEMO_CACHE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_DEMO_CACHE_H

#include "media_paths.h"
#include "rank_demo_manifest.h"

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <string>
#include <utility>
#include <vector>

namespace qmclient::rank_demo
{
	struct SCacheNameTime
	{
		std::string m_Name;
		time_t m_Modified = 0;
	};

	namespace detail
	{
		struct SCacheListContext
		{
			const char *m_pSuffix;
			std::vector<SCacheNameTime> m_vFiles;
		};

		inline int CacheListCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
		{
			(void)StorageType;
			SCacheListContext *pCtx = (SCacheListContext *)pUser;
			// 后缀不区分大小写：文件名可能来自外部工具或旧版本，`.DEMO` 也要能列出来
			if(IsDir || !str_endswith_nocase(pInfo->m_pName, pCtx->m_pSuffix))
				return 0;
			pCtx->m_vFiles.push_back({pInfo->m_pName, pInfo->m_TimeModified});
			return 0;
		}
	} // namespace detail

	// 列出目录内指定后缀的文件（含修改时间）；目录不存在时返回空列表
	inline std::vector<SCacheNameTime> ListCacheFiles(IStorage *pStorage, const char *pDir, const char *pSuffix)
	{
		detail::SCacheListContext Context;
		Context.m_pSuffix = pSuffix;
		pStorage->ListDirectoryInfo(qmclient::media_paths::StorageType(pDir), pDir, detail::CacheListCallback, &Context);
		return std::move(Context.m_vFiles);
	}

	// 查找 demo 浏览器落盘的同一场次回放。
	// 必须匹配完整文件名（地图 + 名次 + ts + demo 名），不能只按地图前缀：
	// 同一地图存在多条历史 Rank 1，前缀匹配会把别的场次误当成当前条目。
	inline bool FindBrowserDownloadedDemo(IStorage *pStorage, const CConfig &Config, const SEntry &Entry, char *pPath, size_t PathSize)
	{
		const std::string ExpectedName = BrowserDemoFileName(Entry.m_Map.c_str(), Entry.m_Demo.c_str(), Entry.m_Ts, Entry.m_Rank);
		if(ExpectedName.empty())
			return false;
		const std::string Directory = qmclient::media_paths::Directory(pStorage, Config, qmclient::media_paths::EKind::DEMOS);
		if(Directory.empty())
			return false;
		for(const SCacheNameTime &File : ListCacheFiles(pStorage, Directory.c_str(), ".demo"))
		{
			if(str_comp_nocase(File.m_Name.c_str(), ExpectedName.c_str()) != 0)
				continue;
			const std::string Path = qmclient::media_paths::Join(Directory, File.m_Name);
			if(Path.empty() || Path.size() >= PathSize)
				return false;
			str_copy(pPath, Path.c_str(), PathSize);
			return true;
		}
		return false;
	}
} // namespace qmclient::rank_demo

#endif
