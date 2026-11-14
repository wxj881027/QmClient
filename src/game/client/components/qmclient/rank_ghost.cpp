#include "rank_ghost.h"

#include "media_paths.h"
#include "rank_demo_cache.h"
#include "rank_demo_manifest.h"
#include "rank_ghost_fetch.h"
#include "rank_ghost_parse.h"

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/ghost.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/message.h>
#include <engine/shared/compression.h>
#include <engine/shared/json.h>
#include <engine/shared/network.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>
#include <engine/storage.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/QmUi/QmToast.h>
#include <game/client/components/effects.h>
#include <game/client/components/ghost.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/modes.h>
#include <game/client/components/qmclient/world_event_gate.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/gamecore.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using rank_ghost_fetch::ValidateOrUnpackDemo;

namespace
{
	constexpr int64_t MANIFEST_CACHE_TTL_SECONDS = 300;

	// 缓存枚举与浏览器复用判定放在 rank_demo_cache.h：只依赖存储，可被集成测试直接调用
	using qmclient::rank_demo::FindBrowserDownloadedDemo;
	using qmclient::rank_demo::ListCacheFiles;
	using qmclient::rank_demo::SCacheNameTime;

	// 生成用于缓存文件的名称：去掉服务端的 .gz 后缀并清理非法字符。
	// 服务端文件名本身是 uuid-hash.demo.gz 形式，这里只做防御性处理。
	void MakeSafeCacheName(const char *pRawName, char *pOut, int OutSize)
	{
		std::string Name = pRawName != nullptr ? pRawName : "";
		if(Name.size() > 3 && Name.compare(Name.size() - 3, 3, ".gz") == 0)
			Name.resize(Name.size() - 3);
		// 去掉已带的 .demo 后缀，后续统一追加，避免出现 .demo.demo
		if(Name.size() > 5 && Name.compare(Name.size() - 5, 5, ".demo") == 0)
			Name.resize(Name.size() - 5);
		str_copy(pOut, Name.c_str(), OutSize);
		str_sanitize_filename(pOut);
	}

	// ===== 缓存去重 =====
	// 同一条官方回放在 manifest 里可能对应多条记录（每名成员一条，uuid 各异），
	// 而缓存文件名含 uuid8，会让同一内容生成多份缓存、被反复下载。
	// 统一按 <map>_rank<N>_<kind>_<time>s_ 前缀复用与清理，保证同一内容只保留一份。
	void EntryCachePrefix(const qmclient::rank_demo::SEntry &Entry, char *pOut, size_t OutSize)
	{
		char aSafeMap[64];
		str_copy(aSafeMap, Entry.m_Map.c_str(), sizeof(aSafeMap));
		str_sanitize_filename(aSafeMap);
		char aSafeTime[32];
		str_copy(aSafeTime, Entry.m_Time.c_str(), sizeof(aSafeTime));
		str_sanitize_filename(aSafeTime);
		str_format(pOut, OutSize, "%s_rank%d_%s_%ss_",
			aSafeMap, Entry.m_Rank, qmclient::rank_demo::IsTeamEntry(Entry) ? "team" : "solo", aSafeTime);
	}

	// 新命名的缓存：文件名带 _solo_ / _team_ 段；不匹配的旧命名 ghost
	// 全部是解析缺陷修复前的产物（位置字段错乱、开火特效异常），直接清理。
	bool IsNamedRankCache(const char *pName)
	{
		return str_find(pName, "_solo_") != nullptr || str_find(pName, "_team_") != nullptr;
	}

	// 前缀键：取文件名中 uuid8 之前的部分（uuid8 内不含下划线，取最后一个 '_' 即可）
	bool EntryCacheKeyFromName(const char *pName, char *pKey, size_t KeySize)
	{
		const char *pLastSep = str_rchr(pName, '_');
		if(pLastSep == nullptr || pLastSep == pName || (size_t)(pLastSep - pName) + 1 >= KeySize)
			return false;
		mem_copy(pKey, pName, pLastSep - pName + 1);
		pKey[pLastSep - pName + 1] = '\0';
		return true;
	}

	// 复用同前缀中最新的缓存文件（忽略 uuid 差异）：“下载过一次后就不再下载”。
	void ReuseEntryCachePath(IStorage *pStorage, const char *pDir, const char *pPrefix, const char *pSuffix, char *pPath, size_t PathSize)
	{
		const std::vector<SCacheNameTime> vFiles = ListCacheFiles(pStorage, pDir, pSuffix);
		const SCacheNameTime *pBest = nullptr;
		for(const SCacheNameTime &File : vFiles)
		{
			if(!str_startswith(File.m_Name.c_str(), pPrefix))
				continue;
			if(pBest == nullptr || File.m_Modified > pBest->m_Modified)
				pBest = &File;
		}
		if(pBest == nullptr)
			return;
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s", pDir, pBest->m_Name.c_str());
		if(str_comp(aPath, pPath) != 0)
			str_copy(pPath, aPath, PathSize);
	}

	// 迁移完成后清理空的遗留目录（demos/rank_ghost），避免 demo 浏览器残留无效入口；
	// RemoveFolder 是非递归删除，目录仍有其他文件时失败并静默跳过
	void RemoveLegacyDemoDirIfEmpty(IStorage *pStorage, const char *pDir)
	{
		if(!pStorage->FolderExists(pDir, IStorage::TYPE_SAVE))
			return;
		if(!ListCacheFiles(pStorage, pDir, ".demo").empty())
			return;
		pStorage->RemoveFolder(pDir, IStorage::TYPE_SAVE);
	}

	// 清理 rank 缓存目录：
	//  - ghosts/rank_ghost 里旧命名的 .gho 全部删除（demo 目录的旧命名仍有效，保留）；
	//  - 同前缀（同地图/名次/类型/成绩）的多份缓存只保留最新一份。
	void PruneRankGhostCaches(IStorage *pStorage, const char *pGhostDir, const char *pDemoDir)
	{
		std::vector<std::string> vRemove;
		auto RemoveOlderDuplicates = [&vRemove, pStorage](const char *pDir, const char *pSuffix, bool DeleteUnnamed) {
			const std::vector<SCacheNameTime> vFiles = ListCacheFiles(pStorage, pDir, pSuffix);
			std::map<std::string, SCacheNameTime> BestByPrefix;
			for(const SCacheNameTime &File : vFiles)
			{
				const bool Named = IsNamedRankCache(File.m_Name.c_str());
				if(!Named && DeleteUnnamed)
				{
					char aPath[IO_MAX_PATH_LENGTH];
					str_format(aPath, sizeof(aPath), "%s/%s", pDir, File.m_Name.c_str());
					vRemove.push_back(aPath);
					continue;
				}
				if(!Named)
					continue;
				char aKey[128];
				if(!EntryCacheKeyFromName(File.m_Name.c_str(), aKey, sizeof(aKey)))
					continue;
				auto It = BestByPrefix.find(aKey);
				if(It == BestByPrefix.end())
				{
					BestByPrefix.emplace(aKey, File);
					continue;
				}
				char aPath[IO_MAX_PATH_LENGTH];
				if(File.m_Modified > It->second.m_Modified)
				{
					str_format(aPath, sizeof(aPath), "%s/%s", pDir, It->second.m_Name.c_str());
					It->second = File;
				}
				else
				{
					str_format(aPath, sizeof(aPath), "%s/%s", pDir, File.m_Name.c_str());
				}
				vRemove.push_back(aPath);
			}
		};
		RemoveOlderDuplicates(pGhostDir, ".gho", true);
		RemoveOlderDuplicates(pDemoDir, ".demo", false);
		for(const std::string &Path : vRemove)
			pStorage->RemoveFile(Path.c_str(), IStorage::TYPE_SAVE);
	}

