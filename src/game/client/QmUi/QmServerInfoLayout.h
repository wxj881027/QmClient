#ifndef GAME_CLIENT_QMUI_QMSERVERINFOLAYOUT_H
#define GAME_CLIENT_QMUI_QMSERVERINFOLAYOUT_H

#include "QmServerInfoGeometry.h"

#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

class CQmServerInfoLayout
{
	char m_aDividerIds[3]{};
	float m_GrabOffset = 0.0f;
	int m_PendingRatio = 500;
	CUIRect m_PreviewLine{};
	CScrollRegion m_aScrollRegions[2];

public:
	QmServerInfoGeometry::SLayout Render(CUi *pUi, CUIRect View, bool ShowTags);
	void RenderOverlay() const;
	void BeginCard(int Index, CUIRect &View, float ContentHeight, bool Collecting);
	void EndCard(int Index, bool Collecting);
};

#endif
