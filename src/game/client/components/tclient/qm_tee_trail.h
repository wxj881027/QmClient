// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_QM_TEE_TRAIL_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_QM_TEE_TRAIL_H

#include <base/color.h>
#include <base/vmath.h>

#include <array>
#include <cstddef>
#include <vector>

// 保留旧几何接口的输入类型；时间和弧长使用双精度，避免长时间游戏后丢失帧内精度。
class CTrailPart
{
public:
	vec2 m_Pos = vec2(0.0f, 0.0f);
	ColorRGBA m_Col = ColorRGBA(1, 1, 1, 1);
	float m_Width = 0.0f;
	int m_Tick = -1;
	double m_Time = -1.0;
	double m_Distance = 0.0;
	float m_Speed = -1.0f;
	float m_Life = 0.0f;
};

namespace qm_tee_trail
{
	// 0 保留原版；旧配置中的 4、5 由 ResolveStyle 映射到新的艺术风格。
	enum
	{
		STYLE_ORIGINAL = 0,
		STYLE_MANGA = 1,
		STYLE_MAGIC = 2,
		STYLE_PIXEL = 3,
		STYLE_COUNT = 4,
	};

	constexpr size_t MAX_POINTS = 192;
	constexpr size_t MAX_RENDER_POINTS = 384;
	constexpr size_t MAX_QUADS = MAX_RENDER_POINTS * 12 + 96;
	constexpr float SAMPLE_SPACING = 6.0f;
	constexpr float MIN_SPEED = 0.15f; // 世界单位 / 游戏 tick

	// 每个玩家独立的等距环形队列；渲染头只是端帽，不占用距离采样点。
	class CTrailState
	{
		std::array<CTrailPart, MAX_POINTS> m_aPoints;
		size_t m_First = 0;
		size_t m_Count = 0;
		CTrailPart m_Head;
		vec2 m_LastPos = vec2(0, 0);
		double m_LastTime = -1.0;
		double m_Carry = 0.0;
		float m_LastSpeed = 0.0f;
		void Push(const CTrailPart &Point);

	public:
		void Reset();
		// 时间以游戏 tick 为单位；Break 显式处理传送、复活与渲染时间源切换。
		void Update(vec2 Position, double Time, float Speed, float Life, bool Break = false);
		void Export(std::vector<CTrailPart> &vOut) const;
	};

	// 顶点沿周界排列；普通混合保存暗主体，加法混合只用于发光层。
	struct SQuad
	{
		vec2 m_aPos[4];
		ColorRGBA m_aColor[4];
		bool m_Additive = false;
	};

	// 一段轨迹的切线只准备一次，细分采样保留原三次公式与浮点运算顺序。
	struct SPreparedCurve
	{
		vec2 m_Start, m_End, m_StartTangent, m_EndTangent;
		vec2 Evaluate(float T) const;
	};
	SPreparedCurve PrepareCurve(vec2 P0, vec2 P1, vec2 P2, vec2 P3);

	int ResolveStyle(int Style);
	float Lifetime(int Style, int Length, float Speed);

	// 同一网格构建器服务全部样式，包括原版。输入从新到旧，输出缓存由调用者复用。
	// PixelSize 是一个屏幕像素对应的世界单位，放大时增加曲线细分并缩窄抗锯齿边。
	void BuildEffect(const std::vector<CTrailPart> &vTrail, int Style, bool UsePresetPalette, double CurTime, float Width, int Seed, std::vector<SQuad> &vOut, float PixelSize = 1.0f, bool Taper = true, bool Fade = false);
}

#endif