	// ===== 旧目录迁移/清理（qmclient/rank1 启用前的一次性处理） =====

	void RemoveGhoFilesIn(IStorage *pStorage, const char *pDir)
	{
		for(const SCacheNameTime &File : ListCacheFiles(pStorage, pDir, ".gho"))
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "%s/%s", pDir, File.m_Name.c_str());
			pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE);
		}
	}

	struct SSubDirCollector
	{
		const char *m_pDir;
		std::vector<std::string> *m_pOut;
	};

	int CollectSubDirCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		auto *pCtx = (SSubDirCollector *)pUser;
		if(IsDir)
			pCtx->m_pOut->push_back(std::string(pCtx->m_pDir) + "/" + pInfo->m_pName);
		return 0;
	}

	// 清理旧影子目录（全部是时间轴修复前的产物，含多轨子目录）
	void CleanupLegacyGhosts(IStorage *pStorage, const char *pDir)
	{
		if(!pStorage->FolderExists(pDir, IStorage::TYPE_SAVE))
			return;
		RemoveGhoFilesIn(pStorage, pDir);
		std::vector<std::string> vSubDirs;
		SSubDirCollector Collector{pDir, &vSubDirs};
		pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, pDir, CollectSubDirCallback, &Collector);
		for(const std::string &Sub : vSubDirs)
			RemoveGhoFilesIn(pStorage, Sub.c_str());
	}

	// 旧 demo 内容有效：搬到新目录复用（新命名优先，其次旧 uuid 命名）
	void MigrateLegacyDemos(IStorage *pStorage, const char *pNewDemoPath, const char *pOldNamedPath, const char *pLegacyNamedPath)
	{
		if(pStorage->FileExists(pNewDemoPath, IStorage::TYPE_SAVE))
			return;
		if(pStorage->FileExists(pOldNamedPath, IStorage::TYPE_SAVE))
			pStorage->RenameFile(pOldNamedPath, pNewDemoPath, IStorage::TYPE_SAVE);
		else if(pStorage->FileExists(pLegacyNamedPath, IStorage::TYPE_SAVE))
			pStorage->RenameFile(pLegacyNamedPath, pNewDemoPath, IStorage::TYPE_SAVE);
	}

	// 条目对应的 ghost 缓存是否存在（单文件或同前缀多轨目录）
	bool GhostCacheExists(IStorage *pStorage, const char *pGhostPath)
	{
		if(pStorage->FileExists(pGhostPath, IStorage::TYPE_SAVE))
			return true;
		char aGroupDir[IO_MAX_PATH_LENGTH];
		str_copy(aGroupDir, pGhostPath, sizeof(aGroupDir));
		const size_t Len = str_length(aGroupDir);
		if(Len > 4 && str_endswith(aGroupDir, ".gho"))
			aGroupDir[Len - 4] = '\0';
		return pStorage->FolderExists(aGroupDir, IStorage::TYPE_SAVE) && !ListCacheFiles(pStorage, aGroupDir, ".gho").empty();
	}

	// 删除条目的 ghost 缓存（单文件与多轨目录内的轨迹都清掉）
	void RemoveGhostCacheFiles(IStorage *pStorage, const char *pGhostPath)
	{
		char aGroupDir[IO_MAX_PATH_LENGTH];
		str_copy(aGroupDir, pGhostPath, sizeof(aGroupDir));
		const size_t Len = str_length(aGroupDir);
		if(Len > 4 && str_endswith(aGroupDir, ".gho"))
			aGroupDir[Len - 4] = '\0';
		if(pStorage->FolderExists(aGroupDir, IStorage::TYPE_SAVE))
		{
			for(const SCacheNameTime &File : ListCacheFiles(pStorage, aGroupDir, ".gho"))
			{
				char aPath[IO_MAX_PATH_LENGTH];
				str_format(aPath, sizeof(aPath), "%s/%s", aGroupDir, File.m_Name.c_str());
				pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE);
			}
		}
		pStorage->RemoveFile(pGhostPath, IStorage::TYPE_SAVE);
	}

} // namespace

CRankGhost::CRankGhost() = default;

CRankGhost::~CRankGhost()
{
	// 兜底：OnShutdown 未走到时也不能让 CDemoPlayer 带着打开的文件析构
	if(m_pParse && m_pParse->m_Context.m_pPlayer)
		m_pParse->m_Context.m_pPlayer->Stop();
}

void CRankGhost::RequestCurrentMapGhost(int Rank)
{
	StartLookup(nullptr, Rank);
}

void CRankGhost::OnConsoleInit()
{
	Console()->Register("qm_rank_ghost", "?s[map] ?i[rank]", CFGFLAG_CLIENT, ConRankGhost, this,
		"Load the official rank replay of a map as a ghost (default: current map, rank 1)");
	Console()->Register("qm_rank_ghost_off", "", CFGFLAG_CLIENT, ConRankGhostOff, this, "Unload the rank ghost");
	Console()->Register("qm_rank_ghost_view", "", CFGFLAG_CLIENT, ConRankGhostView, this,
		"Toggle ghost view mode: play the ghost on an independent timeline (in-game demo player)");
}

void CRankGhost::ConRankGhost(IConsole::IResult *pResult, void *pUserData)
{
	CRankGhost *pSelf = (CRankGhost *)pUserData;
	const char *pMap = pResult->NumArguments() > 0 ? pResult->GetString(0) : nullptr;
	const int Rank = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : 1;
	pSelf->StartLookup(pMap, Rank);
}

void CRankGhost::ConRankGhostOff(IConsole::IResult *pResult, void *pUserData)
{
	CRankGhost *pSelf = (CRankGhost *)pUserData;
	pSelf->m_UnloadPending = true;
}

void CRankGhost::ConRankGhostView(IConsole::IResult *pResult, void *pUserData)
{
	CRankGhost *pSelf = (CRankGhost *)pUserData;
	(void)pResult;
	if(pSelf->m_ViewMode)
		pSelf->ViewStop();
	else if(!pSelf->m_vLoadedSlots.empty())
		pSelf->EnterViewMode();
	else
		pSelf->Echo(Localize("Rank ghost: load a ghost first (Rank 1 page or qm_rank_ghost)"));
}

void CRankGhost::Echo(const char *pMessage) const
{
	if(pMessage == nullptr || pMessage[0] == '\0')
		return;
	GameClient()->m_Chat.Echo(pMessage);
}

void CRankGhost::Fail(const char *pDownloadMessage, const char *pGhostMessage)
{
	Fail(m_PendingMode == EPendingMode::DEMO_ONLY ? pDownloadMessage : pGhostMessage);
}

void CRankGhost::Fail(const char *pMessage)
{
	log_error("rank_ghost", "%s", pMessage != nullptr ? pMessage : "unknown error");
	Echo(pMessage);
	// Rank 1 页的“下载回放”在菜单内触发，聊天区被菜单挡住；失败必须同时给出可见提示。
	// 只有确实有待执行目标时才提示：后台清单预热沿用了上一次请求的模式，不能弹给玩家。
	const bool UserFacingDownload = m_PendingMode == EPendingMode::DEMO_ONLY &&
					(!m_PendingMap.empty() || !m_PendingDemo.empty());
	if(UserFacingDownload && pMessage != nullptr)
		qm_ui::Toast(EQmToastKind::FAILED, pMessage);
	// 任务失败后不应再自动进入查看模式
	m_PendingView = false;
	ClearPendingRequest();
	AbortTask();
}

