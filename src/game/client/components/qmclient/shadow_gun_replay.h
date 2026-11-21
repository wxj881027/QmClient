#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SHADOW_GUN_REPLAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SHADOW_GUN_REPLAY_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/teamscore.h>

#include <functional>
#include <vector>

class CCollision;
struct SSwitchers;

// 影子回放的机关炮只做画面重放：炮位、冷却和等离子球都由回放播放头驱动，
// 目标来自影子轨迹而不是主世界角色。它不写共享碰撞，也不会勾人、挡人或冻结玩家。
namespace qm_shadow_gun
{
	struct SGun
	{
		vec2 m_StartPos = vec2(0.0f, 0.0f);
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Core = vec2(0.0f, 0.0f);
		bool m_Freeze = false;
		bool m_Explosive = false;
		int m_Layer = 0;
		int m_Number = 0;
		int m_aLastFireTeam[NUM_DDRACE_TEAMS] = {};
	};

	struct SPlasma
	{
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Core = vec2(0.0f, 0.0f);
		int m_LifeTime = 0;
		int m_Subtype = 0;
		int m_SwitchNumber = 0;
	};

	struct STarget
	{
		vec2 m_Pos = vec2(0.0f, 0.0f);
		int m_Id = -1;
		int m_Team = -1;
	};

	using FTargetProvider = std::function<void(int Tick, std::vector<STarget> &vTargets)>;

	// 地图扫描规则与快速练习分支共用：实体层 index 减 ENTITY_OFFSET 即实体编号。
	void Load(CCollision *pCollision, std::vector<SGun> &vGuns);
	// 回到时间线零点，清除冷却与在飞等离子球。
	void Reset(std::vector<SGun> &vGuns, std::vector<SPlasma> &vPlasmas);
	// 推进一个 tick。Tick 必须严格递增；倒退由调用方先 Reset 再重新推进。
	void Tick(std::vector<SGun> &vGuns, std::vector<SPlasma> &vPlasmas, int Tick,
		const std::vector<SSwitchers> &vSwitchers, int ViewTeam, CCollision *pCollision,
		const FTargetProvider &TargetProvider);
}

#endif
