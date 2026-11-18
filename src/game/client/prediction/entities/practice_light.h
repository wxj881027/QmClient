#ifndef GAME_CLIENT_PREDICTION_ENTITIES_PRACTICE_LIGHT_H
#define GAME_CLIENT_PREDICTION_ENTITIES_PRACTICE_LIGHT_H

#include <game/client/laser_data.h>
#include <game/client/prediction/entity.h>

#include <optional>

// 快速练习分支的静态激光机关。服务器端 CLight 只存在于 map/game 世界，
// 网络快照裁剪后客户端必须按同一套几何与开关规则继续模拟。
class CPracticeLight : public CEntity
{
	float m_Rotation;
	vec2 m_To;
	vec2 m_Core;
	int m_EvalTick;
	int m_Tick;

	bool HitCharacter();
	void Move();
	void Step();

public:
	int m_CurveLength = 0;
	int m_LengthL = 0;
	float m_AngularSpeed = 0.0f;
	int m_Speed = 0;
	int m_Length = 0;

	CPracticeLight(CGameWorld *pGameWorld, vec2 Pos, float Rotation, int Length, int Layer = 0, int Number = 0);
	CPracticeLight(const CPracticeLight &Other) = default;

	// 从快照继承的同一束激光恢复当前旋转与伸缩相位：练习入口不是地图加载时刻，
	// 直接套地图初始角度会和服务器画面错开一截。返回是否命中有效的快照端点。
	bool InitFromSnapshot(vec2 SnapshotHub, vec2 SnapshotFrom, int SnapshotEvalTick);
	// 配置完角度/长度/速度后立即重算碰撞端点，避免首帧停在零长度或旧角度上。
	void RefreshGeometry();
	// 渲染层数据。开关关闭时返回空，网络裁剪外也能画出与分支碰撞一致的激光。
	std::optional<CLaserData> RenderData(int Team);

	void Tick() override;
};

#endif