void CRankGhost::AbortTask()
{
	const bool DownloadInProgress = m_Stage == EStage::FETCH_DEMO;
	if(m_pManifestRequest)
	{
		m_pManifestRequest->Abort();
		m_pManifestRequest = nullptr;
	}
	if(m_pDemoRequest)
	{
		m_pDemoRequest->Abort();
		m_pDemoRequest = nullptr;
	}
	if(m_pParse && m_pParse->m_Context.m_pPlayer)
		m_pParse->m_Context.m_pPlayer->Stop();
	m_pParse.reset();
	m_Stage = EStage::IDLE;
	// 时间线补齐被中断时必须清掉标记，否则下一次正常解析会误跳过影子写出
	m_EventRebuildOnly = false;
	if(DownloadInProgress && m_aDemoStoragePath[0] != '\0')
		Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
}

// 请求到达终态后清空目标：否则下一次清单刷新会把上次请求当成新请求重放。
void CRankGhost::ClearPendingRequest()
{
	m_PendingMap.clear();
	m_PendingDemo.clear();
}

void CRankGhost::OnUpdate()
{
	if(!m_PendingNotify.empty())
	{
		Echo(m_PendingNotify.c_str());
		m_PendingNotify.clear();
	}

	if(m_UnloadPending)
	{
		m_UnloadPending = false;
		m_StartPending = false;
		m_PendingView = false;
		m_RetryLoadPending = false;
		m_RetryLoadDeadline = 0;
		m_RetryLoadNextAttempt = 0;
		if(m_Stage != EStage::IDLE)
			AbortTask();
		UnloadGhost();
		Echo(Localize("Rank ghost unloaded"));
	}

	if(m_StartPending)
	{
		m_StartPending = false;
		if(m_ManifestLoaded && time_get() - m_ManifestLoadedAt < time_freq() * MANIFEST_CACHE_TTL_SECONDS)
			LookupInManifest();
		else
			StartManifestFetch();
	}

	if(m_RetryLoadPending)
	{
		const int64_t Now = time_get();
		// 地图名匹配不代表地图数据已就绪，未加载完成时不尝试，
		// 避免把刚生成的有效 ghost 当作损坏文件删除
		const auto *pMap = GameClient()->Map();
		const bool MapReady = pMap != nullptr && pMap->IsLoaded();
		// 按 demo 名发起的请求没有登记地图名，改用条目自带的地图名
		const std::string &TargetMap = !m_PendingMap.empty() ? m_PendingMap : m_ActiveEntry.m_Map;
		if(MapReady && Client()->GetCurrentMap()[0] != '\0' && str_comp_nocase(Client()->GetCurrentMap(), TargetMap.c_str()) == 0 && Now >= m_RetryLoadNextAttempt)
		{
			if(LoadGhostTarget())
			{
				m_RetryLoadPending = false;
				NotifyLoaded(m_ActiveEntry.m_Names.c_str(), m_ActiveEntry.m_Time.c_str());
			}
			else
			{
				// 地图已加载仍无法读取，说明缓存影子损坏或与地图不匹配；
				// 删除后从缓存回放重建，避免在同一坏文件上无限重试。
				RemoveGhostCacheFiles(Storage(), m_aGhostStoragePath);
				m_RetryLoadPending = false;
				m_RetryLoadDeadline = 0;
				m_RetryLoadNextAttempt = 0;
				if(Storage()->FileExists(m_aDemoStoragePath, IStorage::TYPE_SAVE))
					StartParse();
				else
					StartDemoFetch();
			}
		}
		if(m_RetryLoadPending && Now >= m_RetryLoadDeadline)
		{
			m_RetryLoadPending = false;
			m_PendingNotify = Localize("Rank ghost: timed out waiting for the map to load");
		}
	}

	AlignToCurrentRun();

	switch(m_Stage)
	{
	case EStage::IDLE:
		break;
	case EStage::FETCH_MANIFEST:
		UpdateManifestStage();
		break;
	case EStage::FETCH_DEMO:
	case EStage::FETCH_DEMO_ONLY:
		UpdateDemoStage();
		break;
	case EStage::PARSE:
		UpdateParseStage();
		break;
	}
}

void CRankGhost::OnMapLoad()
{
	// CGhost::OnMapLoad 已先清空全部槽位并重建列表，旧 rank 槽位不再有效。
	OnGhostsUnloaded();
}

void CRankGhost::OnReset()
{
	if(GameClient() != nullptr && GameClient()->m_Ghost.ManualModeActive())
		GameClient()->m_Ghost.StopManual();
	const bool KeepRetryLoad = m_RetryLoadPending && m_aGhostStoragePath[0] != '\0';
	if(m_Stage != EStage::IDLE)
		AbortTask();
	m_StartPending = false;
	m_RetryLoadPending = KeepRetryLoad;
	m_RetryLoadDeadline = KeepRetryLoad ? time_get() + time_freq() * 30 : 0;
	// 地图切换后稍等片刻再尝试加载，避开地图数据尚未就绪的窗口
	m_RetryLoadNextAttempt = KeepRetryLoad ? time_get() + time_freq() / 2 : 0;
	// 地图切换/断线会清空 CGhost 的全部槽位，这里同步失效本地记录，
	// 避免之后误卸载别的影子。
	m_vLoadedSlots.clear();
	m_vLoadedGhostPaths.clear();
	m_aLoadingGroupPrefix[0] = '\0';
	m_ViewMode = false;
	m_PendingView = false;
	m_EventRebuildOnly = false;
	m_LastAlignedRaceTick = -1;
	m_ViewZoomPersonal = 0.0f;
}

void CRankGhost::OnShutdown()
{
	// 下载/解析未收尾时必须先停掉 CDemoPlayer，否则析构断言会在 Release 下 abort
	if(m_StartPending || m_Stage != EStage::IDLE)
		AbortTask();
	m_StartPending = false;
	UnloadGhost();
}

void CRankGhost::OnRender()
{
	// 查看模式按播放头回放时间线（事件 + 开关状态）；非查看态只重置游标
	if(!IsViewModeActive())
	{
		m_ViewLastTick = -1;
		m_ViewAppliedSwitch = -1;
		m_ViewEventCursor = 0;
		return;
	}
	UpdateViewTimeline();
}

void CRankGhost::StartLookup(const char *pMap, int Rank)
{
	if(m_Stage != EStage::IDLE || m_StartPending)
	{
		m_PendingNotify = Localize("Rank ghost: another task is already running");
		return;
	}

	char aMap[64] = "";
	if(pMap != nullptr && pMap[0] != '\0')
		str_copy(aMap, pMap);
	else
		str_copy(aMap, Client()->GetCurrentMap());

	if(aMap[0] == '\0')
	{
		m_PendingNotify = Localize("Rank ghost: specify a map name when not in a game");
		return;
	}

	// 新请求不能继承上一次等待地图加载的状态，否则地图切换后可能尝试加载旧影子。
	m_RetryLoadPending = false;
	m_RetryLoadDeadline = 0;
	m_RetryLoadNextAttempt = 0;
	m_PendingMap = aMap;
	m_PendingRank = std::clamp(Rank, 1, 9999);
	m_PendingDemo.clear();
	m_PendingMode = EPendingMode::GHOST;
	m_StartPending = true;
	log_info("rank_ghost", "lookup queued: map='%s' rank=%d", aMap, m_PendingRank);
}

