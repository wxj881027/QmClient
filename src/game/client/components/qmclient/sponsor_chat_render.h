#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_RENDER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SPONSOR_CHAT_RENDER_H

#include "sponsor_chat_style.h"

#include <engine/textrender.h>

class IGraphics;

void QmSponsorChatAddPlatinumSplits(CTextCursor &Cursor, const char *pText, float Alpha);
void QmRenderSponsorChatText(IGraphics *pGraphics, ITextRender *pTextRender, STextContainerIndex Index, EQmSponsorChatStyle Style, float Alpha, float X, float Y, float FontSize, const SQmSponsorChatSweep &SweepState, double TimeSeconds);

#endif
