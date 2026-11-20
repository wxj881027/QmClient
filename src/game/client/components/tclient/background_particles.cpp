#include "background_particles.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	constexpr int MAX_BACKGROUND_PARTICLES = 200;
	constexpr float MIN_VIEW_SIZE = 1.0f;
	constexpr float MAX_EFFECTIVE_SIZE = 24.0f;
	constexpr int SHAPE_CUBE = 1;
	constexpr int SHAPE_HEART = 2;
	constexpr int SHAPE_MIXED = 3;
	constexpr int SHAPE_SPHERE = 4;
	constexpr int SHAPE_PYRAMID = 5;
	constexpr int SHAPE_DIAMOND = 6;
	constexpr int SHAPE_RING = 7;
	constexpr int SHAPE_STAR = 8;
	constexpr int SHAPE_CRESCENT = 9;
	constexpr int SHAPE_FIRST = SHAPE_CUBE;
	constexpr int SHAPE_LAST = SHAPE_CRESCENT;
	constexpr int REAL_SHAPE_COUNT = 8;
	constexpr int MAX_MESH_VERTICES = 128;
	constexpr int MAX_RENDER_LINES = 512;
	constexpr int HEART_POINTS = 24;
	constexpr int STAR_POINTS = 10;
	constexpr int CRESCENT_SEGMENTS = 28;

	float ClampDelta(float Delta)
	{
		return std::clamp(Delta, 0.0f, 0.05f);
	}

	float ConfigAlpha()
	{
		return std::clamp(g_Config.m_Qm3DParticlesAlpha, 1, 100) / 100.0f;
	}

	float ClampedSizeMin()
	{
		return (float)std::clamp(g_Config.m_Qm3DParticlesSizeMin, 2, 64);
	}

	float ClampedSizeMax()
	{
		const int SizeMin = std::clamp(g_Config.m_Qm3DParticlesSizeMin, 2, 64);
		const int SizeMax = std::clamp(g_Config.m_Qm3DParticlesSizeMax, SizeMin, 64);
		return (float)SizeMax;
	}

	const SBackgroundParticleMesh &HeartMesh()
	{
		static const SBackgroundParticleMesh s_Mesh = [] {
			constexpr float Thickness = 0.18f;
			SBackgroundParticleMesh Mesh;
			Mesh.m_vVertices.reserve(HEART_POINTS * 2);
			Mesh.m_vEdges.reserve(HEART_POINTS * 2 + HEART_POINTS / 4);
			Mesh.m_vFaces.reserve(HEART_POINTS * 2);
			for(int Layer = 0; Layer < 2; ++Layer)
			{
				const float Z = Layer == 0 ? -Thickness : Thickness;
				for(int Point = 0; Point < HEART_POINTS; ++Point)
				{
					const float T = 2.0f * pi * (float)Point / (float)HEART_POINTS;
					const float X = 16.0f * std::pow(std::sin(T), 3.0f) * 0.055f;
					const float Y = -(13.0f * std::cos(T) - 5.0f * std::cos(2.0f * T) - 2.0f * std::cos(3.0f * T) - std::cos(4.0f * T)) * 0.055f;
					Mesh.m_vVertices.emplace_back(X, Y, Z);
				}
			}
			for(int Point = 0; Point < HEART_POINTS; ++Point)
			{
				const int Next = (Point + 1) % HEART_POINTS;
				Mesh.m_vEdges.push_back({Point, Next});
				Mesh.m_vEdges.push_back({Point + HEART_POINTS, Next + HEART_POINTS});
				if(Point % 4 == 0)
					Mesh.m_vEdges.push_back({Point, Point + HEART_POINTS});
				Mesh.m_vFaces.push_back({Point, Next, Next + HEART_POINTS});
				Mesh.m_vFaces.push_back({Point, Next + HEART_POINTS, Point + HEART_POINTS});
			}
			return Mesh;
		}();
		return s_Mesh;
	}

	const SBackgroundParticleMesh &StarMesh()
	{
		static const SBackgroundParticleMesh s_Mesh = [] {
			SBackgroundParticleMesh Mesh;
			Mesh.m_vVertices.reserve(STAR_POINTS * 2);
			Mesh.m_vEdges.reserve(STAR_POINTS * 2 + STAR_POINTS / 2);
			Mesh.m_vFaces.reserve(STAR_POINTS * 2);
			for(int Point = 0; Point < STAR_POINTS; ++Point)
			{
				const float T = -0.5f * pi + 2.0f * pi * (float)Point / (float)STAR_POINTS;
				const float Radius = (Point % 2) == 0 ? 1.18f : 0.48f;
				Mesh.m_vVertices.emplace_back(std::cos(T) * Radius, std::sin(T) * Radius, -0.26f);
				Mesh.m_vVertices.emplace_back(std::cos(T) * Radius, std::sin(T) * Radius, 0.26f);
			}
			for(int Point = 0; Point < STAR_POINTS; ++Point)
			{
				const int Next = (Point + 1) % STAR_POINTS;
				const int Front = Point * 2;
				const int Back = Front + 1;
				const int NextFront = Next * 2;
				const int NextBack = NextFront + 1;
				Mesh.m_vEdges.push_back({Front, NextFront});
				Mesh.m_vEdges.push_back({Back, NextBack});
				if(Point % 2 == 0)
					Mesh.m_vEdges.push_back({Front, Back});
				Mesh.m_vFaces.push_back({Front, NextFront, NextBack});
				Mesh.m_vFaces.push_back({Front, NextBack, Back});
			}
			return Mesh;
		}();
		return s_Mesh;
	}

	const SBackgroundParticleMesh &CrescentMesh()
	{
		static const SBackgroundParticleMesh s_Mesh = [] {
			constexpr int BoundaryPoints = CRESCENT_SEGMENTS * 2;
			constexpr float Start = -0.78f * pi;
			constexpr float End = 0.78f * pi;
			constexpr float Thickness = 0.12f;
			std::array<vec2, BoundaryPoints> aBoundary;
			for(int Point = 0; Point < CRESCENT_SEGMENTS; ++Point)
			{
				const float T = Start + (End - Start) * (float)Point / (float)(CRESCENT_SEGMENTS - 1);
				aBoundary[Point] = vec2(std::cos(T), std::sin(T));
			}
			for(int Point = 0; Point < CRESCENT_SEGMENTS; ++Point)
			{
				const float T = End - (End - Start) * (float)Point / (float)(CRESCENT_SEGMENTS - 1);
				aBoundary[Point + CRESCENT_SEGMENTS] = vec2(std::cos(T) * 0.58f + 0.36f, std::sin(T) * 0.58f);
			}

			SBackgroundParticleMesh Mesh;
			Mesh.m_vVertices.reserve(BoundaryPoints * 2);
			Mesh.m_vEdges.reserve(BoundaryPoints * 2 + 8);
			Mesh.m_vFaces.reserve(BoundaryPoints * 2);
			for(int Layer = 0; Layer < 2; ++Layer)
			{
				const float Z = Layer == 0 ? -Thickness : Thickness;
				for(const vec2 &Point : aBoundary)
					Mesh.m_vVertices.emplace_back(Point.x, Point.y, Z);
			}
			for(int Point = 0; Point < BoundaryPoints; ++Point)
			{
				const int Next = (Point + 1) % BoundaryPoints;
				Mesh.m_vEdges.push_back({Point, Next});
				Mesh.m_vEdges.push_back({Point + BoundaryPoints, Next + BoundaryPoints});
				if(Point % 7 == 0)
					Mesh.m_vEdges.push_back({Point, Point + BoundaryPoints});
				Mesh.m_vFaces.push_back({Point, Next, Next + BoundaryPoints});
				Mesh.m_vFaces.push_back({Point, Next + BoundaryPoints, Point + BoundaryPoints});
			}
			return Mesh;
		}();
		return s_Mesh;
	}
}