void CRankGhost::StartPendingLookup(const char *pDemoName, EPendingMode Mode)
{
	if(pDemoName == nullptr || pDemoName[0] == '\0')
		return;
	if(m_Stage != EStage::IDLE || m_StartPending)
	{
		Echo(Localize("Rank ghost: another task is already running"));
		return;
	}

	m_RetryLoadPending = false;
	m_RetryLoadDeadline = 0;
	m_RetryLoadNextAttempt = 0;
	m_PendingMap.clear();
	m_PendingDemo = pDemoName;
	m_PendingMode = Mode;
	m_StartPending = true;
	log_info("rank_ghost", "request queued: demo='%s' mode=%s", pDemoName, Mode == EPendingMode::DEMO_ONLY ? "demo" : "ghost");
}

CRankGhost::EManifestState CRankGhost::ManifestState() const
{
	if(m_Stage == EStage::FETCH_MANIFEST)
		return EManifestState::LOADING;
	if(m_ManifestLoaded)
		return EManifestState::READY;
	if(m_ManifestFailed)
		return EManifestState::FAILED;
	return EManifestState::UNKNOWN;
}

void CRankGhost::EnsureManifest()
{
	if(m_Stage != EStage::IDLE || m_StartPending)
		return;
	if(m_ManifestLoaded && time_get() - m_ManifestLoadedAt < time_freq() * MANIFEST_CACHE_TTL_SECONDS)
		return;
	// 失败后冷却一段时间再自动重试，避免页面每帧都发起请求
	if(m_ManifestFailed && time_get() - m_ManifestFailedAt < time_freq() * 60)
		return;
	StartManifestFetch();
}

void CRankGhost::RefreshManifest()
{
	if(m_Stage != EStage::IDLE)
		AbortTask();
	m_ManifestLoaded = false;
	m_ManifestFailed = false;
	StartManifestFetch();
}

std::vector<qmclient::rank_demo::SEntry> CRankGhost::CollectRankEntries(const char *pMap, int Rank) const
{
	std::vector<SEntry> Out;
	if(m_ManifestLoaded)
		qmclient::rank_demo::CollectRankEntries(m_vEntries, pMap, Rank, Out);
	return Out;
}

std::vector<qmclient::rank_demo::SEntry> CRankGhost::CollectSearchEntries(const char *pQuery, int Rank) const
{
	std::vector<SEntry> Out;
	if(!m_ManifestLoaded || pQuery == nullptr || pQuery[0] == '\0')
		return Out;
	std::set<std::string> SeenDemos;
	// 不在这里按展示条数提前退出：先收集全部匹配项，调用方排序后再截断，
	// 否则表头排序只是清单前 N 条的局部排序。
	for(const SEntry &Entry : m_vEntries)
	{
		if(Entry.m_Rank != Rank)
			continue;
		if(str_find_nocase(Entry.m_Map.c_str(), pQuery) == nullptr)
			continue;
		if(!SeenDemos.insert(Entry.m_Demo).second)
			continue;
		Out.push_back(Entry);
	}
	return Out;
}

void CRankGhost::EnsureFolders(IStorage *pStorage)
{
	if(pStorage == nullptr)
		return;
	// 逐级创建（IStorage::CreateFolder 不支持一次创建多级）
	pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
	pStorage->CreateFolder(RANK1_ROOT, IStorage::TYPE_SAVE);
	pStorage->CreateFolder(DEMO_CACHE_DIR, IStorage::TYPE_SAVE);
	char aDir[IO_MAX_PATH_LENGTH];
	str_format(aDir, sizeof(aDir), "%s/%s", RANK1_ROOT, GHOST_SUBDIR);
	pStorage->CreateFolder(aDir, IStorage::TYPE_SAVE);
	// 顺手清理已迁移完毕的遗留目录，demo 浏览器不再显示无效入口
	RemoveLegacyDemoDirIfEmpty(pStorage, LEGACY_DEMO_DIR);
}

void CRankGhost::RequestGhostForDemo(const char *pDemoName)
{
	StartPendingLookup(pDemoName, EPendingMode::GHOST);
}

void CRankGhost::RequestGhostViewForDemo(const char *pDemoName)
{
	if(pDemoName == nullptr || pDemoName[0] == '\0')
		return;
	// 仅当已加载的影子正是该 demo 对应的条目时才直接进入查看模式；
	// 加载的是别的条目时走加载管线（会先卸载旧影子再加载正确目标）
	for(const SEntry &Entry : m_vEntries)
	{
		if(Entry.m_Demo != pDemoName)
			continue;
		if(IsEntryGhostActive(Entry))
		{
			if(!m_ViewMode)
				EnterViewMode();
			return;
		}
		break;
	}
	if(m_Stage != EStage::IDLE || m_StartPending)
	{
		Echo(Localize("Rank ghost: another task is already running"));
		return;
	}
	m_PendingView = true;
	StartPendingLookup(pDemoName, EPendingMode::GHOST);
}

void CRankGhost::RequestGhostOff()
{
	m_UnloadPending = true;
}

void CRankGhost::RequestDemoDownload(const char *pDemoName)
{
	StartPendingLookup(pDemoName, EPendingMode::DEMO_ONLY);
}

bool CRankGhost::IsBusy() const
{
	return m_Stage != EStage::IDLE || m_StartPending;
}

bool CRankGhost::EntryTaskProgress(const SEntry &Entry, EEntryTaskPhase &Phase, int &Percent) const
{
	Phase = EEntryTaskPhase::NONE;
	Percent = 0;
	// 按 demo 名请求时以 m_PendingDemo 为准；按地图/名次请求时
	// 清单解析完成后以 m_ActiveEntry 为准。
	const bool Matches = !m_PendingDemo.empty() ? Entry.m_Demo == m_PendingDemo : Entry.m_Demo == m_ActiveEntry.m_Demo;
	if(!Matches)
		return false;
	switch(m_Stage)
	{
	case EStage::IDLE:
		// 请求已登记但主循环还没开始下载。
		if(m_StartPending || m_PendingView)
		{
			Phase = EEntryTaskPhase::DOWNLOAD;
			return true;
		}
		return false;
	case EStage::FETCH_MANIFEST:
		return false;
	case EStage::FETCH_DEMO:
	case EStage::FETCH_DEMO_ONLY:
		Phase = EEntryTaskPhase::DOWNLOAD;
		Percent = m_pDemoRequest ? m_pDemoRequest->Progress() : 0;
		return true;
	case EStage::PARSE:
		Phase = EEntryTaskPhase::CONVERT;
		if(m_pParse && m_pParse->m_Context.m_pPlayer)
		{
			const SParseContext &Parse = m_pParse->m_Context;
			const int Cur = Parse.m_pPlayer->Info()->m_Info.m_CurrentTick;
			const int RunTicks = Parse.m_RunEnd - Parse.m_RunStart;
			Percent = RunTicks > 0 ? std::clamp((Cur - Parse.m_RunStart) * 100 / RunTicks, 0, 100) : 0;
		}
		return true;
	default:
		return false;
	}
}

