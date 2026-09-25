#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_EMOTICON_PROJECTILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_EMOTICON_PROJECTILE_H

#include <base/vmath.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace QmEmoticon
{
	struct SPlayerBox
	{
		int m_ClientId = -1;
		vec2 m_Pos = vec2(0, 0);
		float m_Half = 0;
	};

	class CAlphaMask
	{
		struct SRect
		{
			int m_Left;
			int m_Top;
			int m_Right;
			int m_Bottom;
		};
		std::vector<SRect> m_vRects;
		int m_Width = 1;
		int m_Height = 1;

	public:
		void Build(const unsigned char *pRgba, int Width, int Height, int Stride = 0)
		{
			m_vRects.clear();
			m_Width = std::max(1, Width);
			m_Height = std::max(1, Height);
			if(pRgba == nullptr || Width <= 0 || Height <= 0)
				return;
			if(Stride == 0)
				Stride = Width * 4;
			for(int Y = 0; Y < Height; ++Y)
			{
				for(int X = 0; X < Width;)
				{
					if(pRgba[Y * Stride + X * 4 + 3] == 0)
					{
						++X;
						continue;
					}
					const int Left = X++;
					while(X < Width && pRgba[Y * Stride + X * 4 + 3] != 0)
						++X;
					if(!m_vRects.empty() && m_vRects.back().m_Bottom == Y && m_vRects.back().m_Left == Left && m_vRects.back().m_Right == X)
						m_vRects.back().m_Bottom = Y + 1;
					else
						m_vRects.push_back({Left, Y, X, Y + 1});
				}
			}
		}

		template<typename TSolid>
		bool Overlaps(vec2 Pos, float Size, float Angle, const TSolid &Solid) const
		{
			if(m_vRects.empty() || Size <= 0.0f)
				return false;
			const vec2 AxisX = direction(Angle);
			const vec2 AxisY(-AxisX.y, AxisX.x);
			const vec2 AbsX(std::abs(AxisX.x), std::abs(AxisX.y));
			const vec2 AbsY(std::abs(AxisY.x), std::abs(AxisY.y));
			const float Radius = Size * 0.707107f;
			for(int Y = (int)std::floor((Pos.y - Radius) / 32.0f); Y <= (int)std::floor((Pos.y + Radius) / 32.0f); ++Y)
			{
				for(int X = (int)std::floor((Pos.x - Radius) / 32.0f); X <= (int)std::floor((Pos.x + Radius) / 32.0f); ++X)
				{
					if(!Solid(X, Y))
						continue;
					const vec2 TileCenter(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
					for(const SRect &Rect : m_vRects)
					{
						const vec2 Half((Rect.m_Right - Rect.m_Left) * Size / (2.0f * m_Width), (Rect.m_Bottom - Rect.m_Top) * Size / (2.0f * m_Height));
						const vec2 Local(((Rect.m_Left + Rect.m_Right) / (2.0f * m_Width) - 0.5f) * Size, ((Rect.m_Top + Rect.m_Bottom) / (2.0f * m_Height) - 0.5f) * Size);
						const vec2 Delta = TileCenter - (Pos + AxisX * Local.x + AxisY * Local.y);
						if(std::abs(Delta.x) < 16.0f + AbsX.x * Half.x + AbsY.x * Half.y - 0.0001f &&
							std::abs(Delta.y) < 16.0f + AbsX.y * Half.x + AbsY.y * Half.y - 0.0001f &&
							std::abs(dot(Delta, AxisX)) < Half.x + 16.0f * (AbsX.x + AbsX.y) - 0.0001f &&
							std::abs(dot(Delta, AxisY)) < Half.y + 16.0f * (AbsY.x + AbsY.y) - 0.0001f)
							return true;
					}
				}
			}
			return false;
		}

		bool OverlapsBox(vec2 Pos, float Size, float Angle, vec2 BoxCenter, vec2 BoxHalf) const
		{
			if(m_vRects.empty() || Size <= 0.0f)
				return false;
			const vec2 AxisX = direction(Angle);
			const vec2 AxisY(-AxisX.y, AxisX.x);
			const vec2 AbsX(std::abs(AxisX.x), std::abs(AxisX.y));
			const vec2 AbsY(std::abs(AxisY.x), std::abs(AxisY.y));
			for(const SRect &Rect : m_vRects)
			{
				const vec2 Half((Rect.m_Right - Rect.m_Left) * Size / (2.0f * m_Width), (Rect.m_Bottom - Rect.m_Top) * Size / (2.0f * m_Height));
				const vec2 Local(((Rect.m_Left + Rect.m_Right) / (2.0f * m_Width) - 0.5f) * Size, ((Rect.m_Top + Rect.m_Bottom) / (2.0f * m_Height) - 0.5f) * Size);
				const vec2 Delta = BoxCenter - (Pos + AxisX * Local.x + AxisY * Local.y);
				if(std::abs(Delta.x) < BoxHalf.x + AbsX.x * Half.x + AbsY.x * Half.y - 0.0001f &&
					std::abs(Delta.y) < BoxHalf.y + AbsX.y * Half.x + AbsY.y * Half.y - 0.0001f &&
					std::abs(dot(Delta, AxisX)) < Half.x + BoxHalf.x * AbsX.x + BoxHalf.y * AbsX.y - 0.0001f &&
					std::abs(dot(Delta, AxisY)) < Half.y + BoxHalf.x * AbsY.x + BoxHalf.y * AbsY.y - 0.0001f)
					return true;
			}
			return false;
		}
	};

	inline bool OverlapsPlayerBoxes(const CAlphaMask &Mask, vec2 Pos, float Size, float Angle, int OwnerClientId, const SPlayerBox *pBoxes, int NumBoxes)
	{
		for(int Index = 0; pBoxes != nullptr && Index < NumBoxes; ++Index)
		{
			if(pBoxes[Index].m_ClientId != OwnerClientId && Mask.OverlapsBox(Pos, Size, Angle, pBoxes[Index].m_Pos, vec2(pBoxes[Index].m_Half, pBoxes[Index].m_Half)))
				return true;
		}
		return false;
	}
}

struct CEmoticonProjectile
{
	vec2 m_Pos = vec2(0.0f, 0.0f);
	vec2 m_Vel = vec2(0.0f, 0.0f);
	float m_Angle = 0.0f;
	float m_AngVel = 0.0f;
	float m_LifeTime = 0.0f;
	float m_SizeScale = 1.0f;
	float m_SizeLimit = 128.0f;
	bool m_Active = false;
	int m_Emoticon = 0;
	int m_OwnerClientId = -1;
	double m_Accumulator = 0.0;
	vec2 m_PreviousPos = vec2(0.0f, 0.0f);
	float m_PreviousAngle = 0.0f;
	static constexpr double STEP = 1.0 / 240.0;

	void Init(vec2 Pos, vec2 Vel, int Emoticon, float SizeScale = 1.0f, int OwnerClientId = -1)
	{
		m_Pos = m_PreviousPos = Pos;
		m_Vel = Vel;
		m_Emoticon = Emoticon;
		m_SizeScale = std::max(SizeScale, 0.1f);
		m_SizeLimit = 128.0f * m_SizeScale;
		m_Angle = m_PreviousAngle = 0.0f;
		m_AngVel = ((rand() % 100) - 50) / 10.0f;
		m_LifeTime = 3.0f;
		m_Active = true;
		m_OwnerClientId = OwnerClientId;
		m_Accumulator = 0.0;
	}

	float Size() const { return std::min(m_SizeLimit, 64.0f * m_SizeScale * (1.0f + std::max(0.0f, 0.5f - m_LifeTime) * 2.0f)); }

	template<typename TSolid>
	bool PlaceOutside(const QmEmoticon::CAlphaMask &Mask, const TSolid &Solid)
	{
		if(!Mask.Overlaps(m_Pos, Size(), m_Angle, Solid))
			return true;
		const int MaxRadius = (int)(Size() + 32);
		for(int Radius = 2; Radius <= MaxRadius; Radius += 2)
			for(int Index = 0; Index < 16; ++Index)
			{
				const vec2 Candidate = m_Pos + direction(-pi / 2 + Index * pi / 8) * (float)Radius;
				if(!Mask.Overlaps(Candidate, Size(), m_Angle, Solid))
				{
					m_Pos = m_PreviousPos = Candidate;
					return true;
				}
			}
		return false;
	}

	template<typename TSolid>
	void Update(float Dt, const QmEmoticon::CAlphaMask &Mask, const TSolid &Solid, const QmEmoticon::SPlayerBox *pPlayerBoxes = nullptr, int NumPlayerBoxes = 0)
	{
		if(!m_Active || Dt <= 0.0f)
			return;
		if(Dt >= m_LifeTime)
		{
			m_LifeTime = 0.0f;
			m_Active = false;
			return;
		}
		m_Accumulator += Dt;
		const auto Blocked = [&](vec2 Pos, float Size, float Angle) {
			return Mask.Overlaps(Pos, Size, Angle, Solid) || QmEmoticon::OverlapsPlayerBoxes(Mask, Pos, Size, Angle, m_OwnerClientId, pPlayerBoxes, NumPlayerBoxes);
		};
		while(m_Accumulator + 1e-9 >= STEP && m_Active)
		{
			m_Accumulator -= STEP;
			const float PreviousSize = Size();
			m_LifeTime -= STEP;
			if(m_LifeTime <= 0.0f)
			{
				m_Active = false;
				break;
			}
			// 空间不足时冻结消失阶段的膨胀；旧轮廓也在墙内则停止飞行。
			if(Mask.Overlaps(m_Pos, Size(), m_Angle, Solid))
			{
				if(PreviousSize < Size() && !Mask.Overlaps(m_Pos, PreviousSize, m_Angle, Solid))
					m_SizeLimit = PreviousSize;
				else
				{
					m_Active = false;
					break;
				}
			}
			m_PreviousPos = m_Pos;
			m_PreviousAngle = m_Angle;
			m_Vel.y += 1500.0f * STEP;
			const int Steps = std::max(1, (int)std::ceil((length(m_Vel) + std::abs(m_AngVel) * Size()) * STEP));
			const float SubDt = STEP / Steps;
			for(int Step = 0; Step < Steps; ++Step)
			{
				for(int Axis = 0; Axis < 2; ++Axis)
				{
					vec2 Next = m_Pos;
					float &Velocity = Axis == 0 ? m_Vel.x : m_Vel.y;
					(Axis == 0 ? Next.x : Next.y) += Velocity * SubDt;
					if(Blocked(Next, Size(), m_Angle))
						Velocity *= -0.6f;
					else
						m_Pos = Next;
				}
				const float NextAngle = m_Angle + m_AngVel * SubDt;
				if(Blocked(m_Pos, Size(), NextAngle))
					m_AngVel *= -0.6f;
				else
					m_Angle = NextAngle;
			}
		}
	}
};

namespace QmEmoticon
{
	template<std::size_t N>
	CEmoticonProjectile *ProjectileSlot(CEmoticonProjectile (&aProjectiles)[N])
	{
		auto *pOldest = &aProjectiles[0];
		for(auto &Projectile : aProjectiles)
		{
			if(!Projectile.m_Active)
				return &Projectile;
			if(Projectile.m_LifeTime < pOldest->m_LifeTime)
				pOldest = &Projectile;
		}
		return pOldest;
	}
}

#endif
