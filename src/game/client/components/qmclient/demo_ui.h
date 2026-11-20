#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H

#include <game/client/ui_rect.h>

#include <algorithm>

namespace qm_demo_ui
{
	constexpr float DISPLAY_HEIGHT = 130.0f;
	constexpr float CAMERA_HEIGHT = 146.0f;

	// 播放器控制卡：120 高，含上下 8 边距、名称栏、进度、按钮与切片行。
	// 查看模式没有源 demo，切片行整段隐藏，高度随之减去 CUT_SECTION_HEIGHT。
	constexpr float PLAYER_PANEL_HEIGHT = 120.0f;
	constexpr float PLAYER_MARGIN = 8.0f;
	constexpr float NAME_BAR_HEIGHT = 20.0f;
	constexpr float SEEK_BAR_HEIGHT = 12.0f;
	constexpr float TIME_BAR_HEIGHT = 14.0f;
	constexpr float BUTTON_BAR_HEIGHT = 22.0f;
	constexpr float CUT_BAR_GAP = 8.0f;
	constexpr float CUT_BAR_HEIGHT = 24.0f;
	constexpr float CUT_SECTION_HEIGHT = CUT_BAR_GAP + CUT_BAR_HEIGHT;
	constexpr float CONTROL_MARGIN = 3.0f;
	constexpr float TRANSPORT_BUTTON_MAX = 22.0f;
	constexpr float SEEK_ROUNDING = 5.0f;
	constexpr float NAME_ACTION_GAP = 3.0f;
	// 控制卡外壳、按钮与名称栏的固定排版事实。调用点只引用这些名字，
	// 再按窗口宽度派生实际矩形，不再把圆角、字面字号写回 menus_demo.cpp。
	constexpr float CARD_SHADOW_OFFSET_Y = 2.0f;
	constexpr float CARD_RADIUS = 12.0f;
	constexpr float CARD_BORDER_WIDTH = 0.6f;
	constexpr float CARD_INNER_RADIUS = 11.4f;
	constexpr float CONTROL_RADIUS = 6.0f;
	constexpr float POPUP_RADIUS = 15.0f;
	constexpr float PLAYHEAD_RADIUS = 2.0f;
	constexpr float CONTROL_FONT_SIZE = 11.0f;
	constexpr float NAME_LABEL_FONT_SIZE = 12.0f;
	constexpr float LIVE_ICON_SIZE = 24.0f;
	constexpr float DEMO_DISPLAY_BUTTON_WIDTH = 80.0f;
	constexpr float CAMERA_BUTTON_WIDTH = 90.0f;

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

	// 导出预设的 GOP 取值：剪辑（3）与最高画质（5）要全帧内，其余预设回到默认 60。
	// 切预设时输入框必须显式写回，否则从「剪辑」切走的成片会残留 GOP=1。
	constexpr int EXPORT_PRESET_EDITING = 3;
	constexpr int EXPORT_PRESET_BEST_QUALITY = 5;
	constexpr int EXPORT_PRESET_FASTEST = 6;
	constexpr int PRESET_GOP_ALL_INTRA = 1;
	constexpr int PRESET_GOP_DEFAULT = 60;

	inline int PresetGop(int Preset)
	{
		return Preset == EXPORT_PRESET_EDITING || Preset == EXPORT_PRESET_BEST_QUALITY ? PRESET_GOP_ALL_INTRA : PRESET_GOP_DEFAULT;
	}

	// 剪辑与最高画质同时切到 4:4:4：完整色度是本项目提升导出画质最有效的一项（实测细线、
	// 彩色小字的色度保真比 4:2:0 高 13 dB 以上）。其余预设回到自动，保证兼容性。
	constexpr int PIXFMT_AUTO = 0;
	constexpr int PIXFMT_YUV444 = 3;

	inline int PresetPixelFormat(int Preset)
	{
		return Preset == EXPORT_PRESET_EDITING || Preset == EXPORT_PRESET_BEST_QUALITY ? PIXFMT_YUV444 : PIXFMT_AUTO;
	}

	// 预设的质量值（写回弹窗的画质输入框）：-1 表示清空（回到「留空 = 默认 CRF 18」）。
	// 最高画质在剪辑组合之上把质量压到 14（肉眼无损档）；最快速度用 23 换编码速度
	// （硬件编码器更快，软件回落时也能明显提速）。所有预设都显式写回，防止上一档残留。
	constexpr int PRESET_QUALITY_DEFAULT = -1;
	inline int PresetQuality(int Preset)
	{
		if(Preset == EXPORT_PRESET_BEST_QUALITY)
			return 14;
		if(Preset == EXPORT_PRESET_FASTEST)
			return 23;
		return PRESET_QUALITY_DEFAULT;
	}

	// 渲染弹窗的导出抗锯齿档位：-1 跟随全局 gfx_fsaa_samples，0 关，2/4/8 显式离屏多采样。
	// 只开到 8x：再高档位在导出场景纯属显存浪费（16/32/64 是窗口 FSAA 的老档位）。
	inline constexpr int VIDEO_FSAA_CHOICES[] = {-1, 0, 2, 4, 8};

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

