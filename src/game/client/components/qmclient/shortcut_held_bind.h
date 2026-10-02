#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SHORTCUT_HELD_BIND_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SHORTCUT_HELD_BIND_H

#include <game/client/components/binds_deepfly_mode.h>

#include <string>

// 修饰键介入后只保留已按住的展示面板，其他命令释放一次；重复按键不能重新打开轮盘。
class CQmShortcutHeldBind
{
	bool m_Restricted = false;
	bool m_ReleaseOnFocusReturn = false;
	std::string m_HeldPanelCommands;

public:
	// 失焦时 SDL 的释放事件结束真实输入，但展示面板延迟到焦点返回再收起，供系统截图捕获。
	// 只保留 Restrict 筛出的面板命令，移动、开火、轮盘仍立即释放。
	template<typename F>
	bool ReleaseWhileUnfocused(const char *pOriginal, F &&Release)
	{
		m_ReleaseOnFocusReturn = Restrict(pOriginal, Release);
		return m_ReleaseOnFocusReturn;
	}
	bool ReleaseOnFocusReturn(bool WindowActive) const { return WindowActive && m_ReleaseOnFocusReturn; }
	bool WaitingForFocusReturn() const { return m_ReleaseOnFocusReturn; }

	bool Restricted() const { return m_Restricted; }
	const char *Command(const char *pOriginal) const { return m_Restricted ? m_HeldPanelCommands.c_str() : pOriginal; }

	template<typename F>
	bool Restrict(const char *pOriginal, F &&Release)
	{
		if(m_Restricted)
			return !m_HeldPanelCommands.empty();
		// 回调可能改写或删除绑定，先保留待解析的命令。
		const std::string Original = pOriginal != nullptr ? pOriginal : "";
		const auto IsHeldPanel = [](const char *pCommand) {
			return str_comp_nocase(pCommand, "+scoreboard") == 0 ||
			       str_comp_nocase(pCommand, "+spectate") == 0 ||
			       str_comp_nocase(pCommand, "+statboard") == 0;
		};
		ForEachTopLevelBindCommand(Original.c_str(), [&](const char *pCommand) {
			if(IsHeldPanel(pCommand))
			{
				if(!m_HeldPanelCommands.empty())
					m_HeldPanelCommands += ';';
				m_HeldPanelCommands += pCommand;
			}
		});
		m_Restricted = true;
		ForEachTopLevelBindCommand(Original.c_str(), [&](const char *pCommand) {
			if(pCommand[0] == '+' && !IsHeldPanel(pCommand))
				Release(pCommand);
		});
		return !m_HeldPanelCommands.empty();
	}
};

#endif
