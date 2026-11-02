#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_APPEARANCE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_APPEARANCE_H

#include "console_color_schemes.h"

#include <base/color.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <array>

namespace QmConsoleAppearance
{
	// 配置里的方案编号：0=现代终端、1=经典 PowerShell、2=自定义，
	// 3 起是 QmConsoleSchemes 的内置配色。整套代码只在这里定义「自定义」的编号，
	// 调用点统一走 IsCustomScheme，避免字面量 2 散落到渲染、布局和测量各处。
	static constexpr int CONFIG_CUSTOM_SCHEME = 2;
	inline bool IsCustomScheme(int Scheme)
	{
		return Scheme == CONFIG_CUSTOM_SCHEME;
	}

	enum EColor
	{
		BACKGROUND,
		TEXT,
		COMMAND,
		PARAMETER,
		STRING,
		NUMBER,
		LINK,
		SEARCH,
		SEARCH_SELECTED,
		COLOR_COUNT
	};

	struct SPalette
	{
		std::array<ColorRGBA, COLOR_COUNT> m_aColors;
		ColorRGBA m_Panel;
		ColorRGBA m_SelectedButton;
		ColorRGBA m_MutedText;
		ColorRGBA m_Border;
		ColorRGBA m_Shadow;
	};

	inline ColorRGBA Blend(ColorRGBA First, ColorRGBA Second, float Amount)
	{
		return {First.r + (Second.r - First.r) * Amount, First.g + (Second.g - First.g) * Amount,
			First.b + (Second.b - First.b) * Amount, First.a};
	}

	// 内置方案编号的起点：0=现代终端、1=经典 PowerShell、2=自定义，
	// 之后的编号依次对应 QmConsoleSchemes::At() 的 Windows Terminal 配色。
	static constexpr int k_BuiltinSchemeBase = 3;

	inline bool IsBuiltinScheme(int Scheme)
	{
		return Scheme >= k_BuiltinSchemeBase && Scheme < k_BuiltinSchemeBase + QmConsoleSchemes::Count();
	}

	inline const QmConsoleSchemes::SDefinition &BuiltinScheme(int Scheme)
	{
		return QmConsoleSchemes::At(Scheme - k_BuiltinSchemeBase);
	}

	// 取一个与正文有区别的强调色。个别方案（如 Solarized）的亮蓝色就是前景色本身，
	// 直接用它会让链接和正文一模一样，所以命中时退回同一色相的非亮版本。
	inline ColorRGBA AccentColor(const QmConsoleSchemes::SAnsiPalette &Ansi, int Preferred, int Fallback)
	{
		const ColorRGBA PreferredColor = Ansi.m_aColors[Preferred];
		return PreferredColor != Ansi.m_Foreground ? PreferredColor : Ansi.m_aColors[Fallback];
	}

	// 把 ANSI 16 色映射到控制台的语法语义色。
	// 选色与默认方案一致：命令最醒目、参数次之、链接偏冷、搜索偏暖，
	// 这样换配色只换色相，语法层级的强弱关系仍然可读。
	inline std::array<ColorRGBA, COLOR_COUNT> AnsiToSemanticColors(const QmConsoleSchemes::SAnsiPalette &Ansi)
	{
		std::array<ColorRGBA, COLOR_COUNT> Colors{};
		Colors[BACKGROUND] = Ansi.m_Background;
		Colors[TEXT] = Ansi.m_Foreground;
		Colors[COMMAND] = AccentColor(Ansi, 11, 3); // brightYellow / yellow
		Colors[PARAMETER] = AccentColor(Ansi, 14, 6); // brightCyan / cyan
		Colors[STRING] = AccentColor(Ansi, 10, 2); // brightGreen / green
		Colors[NUMBER] = AccentColor(Ansi, 13, 5); // brightPurple / purple
		Colors[LINK] = AccentColor(Ansi, 12, 4); // brightBlue / blue
		Colors[SEARCH] = AccentColor(Ansi, 9, 1); // brightRed / red
		Colors[SEARCH_SELECTED] = Ansi.m_aColors[3]; // yellow
		// 搜索命中与当前命中必须能区分开，否则看不出选中的是哪一个。
		if(Colors[SEARCH_SELECTED] == Colors[SEARCH])
			Colors[SEARCH_SELECTED] = AccentColor(Ansi, 0, 8);
		return Colors;
	}

