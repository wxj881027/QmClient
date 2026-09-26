#include "rank_ghost.h"

#include "rank_demo_manifest.h"

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

#include <game/client/components/effects.h>
#include <game/client/components/ghost.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/modes.h>
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

namespace
{
	constexpr int64_t PARSE_TIME_BUDGET_MS = 5;
	constexpr int MIN_PATH_TICKS = 25;
	constexpr size_t MAX_MANIFEST_BYTES = 16 * 1024 * 1024;
	constexpr size_t MAX_DEMO_BYTES = 128 * 1024 * 1024;
	constexpr size_t MAX_UNPACKED_DEMO_BYTES = 256 * 1024 * 1024;
	constexpr int64_t MANIFEST_CACHE_TTL_SECONDS = 300;
	constexpr const char *USER_AGENT = "QmClient (https://github.com/wxj881027/QmClient)";

	// rust::Box 没有默认构造，必须在构造函数里创建
	rust::Box<CSnapshotDelta> CreateSnapshotDelta();
	rust::Box<CSnapshotDelta> CreateSnapshotDeltaSixup();

	// 单个参与者的轨迹与外观（主选手与多轨模式下的其他 Tee 共用）
	struct STrackContext
	{
		std::vector<CGhostCharacter> m_vPath;
		CGhostSkin m_Skin{};
		char m_aOwner[MAX_NAME_LENGTH] = "";
		bool m_FoundClientInfo = false;
		bool m_FoundCharacter = false;
	};

	// demo 解析期间的状态，独立于主客户端，避免打断正常游戏
	struct SParseContext
	{
		rust::Box<CSnapshotDelta> m_pDelta;
		rust::Box<CSnapshotDelta> m_pDeltaSixup;
		std::unique_ptr<CDemoPlayer> m_pPlayer;
		CNetObjHandler m_NetObjHandler;
		// 0.7 回放的消息用独立协议解包（对象类型编号与 0.6 不同）
		protocol7::CNetObjHandler m_NetObjHandler7;

		// 按 cid 分轨：单轨模式只有主选手一条，多轨模式包含回放里所有 Tee
		std::map<int, STrackContext> m_Tracks;
		char m_aMapName[64] = "";
		SHA256_DIGEST m_MapSha256{};

		int m_RunStart = 0;
		int m_RunEnd = 0;
		// 官方 watch 回放不一定带 race 起止标记：缺标记时按"整段回放就是这一轮"处理
		bool m_HasRunMarkers = true;
		// 无标记时的实际结束 tick（解析过程中观察到的最大 tick）
		int m_LastSeenTick = 0;
		int m_Cid = 0;
		// 观战者判定缓存（cid → 是否观战）
		std::map<int, bool> m_aSpectatorCache;
		// 是否在解析中看到过 run 终点之后的快照，用于识别被截断的回放
		bool m_ReachedRunEnd = false;

		static constexpr int MAX_TRACKS = 64;

		SParseContext();
	};

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

	struct SCacheNameTime
	{
		std::string m_Name;
		time_t m_Modified = 0;
	};

	struct SCacheListContext
	{
		const char *m_pSuffix;
		std::vector<SCacheNameTime> m_vFiles;
	};

	int CacheListCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
	{
		(void)StorageType;
		SCacheListContext *pCtx = (SCacheListContext *)pUser;
		if(IsDir || !str_endswith(pInfo->m_pName, pCtx->m_pSuffix))
			return 0;
		pCtx->m_vFiles.push_back({pInfo->m_pName, pInfo->m_TimeModified});
		return 0;
	}

	std::vector<SCacheNameTime> ListCacheFiles(IStorage *pStorage, const char *pDir, const char *pSuffix)
	{
		SCacheListContext Context;
		Context.m_pSuffix = pSuffix;
		pStorage->ListDirectoryInfo(IStorage::TYPE_SAVE, pDir, CacheListCallback, &Context);
		return std::move(Context.m_vFiles);
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

	// 查找 demo 浏览器下载按钮落盘的该图 rank1 回放（命名 <安全地图名>_rank1_<ts>_<demo>.demo）；
	// 命中说明同一份内容已从浏览器下载过，可复用避免重复下载
	bool FindBrowserDownloadedDemo(IStorage *pStorage, const qmclient::rank_demo::SEntry &Entry, char *pPath, size_t PathSize)
	{
		char aSafeMap[64];
		str_copy(aSafeMap, Entry.m_Map.c_str(), sizeof(aSafeMap));
		str_sanitize_filename(aSafeMap);
		char aPrefix[96];
		str_format(aPrefix, sizeof(aPrefix), "%s_rank1_", aSafeMap);
		const std::vector<SCacheNameTime> vFiles = ListCacheFiles(pStorage, "demos", ".demo");
		const SCacheNameTime *pBest = nullptr;
		for(const SCacheNameTime &File : vFiles)
		{
			if(!str_startswith(File.m_Name.c_str(), aPrefix))
				continue;
			if(pBest == nullptr || File.m_Modified > pBest->m_Modified)
				pBest = &File;
		}
		if(pBest == nullptr)
			return false;
		str_format(pPath, PathSize, "demos/%s", pBest->m_Name.c_str());
		return true;
	}

	// 同存储文件流式复制（固定 4 MiB 缓冲，demo 可达数十 MB，不整块进内存）
	bool CopyStorageFile(IStorage *pStorage, const char *pFrom, const char *pTo)
	{
		if(str_comp(pFrom, pTo) == 0)
			return true;
		char aFromPath[IO_MAX_PATH_LENGTH];
		char aToPath[IO_MAX_PATH_LENGTH];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, pFrom, aFromPath, sizeof(aFromPath));
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, pTo, aToPath, sizeof(aToPath));
		if(aFromPath[0] == '\0' || aToPath[0] == '\0')
			return false;
		IOHANDLE pInput = io_open(aFromPath, IOFLAG_READ);
		if(pInput == nullptr)
			return false;
		IOHANDLE pOutput = io_open(aToPath, IOFLAG_WRITE);
		if(pOutput == nullptr)
		{
			io_close(pInput);
			return false;
		}
		std::vector<unsigned char> Buffer(4 * 1024 * 1024);
		bool Ok = true;
		unsigned Read;
		while((Read = io_read(pInput, Buffer.data(), (unsigned)Buffer.size())) > 0)
		{
			if(io_write(pOutput, Buffer.data(), Read) != Read)
			{
				Ok = false;
				break;
			}
		}
		io_close(pInput);
		io_close(pOutput);
		if(!Ok)
			pStorage->RemoveFile(pTo, IStorage::TYPE_SAVE);
		return Ok;
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

	// 按 CGhostRecorder 的格式写 ghost 文件。
	// 不使用 recorder 类是因为它的 Kernel() 依赖接口注册时才注入的指针，
	// 组件内直接构造的实例无法取得 storage。
	class CGhostFileWriter
	{
		IOHANDLE m_File = nullptr;
		alignas(uint32_t) char m_aBuffer[MAX_CHUNK_SIZE] = {};
		alignas(uint32_t) char m_aBufferTemp[MAX_CHUNK_SIZE] = {};
		char *m_pBufferPos = m_aBuffer;
		int m_BufferNumItems = 0;
		std::optional<CGhostItem> m_LastItem;
		bool m_Failed = false;

		void ResetBuffer()
		{
			m_pBufferPos = m_aBuffer;
			m_BufferNumItems = 0;
		}

		bool FlushChunk()
		{
			const int Size = (int)(m_pBufferPos - m_aBuffer);
			if(Size == 0 || m_BufferNumItems == 0 || !m_LastItem.has_value())
			{
				ResetBuffer();
				return !m_Failed;
			}

			int CompressedSize = CVariableInt::Compress(m_aBuffer, Size, m_aBufferTemp, sizeof(m_aBufferTemp));
			if(CompressedSize < 0)
			{
				m_Failed = true;
				ResetBuffer();
				m_LastItem = std::nullopt;
				return false;
			}
			CompressedSize = CNetBase::Compress(m_aBufferTemp, CompressedSize, m_aBuffer, sizeof(m_aBuffer));
			if(CompressedSize < 0)
			{
				m_Failed = true;
				ResetBuffer();
				m_LastItem = std::nullopt;
				return false;
			}

			unsigned char aChunkHeader[4];
			aChunkHeader[0] = m_LastItem.value().m_Type & 0xff;
			aChunkHeader[1] = m_BufferNumItems & 0xff;
			aChunkHeader[2] = (CompressedSize >> 8) & 0xff;
			aChunkHeader[3] = CompressedSize & 0xff;
			if(io_write(m_File, aChunkHeader, sizeof(aChunkHeader)) != sizeof(aChunkHeader) || io_write(m_File, m_aBuffer, CompressedSize) != CompressedSize)
			{
				m_Failed = true;
				ResetBuffer();
				m_LastItem = std::nullopt;
				return false;
			}

			m_LastItem = std::nullopt;
			ResetBuffer();
			return true;
		}

	public:
		bool Open(IStorage *pStorage, const char *pPath, const char *pMap, const SHA256_DIGEST &MapSha256, const char *pOwner, int NumTicks, int TimeMs)
		{
			m_File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(m_File == nullptr)
				return false;

			CGhostHeader Header;
			mem_zero(&Header, sizeof(Header));
			mem_copy(Header.m_aMarker, "TWGHOST", 7);
			Header.m_Version = 6;
			str_copy(Header.m_aOwner, pOwner);
			str_copy(Header.m_aMap, pMap);
			Header.m_MapSha256 = MapSha256;
			uint_to_bytes_be(Header.m_aNumTicks, NumTicks);
			uint_to_bytes_be(Header.m_aTime, TimeMs);
			if(io_write(m_File, &Header, sizeof(Header)) != sizeof(Header))
			{
				io_close(m_File);
				m_File = nullptr;
				return false;
			}

			m_LastItem = std::nullopt;
			m_Failed = false;
			ResetBuffer();
			return true;
		}

		bool WriteData(int Type, const void *pData, size_t Size)
		{
			if(m_File == nullptr || m_Failed || Size == 0 || Size > MAX_ITEM_SIZE || Size % sizeof(uint32_t) != 0)
				return false;
			if((size_t)(sizeof(m_aBuffer) - (m_pBufferPos - m_aBuffer)) < Size)
			{
				if(!FlushChunk())
					return false;
			}

			CGhostItem Item;
			mem_copy(Item.m_aData, pData, Size);
			Item.m_Size = Size;
			Item.m_Type = Type;
			if(m_LastItem.has_value() && m_LastItem.value().m_Type == Item.m_Type)
			{
				// 同类型连续项写差分
				const uint32_t *pPast = (const uint32_t *)m_LastItem.value().m_aData;
				const uint32_t *pCurrent = (const uint32_t *)Item.m_aData;
				uint32_t *pOut = (uint32_t *)m_pBufferPos;
				for(size_t i = 0; i < Size / sizeof(uint32_t); i++)
					pOut[i] = pCurrent[i] - pPast[i];
			}
			else
			{
				if(!FlushChunk())
					return false;
				mem_copy(m_pBufferPos, Item.m_aData, Size);
			}

			m_LastItem = Item;
			m_pBufferPos += Size;
			m_BufferNumItems++;
			if(m_BufferNumItems >= NUM_ITEMS_PER_CHUNK)
				FlushChunk();
			return !m_Failed;
		}

