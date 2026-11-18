/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H
#define GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H

#include <game/client/laser_data.h>
#include <game/client/prediction/entity.h>
#include <game/teamscore.h>

// 与客户端快照的 Inactive 判定保持一致：只有开关层、正编号、编号在范围内
// 且目标队伍关闭时才算关闭；编号越界的机关按开启处理。
inline bool QmPracticeSwitchActive(bool IsSwitchLayer, int Number, int Team, int NumSwitchers, bool SwitchStatus)
{
	if(!IsSwitchLayer || Number <= 0)
		return true;
	if(Number >= NumSwitchers)
		return true;
	return Team >= 0 && Team < NUM_DDRACE_TEAMS && SwitchStatus;
}

// 门的激光端点取碰撞长度，而不是快照里可能被 no-laser tile 截短的可视端点。
// 长度尚未建立时退回快照端点，避免实体初始化间隙画出零长度门。
inline vec2 QmPracticeDoorRenderEnd(vec2 Pos, vec2 SnapshotEnd, vec2 Direction, int Length)
{
	if(Length <= 0 || length(Direction) <= 0.0f)
		return SnapshotEnd;
	return Pos + Direction * (float)Length;
}

// 关门在快照里是收缩到 hub 的激光头，而不是整只消失；练习分支照此折叠。
inline vec2 QmPracticeDoorRenderEnd(vec2 Pos, vec2 SnapshotEnd, vec2 Direction, int Length, bool Active)
{
	if(!Active)
		return Pos;
	return QmPracticeDoorRenderEnd(Pos, SnapshotEnd, Direction, Length);
}

// 分支机关与服务器快照的 hub 去重容差，与服务端端点取整后的误差一致。
inline bool QmPracticeLaserHubMatches(vec2 SnapshotHub, vec2 PracticeHub)
{
	return distance(SnapshotHub, PracticeHub) < 2.0f;
}

class CDoor : public CEntity
{
	vec2 m_To;
	vec2 m_Direction;
	int m_Length;
	bool m_Active;

	int MapSide() const;
	int MapLength(int Side);

public:
	CDoor(CGameWorld *pGameWorld, int Id, const CLaserData *pData);
	// 副本只复制几何，不接管共享 CCollision 上的门碰撞端点：世界树里
	// 同一扇门同时存在于多个 CGameWorld 副本，只有权威世界负责销毁清理。
	CDoor(const CDoor &Other);
	void ResetCollision();
	bool Match(const CDoor *pDoor) const;
	void Read(const CLaserData *pData);
	// 渲染层数据。几何与分支碰撞端点一致；开关关闭时折叠到 hub，与快照一致。
	CLaserData RenderData(int Team);

	void Destroy() override;
};

#endif // GAME_CLIENT_PREDICTION_ENTITIES_DOOR_H