bool CRankGhost::DescribeTask(char *pBuf, size_t BufSize) const
{
	pBuf[0] = '\0';
	switch(m_Stage)
	{
	case EStage::FETCH_MANIFEST:
		str_copy(pBuf, Localize("Rank ghost: fetching the replay list ..."), BufSize);
		return true;
	case EStage::FETCH_DEMO:
	case EStage::FETCH_DEMO_ONLY:
		if(m_pDemoRequest)
		{
			const int Percent = m_pDemoRequest->Progress();
			if(Percent > 0)
				str_format(pBuf, BufSize, Localize("Rank ghost: downloading the replay (%d%%)"), Percent);
			else
				str_copy(pBuf, Localize("Rank ghost: downloading the replay ..."), BufSize);
			return true;
		}
		return false;
	case EStage::PARSE:
		if(m_pParse && m_pParse->m_Context.m_pPlayer)
		{
			const SParseContext &Parse = m_pParse->m_Context;
			const int Cur = Parse.m_pPlayer->Info()->m_Info.m_CurrentTick;
			const int RunTicks = Parse.m_RunEnd - Parse.m_RunStart;
			const int Percent = RunTicks > 0 ? std::clamp((Cur - Parse.m_RunStart) * 100 / RunTicks, 0, 100) : 0;
			str_format(pBuf, BufSize, Localize("Rank ghost: converting the replay (%d%%)"), Percent);
			return true;
		}
		return false;
	default:
		return false;
	}
}

void CRankGhost::BuildEntryCachePaths(const SEntry &Entry, char *pDemoPath, size_t DemoPathSize, char *pGhostPath, size_t GhostPathSize)
{
	char aSafeMap[64];
	str_copy(aSafeMap, Entry.m_Map.c_str(), sizeof(aSafeMap));
	str_sanitize_filename(aSafeMap);

	char aSafeTime[32];
	str_copy(aSafeTime, Entry.m_Time.c_str(), sizeof(aSafeTime));
	str_sanitize_filename(aSafeTime);

	// demo 标识：manifest 的 uuid 前 8 位（缺失时回退 demo 名前 8 位）
	char aUuid8[9] = "";
	const char *pUuid = Entry.m_Uuid.c_str();
	if(pUuid[0] == '\0')
		pUuid = Entry.m_Demo.c_str();
	for(int i = 0, j = 0; i < 8 && pUuid[j] != '\0'; j++)
	{
		if(pUuid[j] == '-')
			continue;
		aUuid8[i++] = pUuid[j];
	}

	str_format(pDemoPath, DemoPathSize, "%s/%s_rank%d_%s_%ss_%s.demo",
		DEMO_CACHE_DIR, aSafeMap, Entry.m_Rank,
		IsTeamEntry(Entry) ? "team" : "solo", aSafeTime, aUuid8);
	str_format(pGhostPath, GhostPathSize, "%s/%s/%s_rank%d_%s_%ss_%s.gho",
		RANK1_ROOT, GHOST_SUBDIR, aSafeMap, Entry.m_Rank,
		IsTeamEntry(Entry) ? "team" : "solo", aSafeTime, aUuid8);
}

bool CRankGhost::IsEntryDemoCached(const SEntry &Entry, char *pDemoPath, size_t DemoPathSize) const
{
	char aGhostPath[IO_MAX_PATH_LENGTH];
	BuildEntryCachePaths(Entry, pDemoPath, DemoPathSize, aGhostPath, sizeof(aGhostPath));
	return Storage()->FileExists(pDemoPath, IStorage::TYPE_SAVE);
}

bool CRankGhost::IsEntryGhostCached(const SEntry &Entry, char *pGhostPath, size_t GhostPathSize) const
{
	char aDemoPath[IO_MAX_PATH_LENGTH];
	BuildEntryCachePaths(Entry, aDemoPath, sizeof(aDemoPath), pGhostPath, GhostPathSize);
	return GhostCacheExists(Storage(), pGhostPath);
}

bool CRankGhost::IsEntryGhostActive(const SEntry &Entry) const
{
	return LoadedGhostCountForEntry(Entry) > 0;
}

int CRankGhost::LoadedGhostCountForEntry(const SEntry &Entry) const
{
	if(m_vLoadedGhostPaths.empty())
		return 0;
	char aDemoPath[IO_MAX_PATH_LENGTH];
	char aGhostPath[IO_MAX_PATH_LENGTH];
	BuildEntryCachePaths(Entry, aDemoPath, sizeof(aDemoPath), aGhostPath, sizeof(aGhostPath));
	// 单文件精确匹配；多轨目录按前缀（<base>/）匹配任意已加载轨迹
	char aGroupPrefix[IO_MAX_PATH_LENGTH];
	str_copy(aGroupPrefix, aGhostPath, sizeof(aGroupPrefix));
	const size_t Len = str_length(aGroupPrefix);
	if(Len > 4 && str_endswith(aGroupPrefix, ".gho"))
		aGroupPrefix[Len - 4] = '\0';
	str_append(aGroupPrefix, "/", sizeof(aGroupPrefix));
	int Count = 0;
	for(const std::string &Path : m_vLoadedGhostPaths)
	{
		if(str_comp(Path.c_str(), aGhostPath) == 0 || str_startswith(Path.c_str(), aGroupPrefix))
			Count++;
	}
	return Count;
}

void CRankGhost::DeleteEntryCache(const SEntry &Entry)
{
	char aDemoPath[IO_MAX_PATH_LENGTH];
	char aGhostPath[IO_MAX_PATH_LENGTH];
	BuildEntryCachePaths(Entry, aDemoPath, sizeof(aDemoPath), aGhostPath, sizeof(aGhostPath));
	if(IsEntryGhostActive(Entry))
	{
		UnloadGhost();
		// 缓存已删，等待中的“加载后进入查看模式”也随之取消
		m_PendingView = false;
	}
	Storage()->RemoveFile(aDemoPath, IStorage::TYPE_SAVE);
	RemoveGhostCacheFiles(Storage(), aGhostPath);
}

bool CRankGhost::ParseManifest(const unsigned char *pData, size_t DataSize)
{
	std::vector<SEntry> ParsedEntries;
	if(!qmclient::rank_demo::ParseManifest(pData, DataSize, ParsedEntries))
	{
		m_vEntries.clear();
		return false;
	}
	m_vEntries = std::move(ParsedEntries);
	return true;
}