void CBackgroundParticles::OnReset()
{
	ResetParticles();
}

void CBackgroundParticles::ResetParticles()
{
	m_vParticles.clear();
	m_vRenderOrder.clear();
	m_RenderOrderDirty = true;
	m_LastConfiguredCount = -1;
}

void CBackgroundParticles::CurrentWorldView(float &Left, float &Top, float &Right, float &Bottom) const
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float Zoom = GameClient()->m_Camera.m_Zoom;
	float aPoints[4];
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100, 100, 100, 0, 0, Graphics()->GameScreenAspect(), Zoom, aPoints);
	Left = minimum(aPoints[0], aPoints[2]);
	Top = minimum(aPoints[1], aPoints[3]);
	Right = maximum(aPoints[0], aPoints[2]);
	Bottom = maximum(aPoints[1], aPoints[3]);

	if(Right - Left < MIN_VIEW_SIZE)
		Right = Left + MIN_VIEW_SIZE;
	if(Bottom - Top < MIN_VIEW_SIZE)
		Bottom = Top + MIN_VIEW_SIZE;
}

ColorRGBA CBackgroundParticles::ParticleColor() const
{
	if(g_Config.m_Qm3DParticlesColorMode == 2)
	{
		return color_cast<ColorRGBA>(ColorHSLA(random_float(), random_float(0.55f, 0.85f), random_float(0.55f, 0.75f), 1.0f));
	}
	return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_Qm3DParticlesColor, true));
}

