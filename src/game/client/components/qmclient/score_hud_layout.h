#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCORE_HUD_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCORE_HUD_LAYOUT_H

#include <game/client/ui_rect.h>

#include <algorithm>

struct SQmScoreHudLayout
{
	float m_BoxLeft;
	float m_BoxWidth;
	float m_RankX;
	float m_TeeX;
};

// 团队比分条与 HUD 编辑器选框共用同一份矩形：分数 + 旗帜/图标占位 + 两侧间距，
// 右缘固定贴住传入的 Right，内容变宽只向左伸缩。
inline CUIRect QmScoreHudTeamBoxRect(float Right, float Top, float ScoreWidth, float ImageSize, float Height)
{
	constexpr float Split = 3.0f;
	const float BoxWidth = ScoreWidth + ImageSize + 2.0f * Split;
	return {Right - BoxWidth, Top, BoxWidth, Height};
}

inline SQmScoreHudLayout QmScoreHudLayout(float Right, float ScoreWidth, float RankTextWidth, float TeeSize)
{
	constexpr float Split = 3.0f;
	constexpr float ImageSize = 16.0f;
	// 保留原最小宽度，并补足人物绘制尺寸与图标占位的差值和文字间距。
	const float PosSize = std::max(16.0f, RankTextWidth + Split + TeeSize - ImageSize);
	const float BoxWidth = ScoreWidth + ImageSize + 2.0f * Split + PosSize;
	const float BoxLeft = Right - BoxWidth;
	return {BoxLeft, BoxWidth, BoxLeft + Split, Right - ScoreWidth - TeeSize / 2.0f - Split};
}

#endif