void CRankGhost::LookupInManifest()
{
	// 目录准备与旧影子清理：无论当前地图是否有官方回放都执行，
	// 保证“回放目录”按钮与迁移逻辑不依赖具体条目
	EnsureFolders(Storage());
	CleanupLegacyGhosts(Storage(), LEGACY_GHOST_DIR);

	// 仅预热清单的浏览请求（Rank 1 页面 EnsureManifest）没有待执行目标
	if(m_PendingMap.empty() && m_PendingDemo.empty())
	{
		m_Stage = EStage::IDLE;
		return;
	}

	// 精确匹配：Rank 1 页面按 manifest 的 demo 名发起（同一 demo 可能有
	// 多条成员记录，任取其一即可，缓存路径与内容都相同）
	const SEntry *pEntry = nullptr;
	if(!m_PendingDemo.empty())
	{
		for(const SEntry &Entry : m_vEntries)
		{
			if(Entry.m_Demo == m_PendingDemo)
			{
				pEntry = &Entry;
				break;
			}
		}
		if(pEntry == nullptr)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), Localize("Rank ghost: no official replay found for '%s'"), m_PendingDemo.c_str());
			Fail(Localize("No rank 1 replay was found for this map"), aBuf);
			return;
		}
	}
	else
	{
		// 同一名次可能有历史记录，取最新完成的一条
		pEntry = qmclient::rank_demo::FindLatest(m_vEntries, m_PendingMap.c_str(), m_PendingRank);
	}
	if(pEntry == nullptr)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), Localize("Rank ghost: no official replay for '%s' rank %d"),
			m_PendingMap.c_str(), m_PendingRank);
		Fail(Localize("No rank 1 replay was found for this map"), aBuf);
		return;
	}

	m_ActiveEntry = *pEntry;
	log_info("rank_ghost", "entry found: demo='%s' time='%s' cid=%d", m_ActiveEntry.m_Demo.c_str(), m_ActiveEntry.m_Time.c_str(), m_ActiveEntry.m_Cid);

	// manifest 缺 ts 字段时用 0 兜底，避免生成异常的文件名
	const int64_t DemoModifiedAt = m_ActiveEntry.m_Ts == std::numeric_limits<int64_t>::min() ? 0 : m_ActiveEntry.m_Ts;

	// 可读缓存命名：<map>_rank<N>_<kind>_<time>s_<uuid8>
	BuildEntryCachePaths(m_ActiveEntry, m_aDemoStoragePath, sizeof(m_aDemoStoragePath), m_aGhostStoragePath, sizeof(m_aGhostStoragePath));

	// 旧 demo 内容有效：搬运到新目录复用（目录已由 EnsureFolders 创建）
	{
		const char *pBaseName = strrchr(m_aDemoStoragePath, '/');
		pBaseName = pBaseName != nullptr ? pBaseName + 1 : m_aDemoStoragePath;
		char aOldNamedDemo[IO_MAX_PATH_LENGTH];
		str_format(aOldNamedDemo, sizeof(aOldNamedDemo), "%s/%s", LEGACY_DEMO_DIR, pBaseName);
		char aSafeDemo[256];
		MakeSafeCacheName(m_ActiveEntry.m_Demo.c_str(), aSafeDemo, sizeof(aSafeDemo));
		char aLegacyNamedDemo[IO_MAX_PATH_LENGTH];
		str_format(aLegacyNamedDemo, sizeof(aLegacyNamedDemo), "%s/%s_%lld.demo", LEGACY_DEMO_DIR, aSafeDemo, (long long)DemoModifiedAt);
		MigrateLegacyDemos(Storage(), m_aDemoStoragePath, aOldNamedDemo, aLegacyNamedDemo);
	}
	// 仅下载回放：不复用已有影子，也不进入解析。否则“下载”会被影子缓存或
	// 前缀去重截走，页面却仍显示未下载，玩家看到的就是“点了没反应”。
	if(m_PendingMode == EPendingMode::DEMO_ONLY)
	{
		EnsureDemoOnlyReady();
		return;
	}

	char aRankGhostDir[IO_MAX_PATH_LENGTH];
	str_format(aRankGhostDir, sizeof(aRankGhostDir), "%s/%s", RANK1_ROOT, GHOST_SUBDIR);

	// 去重：同一 (地图, 名次, 类型, 成绩) 在 manifest 里可能有多条记录（uuid 不同），
	// 这里复用已有缓存中最新的一份，避免同一内容被反复下载、影子列表出现重复项；
	// 顺带清理旧命名的损坏 ghost 缓存与重复文件。
	{
		char aCachePrefix[128];
		EntryCachePrefix(m_ActiveEntry, aCachePrefix, sizeof(aCachePrefix));
		ReuseEntryCachePath(Storage(), aRankGhostDir, aCachePrefix, ".gho", m_aGhostStoragePath, sizeof(m_aGhostStoragePath));
		ReuseEntryCachePath(Storage(), DEMO_CACHE_DIR, aCachePrefix, ".demo", m_aDemoStoragePath, sizeof(m_aDemoStoragePath));
		PruneRankGhostCaches(Storage(), aRankGhostDir, DEMO_CACHE_DIR);
	}

	// 已生成过 ghost：直接加载（多轨目录优先于单文件）
	{
		char aDemoPathProbe[IO_MAX_PATH_LENGTH];
		char aGhostPathProbe[IO_MAX_PATH_LENGTH];
		BuildEntryCachePaths(m_ActiveEntry, aDemoPathProbe, sizeof(aDemoPathProbe), aGhostPathProbe, sizeof(aGhostPathProbe));
		char aGroupDir[IO_MAX_PATH_LENGTH];
		str_copy(aGroupDir, aGhostPathProbe, sizeof(aGroupDir));
		const size_t Len = str_length(aGroupDir);
		if(Len > 4 && str_endswith(aGroupDir, ".gho"))
			aGroupDir[Len - 4] = '\0';
		const bool HasGroup = Storage()->FolderExists(aGroupDir, IStorage::TYPE_SAVE) && !ListCacheFiles(Storage(), aGroupDir, ".gho").empty();
		const bool HasFile = Storage()->FileExists(aGhostPathProbe, IStorage::TYPE_SAVE);
		if(HasGroup || HasFile)
		{
			if(LoadGhostTarget())
			{
				NotifyLoaded(m_ActiveEntry.m_Names.c_str(), m_ActiveEntry.m_Time.c_str());
				// 旧版本缓存没有查看时间线：后台重放一次回放补齐
				if(!ViewTimelineCached())
					KickViewTimelineRebuild();
				return;
			}
			if(Client()->GetCurrentMap()[0] != '\0' && str_comp_nocase(Client()->GetCurrentMap(), m_ActiveEntry.m_Map.c_str()) == 0)
			{
				// 地图已加载仍加载失败：缓存损坏，删掉后从回放重建
				RemoveGhostCacheFiles(Storage(), m_aGhostStoragePath);
				if(Storage()->FileExists(m_aDemoStoragePath, IStorage::TYPE_SAVE))
					StartParse();
				else
					StartDemoFetch();
			}
			else
			{
				m_RetryLoadPending = true;
				m_RetryLoadDeadline = time_get() + time_freq() * 30;
				m_RetryLoadNextAttempt = time_get() + time_freq() / 2;
				// 等待地图属于终态：后续重试按 m_ActiveEntry 的地图名匹配
				ClearPendingRequest();
				m_PendingNotify = Localize("Rank ghost: replay converted, waiting for the map to load ...");
			}
			return;
		}
	}

	// 已下载过回放：直接解析
	if(Storage()->FileExists(m_aDemoStoragePath, IStorage::TYPE_SAVE))
	{
		StartParse();
		return;
	}

	StartDemoFetch();
}

// 仅下载回放：把选中的这一场 demo 确保到条目的精确缓存路径。
// 不走影子加载/解析，也不复用其它 uuid 的同图回放，保证页面状态与实际文件一致。
void CRankGhost::EnsureDemoOnlyReady()
{
	if(Storage()->FileExists(m_aDemoStoragePath, IStorage::TYPE_SAVE))
	{
		// 缓存可能来自被中断的下载或旧版本缺陷：无效文件按未缓存处理，重新下载。
		// 只判断存在就报成功，会让页面显示已下载、点播放却读不出回放。
		if(ValidateOrUnpackDemo(Storage(), m_aDemoStoragePath))
		{
			m_Stage = EStage::IDLE;
			ClearPendingRequest();
			char aBuf[IO_MAX_PATH_LENGTH + 128];
			str_format(aBuf, sizeof(aBuf), Localize("Rank 1 demo already cached under 'Rank 1 replays': %s"), m_aDemoStoragePath);
			Echo(aBuf);
			qm_ui::Toast(EQmToastKind::SUCCESS, aBuf);
			log_info("rank_ghost", "demo already cached: '%s'", m_aDemoStoragePath);
			return;
		}
		Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
		log_info("rank_ghost", "cached demo is not a valid demo, re-downloading: '%s'", m_aDemoStoragePath);
	}
	StartDemoFetch();
}

