#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_MAP_PROGRESS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_HUD_MAP_PROGRESS_H

#include <base/color.h>

#include <game/client/ui_rect.h>

#include <algorithm>

class IGraphics;
class ITextRender;

namespace QmHudMapProgress
{
	enum class ESurface
	{
		HIDDEN,
		STANDALONE,
		PLAYER_STATS,
		ISLAND,
	};

	inline ESurface ResolveSurface(bool Enabled, int Style, bool PlayerStatsEnabled, bool IslandEnabled)
	{
		if(!Enabled)
			return ESurface::HIDDEN;
		if(Style == 2 && IslandEnabled)
			return ESurface::ISLAND;
		if(Style == 1 && PlayerStatsEnabled)
			return ESurface::PLAYER_STATS;
		return ESurface::STANDALONE;
	}

	struct SIslandRows
	{
		bool m_ProgressInHeader = false;
		float m_ProgressY = 0.0f;
		float m_TextY = 0.0f;
		float m_BottomHeight = 0.0f;
	};

	inline SIslandRows IslandRows(bool HasTopRow, bool ShowProgress, int TextLines, float HeaderHeight, float LineHeight, float PaddingY)
	{
		SIslandRows Result;
		Result.m_ProgressInHeader = ShowProgress && !HasTopRow;
		const bool ProgressBelowHeader = ShowProgress && HasTopRow;
		Result.m_ProgressY = Result.m_ProgressInHeader ? (HeaderHeight - LineHeight) * 0.5f : HeaderHeight + PaddingY;
		Result.m_TextY = HeaderHeight + PaddingY + (ProgressBelowHeader ? LineHeight : 0.0f);
		const int BottomLines = std::max(0, TextLines) + (ProgressBelowHeader ? 1 : 0);
		Result.m_BottomHeight = BottomLines > 0 ? PaddingY * 2.0f + LineHeight * BottomLines : 0.0f;
		return Result;
	}

	struct SStripLayout
	{
		CUIRect m_Label{};
		CUIRect m_Track{};
		CUIRect m_Value{};
		bool m_Visible = false;
	};

	inline float MinimumWidth(float LabelWidth, float ValueWidth, float Scale)
	{
		return LabelWidth + ValueWidth + 36.0f * Scale;
	}

	inline SStripLayout StripLayout(const CUIRect &Row, float LabelWidth, float ValueWidth, float Scale)
	{
		SStripLayout Result;
		if(Scale <= 0.0f || Row.w < MinimumWidth(LabelWidth, ValueWidth, Scale) || Row.h < 4.4f * Scale)
			return Result;
		const float Inset = 7.0f * Scale;
		const float Gap = 3.0f * Scale;
		const float FontSize = 4.4f * Scale;
		const float CenterY = Row.y + Row.h * 0.5f;
		Result.m_Label = {Row.x + Inset, CenterY - FontSize * 0.5f, LabelWidth, FontSize};
		Result.m_Value = {Row.x + Row.w - Inset - ValueWidth, CenterY - FontSize * 0.5f, ValueWidth, FontSize};
		const float TrackX = Result.m_Label.x + LabelWidth + Gap;
		Result.m_Track = {TrackX, CenterY - 0.5f * Scale, Result.m_Value.x - Gap - TrackX, Scale};
		Result.m_Visible = true;
		return Result;
	}

	class CProgressState
	{
		bool m_Initialized = false;
		float m_Displayed = 0.0f;

	public:
		void Reset() { *this = {}; }
		float Update(bool HasProgress, float Progress, float DeltaSeconds)
		{
			if(!HasProgress)
			{
				Reset();
				return 0.0f;
			}
			const float Target = std::clamp(Progress, 0.0f, 1.0f);
			if(!m_Initialized)
				m_Displayed = Target;
			else
				m_Displayed += (Target - m_Displayed) * std::clamp(DeltaSeconds * 8.0f, 0.0f, 1.0f);
			m_Initialized = true;
			return m_Displayed;
		}
	};

	float MeasureMinimumWidth(ITextRender &TextRender, float Scale);
	void RenderIslandStrip(IGraphics &Graphics, ITextRender &TextRender, const CUIRect &Row, float Scale, bool HasProgress, float Progress, const ColorRGBA &FillColor, float Alpha);
}

#endif
