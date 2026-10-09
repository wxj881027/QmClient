#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SYNTAX_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SYNTAX_H

#include "console_appearance.h"

#include <base/str.h>

#include <engine/textrender.h>

#include <string_view>
#include <vector>

namespace QmConsoleSyntax
{
	inline bool IsNumber(std::string_view Token)
	{
		size_t Position = 0;
		if(!Token.empty() && (Token[0] == '+' || Token[0] == '-'))
			++Position;
		bool HasDigit = false;
		while(Position < Token.size() && Token[Position] >= '0' && Token[Position] <= '9')
		{
			HasDigit = true;
			++Position;
		}
		if(Position < Token.size() && Token[Position] == '.')
		{
			++Position;
			while(Position < Token.size() && Token[Position] >= '0' && Token[Position] <= '9')
			{
				HasDigit = true;
				++Position;
			}
		}
		if(!HasDigit)
			return false;
		if(Position < Token.size() && (Token[Position] == 'e' || Token[Position] == 'E'))
		{
			++Position;
			if(Position < Token.size() && (Token[Position] == '+' || Token[Position] == '-'))
				++Position;
			const size_t ExponentStart = Position;
			while(Position < Token.size() && Token[Position] >= '0' && Token[Position] <= '9')
				++Position;
			if(Position == ExponentStart)
				return false;
		}
		return Position == Token.size();
	}

	// 只生成显示颜色，不参与命令解析或执行；位置始终按 UTF-8 字节计算。
	inline void AppendColors(const char *pText, const QmConsoleAppearance::SPalette &Palette, std::vector<STextColorSplit> &vColors, int Offset = 0)
	{
		const std::string_view Text(pText);
		bool Command = true;
		size_t Position = 0;
		while(Position < Text.size())
		{
			if(str_isspace(Text[Position]))
			{
				++Position;
				continue;
			}
			const size_t Start = Position;
			ColorRGBA Color;
			if(Text[Position] == '#')
			{
				Position = Text.size();
				Color = Palette.m_MutedText;
			}
			else if(Text[Position] == ';')
			{
				++Position;
				Color = Palette.m_MutedText;
				Command = true;
			}
			else if(Text[Position] == '"')
			{
				++Position;
				while(Position < Text.size())
				{
					if(Text[Position] == '\\' && Position + 1 < Text.size())
						Position += 2;
					else if(Text[Position++] == '"')
						break;
				}
				Color = Palette.m_aColors[QmConsoleAppearance::STRING];
				Command = false;
			}
			else
			{
				while(Position < Text.size() && !str_isspace(Text[Position]) && Text[Position] != ';' && Text[Position] != '#' && Text[Position] != '"')
					++Position;
				const auto Kind = Command ? QmConsoleAppearance::COMMAND :
							    (IsNumber(Text.substr(Start, Position - Start)) ? QmConsoleAppearance::NUMBER : QmConsoleAppearance::PARAMETER);
				Color = Palette.m_aColors[Kind];
				Command = false;
			}
			vColors.emplace_back(Offset + static_cast<int>(Start), static_cast<int>(Position - Start), Color);
		}
	}
}

#endif
