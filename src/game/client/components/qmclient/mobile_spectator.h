#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_SPECTATOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MOBILE_SPECTATOR_H

#include "mobile_spectator_controls.h"

#include <game/client/component.h>

class CQmMobileSpectator : public CComponent
{
	qm_mobile_spectator::CTouchState m_Touch;
	qm_mobile_spectator::CJoinState m_Join;
	bool ControlsAvailable() const;
	bool FreeViewActive() const;
	vec2 ScreenSize() const;
	void PerformAction(int Action);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRelease() override { m_Touch.Cancel(); }
	void OnWindowResize() override { m_Touch.Cancel(); }
	void OnStateChange(int NewState, int OldState) override { OnReset(); }
	void OnNewSnapshot() override;
	bool OnTouchState(std::vector<IInput::CTouchFingerState> &vFingers) override;
	void OnRender() override;
	void ShowMediaFiles(bool Videos);
};

#endif
