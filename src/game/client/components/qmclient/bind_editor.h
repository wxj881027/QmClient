#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_EDITOR_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_EDITOR_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class IConsole;

namespace qm_bind_editor
{
	// 配置中的整条 bind（含键名、引号与转义）必须能被本地控制台完整读回。
	inline constexpr size_t MAX_CONFIG_LINE_BYTES = 16384;
	inline constexpr size_t INPUT_CAPACITY = MAX_CONFIG_LINE_BYTES * 2 + 1;

	struct SCommands
	{
		std::vector<std::string> m_vCommands;
		bool m_Complete = true;
	};

	struct SRegisteredCommand
	{
		std::string m_Name;
		std::string m_Parameters;
		std::string m_Help;
	};

	struct SParameter
	{
		char m_Type;
		std::string m_Name;
		bool m_Optional;
	};

	std::vector<SRegisteredCommand> RegisteredCommands(IConsole &Console);
	std::vector<SParameter> ParseParameters(std::string_view Format);
	bool ComposeCommand(std::string_view Name, const std::vector<SParameter> &vParameters, const std::vector<std::optional<std::string>> &vArguments, std::string &Result);
	SCommands SplitCommands(std::string_view Text);
	std::string JoinCommands(const std::vector<std::string> &vCommands);
	std::string QuoteArgument(std::string_view Text);
	std::string BindCommand(std::string_view KeyName, std::string_view Command);
	bool FitsConfigLine(std::string_view KeyName, std::string_view Command);
	bool AppendCommand(std::string_view KeyName, std::string_view Existing, std::string_view Next, std::string &Result);
}

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_EDITOR_H
