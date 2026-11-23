/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_PLAYERS_H
#define GAME_CLIENT_COMPONENTS_PLAYERS_H
#include <engine/graphics.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/qm_hook_coll_geometry.h>
#include <game/client/components/qmclient/qm_hook_coll_visibility.h>
#include <game/client/components/qmclient/weapon_animation.h>
#include <game/client/render.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

template<typename TCharacterInfo, std::size_t N>
vec2 QmNinjaMuzzleMotion(const CNetObj_Character &RenderPrev, const CNetObj_Character &RenderCur, int ClientId, bool PredictLocalWeapons, const TCharacterInfo (&aCharacters)[N])
{
	const CNetObj_Character *pPrev = &RenderPrev;
	const CNetObj_Character *pCur = &RenderCur;
	// 回放幽灵没有玩家快照槽，使用回放提供的两帧。
	if(!PredictLocalWeapons && ClientId >= 0 && static_cast<std::size_t>(ClientId) < N)
	{
		pPrev = &aCharacters[ClientId].m_Prev;
		pCur = &aCharacters[ClientId].m_Cur;
	}
	return vec2(pCur->m_X, pCur->m_Y) - vec2(pPrev->m_X, pPrev->m_Y);
}

class CPlayers : public CComponent
{
	friend class CGhost;

	void RenderHand6(const CTeeRenderInfo *pInfo, vec2 HandPos, float HandAngle, float Alpha);
	void RenderHand7(const CTeeRenderInfo *pInfo, vec2 HandPos, float HandAngle, float Alpha);

	void RenderHand(const CTeeRenderInfo *pInfo, vec2 CenterPos, vec2 Dir, float AngleOffset, vec2 PostRotOffset, float Alpha);
	// AttackTimeOverride：查看模式回放的播放头换算出的挥锤/开枪相位（秒）。
	// 传入后不再用实时游戏 tick 推导，暂停、拖动进度时动画随播放头冻结或跳转。
	void RenderPlayer(
		const CScreenRect &ScreenRect,
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		const CTeeRenderInfo *pRenderInfo,
		int ClientId,
		float Intra = 0.f,
		std::optional<float> AttackTimeOverride = std::nullopt,
		bool PredictWeaponsForOverride = false);
	void RenderPlayerGhost(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		const CTeeRenderInfo *pRenderInfo,
		int ClientId,
		float Intra = 0.f,
		std::optional<float> AttackTimeOverride = std::nullopt,
		bool PredictWeaponsForOverride = false);

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
	bool ShouldRenderWeaponAnimation(int ClientId) const;

	SQmHookCollLineScratch m_HookCollLineScratch;
	CQmHookCollVisibility m_HookCollVisibility;
	int m_WeaponEmoteQuadContainerIndex;
	int m_aWeaponSpriteMuzzleQuadContainerIndex[NUM_WEAPONS];
	int m_aWeaponSwitchLastWeapons[MAX_CLIENTS];
	double m_aWeaponSwitchStartTimes[MAX_CLIENTS];
	SQmWeaponReloadAnimationState m_aWeaponReloadAnimationStates[MAX_CLIENTS];

	void CreateNinjaTeeRenderInfo();
	void CreateSpectatorTeeRenderInfo();

	std::shared_ptr<CManagedTeeRenderInfo> m_pNinjaTeeRenderInfo;
	std::shared_ptr<CManagedTeeRenderInfo> m_pSpectatorTeeRenderInfo;

	// 钩子辅助线在拥挤服务器上对每个可见玩家各绘制一次；复用暂存缓冲
	// 避免每名玩家每帧重新分配线段与四边形。

public:
	bool IsPlayerInfoAvailable(int ClientId) const;
	float PlayerRenderAlpha(int ClientId) const;
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
	const std::shared_ptr<CManagedTeeRenderInfo> &SpectatorTeeRenderInfo() const { return m_pSpectatorTeeRenderInfo; }
};

#endif
