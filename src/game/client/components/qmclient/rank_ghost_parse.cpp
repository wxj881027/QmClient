/* Copyright © 2026 BestProject Team */
// Rank 1 回放的 demo 解析：快照解码、按 cid 分轨、生成 .gho 与查看时间线。
// 与清单拉取、查看模式分开，解析状态只在这里维护。

#include "rank_ghost_parse.h"

#include "ghost_team_metadata.h"
#include "rank_demo_cache.h"
#include "rank_view_timeline.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/client/ghost.h>
#include <engine/shared/compression.h>
#include <engine/shared/demo_output.h>
#include <engine/shared/network.h>
#include <engine/shared/snapshot.h>
#include <engine/storage.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/components/ghost.h>
#include <game/client/gameclient.h>
#include <game/gamecore.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	constexpr auto PARSE_TIME_BUDGET = std::chrono::milliseconds(5);
	constexpr int MIN_PATH_TICKS = 25;

	using qmclient::rank_demo::ListCacheFiles;
	using qmclient::rank_demo::SCacheNameTime;

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
			m_File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
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
			const bool Flushed = FlushChunk() && io_flush(m_File) == 0 && io_error(m_File) == 0;
			const bool Closed = io_close(std::exchange(m_File, nullptr)) == 0;
			m_Failed |= !Flushed || !Closed;
			return !m_Failed;
		}
	};
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
		// m_VelY 专用于显式冻结标记：转换产物不再用 WEAPON_NINJA 冒充冻结，
		// 真实忍者帧因此能正常显示；旧版 .gho 才回退到武器判定。
		Out.m_VelY = QmEncodeGhostFreezeMarker(pDDNetChar != nullptr ? QmGhostFreezeTicksFromEnd(pDDNetChar->m_FreezeEnd, GhostTick) : 0);
		Out.m_Angle = Char.m_Angle;
		Out.m_Direction = QmEncodeGhostDirection(Char.m_Direction, pDDNetChar != nullptr ? pDDNetChar->m_StrongWeakId : 0);
		Out.m_Weapon = Char.m_Weapon;
		Out.m_HookState = Char.m_HookState;
		Out.m_HookX = Char.m_HookX;
		Out.m_HookY = Char.m_HookY;
		Out.m_AttackTick = Char.m_AttackTick;
		Out.m_Tick = GhostTick;
	}

	// 队伍只写 sidecar，不改 .gho 二进制布局；旧缓存没有这份文件时按未知队伍处理。
	bool WriteGhostTeamMetadataForTracks(IStorage *pStorage, const char *pPath, bool Group, const std::vector<STrackContext *> &vpTracks, const SParseContext &Parse)
	{
		std::vector<SGhostTeamEntry> vEntries;
		for(size_t i = 0; i < vpTracks.size(); i++)
		{
			const STrackContext *pTrack = vpTracks[i];
			const int Team = pTrack->m_Cid >= 0 && pTrack->m_Cid < MAX_CLIENTS ? Parse.m_aTeams[pTrack->m_Cid] : -1;
			if(Team < TEAM_FLOCK || Team >= NUM_DDRACE_TEAMS)
				continue;
			char aFilename[IO_MAX_PATH_LENGTH];
			if(Group)
				str_format(aFilename, sizeof(aFilename), "%02d.gho", (int)i);
			else
				str_copy(aFilename, QmGhostBasename(pPath).c_str(), sizeof(aFilename));
			vEntries.push_back({aFilename, Team});
		}

		char aMetadataPath[IO_MAX_PATH_LENGTH];
		if(Group)
			str_format(aMetadataPath, sizeof(aMetadataPath), "%s/%s", pPath, QmGhostTeamMetadataName());
		else
			str_copy(aMetadataPath, QmGhostTeamMetadataForSingleFile(pPath).c_str(), sizeof(aMetadataPath));
		return QmWriteGhostTeamMetadata(pStorage, aMetadataPath, vEntries);
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
				if(Parse.m_RestrictGhosts && std::find(Parse.m_vAllowedGhosts.begin(), Parse.m_vAllowedGhosts.end(), Cid) == Parse.m_vAllowedGhosts.end())
					continue;
				if((int)Parse.m_Tracks.size() >= SParseContext::MAX_TRACKS)
					continue;
				// 观战者不参与跑图，直接排除（避免静止 Tee 混入轨迹）
				if(IsSpectatorCid(Parse, pSnapshot, Cid))
					continue;
				TrackIt = Parse.m_Tracks.emplace(Cid, STrackContext{}).first;
				TrackIt->second.m_Cid = Cid;
				TrackIt->second.m_vPath.reserve((size_t)std::clamp(Parse.m_RunEnd - Parse.m_RunStart, 0, 60 * 60 * 50));
			}
			STrackContext &Track = TrackIt->second;
			ExtractClientInfo(Parse, pSnapshot, Cid, Track);

			CNetObj_Character Char;
			const CNetObj_DDNetCharacter *pDDNetChar = nullptr;
			if(!UnpackCharacter(Parse, pSnapshot, Cid, Char, pDDNetChar))
				continue;
			CGhostCharacter GhostChar;
			CopyGhostCharacter(GhostChar, Char, pDDNetChar, Tick);
			Track.m_vPath.push_back(GhostChar);
			Track.m_FoundCharacter = true;
		}
	}

} // namespace

