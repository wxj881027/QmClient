#ifndef GAME_CLIENT_QMUI_MAPTAGSPANEL_H
#define GAME_CLIENT_QMUI_MAPTAGSPANEL_H

#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <array>
#include <map>
#include <string>

class CGameClient;

class CQmMapTagsPanel
{
	CLineInputBuffered<41, 10> m_Input;
	CButtonContainer m_SubmitButton;
	CButtonContainer m_RefreshButton;
	std::array<CButtonContainer, 4> m_aPresetButtons;
	std::map<std::string, CButtonContainer> m_TagButtons;
	std::string m_Context;

public:
	void Render(CGameClient *pGameClient, CUIRect View);
};

#endif
