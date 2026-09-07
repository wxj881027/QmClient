/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_ADAPTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_AUTO_TEAM_LOCK_QM_AUTO_TEAM_LOCK_ADAPTER_H

#include "qm_auto_team_lock_logic.h"

class CGameClient;
class IClient;

SQmAutoTeamLockInput BuildQmAutoTeamLockInput(const CGameClient &GameClient, const IClient &Client);
void ApplyQmAutoTeamLockAction(CGameClient &GameClient, const SQmAutoTeamLockAction &Action);

#endif
