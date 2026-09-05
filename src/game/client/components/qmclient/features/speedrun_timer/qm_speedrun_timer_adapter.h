/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_SPEEDRUN_TIMER_QM_SPEEDRUN_TIMER_ADAPTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_SPEEDRUN_TIMER_QM_SPEEDRUN_TIMER_ADAPTER_H

#include "qm_speedrun_timer_logic.h"

class CGameClient;
class IClient;

SQmSpeedrunTimerInput BuildQmSpeedrunTimerInput(const CGameClient &GameClient, const IClient &Client);

#endif