int CBackgroundParticles::ParticleType() const
{
	const int Type = std::clamp(g_Config.m_Qm3DParticlesType, SHAPE_FIRST, SHAPE_LAST);
	if(Type != SHAPE_MIXED)
		return Type;

	const std::array<int, REAL_SHAPE_COUNT> aRealShapes = {
		SHAPE_CUBE,
		SHAPE_HEART,
		SHAPE_SPHERE,
		SHAPE_PYRAMID,
		SHAPE_DIAMOND,
		SHAPE_RING,
		SHAPE_STAR,
		SHAPE_CRESCENT,
	};
	const int ShapeIndex = std::clamp((int)random_float((float)REAL_SHAPE_COUNT), 0, REAL_SHAPE_COUNT - 1);
	return aRealShapes[ShapeIndex];
}

void CBackgroundParticles::SpawnParticle(SParticle &Particle, bool Initial, float Left, float Top, float Right, float Bottom)
{
	const float Margin = (float)std::clamp(g_Config.m_Qm3DParticlesViewMargin, 0, 1000);
	const float DepthRange = (float)std::clamp(g_Config.m_Qm3DParticlesDepth, 10, 1000);
	const float SizeMin = ClampedSizeMin();
	const float SizeMax = ClampedSizeMax();
	m_RenderOrderDirty = true;
	Particle.m_Depth = random_float(0.0f, DepthRange);
	const float DepthFactor = std::clamp(Particle.m_Depth / DepthRange, 0.0f, 1.0f);
	Particle.m_Size = random_float(SizeMin, SizeMax);
	Particle.m_Rotation = vec3(random_float(-0.35f, 0.35f), random_float(-0.35f, 0.35f), random_float(0.0f, 2.0f * pi));
	Particle.m_RotationSpeed = vec3(random_float(-0.08f, 0.08f), random_float(-0.08f, 0.08f), random_float(-0.2f, 0.2f)) * (1.0f - DepthFactor * 0.45f);
	const float Speed = (float)std::clamp(g_Config.m_Qm3DParticlesSpeed, 1, 500) * (1.0f - DepthFactor * 0.60f);
	Particle.m_DriftVel = random_direction() * random_float(0.35f, 1.0f) * Speed;
	Particle.m_PushVel = vec2(0.0f, 0.0f);
	Particle.m_Age = 0.0f;
	Particle.m_Life = random_float(8.0f, 18.0f);
	Particle.m_PulsePhase = random_float(0.0f, 2.0f * pi);
	Particle.m_TwinklePhase = random_float(0.0f, 2.0f * pi);
	Particle.m_TrailSample = 0.0f;
	Particle.m_Type = ParticleType();
	Particle.m_Color = ParticleColor();

	if(Initial)
	{
		Particle.m_Pos = vec2(random_float(Left - Margin, Right + Margin), random_float(Top - Margin, Bottom + Margin));
		Particle.m_Age = random_float(0.0f, Particle.m_Life * 0.75f);
		ResetParticleTrail(Particle);
		return;
	}

	switch((int)random_float(4.0f))
	{
	case 0:
		Particle.m_Pos = vec2(Left - Margin, random_float(Top - Margin, Bottom + Margin));
		break;
	case 1:
		Particle.m_Pos = vec2(Right + Margin, random_float(Top - Margin, Bottom + Margin));
		break;
	case 2:
		Particle.m_Pos = vec2(random_float(Left - Margin, Right + Margin), Top - Margin);
		break;
	default:
		Particle.m_Pos = vec2(random_float(Left - Margin, Right + Margin), Bottom + Margin);
		break;
	}
	ResetParticleTrail(Particle);
}

