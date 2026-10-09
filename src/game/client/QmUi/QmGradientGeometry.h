#ifndef GAME_CLIENT_QMUI_QMGRADIENTGEOMETRY_H
#define GAME_CLIENT_QMUI_QMGRADIENTGEOMETRY_H

#include <array>

// 只绑定配置，不持有页面状态；IME 与聊天复用相同的几何控件。
struct SQmGradientGeometryBinding
{
	int *m_pType;
	int *m_pAngle;
	int *m_pCenterX;
	int *m_pCenterY;
	int *m_pRange;
	int *m_pReverse;

	std::array<int, 6> Values() const
	{
		return {*m_pType, *m_pAngle, *m_pCenterX, *m_pCenterY, *m_pRange, *m_pReverse};
	}

	void Reset() const
	{
		*m_pType = *m_pAngle = *m_pReverse = 0;
		*m_pCenterX = *m_pCenterY = 50;
		*m_pRange = 100;
	}
};

#endif
