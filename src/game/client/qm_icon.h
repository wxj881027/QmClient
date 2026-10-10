// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_ICON_H
#define GAME_CLIENT_QM_ICON_H

#include "qm_icon_color_policy.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/shared/config.h>

#include <game/client/QmUi/QmTheme.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

enum class EQmIcon
{
#define QM_ICON_ENTRY(Id, Name, Codepoint) Id,
#define QM_ICON_ALIAS QM_ICON_ENTRY
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
	COUNT,
};

enum class EQmIconState
{
	NORMAL = 0,
	HOVER,
	ACTIVE,
	DISABLED,
};

inline int NormalizeQmIconWeight(const int Weight)
{
	// 旧双色调和非法配置回退到默认 Bold；Thin 与 Light 共享同一套资源。
	return Weight >= 0 && Weight <= 4 ? Weight : 1;
}

// 与文字渲染器 ActualSize 的 round_truncate 及 GetGlyph 的范围钳制一致。
inline int QmIconRasterPixelSize(float FontSize, float PixelScale)
{
	return round_truncate(std::clamp(FontSize * PixelScale, 6.0f, 128.0f));
}

// 字体图标在目标方框中保留一致的内边距。
inline float QmIconFontSize(const CUIRect &Rect)
{
	return std::min(Rect.w, Rect.h) * 0.8f;
}

inline ColorRGBA QmUiIconColor(const ColorRGBA &Color, const int ConfiguredColor, const unsigned int CustomColor = 0xFFFFFFFF, const double RainbowTime = 0.0)
{
	ColorRGBA Result;
	switch(ConfiguredColor)
	{
	case 2:
		Result = ColorRGBA(0.0f, 0.0f, 0.0f, Color.a);
		break;
	case 3:
		Result = color_cast<ColorRGBA>(ColorHSLA(CustomColor));
		Result.a = Color.a;
		break;
	case 4:
		Result = color_cast<ColorRGBA>(ColorHSLA(static_cast<float>(std::fmod(std::fmod(RainbowTime, 5.0) + 5.0, 5.0) / 5.0), 0.75f, 0.6f, Color.a));
		break;
	case 1:
	default:
		Result = ColorRGBA(1.0f, 1.0f, 1.0f, Color.a);
		break;
	}
	return Result;
}

// 好友、收藏与禁用覆盖层等保持语义色；作用域退出恢复嵌套前的策略。
class CQmIconSemanticColorScope
{
	inline static thread_local bool s_KeepSemanticColor = false;
	bool m_Previous;

public:
	CQmIconSemanticColorScope(bool KeepSemanticColor = true) : m_Previous(s_KeepSemanticColor) { s_KeepSemanticColor = s_KeepSemanticColor || KeepSemanticColor; }
	~CQmIconSemanticColorScope() { s_KeepSemanticColor = m_Previous; }
	CQmIconSemanticColorScope(const CQmIconSemanticColorScope &) = delete;
	CQmIconSemanticColorScope &operator=(const CQmIconSemanticColorScope &) = delete;
	static bool Active() { return s_KeepSemanticColor; }
};

// 每帧只采样一次时钟，先以 double 缩到五秒周期，再参与 RGB 运算。
class CQmIconFrameColorClock
{
	inline static double s_Time = 0.0;

public:
	static void BeginFrame(double Time) { s_Time = std::isfinite(Time) ? std::fmod(Time, 5.0) : 0.0; }
	static double Time() { return s_Time; }
};

// 好友与收藏共用语义图标识别，缓存与回退绘制保持一致。
inline bool QmUiIconHasSemanticColor(EQmIcon Icon)
{
	return Icon == EQmIcon::HEART || Icon == EQmIcon::STAR;
}

// 语义图标实心态：两个开关彼此独立，互不影响；其余图标不受控。
// 实心沿用同一 Phosphor 码位、仅切换到随包 Fill 字面（EFontPreset::ICON_FONT_FILL），
// 字体实测：heart/star 在 Fill 字面为 1 轮廓实心，Regular/Bold/Light 为 2 轮廓空心。
inline bool QmUiIconFilledStyle(EQmIcon Icon)
{
	if(Icon == EQmIcon::HEART)
		return g_Config.m_QmUiFriendIconFilled != 0;
	if(Icon == EQmIcon::STAR)
		return g_Config.m_QmUiFavoriteIconFilled != 0;
	return false;
}

