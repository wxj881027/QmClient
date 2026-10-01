#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_UI_POPUP_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_UI_POPUP_H

#include <game/client/QmUi/SecondaryPanel.h>

class CGameClient;

// 翻译二级面板拥有自己的控件与文本缓存，可从聊天或设置入口打开。
class CTranslateSettingsPopup : public SPopupMenuId
{
	CUi *m_pUi = nullptr;
	CGameClient *m_pGameClient = nullptr;
	CButtonContainer m_CloseButton;
	ui_widget::SSecondaryPanelLabel m_Title, m_InboundToggle, m_OutboundToggle, m_InboundLabel, m_OutboundLabel, m_BackendLabel, m_Warning;
	CUi::SDropDownState m_InboundDropdown, m_OutboundDropdown, m_BackendDropdown;
	static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);

public:
	void Open(CUi *pUi, vec2 BottomRight, CGameClient *pGameClient = nullptr);
};

#endif