		bool Close()
		{
			if(m_File == nullptr)
				return !m_Failed;
			const bool Flushed = FlushChunk();
			io_close(m_File);
			m_File = nullptr;
			return Flushed && !m_Failed;
		}
	};

	bool IsGzipData(const unsigned char *pData, size_t Size)
	{
		return Size >= 2 && pData[0] == 0x1f && pData[1] == 0x8b;
	}

	bool HasDemoMagic(const unsigned char *pData, size_t Size)
	{
		return Size >= 7 && mem_comp(pData, "TWDEMO\0", 7) == 0;
	}

	// 读取文件头，判断是否为有效的 demo（服务端通常已解压，个别缓存路径可能返回原始 gzip 字节）
	bool ValidateOrUnpackDemo(class IStorage *pStorage, const char *pStoragePath)
	{
		void *pData = nullptr;
		unsigned DataSize = 0;
		if(!pStorage->ReadFile(pStoragePath, IStorage::TYPE_SAVE, &pData, &DataSize) || pData == nullptr || DataSize == 0)
		{
			free(pData);
			return false;
		}

		if(!IsGzipData((const unsigned char *)pData, DataSize))
		{
			const bool Valid = HasDemoMagic((const unsigned char *)pData, DataSize);
			free(pData);
			return Valid;
		}

		// gzip：解压到内存后写回
		z_stream Stream = {};
		Stream.next_in = (Bytef *)pData;
		Stream.avail_in = DataSize;
		if(inflateInit2(&Stream, 15 + 32) != Z_OK)
		{
			free(pData);
			return false;
		}

		std::vector<unsigned char> Output;
		Output.resize(std::min<size_t>(std::max<size_t>(DataSize * 4, 1024 * 1024), MAX_UNPACKED_DEMO_BYTES));
		size_t OutputLength = 0;
		int Result = Z_OK;
		while(Result == Z_OK)
		{
			if(OutputLength == Output.size())
			{
				if(Output.size() >= MAX_UNPACKED_DEMO_BYTES)
				{
					inflateEnd(&Stream);
					free(pData);
					return false;
				}
				Output.resize(std::min(Output.size() * 2, MAX_UNPACKED_DEMO_BYTES));
			}
			Stream.next_out = Output.data() + OutputLength;
			Stream.avail_out = (uInt)std::min<size_t>(Output.size() - OutputLength, 0x40000000);
			Result = inflate(&Stream, Z_NO_FLUSH);
			OutputLength = Output.size() - Stream.avail_out;
		}
		inflateEnd(&Stream);
		free(pData);

		if(Result != Z_STREAM_END || OutputLength > MAX_UNPACKED_DEMO_BYTES || !HasDemoMagic(Output.data(), OutputLength))
			return false;

		IOHANDLE File = pStorage->OpenFile(pStoragePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool WriteOk = io_write(File, Output.data(), OutputLength) == OutputLength;
		io_close(File);
		return WriteOk;
	}

	int FindObjItemIndex(const CSnapshot *pSnapshot, int Type, int Id)
	{
		const int NumItems = pSnapshot->NumItems();
		for(int i = 0; i < NumItems; i++)
		{
			if(pSnapshot->GetItemType(i) == Type && pSnapshot->GetItem(i)->Id() == Id)
				return i;
		}
		return -1;
	}

	// 演示影子只需要位置等渲染信息，提取方式与 CGhost::GetGhostCharacter 保持一致
	void CopyGhostCharacter(CGhostCharacter &Out, const CNetObj_Character &Char, const CNetObj_DDNetCharacter *pDDNetChar, int GhostTick)
	{
		Out.m_X = Char.m_X;
		Out.m_Y = Char.m_Y;
		Out.m_VelX = Char.m_VelX;
		Out.m_VelY = 0;
		Out.m_Angle = Char.m_Angle;
		Out.m_Direction = Char.m_Direction;
		int Weapon = Char.m_Weapon;
		if(pDDNetChar != nullptr && pDDNetChar->m_FreezeEnd != 0)
			Weapon = WEAPON_NINJA;
		Out.m_Weapon = Weapon;
		Out.m_HookState = Char.m_HookState;
		Out.m_HookX = Char.m_HookX;
		Out.m_HookY = Char.m_HookY;
		Out.m_AttackTick = Char.m_AttackTick;
		Out.m_Tick = GhostTick;
	}

	// 尝试抓取某 cid 的皮肤与名字（ClientInfo 更新频率低，遇到就记下）
	void ExtractClientInfo(SParseContext &Parse, const CSnapshot *pSnapshot, int Cid, STrackContext &Track)
	{
		if(Track.m_FoundClientInfo)
			return;
		const int Index = FindObjItemIndex(pSnapshot, NETOBJTYPE_CLIENTINFO, Cid);
		if(Index < 0)
			return;

		CUnpacker Unpacker;
		Unpacker.Reset(pSnapshot->GetItem(Index)->Data(), pSnapshot->GetItemSize(Index));
		const void *pRaw = Parse.m_NetObjHandler.SecureUnpackObj(NETOBJTYPE_CLIENTINFO, &Unpacker);
		if(!pRaw)
			return;

		const CNetObj_ClientInfo *pInfo = (const CNetObj_ClientInfo *)pRaw;
		if(!IntsToStr(pInfo->m_aName, std::size(pInfo->m_aName), Track.m_aOwner, sizeof(Track.m_aOwner)))
			Track.m_aOwner[0] = '\0';
		mem_copy(Track.m_Skin.m_aSkin, pInfo->m_aSkin, sizeof(Track.m_Skin.m_aSkin));
		Track.m_Skin.m_UseCustomColor = pInfo->m_UseCustomColor;
		Track.m_Skin.m_ColorBody = pInfo->m_ColorBody;
		Track.m_Skin.m_ColorFeet = pInfo->m_ColorFeet;
		Track.m_FoundClientInfo = true;
	}

	// 解包某个 cid 的 Character（含 DDNet 扩展），返回是否成功。
	// SecureUnpackObj 返回的是处理器内部共享的暂存缓冲区，必须先完整拷贝再继续解包。
	bool UnpackCharacter(SParseContext &Parse, const CSnapshot *pSnapshot, int Cid, CNetObj_Character &OutChar, const CNetObj_DDNetCharacter *&pOutDDNet)
	{
		const int Index = FindObjItemIndex(pSnapshot, NETOBJTYPE_CHARACTER, Cid);
		if(Index < 0)
			return false;

		CUnpacker Unpacker;
		Unpacker.Reset(pSnapshot->GetItem(Index)->Data(), pSnapshot->GetItemSize(Index));
		const void *pRawChar = Parse.m_NetObjHandler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker);
		if(!pRawChar)
			return false;
		mem_copy(&OutChar, pRawChar, sizeof(OutChar));

		pOutDDNet = nullptr;
		const int ExtIndex = FindObjItemIndex(pSnapshot, NETOBJTYPE_DDNETCHARACTER, Cid);
		if(ExtIndex >= 0)
		{
			CUnpacker ExtUnpacker;
			ExtUnpacker.Reset(pSnapshot->GetItem(ExtIndex)->Data(), pSnapshot->GetItemSize(ExtIndex));
			pOutDDNet = (const CNetObj_DDNetCharacter *)Parse.m_NetObjHandler.SecureUnpackObj(NETOBJTYPE_DDNETCHARACTER, &ExtUnpacker);
		}
		return true;
	}

	// 判断 cid 是否观战者（结果按 cid 缓存），观战者不参与跑图
	bool IsSpectatorCid(SParseContext &Parse, const CSnapshot *pSnapshot, int Cid)
	{
		const auto CacheIt = Parse.m_aSpectatorCache.find(Cid);
		if(CacheIt != Parse.m_aSpectatorCache.end())
			return CacheIt->second;
		bool Spectator = true;
		const int Index = FindObjItemIndex(pSnapshot, NETOBJTYPE_PLAYERINFO, Cid);
		if(Index >= 0)
		{
			CUnpacker Unpacker;
			Unpacker.Reset(pSnapshot->GetItem(Index)->Data(), pSnapshot->GetItemSize(Index));
			const void *pRaw = Parse.m_NetObjHandler.SecureUnpackObj(NETOBJTYPE_PLAYERINFO, &Unpacker);
			if(pRaw)
				Spectator = ((const CNetObj_PlayerInfo *)pRaw)->m_Team == TEAM_SPECTATORS;
		}
		Parse.m_aSpectatorCache.emplace(Cid, Spectator);
		return Spectator;
	}

	void ExtractCharacter(SParseContext &Parse, const CSnapshot *pSnapshot, int Tick)
	{
		// 提取快照中所有 Character（solo 回放自然只有一条；teamrun 的相互影响都在各自轨迹里）
		const int NumItems = pSnapshot->NumItems();
		for(int i = 0; i < NumItems; i++)
		{
			if(pSnapshot->GetItemType(i) != NETOBJTYPE_CHARACTER)
				continue;
			const int Cid = pSnapshot->GetItem(i)->Id();
			auto TrackIt = Parse.m_Tracks.find(Cid);
			if(TrackIt == Parse.m_Tracks.end())
			{
				if((int)Parse.m_Tracks.size() >= SParseContext::MAX_TRACKS)
					continue;
				// 观战者不参与跑图，直接排除（避免静止 Tee 混入轨迹）
				if(IsSpectatorCid(Parse, pSnapshot, Cid))
					continue;
				TrackIt = Parse.m_Tracks.emplace(Cid, STrackContext{}).first;
				TrackIt->second.m_vPath.reserve((size_t)std::clamp(Parse.m_RunEnd - Parse.m_RunStart, 0, 60 * 60 * 50));
				ExtractClientInfo(Parse, pSnapshot, Cid, TrackIt->second);
			}
			STrackContext &Track = TrackIt->second;

			CNetObj_Character Char;
			const CNetObj_DDNetCharacter *pDDNetChar = nullptr;
			if(!UnpackCharacter(Parse, pSnapshot, Cid, Char, pDDNetChar))
				continue;
			ExtractClientInfo(Parse, pSnapshot, Cid, Track);

			CGhostCharacter GhostChar;
			CopyGhostCharacter(GhostChar, Char, pDDNetChar, Tick);
			Track.m_vPath.push_back(GhostChar);
			Track.m_FoundCharacter = true;
		}
	}

