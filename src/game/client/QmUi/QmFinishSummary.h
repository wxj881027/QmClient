#ifndef GAME_CLIENT_QMUI_QMFINISHSUMMARY_H
#define GAME_CLIENT_QMUI_QMFINISHSUMMARY_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cstdint>
#include <string>

class IGraphics;
class ITextRender;
namespace QmFinishSummary
{
	struct SSnapshot;
}

namespace qm_finish_summary
{
	class CContentCache
	{
		uint64_t m_Id = 0;
		uint64_t m_Revision = 0;
		bool m_Valid = false;
		std::string m_Language;

	public:
		std::string m_Type;
		std::string m_TimeMajor;
		std::string m_TimeMinor;
		std::string m_Comparison;
		std::string m_Difference;
		std::string m_LocalRank;
		std::string m_ChinaRank;
		std::string m_Rank;
		std::string m_Completion;
		std::string m_Points;
		std::string m_Gain;

		void Update(const QmFinishSummary::SSnapshot &Snapshot);
	};

	inline CUIRect ExpandedRect(float ScreenWidth, float ScreenHeight, float Top)
	{
		const float Width = std::min(std::max(0.0f, ScreenWidth - 40.0f), std::clamp(ScreenWidth * 0.68f, 240.0f, 420.0f));
		const float Height = std::min(Width * 0.42f, std::max(0.0f, ScreenHeight - Top - 10.0f));
		return {(ScreenWidth - Width) * 0.5f, Top, Width, Height};
	}

	inline CUIRect ResolveRect(const CUIRect &Normal, const CUIRect &Expanded, float Progress)
	{
		const float T = std::clamp(Progress, 0.0f, 1.0f);
		return {Normal.x + (Expanded.x - Normal.x) * T, Normal.y,
			Normal.w + (Expanded.w - Normal.w) * T, Normal.h + (Expanded.h - Normal.h) * T};
	}

	// 正文使用最终布局坐标；外框展开到足够空间后淡入，数据刷新不改变布局。
	void Render(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Rect,
		const QmFinishSummary::SSnapshot &Snapshot, CContentCache &Content, float Alpha);
}

#endif
