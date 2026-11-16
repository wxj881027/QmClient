#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_STATE_HUD_BOUNDS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_PLAYER_STATE_HUD_BOUNDS_H

#include <game/client/ui_rect.h>

#include <algorithm>

// 只累计本帧实际绘制的玩家状态内容，不把预留行和独立媒体面板算入编辑框。
class CQmPlayerStateHudBounds
{
	CUIRect m_Rect{};

public:
	void AddRect(const CUIRect &Rect)
	{
		if(Rect.w <= 0.0f || Rect.h <= 0.0f)
			return;
		if(m_Rect.w <= 0.0f || m_Rect.h <= 0.0f)
		{
			m_Rect = Rect;
			return;
		}
		const float Right = std::max(m_Rect.x + m_Rect.w, Rect.x + Rect.w);
		const float Bottom = std::max(m_Rect.y + m_Rect.h, Rect.y + Rect.h);
		m_Rect.x = std::min(m_Rect.x, Rect.x);
		m_Rect.y = std::min(m_Rect.y, Rect.y);
		m_Rect.w = Right - m_Rect.x;
		m_Rect.h = Bottom - m_Rect.y;
	}

	void AddWeapon(float CenterX, float CenterY, vec2 Size)
	{
		// 武器绕中心旋转 -45 度，横纵包围长度均为 (宽 + 高) / sqrt(2)。
		constexpr float DiagonalScale = 0.7071067811865475f;
		const float Extent = (Size.x + Size.y) * DiagonalScale;
		AddRect({CenterX - Extent * 0.5f, CenterY - Extent * 0.5f, Extent, Extent});
	}

	CUIRect Rect() const
	{
		if(m_Rect.w <= 0.0f || m_Rect.h <= 0.0f)
			return {};
		constexpr float Padding = 2.0f;
		return {m_Rect.x - Padding, m_Rect.y - Padding, m_Rect.w + Padding * 2.0f, m_Rect.h + Padding * 2.0f};
	}
};

#endif