void CBackgroundParticles::EnsureParticleCount(float Left, float Top, float Right, float Bottom)
{
	const int Count = std::clamp(g_Config.m_Qm3DParticlesCount, 1, MAX_BACKGROUND_PARTICLES);
	if(m_LastConfiguredCount == Count && (int)m_vParticles.size() == Count)
		return;

	m_RenderOrderDirty = true;
	m_LastConfiguredCount = Count;
	if((int)m_vParticles.size() > Count)
	{
		m_vParticles.resize(Count);
		return;
	}

	const size_t OldSize = m_vParticles.size();
	m_vParticles.resize(Count);
	for(size_t ParticleIndex = OldSize; ParticleIndex < m_vParticles.size(); ++ParticleIndex)
		SpawnParticle(m_vParticles[ParticleIndex], true, Left, Top, Right, Bottom);
}

void CBackgroundParticles::ResetParticleTrail(SParticle &Particle) const
{
	Particle.m_TrailSample = 0.0f;
	Particle.m_TrailCount = 0;
	for(vec2 &TrailPos : Particle.m_aTrailPos)
		TrailPos = Particle.m_Pos;
}

void CBackgroundParticles::UpdateParticleTrail(SParticle &Particle, float Delta) const
{
	if(!g_Config.m_Qm3DParticlesTrail)
	{
		Particle.m_TrailCount = 0;
		Particle.m_TrailSample = 0.0f;
		return;
	}

	const int TrailLength = std::clamp(g_Config.m_Qm3DParticlesTrailLength, 2, MAX_TRAIL_POINTS);
	constexpr float SampleInterval = 0.045f;
	Particle.m_TrailSample += Delta;
	if(Particle.m_TrailCount > 0 && Particle.m_TrailSample < SampleInterval)
		return;

	Particle.m_TrailSample = 0.0f;
	for(int TrailIndex = minimum(TrailLength - 1, Particle.m_TrailCount); TrailIndex > 0; --TrailIndex)
		Particle.m_aTrailPos[TrailIndex] = Particle.m_aTrailPos[TrailIndex - 1];
	Particle.m_aTrailPos[0] = Particle.m_Pos;
	Particle.m_TrailCount = minimum(Particle.m_TrailCount + 1, TrailLength);
}

void CBackgroundParticles::ApplyPlayerPush(SParticle &Particle, float Delta) const
{
	const float Radius = (float)std::clamp(g_Config.m_Qm3DParticlesPushRadius, 0, 1000);
	const float Strength = (float)std::clamp(g_Config.m_Qm3DParticlesPushStrength, 0, 2000);
	if(Radius <= 0.0f || Strength <= 0.0f)
		return;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		const vec2 Diff = Particle.m_Pos - ClientData.m_RenderPos;
		const float Dist = length(Diff);
		if(Dist <= 0.001f || Dist >= Radius)
			continue;

		const float Force = 1.0f - Dist / Radius;
		Particle.m_PushVel += normalize(Diff) * Strength * Force * Delta * 10.0f;
	}
}

void CBackgroundParticles::UpdateParticle(SParticle &Particle, float Delta, float Left, float Top, float Right, float Bottom)
{
	Particle.m_Age += Delta;
	if(Particle.m_Age >= Particle.m_Life)
	{
		SpawnParticle(Particle, false, Left, Top, Right, Bottom);
		return;
	}

	ApplyPlayerPush(Particle, Delta);
	Particle.m_Pos += (Particle.m_DriftVel + Particle.m_PushVel) * Delta;
	Particle.m_PushVel *= std::pow(0.12f, Delta);
	Particle.m_Rotation += Particle.m_RotationSpeed * Delta;
	UpdateParticleTrail(Particle, Delta);

	const float Margin = (float)std::clamp(g_Config.m_Qm3DParticlesViewMargin, 0, 1000);
	if(Particle.m_Pos.x < Left - Margin || Particle.m_Pos.x > Right + Margin ||
		Particle.m_Pos.y < Top - Margin || Particle.m_Pos.y > Bottom + Margin)
	{
		SpawnParticle(Particle, false, Left, Top, Right, Bottom);
	}
}

