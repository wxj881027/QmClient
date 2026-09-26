#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H

#include <algorithm>

struct SQmBrowserNameMapLayout
{
	float m_RightScale;
	float m_NameWidth;
	float m_MapWidth;
};

// 固定列先让出名称/地图的最低空间；空间不足时所有宽度保持非负。
inline SQmBrowserNameMapLayout QmBrowserNameMapLayout(float Width, float RightWidth, float MinNameWidth, float MinMapWidth, float NameShare)
{
	Width = std::max(0.0f, Width);
	RightWidth = std::max(0.0f, RightWidth);
	MinNameWidth = std::max(0.0f, MinNameWidth);
	MinMapWidth = std::max(0.0f, MinMapWidth);
	const float Minimum = MinNameWidth + MinMapWidth;
	const float Reserved = std::min(Width, Minimum);
	const float RightScale = RightWidth > 0.0f ? std::clamp((Width - Reserved) / RightWidth, 0.0f, 1.0f) : 1.0f;
	const float NameMapWidth = std::max(0.0f, Width - RightWidth * RightScale);
	const float Split = std::clamp(NameShare, 0.35f, 0.75f);
	const float NameWidth = NameMapWidth >= Minimum ?
					MinNameWidth + (NameMapWidth - Minimum) * Split :
				Minimum > 0.0f ? NameMapWidth * MinNameWidth / Minimum :
						 NameMapWidth * Split;
	return {RightScale, NameWidth, NameMapWidth - NameWidth};
}

#endif
