/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_JELLY_TEE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_JELLY_TEE_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <array>

class CGameClient;

struct SQmJellyDeform
{
	vec2 m_BodyScale = vec2(1.0f, 1.0f);
	vec2 m_FeetScale = vec2(1.0f, 1.0f);
	float m_BodyAngle = 0.0f;
	float m_FeetAngle = 0.0f;
};

class CQmJelly
{
public:
	explicit CQmJelly(CGameClient *pClient);

	void Reset();
	SQmJellyDeform GetDeform(int ClientId, vec2 PrevVel, vec2 Vel, vec2 LookDir, bool InAir, bool WantOtherDir, float DeltaTime, vec2 ExtraDeformImpulse = vec2(0.0f, 0.0f), float ExtraCompression = 0.0f);

private:
	static constexpr int MAX_JELLY_CLIENTS = MAX_CLIENTS;

	struct CState
	{
		vec2 m_Deform = vec2(0.0f, 0.0f);
		vec2 m_DeformVelocity = vec2(0.0f, 0.0f);
		vec2 m_PrevInputVel = vec2(0.0f, 0.0f);
		float m_Compression = 0.0f;
		float m_CompressionVelocity = 0.0f;
		int m_ClientId = -1;
		bool m_Initialized = false;
	};

	CGameClient *m_pClient = nullptr;
	std::array<CState, MAX_JELLY_CLIENTS> m_aStates{};

	bool IsEnabledFor(int ClientId) const;
	CState *StateFor(int ClientId);
};

#endif
