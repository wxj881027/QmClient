#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_TRAIL_STYLES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_TRAIL_STYLES_H

#include <game/client/components/tclient/qm_tee_trail.h>

namespace qm_tee_trail
{
	// 采样器准备共同的轨迹和衰减，风格模块只负责可见形状，不持有跨帧状态。
	struct SStyleSample
	{
		vec2 m_Pos, m_Normal;
		ColorRGBA m_Tint;
		double m_Distance;
		float m_Age, m_Width, m_Alpha, m_Energy, m_Head, m_Remaining;
	};

	void BuildStyledEffect(const SStyleSample *pSamples, size_t Count, int Style, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut);
}

#endif