void CRankGhost::OnGhostsUnloaded()
{
	// 引擎侧已清空全部槽位，这里只同步本地记录
	m_vLoadedSlots.clear();
	m_vLoadedGhostPaths.clear();
	m_aLoadingGroupPrefix[0] = '\0';
	m_ViewMode = false;
	m_LastAlignedRaceTick = -1;
}

void CRankGhost::OnGhostLoaded(const char *pStoragePath, int Slot)
{
	char aRankGhostPrefix[IO_MAX_PATH_LENGTH];
	str_format(aRankGhostPrefix, sizeof(aRankGhostPrefix), "%s/%s/", RANK1_ROOT, GHOST_SUBDIR);
	if(Slot < 0 || pStoragePath == nullptr || !str_startswith(pStoragePath, aRankGhostPrefix))
		return;
	// 只登记本次任务加载的影子：单文件必须精确匹配，组内任意轨迹按前缀匹配
	const bool IsGroupMember = m_aLoadingGroupPrefix[0] != '\0' && str_startswith(pStoragePath, m_aLoadingGroupPrefix);
	const bool IsSingleTarget = str_comp(pStoragePath, m_aGhostStoragePath) == 0;
	if(!IsGroupMember && !IsSingleTarget)
		return;
	if(std::find(m_vLoadedSlots.begin(), m_vLoadedSlots.end(), Slot) != m_vLoadedSlots.end())
		return;
	RegisterLoadedGhost(pStoragePath, Slot);
}

void CRankGhost::OnGhostUnloaded(int Slot)
{
	for(size_t i = 0; i < m_vLoadedSlots.size(); i++)
	{
		if(m_vLoadedSlots[i] != Slot)
			continue;
		if((int)i < m_ViewSelected)
			--m_ViewSelected;
		m_ViewMembers.Remove(i);
		m_vLoadedSlots.erase(m_vLoadedSlots.begin() + i);
		m_vLoadedGhostPaths.erase(m_vLoadedGhostPaths.begin() + i);
		break;
	}
	if(m_vLoadedSlots.empty())
	{
		m_ViewMode = false;
		GameClient()->m_Ghost.StopManual();
		GameClient()->m_Spectator.OnReset();
		m_ViewSelected = 0;
	}
	else if(m_ViewSelected >= (int)m_vLoadedSlots.size())
		m_ViewSelected = 0;
	m_LastAlignedRaceTick = -1;
}

// 按条目加载缓存：多轨目录（<base>/）优先，其次单文件（<base>.gho）
bool CRankGhost::LoadGhostTarget()
{
	char aDemoPath[IO_MAX_PATH_LENGTH];
	BuildEntryCachePaths(m_ActiveEntry, aDemoPath, sizeof(aDemoPath), m_aGhostStoragePath, sizeof(m_aGhostStoragePath));

	char aGroupDir[IO_MAX_PATH_LENGTH];
	str_copy(aGroupDir, m_aGhostStoragePath, sizeof(aGroupDir));
	const size_t Len = str_length(aGroupDir);
	if(Len > 4 && str_endswith(aGroupDir, ".gho"))
		aGroupDir[Len - 4] = '\0';

	if(Storage()->FolderExists(aGroupDir, IStorage::TYPE_SAVE) && !ListCacheFiles(Storage(), aGroupDir, ".gho").empty())
	{
		if(!LoadGhostGroup(aGroupDir))
			return false;
	}
	else if(Storage()->FileExists(m_aGhostStoragePath, IStorage::TYPE_SAVE))
	{
		if(!LoadGhostFile(m_aGhostStoragePath))
			return false;
	}
	else
		return false;

	// 查看模式时间线（事件/开关状态）与影子一起加载
	LoadViewTimeline();
	return true;
}

bool CRankGhost::LoadGhostFile(const char *pStoragePath)
{
	UnloadGhost();
	m_aLoadingGroupPrefix[0] = '\0';
	const int Slot = GameClient()->m_Ghost.Load(pStoragePath);
	if(Slot < 0)
		return false;
	RegisterLoadedGhost(pStoragePath, Slot);
	AfterGhostLoaded();
	return true;
}

bool CRankGhost::LoadGhostGroup(const char *pDir)
{
	UnloadGhost();
	str_format(m_aLoadingGroupPrefix, sizeof(m_aLoadingGroupPrefix), "%s/", pDir);

	std::vector<SCacheNameTime> vFiles = ListCacheFiles(Storage(), pDir, ".gho");
	std::sort(vFiles.begin(), vFiles.end(), [](const SCacheNameTime &A, const SCacheNameTime &B) {
		return str_comp(A.m_Name.c_str(), B.m_Name.c_str()) < 0;
	});

	const int FreeSlots = GameClient()->m_Ghost.FreeSlots();
	int Loaded = 0;
	int Failed = 0;
	bool Truncated = false;
	for(const SCacheNameTime &File : vFiles)
	{
		if(Loaded >= FreeSlots)
		{
			Truncated = true;
			break;
		}
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s", pDir, File.m_Name.c_str());
		const int Slot = GameClient()->m_Ghost.Load(aPath);
		if(Slot < 0)
		{
			Failed++;
			continue;
		}
		RegisterLoadedGhost(aPath, Slot);
		Loaded++;
	}
	if(Loaded == 0)
	{
		m_aLoadingGroupPrefix[0] = '\0';
		return false;
	}
	if(Truncated)
		Echo(Localize("Rank ghost: some tees were not loaded (not enough ghost slots)"));
	if(Failed > 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Rank ghost: %d tees could not be loaded"), Failed);
		Echo(aBuf);
	}
	log_info("rank_ghost", "ghost group loaded: %d tracks from '%s'", Loaded, pDir);
	AfterGhostLoaded();
	return true;
}

void CRankGhost::RegisterLoadedGhost(const char *pStoragePath, int Slot)
{
	m_vLoadedSlots.push_back(Slot);
	m_vLoadedGhostPaths.push_back(pStoragePath);
	// 新加载的影子需要在之后的帧里按玩家当前进度重新对齐
	m_LastAlignedRaceTick = -1;
	RefreshGhostList();
}

// 加载成功后的公共收尾：按需进入查看模式
void CRankGhost::AfterGhostLoaded()
{
	str_copy(m_aLoadedReplayFilename, m_aDemoStoragePath);
	if(m_PendingView)
	{
		m_PendingView = false;
		EnterViewMode();
	}
}

IDemoPlayer::CInfo CRankGhost::PlaybackInfo() const
{
	IDemoPlayer::CInfo Info = m_ViewPlaybackMetadata;
	const CGhost &Ghost = GameClient()->m_Ghost;
	Info.m_Paused = !Ghost.ManualPlaying();
	Info.m_Speed = Ghost.ManualSpeed();
	Info.m_LastTick = Info.m_FirstTick + maximum(0, Ghost.ManualEndTick());
	Info.m_CurrentTick = Info.m_FirstTick + std::clamp(Ghost.ManualPlaybackTick(), 0, maximum(0, Ghost.ManualEndTick()));
	return Info;
}

