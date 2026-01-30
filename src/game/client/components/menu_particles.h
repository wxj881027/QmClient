#ifndef GAME_CLIENT_COMPONENTS_MENU_PARTICLES_H
#define GAME_CLIENT_COMPONENTS_MENU_PARTICLES_H

#include <base/color.h>
#include <base/vmath.h>
#include <game/client/component.h>
#include <vector>

// Particle system for menu backgrounds with multiple visual effects
class CMenuParticles : public CComponent
{
public:
	enum EParticleType
	{
		PARTICLE_STAR = 0,
		PARTICLE_CIRCLE,
		PARTICLE_SPARKLE,
		PARTICLE_GLOW,
		NUM_PARTICLE_TYPES
	};

	enum EEffectMode
	{
		EFFECT_NONE = 0,
		EFFECT_RAINBOW,
		EFFECT_PULSE,
		EFFECT_WAVE,
		EFFECT_SPIRAL,
		EFFECT_METEOR,
		NUM_EFFECT_MODES
	};

private:
	struct CParticle
	{
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Size;
		float m_Life;
		float m_MaxLife;
		float m_Rotation;
		float m_RotationSpeed;
		ColorRGBA m_Color;
		EParticleType m_Type;
		float m_Phase; // For wave/pulse effects
	};

	std::vector<CParticle> m_vParticles;
	float m_Time;
	float m_SpawnTimer;
	bool m_Enabled;

	void SpawnParticle();
	void UpdateParticle(CParticle &Particle, float TimePassed);
	void RenderParticle(const CParticle &Particle);
	ColorRGBA GetEffectColor(float Phase);

public:
	CMenuParticles();
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnReset() override;

	void SetEnabled(bool Enabled) { m_Enabled = Enabled; }
	bool IsEnabled() const { return m_Enabled; }
};

#endif