void CBackgroundParticles::ApplyParticleCollisions(float Delta)
{
	if(!g_Config.m_Qm3DParticlesCollide)
		return;

	for(size_t LeftIndex = 0; LeftIndex < m_vParticles.size(); ++LeftIndex)
	{
		for(size_t RightIndex = LeftIndex + 1; RightIndex < m_vParticles.size(); ++RightIndex)
		{
			SParticle &Left = m_vParticles[LeftIndex];
			SParticle &Right = m_vParticles[RightIndex];
			const vec2 Diff = Right.m_Pos - Left.m_Pos;
			const float MinDist = (Left.m_Size + Right.m_Size) * 0.45f;
			if(absolute(Diff.x) >= MinDist || absolute(Diff.y) >= MinDist)
				continue;
			const float Dist = length(Diff);
			if(Dist <= 0.001f || Dist >= MinDist)
				continue;

			const vec2 Dir = Diff / Dist;
			const float Push = (MinDist - Dist) * Delta * 12.0f;
			Left.m_PushVel -= Dir * Push;
			Right.m_PushVel += Dir * Push;
		}
	}
}

float CBackgroundParticles::ParticleAlpha(const SParticle &Particle) const
{
	const float FadeIn = std::max(0.001f, g_Config.m_Qm3DParticlesFadeInMs / 1000.0f);
	const float FadeOut = std::max(0.001f, g_Config.m_Qm3DParticlesFadeOutMs / 1000.0f);
	const float In = std::clamp(Particle.m_Age / FadeIn, 0.0f, 1.0f);
	const float Out = std::clamp((Particle.m_Life - Particle.m_Age) / FadeOut, 0.0f, 1.0f);
	return ConfigAlpha() * minimum(In, Out);
}

void CBackgroundParticles::RenderMesh(const SBackgroundParticleMesh &Mesh, vec2 WorldPos, float Depth, float Size, const vec3 &Rotation, ColorRGBA Color, const SBackgroundParticleProjection &Projection, vec2 ScreenOffset) const
{
	if(Mesh.m_vVertices.empty() || Mesh.m_vVertices.size() > MAX_MESH_VERTICES)
		return;

	const float DepthRange = (float)std::clamp(g_Config.m_Qm3DParticlesDepth, 10, 1000);
	const SBackgroundParticleRotation PreparedRotation(Rotation);
	std::array<vec2, MAX_MESH_VERTICES> aProjected;
	for(size_t VertexIndex = 0; VertexIndex < Mesh.m_vVertices.size(); ++VertexIndex)
	{
		const vec3 RotatedVertex = PreparedRotation.Apply(Mesh.m_vVertices[VertexIndex] * Size);
		aProjected[VertexIndex] = BackgroundParticleProjectVertex(Projection, WorldPos, RotatedVertex, Depth, DepthRange) + ScreenOffset;
	}

	Graphics()->SetColor(Color);
	std::array<IGraphics::CLineItem, MAX_RENDER_LINES> aLines;
	int LineCount = 0;
	auto FlushLines = [&] {
		if(LineCount == 0)
			return;
		Graphics()->LinesDraw(aLines.data(), LineCount);
		LineCount = 0;
	};
	auto AddLine = [&](vec2 Start, vec2 End) {
		if(LineCount == MAX_RENDER_LINES)
			FlushLines();
		aLines[LineCount++] = IGraphics::CLineItem(Start, End);
	};

	for(const auto &Edge : Mesh.m_vEdges)
	{
		if(Edge[0] < 0 || Edge[1] < 0 || Edge[0] >= (int)Mesh.m_vVertices.size() || Edge[1] >= (int)Mesh.m_vVertices.size())
			continue;
		AddLine(aProjected[Edge[0]], aProjected[Edge[1]]);
	}

	const float PointRadius = std::clamp(Size * 0.035f, 0.45f, 0.9f);
	for(size_t VertexIndex = 0; VertexIndex < Mesh.m_vVertices.size(); ++VertexIndex)
	{
		const vec2 Point = aProjected[VertexIndex];
		AddLine(Point - vec2(PointRadius, 0.0f), Point + vec2(PointRadius, 0.0f));
		AddLine(Point - vec2(0.0f, PointRadius), Point + vec2(0.0f, PointRadius));
	}
	FlushLines();
}

