#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_INPUT_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_INPUT_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

// 翻译按钮占据输入行最左侧，前缀（「全体」／「队伍」／「聊天」）与正文整体右移一个按钮区。
// 正文宽度由行宽、按钮区和前缀宽度决定，光标与选区都从正文起点开始，不与按钮重叠。
constexpr float QM_CHAT_TRANSLATE_BUTTON_GAP = 4.0f;

inline float QmChatTranslateButtonSize(float FontSize)
{
	return std::max(16.0f, FontSize * 1.35f);
}

// 按钮与输入首行垂直居中，多行输入时仍固定在第一行。
inline float QmChatTranslateButtonHeight(float FontSize)
{
	return std::max(FontSize + 4.0f, 16.0f);
}

inline float QmChatInputMessageWidth(float LineWidth, float FontSize, float PrefixWidth)
{
	return std::max(1.0f, LineWidth - QmChatTranslateButtonSize(FontSize) - QM_CHAT_TRANSLATE_BUTTON_GAP - PrefixWidth);
}

struct SQmChatInputLayout
{
	// 前缀与正文的起点，位于按钮区右侧。
	float m_TextStartX = 0.0f;
	// 从 m_TextStartX 起算的光标行宽，右边界与正文独占整行时一致。
	float m_CursorLineWidth = 0.0f;
	// 前缀之后正文的最大宽度。
	float m_MessageMaxWidth = 1.0f;
	float m_ButtonX = 0.0f;
	float m_ButtonY = 0.0f;
	float m_ButtonW = 0.0f;
	float m_ButtonH = 0.0f;
};

inline SQmChatInputLayout QmChatResolveInputLayout(float X, float Y, float LineWidth, float FontSize, float PrefixWidth)
{
	const float ButtonSize = QmChatTranslateButtonSize(FontSize);
	const float ButtonHeight = QmChatTranslateButtonHeight(FontSize);

	SQmChatInputLayout Layout;
	Layout.m_ButtonX = X;
	Layout.m_ButtonY = Y + (FontSize - ButtonHeight) * 0.5f;
	Layout.m_ButtonW = ButtonSize;
	Layout.m_ButtonH = ButtonHeight;
	Layout.m_TextStartX = X + ButtonSize + QM_CHAT_TRANSLATE_BUTTON_GAP;
	Layout.m_CursorLineWidth = std::max(1.0f, LineWidth - ButtonSize - QM_CHAT_TRANSLATE_BUTTON_GAP);
	Layout.m_MessageMaxWidth = QmChatInputMessageWidth(LineWidth, FontSize, PrefixWidth);
	return Layout;
}

struct SQmChatInputPixelClip
{
	int m_X = 0;
	int m_Y = 0;
	int m_W = 0;
	int m_H = 0;
};

// HUD 移动、缩放后，裁切框必须和文字使用同一屏幕映射；向外取整保留边缘像素。
inline SQmChatInputPixelClip QmChatInputPixelClip(const CUIRect &Rect, const CUIRect &MappedScreen, int ScreenWidth, int ScreenHeight)
{
	if(MappedScreen.w <= 0.0f || MappedScreen.h <= 0.0f || ScreenWidth <= 0 || ScreenHeight <= 0)
		return {};

	const float PixelWidth = ScreenWidth;
	const float PixelHeight = ScreenHeight;
	const float ScaleX = PixelWidth / MappedScreen.w;
	const float ScaleY = PixelHeight / MappedScreen.h;
	// 先限制到帧缓冲再转整数，避免完全移出屏幕的坐标超出整数范围。
	const int Left = (int)std::clamp(std::floor((Rect.x - MappedScreen.x) * ScaleX), 0.0f, PixelWidth);
	const int Top = (int)std::clamp(std::floor((Rect.y - MappedScreen.y) * ScaleY), 0.0f, PixelHeight);
	const int Right = (int)std::clamp(std::ceil((Rect.x + Rect.w - MappedScreen.x) * ScaleX), (float)Left, PixelWidth);
	const int Bottom = (int)std::clamp(std::ceil((Rect.y + Rect.h - MappedScreen.y) * ScaleY), (float)Top, PixelHeight);
	return {Left, Top, Right - Left, Bottom - Top};
}

// 同一绘制映射同时用于裁剪、鼠标命中与 tooltip 锚点，避免 HUD 移动/缩放后坐标错位。
struct SQmChatViewport
{
	CUIRect m_MapRect{};
	vec2 m_PixelSize{};

	vec2 ToLocal(vec2 Pixel) const
	{
		return vec2(m_MapRect.x, m_MapRect.y) + Pixel * vec2(m_MapRect.w, m_MapRect.h) / m_PixelSize;
	}

	CUIRect ToPixels(const CUIRect &Rect) const
	{
		const vec2 Scale = m_PixelSize / vec2(m_MapRect.w, m_MapRect.h);
		return {(Rect.x - m_MapRect.x) * Scale.x, (Rect.y - m_MapRect.y) * Scale.y, Rect.w * Scale.x, Rect.h * Scale.y};
	}

	CUIRect ClipPixels(const CUIRect &Rect) const
	{
		const auto Clip = QmChatInputPixelClip(Rect, m_MapRect, (int)m_PixelSize.x, (int)m_PixelSize.y);
		return {(float)Clip.m_X, (float)Clip.m_Y, (float)Clip.m_W, (float)Clip.m_H};
	}

	CUIRect ToUi(const CUIRect &Rect, const CUIRect &UiScreen) const
	{
		const CUIRect Pixels = ToPixels(Rect);
		const vec2 Scale = vec2(UiScreen.w, UiScreen.h) / m_PixelSize;
		return {UiScreen.x + Pixels.x * Scale.x, UiScreen.y + Pixels.y * Scale.y, Pixels.w * Scale.x, Pixels.h * Scale.y};
	}
};

#endif
