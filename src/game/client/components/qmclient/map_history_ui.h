// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_UI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MAP_HISTORY_UI_H

#include <game/client/QmUi/UiTokens.h>

#include <algorithm>

namespace QmMapHistoryUi
{

	// 列表覆盖式滚动条的轨道宽度：直接取 CListBox 的列表档 token，
	// 网格列数不再按另一套自算宽度预留。
	inline constexpr float LIST_SCROLLBAR_WIDTH = ui_token::list::SCROLLBAR_WIDTH;
	inline constexpr float MIN_CARD_ROW_HEIGHT = 54.0f;
	inline constexpr float MAX_CARD_ROW_HEIGHT = 72.0f;

	struct SWorkspaceMetrics
	{
		float m_OuterMargin;
		float m_TabHeight;
		float m_SectionGap;
		float m_PanelMargin;
		float m_HeaderHeight;
		float m_ControlHeight;
	};

	constexpr SWorkspaceMetrics WorkspaceMetrics(float AvailableHeight)
	{
		const float Height = std::max(AvailableHeight, 0.0f);
		return {
			std::clamp(Height * 0.014f, 4.0f, 8.0f),
			std::clamp(Height * 0.050f, 22.0f, 28.0f),
			std::clamp(Height * 0.012f, 4.0f, 7.0f),
			std::clamp(Height * 0.014f, 5.0f, 8.0f),
			std::clamp(Height * 0.055f, 26.0f, 32.0f),
			std::clamp(Height * 0.042f, 20.0f, 24.0f),
		};
	}

	constexpr int CeilPositive(float Value)
	{
		const int Whole = (int)Value;
		return (float)Whole < Value ? Whole + 1 : Whole;
	}

	constexpr int VisibleCardRows(float AvailableHeight)
	{
		const float Height = std::max(AvailableHeight, 0.0f);
		if(Height <= 0.0f)
			return 1;

		const float PreferredHeight = std::clamp(Height * 0.155f, MIN_CARD_ROW_HEIGHT, MAX_CARD_ROW_HEIGHT);
		const int PreferredRows = std::max(CeilPositive(Height / PreferredHeight), 1);
		const int MaxReadableRows = std::max((int)(Height / MIN_CARD_ROW_HEIGHT), 1);
		return std::min(PreferredRows, MaxReadableRows);
	}

	constexpr float CardRowHeight(float AvailableHeight)
	{
		const float Height = std::max(AvailableHeight, 0.0f);
		return Height > 0.0f ? Height / VisibleCardRows(Height) : MIN_CARD_ROW_HEIGHT;
	}

	constexpr float CardGap(float RowHeight)
	{
		return std::clamp(RowHeight * 0.10f, 5.0f, 7.0f);
	}

	constexpr float CardPadding(float RowHeight)
	{
		return std::clamp(RowHeight * 0.09f, 4.5f, 6.0f);
	}

	constexpr int GridColumns(float AvailableWidth, float RowHeight)
	{
		const float CardWidth = std::max(RowHeight * 3.75f, 1.0f);
		const int Columns = (int)(std::max(AvailableWidth, 0.0f) / CardWidth);
		return std::max(Columns, 1);
	}

	constexpr bool StackControls(float AvailableWidth, float ControlHeight)
	{
		return AvailableWidth < std::max(ControlHeight, 1.0f) * 25.0f;
	}

} // namespace QmMapHistoryUi

#endif
