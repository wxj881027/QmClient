// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QMUI_UITEXTCARET_H
#define GAME_CLIENT_QMUI_UITEXTCARET_H

#include "UiTokens.h"

#include <base/color.h>
#include <base/time.h>

#include <game/client/ui_rect.h>

#include <algorithm>
#include <chrono>
#include <cmath>

class IGraphics;

// 输入光标（caret）公共组件：设置页输入框、控制台输入、聊天输入共用同一套样式与几何。
//
// 本组件只关心「这个时刻该不该画、画在哪、多大、多圆」：布局计算与绘制分离，
// 相位只由调用点传入的绝对时间推导，组件自身不持有跨帧状态，所以并排的两个输入框
// 永远同相位闪烁，不会各闪各的。纯几何函数可直接单元测试，也可挂 Google Benchmark。
//
// 唯一绘制入口是 Render()；调用点不允许自己画光标（包括自绘竖线与方块）。
namespace qm_text_caret
{
	// 光标样式：竖线 / 下划线 / 方块。
	enum class EStyle
	{
		BAR = 0,
		UNDERSCORE = 1,
		BLOCK = 2,
	};

	// 配置项 qm_text_caret_style 的合法区间（含两端）。
	inline constexpr int STYLE_MIN = 0;
	inline constexpr int STYLE_MAX = 2;

	// 配置值归一化：越界值收敛到最近的合法样式，不抛错也不留非法枚举。
	inline EStyle StyleFromConfig(int Value)
	{
		if(Value <= STYLE_MIN)
			return EStyle::BAR;
		if(Value >= STYLE_MAX)
			return EStyle::BLOCK;
		return static_cast<EStyle>(Value);
	}

	// 插入点所在的字符格：x 为插入点左沿，h 通常为字号。
	struct SCaretCell
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		// 该字符的格宽。0 表示只有插入点没有格宽，方块与下划线按字号比例兜底。
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	struct SCaretParams
	{
		EStyle m_Style = EStyle::BAR;
		SCaretCell m_Cell;
		// 一个物理像素对应的世界单位：令牌按物理像素给尺寸，换分辨率时粗细不变。
		float m_WorldPerPixel = 1.0f;
		// 闪烁相位的时间基准（秒）。调用点传绝对时间，组件不保存「本框闪烁起点」。
		double m_TimeSec = 0.0;
		// false 表示恒亮：选区/输入法组合期间，以及动效等级关闭时都走这一档。
		bool m_Blink = true;
		// 输入框文字区域。只用于横向夹取：窄框里光标不许越过右边界。
		// 纵向不夹取——多行滚动的光标允许随内容移出可视带，靠绘制裁剪而不是贴边。
		CUIRect m_HorizBounds{};
	};

	struct SCaretShape
	{
		// 该时刻是否绘制（闪烁的灭相、没有高度时为 false）。
		bool m_Visible = false;
		// 实际绘制矩形与圆角。
		CUIRect m_Rect{};
		float m_Radius = 0.0f;
		// 外描边矩形与圆角：与本体同一条曲线向外扩一圈，保证压在任意底色上都看得清。
		CUIRect m_OutlineRect{};
		float m_OutlineRadius = 0.0f;
		// 一个物理像素对应的世界单位，绘制时用于像素对齐。
		float m_PixelSize = 1.0f;
	};

	// 全站唯一的光标时间基准（秒）：聊天输入、设置页输入、控制台命令输入与日志定位光标
	// 都取同一个进程单调时钟（time_get_nanoseconds），所以并排的输入框永远同相位闪烁。
	// 调用点每帧重新取一次，不要缓存成「本框闪烁起点」。
	inline double NowSeconds()
	{
		return std::chrono::duration<double>(time_get_nanoseconds()).count();
	}

	// 调用点统一的闪烁门控：CaretActive 表示"光标此刻本该闪"（没有拖选、没有输入法组合）。
	// 动效等级 0 时恒亮——这是全站唯一"关掉动效反而更亮"的例外，集中在这里判断，
	// 调用点不要再写死 m_Blink = false（那会让光标永远常亮，正是控制台光标曾经的问题）。
	inline bool ShouldBlink(bool CaretActive, int MotionLevel)
	{
		return CaretActive && MotionLevel > 0;
	}

