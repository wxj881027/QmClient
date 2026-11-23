#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_H

#include "water_hammer_indicator_logic.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <array>

// 冻结时读取本机角色的输入，其他玩家的按键状态取同队 sv_preinput。
class CQmWaterHammerIndicator : public CComponent
{
	std::array<SQmWaterHammerInputState, MAX_CLIENTS> m_aInputs;

	bool CanUseRemoteInput(int ClientId) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnNewSnapshot() override;

	void OnPreInput(const CNetMsg_Sv_PreInput &Message);
	bool IsInPenaltyArea(vec2 Position) const;
	SQmWaterHeldState MarkedInputs(int ClientId) const;
	SQmWaterDirectionState DirectionInputs(int ClientId) const;
	std::optional<ColorRGBA> HighlightColor(int ClientId) const;
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_WATER_HAMMER_INDICATOR_H
