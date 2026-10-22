#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_H

#include "checkpoint_guide_logic.h"

#include <engine/client/enums.h>

#include <game/client/component.h>

#include <array>
#include <cstdint>

class CQmCheckpointGuide : public CComponent
{
	struct SPlayer
	{
		QmCheckpointGuide::CPlanner m_Planner;
		int m_ClientId = -1;
		int64_t m_NextSearch = 0;
	};
	std::array<SPlayer, NUM_DUMMIES> m_aPlayers;
	bool m_WasEnabled = false;

	void ObservePlayers();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;
};

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_CHECKPOINT_GUIDE_H