	// 与上游 demo_extract_chat 工具一致：snapshot delta 需要按对象类型设置静态大小
	rust::Box<CSnapshotDelta> CreateSnapshotDelta()
	{
		rust::Box<CSnapshotDelta> pResult = CSnapshotDelta::New();
		CNetObjHandler NetObjHandler;
		for(int i = 0; i < NUM_NETOBJTYPES; i++)
			pResult->SetStaticsize(i, NetObjHandler.GetObjSize(i));
		return pResult;
	}

	rust::Box<CSnapshotDelta> CreateSnapshotDeltaSixup()
	{
		rust::Box<CSnapshotDelta> pResult = CSnapshotDelta::New();
		protocol7::CNetObjHandler NetObjHandler7;
		// HACK: 只设置 0.7 首个版本存在的对象，避免新对象破坏 snapshot delta
		static const int OLD_NUM_NETOBJTYPES = 23;
		for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
			pResult->SetStaticsize(i, NetObjHandler7.GetObjSize(i));
		return pResult;
	}

	SParseContext::SParseContext() :
		m_pDelta(CreateSnapshotDelta()),
		m_pDeltaSixup(CreateSnapshotDeltaSixup())
	{
	}
} // namespace

// 头文件中前向声明的解析状态
struct CRankGhost::SParseState
{
	SParseContext m_Context;
};

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

void CRankGhost::Fail(const char *pMessage)
{
	log_error("rank_ghost", "%s", pMessage != nullptr ? pMessage : "unknown error");
	Echo(pMessage);
	// 任务失败后不应再自动进入查看模式
	m_PendingView = false;
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

std::vector<qmclient::rank_demo::SEntry> CRankGhost::CollectSearchEntries(const char *pQuery, int Rank, int Limit) const
{
	std::vector<SEntry> Out;
	if(!m_ManifestLoaded || pQuery == nullptr || pQuery[0] == '\0')
		return Out;
	std::set<std::string> SeenDemos;
	for(const SEntry &Entry : m_vEntries)
	{
		if(Entry.m_Rank != Rank)
			continue;
		if(str_find_nocase(Entry.m_Map.c_str(), pQuery) == nullptr)
			continue;
		if(!SeenDemos.insert(Entry.m_Demo).second)
			continue;
		Out.push_back(Entry);
		if((int)Out.size() >= Limit)
			break;
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

bool CRankGhost::FindCachedRankDemo(const char *pMap, int Rank, char *pDemoPath, size_t DemoPathSize) const
{
	char aSafeMap[64];
	str_copy(aSafeMap, pMap, sizeof(aSafeMap));
	str_sanitize_filename(aSafeMap);
	char aPrefix[96];
	str_format(aPrefix, sizeof(aPrefix), "%s_rank%d_", aSafeMap, Rank);
	const std::vector<SCacheNameTime> vFiles = ListCacheFiles(Storage(), DEMO_CACHE_DIR, ".demo");
	const SCacheNameTime *pBest = nullptr;
	for(const SCacheNameTime &File : vFiles)
	{
		if(!str_startswith(File.m_Name.c_str(), aPrefix))
			continue;
		if(pBest == nullptr || File.m_Modified > pBest->m_Modified)
			pBest = &File;
	}
	if(pBest == nullptr)
		return false;
	str_format(pDemoPath, DemoPathSize, "%s/%s", DEMO_CACHE_DIR, pBest->m_Name.c_str());
	return true;
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

void CRankGhost::StartManifestFetch()
{
	if(Http() == nullptr)
	{
		Fail(Localize("Rank ghost: HTTP is not available"));
		return;
	}

	m_pManifestRequest = HttpGet(MANIFEST_URL);
	m_pManifestRequest->MaxResponseSize(MAX_MANIFEST_BYTES);
	m_pManifestRequest->Timeout(CTimeout{5000, 30000, 100, 5});
	m_pManifestRequest->LogProgress(HTTPLOG::FAILURE);
	m_pManifestRequest->FailOnErrorStatus(false);
	m_pManifestRequest->HeaderString("User-Agent", USER_AGENT);
	Http()->Run(m_pManifestRequest);

	m_Stage = EStage::FETCH_MANIFEST;
	log_info("rank_ghost", "fetching manifest from '%s'", MANIFEST_URL);
	Echo(Localize("Rank ghost: fetching the replay list ..."));
}

void CRankGhost::UpdateManifestStage()
{
	if(!m_pManifestRequest || !m_pManifestRequest->Done())
		return;

	const bool Ok = m_pManifestRequest->State() == EHttpState::DONE && m_pManifestRequest->StatusCode() == 200;
	unsigned char *pResult = nullptr;
	size_t ResultLength = 0;
	if(Ok)
		m_pManifestRequest->Result(&pResult, &ResultLength);

	if(!Ok || pResult == nullptr || ResultLength == 0)
	{
		m_pManifestRequest = nullptr;
		m_ManifestFailed = true;
		m_ManifestFailedAt = time_get();
		Fail(Localize("Rank ghost: failed to fetch the replay list"));
		return;
	}

	// 响应缓冲区由请求对象持有，必须等解析完成后再释放请求
	const bool Parsed = ParseManifest(pResult, ResultLength);
	m_pManifestRequest = nullptr;

	if(!Parsed)
	{
		m_ManifestFailed = true;
		m_ManifestFailedAt = time_get();
		Fail(Localize("Rank ghost: the replay list is empty or malformed"));
		return;
	}

	m_ManifestLoaded = true;
	m_ManifestFailed = false;
	m_ManifestLoadedAt = time_get();
	m_Stage = EStage::IDLE;
	log_info("rank_ghost", "manifest loaded: %d entries", (int)m_vEntries.size());
	LookupInManifest();
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
			Fail(aBuf);
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
		Fail(aBuf);
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
		m_vLoadedSlots.erase(m_vLoadedSlots.begin() + i);
		m_vLoadedGhostPaths.erase(m_vLoadedGhostPaths.begin() + i);
		break;
	}
	if(m_vLoadedSlots.empty())
	{
		m_ViewMode = false;
		m_ViewSelected = 0;
	}
	else if(m_ViewSelected >= (int)m_vLoadedSlots.size())
		m_ViewSelected = 0;
	m_LastAlignedRaceTick = -1;
}

void CRankGhost::StartDemoFetch()
{
	if(Http() == nullptr)
	{
		Fail(Localize("Rank ghost: HTTP is not available"));
		return;
	}

	// QmClient：demo 浏览器目录已有同一份 rank1 回放时复用，避免重复下载
	char aBrowserDemo[IO_MAX_PATH_LENGTH];
	if(FindBrowserDownloadedDemo(Storage(), m_ActiveEntry, aBrowserDemo, sizeof(aBrowserDemo)))
	{
		if(m_PendingMode == EPendingMode::DEMO_ONLY)
		{
			m_Stage = EStage::IDLE;
			char aBuf[IO_MAX_PATH_LENGTH + 64];
			str_format(aBuf, sizeof(aBuf), Localize("Rank demo already available in the demo browser: %s"), aBrowserDemo);
			Echo(aBuf);
			log_info("rank_ghost", "demo already downloaded by demo browser: '%s'", aBrowserDemo);
			return;
		}
		// 影子转换需要自己缓存目录里的副本：本地复制代替重新下载
		if(CopyStorageFile(Storage(), aBrowserDemo, m_aDemoStoragePath))
		{
			log_info("rank_ghost", "reusing demo downloaded by demo browser: '%s'", aBrowserDemo);
			FinishDemoFetch();
			return;
		}
		log_info("rank_ghost", "failed to reuse demo '%s', falling back to download", aBrowserDemo);
	}

	char aUrl[1024];
	str_format(aUrl, sizeof(aUrl), "%s/%s", DEMO_URL_PREFIX, m_ActiveEntry.m_Demo.c_str());

	m_pDemoRequest = HttpGetFile(aUrl, Storage(), m_aDemoStoragePath, IStorage::TYPE_SAVE);
	m_pDemoRequest->MaxResponseSize(MAX_DEMO_BYTES);
	m_pDemoRequest->Timeout(CTimeout{5000, 120000, 200, 10});
	m_pDemoRequest->LogProgress(HTTPLOG::FAILURE);
	m_pDemoRequest->FailOnErrorStatus(false);
	m_pDemoRequest->HeaderString("User-Agent", USER_AGENT);
	Http()->Run(m_pDemoRequest);

	m_Stage = EStage::FETCH_DEMO;
	log_info("rank_ghost", "downloading demo from '%s'", aUrl);
	Echo(Localize("Rank ghost: downloading the replay ..."));
}

void CRankGhost::UpdateDemoStage()
{
	if(!m_pDemoRequest || !m_pDemoRequest->Done())
		return;

	const bool Ok = m_pDemoRequest->State() == EHttpState::DONE && m_pDemoRequest->StatusCode() == 200;
	m_pDemoRequest = nullptr;

	if(!Ok)
	{
		Fail(Localize("Rank ghost: failed to download the replay"));
		return;
	}

	FinishDemoFetch();
}

// demo 下载（或本地复用）后的公共收尾：校验并按请求模式处理
void CRankGhost::FinishDemoFetch()
{
	if(!ValidateOrUnpackDemo(Storage(), m_aDemoStoragePath))
	{
		Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
		Fail(Localize("Rank ghost: the downloaded replay is not a valid demo"));
		return;
	}

	// 仅下载请求到此结束（Rank 1 页面的“下载回放”），不解析成影子
	if(m_PendingMode == EPendingMode::DEMO_ONLY)
	{
		m_Stage = EStage::IDLE;
		char aBuf[320];
		str_format(aBuf, sizeof(aBuf), Localize("Rank demo downloaded: %s. Play it from the Rank 1 list or the demo browser."), m_aDemoStoragePath);
		Echo(aBuf);
		log_info("rank_ghost", "demo cached: '%s'", m_aDemoStoragePath);
		return;
	}

	StartParse();
}

void CRankGhost::StartParse()
{
	m_Stage = EStage::IDLE;

	// 全新解析：时间线从头收集
	m_vViewEvents.clear();
	m_vViewSwitchStates.clear();
	m_vViewMessages.clear();
	m_ViewMessageCursor = 0;
	m_ViewEventCursor = 0;
	m_ViewAppliedSwitch = -1;
	m_ViewLastTick = -1;

	m_pParse = std::make_unique<SParseState>();
	SParseContext &Parse = m_pParse->m_Context;
	Parse.m_Cid = m_ActiveEntry.m_Cid;
	STrackContext &Primary = Parse.m_Tracks[Parse.m_Cid];
	StrToInts(Primary.m_Skin.m_aSkin, std::size(Primary.m_Skin.m_aSkin), "default");
	Primary.m_Skin.m_UseCustomColor = 0;
	Primary.m_Skin.m_ColorBody = 0;
	Primary.m_Skin.m_ColorFeet = 0;

	Parse.m_pPlayer = std::make_unique<CDemoPlayer>(&*Parse.m_pDelta, &*Parse.m_pDeltaSixup, false);

	if(Parse.m_pPlayer->Load(Storage(), nullptr, m_aDemoStoragePath, IStorage::TYPE_SAVE) == -1)
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), Localize("Rank ghost: failed to load the replay: %s"), Parse.m_pPlayer->ErrorMessage());
		// CDemoPlayer 析构要求文件已关闭，否则 dbg_assert 在 Release 下也会 abort
		Parse.m_pPlayer->Stop();
		m_pParse.reset();
		Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
		Fail(aBuf);
		return;
	}
	Parse.m_pPlayer->SetListener(this);

	const CDemoPlayer::CPlaybackInfo *pInfo = Parse.m_pPlayer->Info();
	const int NumMarkers = bytes_be_to_uint(pInfo->m_TimelineMarkers.m_aNumTimelineMarkers);
	Parse.m_HasRunMarkers = NumMarkers >= 2;
	if(Parse.m_HasRunMarkers)
	{
		Parse.m_RunStart = (int)bytes_be_to_uint(pInfo->m_TimelineMarkers.m_aTimelineMarkers[0]);
		Parse.m_RunEnd = (int)bytes_be_to_uint(pInfo->m_TimelineMarkers.m_aTimelineMarkers[1]);
		if(Parse.m_RunEnd <= Parse.m_RunStart)
		{
			Parse.m_pPlayer->Stop();
			m_pParse.reset();
			Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
			Fail(Localize("Rank ghost: the replay has an invalid run range"));
			return;
		}
	}
	else
	{
		// 没有起止标记：整段回放就是这一轮，从回放起点开始、到回放结束为止。
		// m_RunEnd 用回放自身的末尾 tick 作上界——不能取"无穷大"，否则轨迹预留会按
		// 上界申请巨额内存；真正的结束 tick 由解析中观察到的最大 tick 决定。
		Parse.m_RunStart = (int)pInfo->m_Info.m_FirstTick;
		Parse.m_RunEnd = maximum((int)pInfo->m_Info.m_LastTick, Parse.m_RunStart + 1);
		log_info("rank_ghost", "demo has no run start/end markers, using the whole replay");
	}

	const CMapInfo *pMapInfo = Parse.m_pPlayer->GetMapInfo();
	str_copy(Parse.m_aMapName, pMapInfo->m_aName);
	if(pMapInfo->m_Sha256.has_value())
		Parse.m_MapSha256 = pMapInfo->m_Sha256.value();
	else
		mem_zero(&Parse.m_MapSha256, sizeof(Parse.m_MapSha256));

	Parse.m_pPlayer->Play();

	m_Stage = EStage::PARSE;
	log_info("rank_ghost", "parsing demo: run ticks [%d, %d], cid=%d", Parse.m_RunStart, Parse.m_RunEnd, Parse.m_Cid);
	Echo(Localize("Rank ghost: converting the replay ..."));
}

