// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "qm_tee_trail.h"

#include <base/math.h>

#include <game/client/components/qmclient/tee_trail_styles.h>
#include <game/client/components/qmclient/trail_band_geometry.h>
#include <game/client/components/qmclient/trail_band_section.h>

#include <algorithm>
#include <cmath>

namespace
{
	using namespace qm_tee_trail;
	using SSample = SStyleSample;

	constexpr float s_aLifeFactors[STYLE_COUNT] = {1.0f, 0.9f, 1.25f, 1.0f};

	float Smooth(float X)
	{
		X = std::clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	vec2 Unit(vec2 V, vec2 Fallback = vec2(1, 0))
	{
		const float Len = length(V);
		return Len > 0.0001f ? V / Len : Fallback;
	}

	ColorRGBA Tint(ColorRGBA A, ColorRGBA B, float T)
	{
		return ColorRGBA(mix(A.r, B.r, T), mix(A.g, B.g, T), mix(A.b, B.b, T), mix(A.a, B.a, T));
	}

	struct SBandPoint
	{
		vec2 m_Pos;
		float m_Left, m_Right;
		ColorRGBA m_Color;
	};

	void EmitPreparedBand(std::vector<SQuad> &vOut, const SQmTrailBandSection &A, const SQmTrailBandSection &B)
	{
		for(int Strip = 0; Strip < 3; ++Strip)
		{
			const ColorRGBA Ca = A.m_aColor[Strip], Cb = B.m_aColor[Strip], Cc = B.m_aColor[Strip + 1], Cd = A.m_aColor[Strip + 1];
			if(vOut.size() < MAX_QUADS && std::max({Ca.a, Cb.a, Cc.a, Cd.a}) >= 0.001f)
				vOut.push_back({{A.m_aPos[Strip], B.m_aPos[Strip], B.m_aPos[Strip + 1], A.m_aPos[Strip + 1]}, {Ca, Cb, Cc, Cd}, false});
		}
	}
}

qm_tee_trail::SPreparedCurve qm_tee_trail::PrepareCurve(vec2 P0, vec2 P1, vec2 P2, vec2 P3)
{
	// 限制三次曲线切向量，折返时压短到零，避免普通 Catmull-Rom 在尖角处过冲。
	const float D = distance(P1, P2);
	const vec2 Segment = Unit(P2 - P1);
	const vec2 Before = Unit(P1 - P0, Segment);
	const vec2 After = Unit(P3 - P2, Segment);
	const vec2 M1 = Unit(Before + Segment, Segment) * D * std::max(0.0f, dot(Before, Segment));
	const vec2 M2 = Unit(Segment + After, Segment) * D * std::max(0.0f, dot(Segment, After));
	return {P1, P2, M1, M2};
}

vec2 qm_tee_trail::SPreparedCurve::Evaluate(float T) const
{
	return m_Start * (2 * T * T * T - 3 * T * T + 1) + m_StartTangent * (T * T * T - 2 * T * T + T) + m_End * (-2 * T * T * T + 3 * T * T) + m_EndTangent * (T * T * T - T * T);
}

void qm_tee_trail::CTrailState::Reset()
{
	m_First = m_Count = 0;
	m_LastTime = -1.0;
	m_Carry = 0.0;
	m_LastSpeed = 0.0f;
	m_Head = CTrailPart();
}

void qm_tee_trail::CTrailState::Push(const CTrailPart &Point)
{
	if(m_Count == MAX_POINTS)
	{
		m_First = (m_First + 1) % MAX_POINTS;
		--m_Count;
	}
	m_aPoints[(m_First + m_Count++) % MAX_POINTS] = Point;
}

void qm_tee_trail::CTrailState::Update(vec2 Position, double Time, float Speed, float Life, bool Break)
{
	if(!std::isfinite(Position.x) || !std::isfinite(Position.y) || !std::isfinite(Time) || !std::isfinite(Speed) || Time < 0)
	{
		Reset();
		return;
	}
	Speed = std::max(0.0f, Speed);
	Life = std::clamp(Life, 1.0f, 400.0f);
	const double Dt = Time - m_LastTime;
	const float Dist = distance(Position, m_LastPos);
	// 长帧间隔不猜测漏掉的运动；正常高速位移由速度预算区分，允许补齐多个中间点。
	if(Break || Dt < 0 || Dt > 12.5 || (m_LastTime >= 0 && Dist > 48.0 + std::max(Speed, m_LastSpeed) * std::max(Dt, 0.0) * 2.5))
		Reset();
	while(m_Count > 0 && Time - m_aPoints[m_First].m_Time >= m_aPoints[m_First].m_Life)
	{
		m_First = (m_First + 1) % MAX_POINTS;
		--m_Count;
	}
	if(m_LastTime < 0 || m_Count == 0)
	{
		m_Head = CTrailPart();
		m_Head.m_Pos = Position;
		m_Head.m_Time = Time;
		m_Head.m_Tick = int(Time);
		m_Head.m_Life = Life;
		m_Head.m_Speed = Speed;
		m_Carry = 0;
		Push(m_Head);
	}
	else if(Dt > 0 && Speed >= MIN_SPEED && Dist / Dt >= MIN_SPEED && Dist > 0.0001f)
	{
		const double StartDistance = m_Head.m_Distance;
		const double EndDistance = StartDistance + Dist;
		double Along = SAMPLE_SPACING - m_Carry;
		// 极端速度也只保留最后一个容量窗口，不为将被覆盖的点做无用循环。
		if((Dist - Along) / SAMPLE_SPACING > MAX_POINTS)
			Along += std::floor((Dist - Along) / SAMPLE_SPACING - MAX_POINTS) * SAMPLE_SPACING;
		for(; Along <= Dist + 0.00001; Along += SAMPLE_SPACING)
		{
			const float T = std::clamp(float(Along / Dist), 0.0f, 1.0f);
			CTrailPart Point;
			Point.m_Pos = mix(m_LastPos, Position, T);
			Point.m_Time = m_LastTime + Dt * T;
			Point.m_Tick = int(Point.m_Time);
			Point.m_Distance = StartDistance + Along;
			Point.m_Speed = mix(m_LastSpeed, Speed, T);
			Point.m_Life = mix(m_Head.m_Life, Life, T);
			Push(Point);
		}
		m_Carry = std::max(0.0, std::fmod(m_Carry + Dist + 0.00001, double(SAMPLE_SPACING)) - 0.00001);
		m_Head.m_Pos = Position;
		m_Head.m_Time = Time;
		m_Head.m_Tick = int(Time);
		m_Head.m_Distance = EndDistance;
		m_Head.m_Speed = Speed;
		m_Head.m_Life = Life;
	}
	m_LastPos = Position;
	m_LastTime = Time;
	m_LastSpeed = Speed;
}

void qm_tee_trail::CTrailState::Export(std::vector<CTrailPart> &vOut) const
{
	vOut.clear();
	if(m_Count == 0 || m_LastTime - m_Head.m_Time >= m_Head.m_Life)
		return;
	vOut.reserve(MAX_POINTS + 1);
	vOut.push_back(m_Head);
	for(size_t i = m_Count; i > 0; --i)
	{
		const CTrailPart &Point = m_aPoints[(m_First + i - 1) % MAX_POINTS];
		if(distance(vOut.back().m_Pos, Point.m_Pos) > 0.001f)
			vOut.push_back(Point);
	}
}

int qm_tee_trail::ResolveStyle(int Style)
{
	// 继续接受旧配置编号，但不再保留旧特效的渲染分支。
	if(Style == 4)
		return STYLE_MANGA;
	if(Style == 5)
		return STYLE_MAGIC;
	return Style > STYLE_ORIGINAL && Style < STYLE_COUNT ? Style : STYLE_ORIGINAL;
}

float qm_tee_trail::Lifetime(int Style, int Length, float Speed)
{
	const float Energy = std::clamp(Speed / 30.0f, 0.0f, 1.0f);
	return std::clamp(Length, 5, 200) * s_aLifeFactors[ResolveStyle(Style)] * (0.75f + 0.45f * Energy);
}

void qm_tee_trail::BuildEffect(const std::vector<CTrailPart> &vTrail, int Style, bool UsePresetPalette, double CurTime, float Width, int Seed, std::vector<SQuad> &vOut, float PixelSize, bool Taper, bool Fade)
{
	vOut.clear();
	if(vTrail.size() < 2 || !std::isfinite(CurTime) || !std::isfinite(Width) || !std::isfinite(PixelSize))
		return;
	Style = ResolveStyle(Style);
	PixelSize = std::clamp(PixelSize, 0.025f, 8.0f);
	Width = Width > 0 ? Width : (Style == STYLE_ORIGINAL ? PixelSize * 0.5f : 4.0f);
	const size_t Count = std::min(vTrail.size(), MAX_POINTS + 1);
	std::array<float, MAX_POINTS + 1> aLengths{};
	for(size_t i = 0; i < Count; ++i)
	{
		const auto &P = vTrail[i];
		if(!std::isfinite(P.m_Pos.x) || !std::isfinite(P.m_Pos.y) || !std::isfinite(P.m_Time) || !std::isfinite(P.m_Distance))
			return;
		const double Time = P.m_Time >= 0 ? P.m_Time : P.m_Tick;
		if(Time > CurTime + 0.01)
			return;
		if(i > 0)
		{
			const float D = distance(vTrail[i - 1].m_Pos, P.m_Pos);
			if(D > 192.0f)
				return;
			aLengths[i] = aLengths[i - 1] + D;
		}
	}
	const float Total = aLengths[Count - 1];
	if(Total < 0.01f)
		return;

	std::array<SSample, MAX_RENDER_POINTS> aSamples;
	size_t SampleCount = 0;
	// 每段至少一个截面，剩余预算用于屏幕空间细分，不会因达到上限丢掉整条尾部。
	// 像素风保持世界网格与锚点不随缩放变化；其余风格继续使用屏幕空间细分。
	const float Step = std::max({Style == STYLE_PIXEL ? SAMPLE_SPACING : PixelSize * 2.5f, 0.75f, Total / float(MAX_RENDER_POINTS - Count)});
	for(size_t i = 0; i + 1 < Count; ++i)
	{
		const auto &A = vTrail[i];
		const auto &B = vTrail[i + 1];
		const float Segment = aLengths[i + 1] - aLengths[i];
		const int Divisions = std::max(1, int(Segment / Step));
		const SPreparedCurve Curve = PrepareCurve(i > 0 ? vTrail[i - 1].m_Pos : A.m_Pos * 2 - B.m_Pos, A.m_Pos, B.m_Pos, i + 2 < Count ? vTrail[i + 2].m_Pos : B.m_Pos * 2 - A.m_Pos);
		for(int j = 0; j < Divisions + (i + 2 == Count ? 1 : 0); ++j)
		{
			if(SampleCount == MAX_RENDER_POINTS)
				break;
			const float T = float(j) / Divisions;
			SSample &S = aSamples[SampleCount++];
			const double Birth = mix(A.m_Time >= 0 ? A.m_Time : double(A.m_Tick), B.m_Time >= 0 ? B.m_Time : double(B.m_Tick), double(T));
			S.m_Age = std::max(0.0f, float(CurTime - Birth));
			const float Life = mix(A.m_Life > 0 ? A.m_Life : 25.0f * s_aLifeFactors[Style], B.m_Life > 0 ? B.m_Life : 25.0f * s_aLifeFactors[Style], T);
			const float Remaining = std::clamp(1.0f - S.m_Age / Life, 0.0f, 1.0f);
			const float Speed = A.m_Speed >= 0 && B.m_Speed >= 0 ? mix(A.m_Speed, B.m_Speed, T) : Segment / std::max(0.01f, float(std::abs(A.m_Tick - B.m_Tick)));
			S.m_Energy = std::clamp(Speed / 30.0f, 0.0f, 1.0f);
			S.m_Head = mix(aLengths[i], aLengths[i + 1], T);
			S.m_Distance = A.m_Time >= 0 ? mix(A.m_Distance, B.m_Distance, double(T)) : -double(S.m_Head);
			S.m_Tint = Tint(A.m_Col, B.m_Col, T);
			const float Tail = Smooth((Total - S.m_Head) / std::max(1.0f, std::min(Total, 30.0f)));
			const float Tapering = Taper ? std::pow(Remaining, 0.65f) * Tail : 1.0f;
			S.m_Width = Width * (Style == STYLE_ORIGINAL ? 1.0f : 0.7f + 0.4f * S.m_Energy) * Tapering;
			S.m_Alpha = std::clamp(S.m_Tint.a, 0.0f, 1.0f) * Remaining * Remaining * Tail;
			if(Fade)
				S.m_Alpha *= 1.0f - S.m_Head / Total;
			S.m_Remaining = Remaining;
			S.m_Pos = Curve.Evaluate(T);
		}
	}
	if(SampleCount < 2)
		return;
	vOut.reserve(MAX_QUADS);
	if(Style != STYLE_ORIGINAL)
	{
		for(size_t i = 0; i < SampleCount; ++i)
		{
			const vec2 Before = aSamples[i > 0 ? i - 1 : i].m_Pos;
			const vec2 After = aSamples[i + 1 < SampleCount ? i + 1 : i].m_Pos;
			const vec2 Tangent = Unit(After - Before);
			aSamples[i].m_Normal = vec2(-Tangent.y, Tangent.x);
		}
		BuildStyledEffect(aSamples.data(), SampleCount, Style, UsePresetPalette, Width, PixelSize, unsigned(Seed) * 0x9e3779b9u, vOut);
		return;
	}

	// 原版保留原有主体截面、接缝、柔边及浮点运算顺序。
	std::array<SBandPoint, MAX_RENDER_POINTS> aBand;
	std::array<vec2, MAX_RENDER_POINTS> aNormals;
	for(size_t i = 0; i < SampleCount; ++i)
	{
		const SSample &S = aSamples[i];
		aBand[i] = {S.m_Pos, S.m_Width, S.m_Width, S.m_Tint.WithAlpha(S.m_Alpha)};
	}
	QmPrepareTrailBandJoins(aBand.data(), SampleCount, aNormals.data());
	const auto PrepareSection = [&](size_t Index) {
		const SBandPoint &P = aBand[Index];
		return QmPrepareTrailBandSection(P.m_Pos, P.m_Left, P.m_Right, P.m_Color, aNormals[Index], 0.22f, PixelSize);
	};
	SQmTrailBandSection Previous = PrepareSection(0);
	for(size_t i = 1; i < SampleCount; ++i)
	{
		const SQmTrailBandSection Current = PrepareSection(i);
		EmitPreparedBand(vOut, Previous, Current);
		Previous = Current;
	}
}
