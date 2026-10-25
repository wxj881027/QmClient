#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_TRAIL_STYLES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TEE_TRAIL_STYLES_H

#include <game/client/components/tclient/qm_tee_trail.h>

namespace qm_tee_trail
{
	struct SPixelNoise
	{
		float m_Cluster;
		float m_Grain;
	};

	SPixelNoise SamplePixelNoise(int X, int Y, unsigned Seed);

	// 世界格与种子决定的噪声不随时间、颜色或宽度变化，缓存由渲染组件持有。
	class CPixelNoiseCache
	{
		struct SEntry
		{
			int m_X = 0, m_Y = 0;
			unsigned m_Seed = 0;
			bool m_Valid = false;
			SPixelNoise m_Noise{};
		};
		std::array<SEntry, 4096> m_aEntries{};

	public:
		const SPixelNoise &Get(int X, int Y, unsigned Seed);
	};

	// 采样器准备共同的轨迹和衰减，风格模块只负责可见形状。
	struct SStyleSample
	{
		vec2 m_Pos, m_Normal;
		ColorRGBA m_Tint;
		double m_Distance;
		float m_Age, m_Width, m_Alpha, m_Energy, m_Head, m_Remaining;
	};

	void BuildStyledEffect(const SStyleSample *pSamples, size_t Count, int Style, bool Preset, float Width, float PixelSize, unsigned Seed, std::vector<SQuad> &vOut, CPixelNoiseCache *pPixelNoiseCache = nullptr);
}

#endif
