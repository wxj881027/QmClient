#ifndef TEST_SUPPORT_PREDICTION_WORLD_H
#define TEST_SUPPORT_PREDICTION_WORLD_H

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>

// 调参使用本地默认值，显式初始化外部借用和配置；执行几何路径前由调用者提供碰撞对象。
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

// 角色核心对外只有 const 视图，改初始状态必须走 GetCore/SetCore 的副本往返。
template<typename Fn>
inline void EditCharacterCore(CCharacter *pCharacter, Fn &&Edit)
{
	CCharacterCore Core = pCharacter->GetCore();
	Edit(Core);
	pCharacter->SetCore(Core);
}

#endif
