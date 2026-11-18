/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_DRAGGER_H
#define GAME_CLIENT_PREDICTION_ENTITIES_DRAGGER_H

#include <game/client/laser_data.h>
#include <game/client/prediction/entity.h>

#include <algorithm>
#include <optional>

// 与服务器 CDragger::Snap 的 subtype 编码一致：低一位是穿墙，
// 剩余位以 0..2 表示 1..3 级强度。
inline int QmPracticeDraggerSubtype(float Strength, bool IgnoreWalls)
{
	const int StrengthSteps = std::clamp(round_to_int(Strength - 1.0f), 0, 2);
	return (IgnoreWalls ? 1 : 0) | (StrengthSteps << 1);
}

class CDragger : public CEntity
{
	vec2 m_Core;
	float m_Strength;
	bool m_IgnoreWalls;
	int m_TargetId;

	void LookForPlayersToDrag();
	void DraggerBeamTick();
	void DraggerBeamReset();

public:
	CDragger(CGameWorld *pGameWorld, int Id, const CLaserData *pData);
	bool Match(CDragger *pDragger);
	void Read(const CLaserData *pData);
	float GetStrength() const { return m_Strength; }
	// 渲染层数据。有分支目标时从轮毂画向目标，否则保留零长度轮毂。
	std::optional<CLaserData> RenderData(int Team);

	void Tick() override;
};

#endif // GAME_CLIENT_PREDICTION_ENTITIES_DRAGGER_H