void CBackgroundParticles::RenderShape(int Type, vec2 WorldPos, float Depth, float Size, const vec3 &Rotation, ColorRGBA Color, const SBackgroundParticleProjection &Projection, vec2 ScreenOffset) const
{
	// 与线段顶点的 8 位 Alpha 舍入一致，跳过完全透明的线框。
	if(round_to_int(Color.a * 255.0f) <= 0)
		return;

	const SBackgroundParticleMesh *pMesh = &BackgroundParticleCubeMesh();
	switch(Type)
	{
	case SHAPE_HEART:
		pMesh = &HeartMesh();
		break;
	case SHAPE_SPHERE:
		pMesh = &BackgroundParticleSphereMesh();
		break;
	case SHAPE_PYRAMID:
		pMesh = &BackgroundParticlePyramidMesh();
		break;
	case SHAPE_DIAMOND:
		pMesh = &BackgroundParticleOctahedronMesh();
		break;
	case SHAPE_RING:
		pMesh = &BackgroundParticleTorusMesh();
		break;
	case SHAPE_STAR:
		pMesh = &StarMesh();
		break;
	case SHAPE_CRESCENT:
		pMesh = &CrescentMesh();
		break;
	default:
		break;
	}
	RenderMesh(*pMesh, WorldPos, Depth, Size, Rotation, Color, Projection, ScreenOffset);
}

void CBackgroundParticles::RenderParticleTrail(const SParticle &Particle, float Size, ColorRGBA Color, const SBackgroundParticleProjection &Projection) const
{
	if(!g_Config.m_Qm3DParticlesTrail || Particle.m_TrailCount <= 1)
		return;

	const int TrailLength = std::clamp(g_Config.m_Qm3DParticlesTrailLength, 2, MAX_TRAIL_POINTS);
	const int TrailCount = minimum(Particle.m_TrailCount, TrailLength);
	const float TrailAlpha = std::clamp(g_Config.m_Qm3DParticlesTrailAlpha, 1, 100) / 100.0f;

	for(int TrailIndex = TrailCount - 1; TrailIndex >= 1; --TrailIndex)
	{
		const float Fade = (float)(TrailCount - TrailIndex) / (float)TrailCount;
		const float TrailSize = Size * (0.82f + 0.18f * Fade);
		RenderShape(Particle.m_Type, Particle.m_aTrailPos[TrailIndex], Particle.m_Depth, TrailSize, Particle.m_Rotation, Color.WithMultipliedAlpha(TrailAlpha * Fade), Projection);
	}
}

