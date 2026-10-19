#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_demo_ui
{
	constexpr float DISPLAY_HEIGHT = 130.0f;
	constexpr float CAMERA_HEIGHT = 146.0f;

	// 渲染回放弹窗与设置页卡片共用同一套单行控件节奏：20 高、4 间距。
	// 分组上下各留 8，标题行 18，标题与首行间隔 4。
	constexpr float RENDER_ROW_HEIGHT = 20.0f;
	constexpr float RENDER_ROW_GAP = 4.0f;
	constexpr float RENDER_SECTION_PADDING = 8.0f;
	constexpr float RENDER_SECTION_TITLE_HEIGHT = 18.0f;
	constexpr float RENDER_SECTION_TITLE_GAP = 4.0f;
	constexpr float RENDER_SECTION_GAP = 10.0f;
	constexpr float RENDER_TOP_GAP = 4.0f;

	// 分辨率行：自定义时左侧两个等宽输入框，中间乘号，右侧保留下拉框。
	constexpr float RESOLUTION_DROPDOWN_MIN_WIDTH = 150.0f;
	constexpr float RESOLUTION_DROPDOWN_MAX_WIDTH = 176.0f;
	constexpr float RESOLUTION_INPUT_GAP = 6.0f;
	constexpr float RESOLUTION_SEPARATOR_WIDTH = 12.0f;

	struct SResolutionPreset
	{
		int m_Width;
		int m_Height;
	};

	// 0 = 跟随窗口；中间四项为常用固定档位；最后一项是自定义。
	constexpr int RESOLUTION_PRESET_COUNT = 6;
	constexpr int RESOLUTION_PRESET_CUSTOM = RESOLUTION_PRESET_COUNT - 1;
	inline constexpr SResolutionPreset RESOLUTION_PRESETS[RESOLUTION_PRESET_COUNT] = {
		{0, 0},
		{1280, 720},
		{1920, 1080},
		{2560, 1440},
		{3840, 2160},
		{0, 0},
	};

	inline int MatchResolutionPreset(int Width, int Height)
	{
		if(Width <= 0 || Height <= 0)
			return 0;
		for(int i = 1; i < RESOLUTION_PRESET_CUSTOM; ++i)
		{
			if(RESOLUTION_PRESETS[i].m_Width == Width && RESOLUTION_PRESETS[i].m_Height == Height)
				return i;
		}
		return RESOLUTION_PRESET_CUSTOM;
	}

	struct SResolutionControlLayout
	{
		CUIRect m_WidthInput{};
		CUIRect m_Separator{};
		CUIRect m_HeightInput{};
		CUIRect m_DropDown{};
	};

	inline SResolutionControlLayout ResolutionControlLayout(const CUIRect &Control, bool Custom)
	{
		SResolutionControlLayout Layout;
		if(!Custom)
		{
			Layout.m_DropDown = Control;
			return Layout;
		}

		const float DropDownWidth = std::min(Control.w, std::clamp(Control.w * 0.38f, RESOLUTION_DROPDOWN_MIN_WIDTH, RESOLUTION_DROPDOWN_MAX_WIDTH));
		CUIRect Inputs;
		Control.VSplitRight(DropDownWidth, &Inputs, &Layout.m_DropDown);
		Inputs.VSplitRight(RESOLUTION_INPUT_GAP, &Inputs, nullptr);
		const float Available = std::max(0.0f, Inputs.w - RESOLUTION_SEPARATOR_WIDTH - 2.0f * RESOLUTION_INPUT_GAP);
		const float InputWidth = Available * 0.5f;
		Layout.m_WidthInput = {Inputs.x, Inputs.y, InputWidth, Inputs.h};
		Layout.m_Separator = {Inputs.x + InputWidth + RESOLUTION_INPUT_GAP, Inputs.y, RESOLUTION_SEPARATOR_WIDTH, Inputs.h};
		Layout.m_HeightInput = {Layout.m_Separator.x + RESOLUTION_SEPARATOR_WIDTH + RESOLUTION_INPUT_GAP, Inputs.y, InputWidth, Inputs.h};
		return Layout;
	}

	// 渲染回放弹窗中平铺的"回放显示"分组（标题 + 三组段选，无开关行）的高度：
	// 上下内边距 16 + 标题行 18 + 标题间隔 4 + 3×(20 + 4) = 110。
	constexpr float DISPLAY_GROUP_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
					       RENDER_SECTION_TITLE_GAP + 3.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	// 视频导出分组：标题 + 8 行（分辨率/编码器/容器/画质/导出预设/GOP/名称/范围）= 230。
	constexpr float EXPORT_GROUP_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
					      RENDER_SECTION_TITLE_GAP + 8.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	// 播放选项分组：标题 + 纯净渲染开关 + HUD/聊天/声音行 + 速度行 = 110。
	constexpr float PLAYBACK_GROUP_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
						RENDER_SECTION_TITLE_GAP + 3.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	// 视频编码分组：标题 + CRF + 渲染预设 + 录制帧率 = 110。
	constexpr float ENCODE_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
					RENDER_SECTION_TITLE_GAP + 3.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	inline CUIRect PlayerRect(const CUIRect &Screen, bool DisplayExpanded, bool CameraExpanded = false)
	{
		const float Width = std::min(640.0f, std::max(0.0f, Screen.w - 24.0f));
		const float Height = 120.0f + (DisplayExpanded ? DISPLAY_HEIGHT + 6.0f : 0.0f) + (CameraExpanded ? CAMERA_HEIGHT + 6.0f : 0.0f);
		return {Screen.x + (Screen.w - Width) * 0.5f, Screen.y + Screen.h - Height - 12.0f, Width, Height};
	}

	constexpr float TransportWidth(float ButtonSize, bool ShowSkipDuration)
	{
		// 十个播放图标、倍速文字与间距；短回放没有时长下拉框。
		return 10.0f * ButtonSize + 36.0f + 39.0f + (ShowSkipDuration ? 59.0f : 0.0f);
	}

	inline float TransportButtonSize(float PanelWidth, bool ShowSkipDuration)
	{
		return std::clamp((PanelWidth - 16.0f - TransportWidth(0.0f, ShowSkipDuration)) / 10.0f, 0.0f, 22.0f);
	}

	struct SCutControls
	{
		CUIRect m_Start;
		CUIRect m_End;
		CUIRect m_Add;
		CUIRect m_Clear;
		CUIRect m_Preview;
		CUIRect m_Export;
	};

	inline SCutControls CutControls(CUIRect Bar, float ButtonSize)
	{
		SCutControls Controls;
		Bar.VSplitRight(Bar.h, &Bar, &Controls.m_Export);
		Bar.VSplitRight(3.0f, &Bar, nullptr);
		Bar.VSplitRight(Bar.h, &Bar, &Controls.m_Preview);
		Bar.VSplitRight(8.0f, &Bar, nullptr);
		Bar.VSplitRight(ButtonSize, &Bar, &Controls.m_Clear);
		Bar.VSplitRight(3.0f, &Bar, nullptr);
		Bar.VSplitRight(ButtonSize, &Bar, &Controls.m_Add);
		Bar.VSplitRight(8.0f, &Bar, nullptr);
		Bar.VSplitMid(&Controls.m_Start, &Controls.m_End, 3.0f);
		return Controls;
	}

	inline CUIRect DraggedPlayerRect(const CUIRect &Screen, const CUIRect &Base, float OffsetX, float OffsetY)
	{
		return {
			std::clamp(Base.x + OffsetX, Screen.x, Screen.x + Screen.w - Base.w),
			std::clamp(Base.y + OffsetY, Screen.y, std::max(Screen.y, Screen.y + Screen.h - Base.h)),
			Base.w,
			Base.h};
	}

	inline float SliceContentHeight(int SegmentCount, bool DisplayExpanded)
	{
		const int VisibleSegments = std::min(std::max(SegmentCount, 0), 4);
		const float SegmentsHeight = SegmentCount > 0 ? 20.0f + VisibleSegments * 20.0f + (SegmentCount > 4 ? 16.0f : 0.0f) + 10.0f : 0.0f;
		return 154.0f + SegmentsHeight + (DisplayExpanded ? DISPLAY_HEIGHT + 4.0f : 0.0f);
	}

	// 渲染回放弹窗内容总高：顶部 4 + 回放显示 110 + 10 + 视频导出 230 + 10 +
	// 播放选项 110 + 10 + 视频编码 110 = 594；在线时追加断线提示 30。
	inline float RenderContentHeight(bool Online)
	{
		return RENDER_TOP_GAP + DISPLAY_GROUP_HEIGHT + RENDER_SECTION_GAP + EXPORT_GROUP_HEIGHT +
		       RENDER_SECTION_GAP + PLAYBACK_GROUP_HEIGHT + RENDER_SECTION_GAP + ENCODE_HEIGHT +
		       (Online ? 30.0f : 0.0f);
	}

	inline CUIRect PopupRect(const CUIRect &Screen, float ContentHeight, float Width = 560.0f)
	{
		const float PopupWidth = std::min(Width, std::max(0.0f, Screen.w - 24.0f));
		const float PopupHeight = std::min(ContentHeight, std::max(0.0f, Screen.h - 24.0f));
		return {Screen.x + (Screen.w - PopupWidth) * 0.5f, Screen.y + (Screen.h - PopupHeight) * 0.5f, PopupWidth, PopupHeight};
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
