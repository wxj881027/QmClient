/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_GAME_STATE_ADAPTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CORE_QM_GAME_STATE_ADAPTER_H

#include "../features/player_indicator/qm_player_indicator.h"

class CGameClient;
class IClient;
class IGraphics;

// 只在官方 adapter 中读取 CGameClient 内部状态，runtime 和 feature 只消费窄 frame。
bool QmPlayerIndicatorAvailable(const CGameClient &GameClient, const IClient &Client);
SQmPlayerIndicatorFrame BuildQmPlayerIndicatorFrame(const CGameClient &GameClient, const IClient &Client, const IGraphics &Graphics);

#endif
