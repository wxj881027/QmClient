#include "qm_developer_badge.h"

#include "qm_title_badge.h"

void CQmDeveloperBadge::Init(IGraphics *pGraphics, const CQmTitleBadge &TitleBadge)
{
	Shutdown(pGraphics);
	m_Surface.Init(pGraphics);
	m_pTitleBadge = &TitleBadge;
}

void CQmDeveloperBadge::Shutdown(IGraphics *pGraphics)
{
	m_Surface.Shutdown(pGraphics);
	m_pTitleBadge = nullptr;
	m_MotionTime = -1.0;
}

void CQmDeveloperBadge::Render(IGraphics *pGraphics, const CUIRect &Rect, float Alpha, double TimeSec) const
{
	if(!Available() || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f)
		return;
	Alpha = std::clamp(Alpha, 0.0f, 1.0f);
	QmDrawBadgeCapsuleGeometry(pGraphics, Rect, ColorRGBA(0x19343b).WithAlpha(Alpha), ColorRGBA(0x4b7d84).WithAlpha(Alpha));
	m_Surface.Render(pGraphics, Rect, ColorRGBA(1, 1, 1, Alpha));
	m_pTitleBadge->RenderLogo(pGraphics, QmDeveloperBadgeLogoRect(Rect), ColorRGBA(0x8bdfcf).WithAlpha(Alpha));
	if(std::isfinite(TimeSec))
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
			pGraphics->SetColor(Color.WithMultipliedAlpha(Opacity * Alpha));
			const vec2 Position = vec2(Rect.x, Rect.y) + Center * Scale;
			// 光点与柔光均为几何圆盘，随徽章整体缩放。
			pGraphics->DrawCircle(Position.x, Position.y, Size * std::min(Scale.x, Scale.y) * 0.5f, 24);
		};
		static const auto s_aTailOpacity = [] {
			std::array<float, QM_DEVELOPER_BADGE_TRAIL_COUNT> aOpacity{};
			for(int i = 0; i < QM_DEVELOPER_BADGE_TRAIL_COUNT; ++i)
				aOpacity[i] = i == 0 ? 1.0f : 0.72f * std::pow(1.0f - i / 23.0f, 1.25f);
			return aOpacity;
		}();
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		for(int i = 8; i >= 0; --i)
			DrawParticle(m_Motion.m_aOrbit[i], i == 0 ? 8.0f : 4.5f, Accent, s_aTailOpacity[i] * (i == 0 ? 0.6f : 0.2f));
		for(int i = 0; i < 2; ++i)
			DrawParticle(m_Motion.m_aCircuit[i], 6.0f, Accent, i == 0 ? 0.22f : 0.18f);
		for(int i = QM_DEVELOPER_BADGE_TRAIL_COUNT - 1; i >= 0; --i)
			DrawParticle(m_Motion.m_aOrbit[i], i == 0 ? 1.45f : 1.2f, i == 0 ? ColorRGBA(0xd8fff5) : Accent, s_aTailOpacity[i]);
		for(int i = 0; i < 2; ++i)
			DrawParticle(m_Motion.m_aCircuit[i], i == 0 ? 2.4f : 2.0f, Accent, i == 0 ? 0.6f : 0.5f);
		pGraphics->QuadsEnd();
	}
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
