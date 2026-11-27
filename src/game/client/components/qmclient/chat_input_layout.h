#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_INPUT_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_INPUT_LAYOUT_H

#include <game/client/QmUi/UiTokens.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <cmath>

// 翻译按钮占据输入行最左侧，前缀（「全体」／「队伍」／「聊天」）与正文整体右移一个按钮区。
// 正文宽度由行宽、按钮区和前缀宽度决定，光标与选区都从正文起点开始，不与按钮重叠。
constexpr float QM_CHAT_TRANSLATE_BUTTON_GAP = ui_token::chat::input::BUTTON_GAP;

// 输入行行高：翻译按钮直径与同一行「全体」前缀块的高度都只取这一个值。
// 两者都是「一行控件」，各算一次就会在换字号后一大一小——这正是按钮比文字矮一截的根因。
inline float QmChatInputRowHeight(float FontSize)
{
	return ui_token::chat::input::RowHeight(FontSize);
}

// 历史区底边：输入行只占一行文字（「全体」/「聊天」前缀那一行），
// 翻译按钮的胶囊高度不属于行距，不能拿来当留白，否则消息和输入行之间会多出半行。
// 真正聊天与 HUD 编辑器的固定预览都从这里取值，两个画面才不会一个贴底、一个悬空。
inline float QmChatHistoryBottom(float BaseY, float FontSize)
{
	return BaseY - std::max(0.0f, FontSize);
}

// 圆形图标按钮：宽高都等于行高（正方形 ⇒ 绘制层按胶囊档裁成正圆），
// 于是按钮与同一行「全体」前缀块等高，首行垂直居中后两者基线对齐。
inline float QmChatTranslateButtonSize(float FontSize)
{
	// 按钮整体比行高再小一档，文字行不被撑高；图标按同一比例一起缩。
	return QmChatInputRowHeight(FontSize) * ui_token::chat::input::BUTTON_ROW_RATIO;
}

// 按钮与输入首行垂直居中，多行输入时仍固定在第一行；高度就是输入行行高。
inline float QmChatTranslateButtonHeight(float FontSize)
{
	return QmChatTranslateButtonSize(FontSize);
}

// 描边宽度：由直径按 token 比例派生，圆变大时描边仍是一圈细边，不跟着变粗。
inline float QmChatTranslateButtonBorderWidth(float ButtonSize)
{
	return ui_token::chat::input::ButtonBorderWidth(ButtonSize);
}

// 按钮内径：直径扣掉两侧描边，图标只在这个圆里排版。
inline float QmChatTranslateButtonInnerSize(float ButtonSize)
{
	return std::max(0.0f, ButtonSize - 2.0f * QmChatTranslateButtonBorderWidth(ButtonSize));
}

// 图标盒（方框边长）：取按钮内径。DoLabel_QmIcon 收的是盒子而不是字形，
// 绘制层会再按 ICON_INK_RATIO 把方框换成字形 em，所以这里不能提前乘。
inline float QmChatTranslateIconBoxSize(float ButtonSize)
{
	// 图标不能和圆同高：外圈留出内边距，图标才不会顶到描边上。
	return QmChatTranslateButtonInnerSize(ButtonSize) * (1.0f - 2.0f * ui_token::chat::input::ICON_INSET_RATIO);
}

// 图标字形（em）尺寸：沿用控件「字形 = 盒子 × ICON_INK_RATIO」的通用比例，
// 盒子取内径，字形因而接近内圈，不再是原来那枚只占内圈四成的小点。
inline float QmChatTranslateIconGlyphSize(float ButtonSize)
{
	return QmChatTranslateIconBoxSize(ButtonSize) * ui_token::control::ICON_INK_RATIO;
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
	// 按钮以首行文字中心为轴居中：行高比字号高出的那半档上下均分，
	// 桌面「全体」文字与手机「全体」前缀块因此都落在同一条基线上。
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
