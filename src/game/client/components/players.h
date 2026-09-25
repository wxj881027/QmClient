/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_PLAYERS_H
#define GAME_CLIENT_COMPONENTS_PLAYERS_H
#include <engine/graphics.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/qm_hook_coll_visibility.h>
#include <game/client/components/qmclient/weapon_animation.h>
#include <game/client/render.h>

#include <memory>
#include <vector>

// 每次钩子提示线绘制独占使用，跨玩家与小窗调用只保留容量，不保留几何。
struct SQmHookCollLineScratch
{
	std::vector<IGraphics::CLineItem> m_vLineSegments;
	std::vector<IGraphics::CFreeformItem> m_vLineQuadSegments;

	void Reset()
	{
		m_vLineSegments.clear();
		m_vLineQuadSegments.clear();
	}

	void AppendQuad(const IGraphics::CLineItem &LineSegment, const vec2 &PerpToAngle, float LineWidth)
	{
		vec2 DrawInitPos(LineSegment.m_X0, LineSegment.m_Y0);
		vec2 DrawFinishPos(LineSegment.m_X1, LineSegment.m_Y1);
		vec2 Pos0 = DrawFinishPos + PerpToAngle * -LineWidth;
		vec2 Pos1 = DrawFinishPos + PerpToAngle * LineWidth;
		vec2 Pos2 = DrawInitPos + PerpToAngle * -LineWidth;
		vec2 Pos3 = DrawInitPos + PerpToAngle * LineWidth;
		m_vLineQuadSegments.emplace_back(Pos0.x, Pos0.y, Pos1.x, Pos1.y, Pos2.x, Pos2.y, Pos3.x, Pos3.y);
	}
};

class CPlayers : public CComponent
{
	friend class CGhost;

	void RenderHand6(const CTeeRenderInfo *pInfo, vec2 HandPos, float HandAngle, float Alpha);
	void RenderHand7(const CTeeRenderInfo *pInfo, vec2 HandPos, float HandAngle, float Alpha);

	void RenderHand(const CTeeRenderInfo *pInfo, vec2 CenterPos, vec2 Dir, float AngleOffset, vec2 PostRotOffset, float Alpha);
	void RenderPlayer(
		const CScreenRect &ScreenRect,
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		const CTeeRenderInfo *pRenderInfo,
		int ClientId,
		float Intra = 0.f);
	void RenderPlayerGhost(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		const CTeeRenderInfo *pRenderInfo,
		int ClientId,
		float Intra = 0.f);

	void RenderHook(
		const CScreenRect &ScreenRect,
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		const CTeeRenderInfo *pRenderInfo,
		int ClientId,
		float Intra = 0.f);
	void RenderHookCollLine(
		const CScreenRect &ScreenRect,
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		int ClientId);
	void RenderWeaponTrajectory(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		int ClientId);
	bool IsPlayerInfoAvailable(int ClientId) const;
	bool ShouldRenderWeaponAnimation(int ClientId) const;

	SQmHookCollLineScratch m_HookCollLineScratch;
	CQmHookCollVisibility m_HookCollVisibility;
	int m_WeaponEmoteQuadContainerIndex;
	int m_aWeaponSpriteMuzzleQuadContainerIndex[NUM_WEAPONS];
	int m_aWeaponSwitchLastWeapons[MAX_CLIENTS];
	double m_aWeaponSwitchStartTimes[MAX_CLIENTS];
	SQmWeaponReloadAnimationState m_aWeaponReloadAnimationStates[MAX_CLIENTS];

	void CreateNinjaTeeRenderInfo();

	std::shared_ptr<CManagedTeeRenderInfo> m_pNinjaTeeRenderInfo;

	// 钩子辅助线在拥挤服务器上对每个可见玩家各绘制一次；复用暂存缓冲
	// 避免每名玩家每帧重新分配线段与四边形。

public:
	float GetPlayerTargetAngle(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		int ClientId,
		float Intra = 0.0f);

	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnReset() override;
	void OnInit() override;
	void OnRender() override;

	const std::shared_ptr<CManagedTeeRenderInfo> &NinjaTeeRenderInfo() const { return m_pNinjaTeeRenderInfo; }
};

#endif
