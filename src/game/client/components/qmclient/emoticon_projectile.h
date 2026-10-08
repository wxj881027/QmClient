#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_EMOTICON_PROJECTILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_EMOTICON_PROJECTILE_H

#include <base/vmath.h>

#include <game/teamscore.h>

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
			vec2 m_NormalizedCenter = vec2(0.0f, 0.0f);
		};
		struct SBoundsNode
		{
			SRect m_Bounds;
			std::size_t m_FirstRect;
			std::size_t m_EndRect;
			std::size_t m_NextNode;
		};
		static constexpr std::size_t RECTS_PER_LEAF = 8;
		std::vector<SRect> m_vRects;
		std::vector<SBoundsNode> m_vBoundsNodes;
		int m_Width = 1;
		int m_Height = 1;
		vec2 m_BoundsCenter = vec2(0.0f, 0.0f);
		vec2 m_BoundsHalf = vec2(0.0f, 0.0f);

		void BuildBoundsNodes(std::size_t FirstRect, std::size_t EndRect)
		{
			SRect Bounds = m_vRects[FirstRect];
			for(std::size_t Index = FirstRect + 1; Index < EndRect; ++Index)
			{
				const SRect &Rect = m_vRects[Index];
				Bounds.m_Left = std::min(Bounds.m_Left, Rect.m_Left);
				Bounds.m_Top = std::min(Bounds.m_Top, Rect.m_Top);
				Bounds.m_Right = std::max(Bounds.m_Right, Rect.m_Right);
				Bounds.m_Bottom = std::max(Bounds.m_Bottom, Rect.m_Bottom);
			}
			Bounds.m_NormalizedCenter = vec2((Bounds.m_Left + Bounds.m_Right) / (2.0f * m_Width) - 0.5f, (Bounds.m_Top + Bounds.m_Bottom) / (2.0f * m_Height) - 0.5f);
			const std::size_t NodeIndex = m_vBoundsNodes.size();
			m_vBoundsNodes.push_back({Bounds, FirstRect, EndRect, 0});
			if(EndRect - FirstRect > RECTS_PER_LEAF)
			{
				m_vBoundsNodes[NodeIndex].m_EndRect = FirstRect;
				const std::size_t Middle = FirstRect + (EndRect - FirstRect) / 2;
				BuildBoundsNodes(FirstRect, Middle);
				BuildBoundsNodes(Middle, EndRect);
			}
			m_vBoundsNodes[NodeIndex].m_NextNode = m_vBoundsNodes.size();
		}

		template<typename TOverlapsRect>
		bool OverlapsRects(const TOverlapsRect &OverlapsRect) const
		{
			if(m_vBoundsNodes.empty())
			{
				for(const SRect &Rect : m_vRects)
					if(OverlapsRect(Rect, 0.0001f))
						return true;
				return false;
			}
			// 包围盒只做保守排除；叶子仍使用原精细判定，查询时不分配内存。
			for(std::size_t NodeIndex = 0; NodeIndex < m_vBoundsNodes.size();)
			{
				const SBoundsNode &Node = m_vBoundsNodes[NodeIndex];
				if(!OverlapsRect(Node.m_Bounds, -0.01f))
				{
					NodeIndex = Node.m_NextNode;
					continue;
				}
				for(std::size_t Index = Node.m_FirstRect; Index < Node.m_EndRect; ++Index)
					if(OverlapsRect(m_vRects[Index], 0.0001f))
						return true;
				++NodeIndex;
			}
			return false;
		}

	public:
		void Build(const unsigned char *pRgba, int Width, int Height, int Stride = 0)
		{
			m_vRects.clear();
			m_vBoundsNodes.clear();
			m_BoundsCenter = m_BoundsHalf = vec2(0.0f, 0.0f);
			m_Width = std::max(1, Width);
			m_Height = std::max(1, Height);
			if(pRgba == nullptr || Width <= 0 || Height <= 0)
				return;
			if(Stride == 0)
				Stride = Width * 4;
			// 按上一行的全部区间合并，避免分离轮廓退化成每个像素行一个矩形。
			// 两行的区间都按横坐标排列，游标单向推进，不扫描历史矩形。
			std::vector<std::size_t> vPreviousRow;
			std::vector<std::size_t> vCurrentRow;
			for(int Y = 0; Y < Height; ++Y)
			{
				vCurrentRow.clear();
				std::size_t PreviousIndex = 0;
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
					while(PreviousIndex < vPreviousRow.size() && m_vRects[vPreviousRow[PreviousIndex]].m_Left < Left)
						++PreviousIndex;
					if(PreviousIndex < vPreviousRow.size())
					{
						const std::size_t RectIndex = vPreviousRow[PreviousIndex];
						SRect &Rect = m_vRects[RectIndex];
						if(Rect.m_Left == Left && Rect.m_Right == X)
						{
							Rect.m_Bottom = Y + 1;
							vCurrentRow.push_back(RectIndex);
							continue;
						}
					}
					vCurrentRow.push_back(m_vRects.size());
					m_vRects.push_back({Left, Y, X, Y + 1});
				}
				vPreviousRow.swap(vCurrentRow);
			}
			if(m_vRects.empty())
				return;
			int Left = Width, Top = Height, Right = 0, Bottom = 0;
			for(SRect &Rect : m_vRects)
			{
				Rect.m_NormalizedCenter = vec2((Rect.m_Left + Rect.m_Right) / (2.0f * m_Width) - 0.5f, (Rect.m_Top + Rect.m_Bottom) / (2.0f * m_Height) - 0.5f);
				Left = std::min(Left, Rect.m_Left);
				Top = std::min(Top, Rect.m_Top);
				Right = std::max(Right, Rect.m_Right);
				Bottom = std::max(Bottom, Rect.m_Bottom);
			}
			m_BoundsCenter = vec2((Left + Right) / (2.0f * m_Width) - 0.5f, (Top + Bottom) / (2.0f * m_Height) - 0.5f);
			m_BoundsHalf = vec2((Right - Left) / (2.0f * m_Width), (Bottom - Top) / (2.0f * m_Height));
			// 轮廓只在材质加载时分层，避免消散膨胀后每块墙砖都扫描全部矩形。
			if(m_vRects.size() > RECTS_PER_LEAF)
			{
				m_vBoundsNodes.reserve(m_vRects.size() / 2 + 1);
				BuildBoundsNodes(0, m_vRects.size());
			}
		}

		std::size_t NumRects() const { return m_vRects.size(); }

		template<typename TSolid>
		bool Overlaps(vec2 Pos, float Size, float Angle, const TSolid &Solid) const
		{
			if(m_vRects.empty() || Size <= 0.0f)
				return false;
			const vec2 AxisX = direction(Angle);
			const vec2 AxisY(-AxisX.y, AxisX.x);
			const vec2 AbsX(std::abs(AxisX.x), std::abs(AxisX.y));
			const vec2 AbsY(std::abs(AxisY.x), std::abs(AxisY.y));
			// 只查询不透明轮廓包围盒覆盖的瓦片；精细相交仍由原来的 SAT 判定。
			const vec2 BoundsCenter = Pos + AxisX * (m_BoundsCenter.x * Size) + AxisY * (m_BoundsCenter.y * Size);
			const vec2 BoundsHalf = (AbsX * m_BoundsHalf.x + AbsY * m_BoundsHalf.y) * Size + vec2(0.01f, 0.01f);
			const int MinX = (int)std::floor((BoundsCenter.x - BoundsHalf.x) / 32.0f);
			const int MaxX = (int)std::floor((BoundsCenter.x + BoundsHalf.x) / 32.0f);
			const int MinY = (int)std::floor((BoundsCenter.y - BoundsHalf.y) / 32.0f);
			const int MaxY = (int)std::floor((BoundsCenter.y + BoundsHalf.y) / 32.0f);
			for(int Y = MinY; Y <= MaxY; ++Y)
			{
				for(int X = MinX; X <= MaxX; ++X)
				{
					if(!Solid(X, Y))
						continue;
					const vec2 TileCenter(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
					if(OverlapsRects([&](const SRect &Rect, float Epsilon) {
						const vec2 Half((Rect.m_Right - Rect.m_Left) * Size / (2.0f * m_Width), (Rect.m_Bottom - Rect.m_Top) * Size / (2.0f * m_Height));
						const vec2 Local = Rect.m_NormalizedCenter * Size;
						const vec2 Delta = TileCenter - (Pos + AxisX * Local.x + AxisY * Local.y);
						return std::abs(Delta.x) < 16.0f + AbsX.x * Half.x + AbsY.x * Half.y - Epsilon &&
						       std::abs(Delta.y) < 16.0f + AbsX.y * Half.x + AbsY.y * Half.y - Epsilon &&
						       std::abs(dot(Delta, AxisX)) < Half.x + 16.0f * (AbsX.x + AbsX.y) - Epsilon &&
						       std::abs(dot(Delta, AxisY)) < Half.y + 16.0f * (AbsY.x + AbsY.y) - Epsilon;
						}))
						return true;
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
			return OverlapsRects([&](const SRect &Rect, float Epsilon) {
				const vec2 Half((Rect.m_Right - Rect.m_Left) * Size / (2.0f * m_Width), (Rect.m_Bottom - Rect.m_Top) * Size / (2.0f * m_Height));
				const vec2 Local = Rect.m_NormalizedCenter * Size;
				const vec2 Delta = BoxCenter - (Pos + AxisX * Local.x + AxisY * Local.y);
				return std::abs(Delta.x) < BoxHalf.x + AbsX.x * Half.x + AbsY.x * Half.y - Epsilon &&
				       std::abs(Delta.y) < BoxHalf.y + AbsX.y * Half.x + AbsY.y * Half.y - Epsilon &&
				       std::abs(dot(Delta, AxisX)) < Half.x + BoxHalf.x * AbsX.x + BoxHalf.y * AbsX.y - Epsilon &&
				       std::abs(dot(Delta, AxisY)) < Half.y + BoxHalf.x * AbsY.x + BoxHalf.y * AbsY.y - Epsilon;
			});
		}
	};

	template<typename TMask>
	bool OverlapsPlayerBoxes(const TMask &Mask, vec2 Pos, float Size, float Angle, int OwnerClientId, const SPlayerBox *pBoxes, int NumBoxes, const CTeamsCore *pTeams = nullptr)
	{
		if(pBoxes == nullptr || (pTeams != nullptr && (OwnerClientId < 0 || OwnerClientId >= MAX_CLIENTS)))
			return false;
		// 两个外接圆不相交时跳过精细轮廓，旋转与超大表情仍保守包含在圆内。
		const float MaskRadius = Size * 0.707107f;
		for(int Index = 0; Index < NumBoxes; ++Index)
		{
			const SPlayerBox &Box = pBoxes[Index];
			if(Box.m_ClientId == OwnerClientId)
				continue;
			const vec2 Delta = Pos - Box.m_Pos;
			const float Radius = MaskRadius + Box.m_Half * 1.414214f;
			if(dot(Delta, Delta) > Radius * Radius)
				continue;
			if(pTeams != nullptr && (Box.m_ClientId < 0 || Box.m_ClientId >= MAX_CLIENTS || !pTeams->SameTeam(OwnerClientId, Box.m_ClientId)))
				continue;
			if(Mask.OverlapsBox(Pos, Size, Angle, Box.m_Pos, vec2(Box.m_Half, Box.m_Half)))
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
	bool m_StopOnCollision = false;
	bool m_Impacted = false;
	int m_Emoticon = 0;
	int m_OwnerClientId = -1;
	double m_Accumulator = 0.0;
	vec2 m_PreviousPos = vec2(0.0f, 0.0f);
	float m_PreviousAngle = 0.0f;
	static constexpr double STEP = 1.0 / 240.0;

	void Init(vec2 Pos, vec2 Vel, int Emoticon, float SizeScale = 1.0f, int OwnerClientId = -1, int DurationSeconds = 5)
	{
		m_Pos = m_PreviousPos = Pos;
		m_Vel = Vel;
		m_Emoticon = Emoticon;
		m_SizeScale = std::max(SizeScale, 0.1f);
		m_SizeLimit = 128.0f * m_SizeScale;
		m_Angle = m_PreviousAngle = 0.0f;
		m_AngVel = ((rand() % 100) - 50) / 10.0f;
		m_LifeTime = static_cast<float>(std::clamp(DurationSeconds, 1, 10));
		m_Active = true;
		m_StopOnCollision = false;
		m_Impacted = false;
		m_OwnerClientId = OwnerClientId;
		m_Accumulator = 0.0;
	}

	float Size() const { return std::min(m_SizeLimit, 64.0f * m_SizeScale * (1.0f + std::max(0.0f, 0.5f - m_LifeTime) * 2.0f)); }
	float FadeAlpha() const { return std::clamp(m_LifeTime * 2.0f, 0.0f, 1.0f); }

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
	void Update(float Dt, const QmEmoticon::CAlphaMask &Mask, const TSolid &Solid, const QmEmoticon::SPlayerBox *pPlayerBoxes = nullptr, int NumPlayerBoxes = 0, const CTeamsCore *pTeams = nullptr)
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
			return Mask.Overlaps(Pos, Size, Angle, Solid) || QmEmoticon::OverlapsPlayerBoxes(Mask, Pos, Size, Angle, m_OwnerClientId, pPlayerBoxes, NumPlayerBoxes, pTeams);
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
					m_Impacted = m_StopOnCollision;
					break;
				}
			}
			m_PreviousPos = m_Pos;
			m_PreviousAngle = m_Angle;
			m_Vel.y += 1500.0f * STEP;
			const int Steps = std::max(1, (int)std::ceil((length(m_Vel) + std::abs(m_AngVel) * Size()) * STEP));
			const float SubDt = STEP / Steps;
			for(int Step = 0; Step < Steps && m_Active; ++Step)
			{
				for(int Axis = 0; Axis < 2 && m_Active; ++Axis)
				{
					vec2 Next = m_Pos;
					float &Velocity = Axis == 0 ? m_Vel.x : m_Vel.y;
					(Axis == 0 ? Next.x : Next.y) += Velocity * SubDt;
					if(Blocked(Next, Size(), m_Angle))
					{
						if(m_StopOnCollision)
						{
							m_Active = false;
							m_Impacted = true;
						}
						else
							Velocity *= -0.6f;
					}
					else
						m_Pos = Next;
				}
				if(!m_Active)
					break;
				const float NextAngle = m_Angle + m_AngVel * SubDt;
				if(Blocked(m_Pos, Size(), NextAngle))
				{
					if(m_StopOnCollision)
					{
						m_Active = false;
						m_Impacted = true;
					}
					else
						m_AngVel *= -0.6f;
				}
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
