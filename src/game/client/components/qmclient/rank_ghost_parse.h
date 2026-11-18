#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_GHOST_PARSE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_GHOST_PARSE_H

// rank_ghost 解析阶段的内部状态。
// 只被 rank_ghost.cpp 与 rank_ghost_parse.cpp 使用，不进入公开头文件。
// SParseContext 持有 demo 播放器与协议处理器，定义放这里是为了让两个
// 翻译单元都能看到完整类型；解析函数与助手仍留在 rank_ghost_parse.cpp。

#include "rank_ghost.h"

#include <base/hash.h>

#include <engine/client/ghost.h>
#include <engine/shared/demo.h>
#include <engine/shared/network.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/components/ghost.h>

#include <map>
#include <memory>
#include <vector>

// 单个参与者的轨迹与外观（主选手与多轨模式下的其他 Tee 共用）
struct STrackContext
{
	std::vector<CGhostCharacter> m_vPath;
	CGhostSkin m_Skin{};
	char m_aOwner[MAX_NAME_LENGTH] = "";
	int m_Cid = -1;
	bool m_FoundClientInfo = false;
	bool m_FoundCharacter = false;
};

// demo 解析期间的状态，独立于主客户端，避免打断正常游戏
struct SParseContext
{
	CSnapshotDelta m_Delta;
	CSnapshotDelta m_DeltaSixup;
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
	bool m_RestrictGhosts = false;
	std::vector<int> m_vAllowedGhosts;
	int m_aTeams[MAX_CLIENTS] = {};
	// 观战者判定缓存（cid → 是否观战）
	std::map<int, bool> m_aSpectatorCache;
	// 是否在解析中看到过 run 终点之后的快照，用于识别被截断的回放
	bool m_ReachedRunEnd = false;

	static constexpr int MAX_TRACKS = 64;

	SParseContext();
};

// 头文件中前向声明的解析状态
struct CRankGhost::SParseState
{
	SParseContext m_Context;
};

#endif
