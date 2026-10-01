#ifndef GAME_EDITOR_QM_EDITOR_LAYOUT_H
#define GAME_EDITOR_QM_EDITOR_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace QmEditorLayout
{
	inline constexpr float MENU_HEIGHT = 24.0f;
	inline constexpr float TABS_HEIGHT = 26.0f;
	inline constexpr float TOOLBAR_HEIGHT = 56.0f;
	inline constexpr float STATUS_HEIGHT = 24.0f;
	inline constexpr float SPLITTER_WIDTH = 4.0f;
	inline constexpr float MIN_LAYERS_WIDTH = 120.0f;
	inline constexpr float MIN_INSPECTOR_WIDTH = 190.0f;
	inline constexpr float MIN_CANVAS_WIDTH = 240.0f;
	inline constexpr float MIN_CANVAS_HEIGHT = 140.0f;
	inline constexpr float DEFAULT_LAYERS_WIDTH = 168.0f;
	inline constexpr float DEFAULT_INSPECTOR_WIDTH = 220.0f;

	struct SWorkspace
	{
		CUIRect m_Menu{};
		CUIRect m_MapTabs{};
		CUIRect m_Toolbar{};
		CUIRect m_Layers{};
		CUIRect m_LayersSplitter{};
		CUIRect m_Canvas{};
		CUIRect m_InspectorSplitter{};
		CUIRect m_Inspector{};
		CUIRect m_ExtraEditor{};
		CUIRect m_Status{};
	};

	inline SWorkspace Calculate(CUIRect Screen, float LayersWidth, float InspectorWidth, float ExtraEditorHeight, bool GuiActive, bool ShowInspector, bool ShowExtraEditor)
	{
		SWorkspace Result;
		Screen.w = std::max(0.0f, Screen.w);
		Screen.h = std::max(0.0f, Screen.h);
		if(!GuiActive)
		{
			Result.m_Canvas = Screen;
			return Result;
		}

		auto TakeTop = [&Screen](float Height) {
			Height = std::min(Height, Screen.h);
			const CUIRect Rect{Screen.x, Screen.y, Screen.w, Height};
			Screen.y += Height;
			Screen.h -= Height;
			return Rect;
		};
		Result.m_Menu = TakeTop(MENU_HEIGHT);
		Result.m_MapTabs = TakeTop(TABS_HEIGHT);
		Result.m_Toolbar = TakeTop(TOOLBAR_HEIGHT);
		const float StatusHeight = std::min(STATUS_HEIGHT, Screen.h);
		Screen.h -= StatusHeight;
		Result.m_Status = {Screen.x, Screen.y + Screen.h, Screen.w, StatusHeight};

		// 窄窗口保留画布，属性仍可通过原有弹窗编辑。
		ShowInspector = ShowInspector && Screen.w >= MIN_LAYERS_WIDTH + MIN_INSPECTOR_WIDTH + MIN_CANVAS_WIDTH + 2.0f * SPLITTER_WIDTH;
		const float MinimumCanvas = std::min(MIN_CANVAS_WIDTH, Screen.w);
		const float SideBudget = std::max(0.0f, Screen.w - MinimumCanvas);
		if(ShowInspector)
		{
			InspectorWidth = std::clamp(InspectorWidth, MIN_INSPECTOR_WIDTH, SideBudget - MIN_LAYERS_WIDTH - 2.0f * SPLITTER_WIDTH);
		}
		else
		{
			InspectorWidth = 0.0f;
		}
		const float InspectorSpace = InspectorWidth > 0.0f ? InspectorWidth + SPLITTER_WIDTH : 0.0f;
		const float MaxLayersWidth = std::max(0.0f, SideBudget - InspectorSpace - SPLITTER_WIDTH);
		LayersWidth = std::clamp(LayersWidth, std::min(MIN_LAYERS_WIDTH, MaxLayersWidth), MaxLayersWidth);
		const float LayersSplitterWidth = LayersWidth > 0.0f ? SPLITTER_WIDTH : 0.0f;
		Result.m_Layers = {Screen.x, Screen.y, LayersWidth, Screen.h};
		Result.m_LayersSplitter = {Screen.x + LayersWidth, Screen.y, LayersSplitterWidth, Screen.h};
		Screen.x += LayersWidth + LayersSplitterWidth;
		Screen.w -= LayersWidth + LayersSplitterWidth;
		if(InspectorWidth > 0.0f)
		{
			Result.m_Inspector = {Screen.x + Screen.w - InspectorWidth, Screen.y, InspectorWidth, Screen.h};
			Result.m_InspectorSplitter = {Result.m_Inspector.x - SPLITTER_WIDTH, Screen.y, SPLITTER_WIDTH, Screen.h};
			Screen.w -= InspectorSpace;
		}

		if(ShowExtraEditor)
		{
			const float MaximumHeight = std::max(0.0f, Screen.h - MIN_CANVAS_HEIGHT);
			ExtraEditorHeight = std::clamp(ExtraEditorHeight, 0.0f, MaximumHeight);
			Screen.h -= ExtraEditorHeight;
			Result.m_ExtraEditor = {Screen.x, Screen.y + Screen.h, Screen.w, ExtraEditorHeight};
		}
		Result.m_Canvas = Screen;
		return Result;
	}
}

#endif
