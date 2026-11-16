// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_QMPASSWORDICON_H
#define GAME_CLIENT_QMUI_QMPASSWORDICON_H

#include <algorithm>
#include <cmath>

// 密码显隐图标的形变动画参数。资产由
// qmclient_scripts/qm_build_password_icon_frames.py 从随包 Phosphor 字体烘焙，
// 这里只放纯计算，便于单元测试直接包含。
namespace qm_password_icon
{
	enum
	{
		STYLE_FADE = 0,
		STYLE_MORPH = 1,
	};

	// 帧条纹理的烘焙边长（源资产），运行时按目标绘制边长重新采样。
	inline constexpr int FRAME_SOURCE_PIXELS = 64;
	inline constexpr int FRAME_MIN_PIXELS = 8;
	inline constexpr int FRAME_MAX_PIXELS = 96;
	inline constexpr int FRAME_COUNT = 24;

	// 烘焙画布的字体单位边长与字体 upm：绘制边长 = FontSize * CANVAS_UNITS / FONT_UNITS。
	// 画布是固定在墨迹中心的 1152 单位方框，因此帧内墨迹与同字号字形墨迹等高。
	inline constexpr float CANVAS_UNITS = 1152.0f;
	inline constexpr float FONT_UNITS = 1024.0f;

	// 帧条在与绘制尺寸 1:1 的纹理上重采样后再绘制：GPU 缩放的峰值衰减是「发糊」
	// 的主因（64px 缩到 14px 时笔画峰值只有 217，同字号字形是 246）。取整到整数
	// 像素并让 quad 落在整数坐标上，采样才不会落到半像素。
	inline int ResolveFramePixels(float Side)
	{
		return std::clamp(static_cast<int>(std::lround(Side)), FRAME_MIN_PIXELS, FRAME_MAX_PIXELS);
	}

	inline int ResolveStyle(int ConfigValue)
	{
		return ConfigValue == STYLE_MORPH ? STYLE_MORPH : STYLE_FADE;
	}

	// UI 单位 -> 物理像素的倍率。QmUi 画在虚拟分辨率上（高 600 / qm_ui_scale），
	// CUi::PixelSize() 返回的正是它的倒数；字形由 FreeType 按「物理像素」光栅化
	// （ActualSize = FontSize × FakeToScreen），帧条如果按 UI 单位取纹理，就会被
	// GPU 放大同一倍率：14 单位见方的图标在 1266 高的窗口里实际显示约 30 物理像素，
	// 双线性放大把笔画摊平，峰值从 255 掉到 210 左右——这才是「动画期间永远有点糊」
	// 的根因，换档位、锐化都作用在错的坐标系里。
	inline float ResolvePhysicalScale(float PixelSize)
	{
		if(!(PixelSize > 0.0f) || !std::isfinite(PixelSize))
			return 1.0f;
		const float Scale = 1.0f / PixelSize;
		return Scale > 0.25f && Scale < 8.0f ? Scale : 1.0f;
	}

	// 帧条纹理的边长（物理像素）：先换算到物理像素，再量化到偶数档。
	// 量化让 hover 的 1.0→1.1 缩放弹簧不至于每帧改纹理尺寸（每次改尺寸都要重采样
	// 并重新上传纹理）；上限取源资产边长，绝不放大源图。
	inline int ResolveFrameTexturePixels(float Side, float PhysicalScale)
	{
		const int Raw = ResolveFramePixels(Side * PhysicalScale);
		return std::min((Raw + 1) & ~1, FRAME_SOURCE_PIXELS);
	}

	inline float FrameSide(float FontSize)
	{
		return FontSize * CANVAS_UNITS / FONT_UNITS;
	}

	// 弹簧进度 -> 帧索引；进度在两端附近抖动时不会越界。
	inline int FrameIndex(float Progress)
	{
		const float Clamped = std::clamp(Progress, 0.0f, 1.0f);
		return std::clamp(static_cast<int>(std::round(Clamped * (FRAME_COUNT - 1))), 0, FRAME_COUNT - 1);
	}

	// 帧条横向 UV：半纹素内缩，避免相邻帧在双线性采样时串色。
	// Pixels 是该档纹理的帧边长（纹理宽 = FRAME_COUNT * Pixels）。
	inline float FrameU0(int Index, int Pixels)
	{
		const float Width = static_cast<float>(FRAME_COUNT * Pixels);
		return (static_cast<float>(Index * Pixels) + 0.5f) / Width;
	}

	inline float FrameU1(int Index, int Pixels)
	{
		const float Width = static_cast<float>(FRAME_COUNT * Pixels);
		return (static_cast<float>((Index + 1) * Pixels) - 0.5f) / Width;
	}

	// 帧条纹理是上下两半：上半填充、下半描边（由 QmGrowGlyphOutline 从填充外扩）。
	// 字体渲染每个字形都有 fill + outline 两张纹理并先画描边，帧条也必须画那圈 1px
	// 暗边——否则图标没有边界，看起来比静止态更白更糊。
	inline float FrameV0(bool Outline, int Pixels)
	{
		const float Height = static_cast<float>(Pixels * 2);
		const float Base = Outline ? static_cast<float>(Pixels) : 0.0f;
		return (Base + 0.5f) / Height;
	}

	inline float FrameV1(bool Outline, int Pixels)
	{
		const float Height = static_cast<float>(Pixels * 2);
		const float Base = Outline ? static_cast<float>(Pixels) : 0.0f;
		return (Base + static_cast<float>(Pixels) - 0.5f) / Height;
	}
} // namespace qm_password_icon

#endif