	// 闪烁是否处于亮相：相位由绝对时间取模得到，负数时间按同一周期回绕。
	//
	// Blink=false 是「恒亮档」，只有两种来源，别拿它当默认值：
	// 1) 光标此刻本来就不该闪：鼠标拖选、输入法组合输入，以及日志定位光标（选中位置标记）；
	// 2) qm_ui_motion_level = 0 —— 这是全站唯一的「关掉动效反而更亮」的例外约定：
	//    动效等级关闭时所有过渡都直接落到终点，光标取「亮相」这个终点，恒亮而不是灭相；
	//    否则关掉动效会得到一个半周期才亮一次、几乎看不见的光标。该判断由调用点做，
	//    组件自身不读配置，保持纯几何与纯相位。
	// 其余情况调用点必须传 Blink=true，并把 NowSeconds() 放进 SCaretParams::m_TimeSec。
	inline bool BlinkVisible(double TimeSec, bool Blink)
	{
		if(!Blink)
			return true;
		const double Period = ui_token::motion::CARET_BLINK_PERIOD_SEC;
		const double Visible = Period * ui_token::motion::CARET_BLINK_DUTY;
		if(Period <= 0.0 || Visible <= 0.0)
			return true;
		double Phase = std::fmod(TimeSec, Period);
		if(Phase < 0.0)
			Phase += Period;
		return Phase < Visible;
	}

	// 纯几何解析：给定样式、光标格与时刻，返回是否可见与实际绘制矩形。
	inline SCaretShape ResolveShape(const SCaretParams &Params)
	{
		SCaretShape Shape;
		const float PixelSize = std::clamp(Params.m_WorldPerPixel, 0.0001f, 1000.0f);
		Shape.m_PixelSize = PixelSize;
		if(Params.m_Cell.m_H <= 0.0f)
			return Shape;

		const float Height = Params.m_Cell.m_H;
		const float FallbackWidth = Height * ui_token::text_caret::WIDTH_RATIO;
		const float CellWidth = Params.m_Cell.m_W > 0.0f ? Params.m_Cell.m_W : FallbackWidth;
		const float Thickness = ui_token::text_caret::THICKNESS_PX * PixelSize;
		// 描边默认取 token；竖线与方块至少要有一圈，压在深色字上也看得出边界。
		float Outline = std::max(ui_token::text_caret::OUTLINE_PX * PixelSize, std::min(Thickness, Height) * ui_token::text_caret::OUTLINE_RATIO);

		CUIRect Rect{};
		float Radius = 0.0f;
		switch(Params.m_Style)
		{
		case EStyle::UNDERSCORE:
			Rect = {Params.m_Cell.m_X, Params.m_Cell.m_Y + Height - Thickness, CellWidth, Thickness};
			Radius = ui_token::text_caret::RADIUS_PX * PixelSize;
			break;
		case EStyle::BLOCK:
			Rect = {Params.m_Cell.m_X, Params.m_Cell.m_Y, CellWidth, Height};
			Radius = ui_token::text_caret::BLOCK_RADIUS_PX * PixelSize;
			break;
		case EStyle::BAR:
		default:
			Rect = {Params.m_Cell.m_X, Params.m_Cell.m_Y, Thickness, Height};
			// 竖线按胶囊收圆：两端是圆头，与正文字形的圆角笔画同一种观感。
			Radius = Thickness * 0.5f;
			break;
		}

		// 窄框夹取：先按可用宽度收窄，再贴住边界，光标整体留在文字区内。
		const bool HasBounds = Params.m_HorizBounds.w > 0.0f;
		const auto ClampHorizontally = [&Params](CUIRect &Target) {
			const float MinX = Params.m_HorizBounds.x;
			const float MaxX = Params.m_HorizBounds.x + Params.m_HorizBounds.w;
			Target.w = std::min(Target.w, Params.m_HorizBounds.w);
			Target.x = std::clamp(Target.x, MinX, MaxX - Target.w);
		};
		if(HasBounds)
			ClampHorizontally(Rect);

		// 圆角不超过最短边的一半，否则渲染器会把形状钳成畸形。
		Radius = std::clamp(Radius, 0.0f, std::min(Rect.w, Rect.h) * 0.5f);

		Shape.m_Visible = BlinkVisible(Params.m_TimeSec, Params.m_Blink);
		Shape.m_Rect = Rect;
		Shape.m_Radius = Radius;
		Shape.m_OutlineRect = {Rect.x - Outline, Rect.y - Outline, Rect.w + Outline * 2.0f, Rect.h + Outline * 2.0f};
		Shape.m_OutlineRadius = Radius + Outline;
		if(HasBounds)
			ClampHorizontally(Shape.m_OutlineRect);
		return Shape;
	}

	// 绘制入口：几何全部来自 SCaretShape，这里只提交两条圆角矩形（描边层 + 本体层）。
	// 描边层先画，本体层盖在上面，光标压在字形或浅色底上都不会糊掉。
	void Render(IGraphics *pGraphics, const SCaretShape &Shape, const ColorRGBA &TextColor, const ColorRGBA &OutlineColor);
} // namespace qm_text_caret

#endif
