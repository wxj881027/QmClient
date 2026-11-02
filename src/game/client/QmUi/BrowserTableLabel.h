#ifndef GAME_CLIENT_QMUI_BROWSERTABLELABEL_H
#define GAME_CLIENT_QMUI_BROWSERTABLELABEL_H

#include <game/client/ui.h>

#include <algorithm>

inline SLabelProperties QmBrowserTableLabelProperties(float Width, bool Ellipsis = true)
{
	// 列宽只决定显示多少文字，保持调用方指定的字号。
	SLabelProperties Props;
	Props.m_MaxWidth = std::max(0.0f, Width);
	Props.m_EnableWidthCheck = false;
	Props.m_StopAtEnd = true;
	Props.m_DisallowNewline = true;
	Props.m_EllipsisAtEnd = Ellipsis;
	return Props;
}

#endif // GAME_CLIENT_QMUI_BROWSERTABLELABEL_H
