#ifndef GAME_CLIENT_QMUI_QMFINISHSUMMARY_H
#define GAME_CLIENT_QMUI_QMFINISHSUMMARY_H

#include "QmFinishSummaryPresentation.h"

#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

class IGraphics;
class ITextRender;
class CRenderTools;
class CManagedTeeRenderInfo;
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
		std::array<int64_t, static_cast<size_t>(ENumber::COUNT)> m_aDisplayedNumbers{};

	public:
		CPresentation m_Presentation;
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

		void Update(const QmFinishSummary::SSnapshot &Snapshot, double NowSeconds, bool FullyExpanded, int MotionLevel);
	};

	inline CUIRect ExpandedRect(float ScreenWidth, float ScreenHeight, float Top, float HudScale = 1.0f, float OuterPadding = 0.0f)
	{
		// 限制最终显示尺寸，HUD 编辑器放大时在变换前抵消缩放。
		constexpr float AspectRatio = 480.0f / 216.0f;
		const float Scale = std::max(1.0f, HudScale);
		const float Padding = std::max(0.0f, OuterPadding) * 2.0f;
		const float MaxHeight = std::min(std::max(0.0f, ScreenHeight * 0.3f - Padding) / Scale, std::max(0.0f, ScreenHeight - Top));
		const float Width = std::min(std::max(0.0f, ScreenWidth * 0.375f - Padding) / Scale, MaxHeight * AspectRatio);
		const float Height = Width / AspectRatio;
		return {(ScreenWidth - Width) * 0.5f, Top, Width, Height};
	}

	// 内容入场独立于数据修订，返回值只表示本次入场是否呈现完毕。
	bool Render(IGraphics &Graphics, ITextRender &TextRender, CRenderTools &RenderTools, const CUIRect &Rect,
		const QmFinishSummary::SSnapshot &Snapshot, const CManagedTeeRenderInfo *pTeeRenderInfo, const CContentCache &Content,
		float Alpha, const CUIRect *pTextScreen = nullptr);
}

#endif