void CRankGhost::UpdateParseStage()
{
	if(!m_pParse)
	{
		m_Stage = EStage::IDLE;
		return;
	}

	CDemoPlayer *pPlayer = m_pParse->m_Context.m_pPlayer.get();
	if(!pPlayer)
	{
		m_pParse.reset();
		m_Stage = EStage::IDLE;
		return;
	}

	// 逐 tick 推进（SeekTick(TICK_NEXT) 走轻量路径），并按帧预算限制耗时。
	// 注意 SeekTick 在回放末尾可能一直返回 true 而播放头不再前进：SetPos 把目标
	// tick 钳在 LastTick 后，TICK_NEXT 的慢速路径会因 m_NextTick >= WantedTick
	// 直接跳过并返回成功。必须按"播放头停滞"识别文件结束，否则解析会永远停在
	// 99% 空转（文件比 run 终点标记短的回放必现）
	const int64_t Deadline = time_get() + time_freq() * PARSE_TIME_BUDGET_MS / 1000;
	int Processed = 0;
	bool Failed = false;
	bool Stalled = false;
	int PrevTick = pPlayer->Info()->m_Info.m_CurrentTick;
	while(pPlayer->IsPlaying() && !pPlayer->BaseInfo()->m_Paused)
	{
		if(!pPlayer->SeekTick(IDemoPlayer::TICK_NEXT))
		{
			Failed = true;
			break;
		}
		Processed++;
		const int CurTick = pPlayer->Info()->m_Info.m_CurrentTick;
		if(CurTick == PrevTick)
		{
			Stalled = true;
			break;
		}
		PrevTick = CurTick;
		if(Processed >= PARSE_TICKS_PER_FRAME)
			break;
		if((Processed & 0xFF) == 0 && time_get() >= Deadline)
			break;
	}

	if(!Failed && !Stalled && pPlayer->IsPlaying() && !pPlayer->BaseInfo()->m_Paused)
		return;

	FinishParse();
}

void CRankGhost::FinishParse()
{
	if(!m_pParse)
	{
		m_Stage = EStage::IDLE;
		return;
	}

	std::unique_ptr<SParseState> pState = std::move(m_pParse);
	SParseContext &Parse = pState->m_Context;
	m_Stage = EStage::IDLE;

	if(Parse.m_pPlayer)
		Parse.m_pPlayer->Stop();

	// 无起止标记时以解析到的最后 tick 为终点（回放放完即这一轮结束）
	int RunEnd = Parse.m_HasRunMarkers ? Parse.m_RunEnd : maximum(Parse.m_LastSeenTick, Parse.m_RunStart + 1);
	// 有标记但文件比终点标记短：轨迹以实际看到的最后 tick 收尾，否则播放时间线
	// 长于轨迹，结尾会停在最后一帧。只差结尾 1 秒内视为正常收尾，截断在半路的
	// 回放仍按失败处理
	const bool TruncatedTail = Parse.m_HasRunMarkers && Parse.m_LastSeenTick > Parse.m_RunStart &&
				   Parse.m_RunEnd - Parse.m_LastSeenTick <= SERVER_TICK_SPEED;
	if(TruncatedTail)
		RunEnd = Parse.m_LastSeenTick;
	const int RunTicks = RunEnd - Parse.m_RunStart;
	const int TimeMs = RunTicks * 1000 / SERVER_TICK_SPEED;
	// 有标记时 m_ReachedRunEnd 为假说明回放在 run 结束前就被截断，避免写出不完整的影子；
	// 只差结尾 1 秒内按完成处理（TruncatedTail），无标记时"回放放完"本身就是合法的结束条件
	const bool RunComplete = !Parse.m_HasRunMarkers || Parse.m_ReachedRunEnd || TruncatedTail;
	if(TruncatedTail)
		log_info("rank_ghost", "replay ends %d ticks before the run end marker, using the last seen tick", Parse.m_RunEnd - Parse.m_LastSeenTick);

	// 主选手轨迹在前，其余按 cid 排序；过短的轨迹（中途加入/离开）丢弃
	std::vector<STrackContext *> vpTracks;
	auto PrimaryIt = Parse.m_Tracks.find(Parse.m_Cid);
	if(PrimaryIt != Parse.m_Tracks.end() && PrimaryIt->second.m_FoundCharacter)
		vpTracks.push_back(&PrimaryIt->second);
	for(auto &TrackEntry : Parse.m_Tracks)
	{
		if(TrackEntry.first == Parse.m_Cid || !TrackEntry.second.m_FoundCharacter)
			continue;
		if((int)TrackEntry.second.m_vPath.size() < MIN_PATH_TICKS)
			continue;
		vpTracks.push_back(&TrackEntry.second);
	}

	if(m_EventRebuildOnly)
	{
		// 时间线补齐：只写 sidecar 缓存，不重写影子、不重载槽位（查看不受打扰）。
		// 失败也不能走上面的清理分支，否则会把缓存里的回放文件删掉。
		m_EventRebuildOnly = false;
		if(vpTracks.empty() || (int)vpTracks[0]->m_vPath.size() < MIN_PATH_TICKS || !RunComplete || TimeMs <= 0)
		{
			log_error("rank_ghost", "view timeline rebuild failed: invalid run range");
			return;
		}
		WriteViewTimeline();
		return;
	}

	if(vpTracks.empty() || (int)vpTracks[0]->m_vPath.size() < MIN_PATH_TICKS || !RunComplete || TimeMs <= 0)
	{
		Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
		Fail(Localize("Rank ghost: could not extract the runner's path from the replay"));
		return;
	}
	// StartTick 必须是轨迹 tick 所在的原始时间轴（demo 绝对 tick）：
	// 渲染端用它把 AttackTick 重定基到当前时间，绝对/相对混用会导致开火等特效常驻。
	const int StartTick = Parse.m_RunStart;
	// 轨迹主人兜底：主选手用 manifest 名单，其余用 cid 标识
	for(size_t i = 0; i < vpTracks.size(); i++)
	{
		STrackContext *pTrack = vpTracks[i];
		if(pTrack->m_aOwner[0] == '\0')
		{
			if(i == 0)
				str_copy(pTrack->m_aOwner, m_ActiveEntry.m_Names.c_str(), sizeof(pTrack->m_aOwner));
			else
				str_format(pTrack->m_aOwner, sizeof(pTrack->m_aOwner), "Tee %d", i);
		}
	}

	auto WriteTrack = [&](CGhostFileWriter &Writer, STrackContext *pTrack, int NumTicks) {
		if(!Writer.Open(Storage(), m_aGhostWritePath, Parse.m_aMapName, Parse.m_MapSha256, pTrack->m_aOwner, NumTicks, TimeMs))
			return false;
		bool WriteOk = Writer.WriteData(GHOSTDATA_TYPE_START_TICK, &StartTick, sizeof(int));
		WriteOk = WriteOk && Writer.WriteData(GHOSTDATA_TYPE_SKIN, &pTrack->m_Skin, sizeof(CGhostSkin));
		for(const CGhostCharacter &Char : pTrack->m_vPath)
			WriteOk = WriteOk && Writer.WriteData(GHOSTDATA_TYPE_CHARACTER, &Char, sizeof(CGhostCharacter));
		return Writer.Close() && WriteOk;
	};

	if(vpTracks.size() == 1)
	{
		// 单轨：沿用单文件缓存
		const int NumTicks = (int)vpTracks[0]->m_vPath.size();
		char aTempGhostPath[IO_MAX_PATH_LENGTH];
		str_format(aTempGhostPath, sizeof(aTempGhostPath), "%s.tmp", m_aGhostStoragePath);
		Storage()->RemoveFile(aTempGhostPath, IStorage::TYPE_SAVE);
		str_copy(m_aGhostWritePath, aTempGhostPath, sizeof(m_aGhostWritePath));
		CGhostFileWriter TempWriter;
		const bool WriteOk = WriteTrack(TempWriter, vpTracks[0], NumTicks);
		if(!WriteOk || !Storage()->RenameFile(aTempGhostPath, m_aGhostStoragePath, IStorage::TYPE_SAVE))
		{
			Storage()->RemoveFile(aTempGhostPath, IStorage::TYPE_SAVE);
			Fail(Localize("Rank ghost: failed to write the ghost file"));
			return;
		}
	}
	else
	{
		// 多轨（teamrun）：每个参与者一个标准 .gho，存进同前缀目录（00 = 主选手）
		char aGroupDir[IO_MAX_PATH_LENGTH];
		str_copy(aGroupDir, m_aGhostStoragePath, sizeof(aGroupDir));
		const size_t Len = str_length(aGroupDir);
		if(Len > 4 && str_endswith(aGroupDir, ".gho"))
			aGroupDir[Len - 4] = '\0';
		Storage()->CreateFolder(aGroupDir, IStorage::TYPE_SAVE);

		char aFailedPath[IO_MAX_PATH_LENGTH] = "";
		for(size_t i = 0; i < vpTracks.size(); i++)
		{
			const int NumTicks = (int)vpTracks[i]->m_vPath.size();
			str_format(m_aGhostWritePath, sizeof(m_aGhostWritePath), "%s/%02d.gho", aGroupDir, (int)i);
			CGhostFileWriter Writer;
			if(!WriteTrack(Writer, vpTracks[i], NumTicks))
			{
				str_copy(aFailedPath, m_aGhostWritePath, sizeof(aFailedPath));
				break;
			}
		}
		if(aFailedPath[0] != '\0')
		{
			// 失败清理：删除已写出的轨迹文件，避免留下半成品
			for(const SCacheNameTime &File : ListCacheFiles(Storage(), aGroupDir, ".gho"))
			{
				char aPath[IO_MAX_PATH_LENGTH];
				str_format(aPath, sizeof(aPath), "%s/%s", aGroupDir, File.m_Name.c_str());
				Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
			}
			log_error("rank_ghost", "failed to write multi-track ghost file '%s'", aFailedPath);
			Fail(Localize("Rank ghost: failed to write the ghost file"));
			return;
		}
		log_info("rank_ghost", "multi-track ghost written: %d tracks", (int)vpTracks.size());
	}

	// 查看模式时间线随影子一起缓存到 sidecar 文件
	WriteViewTimeline();

	if(!LoadGhostTarget())
	{
		// 生成成功但当前地图还不是目标地图：保留文件，等地图加载后由 OnUpdate 重试
		const std::string &TargetMap = !m_PendingMap.empty() ? m_PendingMap : m_ActiveEntry.m_Map;
		if(Client()->GetCurrentMap()[0] == '\0' || str_comp_nocase(Client()->GetCurrentMap(), TargetMap.c_str()) != 0)
		{
			m_RetryLoadPending = true;
			m_RetryLoadDeadline = time_get() + time_freq() * 30;
			m_RetryLoadNextAttempt = time_get() + time_freq() / 2;
			m_PendingNotify = Localize("Rank ghost: replay converted, waiting for the map to load ...");
			return;
		}
		log_error("rank_ghost", "ghost written but loading failed (map is loaded)");
		Fail(Localize("Rank ghost: failed to load the generated ghost (join the map first)"));
		return;
	}
	log_info("rank_ghost", "ghost loaded: %d tracks, %d ms", (int)vpTracks.size(), TimeMs);

	char aName[MAX_NAME_LENGTH];
	str_copy(aName, vpTracks[0]->m_aOwner, sizeof(aName));
	if(aName[0] == '\0')
		str_copy(aName, m_ActiveEntry.m_Names.c_str(), sizeof(aName));
	char aTime[32];
	str_format(aTime, sizeof(aTime), "%.2f", TimeMs / 1000.0f);
	NotifyLoaded(aName, aTime);
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
	if(m_PendingView)
	{
		m_PendingView = false;
		EnterViewMode();
	}
}

