#ifndef TEST_SUPPORT_PREDICTION_WORLD_H
#define TEST_SUPPORT_PREDICTION_WORLD_H

#include <game/client/prediction/gameworld.h>

// 复制/登记测试不执行碰撞或 Tick，调参使用本地默认值，显式初始化外部借用和配置。
class CPredictionTestWorld : public CGameWorld
{
	CTuningParams m_Tuning;

public:
	CPredictionTestWorld()
	{
		Init(nullptr, &m_Tuning, nullptr);
		m_WorldConfig = {};
		m_LocalClientId = 0;
		m_GameTick = 100;
	}
};

#endif