	inline SPalette ResolvePalette(int Scheme, int Opacity, const std::array<unsigned, COLOR_COUNT> &Custom)
	{
		SPalette Result{{ColorRGBA(0.047f, 0.047f, 0.047f, 1.0f), ColorRGBA(0.80f, 0.80f, 0.80f, 1.0f),
					ColorRGBA(0.976f, 0.945f, 0.647f, 1.0f), ColorRGBA(0.38f, 0.84f, 0.84f, 1.0f),
					ColorRGBA(0.647f, 0.839f, 0.612f, 1.0f), ColorRGBA(0.839f, 0.647f, 0.898f, 1.0f),
					ColorRGBA(0.424f, 0.714f, 1.0f, 1.0f), ColorRGBA(0.957f, 0.529f, 0.443f, 1.0f),
					ColorRGBA(0.976f, 0.945f, 0.647f, 1.0f)},
			{}, {}, {}, {}, {}};
		if(Scheme == 1)
		{
			Result.m_aColors[BACKGROUND] = ColorRGBA(0.004f, 0.141f, 0.337f, 1.0f);
			Result.m_aColors[TEXT] = ColorRGBA(0.94f, 0.94f, 0.94f, 1.0f);
		}
		else if(IsCustomScheme(Scheme))
		{
			for(size_t i = 0; i < Custom.size(); ++i)
				Result.m_aColors[i] = color_cast<ColorRGBA>(ColorHSLA(Custom[i]));
		}
		else if(IsBuiltinScheme(Scheme))
		{
			Result.m_aColors = AnsiToSemanticColors(BuiltinScheme(Scheme).m_Palette);
		}
		Result.m_aColors[BACKGROUND].a = std::clamp(Opacity, 20, 100) / 100.0f;
		Result.m_Panel = Blend(Result.m_aColors[BACKGROUND], Result.m_aColors[TEXT], 0.06f);
		Result.m_SelectedButton = Blend(Result.m_Panel, Result.m_aColors[COMMAND], 0.22f);
		Result.m_MutedText = Blend(Result.m_aColors[TEXT], Result.m_aColors[BACKGROUND], 0.25f);
		// 底边向文本色适度靠拢，阴影保留背景色；两者透明度均跟随背景。
		Result.m_Border = Blend(Result.m_aColors[BACKGROUND], Result.m_aColors[TEXT], 0.18f);
		Result.m_Shadow = Result.m_aColors[BACKGROUND].WithAlpha(Result.m_aColors[BACKGROUND].a * 0.4f);
		return Result;
	}

	// 自定义方案的九项取色，按 EColor 顺序排列。凡是需要"替玩家算配色"的地方
	//（渲染、列表预览）都必须走这里，传空数组会把自定义方案算成一片黑。
	inline std::array<unsigned, COLOR_COUNT> CustomColors(const CConfig &Config)
	{
		return {Config.m_QmConsoleBackgroundColor, Config.m_QmConsoleTextColor, Config.m_QmConsoleCommandColor,
			Config.m_QmConsoleParameterColor, Config.m_QmConsoleStringColor, Config.m_QmConsoleNumberColor,
			Config.m_QmConsoleLinkColor, Config.m_QmConsoleSearchColor, Config.m_QmConsoleSearchSelectedColor};
	}

	inline SPalette Palette(const CConfig &Config)
	{
		return ResolvePalette(Config.m_QmConsoleColorScheme, Config.m_QmConsoleOpacity, CustomColors(Config));
	}

	inline float FontSize(int ConfiguredSize)
	{
		return static_cast<float>(std::clamp(ConfiguredSize, 1, 24));
	}

	inline void Reset(CConfig &Config)
	{
		Config.m_QmConsoleFontSize = DefaultConfig::QmConsoleFontSize;
		Config.m_QmConsoleColorScheme = DefaultConfig::QmConsoleColorScheme;
		Config.m_QmConsoleOpacity = DefaultConfig::QmConsoleOpacity;
		Config.m_QmConsoleHighlightCommands = DefaultConfig::QmConsoleHighlightCommands;
		Config.m_QmConsoleBackgroundColor = DefaultConfig::QmConsoleBackgroundColor;
		Config.m_QmConsoleTextColor = DefaultConfig::QmConsoleTextColor;
		Config.m_QmConsoleCommandColor = DefaultConfig::QmConsoleCommandColor;
		Config.m_QmConsoleParameterColor = DefaultConfig::QmConsoleParameterColor;
		Config.m_QmConsoleStringColor = DefaultConfig::QmConsoleStringColor;
		Config.m_QmConsoleNumberColor = DefaultConfig::QmConsoleNumberColor;
		Config.m_QmConsoleLinkColor = DefaultConfig::QmConsoleLinkColor;
		Config.m_QmConsoleSearchColor = DefaultConfig::QmConsoleSearchColor;
		Config.m_QmConsoleSearchSelectedColor = DefaultConfig::QmConsoleSearchSelectedColor;
	}
}

#endif