void CRankGhost::EnterViewMode()
{
	if(m_vLoadedSlots.empty())
		return;
	GameClient()->m_Ghost.StartRenderManual();
	if(!GameClient()->m_Ghost.ManualModeActive())
		return;
	m_ViewMode = true;
	// 查看模式不跟跑，清掉对齐标记，退出后由下一次跑图重新对齐
	m_LastAlignedRaceTick = -1;
	// 首次进入必须给出可退出的指引：控制面板由 ESC 开关，退出回放要走游戏菜单（双击 ESC）
	const char *pHint = Localize("Ghost view mode: ESC toggles the control panel. Double-tap ESC to open the game menu (leave the replay). Hold the spectate key for the member list.");
	Echo(pHint);
	GameClient()->m_QmHudNotifications.QueueEcho(pHint, g_Config.m_ClMessageClientColor);
}

bool CRankGhost::IsViewModeActive() const
{
	return m_ViewMode && !m_vLoadedSlots.empty();
}

bool CRankGhost::GetViewState(SViewState &Out) const
{
	Out = SViewState();
	if(!IsViewModeActive())
		return false;
	const int EndTick = GameClient()->m_Ghost.ManualEndTick();
	if(EndTick <= 0)
		return false;
	// 总时长必须与播放头同一时间基（EndTick 个 tick @ SERVER_TICK_SPEED）。
	// manifest 里的成绩时间只覆盖 run 本身：回放文件往往还带起点前的等待段，
	// 甚至可能被最短轨迹截断，用它当总时长会让计时飞转、进度条与虚影位置对不上
	const float TotalSeconds = EndTick / (float)SERVER_TICK_SPEED;
	const int CurTick = std::clamp(GameClient()->m_Ghost.ManualPlaybackTick(), 0, EndTick);
	Out.m_Active = true;
	Out.m_Playing = GameClient()->m_Ghost.ManualPlaying();
	Out.m_TotalSeconds = TotalSeconds;
	Out.m_CurSeconds = TotalSeconds * CurTick / (float)EndTick;
	Out.m_Progress = CurTick / (float)EndTick;
	// Finish 事件是回放时间线中唯一可靠的终点信号。记录它的相对 tick，
	// 让原生 DDRace Finish HUD 可以使用独立播放时钟完成 4 秒停留 + 2 秒淡出。
	for(const SViewEvent &Event : m_vViewEvents)
	{
		if(Event.m_Type != NETEVENTTYPE_FINISH || Event.m_RelTick > CurTick)
			continue;
		Out.m_Finished = true;
		Out.m_FinishSeconds = TotalSeconds * Event.m_RelTick / (float)EndTick;
		break;
	}

	// 成绩类消息带有真实成绩与差值（快照事件里都没有），优先用它们覆盖上面的估算值。
	// 注意两条消息的时间单位不同：RACE_FINISH 是毫秒，RACE_TIME 的 m_Check 是厘秒。
	if(const SViewMessage *pFinish = FindLatestViewMessage(EViewMessageType::RACE_FINISH, CurTick))
	{
		Out.m_Finished = true;
		Out.m_FinishSeconds = pFinish->m_aData[1] / 1000.0f;
		if(pFinish->m_aData[2] != 0)
		{
			Out.m_HasFinishDiff = true;
			Out.m_FinishDiffSeconds = pFinish->m_aData[2] / 1000.0f;
		}
		Out.m_FinishIsRecord = pFinish->m_aData[3] != 0 || pFinish->m_aData[4] != 0;
	}
	if(const SViewMessage *pRaceTime = FindLatestViewMessage(EViewMessageType::RACE_TIME, CurTick))
	{
		if(pRaceTime->m_aData[2] != 0)
		{
			// 终点那一帧的服务端成绩：比 Finish 事件的估算精确
			Out.m_Finished = true;
			Out.m_FinishSeconds = pRaceTime->m_aData[0] / 1000.0f;
			if(pRaceTime->m_aData[1] != 0 && !Out.m_HasFinishDiff)
			{
				Out.m_HasFinishDiff = true;
				Out.m_FinishDiffSeconds = pRaceTime->m_aData[1] / 100.0f;
			}
		}
		else if(pRaceTime->m_aData[1] != 0)
		{
			// 检查点差值：与原生 HUD 的 m_TimeCpDiff 同一语义，附带播放头年龄
			Out.m_HasCpDiff = true;
			Out.m_CpDiffSeconds = pRaceTime->m_aData[1] / 100.0f;
			Out.m_CpDiffAgeSeconds = (CurTick - pRaceTime->m_RelTick) * TotalSeconds / (float)EndTick;
		}
	}
	return true;
}

void CRankGhost::ViewPlayPause()
{
	if(!IsViewModeActive())
		return;
	CGhost *pGhost = &GameClient()->m_Ghost;
	// 播到末帧（自动暂停）后再按播放 = 从头重播
	if(!pGhost->ManualPlaying() && pGhost->ManualPlaybackTick() >= pGhost->ManualEndTick())
	{
		pGhost->ManualSeek(0);
		pGhost->ManualSetPlaying(true);
		return;
	}
	pGhost->ManualSetPlaying(!pGhost->ManualPlaying());
}

void CRankGhost::ViewSeek(float Fraction)
{
	if(!IsViewModeActive())
		return;
	const int MaxTick = maximum(1, GameClient()->m_Ghost.ManualEndTick());
	GameClient()->m_Ghost.ManualSeek((int)(std::clamp(Fraction, 0.0f, 1.0f) * MaxTick));
}

void CRankGhost::ViewStop()
{
	if(!m_ViewMode)
		return;
	m_ViewMode = false;
	m_ViewSelected = 0;
	m_ViewCameraMode = EViewCameraMode::MEMBER;
	m_ViewFreeCameraValid = false;
	m_ViewZoomPersonal = 0.0f;
	GameClient()->m_Ghost.StopManual();
	Echo(Localize("Ghost view mode off. The ghost follows your runs again."));
}

// ===== 查看模式时间线：录制 / 缓存 / 回放 =====

