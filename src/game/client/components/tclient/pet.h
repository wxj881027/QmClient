#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_PET_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_PET_H

#include <game/client/component.h>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

// 准备失败只隐藏本帧；绘制开始后的设备异常保持传播，不尝试恢复失败批次。
template<typename TPrepare, typename TDraw>
bool QmTClientPetRenderFrame(int &FollowedClientId, TPrepare &&Prepare, TDraw &&Draw)
{
	try
	{
		std::forward<TPrepare>(Prepare)();
	}
	catch(const std::bad_alloc &)
	{
		FollowedClientId = -1;
		return false;
	}
	catch(const std::length_error &)
	{
		FollowedClientId = -1;
		return false;
	}
	std::forward<TDraw>(Draw)();
	return true;
}

inline void QmTClientPetAdvanceSpring(vec2 &Position, vec2 &Velocity, vec2 Target, float Delta)
{
	Position += Velocity * Delta;
	const vec2 DeltaPosition = Target - Position;
	const float DeltaLength = length(DeltaPosition);
	constexpr float SpringStrength = 50.0f;
	const vec2 DeltaDamped = (Velocity * -2.0f * std::sqrt(SpringStrength) + DeltaPosition * SpringStrength) * Delta;
	constexpr float Friction = 0.01f;
	const vec2 DeltaWizzy = (Velocity + DeltaPosition * Delta * 50.0f) * std::pow(Friction, Delta) - Velocity;
	Velocity += mix(DeltaDamped, DeltaWizzy, std::clamp(DeltaLength / 64.0f, 0.0f, 1.0f));
}

class CPet : public CComponent
{
private:
	vec2 m_Target;
	vec2 m_Position;
	vec2 m_Velocity;
	vec2 m_Dir;
	float m_Alpha = 0.0f;
	int m_FollowedClientId = -1;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnMapLoad() override;
	bool IsVisibleForClient(int ClientId) const { return ClientId >= 0 && m_FollowedClientId == ClientId && m_Alpha > 0.0f; }
	vec2 Position() const { return m_Position; }
};

#endif