int CRankGhost::PlaybackTickSpeed() const { return Client()->GameTickSpeed(); }
IDemoPlayer *CRankGhost::PlaybackMetadataReader() const
{
	// 练习 /pg 组没有源 demo，不能把在线 demo player 的元数据借来回放；
	// 查看模式自己的 m_ViewPlaybackMetadata 已足够驱动进度条与播放头。
	return m_aLoadedReplayFilename[0] == '\0' ? nullptr : DemoPlayer();
}
void CRankGhost::PlaybackSeek(int Tick)
{
	if(IsViewModeActive())
		GameClient()->m_Ghost.ManualSeek(Tick - m_ViewPlaybackMetadata.m_FirstTick);
}
int CRankGhost::PlaybackAdjacentTick(int Tick, IDemoPlayer::ETickOffset Offset) const
{
	const int First = m_ViewPlaybackMetadata.m_FirstTick;
	return First + GameClient()->m_Ghost.ManualAdjacentTick(Tick - First, Offset);
}

void CRankGhost::PlaybackSetPlaying(bool Playing)
{
	if(IsViewModeActive())
		GameClient()->m_Ghost.ManualSetPlaying(Playing);
}
void CRankGhost::PlaybackSetSpeed(float Speed)
{
	if(IsViewModeActive())
		GameClient()->m_Ghost.ManualSetSpeed(Speed);
}

bool CRankGhost::ExportViewCut(const char *pDestination, const std::vector<SDemoSliceSegment> &vSegments, DEMOFUNC_FILTER pFilter, void *pUser)
{
	if(!IsViewModeActive() || m_aViewPlaybackFilename[0] == '\0')
		return false;
	auto Delta = CSnapshotDelta::New();
	auto DeltaSixup = CSnapshotDelta::New();
	CDemoEditor Editor;
	Editor.Init(&*Delta, &*DeltaSixup, Console(), Storage());
	// 临时 demo 读取器会重置全局剪辑标记；保留当前在线会话的编辑状态。
	const int SliceBegin = g_Config.m_ClDemoSliceBegin;
	const int SliceEnd = g_Config.m_ClDemoSliceEnd;
	const bool Result = Editor.Slice(m_aViewPlaybackFilename, pDestination, vSegments, pFilter, pUser);
	g_Config.m_ClDemoSliceBegin = SliceBegin;
	g_Config.m_ClDemoSliceEnd = SliceEnd;
	return Result;
}

bool CRankGhost::ViewMemberRenderInfo(int Index, CTeeRenderInfo *pInfo) const
{
	return Index >= 0 && Index < (int)m_vLoadedSlots.size() && GameClient()->m_Ghost.GetGhostRenderInfo(m_vLoadedSlots[Index], pInfo);
}

bool CRankGhost::ViewMemberPosition(int Index, vec2 *pPosition) const
{
	return IsViewModeActive() && Index >= 0 && Index < (int)m_vLoadedSlots.size() && GameClient()->m_Ghost.GetManualRenderPos(m_vLoadedSlots[Index], pPosition);
}

void CRankGhost::UnloadGhost()
{
	m_LastAlignedRaceTick = -1;
	m_ViewMode = false;
	GameClient()->m_LocalWorld.EndViewSession();
	// 注意：这里不能清 m_PendingView —— 加载管线（LoadGhostFile/LoadGhostGroup）
	// 第一步就会调用本函数卸载旧影子，而 m_PendingView 正是"加载完成后进入查看
	// 模式"的请求标记；显式卸载/失败路径各自负责清除它。
	for(int Slot : m_vLoadedSlots)
		GameClient()->m_Ghost.Unload(Slot);
	m_vLoadedSlots.clear();
	m_vLoadedGhostPaths.clear();
	m_aLoadingGroupPrefix[0] = '\0';
	// 查看时间线只对应当前影子，卸载即失效
	m_vViewEvents.clear();
	m_vViewSwitchStates.clear();
	m_vViewMessages.clear();
	m_ViewEventCursor = 0;
	m_ViewMessageCursor = 0;
	m_ViewAppliedSwitch = -1;
	m_ViewLastTick = -1;
	// 视角倍率随影子一起失效：新影子按自己的包围盒重新取景
	m_ViewZoomPersonal = 0.0f;
}

// 刷新 Ghost 页列表，并把我们占用的槽位写回列表项，
// 否则玩家打开 Ghost 页会看到影子文件显示为“未激活”。
void CRankGhost::RefreshGhostList()
{
	GameClient()->m_Menus.GhostlistPopulate();
	for(CMenus::CGhostItem &Item : GameClient()->m_Menus.m_vGhosts)
	{
		for(size_t i = 0; i < m_vLoadedGhostPaths.size(); i++)
		{
			if(str_comp(Item.m_aFilename, m_vLoadedGhostPaths[i].c_str()) == 0)
			{
				Item.m_Slot = m_vLoadedSlots[i];
				break;
			}
		}
	}
}

// CGhost 只在玩家从起点线前跨到线后的瞬间开始播放影子。
// 如果影子加载时玩家已经在跑图（或开始新一轮），这里主动把影子的播放起点
// 对齐到当前 run 的开始 tick，让影子立刻按当前进度出现在画面里。
void CRankGhost::AlignToCurrentRun()
{
	if(m_ViewMode)
	{
		// 查看模式使用独立时间线，不做跑图对齐
		m_LastAlignedRaceTick = -1;
		return;
	}
	if(m_vLoadedSlots.empty() || Client()->State() != IClient::STATE_ONLINE)
	{
		m_LastAlignedRaceTick = -1;
		return;
	}
	if(!GameClient()->m_GameInfo.m_Race || !GameClient()->m_Snap.m_pGameInfoObj || GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		m_LastAlignedRaceTick = -1;
		return;
	}

	const int RaceTick = GameClient()->LastRaceTick();
	if(RaceTick < 0)
	{
		// 还没出发：等玩家跨过起点线时由 CGhost 自行开始
		m_LastAlignedRaceTick = -1;
		return;
	}
	if(RaceTick == m_LastAlignedRaceTick)
		return;

	GameClient()->m_Ghost.StartRender(RaceTick);
	m_LastAlignedRaceTick = RaceTick;
}

void CRankGhost::NotifyLoaded(const char *pOwner, const char *pTimeText)
{
	char aBuf[320];
	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->LastRaceTick() >= 0)
	{
		// 玩家已在跑图中：影子会立即按当前进度对齐播放
		str_format(aBuf, sizeof(aBuf), Localize("Rank ghost loaded: %s, %s s. It follows your current run."),
			pOwner, pTimeText);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), Localize("Rank ghost loaded: %s, %s s. It plays when you cross the start line."),
			pOwner, pTimeText);
	}
	Echo(aBuf);
	ClearPendingRequest();
}

void CRankGhost::RecordViewMessage(int RelTick, EViewMessageType Type, const int *pData, int NumInts)
{
	SViewMessage Message;
	Message.m_RelTick = RelTick;
	Message.m_Type = Type;
	mem_zero(Message.m_aData, sizeof(Message.m_aData));
	if(NumInts > 0)
		mem_copy(Message.m_aData, pData, (size_t)minimum(NumInts, (int)std::size(Message.m_aData)) * sizeof(int));
	m_vViewMessages.push_back(Message);
}

const CRankGhost::SViewMessage *CRankGhost::FindLatestViewMessage(EViewMessageType Type, int RelTick) const
{
	// 消息按 RelTick 非递减写入：从当前位置向前找第一条同类型且不晚于播放头的记录
	for(auto It = m_vViewMessages.rbegin(); It != m_vViewMessages.rend(); ++It)
	{
		if(It->m_Type != Type)
			continue;
		if(It->m_RelTick <= RelTick)
			return &*It;
	}
	return nullptr;
}
