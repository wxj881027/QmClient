/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_PLAYER_INDICATOR_QM_PLAYER_INDICATOR_ADAPTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_PLAYER_INDICATOR_QM_PLAYER_INDICATOR_ADAPTER_H

#include "qm_player_indicator.h"

class CGameClient;
class IClient;
class IGraphics;

bool QmPlayerIndicatorAvailable(const CGameClient &GameClient, const IClient &Client);
SQmPlayerIndicatorFrame BuildQmPlayerIndicatorFrame(const CGameClient &GameClient, const IClient &Client, const IGraphics &Graphics);

#endif
