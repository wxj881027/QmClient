#include "menu_particles.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <game/client/gameclient.h>

#include <cmath>

CMenuParticles::CMenuParticles()
{
	m_Time = 0.0f;
	m_SpawnTimer = 0.0f;
	m_Enabled = true;
	m_vParticles.reserve(200);
}

void CMenuParticles::OnReset()
{
	m_vParticles.clear();
	m_Time = 0.0f;
	m_SpawnTimer = 0.0f;
}

void CMenuParticles::OnRender()
{
	if(!m_Enabled || !g_Config.m_ClMenuParticles)
		return;

	const float TimePassed = Client()->RenderFrameTime();
	m_Time += TimePassed;
	m_SpawnTimer += TimePassed;

	// Spawn new particles
	const float SpawnRate = 0.05f; // Spawn every 50ms
	while(m_SpawnTimer >= SpawnRate && m_vParticles.size() < 150)
	{
		SpawnParticle();
		m_SpawnTimer -= SpawnRate;
	}

	// Update and render particles
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	for(auto it = m_vParticles.begin(); it != m_vParticles.end();)
	{
		UpdateParticle(*it, TimePassed);
		
		if(it->m_Life <= 0.0f)
		{
			it = m_vParticles.erase(it);
		}
		else
		{
			RenderParticle(*it);
			++it;
		}
	}

	Graphics()->QuadsEnd();
}

void CMenuParticles::SpawnParticle()
{
	CParticle Particle;
	
	// Get screen dimensions
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	
	// Random spawn position
	Particle.m_Pos.x = ScreenX0 + (ScreenX1 - ScreenX0) * (rand() % 1000) / 999.0f;
	Particle.m_Pos.y = ScreenY0 + (ScreenY1 - ScreenY0) * (rand() % 1000) / 999.0f;
	
	// Random velocity based on effect mode
	const float Speed = 20.0f + (rand() % 100) / 10.0f;
	const float Angle = (rand() % 360) * 3.14159f / 180.0f;
	
	switch(g_Config.m_ClMenuParticleEffect)
	{
	case EFFECT_METEOR:
		// Meteors fall diagonally
		Particle.m_Vel.x = -Speed * 0.5f;
		Particle.m_Vel.y = Speed;
		Particle.m_Pos.x = ScreenX1 + (rand() % 200);
		Particle.m_Pos.y = ScreenY0 - (rand() % 100);
		break;
	case EFFECT_SPIRAL:
		// Spiral outward from center
		{
			const float CenterX = (ScreenX0 + ScreenX1) * 0.5f;
			const float CenterY = (ScreenY0 + ScreenY1) * 0.5f;
			Particle.m_Pos.x = CenterX;
			Particle.m_Pos.y = CenterY;
			Particle.m_Vel.x = std::cos(m_Time * 2.0f) * Speed;
			Particle.m_Vel.y = std::sin(m_Time * 2.0f) * Speed;
		}
		break;
	default:
		// Random direction
		Particle.m_Vel.x = std::cos(Angle) * Speed;
		Particle.m_Vel.y = std::sin(Angle) * Speed;
		break;
	}
	
	// Random size
	Particle.m_Size = 2.0f + (rand() % 60) / 10.0f;
	
	// Lifetime
	Particle.m_Life = 3.0f + (rand() % 200) / 100.0f;
	Particle.m_MaxLife = Particle.m_Life;
	
	// Rotation
	Particle.m_Rotation = (rand() % 360) * 3.14159f / 180.0f;
	Particle.m_RotationSpeed = ((rand() % 200) - 100) / 100.0f;
	
	// Type
	Particle.m_Type = static_cast<EParticleType>(rand() % NUM_PARTICLE_TYPES);
	
	// Phase for effects
	Particle.m_Phase = (rand() % 1000) / 999.0f;
	
	// Initial color
	Particle.m_Color = GetEffectColor(Particle.m_Phase);
	
	m_vParticles.push_back(Particle);
}

void CMenuParticles::UpdateParticle(CParticle &Particle, float TimePassed)
{
	// Update position
	Particle.m_Pos += Particle.m_Vel * TimePassed;
	
	// Update rotation
	Particle.m_Rotation += Particle.m_RotationSpeed * TimePassed;
	
	// Update life
	Particle.m_Life -= TimePassed;
	
	// Update phase for animated effects
	Particle.m_Phase += TimePassed * 0.5f;
	if(Particle.m_Phase > 1.0f)
		Particle.m_Phase -= 1.0f;
	
	// Update color based on effect mode
	if(g_Config.m_ClMenuParticleEffect != EFFECT_NONE)
	{
		Particle.m_Color = GetEffectColor(Particle.m_Phase + m_Time * 0.2f);
	}
	
	// Fade out at the end of life
	const float LifeRatio = Particle.m_Life / Particle.m_MaxLife;
	Particle.m_Color.a = std::min(1.0f, LifeRatio * 2.0f) * g_Config.m_ClMenuParticleAlpha / 100.0f;
	
	// Apply gravity for meteor effect
	if(g_Config.m_ClMenuParticleEffect == EFFECT_METEOR)
	{
		Particle.m_Vel.y += 50.0f * TimePassed;
	}
}

