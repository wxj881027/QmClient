#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_INPUT_OVERLAY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_INPUT_OVERLAY_H

#include <game/client/component.h>

class CInputOverlay : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
