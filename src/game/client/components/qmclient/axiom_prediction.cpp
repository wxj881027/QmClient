#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

// 仅在线客户端启用；公共碰撞类和服务端不读取客户端配置。
void CGameClient::UpdateQmAxiomPrediction()
{
	const bool Enabled = Client()->State() == IClient::STATE_ONLINE && g_Config.m_QmAxiomQuadPrediction;
	m_Collision.SetQmAxiomQuadCollision(Enabled);
	if(!m_Collision.QmAxiomQuadCollisionEnabled())
		return;

	const auto *pInfo = static_cast<const CNetObj_GameInfo *>(Client()->SnapFindItem(IClient::SNAP_CURRENT, NETOBJTYPE_GAMEINFO, 0));
	const int RoundStart = pInfo ? pInfo->m_RoundStartTick : 0;
	m_Collision.SetQmAxiomQuadRoundStartTick(RoundStart);
	const bool Predicted = g_Config.m_ClPredict &&
			       !(m_Snap.m_SpecInfo.m_Active && m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW);
	const double Tick = Predicted ?
				    Client()->PredGameTick(g_Config.m_ClDummy) - 1 + double(Client()->PredIntraGameTick(g_Config.m_ClDummy)) :
				    mix<double>(Client()->PrevGameTick(g_Config.m_ClDummy), Client()->GameTick(g_Config.m_ClDummy), Client()->IntraGameTick(g_Config.m_ClDummy));
	m_Collision.SetQmAxiomQuadTime(pInfo ? (Tick - RoundStart) / Client()->GameTickSpeed() : 0.0);
}

void CCharacter::UpdateQmAxiomHook()
{
	if(!Collision()->QmAxiomQuadCollisionEnabled() || m_Core.m_HookState != HOOK_GRABBED || m_Core.HookedPlayer() != -1)
		m_QmAxiomHookQuad = -1;
	else if(m_QmAxiomHookQuad >= 0)
	{
		if(!Collision()->GetQmAxiomHookAnchor(m_QmAxiomHookQuad, m_QmAxiomHookLocalPos, &m_Core.m_HookPos))
		{
			m_QmAxiomHookQuad = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_HookPos = m_Core.m_Pos;
		}
	}
	else
		Collision()->FindQmAxiomHookAnchor(m_Core.m_HookPos, &m_QmAxiomHookQuad, &m_QmAxiomHookLocalPos);
}

void CCharacter::HandleQmAxiomQuads()
{
	if(!Collision()->QmAxiomQuadCollisionEnabled() || !GameWorld()->m_WorldConfig.m_PredictDDRace)
	{
		m_LastQmAxiomRefillJumps = false;
		return;
	}
	bool Refill = false;
	// 按组、图层和四边形顺序处理全部命中，后续解冻层可以覆盖冻结。
	for(int Tile : Collision()->GetQmAxiomQuadTiles(m_Pos))
	{
		if(Tile == TILE_FREEZE && !m_Core.m_Super && !m_Core.m_Invincible && !m_Core.m_DeepFrozen)
			Freeze();
		else if(Tile == TILE_UNFREEZE && !m_Core.m_DeepFrozen)
			Unfreeze();
		else if(Tile == TILE_LFREEZE && !m_Core.m_Super && !m_Core.m_Invincible)
			m_Core.m_LiveFrozen = true;
		else if(Tile == TILE_LUNFREEZE)
			m_Core.m_LiveFrozen = false;
		else if(Tile == TILE_DFREEZE && !m_Core.m_Super && !m_Core.m_Invincible)
			m_Core.m_DeepFrozen = true;
		else if(Tile == TILE_DUNFREEZE)
			m_Core.m_DeepFrozen = false;
		else if(Tile == TILE_REFILL_JUMPS)
		{
			Refill = true;
			if(!m_LastQmAxiomRefillJumps)
				m_Core.m_JumpedTotal = m_Core.m_Jumped = 0;
		}
		else if(Tile == TILE_TELECHECKINEVIL && !m_Core.m_Super && !m_Core.m_Invincible)
		{
			for(int Checkpoint = m_TeleCheckpoint - 1; Checkpoint >= 0; --Checkpoint)
			{
				const auto &vOuts = Collision()->TeleCheckOuts(Checkpoint);
				if(vOuts.empty())
					continue;
				m_Core.m_Pos = vOuts[GameWorld()->m_Core.RandomOr0(vOuts.size())];
				m_Core.m_Vel = vec2(0, 0);
				if(!g_Config.m_SvTeleportHoldHook)
				{
					ResetHook();
					GameWorld()->ReleaseHooked(GetCid());
				}
				break;
			}
		}
		// QKill 与 QFinish 可供碰撞查询，死亡和过关仍由服务端确认。
	}
	m_LastQmAxiomRefillJumps = Refill;
}
