#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MENU_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BIND_MENU_PREVIEW_H

#include "bind_menu_layout.h"
#include "wheel_presets.h"

#include <string>

class CGameClient;
class CUi;

namespace qm_bind_menu_preview
{
	struct SOption
	{
		const char *m_pName = "";
		const char *m_pCommand = "";
		const qm_wheel_presets::SPreset *m_pPreset = nullptr;
		bool m_LegacyScript = false;
	};

	void RenderOption(CUi *pUi, CGameClient *pGameClient, const CUIRect &Rect, qm_bind_menu::EMenu Menu,
		const SOption &Option, float UiScale, float Alpha, bool Hovered = false);
	void Render(CUi *pUi, CGameClient *pGameClient, const SLayout &Layout, qm_bind_menu::EMenu Menu,
		const SOption *pOptions, int OptionCount, int Hovered, int Scroll, float UiScale, float Alpha);
	std::string BuildHoverText(qm_bind_menu::EMenu Menu, const SOption &Option, const qm_wheel_presets::SPreset *pPreset);
} // namespace qm_bind_menu_preview

#endif
