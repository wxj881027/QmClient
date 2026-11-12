#ifndef GAME_CLIENT_QMUI_QMAUTHORTEE_H
#define GAME_CLIENT_QMUI_QMAUTHORTEE_H

#include <base/vmath.h>

namespace QmSponsorAuthors
{
	struct STeePresentation;
}

class CAnimState;
class CRenderTools;
class CTeeRenderInfo;
class CUi;
class CUIRect;

namespace QmAuthorTee
{
	void Render(CUi *pUi, CRenderTools *pRenderTools, const CAnimState *pAnim, const CTeeRenderInfo &SkinInfo, int Emote, bool Cute, vec2 Position, const CUIRect &Bounds, const QmSponsorAuthors::STeePresentation &Presentation);
}

#endif
