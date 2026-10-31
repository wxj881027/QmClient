#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_GEOMETRY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_HOOK_COLL_GEOMETRY_H

#include <engine/graphics.h>

#include <optional>
#include <vector>

// 每条提示线独占可见结果；跨玩家和视口只复用容量。
struct SQmHookCollLineScratch
{
	std::vector<IGraphics::CLineItem> m_vLineSegments;
	std::vector<IGraphics::CFreeformItem> m_vLineQuadSegments;
	std::optional<IGraphics::CLineItem> m_TipLine;
	std::optional<IGraphics::CFreeformItem> m_TipQuad;

	void Reset() noexcept
	{
		m_vLineSegments.clear();
		m_vLineQuadSegments.clear();
		m_TipLine.reset();
		m_TipQuad.reset();
	}

	template<typename F>
	[[nodiscard]] bool TryPrepare(F &&Prepare) noexcept
	{
		Reset();
		try
		{
			if(Prepare())
				return true;
		}
		catch(...)
		{
			// 可选提示线准备失败时不发布前缀，后续帧可重新准备。
		}
		Reset();
		return false;
	}

	static IGraphics::CFreeformItem MakeQuad(const IGraphics::CLineItem &LineSegment, const vec2 &PerpToAngle, float LineWidth)
	{
		const vec2 DrawInitPos(LineSegment.m_X0, LineSegment.m_Y0);
		const vec2 DrawFinishPos(LineSegment.m_X1, LineSegment.m_Y1);
		const vec2 Pos0 = DrawFinishPos + PerpToAngle * -LineWidth;
		const vec2 Pos1 = DrawFinishPos + PerpToAngle * LineWidth;
		const vec2 Pos2 = DrawInitPos + PerpToAngle * -LineWidth;
		const vec2 Pos3 = DrawInitPos + PerpToAngle * LineWidth;
		return {Pos0.x, Pos0.y, Pos1.x, Pos1.y, Pos2.x, Pos2.y, Pos3.x, Pos3.y};
	}

	void AppendQuad(const IGraphics::CLineItem &LineSegment, const vec2 &PerpToAngle, float LineWidth)
	{
		m_vLineQuadSegments.push_back(MakeQuad(LineSegment, PerpToAngle, LineWidth));
	}

	void PrepareQuads(const vec2 &PerpToAngle, float LineWidth, bool IncludeTip)
	{
		m_vLineQuadSegments.clear();
		m_TipQuad.reset();
		m_vLineQuadSegments.reserve(m_vLineSegments.size());
		for(const auto &Line : m_vLineSegments)
			AppendQuad(Line, PerpToAngle, LineWidth);
		if(IncludeTip && m_TipLine)
			m_TipQuad = MakeQuad(*m_TipLine, PerpToAngle, LineWidth);
		m_vLineSegments.clear();
	}
};

#endif
