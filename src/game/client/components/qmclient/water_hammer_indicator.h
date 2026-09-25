#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_H

#include "water_hammer_indicator_logic.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <array>

// 接收 sv_preinput，并为渲染层提供「同队可见玩家正在水里卡锤」状态。
class CQmWaterHammerIndicator : public CComponent
{
	struct SInputState
	{
		bool m_Received = false;
		int m_WantedWeapon = 0;
		int m_Fire = 0;
	};

	std::array<SInputState, MAX_CLIENTS> m_aInputs;

	bool IsInPenaltyArea(vec2 Position) const;
	bool IsClientInPenaltyArea(int ClientId) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnNewSnapshot() override;

	void OnPreInput(const CNetMsg_Sv_PreInput &Message);
	bool IsMarked(int ClientId) const;
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_H
