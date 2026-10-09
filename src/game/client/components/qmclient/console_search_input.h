#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SEARCH_INPUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CONSOLE_SEARCH_INPUT_H

#include <optional>
#include <string>
#include <utility>

// 搜索和命令共用输入行；草稿存在即处于搜索模式，避免维护两份模式状态。
class CQmConsoleSearchInput
{
	std::optional<std::string> m_CommandDraft;

public:
	bool IsSearching() const { return m_CommandDraft.has_value(); }

	bool Begin(const char *pCommand)
	{
		if(IsSearching())
			return false;
		m_CommandDraft = pCommand;
		return true;
	}

	std::optional<std::string> End()
	{
		auto Draft = std::move(m_CommandDraft);
		m_CommandDraft.reset();
		return Draft;
	}
};

#endif
