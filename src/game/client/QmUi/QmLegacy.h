/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_QM_UI_QM_LEGACY_H
#define GAME_CLIENT_QM_UI_QM_LEGACY_H

#include "../ui_rect.h"
#include "QmLayout.h"

class CUiV2LegacyAdapter
{
public:
	static CUIRect ToCUIRect(const SUiLayoutBox &Box);
	static SUiLayoutBox FromCUIRect(const CUIRect &Rect);
};

#endif