SParseContext::SParseContext()
{
	// 与上游 demo_extract_chat 工具一致：snapshot delta 需要按对象类型设置静态大小
	CNetObjHandler NetObjHandler;
	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		m_Delta.SetStaticsize(i, NetObjHandler.GetObjSize(i));

	protocol7::CNetObjHandler NetObjHandler7;
	// HACK: 只设置 0.7 首个版本存在的对象，避免新对象破坏 snapshot delta
	static const int OLD_NUM_NETOBJTYPES = 23;
	for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
		m_DeltaSixup.SetStaticsize(i, NetObjHandler7.GetObjSize(i));
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
	// 队伍消息缺失时保持 -1：回放阶段回落到查看者当前队伍，而不是误判成 flock。
	std::fill(std::begin(Parse.m_aTeams), std::end(Parse.m_aTeams), -1);
	Parse.m_RestrictGhosts = m_ActiveEntry.m_HasGhosts;
	Parse.m_vAllowedGhosts = m_ActiveEntry.m_vGhosts;
	if(Parse.m_RestrictGhosts)
	{
		if(Parse.m_vAllowedGhosts.empty())
		{
			m_pParse.reset();
			if(!m_EventRebuildOnly)
				Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
			Fail(Localize("Rank ghost: this replay has no playable tee"));
			return;
		}
		if(std::find(Parse.m_vAllowedGhosts.begin(), Parse.m_vAllowedGhosts.end(), Parse.m_Cid) == Parse.m_vAllowedGhosts.end())
			Parse.m_Cid = Parse.m_vAllowedGhosts.front();
		m_ActiveEntry.m_Cid = Parse.m_Cid;
	}
	STrackContext &Primary = Parse.m_Tracks[Parse.m_Cid];
	Primary.m_Cid = Parse.m_Cid;
	StrToInts(Primary.m_Skin.m_aSkin, std::size(Primary.m_Skin.m_aSkin), "default");
	Primary.m_Skin.m_UseCustomColor = 0;
	Primary.m_Skin.m_ColorBody = 0;
	Primary.m_Skin.m_ColorFeet = 0;

	Parse.m_pPlayer = std::make_unique<CDemoPlayer>(&Parse.m_Delta, &Parse.m_DeltaSixup, false);

	if(Parse.m_pPlayer->Load(Storage(), m_aDemoStoragePath, IStorage::TYPE_SAVE, false) == -1)
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), Localize("Rank ghost: failed to load the replay: %s"), Parse.m_pPlayer->ErrorMessage());
		// CDemoPlayer 析构要求文件已关闭，否则 dbg_assert 在 Release 下也会 abort
		Parse.m_pPlayer->Stop();
		m_pParse.reset();
		if(!m_EventRebuildOnly)
			Storage()->RemoveFile(m_aDemoStoragePath, IStorage::TYPE_SAVE);
		Fail(aBuf);
		return;
	}
	Parse.m_pPlayer->SetListener(this);

	const IDemoPlayer::CInfo *pInfo = Parse.m_pPlayer->BaseInfo();
	Parse.m_HasRunMarkers = pInfo->m_NumTimelineMarkers >= 2;
	if(Parse.m_HasRunMarkers)
	{
		Parse.m_RunStart = pInfo->m_aTimelineMarkers[0];
		Parse.m_RunEnd = pInfo->m_aTimelineMarkers[1];
		if(Parse.m_RunStart < 0 || Parse.m_RunEnd <= Parse.m_RunStart)
		{
			Parse.m_pPlayer->Stop();
			m_pParse.reset();
			if(!m_EventRebuildOnly)
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
		Parse.m_RunStart = maximum(0, pInfo->m_FirstTick);
		Parse.m_RunEnd = maximum(pInfo->m_LastTick, Parse.m_RunStart < std::numeric_limits<int>::max() ? Parse.m_RunStart + 1 : Parse.m_RunStart);
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
	const auto Deadline = time_get_nanoseconds() + PARSE_TIME_BUDGET;
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
		if(Processed >= PARSE_TICKS_PER_FRAME || time_get_nanoseconds() >= Deadline)
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
	const int64_t RunTicks = static_cast<int64_t>(RunEnd) - Parse.m_RunStart;
	const int64_t DurationMs = RunTicks * 1000 / SERVER_TICK_SPEED;
	const int TimeMs = DurationMs > 0 && DurationMs <= std::numeric_limits<int>::max() ? static_cast<int>(DurationMs) : 0;
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
		bool Published = false;
		try
		{
			demo_output::CTransaction Output;
			if(Output.Prepare(*Storage(), m_aGhostStoragePath))
			{
				str_copy(m_aGhostWritePath, Output.Filename(), sizeof(m_aGhostWritePath));
				CGhostFileWriter Writer;
				Published = WriteTrack(Writer, vpTracks[0], static_cast<int>(vpTracks[0]->m_vPath.size())) && Output.Publish();
			}
		}
		catch(...)
		{
		}
		if(!Published)
		{
			Fail(Localize("Rank ghost: failed to write the ghost file"));
			return;
		}
		if(!WriteGhostTeamMetadataForTracks(Storage(), m_aGhostStoragePath, false, vpTracks, Parse))
			log_error("rank_ghost", "failed to write ghost team metadata for '%s'", m_aGhostStoragePath);
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
			auto pWriter = std::make_unique<CGhostFileWriter>();
			if(!WriteTrack(*pWriter, vpTracks[i], NumTicks))
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
		if(!WriteGhostTeamMetadataForTracks(Storage(), aGroupDir, true, vpTracks, Parse))
			log_error("rank_ghost", "failed to write ghost team metadata for '%s'", aGroupDir);
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
	RecordViewTimeline(pSnapshot, Tick - Parse.m_RunStart, Parse.m_aTeams[Parse.m_Cid]);

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
	if(UnpackMessageId(&Msg, &Sys, &Unpacker, &Packer) == UNPACKMESSAGE_ERROR || Sys)
		return;
	if(Msg == NETMSGTYPE_SV_TEAMSSTATE || Msg == NETMSGTYPE_SV_TEAMSSTATELEGACY)
	{
		// demo 的开关状态不携带队伍号，必须从同一时间线的队伍消息恢复。
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const int Team = Unpacker.GetInt();
			if(Unpacker.Error())
				break;
			Parse.m_aTeams[i] = Team >= TEAM_FLOCK && Team < NUM_DDRACE_TEAMS ? Team : -1;
		}
		return;
	}

	// 消息按 tick 顺序到达。终点成绩可能紧跟在 run 结束标记之后（服务端在终点帧
	// 才发送），因此允许越过终点两个 tick，并把相对 tick 钳在轨迹长度内，
	// 否则播放头永远到不了这条记录。
	const int Tick = Parse.m_pPlayer->Info()->m_Info.m_CurrentTick;
	if(Tick < Parse.m_RunStart || static_cast<int64_t>(Tick) > static_cast<int64_t>(Parse.m_RunEnd) + 2)
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
	else if(Msg == NETMSGTYPE_SV_DDRACETIME || Msg == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		const auto RecordRaceTime = [&](const auto *pMsg) {
			if(pMsg == nullptr)
				return;
			const int64_t TimeMs = static_cast<int64_t>(pMsg->m_Time) * 10;
			if(TimeMs < std::numeric_limits<int>::min() || TimeMs > std::numeric_limits<int>::max())
				return;
			const int aData[] = {static_cast<int>(TimeMs), pMsg->m_Check, pMsg->m_Finish};
			RecordViewMessage(RelTick, EViewMessageType::RACE_TIME, aData, static_cast<int>(std::size(aData)));
		};
		const void *pMessage = Parse.m_NetObjHandler.SecureUnpackMsg(Msg, &Unpacker);
		if(Msg == NETMSGTYPE_SV_DDRACETIME)
			RecordRaceTime(static_cast<const CNetMsg_Sv_DDRaceTime *>(pMessage));
		else
			RecordRaceTime(static_cast<const CNetMsg_Sv_DDRaceTimeLegacy *>(pMessage));
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
