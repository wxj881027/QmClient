#ifndef GAME_CLIENT_QMUI_QMBADGEGEOMETRY_H
#define GAME_CLIENT_QMUI_QMBADGEGEOMETRY_H

#include <engine/graphics.h>

#include <array>
#include <cstddef>
#include <vector>

class CUIRect;

enum class EQmBadgeGeometry
{
	DEVELOPER,
	LOGO,
	MOON_LEFT,
	MOON_MIDDLE,
	MOON_RIGHT,
	SUN_LEFT,
	SUN_MIDDLE,
	SUN_RIGHT,
	DEVELOPER_LETTERING,
};

struct SQmBadgeGeometryQuad
{
	IGraphics::CFreeformItem m_Quad;
	unsigned m_Color;
	float m_Alpha;
};

struct SQmBadgeGeometryMesh
{
	const SQmBadgeGeometryQuad *m_pQuads;
	size_t m_Count;
	vec2 m_Size;
};

SQmBadgeGeometryMesh QmBadgeGeometryMesh(EQmBadgeGeometry Kind);

struct SQmBadgeClippedQuads
{
	std::array<IGraphics::CFreeformItem, 3> m_aQuads;
	int m_Count = 0;
};

SQmBadgeClippedQuads QmClipBadgeGeometry(const IGraphics::CFreeformItem &Quad, float Right);
void QmDrawBadgeCapsuleGeometry(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Background, ColorRGBA Border);

class CQmBadgeGeometry
{
	struct SRange
	{
		int m_First;
		int m_Count;
		ColorRGBA m_Color;
	};

	EQmBadgeGeometry m_Kind;
	mutable int m_Container = -1;
	mutable uint32_t m_GraphicsVersion = 0;
	mutable std::vector<SRange> m_vRanges;

public:
	explicit CQmBadgeGeometry(EQmBadgeGeometry Kind) :
		m_Kind(Kind) {}
	CQmBadgeGeometry(const CQmBadgeGeometry &) = delete;
	CQmBadgeGeometry &operator=(const CQmBadgeGeometry &) = delete;
	void Init(IGraphics *pGraphics) const;
	void Shutdown(IGraphics *pGraphics) const;
	bool Available() const { return m_Container >= 0; }
	void Render(IGraphics *pGraphics, const CUIRect &Rect, ColorRGBA Tint, float DesignClipWidth = -1.0f) const;
};

#endif
