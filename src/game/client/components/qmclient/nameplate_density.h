#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_DENSITY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_DENSITY_H

#include <algorithm>
#include <cmath>
#include <cstdint>

// 整块名牌共用一次密度决策：动画内不重建，动画结束才占用完整部件预算。
class CQmNameplateDensity
{
	float m_Ratio = 0.0f;
	float m_ReferencePixels = 0.0f;
	uint64_t m_Revision = 0;

public:
	float Ratio() const { return m_Ratio; }
	uint64_t Revision() const { return m_Revision; }
	void Reset() { *this = CQmNameplateDensity(); }
	bool Update(float RequestedRatio, float ReferencePixels, float Grid, bool Zooming, int Cost, int FrameBudget, int &Budget)
	{
		if(!std::isfinite(RequestedRatio) || RequestedRatio <= 0.0f ||
			!std::isfinite(ReferencePixels) || ReferencePixels <= 0.0f ||
			!std::isfinite(Grid) || Grid <= 0.0f || Grid > 1.0f || Cost < 0)
			return false;
		const float PhysicalDensity = RequestedRatio * ReferencePixels;
		if(!std::isfinite(PhysicalDensity) || PhysicalDensity <= 0.0f)
			return false;
		// 物理密度固定在对数档位上，分辨率和非整数停点共用同一组栅格字号。
		const float Target = std::exp((std::round(std::log(PhysicalDensity) / Grid) + 1.0f) * Grid) / ReferencePixels;
		if(!std::isfinite(Target) || Target <= 0.0f)
			return false;
		// 仅多烘焙一个相机档位；缩放使用滞回，窗口尺寸和 DPI 独立检测。
		const float Drift = m_Ratio > 0.0f ? std::log(PhysicalDensity / (m_Ratio * m_ReferencePixels)) + Grid : 0.0f;
		const bool ReferenceChanged = m_ReferencePixels > 0.0f && std::abs(std::log(ReferencePixels / m_ReferencePixels)) > 0.0001f;
		if(m_Ratio > 0.0f && (Zooming || (!ReferenceChanged && std::abs(Drift) <= Grid * 0.65f)))
			return false;
		// 初次内容必须立即出现。超预算名牌在完整预算帧独占一次，避免永久饥饿。
		const int Charge = std::min(Cost, std::max(0, FrameBudget));
		if(m_Ratio > 0.0f && ((Cost > 0 && Charge == 0) || Budget < Charge))
			return false;
		Budget = std::max(0, Budget - Charge);
		m_Ratio = Target;
		m_ReferencePixels = ReferencePixels;
		++m_Revision;
		return true;
	}
};

#endif