// 从解析中的快照收集交互事件与开关状态（run 区间内，tick 已转为相对值）
void CRankGhost::RecordViewTimeline(const CSnapshot *pSnapshot, int RelTick)
{
	const int NumItems = pSnapshot->NumItems();
	for(int i = 0; i < NumItems; i++)
	{
		const int Type = pSnapshot->GetItemType(i);
		int NumInts = 0;
		// 事件对象都使用固定的 int 字段；这里按生成协议的实际布局拷贝，
		// 让终点、出生、死亡和地图世界音效也进入独立查看时间线。
		if(Type == NETEVENTTYPE_HAMMERHIT || Type == NETEVENTTYPE_EXPLOSION ||
			Type == NETEVENTTYPE_BIRTHDAY || Type == NETEVENTTYPE_FINISH ||
			Type == NETEVENTTYPE_SPAWN)
			NumInts = 2;
		else if(Type == NETEVENTTYPE_DAMAGEIND || Type == NETEVENTTYPE_DEATH ||
			Type == NETEVENTTYPE_SOUNDWORLD || Type == NETEVENTTYPE_MAPSOUNDWORLD)
			NumInts = 3;
		else if(Type != NETOBJTYPE_SWITCHSTATE)
			continue;

		const CSnapshotItem *pItem = pSnapshot->GetItem(i);
		const int ItemSize = pSnapshot->GetItemSize(i);

		if(Type == NETOBJTYPE_SWITCHSTATE)
		{
			// 服务端每份快照都全量下发，仅在状态变化时记录（32 字节状态位 = 8 个 int）
			if(ItemSize < 36)
				continue;
			const CNetObj_SwitchState *pState = (const CNetObj_SwitchState *)pItem->Data();
			SViewSwitchState Record;
			Record.m_RelTick = RelTick;
			Record.m_HighestSwitchNumber = std::clamp(pState->m_HighestSwitchNumber, 0, 255);
			mem_zero(Record.m_aStatus, sizeof(Record.m_aStatus));
			const int MaxSwitch = minimum(Record.m_HighestSwitchNumber, (int)std::size(Record.m_aStatus) * 32 - 1);
			for(int j = 0; j <= MaxSwitch; j++)
			{
				if(((unsigned)pState->m_aStatus[j / 32] >> (j % 32)) & 1)
					Record.m_aStatus[j / 32] |= 1u << (j % 32);
			}
			if(!m_vViewSwitchStates.empty())
			{
				const SViewSwitchState &Last = m_vViewSwitchStates.back();
				if(Last.m_HighestSwitchNumber == Record.m_HighestSwitchNumber &&
					mem_comp(Last.m_aStatus, Record.m_aStatus, sizeof(Record.m_aStatus)) == 0)
					continue;
			}
			m_vViewSwitchStates.push_back(Record);
			continue;
		}

		if(ItemSize < (int)(NumInts * sizeof(int)))
			continue;
		SViewEvent Event;
		Event.m_RelTick = RelTick;
		Event.m_Type = Type;
		mem_zero(Event.m_aData, sizeof(Event.m_aData));
		mem_copy(Event.m_aData, pItem->Data(), NumInts * sizeof(int));
		m_vViewEvents.push_back(Event);
	}
}

// sidecar 版本号：加入消息段（终点成绩/差值、全局音效）后为 QMGHEVT2。
// 旧版本文件会被 ViewTimelineCached() 判为未缓存，由补齐流程重建一次。
static constexpr char VIEW_TIMELINE_MAGIC[8] = {'Q', 'M', 'G', 'H', 'E', 'V', 'T', '2'};

// sidecar 缓存路径：多轨组 <base>/events.qmevt，单轨迹 <base>.qmevt
bool CRankGhost::ViewTimelinePath(char *pBuf, size_t BufSize) const
{
	char aBase[IO_MAX_PATH_LENGTH];
	str_copy(aBase, m_aGhostStoragePath, sizeof(aBase));
	const size_t Len = str_length(aBase);
	if(Len > 4 && str_endswith(aBase, ".gho"))
		aBase[Len - 4] = '\0';

	char aGroupDir[IO_MAX_PATH_LENGTH];
	str_copy(aGroupDir, aBase, sizeof(aGroupDir));
	if(Storage()->FolderExists(aGroupDir, IStorage::TYPE_SAVE) && !ListCacheFiles(Storage(), aGroupDir, ".gho").empty())
	{
		str_format(pBuf, BufSize, "%s/events.qmevt", aGroupDir);
		return true;
	}
	str_format(pBuf, BufSize, "%s.qmevt", aBase);
	return false;
}

bool CRankGhost::ViewTimelineCached() const
{
	char aPath[IO_MAX_PATH_LENGTH];
	ViewTimelinePath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;
	char aMagic[8];
	const bool Current = io_read(File, aMagic, sizeof(aMagic)) == sizeof(aMagic) &&
			     mem_comp(aMagic, VIEW_TIMELINE_MAGIC, sizeof(aMagic)) == 0;
	io_close(File);
	// 旧版本 sidecar（例如没有消息段的 QMGHEVT1）按未缓存处理，
	// 由补齐流程重放一次回放重建，避免查看模式永远缺终点成绩与全局音效。
	return Current;
}

void CRankGhost::WriteViewTimeline()
{
	if(m_vViewEvents.empty() && m_vViewSwitchStates.empty() && m_vViewMessages.empty())
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	ViewTimelinePath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
	{
		log_error("rank_ghost", "failed to write view timeline '%s'", aPath);
		return;
	}

	io_write(File, VIEW_TIMELINE_MAGIC, sizeof(VIEW_TIMELINE_MAGIC));
	uint32_t Count = (uint32_t)m_vViewEvents.size();
	io_write(File, &Count, sizeof(Count));
	for(const SViewEvent &Event : m_vViewEvents)
	{
		io_write(File, &Event.m_RelTick, sizeof(Event.m_RelTick));
		io_write(File, &Event.m_Type, sizeof(Event.m_Type));
		io_write(File, Event.m_aData, sizeof(Event.m_aData));
	}
	Count = (uint32_t)m_vViewSwitchStates.size();
	io_write(File, &Count, sizeof(Count));
	for(const SViewSwitchState &State : m_vViewSwitchStates)
	{
		io_write(File, &State.m_RelTick, sizeof(State.m_RelTick));
		io_write(File, &State.m_HighestSwitchNumber, sizeof(State.m_HighestSwitchNumber));
		io_write(File, State.m_aStatus, sizeof(State.m_aStatus));
	}
	Count = (uint32_t)m_vViewMessages.size();
	io_write(File, &Count, sizeof(Count));
	for(const SViewMessage &Message : m_vViewMessages)
	{
		// 枚举按 int 落盘：m_Type 的底层宽度是实现定义的，写字节会破坏跨版本读取
		const int Type = (int)Message.m_Type;
		io_write(File, &Message.m_RelTick, sizeof(Message.m_RelTick));
		io_write(File, &Type, sizeof(Type));
		io_write(File, Message.m_aData, sizeof(Message.m_aData));
	}
	io_close(File);
	log_info("rank_ghost", "view timeline written: %d events, %d switch states, %d messages ('%s')",
		(int)m_vViewEvents.size(), (int)m_vViewSwitchStates.size(), (int)m_vViewMessages.size(), aPath);
}

bool CRankGhost::LoadViewTimeline()
{
	m_vViewEvents.clear();
	m_vViewSwitchStates.clear();
	m_vViewMessages.clear();
	m_ViewEventCursor = 0;
	m_ViewMessageCursor = 0;
	m_ViewAppliedSwitch = -1;
	m_ViewLastTick = -1;

	char aPath[IO_MAX_PATH_LENGTH];
	ViewTimelinePath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;

	char aMagic[8];
	uint32_t Count = 0;
	bool Ok = io_read(File, aMagic, sizeof(aMagic)) == sizeof(aMagic) &&
		  mem_comp(aMagic, VIEW_TIMELINE_MAGIC, sizeof(aMagic)) == 0 &&
		  io_read(File, &Count, sizeof(Count)) == sizeof(Count);
	if(Ok)
	{
		m_vViewEvents.reserve(Count);
		for(uint32_t i = 0; i < Count && Ok; i++)
		{
			SViewEvent Event;
			Ok = io_read(File, &Event.m_RelTick, sizeof(Event.m_RelTick)) == sizeof(Event.m_RelTick) &&
			     io_read(File, &Event.m_Type, sizeof(Event.m_Type)) == sizeof(Event.m_Type) &&
			     io_read(File, Event.m_aData, sizeof(Event.m_aData)) == sizeof(Event.m_aData);
			if(Ok)
				m_vViewEvents.push_back(Event);
		}
	}
	if(Ok)
	{
		Ok = io_read(File, &Count, sizeof(Count)) == sizeof(Count);
		if(Ok)
		{
			m_vViewSwitchStates.reserve(Count);
			for(uint32_t i = 0; i < Count && Ok; i++)
			{
				SViewSwitchState State;
				Ok = io_read(File, &State.m_RelTick, sizeof(State.m_RelTick)) == sizeof(State.m_RelTick) &&
				     io_read(File, &State.m_HighestSwitchNumber, sizeof(State.m_HighestSwitchNumber)) == sizeof(State.m_HighestSwitchNumber) &&
				     io_read(File, State.m_aStatus, sizeof(State.m_aStatus)) == sizeof(State.m_aStatus);
				if(Ok)
					m_vViewSwitchStates.push_back(State);
			}
		}
	}
	if(Ok)
	{
		Ok = io_read(File, &Count, sizeof(Count)) == sizeof(Count);
		if(Ok)
		{
			m_vViewMessages.reserve(Count);
			for(uint32_t i = 0; i < Count && Ok; i++)
			{
				SViewMessage Message;
				int Type = 0;
				Ok = io_read(File, &Message.m_RelTick, sizeof(Message.m_RelTick)) == sizeof(Message.m_RelTick) &&
				     io_read(File, &Type, sizeof(Type)) == sizeof(Type) &&
				     io_read(File, Message.m_aData, sizeof(Message.m_aData)) == sizeof(Message.m_aData);
				if(!Ok)
					break;
				if(Type < 0 || Type > (int)EViewMessageType::MAP_SOUND_GLOBAL)
				{
					// 未知类型按缓存损坏处理，交给补齐流程重建
					Ok = false;
					break;
				}
				Message.m_Type = (EViewMessageType)Type;
				m_vViewMessages.push_back(Message);
			}
		}
	}
	io_close(File);
	if(!Ok)
	{
		// 缓存损坏按缺失处理，旧缓存可由补齐流程重建
		log_error("rank_ghost", "failed to read view timeline '%s'", aPath);
		m_vViewEvents.clear();
		m_vViewSwitchStates.clear();
		m_vViewMessages.clear();
		return false;
	}
	log_info("rank_ghost", "view timeline loaded: %d events, %d switch states, %d messages ('%s')",
		(int)m_vViewEvents.size(), (int)m_vViewSwitchStates.size(), (int)m_vViewMessages.size(), aPath);
	return true;
}