void CBackgroundParticles::RenderParticle(const SParticle &Particle, const SBackgroundParticleProjection &Projection) const
{
	const float DepthRange = (float)std::clamp(g_Config.m_Qm3DParticlesDepth, 10, 1000);
	const float DepthFactor = std::clamp(Particle.m_Depth / DepthRange, 0.0f, 1.0f);
	float Size = minimum(Particle.m_Size, MAX_EFFECTIVE_SIZE);
	if(g_Config.m_Qm3DParticlesPulse)
	{
		const float PulseStrength = std::clamp(g_Config.m_Qm3DParticlesPulseStrength, 0, 50) / 100.0f;
		const float PulseSpeed = std::clamp(g_Config.m_Qm3DParticlesPulseSpeed, 10, 300) / 100.0f;
		Size *= maximum(0.2f, 1.0f + std::sin(Particle.m_Age * PulseSpeed * 2.0f * pi + Particle.m_PulsePhase) * PulseStrength);
	}

	float Alpha = ParticleAlpha(Particle) * (1.0f - DepthFactor * 0.35f);
	if(g_Config.m_Qm3DParticlesTwinkle)
	{
		const float TwinkleStrength = std::clamp(g_Config.m_Qm3DParticlesTwinkleStrength, 0, 100) / 100.0f;
		const float TwinkleWave = 0.5f + 0.5f * std::sin(Particle.m_Age * 3.2f + Particle.m_TwinklePhase);
		const float TwinkleAlpha = 0.35f + 0.65f * TwinkleWave;
		Alpha *= 1.0f + (TwinkleAlpha - 1.0f) * TwinkleStrength;
	}
	if(Size <= 0.5f)
		return;

	const ColorRGBA Color = Particle.m_Color.WithMultipliedAlpha(Alpha);
	RenderParticleTrail(Particle, Size, Color, Projection);

	if(g_Config.m_Qm3DParticlesGlow)
	{
		const float GlowAlpha = std::clamp(g_Config.m_Qm3DParticlesGlowAlpha, 1, 100) / 100.0f;
		const float GlowOffset = (float)std::clamp(g_Config.m_Qm3DParticlesGlowOffset, 1, 20);
		const ColorRGBA GlowColor = Color.WithMultipliedAlpha(GlowAlpha);
		RenderShape(Particle.m_Type, Particle.m_Pos, Particle.m_Depth, Size, Particle.m_Rotation, GlowColor, Projection, -vec2(GlowOffset, GlowOffset));
	}

	RenderShape(Particle.m_Type, Particle.m_Pos, Particle.m_Depth, Size, Particle.m_Rotation, Color, Projection);
}

void CBackgroundParticles::OnRender()
{
	if(!g_Config.m_Qm3DParticles)
	{
		m_LastConfiguredCount = -1;
		return;
	}

	float Left;
	float Top;
	float Right;
	float Bottom;
	CurrentWorldView(Left, Top, Right, Bottom);
	EnsureParticleCount(Left, Top, Right, Bottom);
	if(m_vParticles.empty())
		return;

	const float Delta = ClampDelta(Client()->RenderFrameTime());
	const bool ViewJumped = absolute(Left - m_LastLeft) + absolute(Top - m_LastTop) + absolute(Right - m_LastRight) + absolute(Bottom - m_LastBottom) > (Right - Left + Bottom - Top);
	m_LastLeft = Left;
	m_LastTop = Top;
	m_LastRight = Right;
	m_LastBottom = Bottom;
	if(ViewJumped)
	{
		for(SParticle &Particle : m_vParticles)
			SpawnParticle(Particle, true, Left, Top, Right, Bottom);
	}

	for(SParticle &Particle : m_vParticles)
		UpdateParticle(Particle, Delta, Left, Top, Right, Bottom);
	ApplyParticleCollisions(Delta);

	if(m_RenderOrderDirty)
	{
		m_vRenderOrder.clear();
		m_vRenderOrder.reserve(m_vParticles.size());
		for(size_t ParticleIndex = 0; ParticleIndex < m_vParticles.size(); ++ParticleIndex)
			m_vRenderOrder.push_back((int)ParticleIndex);
		std::sort(m_vRenderOrder.begin(), m_vRenderOrder.end(), [&](int LeftIndex, int RightIndex) {
			return m_vParticles[LeftIndex].m_Depth > m_vParticles[RightIndex].m_Depth;
		});
		m_RenderOrderDirty = false;
	}

	SBackgroundParticleProjection Projection;
	Projection.m_CameraCenter = GameClient()->m_Camera.m_Center;
	Projection.m_WorldLeft = Left;
	Projection.m_WorldTop = Top;
	Projection.m_WorldRight = Right;
	Projection.m_WorldBottom = Bottom;
	Projection.m_ViewportWidth = (float)Graphics()->ScreenWidth();
	Projection.m_ViewportHeight = (float)Graphics()->ScreenHeight();

	Graphics()->MapScreen(0.0f, 0.0f, Projection.m_ViewportWidth, Projection.m_ViewportHeight);
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	for(const int ParticleIndex : m_vRenderOrder)
		RenderParticle(m_vParticles[ParticleIndex], Projection);
	Graphics()->LinesEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