void CMenuParticles::RenderParticle(const CParticle &Particle)
{
	Graphics()->SetColor(Particle.m_Color);
	
	// Render based on particle type
	switch(Particle.m_Type)
	{
	case PARTICLE_STAR:
		{
			// Star shape (rendered as rotated quad with elongated arms)
			Graphics()->QuadsSetRotation(Particle.m_Rotation);
			IGraphics::CQuadItem QuadItem(Particle.m_Pos.x, Particle.m_Pos.y, Particle.m_Size, Particle.m_Size * 1.5f);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsSetRotation(Particle.m_Rotation + 3.14159f / 2.0f);
			IGraphics::CQuadItem QuadItem2(Particle.m_Pos.x, Particle.m_Pos.y, Particle.m_Size, Particle.m_Size * 1.5f);
			Graphics()->QuadsDrawTL(&QuadItem2, 1);
		}
		break;
	case PARTICLE_CIRCLE:
		{
			// Simple circle
			IGraphics::CQuadItem QuadItem(Particle.m_Pos.x - Particle.m_Size * 0.5f, 
										 Particle.m_Pos.y - Particle.m_Size * 0.5f, 
										 Particle.m_Size, Particle.m_Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
		}
		break;
	case PARTICLE_SPARKLE:
		{
			// Sparkle (small bright point with glow)
			const float GlowSize = Particle.m_Size * 2.0f;
			ColorRGBA GlowColor = Particle.m_Color;
			GlowColor.a *= 0.3f;
			Graphics()->SetColor(GlowColor);
			IGraphics::CQuadItem QuadItem(Particle.m_Pos.x - GlowSize * 0.5f, 
										 Particle.m_Pos.y - GlowSize * 0.5f, 
										 GlowSize, GlowSize);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			
			// Bright center
			Graphics()->SetColor(Particle.m_Color);
			IGraphics::CQuadItem QuadItem2(Particle.m_Pos.x - Particle.m_Size * 0.3f, 
										  Particle.m_Pos.y - Particle.m_Size * 0.3f, 
										  Particle.m_Size * 0.6f, Particle.m_Size * 0.6f);
			Graphics()->QuadsDrawTL(&QuadItem2, 1);
		}
		break;
	case PARTICLE_GLOW:
		{
			// Soft glow effect with pulsing
			const float Pulse = 0.8f + std::sin(m_Time * 3.0f + Particle.m_Phase * 6.28f) * 0.2f;
			const float Size = Particle.m_Size * Pulse;
			IGraphics::CQuadItem QuadItem(Particle.m_Pos.x - Size * 0.5f, 
										 Particle.m_Pos.y - Size * 0.5f, 
										 Size, Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
		}
		break;
	}
	
	Graphics()->QuadsSetRotation(0.0f);
}

ColorRGBA CMenuParticles::GetEffectColor(float Phase)
{
	ColorRGBA Color;
	
	switch(g_Config.m_ClMenuParticleEffect)
	{
	case EFFECT_RAINBOW:
		{
			// HSV to RGB conversion for rainbow
			const float Hue = std::fmod(Phase * 360.0f, 360.0f);
			const float S = 1.0f;
			const float V = 1.0f;
			
			const float C = V * S;
			const float X = C * (1.0f - std::abs(std::fmod(Hue / 60.0f, 2.0f) - 1.0f));
			const float M = V - C;
			
			float R = 0, G = 0, B = 0;
			if(Hue < 60) { R = C; G = X; B = 0; }
			else if(Hue < 120) { R = X; G = C; B = 0; }
			else if(Hue < 180) { R = 0; G = C; B = X; }
			else if(Hue < 240) { R = 0; G = X; B = C; }
			else if(Hue < 300) { R = X; G = 0; B = C; }
			else { R = C; G = 0; B = X; }
			
			Color.r = R + M;
			Color.g = G + M;
			Color.b = B + M;
			Color.a = 1.0f;
		}
		break;
	case EFFECT_PULSE:
		{
			// Pulse between two colors
			const float Pulse = (std::sin(Phase * 6.28f) + 1.0f) * 0.5f;
			Color.r = 0.3f + Pulse * 0.7f;
			Color.g = 0.5f + Pulse * 0.5f;
			Color.b = 1.0f;
			Color.a = 1.0f;
		}
		break;
	case EFFECT_WAVE:
		{
			// Wave effect with cyan/magenta gradient
			const float Wave = (std::sin(Phase * 6.28f * 2.0f) + 1.0f) * 0.5f;
			Color.r = 0.5f + Wave * 0.5f;
			Color.g = 0.7f - Wave * 0.3f;
			Color.b = 1.0f - Wave * 0.3f;
			Color.a = 1.0f;
		}
		break;
	case EFFECT_SPIRAL:
		{
			// Spiral gradient
			Color.r = 1.0f;
			Color.g = 0.5f + Phase * 0.5f;
			Color.b = Phase;
			Color.a = 1.0f;
		}
		break;
	case EFFECT_METEOR:
		{
			// Fiery colors for meteors
			Color.r = 1.0f;
			Color.g = 0.3f + Phase * 0.4f;
			Color.b = 0.1f;
			Color.a = 1.0f;
		}
		break;
	default:
		Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		break;
	}
	
	return Color;
}
