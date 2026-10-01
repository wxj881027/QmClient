#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TRAILS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TRAILS_H

#include <base/color.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/components/tclient/qm_tee_trail.h>

class CTrails : public CComponent
{
public:
	CTrails() = default;
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;
	void OnNewSnapshot() override;

	enum COLORMODES
	{
		COLORMODE_SOLID = 1,
		COLORMODE_TEE,
		COLORMODE_RAINBOW,
		COLORMODE_SPEED,
		COLORMODE_RANDOM,
	};

private:
	qm_tee_trail::CTrailState m_aTrailStates[MAX_CLIENTS];
	qm_tee_trail::CTrailClock m_Clock;
	int m_aPositionSources[MAX_CLIENTS] = {};
	std::vector<CTrailPart> m_vTrail;
	std::vector<qm_tee_trail::SQuad> m_vQuads;
	int m_LastDummy = -1;
	int m_LastStyle = -1;
	int m_LastLength = -1;
	void RenderTeeTrails();
};

#endif
