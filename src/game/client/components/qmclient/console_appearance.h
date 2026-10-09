#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_APPEARANCE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_APPEARANCE_H

#include <base/color.h>

#include <engine/shared/config.h>

#include <algorithm>
#include <array>

namespace QmConsoleAppearance
{
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
	};

	inline ColorRGBA Blend(ColorRGBA First, ColorRGBA Second, float Amount)
	{
		return {First.r + (Second.r - First.r) * Amount, First.g + (Second.g - First.g) * Amount,
			First.b + (Second.b - First.b) * Amount, First.a};
	}

	inline SPalette ResolvePalette(int Scheme, int Opacity, const std::array<unsigned, COLOR_COUNT> &Custom)
	{
		SPalette Result{{ColorRGBA(0.047f, 0.047f, 0.047f, 1.0f), ColorRGBA(0.80f, 0.80f, 0.80f, 1.0f),
					ColorRGBA(0.976f, 0.945f, 0.647f, 1.0f), ColorRGBA(0.38f, 0.84f, 0.84f, 1.0f),
					ColorRGBA(0.647f, 0.839f, 0.612f, 1.0f), ColorRGBA(0.839f, 0.647f, 0.898f, 1.0f),
					ColorRGBA(0.424f, 0.714f, 1.0f, 1.0f), ColorRGBA(0.957f, 0.529f, 0.443f, 1.0f),
					ColorRGBA(0.976f, 0.945f, 0.647f, 1.0f)},
			{}, {}, {}};
		if(Scheme == 1)
		{
			Result.m_aColors[BACKGROUND] = ColorRGBA(0.004f, 0.141f, 0.337f, 1.0f);
			Result.m_aColors[TEXT] = ColorRGBA(0.94f, 0.94f, 0.94f, 1.0f);
		}
		else if(Scheme == 2)
		{
			for(size_t i = 0; i < Custom.size(); ++i)
				Result.m_aColors[i] = color_cast<ColorRGBA>(ColorHSLA(Custom[i]));
		}
		Result.m_aColors[BACKGROUND].a = std::clamp(Opacity, 20, 100) / 100.0f;
		Result.m_Panel = Blend(Result.m_aColors[BACKGROUND], Result.m_aColors[TEXT], 0.06f);
		Result.m_SelectedButton = Blend(Result.m_Panel, Result.m_aColors[COMMAND], 0.22f);
		Result.m_MutedText = Blend(Result.m_aColors[TEXT], Result.m_aColors[BACKGROUND], 0.25f);
		return Result;
	}

	inline SPalette Palette(const CConfig &Config)
	{
		return ResolvePalette(Config.m_QmConsoleColorScheme, Config.m_QmConsoleOpacity,
			{Config.m_QmConsoleBackgroundColor, Config.m_QmConsoleTextColor, Config.m_QmConsoleCommandColor,
				Config.m_QmConsoleParameterColor, Config.m_QmConsoleStringColor, Config.m_QmConsoleNumberColor,
				Config.m_QmConsoleLinkColor, Config.m_QmConsoleSearchColor, Config.m_QmConsoleSearchSelectedColor});
	}

	inline float FontSize(int ConfiguredSize)
	{
		return static_cast<float>(std::clamp(ConfiguredSize, 8, 24));
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
