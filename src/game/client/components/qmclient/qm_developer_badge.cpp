#include "qm_developer_badge.h"

#include <engine/storage.h>

void CQmDeveloperBadge::Init(IGraphics *pGraphics)
{
	Shutdown(pGraphics);
	CImageInfo Image;
	if(pGraphics->LoadPng(Image, "qmclient/developer_badge.png", IStorage::TYPE_ALL))
		m_Texture = pGraphics->LoadTextureRawMove(Image, 0, "qm_developer_badge");

	// 一张小纹理同时提供抗锯齿圆点和柔光，所有光点在同一个批次提交。
	constexpr int CellSize = 32;
	std::array<uint8_t, CellSize * CellSize * 2 * 4> aPixels{};
	for(int y = 0; y < CellSize; ++y)
		for(int x = 0; x < CellSize * 2; ++x)
		{
			const float Dx = ((x % CellSize) + 0.5f) / CellSize * 2.0f - 1.0f;
			const float Dy = (y + 0.5f) / CellSize * 2.0f - 1.0f;
			const float Radius = std::sqrt(Dx * Dx + Dy * Dy);
			const float Opacity = x < CellSize ? std::clamp((1.0f - Radius) * CellSize * 0.5f, 0.0f, 1.0f) :
							     std::clamp((std::exp(-5.0f * Radius * Radius) - std::exp(-5.0f)) / (1.0f - std::exp(-5.0f)), 0.0f, 1.0f);
			const int Pixel = (y * CellSize * 2 + x) * 4;
			aPixels[Pixel] = aPixels[Pixel + 1] = aPixels[Pixel + 2] = 255;
			aPixels[Pixel + 3] = static_cast<uint8_t>(Opacity * 255.0f);
		}
	CImageInfo Particle;
	Particle.m_Width = CellSize * 2;
	Particle.m_Height = CellSize;
	Particle.m_Format = CImageInfo::FORMAT_RGBA;
	Particle.m_pData = aPixels.data();
	m_ParticleTexture = pGraphics->LoadTextureRaw(Particle, IGraphics::TEXLOAD_NO_MIPMAPS, "qm_developer_badge_particles");
}

void CQmDeveloperBadge::Shutdown(IGraphics *pGraphics)
{
	if(m_Texture.IsValid())
		pGraphics->UnloadTexture(&m_Texture);
	if(m_ParticleTexture.IsValid())
		pGraphics->UnloadTexture(&m_ParticleTexture);
	m_MotionTime = -1.0;
}

void CQmDeveloperBadge::Render(IGraphics *pGraphics, const CUIRect &Rect, float Alpha, double TimeSec) const
{
	if(!Available() || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f)
		return;
	pGraphics->TextureSet(m_Texture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(0.0f);
	pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, std::clamp(Alpha, 0.0f, 1.0f));
	IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	if(m_ParticleTexture.IsValid())
	{
		// 聊天、名牌和多个玩家共用同一时间采样，不为每枚徽章重复计算轨迹。
		if(m_MotionTime != TimeSec)
		{
			m_Motion = QmSampleDeveloperBadgeMotion(TimeSec);
			m_MotionTime = TimeSec;
		}
		const vec2 Scale(Rect.w / QM_DEVELOPER_BADGE_WIDTH, Rect.h / QM_DEVELOPER_BADGE_HEIGHT);
		const ColorRGBA Accent(0x8bdfcf);
		const auto DrawParticle = [&](vec2 Center, float Size, ColorRGBA Color, float Opacity) {
			pGraphics->SetColor(Color.WithMultipliedAlpha(Opacity * std::clamp(Alpha, 0.0f, 1.0f)));
			IGraphics::CQuadItem Particle(Rect.x + (Center.x - Size * 0.5f) * Scale.x,
				Rect.y + (Center.y - Size * 0.5f) * Scale.y, Size * Scale.x, Size * Scale.y);
			pGraphics->QuadsDrawTL(&Particle, 1);
		};
		static const auto s_aTailOpacity = [] {
			std::array<float, QM_DEVELOPER_BADGE_TRAIL_COUNT> aOpacity{};
			for(int i = 0; i < QM_DEVELOPER_BADGE_TRAIL_COUNT; ++i)
				aOpacity[i] = i == 0 ? 1.0f : 0.72f * std::pow(1.0f - i / 23.0f, 1.25f);
			return aOpacity;
		}();
		pGraphics->TextureSet(m_ParticleTexture);
		pGraphics->QuadsBegin();
		pGraphics->QuadsSetSubset(0.5f, 0.0f, 1.0f, 1.0f);
		for(int i = 8; i >= 0; --i)
			DrawParticle(m_Motion.m_aOrbit[i], i == 0 ? 8.0f : 4.5f, Accent, s_aTailOpacity[i] * (i == 0 ? 0.6f : 0.2f));
		for(int i = 0; i < 2; ++i)
			DrawParticle(m_Motion.m_aCircuit[i], 6.0f, Accent, i == 0 ? 0.22f : 0.18f);
		pGraphics->QuadsSetSubset(0.0f, 0.0f, 0.5f, 1.0f);
		for(int i = QM_DEVELOPER_BADGE_TRAIL_COUNT - 1; i >= 0; --i)
			DrawParticle(m_Motion.m_aOrbit[i], i == 0 ? 1.45f : 1.2f, i == 0 ? ColorRGBA(0xd8fff5) : Accent, s_aTailOpacity[i]);
		for(int i = 0; i < 2; ++i)
			DrawParticle(m_Motion.m_aCircuit[i], i == 0 ? 2.4f : 2.0f, Accent, i == 0 ? 0.6f : 0.5f);
		pGraphics->QuadsEnd();
		pGraphics->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	}
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