// 字体图标在最终绘制时使用同一颜色策略，保留各自状态 alpha。
inline ColorRGBA ConfiguredQmUiIconColor(const ColorRGBA &Color, EQmIcon Icon = EQmIcon::COUNT)
{
	if(CQmIconSemanticColorScope::Active())
		return Color;
	// 好友与收藏独立着色，沿用调用者的禁用、悬停与动画透明度。
	if(QmUiIconHasSemanticColor(Icon))
	{
		const unsigned SemanticColor = Icon == EQmIcon::HEART ? g_Config.m_QmUiFriendIconColor : g_Config.m_QmUiFavoriteIconColor;
		return color_cast<ColorRGBA>(ColorHSLA(SemanticColor)).WithAlpha(Color.a);
	}
	const int Preset = qm_icon_settings::CustomColorEnabled(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled) ? 3 : g_Config.m_QmUiIconColor;
	if(Preset != 4)
		return QmUiIconColor(Color, Preset, g_Config.m_QmUiIconCustomColor);

	return QmUiIconColor(Color, Preset, g_Config.m_QmUiIconCustomColor, CQmIconFrameColorClock::Time());
}

// 字体图标共用实际背景保护策略。
inline ColorRGBA ConfiguredQmUiIconContrastColor(const ColorRGBA &Primary)
{
	if(CUiScopedSurfaceText::HasKnownSurface())
		return QmUiIconSurfaceProtection(Primary, CUiScopedSurfaceText::CurrentSurface());
	// 地图像素未知时保留弱保护，不能把默认深色背板当作真实背景。
	// 彩虹固定深色保护，不随本体亮度越界而黑白闪变。
	const bool Rainbow = !CQmIconSemanticColorScope::Active() && g_Config.m_QmUiIconColor == 4 && !qm_icon_settings::CustomColorEnabled(g_Config.m_QmUiIconColor, g_Config.m_QmUiIconCustomColorEnabled);
	const ColorRGBA Protection = Rainbow ? ColorRGBA(0, 0, 0, Primary.a) : QmUiIconContrastColor(Primary);
	return Protection.WithMultipliedAlpha(0.35f);
}

struct SQmIconStyle
{
	ColorRGBA m_Normal{qm_theme::ICON.m_Normal};
	ColorRGBA m_Hover{qm_theme::ICON.m_Hover};
	ColorRGBA m_Active{qm_theme::ICON.m_Active};
	ColorRGBA m_Disabled{qm_theme::ICON.m_Disabled};

	ColorRGBA Color(EQmIconState State) const
	{
		switch(State)
		{
		case EQmIconState::HOVER: return m_Hover;
		case EQmIconState::ACTIVE: return m_Active;
		case EQmIconState::DISABLED: return m_Disabled;
		default: return m_Normal;
		}
	}
};

// 字体图标提交计数仅用于已启用的性能诊断，普通路径不计时不分配。
class CQmIconDrawDiagnostics
{
	inline static uint64_t s_Draws = 0;

public:
	static void Record(int Count = 1)
	{
		if(Count > 0 && (g_Config.m_QmPerfDebug || g_Config.m_QmPerfLogfile || g_Config.m_QmPerfStutterDiagnostics))
			s_Draws += Count;
	}
	static uint64_t Take() { return std::exchange(s_Draws, 0); }
};

class CQmIconRegistry
{
public:
	static const char *IconName(EQmIcon Icon)
	{
		switch(Icon)
		{
#define QM_ICON_ENTRY(Id, Name, Codepoint) \
	case EQmIcon::Id: return Name;
#define QM_ICON_ALIAS QM_ICON_ENTRY
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
		default: return "";
		}
	}

	// 仅接受一个完整字形，正文与混合文本不进入图标绘制路径。
	static EQmIcon IconFromGlyph(const char *pText, int Length = -1)
	{
		if(pText == nullptr || Length == 0)
			return EQmIcon::COUNT;
		char aGlyph[5];
		if(Length >= 0)
		{
			if(Length > 4)
				return EQmIcon::COUNT;
			mem_copy(aGlyph, pText, Length);
			aGlyph[Length] = '\0';
			pText = aGlyph;
		}
		if(!pText[0])
			return EQmIcon::COUNT;
		const char *pEnd = pText;
		const int Codepoint = str_utf8_decode(&pEnd);
		if((Length < 0 && *pEnd != '\0') || (Length >= 0 && pEnd - pText != Length))
			return EQmIcon::COUNT;
		switch(Codepoint)
		{
#define QM_ICON_ENTRY(Id, Name, Codepoint) \
	case Codepoint: return EQmIcon::Id;
#define QM_ICON_ALIAS(Id, Name, Codepoint)
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
		default: return EQmIcon::COUNT;
		}
	}

	static int Codepoint(EQmIcon Icon)
	{
		switch(Icon)
		{
#define QM_ICON_ENTRY(Id, Name, Codepoint) \
	case EQmIcon::Id: return Codepoint;
#define QM_ICON_ALIAS QM_ICON_ENTRY
#include "qm_icon_registry.inc"
#undef QM_ICON_ENTRY
#undef QM_ICON_ALIAS
		default: return 0;
		}
	}
};

#endif