void CRankGhost::KickViewTimelineRebuild()
{
	if(m_Stage != EStage::IDLE || m_StartPending)
		return;
	if(!Storage()->FileExists(m_aDemoStoragePath, IStorage::TYPE_SAVE))
		return;
	log_info("rank_ghost", "view timeline missing, rebuilding from the cached replay");
	m_EventRebuildOnly = true;
	StartParse();
}

// 查看模式每帧：按播放头派发交互事件、应用开关状态
void CRankGhost::UpdateViewTimeline()
{
	CGhost *pGhost = &GameClient()->m_Ghost;
	const int CurTick = pGhost->ManualPlaybackTick();
	if(CurTick < m_ViewLastTick)
	{
		// 播放头后退（拖进度条）：重置游标，重放这段的特效与状态
		m_ViewLastTick = CurTick;
		m_ViewAppliedSwitch = -1;
		m_ViewEventCursor = std::lower_bound(m_vViewEvents.begin(), m_vViewEvents.end(), CurTick,
					    [](const SViewEvent &Event, int Tick) { return Event.m_RelTick < Tick; }) -
				    m_vViewEvents.begin();
		m_ViewMessageCursor = std::lower_bound(m_vViewMessages.begin(), m_vViewMessages.end(), CurTick,
					      [](const SViewMessage &Message, int Tick) { return Message.m_RelTick < Tick; }) -
				      m_vViewMessages.begin();
	}
	m_ViewLastTick = CurTick;

	// 事件派发限流：大幅快进时按帧铺开，避免单帧粒子爆炸
	int Budget = 96;
	while(m_ViewEventCursor < m_vViewEvents.size() && Budget-- > 0)
	{
		const SViewEvent &Event = m_vViewEvents[m_ViewEventCursor];
		if(Event.m_RelTick > CurTick)
			break;
		DispatchViewEvent(Event);
		m_ViewEventCursor++;
	}

	// 消息时间线：只有全局音效是"播一次"的动作，成绩类消息由 GetViewState 按
	// 播放头查询，不需要游标状态
	while(m_ViewMessageCursor < m_vViewMessages.size())
	{
		const SViewMessage &Message = m_vViewMessages[m_ViewMessageCursor];
		if(Message.m_RelTick > CurTick)
			break;
		if(Message.m_Type == EViewMessageType::SOUND_GLOBAL || Message.m_Type == EViewMessageType::MAP_SOUND_GLOBAL)
			DispatchViewMessage(Message);
		m_ViewMessageCursor++;
	}

	// 开关状态：应用到渲染所用的队伍槽位；服务端每份快照都会重发真实状态，
	// 退出查看模式后下一次快照即自动恢复，无需备份
	if(!m_vViewSwitchStates.empty())
	{
		if(m_ViewAppliedSwitch < 0)
		{
			auto It = std::upper_bound(m_vViewSwitchStates.begin(), m_vViewSwitchStates.end(), CurTick,
				[](int Tick, const SViewSwitchState &State) { return Tick < State.m_RelTick; });
			m_ViewAppliedSwitch = (int)(It - m_vViewSwitchStates.begin()) - 1;
			if(m_ViewAppliedSwitch >= 0)
				ApplyViewSwitchState(m_vViewSwitchStates[m_ViewAppliedSwitch]);
		}
		else
		{
			while(m_ViewAppliedSwitch + 1 < (int)m_vViewSwitchStates.size() &&
				m_vViewSwitchStates[m_ViewAppliedSwitch + 1].m_RelTick <= CurTick)
			{
				m_ViewAppliedSwitch++;
				ApplyViewSwitchState(m_vViewSwitchStates[m_ViewAppliedSwitch]);
			}
		}
	}
}

void CRankGhost::DispatchViewEvent(const SViewEvent &Event)
{
	CGameClient *pGameClient = GameClient();
	const vec2 Pos(Event.m_aData[0], Event.m_aData[1]);
	switch(Event.m_Type)
	{
	case NETEVENTTYPE_HAMMERHIT:
		pGameClient->m_Effects.HammerHit(Pos, 1.0f, 1.0f);
		break;
	case NETEVENTTYPE_EXPLOSION:
		pGameClient->m_Effects.Explosion(Pos, 1.0f);
		break;
	case NETEVENTTYPE_BIRTHDAY:
	case NETEVENTTYPE_FINISH:
		// 与 CGameClient 的快照事件渲染保持一致：终点特效就是 confetti。
		pGameClient->m_Effects.Confetti(Pos, 1.0f);
		break;
	case NETEVENTTYPE_SPAWN:
		pGameClient->m_Effects.PlayerSpawn(Pos, 1.0f, 1.0f);
		break;
	case NETEVENTTYPE_DEATH:
		pGameClient->m_Effects.PlayerDeath(Pos, Event.m_aData[2], 1.0f);
		break;
	case NETEVENTTYPE_DAMAGEIND:
		pGameClient->m_Effects.DamageIndicator(Pos, direction(Event.m_aData[2] / 256.0f), 1.0f);
		break;
	case NETEVENTTYPE_SOUNDWORLD:
	{
		// 普通世界音效走游戏音效；地图世界音效必须走地图音效容器，
		// 否则机关音效的地图专属资源与音量语义会丢失。
		// 门控条件与 CGameClient::ProcessEvents 的快照事件分发一致：关闭游戏音效、
		// 专注模式静音跳跃/死亡音、赛道音效的枪声/长痛音开关。否则查看模式会播出
		// 在线跑图时被过滤掉的声音，与 demo 播放听感不一致。
		if(!Config()->m_SndGame)
			break;
		const int SoundId = Event.m_aData[2];
		const SQmFocusModeDecisions Focus = GetQmFocusModeDecisions();
		if(SoundId == SOUND_PLAYER_JUMP && !Focus.m_AirJump.m_PlaySound)
			break;
		if(SoundId == SOUND_PLAYER_DIE && !Focus.m_PlayDeathOrSpawnSound)
			break;
		if(pGameClient->m_GameInfo.m_RaceSounds &&
			((SoundId == SOUND_GUN_FIRE && !g_Config.m_SndGun) || (SoundId == SOUND_PLAYER_PAIN_LONG && !g_Config.m_SndLongPain)))
			break;
		pGameClient->m_Sounds.PlayAt(CSounds::CHN_WORLD, SoundId, 1.0f, Pos);
		break;
	}
	case NETEVENTTYPE_MAPSOUNDWORLD:
		if(Config()->m_SndGame)
			pGameClient->m_MapSounds.PlayAt(CSounds::CHN_WORLD, Event.m_aData[2], Pos);
		break;
	default:
		break;
	}
}

void CRankGhost::DispatchViewMessage(const SViewMessage &Message)
{
	// 只有全局音效需要在这里派发：成绩类消息由 GetViewState 按播放头查询。
	// 门控与 CGameClient::OnMessage 的同名消息保持一致（关闭游戏音效即静音）。
	CGameClient *pGameClient = GameClient();
	if(!Config()->m_SndGame)
		return;
	if(Message.m_Type == EViewMessageType::SOUND_GLOBAL)
	{
		const int SoundId = Message.m_aData[0];
		// CTF 类全局音效在线路径走 Enqueue（避免同 tick 重复触发），其余直接播
		if(SoundId == SOUND_CTF_DROP || SoundId == SOUND_CTF_RETURN || SoundId == SOUND_CTF_CAPTURE ||
			SoundId == SOUND_CTF_GRAB_EN || SoundId == SOUND_CTF_GRAB_PL)
			pGameClient->m_Sounds.Enqueue(CSounds::CHN_GLOBAL, SoundId);
		else
			pGameClient->m_Sounds.Play(CSounds::CHN_GLOBAL, SoundId, 1.0f);
	}
	else if(Message.m_Type == EViewMessageType::MAP_SOUND_GLOBAL)
	{
		pGameClient->m_MapSounds.Play(CSounds::CHN_GLOBAL, Message.m_aData[0]);
	}
}

void CRankGhost::ApplyViewSwitchState(const SViewSwitchState &State)
{
	auto &vSwitchers = GameClient()->Switchers();
	if(vSwitchers.empty())
		return;
	const int Team = std::clamp(GameClient()->SwitchStateTeam(), (int)TEAM_FLOCK, NUM_DDRACE_TEAMS - 1);
	const int Count = minimum(State.m_HighestSwitchNumber + 1, (int)vSwitchers.size());
	for(int j = 0; j < Count; j++)
	{
		const bool Status = (State.m_aStatus[j / 32] >> (j % 32)) & 1;
		vSwitchers[j].m_aStatus[Team] = Status;
		// demo 里的 endtick 是绝对 tick，放到本地时间轴会瞬间过期；
		// 查看期间按稳定状态处理，定时开关的回关由后续状态变化记录覆盖
		vSwitchers[j].m_aEndTick[Team] = 0;
		vSwitchers[j].m_aType[Team] = Status ? TILE_SWITCHOPEN : TILE_SWITCHCLOSE;
	}
}

void CRankGhost::ViewSelectMember(int Index)
{
	if(!IsViewModeActive() || m_vLoadedSlots.empty())
	{
		m_ViewSelected = 0;
		return;
	}
	m_ViewSelected = std::clamp(Index, 0, (int)m_vLoadedSlots.size() - 1);
	// 点选成员即恢复跟随视角
	m_ViewCameraMode = EViewCameraMode::MEMBER;
}

void CRankGhost::ViewSetCameraMode(EViewCameraMode Mode)
{
	m_ViewCameraMode = Mode;
	if(Mode == EViewCameraMode::FREE)
	{
		// 进入自由视角时锚定当前镜头位置，之后由鼠标平移
		m_ViewFreeCameraCenter = GameClient()->m_Camera.m_Center;
		m_ViewFreeCameraValid = true;
	}
	else
	{
		m_ViewFreeCameraValid = false;
	}
}

// 多人同框的用户倍率：在自动取景之上叠加，使 zoom+/- 仍然可调（正=放大）
void CRankGhost::ViewAdjustZoomPersonal(float DeltaSteps)
{
	ViewSetZoomPersonal(m_ViewZoomPersonal + DeltaSteps);
}

void CRankGhost::ViewSetZoomPersonal(float Steps)
{
	m_ViewZoomPersonal = std::clamp(Steps, -VIEW_ZOOM_PERSONAL_LIMIT, VIEW_ZOOM_PERSONAL_LIMIT);
}

