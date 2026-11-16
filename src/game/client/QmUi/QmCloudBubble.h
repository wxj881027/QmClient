#ifndef GAME_CLIENT_QMUI_QMCLOUDBUBBLE_H
#define GAME_CLIENT_QMUI_QMCLOUDBUBBLE_H

#include "QmChatBubble.h"

#include <engine/graphics.h>

#include <cstdint>
#include <vector>

namespace qm_chat_bubble
{

	constexpr int CLOUD_PUFF_COUNT = 9;
	constexpr int CLOUD_TILE_SIZE = 128;
	constexpr int CLOUD_ATLAS_SIZE = CLOUD_TILE_SIZE * 2;

	inline float CloudExitDuration(float MessageDuration)
	{
		// 最短一秒的消息仍留出聚集和阅读时间。
		return std::clamp(MessageDuration * 0.35f, 0.15f, 0.8f);
	}

	inline float CloudTextOpacity(float Assembly)
	{
		const float T = std::clamp((Assembly - 0.2f) / 0.65f, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	inline float CloudPuffOpacity(float Opacity)
	{
		// 中心通常有四团重叠，补偿叠加以免低透明度设置仍变成实心白块。
		return 1.0f - std::sqrt(std::sqrt(1.0f - std::clamp(Opacity, 0.0f, 1.0f)));
	}

	inline float CloudNoise(vec3 Position)
	{
		const int X = static_cast<int>(std::floor(Position.x));
		const int Y = static_cast<int>(std::floor(Position.y));
		const int Z = static_cast<int>(std::floor(Position.z));
		auto Smooth = [](float T) { return T * T * (3.0f - 2.0f * T); };
		const vec3 F(Smooth(Position.x - X), Smooth(Position.y - Y), Smooth(Position.z - Z));
		auto Value = [](int x, int y, int z) {
			uint32_t Hash = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^ static_cast<uint32_t>(z) * 0xcb1ab31fu;
			Hash ^= Hash >> 13;
			Hash *= 0x85ebca6bu;
			Hash ^= Hash >> 16;
			return static_cast<float>(Hash & 65535u) / 65535.0f;
		};
		const float Front = mix(mix(Value(X, Y, Z), Value(X + 1, Y, Z), F.x), mix(Value(X, Y + 1, Z), Value(X + 1, Y + 1, Z), F.x), F.y);
		const float Back = mix(mix(Value(X, Y, Z + 1), Value(X + 1, Y, Z + 1), F.x), mix(Value(X, Y + 1, Z + 1), Value(X + 1, Y + 1, Z + 1), F.x), F.y);
		return mix(Front, Back, F.z);
	}

	inline float CloudDensity(vec3 Position, int Variant)
	{
		const vec3 Seed(Variant * 3.7f + 1.2f, Variant * 2.1f, Variant * 5.3f);
		// 低频噪声形成云团起伏，高频只补少量细节，保留连续柔边。
		const float Billow = 0.24f * (CloudNoise(Position * 3.4f + Seed) - 0.5f) + 0.09f * (CloudNoise(Position * 8.0f + Seed) - 0.5f);
		return std::clamp((0.88f - length(Position) + Billow) * 5.0f, 0.0f, 1.0f);
	}

	inline ColorRGBA CloudTexel(vec2 Position, int Variant)
	{
		float Alpha = 0.0f;
		float Light = 0.0f;
		// 固定 12 层体密度积分，只在创建共享缓存时执行。
		for(int Layer = 0; Layer < 12; ++Layer)
		{
			const vec3 Point(Position.x, Position.y, 1.1f - Layer * 0.2f);
			const float Density = CloudDensity(Point, Variant);
			if(Density <= 0.0f)
				continue;
			const float Occlusion = CloudDensity(Point + vec3(-0.18f, -0.25f, 0.22f), Variant);
			const float Shade = 0.70f + 0.30f * (1.0f - Occlusion);
			const float Coverage = (1.0f - Alpha) * (1.0f - std::exp(-Density * 0.75f));
			Light += Shade * Coverage;
			Alpha += Coverage;
		}
		const float Shade = Alpha > 0.0f ? Light / Alpha : 1.0f;
		return ColorRGBA(Shade, std::min(1.0f, Shade + 0.015f), std::min(1.0f, Shade + 0.035f), Alpha);
	}

	struct SCloudPuff
	{
		vec2 m_Center;
		vec2 m_Size;
		int m_Variant;
	};

	inline std::array<SCloudPuff, CLOUD_PUFF_COUNT> CloudPuffs(const SLayout &Layout, float Assembly)
	{
		struct SSeed
		{
			vec2 m_Position;
			vec2 m_Size;
			vec2 m_Drift;
		};
		// 先铺连续正文区域，再叠不对称的云顶；数量固定，与消息长度和玩家数量无关。
		static const std::array<SSeed, CLOUD_PUFF_COUNT> s_aSeeds = {{
			{{-0.22f, 0.08f}, {0.76f, 1.06f}, {-0.85f, 0.25f}},
			{{0.23f, 0.08f}, {0.75f, 1.02f}, {0.95f, 0.18f}},
			{{0.0f, 0.10f}, {0.88f, 1.10f}, {0.12f, 0.65f}},
			{{-0.34f, -0.02f}, {0.53f, 0.92f}, {-1.0f, -0.15f}},
			{{0.34f, -0.04f}, {0.50f, 0.90f}, {1.0f, -0.22f}},
			{{-0.19f, -0.18f}, {0.56f, 1.10f}, {-0.55f, -0.80f}},
			{{0.09f, -0.20f}, {0.61f, 1.14f}, {0.35f, -1.0f}},
			{{0.27f, -0.13f}, {0.45f, 0.94f}, {0.75f, -0.65f}},
			{{-0.04f, 0.06f}, {0.65f, 0.89f}, {-0.20f, -0.45f}},
		}};
		Assembly = std::clamp(Assembly, 0.0f, 1.0f);
		const float Spread = (1.0f - Assembly) * (1.0f - Assembly);
		const vec2 Center = Layout.m_Position + Layout.m_Size * 0.5f;
		const vec2 Travel = vec2(std::min(Layout.m_Size.x * 0.35f, 75.0f * Layout.m_Scale), 45.0f * Layout.m_Scale);
		std::array<SCloudPuff, CLOUD_PUFF_COUNT> Result;
		for(size_t i = 0; i < Result.size(); ++i)
		{
			const auto &Seed = s_aSeeds[i];
			Result[i] = {Center + vec2(Seed.m_Position.x * Layout.m_Size.x, Seed.m_Position.y * Layout.m_Size.y) + vec2(Seed.m_Drift.x * Travel.x, Seed.m_Drift.y * Travel.y) * Spread,
				vec2(Seed.m_Size.x * Layout.m_Size.x, Seed.m_Size.y * Layout.m_Size.y) * (0.68f + 0.32f * Assembly), static_cast<int>(i % 4)};
		}
		return Result;
	}

	class CCloudRenderer
	{
		IGraphics::CTextureHandle m_Texture;

	public:
		void Init(IGraphics *pGraphics);
		void Shutdown(IGraphics *pGraphics);
		void Render(IGraphics *pGraphics, const SLayout &Layout, const SPalette &Colors, float Assembly);
	};
}
#endif
