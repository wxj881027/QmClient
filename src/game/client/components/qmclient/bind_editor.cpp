#include "bind_editor.h"

#include <engine/console.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <utility>

namespace qm_bind_editor
{
	namespace
	{
		bool IsWhitespace(const char Character)
		{
			return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
		}

		std::string NormalizeSegment(std::string Segment)
		{
			const auto First = Segment.find_first_not_of(" \t\r\n");
			if(First == std::string::npos)
				return {};
			Segment.erase(0, First);
			return Segment;
		}

		void AddSegment(std::string Segment, SCommands &Result)
		{
			Segment = NormalizeSegment(std::move(Segment));
			if(!Segment.empty())
				Result.m_vCommands.push_back(std::move(Segment));
		}
	}

	std::vector<SRegisteredCommand> RegisteredCommands(IConsole &Console)
	{
		std::vector<SRegisteredCommand> vCommands;
		for(const auto *pInfo = Console.FirstCommandInfo(IConsole::CLIENT_ID_UNSPECIFIED, CFGFLAG_CLIENT);
			pInfo != nullptr;
			pInfo = Console.NextCommandInfo(pInfo, IConsole::CLIENT_ID_UNSPECIFIED, CFGFLAG_CLIENT))
		{
			// 远程控制台的临时命令不属于本地绑定目录；复制内容避免保留注册表指针。
			if(Console.GetCommandInfo(pInfo->Name(), CFGFLAG_CLIENT, false) == pInfo)
				vCommands.push_back({pInfo->Name(), pInfo->Params(), pInfo->Help()});
		}
		return vCommands;
	}

	std::vector<SParameter> ParseParameters(const std::string_view Format)
	{
		std::vector<SParameter> vParameters;
		bool Optional = false;
		for(size_t Index = 0; Index < Format.size();)
		{
			const char Type = Format[Index++];
			if(IsWhitespace(Type))
				continue;
			if(Type == '?')
			{
				Optional = true;
				continue;
			}
			std::string Name(1, Type);
			if(Index < Format.size() && Format[Index] == '[')
			{
				const size_t End = Format.find(']', Index + 1);
				if(End == std::string_view::npos)
					break;
				Name = Format.substr(Index + 1, End - Index - 1);
				Index = End + 1;
			}
			vParameters.push_back({Type, std::move(Name), Optional});
		}
		return vParameters;
	}

	bool ComposeCommand(const std::string_view Name, const std::vector<SParameter> &vParameters, const std::vector<std::optional<std::string>> &vArguments, std::string &Result)
	{
		if(Name.empty() || Name.find_first_of(" \t\r\n;#\"\\") != std::string_view::npos || Name.find('\0') != std::string_view::npos || vParameters.size() != vArguments.size())
			return false;
		std::string Command(Name);
		bool Omitted = false;
		for(size_t Index = 0; Index < vParameters.size(); ++Index)
		{
			if(!vArguments[Index].has_value())
			{
				if(!vParameters[Index].m_Optional)
					return false;
				Omitted = true;
				continue;
			}
			const std::string &Argument = *vArguments[Index];
			if(Omitted || Argument.find_first_of("\r\n") != std::string::npos || Argument.find('\0') != std::string::npos)
				return false;
			Command.push_back(' ');
			Command += QuoteArgument(Argument);
		}
		Result = std::move(Command);
		return true;
	}

	SCommands SplitCommands(const std::string_view Text)
	{
		SCommands Result;
		std::string Segment;
		Segment.reserve(Text.size());
		bool InQuotes = false;
		bool EscapeNext = false;
		for(size_t Index = 0; Index < Text.size(); ++Index)
		{
			const char Character = Text[Index];
			if(EscapeNext)
			{
				Segment.push_back(Character);
				EscapeNext = false;
				continue;
			}
			if(Character == '\\')
			{
				Segment.push_back(Character);
				EscapeNext = true;
				continue;
			}
			if(Character == '"')
			{
				InQuotes = !InQuotes;
				Segment.push_back(Character);
				continue;
			}
			if(!InQuotes && Character == ';')
			{
				AddSegment(std::move(Segment), Result);
				Segment.clear();
				continue;
			}
			if(!InQuotes && Character == '#' && (Segment.empty() || std::all_of(Segment.begin(), Segment.end(), IsWhitespace)))
			{
				// 控制台注释从当前命令边界开始生效，后续内容不会成为绑定动作。
				break;
			}
			if(!InQuotes && Character == '#' && !Segment.empty())
			{
				// 行内注释结束当前动作；保留动作本身的内容。
				AddSegment(std::move(Segment), Result);
				break;
			}
			Segment.push_back(Character);
		}

		if(InQuotes || EscapeNext)
		{
			Result.m_Complete = false;
			return Result;
		}
		AddSegment(std::move(Segment), Result);
		return Result;
	}

	std::string JoinCommands(const std::vector<std::string> &vCommands)
	{
		std::string Result;
		for(const std::string &Command : vCommands)
		{
			if(Command.empty())
				continue;
			if(!Result.empty())
				Result += "; ";
			Result += Command;
		}
		return Result;
	}

	std::string QuoteArgument(const std::string_view Text)
	{
		std::string Result;
		Result.reserve(Text.size() + 2);
		Result.push_back('"');
		for(const char Character : Text)
		{
			if(Character == '"' || Character == '\\')
				Result.push_back('\\');
			Result.push_back(Character);
		}
		Result.push_back('"');
		return Result;
	}

	std::string BindCommand(const std::string_view KeyName, const std::string_view Command)
	{
		std::string Result = "bind ";
		Result.append(KeyName);
		Result.push_back(' ');
		Result += QuoteArgument(Command);
		return Result;
	}

	bool FitsConfigLine(const std::string_view KeyName, const std::string_view Command)
	{
		if(KeyName.empty() || Command.find('\0') != std::string_view::npos)
			return false;
		for(const char Character : Command)
		{
			if(Character == '\n' || Character == '\r')
				return false;
		}
		return BindCommand(KeyName, Command).size() <= MAX_CONFIG_LINE_BYTES;
	}

	bool AppendCommand(const std::string_view KeyName, const std::string_view Existing, const std::string_view Next, std::string &Result)
	{
		const SCommands Current = SplitCommands(Existing);
		const SCommands Added = SplitCommands(Next);
		if(!Current.m_Complete || !Added.m_Complete || Added.m_vCommands.empty())
			return false;

		std::vector<std::string> vCommands = Current.m_vCommands;
		vCommands.insert(vCommands.end(), Added.m_vCommands.begin(), Added.m_vCommands.end());
		const std::string Combined = JoinCommands(vCommands);
		if(!FitsConfigLine(KeyName, Combined))
			return false;
		Result = Combined;
		return true;
	}
}