bool CRankGhost::ViewFreeCameraCenter(vec2 *pOut) const
{
	if(m_ViewCameraMode != EViewCameraMode::FREE || !m_ViewFreeCameraValid)
		return false;
	*pOut = m_ViewFreeCameraCenter;
	return true;
}

void CRankGhost::ViewFreeCameraPan(float Dx, float Dy)
{
	if(m_ViewCameraMode != EViewCameraMode::FREE || !m_ViewFreeCameraValid)
		return;
	// Dx/Dy 是原始鼠标增量（窗口像素，未经 UI/菜单灵敏度换算）：按当前缩放 1:1 映射成
	// 世界位移，也就是"世界跟着光标走"的手感。不能用 Ui()->ConvertMouseMove 之后再算——
	// 那套走的是菜单灵敏度（默认 ui_mousesens=200，等于双倍），会明显过快。
	float WorldWidth, WorldHeight;
	Graphics()->CalcScreenParams(Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom, &WorldWidth, &WorldHeight);
	const float WindowWidth = maximum(1.0f, (float)Graphics()->WindowWidth());
	m_ViewFreeCameraCenter += vec2(Dx, Dy) * (WorldWidth / WindowWidth);
}

bool CRankGhost::ViewFocusAllMembers(vec2 *pCenter, vec2 *pSize) const
{
	if(!IsViewModeActive() || m_vLoadedSlots.empty())
		return false;
	bool HasAny = false;
	vec2 Min(0.0f, 0.0f), Max(0.0f, 0.0f);
	for(size_t i = 0; i < m_vLoadedSlots.size(); i++)
	{
		vec2 Pos;
		if(!GameClient()->m_Ghost.GetManualRenderPos(m_vLoadedSlots[i], &Pos))
			continue;
		if(!HasAny)
		{
			Min = Pos;
			Max = Pos;
			HasAny = true;
			continue;
		}
		Min = vec2(minimum(Min.x, Pos.x), minimum(Min.y, Pos.y));
		Max = vec2(maximum(Max.x, Pos.x), maximum(Max.y, Pos.y));
	}
	if(!HasAny)
		return false;
	*pCenter = (Min + Max) * 0.5f;
	*pSize = Max - Min;
	return true;
}

bool CRankGhost::ViewMemberName(int Index, char *pBuf, size_t BufSize) const
{
	if(Index < 0 || Index >= (int)m_vLoadedSlots.size())
		return false;
	return GameClient()->m_Ghost.GetGhostPlayer(m_vLoadedSlots[Index], pBuf, BufSize);
}

bool CRankGhost::ViewFocus(vec2 *pOut) const
{
	if(!IsViewModeActive() || m_ViewCameraMode != EViewCameraMode::MEMBER || m_vLoadedSlots.empty())
		return false;
	const int Idx = std::clamp(m_ViewSelected, 0, (int)m_vLoadedSlots.size() - 1);
	return GameClient()->m_Ghost.GetManualRenderPos(m_vLoadedSlots[Idx], pOut);
}

void CRankGhost::UnloadGhost()
{
	m_LastAlignedRaceTick = -1;
	m_ViewMode = false;
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
}

void CRankGhost::OnDemoPlayerSnapshot(void *pData, int Size)
{
	(void)Size;
	if(!m_pParse || !m_pParse->m_Context.m_pPlayer)
		return;

	SParseContext &Parse = m_pParse->m_Context;
	const CSnapshot *pSnapshot = (const CSnapshot *)pData;
	const int Tick = Parse.m_pPlayer->Info()->m_Info.m_CurrentTick;

	// 皮肤/名字可能在 run 开始前就已出现，尽早抓取
	ExtractClientInfo(Parse, pSnapshot, Parse.m_Cid, Parse.m_Tracks[Parse.m_Cid]);

	if(Tick < Parse.m_RunStart)
		return;
	if(Tick > Parse.m_LastSeenTick)
		Parse.m_LastSeenTick = Tick;
	if(Parse.m_HasRunMarkers)
	{
		if(Tick >= Parse.m_RunEnd)
			Parse.m_ReachedRunEnd = true;
		if(Tick > Parse.m_RunEnd)
		{
			Parse.m_pPlayer->Pause();
			return;
		}
	}

	// run 区间内顺手记录交互事件与开关状态，供查看模式回放
	RecordViewTimeline(pSnapshot, Tick - Parse.m_RunStart);

	ExtractCharacter(Parse, pSnapshot, Tick);
}

void CRankGhost::OnDemoPlayerMessage(void *pData, int Size)
{
	// 轨迹来自 snapshot，但终点成绩/差值、检查点差值与全局音效只存在于消息流里。
	// 这里按消息类型挑出这几类记进查看时间线，其余消息一律忽略。
	if(!m_pParse || !m_pParse->m_Context.m_pPlayer)
		return;
	SParseContext &Parse = m_pParse->m_Context;

	CUnpacker Unpacker;
	Unpacker.Reset(pData, Size);
	CMsgPacker Packer(NETMSG_EX, true);

	int Msg;
	bool Sys;
	CUuid Uuid;
	if(UnpackMessageId(&Msg, &Sys, &Uuid, &Unpacker, &Packer) == UNPACKMESSAGE_ERROR || Sys)
		return;

	// 消息按 tick 顺序到达。终点成绩可能紧跟在 run 结束标记之后（服务端在终点帧
	// 才发送），因此允许越过终点两个 tick，并把相对 tick 钳在轨迹长度内，
	// 否则播放头永远到不了这条记录。
	const int Tick = Parse.m_pPlayer->Info()->m_Info.m_CurrentTick;
	if(Tick < Parse.m_RunStart || Tick > Parse.m_RunEnd + 2)
		return;
	const int RelTick = minimum(Tick - Parse.m_RunStart, Parse.m_RunEnd - Parse.m_RunStart);

	if(Parse.m_pPlayer->IsSixup())
	{
		// 0.7：Sv_RaceFinish 是普通消息，检查点差值走 Sv_Checkpoint。
		// 官方服务端用 MSGFLAG_NORECORD 发送 Sv_RaceFinish（见 CGameContext::SendFinish），
		// 这种消息不会进 demo；保留分支是为兼容不设该标志的服务端。
		if(Msg == protocol7::NETMSGTYPE_SV_RACEFINISH)
		{
			const protocol7::CNetMsg_Sv_RaceFinish *pMsg = (const protocol7::CNetMsg_Sv_RaceFinish *)Parse.m_NetObjHandler7.SecureUnpackMsg(Msg, &Unpacker);
			if(pMsg == nullptr || pMsg->m_ClientId != Parse.m_Cid)
				return;
			const int aData[5] = {pMsg->m_ClientId, pMsg->m_Time, pMsg->m_Diff, pMsg->m_RecordPersonal ? 1 : 0, pMsg->m_RecordServer ? 1 : 0};
			RecordViewMessage(RelTick, EViewMessageType::RACE_FINISH, aData, (int)std::size(aData));
		}
		else if(Msg == protocol7::NETMSGTYPE_SV_CHECKPOINT)
		{
			const protocol7::CNetMsg_Sv_Checkpoint *pMsg = (const protocol7::CNetMsg_Sv_Checkpoint *)Parse.m_NetObjHandler7.SecureUnpackMsg(Msg, &Unpacker);
			if(pMsg == nullptr)
				return;
			// 与 sixup_translate_game 的换算一致：0.7 的 m_Diff 是毫秒，0.6 的 m_Check 是厘秒。
			// 0.7 没有对应的成绩时间，m_Time 用占位值（只有 Finish 消息的 m_Time 有意义）。
			const int aData[5] = {10, pMsg->m_Diff / 10, 0, 0, 0};
			RecordViewMessage(RelTick, EViewMessageType::RACE_TIME, aData, 3);
		}
		return;
	}

	if(Msg == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = (const CNetMsg_Sv_RaceFinish *)Parse.m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
		// 只关心主选手（回放主人）的终点；其他参与者的成绩不驱动 Finish HUD
		if(pMsg == nullptr || pMsg->m_ClientId != Parse.m_Cid)
			return;
		const int aData[5] = {pMsg->m_ClientId, pMsg->m_Time, pMsg->m_Diff, pMsg->m_RecordPersonal ? 1 : 0, pMsg->m_RecordServer ? 1 : 0};
		RecordViewMessage(RelTick, EViewMessageType::RACE_FINISH, aData, (int)std::size(aData));
	}
	else if(Msg == NETMSGTYPE_SV_DDRACETIME)
	{
		const CNetMsg_Sv_DDRaceTime *pMsg = (const CNetMsg_Sv_DDRaceTime *)Parse.m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
		if(pMsg == nullptr)
			return;
		// 原生 HUD 用 TIME_HOURS_CENTISECS 显示 m_Time、用 m_Check / 100 算差值，
		// 这里统一把成绩换算成毫秒、差值保持厘秒，与 RACE_FINISH 的毫秒区分开
		const int aData[5] = {pMsg->m_Time * 10, pMsg->m_Check, pMsg->m_Finish, 0, 0};
		RecordViewMessage(RelTick, EViewMessageType::RACE_TIME, aData, 3);
	}
	else if(Msg == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		const CNetMsg_Sv_DDRaceTimeLegacy *pMsg = (const CNetMsg_Sv_DDRaceTimeLegacy *)Parse.m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
		if(pMsg == nullptr)
			return;
		const int aData[5] = {pMsg->m_Time * 10, pMsg->m_Check, pMsg->m_Finish, 0, 0};
		RecordViewMessage(RelTick, EViewMessageType::RACE_TIME, aData, 3);
	}
	else if(Msg == NETMSGTYPE_SV_SOUNDGLOBAL)
	{
		const CNetMsg_Sv_SoundGlobal *pMsg = (const CNetMsg_Sv_SoundGlobal *)Parse.m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
		if(pMsg == nullptr)
			return;
		const int aData[5] = {pMsg->m_SoundId, 0, 0, 0, 0};
		RecordViewMessage(RelTick, EViewMessageType::SOUND_GLOBAL, aData, 1);
	}
	else if(Msg == NETMSGTYPE_SV_MAPSOUNDGLOBAL)
	{
		const CNetMsg_Sv_MapSoundGlobal *pMsg = (const CNetMsg_Sv_MapSoundGlobal *)Parse.m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
		if(pMsg == nullptr)
			return;
		const int aData[5] = {pMsg->m_SoundId, 0, 0, 0, 0};
		RecordViewMessage(RelTick, EViewMessageType::MAP_SOUND_GLOBAL, aData, 1);
	}
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