	// 视频导出分组：标题 + 10 行（分辨率/编码器/画质/像素格式/抗锯齿/容器/导出预设/GOP/名称/范围）= 278。
	constexpr float EXPORT_GROUP_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
					      RENDER_SECTION_TITLE_GAP + 10.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	// 播放选项分组：标题 + 纯净渲染开关 + HUD/聊天/声音行 + 速度行 = 110。
	constexpr float PLAYBACK_GROUP_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
						RENDER_SECTION_TITLE_GAP + 3.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	// 视频编码分组：标题 + CRF + 渲染预设 + 录制帧率 + 转换线程 + 采集队列
	// + 分段时长 + 分段预热 + ProRes 档位 = 8 行。
	constexpr float ENCODE_HEIGHT = 2.0f * RENDER_SECTION_PADDING + RENDER_SECTION_TITLE_HEIGHT +
					RENDER_SECTION_TITLE_GAP + 8.0f * (RENDER_ROW_HEIGHT + RENDER_ROW_GAP);

	inline CUIRect PlayerRect(const CUIRect &Screen, bool DisplayExpanded, bool CameraExpanded = false, bool ShowCutControls = true)
	{
		const float Width = std::min(640.0f, std::max(0.0f, Screen.w - 24.0f));
		const float Height = PLAYER_PANEL_HEIGHT - (ShowCutControls ? 0.0f : CUT_SECTION_HEIGHT) +
				     (DisplayExpanded ? DISPLAY_HEIGHT + 6.0f : 0.0f) + (CameraExpanded ? CAMERA_HEIGHT + 6.0f : 0.0f);
		return {Screen.x + (Screen.w - Width) * 0.5f, Screen.y + Screen.h - Height - 12.0f, Width, Height};
	}

	constexpr float TransportWidth(float ButtonSize, bool ShowSkipDuration)
	{
		// 十个播放图标、倍速文字与间距；短回放没有时长下拉框。
		return 10.0f * ButtonSize + 36.0f + 39.0f + (ShowSkipDuration ? 59.0f : 0.0f);
	}

	inline float TransportButtonSize(float PanelWidth, bool ShowSkipDuration)
	{
		return std::clamp((PanelWidth - 16.0f - TransportWidth(0.0f, ShowSkipDuration)) / 10.0f, 0.0f, TRANSPORT_BUTTON_MAX);
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

	inline CUIRect PanelDragRect(const CUIRect &Panel, CUIRect Title, bool DirectTouch)
	{
		if(!DirectTouch)
			return Panel;
		// 触屏仅用标题左侧挪动面板，内容控件保留自己的拖动归属。
		Title.w = std::clamp(Title.w, 0.0f, 120.0f);
		return Title;
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

	// 渲染回放弹窗内容总高：顶部 4 + 回放显示 110 + 10 + 视频导出 278 + 10 +
	// 播放选项 110 + 10 + 视频编码 110 = 642；在线时追加断线提示 30。
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

	// 公共弹窗外壳固定占位：上下各 12 内边距、标题 22、标题后 6 间隔、
	// 底部 26 按钮行 + 8 间隔。内容高度只需要算自己的部分。
	constexpr float POPUP_CHROME_HEIGHT = 86.0f;

	// 合影地图选择弹窗沿用渲染回放弹窗的行节奏（20 高、4 间距、6 分组间隔），
	// 列表高度按可见地图条数收缩：只有一两张地图时不留整片空白，超过上限后不再长高。
	constexpr float POSE_MAP_HINT_HEIGHT = 16.0f;
	constexpr float POSE_MAP_ROW_HEIGHT = RENDER_ROW_HEIGHT;
	constexpr float POSE_MAP_ERROR_HEIGHT = 14.0f;
	constexpr float POSE_MAP_GAP = RENDER_ROW_GAP;
	constexpr float POSE_MAP_LIST_GAP = 6.0f;
	// 文件夹选择器本质是列表：一次至少给 8 行，地图多时给到 12 行，避免只有几行还要滚动。
	constexpr int POSE_MAP_MIN_ROWS = 8;
	constexpr int POSE_MAP_MAX_ROWS = 12;

	inline int PoseMapVisibleRows(int MapCount)
	{
		return std::clamp(MapCount, POSE_MAP_MIN_ROWS, POSE_MAP_MAX_ROWS);
	}

	// 渲染模式在输入行下方多画四行（输出格式 / 分辨率倍数 / 锐化 / 像素预算），
	// 列表下移，高度必须一起算。
	constexpr int POSE_MAP_RENDER_ROWS = 4;

	inline float PoseMapRenderRowsHeight(int RenderRows)
	{
		return RenderRows > 0 ? RenderRows * (POSE_MAP_ROW_HEIGHT + POSE_MAP_GAP) : 0.0f;
	}

	inline float PoseMapContentHeight(int MapCount, int RenderRows = 0)
	{
		return POSE_MAP_HINT_HEIGHT + POSE_MAP_GAP + POSE_MAP_ROW_HEIGHT + PoseMapRenderRowsHeight(RenderRows) + POSE_MAP_LIST_GAP +
		       PoseMapVisibleRows(MapCount) * POSE_MAP_ROW_HEIGHT + POSE_MAP_GAP + POSE_MAP_ERROR_HEIGHT;
	}
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_UI_H
