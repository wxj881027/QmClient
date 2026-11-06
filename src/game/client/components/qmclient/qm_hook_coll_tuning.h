#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_TUNING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_TUNING_H

#include <engine/shared/protocol.h>

#include <optional>

// 未预测玩家沿用本地角色调参；没有本地角色时，由调用方使用连接调参。
inline std::optional<int> QmHookCollTuningClient(bool IsPredicted, int RenderedId, int LocalId)
{
	const int Id = IsPredicted ? RenderedId : LocalId;
	if(Id < 0 || Id >= MAX_CLIENTS)
		return std::nullopt;
	return Id;
}

#endif
